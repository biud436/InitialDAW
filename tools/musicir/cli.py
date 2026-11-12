"""music-ir 명령행: 곡 전체에서 구간, 트랙, 음표, 코드 자리로 좁혀 가며 조회하고 검사한다."""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import unicodedata

from . import check, infer, model

SONGS = os.path.join(model.REPO, "songs")
RENDER = os.path.join(model.REPO, "build", "engine", "music-render")
TREND = {"rise": "오름", "fall": "내림", "steady": "유지", "peak": "정점", "drop": "빠짐", "release": "풀림"}


# ---------------------------------------------------------------------------
# 곡 찾기
# ---------------------------------------------------------------------------
class Ctx:
    def __init__(self, ir, ir_file, json_file, data, others=()):
        self.ir, self.ir_file, self.json_file, self.data, self.others = ir, ir_file, json_file, data, list(others)
        self.inferred = ir.get("origin") == "inferred"
        self.bb = ir["time"]["barBeats"]
        self.tm = model.TempoMap(data["tempo"])
        self.flats = model.uses_flats(ir["song"].get("key"))
        self.sec = {s["id"]: s for s in ir["sections"]}
        self.trk = {t["id"]: t for t in ir["tracks"]}
        self.by_name = {t["name"]: t for t in data.get("tracks", [])}

    def rel(self, path):
        return os.path.relpath(path, model.REPO) if path else "-"

    def track_json(self, tid):
        t = self.trk.get(tid)
        return self.by_name.get(t["name"] if t else tid)

    def where(self, beat):
        bar = int(beat // self.bb + 1e-9) + 1
        return f"{bar}:{beat - (bar - 1) * self.bb + 1:g}"

    def roles(self, tid, sid=None):
        t = self.trk.get(tid, {})
        if sid and sid in (t.get("roles") or {}):
            return t["roles"][sid]
        return t.get("role", [])


def _ir_for(json_file):
    f = os.path.splitext(json_file)[0] + ".ir.json"
    return f if os.path.exists(f) else None


def resolve(arg, ir_override=None):
    """곡 인자(곡 폴더 이름, 폴더, JSON, IR, JSON 파일 이름)를 Ctx 로"""
    cands = []
    if os.path.isfile(arg):
        cands = [os.path.abspath(arg)]
    else:
        folder = arg if os.path.isdir(arg) else os.path.join(SONGS, arg)
        if os.path.isdir(folder):
            irs = sorted(f for f in os.listdir(folder) if f.endswith(".ir.json"))
            jsons = sorted(f for f in os.listdir(folder) if f.endswith(".json") and not f.endswith(".ir.json"))
            cands = [os.path.join(folder, f) for f in (irs or jsons)]
        else:
            stem = os.path.splitext(arg)[0]
            for d in sorted(os.listdir(SONGS)):
                p = os.path.join(SONGS, d, stem + ".json")
                if os.path.exists(p):
                    cands = [p]
                    break
    if not cands:
        raise SystemExit(f"곡을 찾지 못했습니다: {arg} (songs/ 의 폴더 이름, JSON 파일 이름, 경로)")
    first = cands[0]
    if first.endswith(".ir.json"):
        ir_file = first
        ir = model.load_json(ir_file)
        json_file = os.path.join(os.path.dirname(ir_file), ir["song"]["composition"])
    else:
        json_file = first
        ir_file = ir_override or _ir_for(json_file)
        ir = model.load_json(ir_file) if ir_file else None
    data = model.load_json(json_file)
    if ir_override:
        ir_file, ir = ir_override, model.load_json(ir_override)
    if ir is None or "sections" not in ir:
        ir = infer.infer(data, json_file)
        ir_file = None
    others = [os.path.basename(c) for c in cands[1:]]
    return Ctx(ir, ir_file, json_file, data, others)


def find_sections(ctx, ref):
    """구간 참조: id, 이름, 별칭, form:<형식>, last:<형식>, first:<형식>, 마디 범위 61-76 (쉼표로 여럿)"""
    out = []
    for part in [p.strip() for p in str(ref).split(",") if p.strip()]:
        secs = ctx.ir["sections"]
        hit = [s for s in secs if part in (s["id"], s.get("name")) or part in (s.get("aliases") or [])]
        m = re.match(r"^(form|last|first):(.+)$", part)
        if not hit and m:
            same = [s for s in secs if s.get("form") == m.group(2)]
            hit = same if m.group(1) == "form" else same[-1:] if m.group(1) == "last" else same[:1]
        r = re.match(r"^(\d+)-(\d+)$", part)
        if not hit and r:
            a, b = int(r.group(1)), int(r.group(2))
            hit = [s for s in secs if s["bars"][0] <= b and s["bars"][1] >= a]
        if not hit:
            raise SystemExit(f"구간을 찾지 못했습니다: {part} (있는 구간: {', '.join(s['id'] for s in secs)})")
        out += [s["id"] for s in hit if s["id"] not in out]
    return out


def find_tracks(ctx, ref):
    """트랙 참조: id, 이름, role:<역할>, family:<갈래> (쉼표로 여럿)"""
    out = []
    for part in [p.strip() for p in str(ref).split(",") if p.strip()]:
        ts = ctx.ir["tracks"]
        hit = [t for t in ts if part in (t["id"], t["name"]) or part.lower() == t["name"].lower()]
        if not hit and part.startswith("role:"):
            hit = [t for t in ts if part[5:] in t.get("role", []) or any(part[5:] in r for r in (t.get("roles") or {}).values())]
        if not hit and part.startswith("family:"):
            hit = [t for t in ts if t.get("family") == part[7:]]
        if not hit:
            raise SystemExit(f"트랙을 찾지 못했습니다: {part} (있는 트랙: {', '.join(t['id'] for t in ts)})")
        out += [t["id"] for t in hit if t["id"] not in out]
    return out


# ---------------------------------------------------------------------------
# 출력
# ---------------------------------------------------------------------------
def width(s):
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in str(s))


