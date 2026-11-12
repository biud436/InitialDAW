"""작곡 생성기가 곡 JSON 과 함께 Music IR 을 쓴다.

생성기는 세 가지만 한다:
  1. 음표와 레인 점을 만들 때 trace(obj) 를 지난다. 그 음표를 만든 코드 자리(song() 의 문장과 도우미 함수)를 적는다.
  2. describe(ir) 에서 구간, 화성표, 트랙의 역할, 주제, 관계, 의존을 선언한다 (작곡할 때 이미 아는 뜻).
  3. json.dump 대신 save(path, data, describe) 로 쓴다. JSON 은 전과 같은 바이트로 쓰고, 곁에 <이름>.ir.json 을 쓴다.

IR 을 만들다 실패해도 JSON 은 이미 쓴 뒤다. 렌더링은 IR 을 읽지 않는다.
환경 변수: MUSICIR_OUT=<폴더> (그 폴더에 쓴다, 다시 생성 비교용), MUSICIR_IR_ONLY=1 (JSON 은 두고 IR 만), MUSICIR_FORCE=1 (덮어쓰기 막기를 넘는다),
MUSICIR_OFF=1 (IR 없이 예전처럼 JSON 만 쓴다).
"""
import datetime
import hashlib
import json
import linecache
import os
import shutil
import sys

from . import model

SCHEMA_VERSION = 1
_rec = None          # 지금 곡의 기록
_reading = set()     # 생성기가 읽은 파일 (보정표 등)
_quiet = [False]


def _audit(event, args):
    if event != "open" or _quiet[0]:
        return
    try:
        path, mode = args[0], args[1]
        if not isinstance(path, str) or not isinstance(mode, str) or any(c in mode for c in "wax+"):
            return
        full = os.path.abspath(path)
        if not full.startswith(model.REPO + os.sep) or full.endswith((".py", ".pyc")) or os.sep + "tools" + os.sep in full[len(model.REPO):]:
            return
        _reading.add(full)
    except Exception:
        pass


sys.addaudithook(_audit)


def _script():
    main = sys.modules.get("__main__")
    return os.path.abspath(getattr(main, "__file__", "") or "")


class _Recorder:
    """음표(dict)마다 만든 자리: id(obj) -> (obj, (함수, 줄, 도우미))"""

    def __init__(self, song_id):
        self.song_id = song_id
        self.script = _script()
        self.objs = {}
        self.files = {}

    def mine(self, code):
        f = code.co_filename
        hit = self.files.get(f)
        if hit is None:
            hit = self.files[f] = os.path.abspath(f) == self.script
        return hit

    def record(self, obj, frame):
        owner = getattr(frame.f_code, "co_qualname", frame.f_code.co_name)
        cls = owner.split(".")[0] + "." if "." in owner else None
        chain = []
        fr = frame.f_back
        while fr is not None:
            code = fr.f_code
            name = getattr(code, "co_qualname", code.co_name)
            if self.mine(code) and not (cls and name.startswith(cls)) and not code.co_name.startswith("<"):
                chain.append((name, fr.f_lineno))
            fr = fr.f_back
        chain = [c for c in chain if c[0] != "main"] or chain
        if not chain:
            return
        root = chain[-1]
        helper = chain[0][0] if len(chain) > 1 else None
        self.objs[id(obj)] = (obj, (root[0], root[1], helper))


def off():
    return os.environ.get("MUSICIR_OFF") == "1"


def start(song_id):
    """이 곡의 음표 출처를 적기 시작한다 (판이 여럿이면 판마다 부른다)"""
    global _rec
    _rec = None if off() else _Recorder(song_id)
    return _rec


def trace(obj):
    """음표나 레인 점(dict)을 그대로 돌려주며 만든 코드 자리를 적는다"""
    r = _rec
    if r is not None:
        try:
            r.record(obj, sys._getframe(1))
        except Exception:
            pass
    return obj


# ---------------------------------------------------------------------------
# 선언
# ---------------------------------------------------------------------------
ROLES = {"main-melody", "counter-melody", "answer", "motif", "harmony", "pad", "bass", "sub-bass", "arpeggio", "ostinato",
         "rhythm", "texture", "accent", "transition", "atmosphere", "doubling"}
USES = {"original", "transposed", "octave", "partial", "varied"}


def _roles(value):
    roles = [value] if isinstance(value, str) else list(value)
    bad = [r for r in roles if r not in ROLES]
    if bad:
        raise ValueError(f"모르는 역할 {bad}: {sorted(ROLES)}")
    return roles


