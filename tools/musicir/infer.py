"""IR 이 없는 곡에서 잴 수 있는 것만 읽어 IR 꼴로 만든다 (기존 곡 옮기기).

템포, 박자, 트랙, 음역, 밀도, 세기는 곡 JSON 에서 그대로 잰다. 구간의 경계, 트랙의 역할, 조성은 추론이므로
"origin": "inferred" 와 신뢰도(confidence), 근거(basis)를 붙인다. 구간의 뜻, 동기, 화성 기능은 지어내지 않는다.
"""
import os
import re

from . import model

MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def _corr(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    den = (sum((x - ma) ** 2 for x in a) * sum((y - mb) ** 2 for y in b)) ** 0.5
    return num / den if den else 0.0


def key_candidates(data):
    """음 길이로 무게를 준 음높이 클래스 분포와 Krumhansl 조성 윤곽의 상관 (상위 두 개)"""
    hist = [0.0] * 12
    for t in data.get("tracks", []):
        if not model.pitched(t.get("instrument")):
            continue
        for n in t.get("notes", []):
            hist[model.parse_pitch(n["pitch"]) % 12] += float(n["durationBeat"])
    if not any(hist):
        return []
    scores = []
    for tonic in range(12):
        for mode, prof in (("major", MAJOR), ("minor", MINOR)):
            rot = [prof[(i - tonic) % 12] for i in range(12)]
            scores.append((_corr(hist, rot), f"{NAMES[tonic]} {mode}"))
    scores.sort(reverse=True)
    return [{"key": k, "r": round(r, 2)} for r, k in scores[:2]]


def _boundaries(data, bb, bars):
    """4마디 격자에서 앞뒤 네 마디의 활동 트랙과 밀도가 크게 바뀌는 마디"""
    active = [set() for _ in range(bars)]
    count = [0] * bars
    for t in data.get("tracks", []):
        for n in t.get("notes", []):
            k = int(float(n["startBeat"]) // bb)
            if 0 <= k < bars:
                active[k].add(t["name"])
                count[k] += 1
    tempo_bars = set()
    if isinstance(data["tempo"], list):
        tempo_bars = {int(float(p["beat"]) // bb) + 1 for p in data["tempo"] if not p.get("ramp")}
    out = []
    for bar in range(5, bars + 1, 4):
        k = bar - 1
        before = set().union(*active[max(0, k - 4):k])
        after = set().union(*active[k:k + 4])
        if not before and not after:
            continue
        union = before | after
        dist = 1 - len(before & after) / len(union) if union else 0
        nb, na = sum(count[max(0, k - 4):k]) or 1, sum(count[k:k + 4]) or 1
        ratio = na / nb
        if dist >= 0.25 or ratio >= 1.6 or ratio <= 0.6:
            basis = []
            if after - before:
                basis.append("들어옴: " + ", ".join(sorted(after - before)))
            if before - after:
                basis.append("빠짐: " + ", ".join(sorted(before - after)))
            if ratio >= 1.6 or ratio <= 0.6:
                basis.append(f"음표 밀도 x{ratio:.1f}")
            conf = min(0.9, 0.35 + dist + (0.15 if ratio >= 1.6 or ratio <= 0.6 else 0) + (0.1 if bar in tempo_bars else 0))
            out.append((bar, round(conf, 2), "; ".join(basis)))
    return out


def _role(track, stats, highest, lowest):
    """트랙 이름, 악기, 음역, 겹침으로 짐작한 역할 [(역할, 신뢰도, 근거)]"""
    words = set(re.findall(r"[a-z]+", track["name"].lower()))
    inst = track.get("instrument", "")
    fam = model.family(inst)
    if fam == "fx":
        return ["atmosphere"], 0.5, f"악기 {inst}"
    if not model.pitched(inst) or fam == "percussion":
        return ["rhythm"], 0.8, f"악기 {inst}"
    for word, role in (("melody", "main-melody"), ("lead", "main-melody"), ("voice", "main-melody"), ("bass", "bass"), ("basses", "bass"),
                       ("sub", "sub-bass"), ("pad", "pad"), ("arp", "arpeggio"), ("choir", "pad")):
        if word in words:
            return [role], 0.7, f"트랙 이름 '{track['name']}'"
    if stats["poly"] <= 1 and track["name"] == highest:
        return ["main-melody"], 0.5, "가장 높은 홑음 트랙"
    if track["name"] == lowest:
        return ["bass"], 0.5, "가장 낮은 트랙"
    if stats["poly"] >= 3:
        return ["harmony"], 0.4, f"동시에 {stats['poly']}음"
    return ["texture"], 0.2, "근거 약함"


def infer(data, json_path):
    tm = model.TempoMap(data["tempo"])
    bb = model.bar_beats(data)
    end = model.end_beat(data)
    bars = max(1, int(-(-end // bb)))
    cuts = _boundaries(data, bb, bars)
    starts = [1] + [c[0] for c in cuts]
    sections = []
    for i, first in enumerate(starts):
        last = (starts[i + 1] - 1) if i + 1 < len(starts) else bars
        b0, b1 = (first - 1) * bb, last * bb
        s = {"id": f"s{i + 1}", "name": f"구간 {i + 1}", "bars": [first, last], "beats": [b0, b1],
             "seconds": [round(tm.seconds(b0), 2), round(tm.seconds(b1), 2)], "origin": "inferred"}
        if i > 0:
            s["confidence"], s["basis"] = cuts[i - 1][1], cuts[i - 1][2]
        sections.append(s)
    items = [(s["id"], s["beats"][0], s["beats"][1]) for s in sections]
    names = {t["name"]: t["name"] for t in data.get("tracks", [])}
    ana = model.analyze(data, items, names)
    whole = {}
    for t in data.get("tracks", []):
        whole[t["name"]] = model.cell_stats(t.get("notes", []), [], bars)
    melodic = [t for t in data.get("tracks", []) if model.pitched(t.get("instrument")) and whole[t["name"]]["n"]]
    mean = {t["name"]: sum(model.parse_pitch(n["pitch"]) for n in t["notes"]) / len(t["notes"]) for t in melodic}
    highest = max(mean, key=mean.get) if mean else None
    lowest = min(mean, key=mean.get) if mean else None
    tracks = []
    for t in data.get("tracks", []):
        st = whole[t["name"]]
        entry = {"id": t["name"], "name": t["name"], "instrument": t.get("instrument"), "family": model.family(t.get("instrument"))}
        if not model.pitched(t.get("instrument")):
            entry["pitched"] = False
        if st["n"]:
            role, conf, basis = _role(t, st, highest, lowest)
            entry.update({"role": role, "origin": "inferred", "confidence": conf, "basis": basis})
        entry["settings"] = ana["tracks"][t["name"]]["settings"]
        tracks.append(entry)
    keys = key_candidates(data)
    song = {"id": os.path.splitext(os.path.basename(json_path))[0], "title": data.get("title", ""), "composition": os.path.basename(json_path)}
    if keys:
        song["keyCandidates"] = keys
    cells = {tid: info["cells"] for tid, info in ana["tracks"].items()}
    return {
        "musicIr": 1,
        "origin": "inferred",
        "song": song,
        "fingerprint": {"composition": ana["composition"], "global": ana["global"]},
        "time": {"meter": data.get("timeSignature", [4, 4]), "barBeats": bb, "tempo": data["tempo"], "endBeat": end,
                 "endSeconds": round(tm.seconds(end), 2), "tailSeconds": (data.get("render") or {}).get("tailSeconds", 0)},
        "sections": sections,
        "harmony": [],
        "tracks": tracks,
        "motifs": [],
        "relations": [],
        "dependencies": [],
        "code": [],
        "cells": cells,
        "sites": {},
    }