def table(rows, indent="  "):
    if not rows:
        return []
    cols = max(len(r) for r in rows)
    w = [max(width(r[i]) if i < len(r) else 0 for r in rows) for i in range(cols)]
    out = []
    for r in rows:
        cells = [str(c) + " " * (w[i] - width(c)) for i, c in enumerate(r)]
        out.append((indent + "  ".join(cells)).rstrip())
    return out


def emit(args, res, lines):
    if getattr(args, "json", False):
        print(json.dumps(res, ensure_ascii=False, indent=1))
    else:
        print("\n".join(lines))


def bars_text(s):
    a, b = s["bars"]
    return f"{a}-{b}" if a != b else f"{a}"


def energy_text(s):
    e = s.get("energy") or {}
    if not e:
        return "-"
    t = f"{e.get('level', '?')} {TREND.get(e.get('trend'), e.get('trend', ''))}".strip()
    return t + (f", 긴장 {e['tension']}" if "tension" in e else "")


def range_text(ctx, pr):
    return f"{model.spell(pr[0], ctx.flats)}-{model.spell(pr[1], ctx.flats)}" if pr else "-"


def freshness_line(ctx):
    if not ctx.ir_file:
        return "IR 없음: 곡 JSON 에서 잰 값과 추론(origin inferred, 신뢰도 표시)만 있습니다"
    f = check.freshness(ctx.ir, ctx.ir_file, ctx.data)
    parts = {"same": "JSON 지문 같음", "changed": f"JSON 이 IR 뒤에 바뀜 ({len(f['cells'])}칸)",
             "divergent": f"JSON 이 생성기 출력과 다름 ({len(f['cells'])}칸, IR 을 만들 때부터)"}[f["json"]]
    gen = {"same": "생성기 같음", "changed": "생성기가 IR 뒤에 바뀜", "missing": "생성기 없음", "none": "생성기 정보 없음"}[f["generator"]]
    extra = f", 입력 바뀜: {', '.join(os.path.basename(x) for x in f['inputs'])}" if f["inputs"] else ""
    return f"{parts}, {gen}{extra}"


def section_measure(ctx, sid):
    cells = {tid: c[sid] for tid, c in ctx.ir["cells"].items() if sid in c and c[sid].get("n")}
    s = ctx.sec[sid]
    nbars = s["bars"][1] - s["bars"][0] + 1
    n = sum(c["n"] for c in cells.values())
    vels = [(c["vel"][1], c["n"]) for tid, c in cells.items() if ctx.trk.get(tid, {}).get("pitched", True)]
    pitches = [c["pitch"] for tid, c in cells.items() if ctx.trk.get(tid, {}).get("pitched", True)]
    out = {"notes": n, "perBar": round(n / nbars, 1), "activeTracks": len(cells), "tracks": len(ctx.ir["tracks"])}
    if vels:
        out["velocityMean"] = round(sum(v * k for v, k in vels) / sum(k for _, k in vels))
    if pitches:
        out["register"] = [min(p[0] for p in pitches), max(p[1] for p in pitches)]
    chords = [c for c in ctx.ir.get("harmony", []) if s["beats"][0] - 1e-6 <= c["beat"] < s["beats"][1] - 1e-6]
    if chords:
        out["chordsPerBar"] = round(len(chords) / nbars, 2)
    return out