class Song:
    """describe(ir) 이 받는 선언 모음. 박은 4분음표, 마디는 1부터 센다."""

    def __init__(self, data):
        self.data = data
        self.bb = model.bar_beats(data)
        self.info, self.sections, self.chords, self.tracks = {}, [], [], []
        self.motifs, self.uses, self.relations, self.deps, self.code_notes = [], [], [], [], []

    def beat(self, bar, beat=0.0):
        return (bar - 1) * self.bb + beat

    def song(self, key=None, genre=None, summary=None):
        self.info.update({k: v for k, v in (("key", key), ("genre", genre), ("summary", summary)) if v})

    def section(self, id, name, first_bar, last_bar, form=None, purpose=None, energy=None, trend=None, tension=None,
                key=None, progression=None, roman=None, aliases=(), note=None):
        """구간: first_bar 부터 last_bar 까지 (둘 다 포함). energy, tension 은 1~5 의 의도 (소리 크기가 아니다)"""
        s = {"id": id, "name": name, "bars": [first_bar, last_bar], "beats": [self.beat(first_bar), self.beat(last_bar + 1)]}
        for k, v in (("form", form), ("purpose", purpose), ("key", key), ("progression", progression), ("roman", roman), ("note", note)):
            if v:
                s[k] = v
        if energy is not None or trend:
            s["energy"] = {k: v for k, v in (("level", energy), ("trend", trend), ("tension", tension)) if v is not None}
        if aliases:
            s["aliases"] = list(aliases)
        self.sections.append(s)

    def harmony(self, segments, chords, names=None):
        """화음표: segments 는 [(시작 박, 길이, 화음 이름)], chords 는 {이름: (구성음의 음높이 클래스, 베이스)},
        names 는 생성기 안의 이름을 화음 기호로 바꿀 때 (예: {"D9": "Dadd9"})"""
        names = names or {}
        for start, length, name in segments:
            pcs, bass = chords[name]
            self.chords.append({"beat": round(start, 5), "beats": round(length, 5), "chord": names.get(name, name),
                                "tones": sorted(set(pcs)), "bass": bass})

    def track(self, id, name, role, roles=None, note=None):
        """트랙의 역할. name 은 JSON 의 트랙 이름, roles 는 {구간: 역할} (그 구간에서만 다를 때)"""
        t = {"id": id, "name": name, "role": _roles(role)}
        if roles:
            t["roles"] = {k: _roles(v) for k, v in roles.items()}
        if note:
            t["note"] = note
        self.tracks.append(t)

    def motif(self, id, name, notes=None, kind="motif", data=None, note=None):
        """주제나 동기. notes 는 [(동기 첫 박에서의 박, 길이, 음높이)], data 는 생성기 안의 데이터 이름"""
        m = {"id": id, "name": name, "kind": kind}
        if data:
            m["data"] = data
        if notes:
            m["notes"] = [[round(t, 5), round(d, 5), model.parse_pitch(p)] for t, d, p in notes]
            m["beats"] = round(max(t + d for t, d, _ in m["notes"]), 5)
        if note:
            m["note"] = note
        self.motifs.append(m)

    def use(self, motif, track, bar, how="original", transpose=0, part=None, note=None):
        """동기가 쓰인 자리. part=(첫 마디, 끝 마디 다음) 은 동기의 일부 마디만 쓸 때 (0부터 센다)"""
        if how not in USES:
            raise ValueError(f"모르는 쓰임 {how}: {sorted(USES)}")
        u = {"motif": motif, "track": track, "bar": bar, "how": how}
        if transpose:
            u["transpose"] = transpose
        if part:
            u["part"] = list(part)
        if note:
            u["note"] = note
        self.uses.append(u)

    def relate(self, a, b, kind, note=None):
        """구간 a 가 구간 b 에 대해 kind (restates, varies, recalls, prepares, contrasts, answers)"""
        self.relations.append({k: v for k, v in (("from", a), ("to", b), ("kind", kind), ("note", note)) if v})

    def depends(self, tracks, on, kind, via=None, note=None):
        """tracks 가 on(트랙 id 또는 harmony)에 기대는 관계. via 는 그 일을 하는 생성기의 함수 이름"""
        tracks = [tracks] if isinstance(tracks, str) else list(tracks)
        self.deps.append({k: v for k, v in (("from", tracks), ("to", on), ("kind", kind), ("via", via), ("note", note),
                                                 ("origin", "declared")) if v})

    def code(self, symbol, note, affects="all"):
        """여러 구간에 함께 듣는 생성기의 값이나 함수 (고치면 affects 가 모두 바뀐다)"""
        self.code_notes.append({"symbol": symbol, "note": note, "affects": affects})


