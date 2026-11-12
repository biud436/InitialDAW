"""고칠 범위를 정하고(plan), 고친 생성기의 결과를 검사하고(verify), 바뀐 칸만 곡 JSON 에 반영한다(apply).

  plan    대상 구간과 트랙, 보호할 트랙과 화성을 정하고 지금 생성기 출력과 디스크의 JSON 을 떠 둔다
  verify  고친 생성기를 임시 폴더에서 돌려 떠 둔 출력과 칸마다 견준다 (보호 대상, 범위 밖, 화성, 불러오기)
  apply   검사를 통과한 결과를 쓴다. 디스크의 JSON 이 생성기 출력과 이미 달랐으면(앱 편집, 보정표 변경) 대상 칸만 옮겨 심는다
"""
import argparse
import collections
import datetime
import json
import os
import shutil
import subprocess
import sys

from . import check, model

PLANS = os.path.join(model.REPO, "build", "music-ir", "plans")
RENDER = os.path.join(model.REPO, "build", "engine", "music-render")
HARMONIC_ROLES = {"harmony", "pad", "bass", "sub-bass", "arpeggio"}


def add_commands(sub):
    p = sub.add_parser("plan", help="고칠 범위를 정하고 지금 상태를 떠 둔다")
    p.add_argument("song")
    p.add_argument("--sections", required=True, help="대상 구간 (id, 이름, 별칭, last:<형식>, 마디 범위)")
    p.add_argument("--tracks", required=True, help="대상 트랙 (id, 이름, role:<역할>, family:<갈래>)")
    p.add_argument("--protect", default="", help="보호할 트랙과 harmony (예: role:main-melody,piano,harmony)")
    p.add_argument("--allow", default="notes,cc", help="대상 칸에서 바꿔도 되는 것: notes, cc, settings(대상 트랙의 믹서와 효과), new-tracks")
    p.add_argument("--intent", default="", help="요청을 풀어 쓴 한 줄")
    p.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    p = sub.add_parser("verify", help="고친 생성기를 임시로 돌려 계획과 견준다")
    p.add_argument("plan", help="plan.json 경로 또는 곡 이름 (가장 최근 계획)")
    p.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    p = sub.add_parser("apply", help="검사를 통과한 결과를 곡 JSON 과 IR 로 쓴다")
    p.add_argument("plan")


def run(args):
    return {"plan": cmd_plan, "verify": cmd_verify, "apply": cmd_apply}[args.cmd](args)


# ---------------------------------------------------------------------------
# 공통
# ---------------------------------------------------------------------------
def song_folder(arg):
    from .cli import SONGS
    if os.path.isdir(arg):
        return os.path.abspath(arg)
    if os.path.isdir(os.path.join(SONGS, arg)):
        return os.path.join(SONGS, arg)
    if os.path.isfile(arg):
        return os.path.dirname(os.path.abspath(arg))
    raise SystemExit(f"곡 폴더를 찾지 못했습니다: {arg}")


def generate(folder, out):
    """생성기를 out 폴더로 돌린다 (디스크의 곡 파일은 건드리지 않는다)"""
    env = dict(os.environ, MUSICIR_OUT=out)
    env.pop("MUSICIR_IR_ONLY", None)
    p = subprocess.run([sys.executable, os.path.join(folder, "compose.py")], capture_output=True, text=True, env=env, cwd=model.REPO)
    if p.returncode != 0:
        raise SystemExit(f"생성기가 실패했습니다:\n{p.stderr[-3000:]}")
    return sorted(f for f in os.listdir(out) if f.endswith(".json") and not f.endswith(".ir.json"))


