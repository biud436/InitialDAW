"""IR 과 지금 파일의 관계: 신선함(생성기, 입력, JSON 지문), 참조 무결성, 코드 자리 찾기."""
import os

from . import model


def section_items(ir):
    return [(s["id"], s["beats"][0], s["beats"][1]) for s in ir.get("sections", [])]


def track_ids(ir):
    return {t["name"]: t["id"] for t in ir.get("tracks", [])}


def analyze(ir, data):
    return model.analyze(data, section_items(ir), track_ids(ir), ir.get("harmony"))


def compare_cells(ir, data):
    """IR 에 적힌 칸 지문과 data 를 비교: [{"track", "section", "what"}]"""
    if "sections" not in ir:
        same = model.composition_hash(data) == (ir.get("fingerprint") or {}).get("composition")
        return [] if same else [{"track": "*", "section": "*", "what": "곡 전체"}]
    ana = analyze(ir, data)
    out = []
    if ana["global"] != ir["fingerprint"].get("global"):
        out.append({"track": "*", "section": "*", "what": "곡 설정 (템포, 버스, 마스터, 렌더)"})
    known = {t["id"]: t for t in ir["tracks"]}
    for tid, t in known.items():
        if tid not in ana["tracks"]:
            out.append({"track": tid, "section": "*", "what": f"트랙 없음 ('{t['name']}')"})
    for tid, info in ana["tracks"].items():
        if tid not in known:
            out.append({"track": tid, "section": "*", "what": "새 트랙"})
            continue
        if info["settings"] != known[tid].get("settings"):
            out.append({"track": tid, "section": "*", "what": "트랙 설정 (믹서, 효과, 센드, 신스)"})
        old = ir["cells"].get(tid, {})
        for sid in sorted(set(old) | set(info["cells"]), key=_order(ir)):
            a, b = old.get(sid, {}).get("hash", ""), info["cells"].get(sid, {}).get("hash", "")
            if a != b:
                na, nb = old.get(sid, {}).get("n", 0), info["cells"].get(sid, {}).get("n", 0)
                out.append({"track": tid, "section": sid, "what": f"음표 {na} -> {nb}" if na != nb else "음표나 레인 값"})
    return out


def _order(ir):
    ids = [s["id"] for s in ir.get("sections", [])]
    return lambda sid: ids.index(sid) if sid in ids else len(ids)


def cell_changes(ir, data):
    return [f"{c['track']} @ {c['section']}: {c['what']}" for c in compare_cells(ir, data)]


def script_path(ir, ir_file):
    return os.path.join(os.path.dirname(os.path.abspath(ir_file)), ir["generator"]["script"]) if ir.get("generator") else None


def freshness(ir, ir_file, data):
    """{"json": same|changed|divergent, "generator": same|changed|missing|none, "inputs": [바뀐 입력], "cells": [...]}"""
    out = {"generator": "none", "inputs": [], "cells": []}
    gen = ir.get("generator")
    if gen:
        script = script_path(ir, ir_file)
        sha = model.file_sha(script)
        out["generator"] = "missing" if sha is None else "same" if sha == gen.get("sha256") else "changed"
        for rel, old in (gen.get("inputs") or {}).items():
            if model.file_sha(os.path.join(model.REPO, rel)) != old:
                out["inputs"].append(rel)
    h = model.composition_hash(data)
    fp = ir.get("fingerprint") or {}
    if h == fp.get("composition"):
        out["json"] = "same"
    else:
        out["json"] = "divergent" if h == fp.get("file") else "changed"
        out["cells"] = compare_cells(ir, data)
    return out


def locate(ir, ir_file, script=None):
    """{자리 id: 지금의 줄 번호 또는 None}. 줄 번호가 아니라 함수 안의 문장(code, occ)으로 찾는다"""
    script = script or script_path(ir, ir_file)
    if not script or not os.path.exists(script):
        return {sid: None for sid in ir.get("sites", {})}
    lines = open(script).read().split("\n")
    funcs = model.functions(script)
    out = {}
    for sid, s in ir.get("sites", {}).items():
        first, last = funcs.get(s["fn"], (1, len(lines)))
        hits = [k for k in range(first, last + 1) if k - 1 < len(lines) and lines[k - 1].strip() == s["code"]]
        out[sid] = hits[s.get("occ", 0)] if s.get("occ", 0) < len(hits) else None
    return out