# ---------------------------------------------------------------------------
# IR 만들기
# ---------------------------------------------------------------------------
def site_id(fn, code, occ):
    return hashlib.sha1(f"{fn}|{code}|{occ}".encode()).hexdigest()[:6]


def _sites(rec, data, sections, track_ids):
    """음표와 점마다 적은 자리를 모아 {자리 id: {...}} 와 {(트랙, 구간): {자리 id: 개수}}"""
    funcs = model.functions(rec.script)
    lines = open(rec.script).read().split("\n") if rec.script and os.path.exists(rec.script) else []
    sites, cells, keymap = {}, {}, {}

    def site_for(key):
        if key in keymap:
            return keymap[key]
        fn, line, helper = key
        code = linecache.getline(rec.script, line).strip()
        first = funcs.get(fn, (1, line))[0]
        occ = sum(1 for k in range(first, line) if k - 1 < len(lines) and lines[k - 1].strip() == code)
        sid = site_id(fn, code, occ)
        sites.setdefault(sid, {"fn": fn, "code": code, "occ": occ, "helper": helper,
                               "sections": set(), "tracks": set(), "notes": 0, "points": 0})
        keymap[key] = sid
        return sid

    for t in data.get("tracks", []):
        tid = track_ids.get(t["name"], t["name"])
        items = [(n, "notes") for n in t.get("notes", [])]
        items += [(p, "points") for pts in model.track_lanes(t).values() for p in pts]
        for obj, kind in items:
            hit = rec.objs.get(id(obj))
            if hit is None or hit[0] is not obj:
                continue
            sid = site_for(hit[1])
            sec = sections.at(float(obj["startBeat"] if kind == "notes" else obj["beat"]))
            s = sites[sid]
            s["sections"].add(sec)
            s["tracks"].add(tid)
            s[kind] += 1
            c = cells.setdefault((tid, sec), {})
            c[sid] = c.get(sid, 0) + 1
    order = sections.ids()
    for s in sites.values():
        s["sections"] = sorted(s["sections"], key=lambda x: order.index(x) if x in order else len(order))
        s["tracks"] = sorted(s["tracks"])
        if s["helper"] is None:
            del s["helper"]
    return sites, cells