def find_plan(arg):
    if os.path.isfile(arg):
        return os.path.abspath(arg)
    if os.path.isdir(arg) and os.path.exists(os.path.join(arg, "plan.json")):
        return os.path.join(os.path.abspath(arg), "plan.json")
    song = os.path.basename(song_folder(arg))
    cands = sorted(d for d in os.listdir(PLANS) if d.startswith(song + "-")) if os.path.isdir(PLANS) else []
    if not cands:
        raise SystemExit(f"계획이 없습니다: {arg}")
    return os.path.join(PLANS, cands[-1], "plan.json")


def ctx_of(ir, data):
    from .cli import Ctx
    return Ctx(ir, None, "", data)


def cell_diff(track_a, track_b, sections, sid):
    """두 트랙의 한 구간: 더한 음, 뺀 음, 고친 음(같은 높이와 자리), 레인 점의 증감"""
    na, pa = model.cell_notes(track_a, sections, sid) if track_a else ([], [])
    nb, pb = model.cell_notes(track_b, sections, sid) if track_b else ([], [])
    ka = collections.Counter(tuple(model.note_key(n)) for n in na)
    kb = collections.Counter(tuple(model.note_key(n)) for n in nb)
    added, removed = kb - ka, ka - kb
    pos = collections.Counter((k[0], k[1]) for k in removed.elements()) & collections.Counter((k[0], k[1]) for k in added.elements())
    modified = sum(pos.values())
    la = collections.Counter(tuple(model.point_key(lane, p)) for lane, p in pa)
    lb = collections.Counter(tuple(model.point_key(lane, p)) for lane, p in pb)
    return {"added": sum(added.values()) - modified, "removed": sum(removed.values()) - modified, "modified": modified,
            "pointsAdded": sum((lb - la).values()), "pointsRemoved": sum((la - lb).values()), "before": len(na), "after": len(nb),
            "lanes": sorted({k[0] for k in (lb - la) + (la - lb)})}


def clashes(target_notes, melody_notes):
    """선율과 단2도, 단9도로 0.1박 넘게 함께 울리는 반주 음 수"""
    mel = [(float(n["startBeat"]), float(n["startBeat"]) + float(n["durationBeat"]), model.parse_pitch(n["pitch"])) for n in melody_notes]
    count = 0
    for n in target_notes:
        a0, p = float(n["startBeat"]), model.parse_pitch(n["pitch"])
        a1 = a0 + float(n["durationBeat"])
        if any(min(a1, v1) - max(a0, v0) > 0.1 and abs(p - v) in (1, 13, 25) for v0, v1, v in mel):
            count += 1
    return count


def validate(path):
    if not os.path.exists(RENDER):
        return None, "music-render 가 없어 건너뜀 (./build.sh)"
    p = subprocess.run([RENDER, "validate", path], capture_output=True, text=True)
    try:
        res = json.loads(p.stdout)
    except json.JSONDecodeError:
        return False, p.stderr[-300:]
    return bool(res.get("ok")), "; ".join(e.get("message", "") for e in res.get("errors", [])[:3])