# ---------------------------------------------------------------------------
# 명령
# ---------------------------------------------------------------------------
def cmd_overview(ctx, args):
    ir, song, t = ctx.ir, ctx.ir["song"], ctx.ir["time"]
    bars = int(round(t["endBeat"] / ctx.bb))
    head = [song.get("title", ""), f"({song['id']})"]
    meta = [x for x in (song.get("key"), song.get("genre")) if x]
    if not meta and song.get("keyCandidates"):
        meta = ["조성 후보 " + ", ".join(f"{k['key']} r={k['r']}" for k in song["keyCandidates"])]
    tempo = ctx.tm.bpm_text()
    lines = [" ".join(head) + " | " + " | ".join(meta + [f"{t['meter'][0]}/{t['meter'][1]}, {tempo}", f"{bars}마디 {model.clock(t['endSeconds'])} + 꼬리 {t.get('tailSeconds', 0):g}초"]),
             f"IR: {ctx.rel(ctx.ir_file)} | {'추론' if ctx.inferred else '선언'} | {freshness_line(ctx)}"]
    if ctx.others:
        lines.append("같은 폴더의 다른 판: " + ", ".join(ctx.others))
    if song.get("summary"):
        lines.append(song["summary"])
    lines.append("구간")
    rows = []
    for s in ir["sections"]:
        extra = s.get("progression", "") if not ctx.inferred else f"신뢰도 {s.get('confidence', '-')}: {s.get('basis', '곡의 시작')}"
        rows.append([s["id"], bars_text(s), s["name"], s.get("form", "-"), energy_text(s), extra])
    lines += table(rows)
    lines.append("트랙")
    rows = []
    for tr in ir["tracks"]:
        role = ",".join(tr.get("role", [])) or "-"
        if tr.get("roles"):
            role += " (" + "; ".join(f"{k}: {','.join(v)}" for k, v in tr["roles"].items()) + ")"
        if tr.get("origin") == "inferred":
            role += f" [추론 {tr['confidence']}: {tr['basis']}]"
        rows.append([tr["id"], tr["name"], tr.get("instrument", ""), role])
    lines += table(rows)
    if ir.get("motifs"):
        lines.append("주제와 동기")
        rows = []
        for m in ir["motifs"]:
            uses = ", ".join(f"{u['track']}@{u['bar']}" + (f"({u['how']})" if u["how"] != "original" else "") for u in m["uses"]) or "-"
            rows.append([m["id"], m["name"], m.get("data", ""), uses])
        lines += table(rows)
    if ir.get("relations"):
        lines.append("관계: " + " | ".join(f"{r['from']} {r['kind']} {r['to']}" for r in ir["relations"]))
    lines.append("다음: section <구간> | tracks --section <구간> | deps --section <구간> | source --section <구간> --track <트랙> | notes ...")
    res = {"song": song, "ir": ctx.rel(ctx.ir_file), "inferred": ctx.inferred, "time": t,
           "sections": [{k: s.get(k) for k in ("id", "name", "bars", "form", "energy", "progression", "confidence", "basis") if s.get(k) is not None}
                        for s in ir["sections"]],
           "tracks": [{k: tr.get(k) for k in ("id", "name", "instrument", "family", "role", "roles", "confidence") if tr.get(k) is not None} for tr in ir["tracks"]],
           "motifs": [{"id": m["id"], "name": m["name"], "uses": [[u["track"], u["bar"], u["how"]] for u in m["uses"]]} for m in ir.get("motifs", [])],
           "relations": ir.get("relations", [])}
    emit(args, res, lines)


def cmd_sections(ctx, args):
    rows, res = [], []
    for s in ctx.ir["sections"]:
        m = section_measure(ctx, s["id"])
        rows.append([s["id"], bars_text(s), f"{model.clock(s['seconds'][0])}-{model.clock(s['seconds'][1])}", s["name"], s.get("form", "-"),
                     energy_text(s), f"음표 {m['notes']} (마디당 {m['perBar']})", f"트랙 {m['activeTracks']}/{m['tracks']}",
                     f"벨 {m.get('velocityMean', '-')}"])
        res.append(dict({"id": s["id"], "bars": s["bars"], "seconds": s["seconds"], "name": s["name"], "form": s.get("form"),
                         "energy": s.get("energy")}, measured=m))
    emit(args, res, table(rows, ""))


def harmony_rows(ctx, sid):
    s = ctx.sec[sid]
    chords = model.Harmony(ctx.ir.get("harmony")).between(*s["beats"])
    return [f"{ctx.where(max(c['beat'], s['beats'][0]))} {c['chord']}" for c in chords]


def code_span(ctx, sid):
    """이 구간에서만 쓰인 코드 자리의 지금 줄 범위"""
    if not ctx.ir.get("sites") or not ctx.ir_file:
        return None
    lines = check.locate(ctx.ir, ctx.ir_file)
    own = [lines[k] for k, s in ctx.ir["sites"].items() if s["sections"] == [sid] and lines.get(k)]
    fns = sorted({s["fn"] for k, s in ctx.ir["sites"].items() if s["sections"] == [sid]})
    return (min(own), max(own), fns, len(own)) if own else None


def render_cmd(ctx, sid):
    s = ctx.sec[sid]
    a, b = s["seconds"]
    stem = os.path.splitext(os.path.basename(ctx.json_file))[0]
    return (f"{ctx.rel(RENDER)} realtime {ctx.rel(ctx.json_file)} --from {a:.2f} --seconds {b - a:.2f} --no-pace "
            f"--output build/songs/{stem}-{sid}.wav")


