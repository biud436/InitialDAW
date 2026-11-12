#!/usr/bin/env python3
"""Music IR 을 저장소의 임시 사본에서 끝까지 시험한다: 생성기 출력 보존, 스키마, 조회, 참조 무결성, 지문,
고칠 범위와 보호 대상 검사, 앱 편집을 지키는 덮어쓰기 막기와 옮겨 심기.

사용법: python3 tools/musicir/tests/test_music_ir.py [music-render 실행 파일]
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(os.path.dirname(HERE))
REPO = os.path.dirname(TOOLS)
RENDER = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(REPO, "build", "engine", "music-render")
sys.path.insert(0, TOOLS)
from musicir import check, infer, model, schema  # noqa: E402

FAILS = []
CHECKS = 0


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("ok   " if cond else "FAIL ") + name + ("" if cond or not detail else "  | " + str(detail)[:800]))
    if not cond:
        FAILS.append(name)


class Mirror:
    """저장소의 임시 사본: tools 와 곡은 복사, engine 과 music-render 는 링크"""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix="music-ir-test-")
        shutil.copytree(TOOLS, os.path.join(self.root, "tools"), ignore=shutil.ignore_patterns("__pycache__"))
        os.symlink(os.path.join(REPO, "engine"), os.path.join(self.root, "engine"))
        os.makedirs(os.path.join(self.root, "build", "engine"))
        if os.path.exists(RENDER):
            os.symlink(RENDER, os.path.join(self.root, "build", "engine", "music-render"))
        for song in SONGS:
            shutil.copytree(os.path.join(REPO, "songs", song), os.path.join(self.root, "songs", song), ignore=shutil.ignore_patterns("__pycache__"))

    def path(self, *parts):
        return os.path.join(self.root, *parts)

    def compose(self, song, **env):
        e = dict(os.environ)
        for k in ("MUSICIR_OUT", "MUSICIR_IR_ONLY", "MUSICIR_FORCE", "MUSICIR_OFF"):
            e.pop(k, None)
        e.update(env)
        return subprocess.run([sys.executable, self.path("songs", song, "compose.py")], capture_output=True, text=True, env=e, cwd=self.root)

    def ir(self, *args):
        p = subprocess.run([sys.executable, self.path("tools", "music-ir.py"), *args], capture_output=True, text=True, cwd=self.root)
        return p.returncode, p.stdout, p.stderr

    def ir_json(self, *args):
        rc, out, err = self.ir("--json", *args)
        try:
            return rc, json.loads(out)
        except json.JSONDecodeError:
            return rc, {"_raw": out, "_err": err}

    def edit(self, song, old, new, count=1):
        p = self.path("songs", song, "compose.py")
        s = open(p).read()
        assert s.count(old) == count, f"{old!r} 가 {s.count(old)}번 있다"
        open(p, "w").write(s.replace(old, new))

    def restore(self, song, name="compose.py"):
        shutil.copy2(os.path.join(REPO, "songs", song, name), self.path("songs", song, name))

    def load(self, song, name):
        return model.load_json(self.path("songs", song, name))

    def save(self, song, name, data):
        with open(self.path("songs", song, name), "w") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
            f.write("\n")


def track(data, name):
    return next(t for t in data["tracks"] if t["name"] == name)


def validate(path):
    if not os.path.exists(RENDER):
        return None
    p = subprocess.run([RENDER, "validate", path], capture_output=True, text=True)
    return json.loads(p.stdout)


# ---------------------------------------------------------------------------
def test_generator_output(m):
    """1. 기존 JSON 호환: IR 을 붙인 생성기가 전과 같은 바이트를 쓴다. 2. 렌더러는 곁 파일과 무관하다"""
    p = m.compose("glass-circuit")
    ok("glass-circuit 생성기가 돈다", p.returncode == 0, p.stderr)
    same = open(m.path("songs", "glass-circuit", "glass_circuit.json"), "rb").read() == open(os.path.join(REPO, "songs", "glass-circuit", "glass_circuit.json"), "rb").read()
    ok("glass-circuit: IR 을 붙인 생성기의 JSON 이 커밋된 JSON 과 바이트까지 같다", same)
    on, off = tempfile.mkdtemp(), tempfile.mkdtemp()
    for song in ("glass-circuit", "rusted-wings"):
        a = m.compose(song, MUSICIR_OUT=on)
        b = m.compose(song, MUSICIR_OUT=off, MUSICIR_OFF="1")
        ok(f"{song}: IR 을 켜고 끈 생성기가 모두 돈다", a.returncode == 0 and b.returncode == 0, a.stderr + b.stderr)
    for f in sorted(os.listdir(off)):
        ok(f"{f}: IR 을 켜도 끈 것과 바이트까지 같다", open(os.path.join(on, f), "rb").read() == open(os.path.join(off, f), "rb").read())
    ok("IR 을 끄면 곁 파일을 쓰지 않는다", not [f for f in os.listdir(off) if f.endswith(".ir.json")])
    ok("IR 을 켜면 판마다 곁 파일을 쓴다", sorted(f for f in os.listdir(on) if f.endswith(".ir.json")) ==
       ["glass_circuit.ir.json", "rusted_wings.ir.json", "rusted_wings2.ir.json"])
    if os.path.exists(RENDER):
        for f in ("glass_circuit.json", "rusted_wings.json", "rusted_wings2.json"):
            v = validate(os.path.join(on, f))
            ok(f"{f}: music-render validate 통과", v.get("ok") is True, v.get("errors"))
        src = m.path("songs", "glass-circuit", "glass_circuit.json")
        with_ir = validate(src)
        lone = tempfile.mkdtemp()
        shutil.copy2(src, lone)
        without = validate(os.path.join(lone, "glass_circuit.json"))
        ok("곁 파일이 있어도 없어도 validate 요약이 같다", with_ir["summary"] == without["summary"])
    else:
        print("skip music-render 가 없어 validate 를 건너뜀")
    for d in (on, off):
        shutil.rmtree(d)


def test_schema(m):
    """3. IR 스키마"""
    for song, name in (("glass-circuit", "glass_circuit.ir.json"), ("rusted-wings", "rusted_wings.ir.json"), ("rusted-wings", "rusted_wings2.ir.json")):
        errs = schema.errors(m.load(song, name))
        ok(f"{name}: 스키마 통과", not errs, errs[:5])
    inf = infer.infer(m.load("dark-forest", "dark_forest.json"), "dark_forest.json")
    ok("추론한 IR 도 스키마 통과", not schema.errors(inf), schema.errors(inf)[:5])
    bad = m.load("glass-circuit", "glass_circuit.ir.json")
    bad["tracks"][0]["role"] = ["lead-guitar"]
    del bad["sections"][0]["beats"]
    errs = schema.errors(bad)
    ok("스키마가 모르는 역할을 잡는다", any("lead-guitar" in e for e in errs), errs)
    ok("스키마가 빠진 필드를 잡는다", any("beats 가 없다" in e for e in errs), errs)


def test_queries(m):
    """4. 구간과 트랙 조회의 정확성"""
    data = m.load("glass-circuit", "glass_circuit.json")
    rc, res = m.ir_json("section", "glass-circuit", "절정")
    s = res.get("section", {})
    ok("별칭으로 구간을 찾는다 (절정 -> p1)", rc == 0 and s.get("id") == "p1", res)
    ok("p1 은 49~56마디, 박 192~224", s.get("bars") == [49, 56] and s.get("beats") == [192.0, 224.0], s)
    ok("p1 의 시작 초는 192 x 60 / 110", abs(s.get("seconds", [0])[0] - 192 * 60 / 110) < 0.01, s.get("seconds"))
    mel = [n for n in track(data, "Piano Melody")["notes"] if 192 <= n["startBeat"] < 224]
    ok("p1 선율 칸의 음표 수가 JSON 을 직접 센 것과 같다", res["cells"]["melody"]["n"] == len(mel), (res["cells"]["melody"]["n"], len(mel)))
    ok("p1 선율 칸의 음역이 JSON 과 같다", res["cells"]["melody"]["pitch"] == [min(n["pitch"] for n in mel), max(n["pitch"] for n in mel)])
    ok("p1 의 첫 화음은 49:1 Dadd9", res["harmony"][0] == "49:1 Dadd9", res["harmony"][:2])
    ok("p1 에서 주제 A 가 옥타브로 일치 1.0", any(u["motif"] == "theme-a" and u["how"] == "octave" and u["match"] == 1.0 for u in res["motifs"]), res["motifs"])
    rc, res = m.ir_json("section", "glass-circuit", "last:climax")
    ok("last:climax -> p2", res.get("section", {}).get("id") == "p2")
    rc, res = m.ir_json("tracks", "glass-circuit", "--track", "family:strings")
    ok("glass-circuit 의 현악 갈래는 air 하나", [t["id"] for t in res] == ["air"], res)
    rc, res = m.ir_json("tracks", "rusted-wings", "--track", "family:strings")
    ok("rusted-wings 의 현악 갈래는 vln, vla, vc, cb", [t["id"] for t in res] == ["vln", "vla", "vc", "cb"], res)
    rc, res = m.ir_json("section", "rusted-wings", "마지막 코러스")
    ok("rusted-wings: 마지막 코러스 -> final 61~76마디", res.get("section", {}).get("id") == "final" and res["section"]["bars"] == [61, 76])
    rc, res = m.ir_json("notes", "glass-circuit", "--section", "p2", "--track", "air")
    want = [n for n in track(data, "Air")["notes"] if 224 <= n["startBeat"] < 256]
    ok("notes 가 구간의 음표를 그대로 준다", res["air"]["notes"] == want, res.get("air"))
    rc, res = m.ir_json("source", "glass-circuit", "--section", "p2", "--track", "air")
    lines = open(m.path("songs", "glass-circuit", "compose.py")).read().split("\n")
    real = next(i + 1 for i, line in enumerate(lines) if 'P["air"].add(p, b(49 + k)' in line)
    site = next((r for r in res if "P[\"air\"].add(p, b(49 + k)" in r["code"]), None)
    ok("source 가 air 음표를 만든 문장과 지금 줄을 준다", site is not None and site["currentLine"] == real, (site, real))
    rc, res = m.ir_json("deps", "rusted-wings", "--section", "final", "--track", "family:strings")
    ok("deps: 현악은 화음표를 따른다", any(d["kind"] == "follows-harmony" and d["to"] == "harmony" for d in res["dependsOn"]), res.get("dependsOn"))
    ok("deps: 현악은 clear_voice 로 선율을 피한다", any(d["kind"] == "avoids-clash" and d.get("via") == "clear_voice" for d in res["dependsOn"]))
    ok("deps: string_pad 를 다른 구간도 쓴다", "string_pad" in res["sharedHelpers"] and "chorus1" in res["sharedHelpers"]["string_pad"], res.get("sharedHelpers"))
    rc, res = m.ir_json("compare", "glass-circuit", "a1", "p1")
    ok("compare: a1 과 p1 의 진행이 같다", res.get("harmonySame") is True)
    rc, res = m.ir_json("compare", "glass-circuit", "a1", "b")
    ok("compare: a1 과 b 의 진행은 다르다", res.get("harmonySame") is False)
    ir = m.load("rusted-wings", "rusted_wings.ir.json")
    v = validate(m.path("songs", "rusted-wings", "rusted_wings.json"))
    if v:
        est = v["summary"]["estimatedDuration"]
        ok("루바토 템포 지도의 길이가 엔진과 같다", abs(ir["time"]["endSeconds"] + ir["time"]["tailSeconds"] - est) < 0.02,
           (ir["time"]["endSeconds"], est))
    rc, out, _ = m.ir("overview", "dark-forest")
    ok("IR 이 없는 곡은 추론으로 요약한다", rc == 0 and "추론" in out and "신뢰도" in out, out[:300])
    rc, out, _ = m.ir("harmony", "dark-forest")
    ok("추론한 IR 은 화성을 지어내지 않는다", "추론하지 않습니다" in out, out)


def test_integrity(m):
    """5. 참조 무결성"""
    for song in ("glass-circuit", "rusted-wings"):
        rc, res = m.ir_json("check", song)
        errs = [p for p in res.get("problems", []) if p[0] == "error"]
        ok(f"{song}: 새 IR 에 참조 오류가 없다", not errs, errs)
    data = m.load("glass-circuit", "glass_circuit.json")
    track(data, "Air")["name"] = "High Air"
    tmp = m.path("songs", "glass-circuit", "renamed.json")
    with open(tmp, "w") as f:
        json.dump(data, f)
    rc, res = m.ir_json("--ir", m.path("songs", "glass-circuit", "glass_circuit.ir.json"), "check", tmp)
    probs = [p[1] for p in res.get("problems", [])]
    ok("이름이 바뀐 트랙을 잡고 새 이름 후보를 준다", any("'Air'" in p and "High Air" in p for p in probs), probs)
    os.remove(tmp)
    ir = m.load("glass-circuit", "glass_circuit.ir.json")
    for mo in ir["motifs"]:
        for u in mo["uses"]:
            if mo["id"] == "theme-a" and u["bar"] == 49:
                u["bar"] = 50
    probs = check.integrity(ir, m.path("songs", "glass-circuit", "glass_circuit.ir.json"), m.load("glass-circuit", "glass_circuit.json"))
    ok("동기의 자리가 실제 음과 맞지 않으면 잡는다", any("theme-a" in p[1] and "50마디" in p[1] for p in probs), probs)


def test_stale(m):
    """6. 곡이 바뀐 뒤의 낡은 IR"""
    ir = m.load("glass-circuit", "glass_circuit.ir.json")
    data = m.load("glass-circuit", "glass_circuit.json")
    note = next(n for n in track(data, "Piano Melody")["notes"] if n["startBeat"] >= 196)
    note["velocity"] += 5
    cells = check.compare_cells(ir, data)
    ok("음표 하나를 고치면 그 칸 하나만 다르다 (melody @ p1)", [(c["track"], c["section"]) for c in cells] == [("melody", "p1")], cells)
    data = m.load("glass-circuit", "glass_circuit.json")
    track(data, "Pad")["mixer"]["gainDb"] -= 1
    cells = check.compare_cells(ir, data)
    ok("트랙 설정을 고치면 설정 칸만 다르다", [(c["track"], c["section"]) for c in cells] == [("pad", "*")], cells)
    f = check.freshness(ir, m.path("songs", "glass-circuit", "glass_circuit.ir.json"), m.load("glass-circuit", "glass_circuit.json"))
    ok("지금은 JSON 과 생성기가 IR 과 같다", f["json"] == "same" and f["generator"] == "same" and not f["inputs"], f)
    with open(m.path("songs", "glass-circuit", "compose.py"), "a") as fh:
        fh.write("# 시험\n")
    rc, res = m.ir_json("check", "glass-circuit")
    ok("생성기가 바뀌면 check 가 알린다", rc == 1 and res["freshness"]["generator"] == "changed", res.get("freshness"))
    m.restore("glass-circuit")
    ir2 = json.loads(json.dumps(ir))
    key = next(iter(ir2["generator"]["inputs"]))
    ir2["generator"]["inputs"][key] = "0" * 64
    f = check.freshness(ir2, m.path("songs", "glass-circuit", "glass_circuit.ir.json"), m.load("glass-circuit", "glass_circuit.json"))
    ok("생성기가 읽은 파일(보정표)이 바뀌면 알린다", f["inputs"] == [key], f)
    ok("생성기가 읽은 보정표를 입력으로 적었다", key == "engine/instruments/calibration/grand-piano.json", key)
    rc, res = m.ir_json("check", "rusted-wings")
    ok("rusted-wings: 커밋된 JSON 이 지금 생성기 출력과 다른 것(보정표 변경)을 divergent 로 알린다",
       res["freshness"]["json"] == "divergent" and res["freshness"]["cells"], res.get("freshness", {}).get("json"))


AIR_P2 = '''    for k in (0, 8):
        for p in ("A5", "E6"):
            P["air"].add(p, b(49 + k), 31.0 if k == 0 else 28.0, 84)
'''
AIR_P2_MORE = AIR_P2 + '''    for p in ("F#5", "C#6"):
        P["air"].add(p, b(57), 28.0, 72)
'''


def test_scope(m):
    """7. 고칠 범위, 8. 보호 대상"""
    rc, out, err = m.ir("plan", "glass-circuit", "--sections", "p2", "--tracks", "air", "--protect", "melody,harmony")
    ok("plan 을 세운다", rc == 0 and "대상 구간: p2" in out, out + err)
    ok("plan 이 고칠 코드 자리를 지금 줄로 준다", re.search(r"song\(\):\d+\s+P\[\"air\"\]\.add", out) is not None, out)
    m.edit("glass-circuit", AIR_P2, AIR_P2_MORE)
    rc, res = m.ir_json("verify", "glass-circuit")
    out = res["outputs"]["glass_circuit.json"]
    ok("대상 칸만 고치면 verify 통과", rc == 0 and out["ok"], [r for r in out["rules"] if not r["ok"]])
    ok("바뀐 칸은 air @ p2 하나, 음표 2개 더함", [(c["track"], c["section"], c.get("added")) for c in out["changes"]] == [("air", "p2", 2)], out["changes"])
    rc, out_, err = m.ir("apply", "glass-circuit")
    ok("apply 가 생성기 출력을 쓴다", rc == 0 and "생성기 출력 그대로" in out_, out_ + err)
    rc, res = m.ir_json("check", "glass-circuit")
    ok("apply 뒤 check 통과", rc == 0 and res["ok"], res.get("freshness"))
    a = len([n for n in track(m.load("glass-circuit", "glass_circuit.json"), "Air")["notes"] if n["startBeat"] >= 224])
    ok("곡 JSON 의 air @ p2 에 음이 둘 늘었다", a == 4, a)
    m.restore("glass-circuit")
    m.restore("glass-circuit", "glass_circuit.json")
    m.restore("glass-circuit", "glass_circuit.ir.json")

    m.ir("plan", "glass-circuit", "--sections", "p2", "--tracks", "air", "--protect", "melody,harmony")
    m.edit("glass-circuit", "play_melody(mel, 57, THEME_A2, ARCH_A2, lift=8, octave=True)", "play_melody(mel, 57, THEME_A2, ARCH_A2, lift=12, octave=True)")
    rc, res = m.ir_json("verify", "glass-circuit")
    bad = [r["text"] for r in res["outputs"]["glass_circuit.json"]["rules"] if not r["ok"] and r["level"] == "error"]
    ok("보호한 선율을 바꾸면 verify 실패", rc == 1 and any("보호 트랙" in t and "melody @ p2" in t for t in bad), bad)
    m.restore("glass-circuit")
    m.edit("glass-circuit", AIR_P2, AIR_P2 + '''    P["air"].add("B5", b(25), 8.0, 70)\n''')
    rc, res = m.ir_json("verify", "glass-circuit")
    bad = [r["text"] for r in res["outputs"]["glass_circuit.json"]["rules"] if not r["ok"] and r["level"] == "error"]
    ok("대상 구간 밖(air @ b)을 바꾸면 verify 실패", rc == 1 and any("대상 밖" in t and "air @ b" in t for t in bad), bad)
    m.restore("glass-circuit")
    m.edit("glass-circuit", 'PROG_CAD = ["Gmaj9", "Gm6", "D9", "D9"]', 'PROG_CAD = ["Gmaj9", "Gm6", "D9", "E/D"]')
    rc, res = m.ir_json("verify", "glass-circuit")
    bad = [r["text"] for r in res["outputs"]["glass_circuit.json"]["rules"] if not r["ok"] and r["level"] == "error"]
    ok("화음표를 바꾸면 verify 실패", rc == 1 and "화음표가 바뀌었다" in bad, bad)
    m.restore("glass-circuit")
    rc, out, err = m.ir("apply", "glass-circuit")
    ok("verify 를 통과하지 않은 계획은 apply 하지 않는다", rc != 0, out + err)


def test_preserve(m):
    """9. 생성기를 다시 돌려도 앱에서 고친 것이 남는다"""
    jp = m.path("songs", "glass-circuit", "glass_circuit.json")
    data = m.load("glass-circuit", "glass_circuit.json")
    note = next(n for n in track(data, "Piano Melody")["notes"] if n["startBeat"] >= 196)
    note["velocity"] = 33
    m.save("glass-circuit", "glass_circuit.json", data)
    edited = open(jp, "rb").read()
    p = m.compose("glass-circuit")
    ok("앱에서 고친 JSON 을 생성기가 덮어쓰지 않는다", open(jp, "rb").read() == edited and "쓰지 않았습니다" in p.stderr, p.stderr[-400:])
    ok("막은 이유에 고친 칸(melody @ p1)을 적는다", "melody @ p1" in p.stderr, p.stderr[-400:])

    rc, out, err = m.ir("plan", "glass-circuit", "--sections", "p2", "--tracks", "air", "--protect", "melody,harmony")
    ok("plan 이 앱 편집 칸을 알린다", "1칸 다릅니다" in out, out)
    m.edit("glass-circuit", AIR_P2, AIR_P2_MORE)
    rc, res = m.ir_json("verify", "glass-circuit")
    ok("앱 편집이 있어도 생성기 수정만 견주어 verify 통과", rc == 0, res)
    rc, out, err = m.ir("apply", "glass-circuit")
    ok("apply 가 대상 칸만 옮겨 심는다", rc == 0 and "옮겨 심음" in out, out + err)
    after = m.load("glass-circuit", "glass_circuit.json")
    kept = next(n for n in track(after, "Piano Melody")["notes"] if n["startBeat"] == note["startBeat"] and n["pitch"] == note["pitch"])
    ok("앱에서 고친 선율의 벨로시티가 남았다", kept["velocity"] == 33, kept)
    ok("air @ p2 에 새 음이 들어왔다", len([n for n in track(after, "Air")["notes"] if n["startBeat"] >= 224]) == 4)
    rc, res = m.ir_json("check", "glass-circuit")
    ok("옮겨 심은 뒤 check 는 앱 편집 칸만 다르다고 한다", res["freshness"]["json"] == "divergent" and
       [(c["track"], c["section"]) for c in res["freshness"]["cells"]] == [("melody", "p1")], res.get("freshness"))
    p = m.compose("glass-circuit", MUSICIR_FORCE="1")
    backup = m.path("build", "music-ir", "backup")
    ok("MUSICIR_FORCE 로 덮어쓰면 지금 파일을 남겨 둔다", p.returncode == 0 and os.path.isdir(backup) and os.listdir(backup), p.stderr)
    for f in ("compose.py", "glass_circuit.json", "glass_circuit.ir.json"):
        m.restore("glass-circuit", f)

    # 커밋된 JSON 이 지금 생성기 출력과 다른 곡(보정표가 바뀐 녹슨 날개): 현악만 고쳐도 선율은 커밋된 그대로
    before = m.load("rusted-wings", "rusted_wings.json")
    rc, out, err = m.ir("plan", "rusted-wings", "--sections", "마지막 코러스", "--tracks", "family:strings", "--protect", "role:main-melody,piano,harmony")
    ok("rusted-wings: plan 이 보정표 차이를 알린다", rc == 0 and "20칸 다릅니다" in out, out + err)
    m.edit("rusted-wings", "    string_pad(P, fc, cb=True)\n",
           "    string_pad(P, fc, cb=True)\n    for start, dur, chord in fc:\n        for p in voicing(chord, 74, 83, 2):\n            P[\"vln\"].add(p, start, dur, 60, \"sustain\")\n")
    rc, res = m.ir_json("verify", "rusted-wings")
    ok("rusted-wings: 바이올린을 더한 생성기가 두 판 모두 verify 통과", rc == 0, {k: [r for r in v["rules"] if not r["ok"]] for k, v in res.get("outputs", {}).items()})
    rc, out, err = m.ir("apply", "rusted-wings")
    ok("rusted-wings: apply 가 대상 칸만 옮겨 심는다", rc == 0 and out.count("옮겨 심음") == 2, out + err)
    after = m.load("rusted-wings", "rusted_wings.json")
    ok("rusted-wings: 선율 트랙이 커밋된 JSON 과 같다 (보정표 차이가 새지 않았다)", track(after, "Voice (Clarinet)") == track(before, "Voice (Clarinet)"))
    ok("rusted-wings: 피아노 트랙이 커밋된 JSON 과 같다", track(after, "Piano") == track(before, "Piano"))
    va = [n for n in track(after, "Violins")["notes"] if 240 <= n["startBeat"] < 304]
    vb = [n for n in track(before, "Violins")["notes"] if 240 <= n["startBeat"] < 304]
    ok("rusted-wings: 마지막 코러스의 바이올린이 늘었다", len(va) > len(vb), (len(vb), len(va)))
    ok("rusted-wings: 마지막 코러스 밖의 바이올린은 그대로",
       [n for n in track(after, "Violins")["notes"] if not 240 <= n["startBeat"] < 304] == [n for n in track(before, "Violins")["notes"] if not 240 <= n["startBeat"] < 304])
    v = validate(m.path("songs", "rusted-wings", "rusted_wings.json"))
    if v:
        ok("rusted-wings: 옮겨 심은 곡이 validate 통과", v["ok"] is True, v.get("errors"))
    for f in ("compose.py", "rusted_wings.json", "rusted_wings2.json", "rusted_wings.ir.json", "rusted_wings2.ir.json"):
        m.restore("rusted-wings", f)


def test_ir_failure(m):
    """IR 을 만들다 실패해도 곡 JSON 은 쓴다"""
    m.edit("glass-circuit", '    ir.use("cadence", "melody", S["cad"])\n', '    ir.use("cadence", "melody", S["cad"])\n    ir.use("nothing", "melody", 1)\n')
    out = tempfile.mkdtemp()
    p = m.compose("glass-circuit", MUSICIR_OUT=out)
    ok("IR 이 실패해도 생성기는 성공한다", p.returncode == 0 and "IR 을 만들지 못했습니다" in p.stderr, p.stderr)
    ok("IR 이 실패해도 곡 JSON 은 같은 바이트", open(os.path.join(out, "glass_circuit.json"), "rb").read() ==
       open(os.path.join(REPO, "songs", "glass-circuit", "glass_circuit.json"), "rb").read())
    bad = model.load_json(os.path.join(out, "glass_circuit.ir.json"))
    ok("실패한 IR 에는 오류와 지문만 남는다", "error" in bad and "nothing" in bad["error"] and bad["fingerprint"]["composition"], bad.get("error"))
    m.restore("glass-circuit")
    shutil.rmtree(out)


SONGS = ("glass-circuit", "rusted-wings", "dark-forest")
SKIP = 77   # CTest 의 SKIP_RETURN_CODE


def main():
    missing = [s for s in SONGS if not os.path.isdir(os.path.join(REPO, "songs", s))]
    if missing:
        print("시험에 쓰는 곡이 songs/ 에 없어 건너뜁니다:", ", ".join(missing))
        sys.exit(SKIP)
    m = Mirror()
    try:
        for t in (test_generator_output, test_schema, test_queries, test_integrity, test_stale, test_scope, test_preserve, test_ir_failure):
            print(f"-- {t.__name__}: {t.__doc__.strip().splitlines()[0]}")
            t(m)
    finally:
        shutil.rmtree(m.root, ignore_errors=True)
    print(f"\n{CHECKS - len(FAILS)}/{CHECKS} 통과")
    if FAILS:
        print("실패:", *FAILS, sep="\n  ")
        sys.exit(1)


if __name__ == "__main__":
    main()