# ---------------------------------------------------------------------------
# plan
# ---------------------------------------------------------------------------
def cmd_plan(args):
    from .cli import find_sections, find_tracks, table
    folder = song_folder(args.song)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    pdir = os.path.join(PLANS, f"{os.path.basename(folder)}-{stamp}")
    base, disk = os.path.join(pdir, "baseline"), os.path.join(pdir, "disk")
    os.makedirs(disk, exist_ok=True)
    outputs = generate(folder, base)
    if not outputs:
        raise SystemExit("생성기가 곡 JSON 을 쓰지 않았습니다")
    first_ir = model.load_json(os.path.join(base, outputs[0][:-5] + ".ir.json"))
    if "sections" not in first_ir:
        raise SystemExit(f"IR 을 만들지 못했습니다: {first_ir.get('error')}")
    ctx = ctx_of(first_ir, model.load_json(os.path.join(base, outputs[0])))
    sections = find_sections(ctx, args.sections)
    tracks = find_tracks(ctx, args.tracks)
    protect_refs = [p.strip() for p in args.protect.split(",") if p.strip()]
    protect_harmony = "harmony" in protect_refs
    protected = find_tracks(ctx, ",".join(p for p in protect_refs if p != "harmony")) if [p for p in protect_refs if p != "harmony"] else []
    overlap = set(protected) & set(tracks)
    if overlap:
        raise SystemExit(f"보호할 트랙이 대상에도 있습니다: {', '.join(sorted(overlap))}")
    allow = [a.strip() for a in args.allow.split(",") if a.strip()]

    outs = []
    for f in outputs:
        cur = os.path.join(folder, f)
        entry = {"composition": f, "drift": []}
        if os.path.exists(cur):
            shutil.copy2(cur, os.path.join(disk, f))
            ir = model.load_json(os.path.join(base, f[:-5] + ".ir.json"))
            entry["drift"] = check.compare_cells(ir, model.load_json(cur))
        else:
            entry["new"] = True
        outs.append(entry)

    script = os.path.join(folder, "compose.py")
    sites = check.locate(first_ir, None, script)
    funcs = model.functions(script)
    target_sites = []
    for k, s in first_ir.get("sites", {}).items():
        if set(s["sections"]) & set(sections) and set(s["tracks"]) & set(tracks):
            target_sites.append(dict(s, site=k, currentLine=sites.get(k), only=set(s["sections"]) <= set(sections)))
    target_sites.sort(key=lambda s: s["currentLine"] or 0)
    from .cli import sharing
    helpers, consts, _ = sharing(ctx, sections, tracks)
    deps = [d for d in first_ir.get("dependencies", []) if set(d["from"]) & set(tracks) or d["to"] in tracks]
    rules = ["보호 트랙: " + (", ".join(protected) or "없음") + " 의 모든 칸과 설정이 그대로",
             "화음표가 그대로" + (" 이고 대상 칸의 화성 트랙이 화음 구성음을 지킨다" if protect_harmony else ""),
             "대상(트랙 x 구간) 밖의 칸, 곡 설정, 대상이 아닌 트랙의 설정이 그대로",
             "대상 칸에서 바뀐 것은 허락한 종류만: " + ", ".join(allow),
             "새 IR 의 참조 무결성 (트랙, 구간, 동기의 일치)",
             "music-render validate 로 곡을 불러올 수 있다",
             "주의: 대상 칸이 선율과 단2도, 단9도로 부딪는 음이 늘지 않는다"]
    plan = {"musicIrPlan": 1, "created": stamp, "song": os.path.basename(folder), "folder": os.path.relpath(folder, model.REPO),
            "generatorSha": model.file_sha(script), "intent": args.intent,
            "targets": {"sections": sections, "tracks": tracks}, "protect": {"tracks": protected, "harmony": protect_harmony},
            "allow": allow, "outputs": outs, "sites": target_sites, "dependencies": deps,
            "sharedHelpers": helpers, "sharedData": consts, "rules": rules}
    with open(os.path.join(pdir, "plan.json"), "w") as f:
        json.dump(plan, f, ensure_ascii=False, indent=1)

    secs = {s["id"]: s for s in first_ir["sections"]}
    L = [f"계획: {os.path.relpath(os.path.join(pdir, 'plan.json'), model.REPO)}"]
    if args.intent:
        L.append(f"요청: {args.intent}")
    L.append("대상 구간: " + ", ".join(f"{s} ({secs[s]['name']}, {secs[s]['bars'][0]}-{secs[s]['bars'][1]}마디)" for s in sections))
    L.append("대상 트랙: " + ", ".join(f"{t} ({ctx.trk[t]['name']})" for t in tracks))
    L.append("보호: " + ", ".join([f"{t} ({ctx.trk[t]['name']})" for t in protected] + (["화성(화음표)"] if protect_harmony else [])) +
             " | 대상 밖의 모든 칸")
    L.append("바꿔도 되는 것: " + ", ".join(allow))
    for o in outs:
        if o["drift"]:
            L.append(f"주의: 디스크의 {o['composition']} 가 생성기의 지금 출력과 {len(o['drift'])}칸 다릅니다 "
                     f"(예: {o['drift'][0]['track']} @ {o['drift'][0]['section']}). apply 는 이 칸들을 디스크 그대로 두고 대상 칸만 옮겨 심습니다.")
    L.append("고칠 코드 자리 (지금 줄)")
    rows = []
    for s in target_sites:
        rows.append([f"{s['fn']}():{s['currentLine'] or '?'}", s["code"][:96], ",".join(t for t in s["tracks"] if t in tracks),
                     "이 구간만" if s["only"] else "다른 구간도: " + ", ".join(x for x in s["sections"] if x not in sections)])
    L += table(rows)
    used = sorted({s["helper"] for s in target_sites if s.get("helper")})
    if used:
        L.append("도우미: " + ", ".join(f"{h}() {funcs[h][0]}줄" for h in used if h in funcs))
    if helpers or consts:
        L.append("함께 쓰는 코드 (그대로 고치면 다른 구간도 바뀐다): " + "; ".join(f"{k} -> {', '.join(v)}" for k, v in list(helpers.items()) + list(consts.items())))
    if deps:
        L.append("의존: " + "; ".join(f"{','.join(d['from'])} {d['kind']} {d['to']}" + (f" ({d['via']})" if d.get("via") else "") for d in deps))
    L.append("검사 규칙")
    L += [f"  {i + 1}. {r}" for i, r in enumerate(rules)]
    L.append(f"다음: 생성기를 고친 뒤 music-ir verify {os.path.relpath(pdir, model.REPO)}")
    if getattr(args, "json", False):
        print(json.dumps(plan, ensure_ascii=False, indent=1))
    else:
        print("\n".join(L))
    return 0