def build(rec, data, song, composition_name):
    """선언(song)과 곡 데이터(data)로 IR 문서를 만든다"""
    tm = model.TempoMap(data["tempo"])
    bb = model.bar_beats(data)
    end = model.end_beat(data)
    names = {t["name"] for t in data.get("tracks", [])}
    declared = {t["name"]: t["id"] for t in song.tracks}
    track_ids = {n: declared.get(n, n) for n in names}
    section_items = [(s["id"], s["beats"][0], s["beats"][1]) for s in song.sections]
    sections = model.Sections(section_items)

    for s in song.sections:
        s["seconds"] = [round(tm.seconds(s["beats"][0]), 2), round(tm.seconds(s["beats"][1]), 2)]

    tracks = []
    by_name = {t["name"]: t for t in data.get("tracks", [])}
    for t in data.get("tracks", []):
        decl = next((d for d in song.tracks if d["name"] == t["name"]), None)
        entry = {"id": track_ids[t["name"]], "name": t["name"], "instrument": t.get("instrument"), "family": model.family(t.get("instrument"))}
        if not model.pitched(t.get("instrument")):
            entry["pitched"] = False
        if decl:
            entry.update({k: v for k, v in decl.items() if k not in ("id", "name")})
        else:
            entry["origin"] = "undeclared"
        tracks.append(entry)
    warnings = [f"선언한 트랙 '{d['name']}' 이 곡에 없다 (음표가 없어 빠졌다)" for d in song.tracks if d["name"] not in names]
    for a, b in zip(section_items, section_items[1:]):
        if abs(a[2] - b[1]) > model.EPS:
            warnings.append(f"구간 {a[0]} 과 {b[0]} 사이가 {'겹친다' if a[2] > b[1] else '비었다'}")
    if section_items and (section_items[0][1] > model.EPS or section_items[-1][2] < end - model.EPS):
        warnings.append(f"구간이 곡 전체(0~{end:g}박)를 덮지 않는다")

    # 동기의 쓰임: 선언한 자리에서 실제 음과 맞춰 본다
    motifs = []
    for m in song.motifs:
        m = dict(m)
        m["uses"] = []
        for u in song.uses:
            if u["motif"] != m["id"]:
                continue
            u = dict(u)
            start = song.beat(u["bar"])
            u["section"] = sections.at(start)
            name = next((t["name"] for t in tracks if t["id"] == u["track"]), None)
            if name is None:
                raise ValueError(f"동기 {m['id']} 의 트랙 {u['track']} 이 없다")
            score, extra = model.motif_match(m, by_name[name].get("notes", []), start, u.get("transpose", 0), u.get("part"), bb)
            if score is not None:
                u["match"] = score
                if extra:
                    u["extra"] = extra
            m["uses"].append(u)
        motifs.append(m)
    unknown = {u["motif"] for u in song.uses} - {m["id"] for m in song.motifs}
    if unknown:
        raise ValueError(f"선언하지 않은 동기: {sorted(unknown)}")

    # 의존: 선언한 것과 곡 데이터에서 읽은 것 (사이드체인, 센드)
    deps = list(song.deps)
    keyed = {}
    for t in data.get("tracks", []):
        for e in t.get("effects", []) or []:
            if e.get("sidechain"):
                keyed.setdefault(track_ids.get(e["sidechain"], e["sidechain"]), []).append(track_ids[t["name"]])
    for key, users in keyed.items():
        deps.append({"from": users, "to": key, "kind": "sidechain", "origin": "derived", "note": "이 트랙이 소리를 낼 때 눌린다"})
    cells_src = _sites(rec, data, sections, track_ids) if rec else ({}, {})
    sites, site_cells = cells_src
    ana = model.analyze(data, section_items, track_ids, song.chords)
    cells = {}
    for tid, info in ana["tracks"].items():
        for sid, st in info["cells"].items():
            c = dict(st)
            srcs = site_cells.get((tid, sid))
            if srcs:
                c["sites"] = [k for k, _ in sorted(srcs.items(), key=lambda kv: -kv[1])]
            cells.setdefault(tid, {})[sid] = c
    for t in tracks:
        t["settings"] = ana["tracks"][t["id"]]["settings"]

    script = rec.script if rec else _script()
    root = os.path.dirname(script)
    inputs = {}
    for path in sorted(_reading):
        inputs[os.path.relpath(path, model.REPO)] = model.file_sha(path)
    doc = {
        "musicIr": SCHEMA_VERSION,
        "song": dict({"id": rec.song_id if rec else os.path.basename(root), "title": data.get("title", ""), "composition": composition_name}, **song.info),
        "generator": {"script": os.path.basename(script), "sha256": model.file_sha(script), "inputs": inputs},
        "fingerprint": {"composition": ana["composition"], "global": ana["global"], "harmony": model.short_hash(song.chords)},
        "time": {"meter": data.get("timeSignature", [4, 4]), "barBeats": bb, "tempo": data["tempo"], "endBeat": end,
                 "endSeconds": round(tm.seconds(end), 2), "tailSeconds": (data.get("render") or {}).get("tailSeconds", 0)},
        "sections": song.sections,
        "harmony": song.chords,
        "tracks": tracks,
        "motifs": motifs,
        "relations": song.relations,
        "dependencies": deps,
        "code": song.code_notes,
        "cells": cells,
        "sites": {k: dict(v) for k, v in sorted(sites.items())},
    }
    if warnings:
        doc["warnings"] = warnings
    return doc


# ---------------------------------------------------------------------------
# 쓰기
# ---------------------------------------------------------------------------
def ir_path(json_path):
    base, _ = os.path.splitext(json_path)
    return base + ".ir.json"