def cmd_section(ctx, args):
    sid = find_sections(ctx, args.section)[0]
    s = ctx.sec[sid]
    m = section_measure(ctx, sid)
    lines = [f"{sid} {s['name']} ({s.get('form', '-')}) | {bars_text(s)}마디, 박 {s['beats'][0]:g}-{s['beats'][1]:g}, "
             f"{model.clock(s['seconds'][0])}-{model.clock(s['seconds'][1])}"]
    if s.get("origin") == "inferred":
        lines.append(f"추론한 구간 (신뢰도 {s.get('confidence', '-')}): {s.get('basis', '곡의 시작')}")
    if s.get("purpose"):
        lines.append(f"목적: {s['purpose']}")
    if s.get("note"):
        lines.append(f"메모: {s['note']}")
    meas = f"음표 {m['notes']} (마디당 {m['perBar']}), 트랙 {m['activeTracks']}/{m['tracks']}, 벨로시티 평균 {m.get('velocityMean', '-')}"
    if m.get("register"):
        meas += f", 음역 {range_text(ctx, m['register'])}"
    if m.get("chordsPerBar"):
        meas += f", 화성 리듬 마디당 {m['chordsPerBar']}"
    lines.append(f"에너지 의도: {energy_text(s)} | 잰 값: {meas}")
    hr = harmony_rows(ctx, sid)
    if hr or s.get("progression"):
        lines.append(f"화성: {s.get('key') or ctx.ir['song'].get('key', '')} | {s.get('roman', '')} | {s.get('progression', '')}".replace("|  |", "|"))
        if hr:
            lines.append("  " + " | ".join(hr))
    uses = [(mo, u) for mo in ctx.ir.get("motifs", []) for u in mo["uses"] if u.get("section") == sid]
    if uses:
        lines.append("주제: " + " | ".join(f"{mo['id']}({mo['name']}) {u['track']} {u['bar']}마디 {u['how']}" +
                                          (f" 일치 {u['match']:.2f}" if "match" in u else "") for mo, u in uses))
    rels = [r for r in ctx.ir.get("relations", []) if sid in (r["from"], r["to"])]
    if rels:
        lines.append("관계: " + " | ".join(f"{r['from']} {r['kind']} {r['to']}" + (f" ({r['note']})" if r.get("note") else "") for r in rels))
    rows, idle, cells = [], [], {}
    for tr in ctx.ir["tracks"]:
        c = ctx.ir["cells"].get(tr["id"], {}).get(sid)
        if not c or not c.get("n"):
            if c and c.get("cc"):
                rows.append([tr["id"], ",".join(ctx.roles(tr["id"], sid)) or "-", 0, "-", "-", "-", "-", "-", " ".join(c["cc"])])
            else:
                idle.append(tr["id"])
            continue
        cells[tr["id"]] = c
        rows.append([tr["id"], ",".join(ctx.roles(tr["id"], sid)) or "-", c["n"], c["density"], c["poly"],
                     range_text(ctx, c["pitch"]) if tr.get("pitched", True) else f"키 {c['pitch'][0]}-{c['pitch'][1]}",
                     "/".join(str(v) for v in c["vel"]), c.get("chordTones", "-"),
                     " ".join(f"{k}:{v[0]:g}-{v[1]:g}" for k, v in (c.get("cc") or {}).items())])
    lines.append("트랙 (id, 역할, 음표, 마디당, 동시, 음역, 벨로시티 최소/평균/최대, 화음음 비율, CC)")
    lines += table(rows)
    if idle:
        lines.append("쉬는 트랙: " + ", ".join(idle))
    span = code_span(ctx, sid)
    if span:
        lines.append(f"코드: {', '.join(span[2])}() {span[0]}-{span[1]}줄 (이 구간에만 쓰인 자리 {span[3]}곳) -> source --section {sid}")
    lines.append("렌더: " + render_cmd(ctx, sid))
    res = {"section": s, "measured": m, "harmony": hr, "motifs": [{"motif": mo["id"], **u} for mo, u in uses], "relations": rels,
           "cells": cells, "idle": idle, "code": span and {"lines": [span[0], span[1]], "functions": span[2]}, "render": render_cmd(ctx, sid)}
    emit(args, res, lines)


def cmd_tracks(ctx, args):
    sids = find_sections(ctx, args.section) if args.section else None
    want = find_tracks(ctx, args.track) if args.track else [t["id"] for t in ctx.ir["tracks"]]
    if sids and len(sids) == 1 and not args.track:
        args.section = sids[0]
        return cmd_section(ctx, args)
    rows, res = [], []
    order = [s["id"] for s in ctx.ir["sections"]]
    for tid in want:
        tr = ctx.trk[tid]
        cells = ctx.ir["cells"].get(tid, {})
        active = [sid for sid in order if cells.get(sid, {}).get("n") and (not sids or sid in sids)]
        n = sum(cells[sid]["n"] for sid in active)
        rows.append([tid, tr["name"], tr.get("instrument", ""), tr.get("family", ""), ",".join(tr.get("role", [])) or "-", n, " ".join(active) or "-"])
        res.append({"id": tid, "name": tr["name"], "instrument": tr.get("instrument"), "family": tr.get("family"), "role": tr.get("role"),
                    "roles": tr.get("roles"), "note": tr.get("note"), "notes": n, "sections": active})
    lines = table(rows, "")
    if args.track and len(want) == 1 and ctx.trk[want[0]].get("note"):
        lines.append("메모: " + ctx.trk[want[0]]["note"])
    emit(args, res, lines)


def cmd_harmony(ctx, args):
    sids = find_sections(ctx, args.section) if args.section else [s["id"] for s in ctx.ir["sections"]]
    if not ctx.ir.get("harmony"):
        emit(args, {"harmony": []}, ["화음표가 없습니다 (생성기가 선언하지 않았거나 IR 이 추론한 것입니다). 화성은 추론하지 않습니다."])
        return
    lines, res = [], {}
    for sid in sids:
        s = ctx.sec[sid]
        hr = harmony_rows(ctx, sid)
        lines.append(f"{sid} {bars_text(s)} {s.get('roman', '')}".rstrip())
        lines.append("  " + " | ".join(hr))
        res[sid] = {"roman": s.get("roman"), "progression": s.get("progression"),
                    "chords": [c for c in model.Harmony(ctx.ir["harmony"]).between(*s["beats"])]}
    emit(args, res, lines)