# ---------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------
def compare_output(plan, base_dir, after_dir, name, script):
    """한 출력의 검사: (규칙 결과 목록, 바뀐 칸 목록)"""
    targets, protect = plan["targets"], plan["protect"]
    a_ir = model.load_json(os.path.join(base_dir, name[:-5] + ".ir.json"))
    b_ir = model.load_json(os.path.join(after_dir, name[:-5] + ".ir.json"))
    a, b = model.load_json(os.path.join(base_dir, name)), model.load_json(os.path.join(after_dir, name))
    results, changes = [], []

    def rule(ok, text, level="error"):
        results.append({"ok": ok, "level": level, "text": text})

    if "sections" not in b_ir:
        rule(False, f"새 IR 을 만들지 못했다: {b_ir.get('error')}")
        return results, changes
    secs = model.Sections(check.section_items(a_ir))
    sec_ids = [s["id"] for s in a_ir["sections"]]
    a_names = {t["id"]: t["name"] for t in a_ir["tracks"]}
    b_names = {t["id"]: t["name"] for t in b_ir["tracks"]}
    a_tracks = {t["name"]: t for t in a["tracks"]}
    b_tracks = {t["name"]: t for t in b["tracks"]}
    tset, sset = set(targets["tracks"]), set(targets["sections"])

    rule([s["id"] for s in b_ir["sections"]] == sec_ids and all(x["beats"] == y["beats"] for x, y in zip(a_ir["sections"], b_ir["sections"])),
         "구간 경계가 그대로")
    rule(model.short_hash(model.global_settings(a)) == model.short_hash(model.global_settings(b)), "곡 설정(템포, 박자, 버스, 마스터, 렌더)이 그대로")
    harmony_same = a_ir.get("harmony") == b_ir.get("harmony")
    rule(harmony_same, "화음표가 그대로" if harmony_same else "화음표가 바뀌었다")

    ids = sorted(set(a_names) | set(b_names), key=lambda x: (x not in a_names, x))
    out_of_scope, protected_bad = [], []
    for tid in ids:
        ta, tb = a_tracks.get(a_names.get(tid)), b_tracks.get(b_names.get(tid))
        if ta is None:
            ok = "new-tracks" in plan["allow"] and all(secs.at(float(n["startBeat"])) in sset for n in tb.get("notes", []))
            rule(ok, f"새 트랙 {tid}" + ("" if ok else " (허락하지 않았거나 대상 구간 밖에 음이 있다)"))
            changes.append({"track": tid, "section": "*", "what": "새 트랙"})
            continue
        if tb is None:
            rule(False, f"트랙 {tid} 이 사라졌다")
            continue
        if model.short_hash(model.track_settings(ta)) != model.short_hash(model.track_settings(tb)):
            ok = tid in tset and "settings" in plan["allow"]
            changes.append({"track": tid, "section": "*", "what": "트랙 설정"})
            if not ok:
                (protected_bad if tid in protect["tracks"] else out_of_scope).append(f"{tid} 설정")
        for sid in sec_ids + ["_before", "_after"]:
            d = cell_diff(ta, tb, secs, sid)
            if not (d["added"] or d["removed"] or d["modified"] or d["pointsAdded"] or d["pointsRemoved"]):
                continue
            changes.append(dict({"track": tid, "section": sid}, **d))
            if tid in protect["tracks"]:
                protected_bad.append(f"{tid} @ {sid}")
            elif tid not in tset or sid not in sset:
                out_of_scope.append(f"{tid} @ {sid}")
            else:
                if (d["added"] or d["removed"] or d["modified"]) and "notes" not in plan["allow"]:
                    out_of_scope.append(f"{tid} @ {sid} 음표 (허락하지 않음)")
                if (d["pointsAdded"] or d["pointsRemoved"]) and "cc" not in plan["allow"]:
                    out_of_scope.append(f"{tid} @ {sid} 레인 (허락하지 않음)")
    rule(not protected_bad, "보호 트랙이 그대로" + ("" if not protected_bad else ": 바뀐 칸 " + ", ".join(protected_bad[:8])))
    rule(not out_of_scope, "대상 밖이 그대로" + ("" if not out_of_scope else ": 바뀐 칸 " + ", ".join(out_of_scope[:8])))
    touched = any(c["track"] in tset and c["section"] in sset for c in changes)
    rule(touched, "대상 칸이 바뀌었다" if touched else "대상 칸이 바뀌지 않았다", level="warn")

    harmony = model.Harmony(b_ir.get("harmony"))
    if plan["protect"]["harmony"] and harmony.items:
        roles = {t["id"]: t for t in b_ir["tracks"]}
        for tid in targets["tracks"]:
            for sid in targets["sections"]:
                r = roles.get(tid, {})
                rr = set((r.get("roles") or {}).get(sid, r.get("role", [])))
                if not rr & HARMONIC_ROLES or not r.get("pitched", True):
                    continue
                before = a_ir["cells"].get(tid, {}).get(sid, {}).get("chordTones")
                after = b_ir["cells"].get(tid, {}).get(sid, {}).get("chordTones")
                if after is None:
                    continue
                ok = after >= 0.9 or (before is not None and after >= before - 0.02)
                rule(ok, f"{tid} @ {sid} 화음 구성음 비율 {before if before is not None else '-'} -> {after}")
    mel_ids = [t["id"] for t in b_ir["tracks"] if "main-melody" in t.get("role", [])]
    for sid in targets["sections"]:
        mel_a = [n for tid in mel_ids for n in model.cell_notes(a_tracks.get(a_names.get(tid), {}), secs, sid)[0]]
        mel_b = [n for tid in mel_ids for n in model.cell_notes(b_tracks.get(b_names.get(tid), {}), secs, sid)[0]]
        for tid in targets["tracks"]:
            if tid in mel_ids:
                continue
            na = model.cell_notes(a_tracks.get(a_names.get(tid), {}), secs, sid)[0] if a_names.get(tid) in a_tracks else []
            nb = model.cell_notes(b_tracks.get(b_names.get(tid), {}), secs, sid)[0] if b_names.get(tid) in b_tracks else []
            ca, cb = clashes(na, mel_a), clashes(nb, mel_b)
            if ca or cb:
                rule(cb <= ca, f"{tid} @ {sid} 선율과 단2도, 단9도로 부딪는 음 {ca} -> {cb}", level="warn")
    probs = check.integrity(b_ir, None, b, script)
    errs = [m for s, m in probs if s == "error"]
    rule(not errs, "새 IR 의 참조 무결성" + ("" if not errs else ": " + "; ".join(errs[:3])))
    for s, m in probs:
        if s == "warn":
            rule(False, m, level="warn")
    ok, msg = validate(os.path.join(after_dir, name))
    rule(ok is not False, "곡을 불러올 수 있다 (music-render validate)" + (f": {msg}" if msg else ""), level="error" if ok is False else "info")
    return results, changes