def integrity(ir, ir_file, data, script=None):
    """참조 무결성: 트랙 이름, 구간 범위, 동기의 자리, 관계, 의존, 코드 자리. [(심각도, 내용)]"""
    from . import schema
    probs = [("error", f"스키마: {e}") for e in schema.errors(ir)[:5]]
    if ir.get("error"):
        probs.append(("error", f"IR 을 만들 때 실패: {ir['error']}"))
        return probs
    names = {t["name"] for t in data.get("tracks", [])}
    ids = {t["id"] for t in ir["tracks"]}
    sids = [s["id"] for s in ir["sections"]]
    for t in ir["tracks"]:
        if t["name"] not in names:
            hint = ""
            old = (ir["cells"].get(t["id"]) or {})
            if old:
                cand = [n for n in names if n not in {x["name"] for x in ir["tracks"]}]
                hint = f" (새 이름 후보: {', '.join(cand)})" if cand else ""
            probs.append(("error", f"트랙 '{t['name']}' ({t['id']}) 이 곡 JSON 에 없다{hint}"))
    if len(set(sids)) != len(sids):
        probs.append(("error", "구간 id 가 겹친다"))
    end = model.end_beat(data)
    items = section_items(ir)
    for a, b in zip(items, items[1:]):
        if abs(a[2] - b[1]) > model.EPS:
            probs.append(("warn", f"구간 {a[0]} 과 {b[0]} 사이가 {'겹친다' if a[2] > b[1] else '비었다'}"))
    if items and items[-1][2] < end - model.EPS:
        probs.append(("warn", f"마지막 구간이 {items[-1][2]:g}박에서 끝나지만 곡은 {end:g}박까지 간다"))
    for t in ir["tracks"]:
        for sid in (t.get("roles") or {}):
            if sid not in sids:
                probs.append(("error", f"트랙 {t['id']} 의 역할이 없는 구간 {sid} 을 가리킨다"))
    by_name = {t["name"]: t for t in data.get("tracks", [])}
    id_to_name = {t["id"]: t["name"] for t in ir["tracks"]}
    bb = ir["time"]["barBeats"]
    for m in ir.get("motifs", []):
        for u in m.get("uses", []):
            if u["track"] not in ids:
                probs.append(("error", f"동기 {m['id']} 의 트랙 {u['track']} 이 없다"))
                continue
            track = by_name.get(id_to_name[u["track"]])
            if track is None or "match" not in u:
                continue
            score, _ = model.motif_match(m, track.get("notes", []), (u["bar"] - 1) * bb, u.get("transpose", 0), u.get("part"), bb)
            if u["how"] != "varied" and score is not None and score < 0.9:
                probs.append(("warn", f"동기 {m['id']} 이 {u['track']} {u['bar']}마디에서 {score:.2f} 만 맞는다 ({u['how']} 로 선언)"))
            elif score is not None and score < u["match"] - 0.05:
                probs.append(("warn", f"동기 {m['id']} 의 {u['track']} {u['bar']}마디 일치가 {u['match']:.2f} 에서 {score:.2f} 로 줄었다"))
    for r in ir.get("relations", []):
        for k in ("from", "to"):
            if r[k] not in sids:
                probs.append(("error", f"관계 {r['from']} -> {r['to']} 이 없는 구간을 가리킨다"))
    for d in ir.get("dependencies", []):
        for t in d["from"] + ([d["to"]] if d["to"] != "harmony" else []):
            if t not in ids and t not in names:
                probs.append(("error", f"의존 {d['kind']} 이 없는 트랙 {t} 을 가리킨다"))
    if ir.get("sites"):
        lost = [sid for sid, line in locate(ir, ir_file, script).items() if line is None]
        if lost:
            probs.append(("warn", f"생성기에서 찾지 못한 코드 자리 {len(lost)}곳 (생성기를 고친 뒤 다시 돌리지 않았다)"))
    for w in ir.get("warnings", []):
        probs.append(("warn", w))
    return probs