def cmd_motifs(ctx, args):
    lines, res = [], ctx.ir.get("motifs", [])
    for m in res:
        lines.append(f"{m['id']} {m['name']} ({m.get('kind', 'motif')}, {m.get('data', '-')}){': ' + m['note'] if m.get('note') else ''}")
        for u in m["uses"]:
            lines.append(f"  {u['section']:<8} {u['track']:<10} {u['bar']}마디 {u['how']}" + (f" +{u['transpose']}" if u.get("transpose") else "") +
                         (f" 마디 {u['part'][0]}-{u['part'][1] - 1}" if u.get("part") else "") +
                         (f" 일치 {u['match']:.2f}" if "match" in u else "") + (f" 다른 음 {u['extra']}" if u.get("extra") else ""))
    emit(args, res, lines or ["선언한 주제가 없습니다."])


CONST = re.compile(r"\b([A-Z][A-Z0-9_]{2,})\b")
IGNORE = {"BAR", "BPM", "END", "PIANO", "KICK", "CLAP", "HAT", "STICK", "CRASH", "OPEN_HAT", "HERE", "REPO"}


def sharing(ctx, sids, tids):
    """대상 칸의 코드 자리가 다른 구간과 함께 쓰는 도우미 함수와 데이터 이름"""
    sites = ctx.ir.get("sites", {})
    mine = {k for tid in tids for sid in sids for k in ctx.ir["cells"].get(tid, {}).get(sid, {}).get("sites", [])}
    helpers, consts, wide = {}, {}, []
    for k in mine:
        s = sites[k]
        others = [x for x in s["sections"] if x not in sids]
        if others:
            wide.append((k, s, others))
        if s.get("helper"):
            used = sorted({x for v in sites.values() if v.get("helper") == s["helper"] for x in v["sections"]} - set(sids))
            if used:
                helpers[s["helper"]] = used
        for c in CONST.findall(s["code"]):
            if c in IGNORE:
                continue
            used = sorted({x for v in sites.values() if c in CONST.findall(v["code"]) for x in v["sections"]} - set(sids))
            if used:
                consts[c] = used
    order = [s["id"] for s in ctx.ir["sections"]]
    srt = lambda xs: sorted(xs, key=lambda x: order.index(x) if x in order else 99)
    return {k: srt(v) for k, v in helpers.items()}, {k: srt(v) for k, v in consts.items()}, wide


def cmd_deps(ctx, args):
    sids = find_sections(ctx, args.section) if args.section else [s["id"] for s in ctx.ir["sections"]]
    if args.track:
        tids = find_tracks(ctx, args.track)
    else:
        tids = [t["id"] for t in ctx.ir["tracks"] if any(ctx.ir["cells"].get(t["id"], {}).get(s, {}).get("n") for s in sids)]
    lines, res = [], {"tracks": tids, "sections": sids}
    deps = ctx.ir.get("dependencies", [])
    out_d = [d for d in deps if set(d["from"]) & set(tids)]
    in_d = [d for d in deps if d["to"] in tids and not set(d["from"]) & set(tids)]
    if out_d:
        lines.append("이 트랙들이 기대는 것")
        lines += table([[",".join(x for x in d["from"] if x in tids), "->", d["to"], d["kind"], d.get("via", "-"), d.get("note", "")] for d in out_d])
    if in_d:
        lines.append("이 트랙들에 기대는 것 (바꾸면 함께 바뀐다)")
        lines += table([[",".join(d["from"]), "->", d["to"], d["kind"], d.get("via", "-"), d.get("note", "")] for d in in_d])
    res["dependsOn"], res["dependents"] = out_d, in_d
    rels = [r for r in ctx.ir.get("relations", []) if r["from"] in sids or r["to"] in sids]
    if rels:
        lines.append("구간 관계")
        lines += table([[r["from"], r["kind"], r["to"], r.get("note", "")] for r in rels])
    res["relations"] = rels
    shared = []
    for m in ctx.ir.get("motifs", []):
        here = [u for u in m["uses"] if u.get("section") in sids and u["track"] in tids]
        there = [u for u in m["uses"] if u.get("section") not in sids]
        if here and there:
            shared.append({"motif": m["id"], "data": m.get("data"), "here": [(u["track"], u["bar"]) for u in here],
                           "elsewhere": [(u["section"], u["track"], u["bar"], u["how"]) for u in there]})
    if shared:
        lines.append("같은 주제가 다른 구간에도 있다 (데이터를 고치면 그 구간도 바뀐다)")
        lines += table([[s["motif"], s["data"] or "-", ", ".join(f"{a}@{b}{'(' + c + ')' if c != 'original' else ''}" for a, _, b, c in
                                                             [(e[0], e[1], e[2], e[3]) for e in s["elsewhere"]])] for s in shared])
    res["sharedMotifs"] = shared
    helpers, consts, wide = sharing(ctx, sids, tids)
    if helpers or consts:
        lines.append("함께 쓰는 코드 (고치면 다른 구간도 바뀐다: 이 구간만 바꾸려면 호출 자리를 바꾸거나 새 함수를 만든다)")
        lines += table([["함수", h, ", ".join(v)] for h, v in sorted(helpers.items())] + [["데이터", c, ", ".join(v)] for c, v in sorted(consts.items())])
    if wide:
        lines.append("여러 구간에 걸친 코드 자리 (한 문장이 이 구간 밖의 음도 만든다)")
        lines += table([[s["fn"] + "()", s["code"][:90], "+ " + ", ".join(o)] for _, s, o in wide])
    res["sharedHelpers"], res["sharedData"] = helpers, consts
    res["wideSites"] = [{"site": k, "code": s["code"], "otherSections": o} for k, s, o in wide]
    if ctx.ir.get("code"):
        lines.append("곡 전체에 듣는 값: " + " | ".join(f"{c['symbol']} ({c['note']})" for c in ctx.ir["code"]))
        res["code"] = ctx.ir["code"]
    emit(args, res, lines or ["선언한 의존이 없습니다."])