def cmd_verify(args):
    path = find_plan(args.plan)
    pdir = os.path.dirname(path)
    plan = model.load_json(path)
    folder = os.path.join(model.REPO, plan["folder"])
    after = os.path.join(pdir, "after")
    shutil.rmtree(after, ignore_errors=True)
    outputs = generate(folder, after)
    base = os.path.join(pdir, "baseline")
    report = {"plan": os.path.relpath(path, model.REPO), "outputs": {}, "ok": True}
    L = [f"검사: {os.path.relpath(path, model.REPO)}"]
    if plan.get("intent"):
        L.append(f"요청: {plan['intent']}")
    for name in outputs:
        if not os.path.exists(os.path.join(base, name)):
            L.append(f"{name}: 계획에 없던 출력")
            report["ok"] = False
            continue
        results, changes = compare_output(plan, base, after, name, os.path.join(folder, "compose.py"))
        ok = all(r["ok"] or r["level"] != "error" for r in results)
        report["outputs"][name] = {"ok": ok, "rules": results, "changes": changes}
        report["ok"] &= ok
        L.append(f"{name}: {'통과' if ok else '실패'}")
        if changes:
            L.append("  바뀐 칸")
            for c in changes:
                if c["section"] == "*":
                    L.append(f"    {c['track']}: {c['what']}")
                    continue
                parts = [f"음표 {c['before']} -> {c['after']}"]
                if c["added"] or c["removed"] or c["modified"]:
                    parts.append(f"+{c['added']} -{c['removed']} ~{c['modified']}")
                if c["pointsAdded"] or c["pointsRemoved"]:
                    parts.append(f"레인 {','.join(c['lanes'])} +{c['pointsAdded']} -{c['pointsRemoved']}점")
                L.append(f"    {c['track']} @ {c['section']}: " + ", ".join(parts))
        else:
            L.append("  바뀐 칸 없음")
        for r in results:
            mark = "ok  " if r["ok"] else ("실패" if r["level"] == "error" else "주의")
            if r["level"] == "info" and r["ok"]:
                mark = "ok  "
            L.append(f"  {mark} {r['text']}")
    kept = [t for t in plan["protect"]["tracks"]] + (["화음표"] if plan["protect"]["harmony"] else [])
    L.append(f"유지한 것: {', '.join(kept) or '-'}, 대상 밖의 모든 칸")
    L.append("결과: " + ("통과. music-ir apply 로 씁니다" if report["ok"] else "실패. 생성기를 고쳐 다시 verify"))
    plan["verified"] = {"ok": report["ok"], "generatorSha": model.file_sha(os.path.join(folder, "compose.py"))}
    with open(path, "w") as f:
        json.dump(plan, f, ensure_ascii=False, indent=1)
    if getattr(args, "json", False):
        print(json.dumps(report, ensure_ascii=False, indent=1))
    else:
        print("\n".join(L))
    return 0 if report["ok"] else 1