def write_ir(path, doc):
    """한 줄에 한 항목: 구간, 화음, 트랙, 동기, 칸, 자리 (git diff 가 읽기 쉽게)"""
    def one(x):
        return json.dumps(x, ensure_ascii=False, separators=(", ", ": "))

    out = ["{"]
    keys = list(doc.keys())
    for i, k in enumerate(keys):
        v = doc[k]
        comma = "," if i + 1 < len(keys) else ""
        if isinstance(v, list) and v:
            out.append(f' "{k}": [')
            out += [f"  {one(x)}{',' if j + 1 < len(v) else ''}" for j, x in enumerate(v)]
            out.append(f" ]{comma}")
        elif isinstance(v, dict) and v and k in ("cells", "sites"):
            out.append(f' "{k}": {{')
            items = list(v.items())
            for j, (kk, vv) in enumerate(items):
                tail = "," if j + 1 < len(items) else ""
                if k == "cells":
                    out.append(f'  "{kk}": {{')
                    sub = list(vv.items())
                    out += [f'   "{a}": {one(b)}{"," if m + 1 < len(sub) else ""}' for m, (a, b) in enumerate(sub)]
                    out.append(f"  }}{tail}")
                else:
                    out.append(f'  "{kk}": {one(vv)}{tail}')
            out.append(f" }}{comma}")
        else:
            out.append(f' "{k}": {one(v)}{comma}')
    out.append("}")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def _refuse(path, ir_file, data_on_disk):
    """디스크의 JSON 이 지난번 생성 결과와 다르면 무엇이 다른지 적는다"""
    from . import check
    try:
        old = model.load_json(ir_file)
        diff = check.cell_changes(old, data_on_disk)
    except Exception as e:
        diff = [f"(비교 실패: {e})"]
    print(f"\n[music-ir] {os.path.basename(path)} 를 쓰지 않았습니다: 디스크의 JSON 이 이 생성기가 지난번에 쓴 것과 다릅니다.", file=sys.stderr)
    print("  앱에서 고쳤거나 다른 생성기 입력(보정표 등)으로 만든 판입니다. 다른 칸:", file=sys.stderr)
    for line in diff[:12]:
        print("   ", line, file=sys.stderr)
    if len(diff) > 12:
        print(f"    외 {len(diff) - 12}칸", file=sys.stderr)
    print("  고친 판을 지키려면 다른 이름으로 저장하세요. 덮어쓰려면 MUSICIR_FORCE=1 (지금 파일은 build/music-ir/backup 에 남깁니다).", file=sys.stderr)


def save(path, data, describe=None):
    """곡 JSON 을 쓰고 곁에 Music IR 을 쓴다. JSON 을 쓰지 않았으면 False"""
    out_dir = os.environ.get("MUSICIR_OUT")
    ir_only = os.environ.get("MUSICIR_IR_ONLY") == "1"
    force = os.environ.get("MUSICIR_FORCE") == "1" or "--force" in sys.argv
    target = os.path.join(out_dir, os.path.basename(path)) if out_dir else path
    if off():
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(target, "w") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")
        return True
    ir_file = ir_path(target)
    _quiet[0] = True
    try:
        if not ir_only and not out_dir and os.path.exists(path) and os.path.exists(ir_path(path)):
            on_disk = model.load_json(path)
            old = model.load_json(ir_path(path))
            recorded = (old.get("fingerprint") or {}).get("composition")
            if recorded and model.composition_hash(on_disk) != recorded:
                if not force:
                    _refuse(path, ir_path(path), on_disk)
                    return False
                backup = os.path.join(model.REPO, "build", "music-ir", "backup")
                os.makedirs(backup, exist_ok=True)
                stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
                shutil.copy2(path, os.path.join(backup, f"{os.path.splitext(os.path.basename(path))[0]}.{stamp}.json"))
    finally:
        _quiet[0] = False
    if not ir_only:
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
        with open(target, "w") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")
    try:
        song = Song(data)
        if describe:
            describe(song)
        doc = build(_rec, data, song, os.path.basename(path))
    except Exception as e:
        print(f"[music-ir] IR 을 만들지 못했습니다 ({type(e).__name__}: {e}). 곡 JSON 은 썼습니다.", file=sys.stderr)
        doc = {"musicIr": SCHEMA_VERSION, "error": f"{type(e).__name__}: {e}",
               "song": {"id": _rec.song_id if _rec else "", "title": data.get("title", ""), "composition": os.path.basename(path)},
               "generator": {"script": os.path.basename(_script()), "sha256": model.file_sha(_script()), "inputs": {}},
               "fingerprint": {"composition": model.composition_hash(data)}}
    if ir_only and not out_dir and os.path.exists(path):
        doc["fingerprint"]["file"] = model.composition_hash(model.load_json(path))
    try:
        write_ir(ir_file, doc)
    except Exception as e:
        print(f"[music-ir] IR 을 쓰지 못했습니다: {e}", file=sys.stderr)
        if os.path.exists(ir_file):
            os.remove(ir_file)
    return not ir_only