def cmd_compare(ctx, args):
    a, b = find_sections(ctx, args.a)[0], find_sections(ctx, args.b)[0]
    sa, sb = ctx.sec[a], ctx.sec[b]
    ma, mb = section_measure(ctx, a), section_measure(ctx, b)
    head = [["", f"{a} {bars_text(sa)}", f"{b} {bars_text(sb)}"],
            ["형식", sa.get("form", "-"), sb.get("form", "-")],
            ["에너지 의도", energy_text(sa), energy_text(sb)],
            ["마디", sa["bars"][1] - sa["bars"][0] + 1, sb["bars"][1] - sb["bars"][0] + 1],
            ["음표", f"{ma['notes']} (마디당 {ma['perBar']})", f"{mb['notes']} (마디당 {mb['perBar']})"],
            ["트랙", ma["activeTracks"], mb["activeTracks"]],
            ["벨로시티", ma.get("velocityMean", "-"), mb.get("velocityMean", "-")],
            ["음역", range_text(ctx, ma.get("register")), range_text(ctx, mb.get("register"))]]
    lines = table(head, "")
    H = model.Harmony(ctx.ir.get("harmony"))
    ca = [(c["beat"] - sa["beats"][0], c["chord"]) for c in H.between(*sa["beats"])]
    cb = [(c["beat"] - sb["beats"][0], c["chord"]) for c in H.between(*sb["beats"])]
    if ca or cb:
        n = min(len(ca), len(cb))
        same = sum(1 for x, y in zip(ca, cb) if x == y)
        lines.append(f"화성: 앞에서부터 {n}개 중 {same}개 같은 자리, 같은 화음" + (" (진행이 같다)" if same == len(ca) == len(cb) else ""))
    for sid in (a, b):
        uses = [f"{m['id']}:{u['track']}@{u['bar']}({u['how']})" for m in ctx.ir.get("motifs", []) for u in m["uses"] if u.get("section") == sid]
        if uses:
            lines.append(f"주제 {sid}: " + ", ".join(uses))
    rel = [r for r in ctx.ir.get("relations", []) if {r["from"], r["to"]} == {a, b}]
    if rel:
        lines.append("관계: " + " | ".join(f"{r['from']} {r['kind']} {r['to']}: {r.get('note', '')}" for r in rel))
    rows = []
    for tr in ctx.ir["tracks"]:
        x = ctx.ir["cells"].get(tr["id"], {}).get(a, {})
        y = ctx.ir["cells"].get(tr["id"], {}).get(b, {})
        if not x.get("n") and not y.get("n"):
            continue
        fx = f"{x['n']}개 벨{x['vel'][1]} {range_text(ctx, x['pitch']) if tr.get('pitched', True) else ''}".strip() if x.get("n") else "-"
        fy = f"{y['n']}개 벨{y['vel'][1]} {range_text(ctx, y['pitch']) if tr.get('pitched', True) else ''}".strip() if y.get("n") else "-"
        same = "같음" if x.get("hash") and x.get("hash") == y.get("hash") else ""
        rows.append([tr["id"], fx, fy, same])
    lines.append("트랙")
    lines += table(rows)
    emit(args, {"a": a, "b": b, "measured": [ma, mb], "harmonySame": ca == cb, "tracks": rows}, lines)