# ---------------------------------------------------------------------------
# apply
# ---------------------------------------------------------------------------
def transplant(disk, new, ir, targets):
    """disk 곡에서 대상 칸(트랙 x 구간)의 음표와 레인 점만 new 의 것으로 바꾼다"""
    secs = model.Sections(check.section_items(ir))
    names = {t["id"]: t["name"] for t in ir["tracks"]}
    sset = set(targets["sections"])
    by_disk = {t["name"]: t for t in disk["tracks"]}
    by_new = {t["name"]: t for t in new["tracks"]}
    for tid in targets["tracks"]:
        name = names.get(tid)
        td, tn = by_disk.get(name), by_new.get(name)
        if tn is None:
            continue
        if td is None:
            disk["tracks"].append(tn)
            continue
        inside = lambda beat: secs.at(float(beat)) in sset
        td["notes"] = sorted([n for n in td.get("notes", []) if not inside(n["startBeat"])] +
                             [n for n in tn.get("notes", []) if inside(n["startBeat"])],
                             key=lambda n: (float(n["startBeat"]), model.parse_pitch(n["pitch"])))
        lanes_new = model.track_lanes(tn)
        for lane in sorted(set(model.track_lanes(td)) | set(lanes_new)):
            holder = td["automation"] if lane in (td.get("automation") or {}) else td
            old = holder.get(lane, [])
            merged = [p for p in old if not inside(p["beat"])] + [p for p in lanes_new.get(lane, []) if inside(p["beat"])]
            merged.sort(key=lambda p: float(p["beat"]))
            if merged:
                holder[lane] = merged
            elif lane in holder:
                del holder[lane]
    return disk