def cmd_notes(ctx, args):
    sids = find_sections(ctx, args.section) if args.section else None
    tids = find_tracks(ctx, args.track)
    lo, hi = 0.0, float("inf")
    if sids:
        lo = min(ctx.sec[s]["beats"][0] for s in sids)
        hi = max(ctx.sec[s]["beats"][1] for s in sids)
    if args.bars:
        a, _, b = args.bars.partition("-")
        lo, hi = (int(a) - 1) * ctx.bb, int(b or a) * ctx.bb
    lines, res = [], {}
    for tid in tids:
        t = ctx.track_json(tid)
        if t is None:
            continue
        notes = sorted((n for n in t.get("notes", []) if lo - 1e-6 <= float(n["startBeat"]) < hi - 1e-6), key=lambda n: (n["startBeat"], model.parse_pitch(n["pitch"])))
        lanes = {k: [p for p in v if lo - 1e-6 <= float(p["beat"]) < hi - 1e-6] for k, v in model.track_lanes(t).items()}
        lanes = {k: v for k, v in lanes.items() if v}
        lines.append(f"{tid} ({t['name']}, {t.get('instrument')}) 박 {lo:g}-{hi:g}: 음표 {len(notes)}" + "".join(f", {k} {len(v)}점" for k, v in lanes.items()))
        shown = notes[:args.limit]
        rows = [[ctx.where(float(n["startBeat"])), model.spell(model.parse_pitch(n["pitch"]), ctx.flats), f"{float(n['durationBeat']):g}", n["velocity"],
                 n.get("articulation", "")] for n in shown]
        lines += table(rows)
        if len(notes) > len(shown):
            lines.append(f"  ... {len(notes) - len(shown)}개 더 (--limit)")
        for k, v in lanes.items():
            pts = " ".join(f"{ctx.where(float(p['beat']))}={p['value']:g}{'~' if p.get('curve') == 'smooth' else ''}" for p in v[:args.limit])
            lines.append(f"  {k}: {pts}" + (" ..." if len(v) > args.limit else ""))
        res[tid] = {"notes": notes, "lanes": lanes}
    emit(args, res, lines)


def cmd_source(ctx, args):
    if not ctx.ir.get("sites"):
        emit(args, {}, ["코드 자리가 없습니다 (생성기가 musicir.trace 를 쓰지 않았습니다)."])
        return
    sids = find_sections(ctx, args.section) if args.section else None
    tids = find_tracks(ctx, args.track) if args.track else None
    where = check.locate(ctx.ir, ctx.ir_file)
    script = check.script_path(ctx.ir, ctx.ir_file)
    funcs = model.functions(script)
    rows, res, helpers = [], [], set()
    for k, s in sorted(ctx.ir["sites"].items(), key=lambda kv: (where.get(kv[0]) or 10 ** 6)):
        if sids and not set(s["sections"]) & set(sids):
            continue
        if tids and not set(s["tracks"]) & set(tids):
            continue
        line = where.get(k)
        rows.append([f"{s['fn']}():{line if line else '?'}", s["code"][:100], ",".join(s["tracks"]), " ".join(s["sections"]) if len(s["sections"]) <= 3 else f"{len(s['sections'])}구간",
                     f"음표 {s['notes']}" if s["notes"] else f"점 {s['points']}"])
        if s.get("helper"):
            helpers.add(s["helper"])
        res.append(dict(s, site=k, currentLine=line))
    lines = [f"{ctx.rel(script)}"] + table(rows)
    if helpers:
        lines.append("도우미 함수: " + ", ".join(f"{h}() {funcs[h][0]}-{funcs[h][1]}줄" for h in sorted(helpers) if h in funcs))
    if any(r["currentLine"] is None for r in res):
        lines.append("? 는 지금의 생성기에서 찾지 못한 자리입니다 (생성기를 고친 뒤 다시 돌리지 않았습니다).")
    emit(args, res, lines)


def cmd_check(ctx, args):
    if not ctx.ir_file:
        print("IR 이 없습니다 (추론만 할 수 있습니다): " + ctx.rel(ctx.json_file))
        return 1
    f = check.freshness(ctx.ir, ctx.ir_file, ctx.data)
    probs = check.integrity(ctx.ir, ctx.ir_file, ctx.data)
    lines = [f"{ctx.rel(ctx.ir_file)}: {freshness_line(ctx)}"]
    for c in f["cells"][:args.limit]:
        lines.append(f"  다른 칸 {c['track']} @ {c['section']}: {c['what']}")
    if len(f["cells"]) > args.limit:
        lines.append(f"  ... {len(f['cells']) - args.limit}칸 더")
    if f["json"] == "divergent":
        lines.append("  디스크의 JSON 은 생성기의 지금 출력이 아닙니다. 생성기를 다시 돌리면 위 칸이 바뀝니다 (덮어쓰기는 막혀 있습니다).")
    if f["inputs"]:
        lines.append("  생성기가 읽은 파일이 바뀌었습니다: " + ", ".join(f["inputs"]) + " (다시 돌리면 결과가 달라질 수 있습니다)")
    for sev, msg in probs:
        lines.append(f"  {'오류' if sev == 'error' else '주의'}: {msg}")
    errors = [p for p in probs if p[0] == "error"]
    ok = not errors and f["json"] in ("same", "divergent") and f["generator"] == "same"
    lines.append("결과: " + ("통과" if ok else "확인 필요"))
    emit(args, {"freshness": f, "problems": probs, "ok": ok}, lines)
    return 0 if ok else 1


def cmd_infer(args):
    data = model.load_json(args.song)
    ir = infer.infer(data, args.song)
    if args.write:
        from . import record
        path = os.path.splitext(args.song)[0] + ".ir.json"
        if os.path.exists(path) and model.load_json(path).get("origin") != "inferred":
            raise SystemExit(f"선언한 IR 이 이미 있습니다: {path}")
        record.write_ir(path, ir)
        print(f"썼습니다: {os.path.relpath(path, model.REPO)}")
        return 0
    ctx = Ctx(ir, None, os.path.abspath(args.song), data)
    cmd_overview(ctx, args)
    return 0