def cmd_apply(args):
    from . import record
    path = find_plan(args.plan)
    pdir = os.path.dirname(path)
    plan = model.load_json(path)
    folder = os.path.join(model.REPO, plan["folder"])
    v = plan.get("verified")
    if not v or not v.get("ok"):
        raise SystemExit("verify 를 통과하지 않은 계획입니다")
    if v["generatorSha"] != model.file_sha(os.path.join(folder, "compose.py")):
        raise SystemExit("verify 뒤에 생성기가 바뀌었습니다. 다시 verify 하세요")
    after, disk_dir = os.path.join(pdir, "after"), os.path.join(pdir, "disk")
    for o in plan["outputs"]:
        name = o["composition"]
        cur, saved = os.path.join(folder, name), os.path.join(disk_dir, name)
        if os.path.exists(cur) and os.path.exists(saved) and model.composition_hash(model.load_json(cur)) != model.composition_hash(model.load_json(saved)):
            raise SystemExit(f"계획 뒤에 {name} 가 바뀌었습니다 (앱에서 저장했나요?). 새로 plan 하세요")
        new_ir = model.load_json(os.path.join(after, name[:-5] + ".ir.json"))
        if not o["drift"]:
            shutil.copy2(os.path.join(after, name), cur)
            shutil.copy2(os.path.join(after, name[:-5] + ".ir.json"), os.path.join(folder, name[:-5] + ".ir.json"))
            how = "생성기 출력 그대로"
        else:
            merged = transplant(model.load_json(cur), model.load_json(os.path.join(after, name)), new_ir, plan["targets"])
            with open(cur, "w") as f:
                json.dump(merged, f, ensure_ascii=False, indent=1)
                f.write("\n")
            new_ir["fingerprint"]["file"] = model.composition_hash(merged)
            record.write_ir(os.path.join(folder, name[:-5] + ".ir.json"), new_ir)
            how = f"대상 칸만 옮겨 심음 (생성기와 다른 {len(o['drift'])}칸은 디스크 그대로)"
        ok, msg = validate(cur)
        print(f"{os.path.relpath(cur, model.REPO)}: {how}, validate {'통과' if ok else '건너뜀' if ok is None else '실패: ' + msg}")
    return 0