def run_generator(folder, env_extra):
    script = os.path.join(folder, "compose.py")
    env = dict(os.environ, **env_extra)
    p = subprocess.run([sys.executable, script], capture_output=True, text=True, env=env, cwd=model.REPO)
    if p.returncode != 0:
        raise SystemExit(f"생성기가 실패했습니다:\n{p.stderr[-2000:]}")
    return p


def cmd_regen(args):
    folder = args.song if os.path.isdir(args.song) else os.path.join(SONGS, args.song)
    if not os.path.exists(os.path.join(folder, "compose.py")):
        raise SystemExit(f"생성기가 없습니다: {folder}")
    if args.ir_only:
        p = run_generator(folder, {"MUSICIR_IR_ONLY": "1"})
        print(p.stdout.strip())
        print("IR 만 썼습니다 (곡 JSON 은 그대로).")
        return 0
    with tempfile.TemporaryDirectory() as tmp:
        run_generator(folder, {"MUSICIR_OUT": tmp})
        same = True
        for f in sorted(os.listdir(tmp)):
            if not f.endswith(".json") or f.endswith(".ir.json"):
                continue
            new, cur = os.path.join(tmp, f), os.path.join(folder, f)
            if not os.path.exists(cur):
                print(f"{f}: 새 파일")
                same = False
                continue
            a, b = model.load_json(cur), model.load_json(new)
            ir = model.load_json(os.path.join(tmp, f[:-5] + ".ir.json"))
            if model.composition_hash(a) == model.composition_hash(b):
                print(f"{f}: 생성기의 지금 출력과 같다")
                continue
            same = False
            diff = check.compare_cells(ir, a)
            print(f"{f}: 생성기의 지금 출력과 {len(diff)}칸 다르다 (다시 돌리면 바뀌는 칸)")
            for c in diff[:args.limit]:
                print(f"  {c['track']} @ {c['section']}: {c['what']}")
    return 0 if same else 1


def main(argv=None):
    ap = argparse.ArgumentParser(prog="music-ir", description="곡의 Music IR 을 조회하고 검사한다. 곡은 songs/ 의 폴더 이름, JSON 파일 이름, 경로로 준다.")
    ap.add_argument("--json", action="store_true", help="구조화된 JSON 으로 출력")
    ap.add_argument("--ir", help="이 IR 로 조회한다 (앱에서 다른 이름으로 저장한 판을 원래 IR 과 견줄 때)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add(name, help_text):
        p = sub.add_parser(name, help=help_text)
        p.add_argument("song")
        p.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
        return p

    add("overview", "1단계: 곡 전체 (구간, 트랙의 역할, 주제, 관계)")
    add("sections", "구간마다 잰 값")
    p = add("section", "2단계: 한 구간 (화성, 트랙별 잰 값, 주제, 관계, 코드 줄)")
    p.add_argument("section")
    p = add("tracks", "트랙의 역할과 쓰인 구간")
    p.add_argument("--section")
    p.add_argument("--track")
    p = add("harmony", "화음표")
    p.add_argument("--section")
    add("motifs", "주제와 쓰인 자리, 실제 음과의 일치")
    p = add("deps", "의존: 기대는 것, 기대는 트랙, 같은 주제, 함께 쓰는 코드")
    p.add_argument("--section")
    p.add_argument("--track")
    p = add("compare", "두 구간 비교")
    p.add_argument("a")
    p.add_argument("b")
    p = add("notes", "3단계: 실제 음표와 CC")
    p.add_argument("--section")
    p.add_argument("--track", required=True)
    p.add_argument("--bars", help="마디 범위, 예: 61-64")
    p.add_argument("--limit", type=int, default=64)
    p = add("source", "3단계: 음표를 만든 생성기의 코드 자리 (지금 줄 번호)")
    p.add_argument("--section")
    p.add_argument("--track")
    p = add("check", "IR 의 신선함과 참조 무결성")
    p.add_argument("--limit", type=int, default=20)
    p = sub.add_parser("infer", help="IR 이 없는 곡 JSON 에서 잰 값과 추론으로 요약")
    p.add_argument("song")
    p.add_argument("--write", action="store_true", help="<이름>.ir.json 으로 쓴다 (origin inferred)")
    p.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    p = sub.add_parser("regen", help="생성기를 임시 폴더에서 돌려 디스크의 JSON 과 견준다 (--ir-only: JSON 은 두고 IR 만 쓴다)")
    p.add_argument("song")
    p.add_argument("--ir-only", action="store_true")
    p.add_argument("--limit", type=int, default=20)
    from . import edit
    edit.add_commands(sub)

    args = ap.parse_args(argv)
    if args.cmd == "infer":
        return cmd_infer(args)
    if args.cmd == "regen":
        return cmd_regen(args)
    if args.cmd in ("plan", "verify", "apply"):
        return edit.run(args)
    ctx = resolve(args.song, args.ir)
    fn = {"overview": cmd_overview, "sections": cmd_sections, "section": cmd_section, "tracks": cmd_tracks, "harmony": cmd_harmony,
          "motifs": cmd_motifs, "deps": cmd_deps, "compare": cmd_compare, "notes": cmd_notes, "source": cmd_source, "check": cmd_check}[args.cmd]
    return fn(ctx, args) or 0
