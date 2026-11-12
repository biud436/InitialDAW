#!/usr/bin/env python3
"""엔진과 앱의 결과를 기준선으로 저장하고, 나중의 결과를 기준선과 비교한다.

JUCE 를 걷어내는 동안(docs/plans/own-framework.md) 결과가 바뀌지 않았는지 확인하는 도구다. 곡마다 검사 결과,
32비트 렌더링, MIDI 와 MusicXML 내보내기, 저장 형식 왕복을 기록하고, 내보내기 형식과 앱 화면 PNG 의 해시를 기록한다.

샘플 악기는 sfizz 가 샘플을 뒤에서 읽는 시점에 따라 같은 곡도 -150 dB 남짓 다르게 렌더링될 수 있다 (D8 계획 5절).
그래서 렌더링은 파일로 보관하고(build/baseline/renders), 해시가 다르면 샘플 차이를 재서 TOLERANCE_DB 아래면 같다고 본다.
정밀한 비교는 engine/tests/test_parity.cpp 가 JUCE 구현과 비트 단위로 한다.

사용법:
  tools/baseline.py make  [--out build/baseline] [--build build] [--only <이름 일부>]...
  tools/baseline.py check [--out build/baseline] [--build build] [--only <이름 일부>]...
  tools/baseline.py diff <곡.json> [--out build/baseline] [--build build]   # 기준 엔진과 지금 엔진의 렌더링 차이
"""
import argparse
import glob
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP_REL = "app/InitialDAW_artefacts/Release/InitialDAW.app/Contents/MacOS/InitialDAW"
VOLATILE = {"file", "output", "renderSeconds", "stems", "report"}
TOLERANCE_DB = -90.0   # 렌더링의 샘플 차이가 이 크기(dBFS 피크) 아래면 같다고 본다 (같은 바이너리로 「늦은 편지」가 -97.2 dB 까지 달랐다)


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def stable(value):
    """경로와 걸린 시간처럼 실행마다 바뀌는 값을 뺀다."""
    if isinstance(value, dict):
        return {k: stable(v) for k, v in value.items() if k not in VOLATILE}
    if isinstance(value, list):
        return [stable(v) for v in value]
    return value


def run(cmd, cwd=ROOT):
    p = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    try:
        data = json.loads(p.stdout) if p.stdout.strip().startswith("{") else None
    except json.JSONDecodeError:
        data = None
    return p.returncode, data, p


def read_float_wav(path):
    """32비트 부동소수점 WAV 의 샘플(인터리브)과 채널 수. numpy 가 없으면 None."""
    try:
        import numpy as np
    except ImportError:
        return None, 0
    data = open(path, "rb").read()
    pos, channels, samples = 12, 0, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            channels = struct.unpack("<H", body[2:4])[0]
        elif cid == b"data":
            samples = np.frombuffer(body, dtype="<f4")
        pos += 8 + size + (size & 1)
    return samples, channels


def levels(path):
    import math
    samples, channels = read_float_wav(path)
    if samples is None or not channels or samples.size == 0:
        return {}
    import numpy as np
    peak = float(np.max(np.abs(samples)))
    rms = float(np.sqrt(np.mean(samples.astype(np.float64) ** 2)))
    db = lambda x: round(20 * math.log10(x), 4) if x > 0 else None
    return {"frames": int(samples.size // channels), "peakDb": db(peak), "rmsDb": db(rms)}


def songs(only):
    files = sorted(glob.glob(os.path.join(ROOT, "songs", "**", "*.json"), recursive=True))
    files += sorted(glob.glob(os.path.join(ROOT, "engine", "examples", "*.json")))
    files += sorted(glob.glob(os.path.join(ROOT, "templates", "*.json")))
    files = [f for f in files if not f.endswith(".ir.json") and not os.path.basename(f).startswith("groove")]
    out = []
    for f in files:
        try:
            head = json.load(open(f, encoding="utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        if isinstance(head, dict) and "tracks" in head:
            rel = os.path.relpath(f, ROOT)
            if not only or any(o in rel for o in only):
                out.append(rel)
    return out


def render_name(rel):
    return rel.replace("/", "__").replace(".json", ".wav")


def sample_diff(a, b):
    """두 32비트 WAV 의 샘플 차이: 피크(dBFS), 다른 샘플 수, 처음 다른 초."""
    import math
    import numpy as np
    x, cx = read_float_wav(a)
    y, cy = read_float_wav(b)
    if x is None or y is None or cx != cy or x.size != y.size:
        return {"sameLength": False}
    d = np.abs(x.astype(np.float64) - y.astype(np.float64))
    peak = float(d.max()) if d.size else 0.0
    nz = np.nonzero(d)[0]
    return {"sameLength": True, "peakDb": round(20 * math.log10(peak), 1) if peak > 0 else None,
            "samples": int(nz.size), "firstSecond": round(float(nz[0] // cx) / 48000.0, 3) if nz.size else None}


def song_record(engine, rel, tmp, keep=None):
    src = os.path.join(ROOT, rel)
    rec = {}
    code, data, _ = run([engine, "validate", src])
    rec["validate"] = {"exit": code, "result": stable(data)}
    if code != 0:
        return rec
    wav = os.path.join(tmp, "song.wav")
    start = time.time()
    code, data, p = run([engine, "render", src, "--output", wav, "--bit-depth", "32", "--quiet"])
    rec["render"] = {"exit": code, "result": stable(data), "seconds": round(time.time() - start, 1)}
    if code == 0:
        rec["render"]["sha256"] = sha(wav)
        rec["render"]["levels"] = levels(wav)
        if keep:
            os.makedirs(os.path.dirname(keep), exist_ok=True)
            shutil.move(wav, keep)
    else:
        rec["render"]["stderr"] = p.stderr[-400:]
    for kind, ext in (("export-midi", "mid"), ("export-musicxml", "musicxml")):
        out = os.path.join(tmp, "out." + ext)
        code, _, _ = run([engine, kind, src, "--output", out])
        rec[kind] = {"exit": code, "sha256": sha(out) if code == 0 and os.path.exists(out) else None}
    out = os.path.join(tmp, "format.json")
    code, data, _ = run([engine, "format", src, "--output", out])
    if code == 0 and os.path.exists(out):
        rec["format"] = {"exit": code, "sha256": sha(out), "changed": (data or {}).get("changed")}
    else:
        rec["format"] = {"exit": code}
    return rec


def format_records(engine, tmp):
    """내보내기 형식마다 짧은 곡 하나의 파일 해시."""
    demo = os.path.join(ROOT, "engine", "examples", "piano_demo.json")
    # 반복 구간을 붙인 짧은 곡 (4박에서 16박까지)
    song = json.load(open(demo, encoding="utf-8"))
    song.setdefault("metadata", {})["loop"] = {"startBeat": 4, "endBeat": 16}
    loop = os.path.join(tmp, "loop_song.json")
    json.dump(song, open(loop, "w", encoding="utf-8"), ensure_ascii=False)
    cases = {
        "wav16": [demo, "--output", "{o}.wav", "--bit-depth", "16"],
        "wav24": [demo, "--output", "{o}.wav", "--bit-depth", "24"],
        "wav32": [demo, "--output", "{o}.wav", "--bit-depth", "32"],
        "flac16": [demo, "--output", "{o}.flac", "--bit-depth", "16"],
        "flac24": [demo, "--output", "{o}.flac", "--bit-depth", "24"],
        "ogg": [demo, "--output", "{o}.ogg"],
        "mp3": [demo, "--output", "{o}.mp3"],
        "bars": [demo, "--output", "{o}.wav", "--bars", "2-3", "--fade", "1"],
        "loop-wav": [loop, "--output", "{o}.wav", "--loop"],
        "loop-ogg": [loop, "--output", "{o}.ogg", "--loop"],
        "sr44100": [demo, "--output", "{o}.wav", "--sample-rate", "44100"],
    }
    out = {}
    for name, args in cases.items():
        stem = os.path.join(tmp, "fmt_" + name)
        args = [a.replace("{o}", stem) for a in args]
        code, data, _ = run([engine, "render", *args, "--quiet"])
        path = args[args.index("--output") + 1]
        out[name] = {"exit": code, "sha256": sha(path) if code == 0 else None, "result": stable(data)}
    stems = os.path.join(tmp, "stems")
    code, _, _ = run([engine, "render", demo, "--output", os.path.join(tmp, "s.wav"), "--stems", stems, "--quiet"])
    out["stems"] = {"exit": code, "files": {f: sha(os.path.join(stems, f)) for f in sorted(os.listdir(stems))} if code == 0 else {}}
    return out


def snapshot_records(app, tmp):
    """앱 화면 PNG 해시 (CTest 의 스냅숏과 README 스크린샷 종류)."""
    orch = os.path.join(ROOT, "engine", "examples", "orchestral_demo.json")
    dexed = os.path.join(ROOT, "engine", "examples", "dexed_demo.json")
    red = os.path.join(ROOT, "songs", "red-star", "red_star.json")
    cases = {
        "track": ["--size", "1280x800", "--track", "1", orch],
        "mixer": ["--size", "1280x800", "--track", "0", "--at", "4", "--effect", "master:0", orch],
        "score": ["--size", "1280x800", "--track", "0", "--score", orch],
        "synth": ["--instrument", "1", dexed],
        "export": ["--export", orch],
        "new": ["--new", "--track", "4"],
        "chorus": ["--size", "1440x900", "--track", "3", "--at", "40", "--bars", "9-16", "--lane", "cc1", red],
    }
    out = {}
    for name, args in cases.items():
        png = os.path.join(tmp, name + ".png")
        p = subprocess.run([app, "--snapshot", png, *args], capture_output=True, text=True, cwd=ROOT)
        out[name] = {"exit": p.returncode, "sha256": sha(png) if p.returncode == 0 and os.path.exists(png) else None}
    return out


def refresh(args):
    """기준선을 만든 엔진(또는 지금 앱)으로 내보내기 형식이나 화면 PNG 만 다시 기록한다."""
    path = os.path.join(args.out, "baseline.json")
    data = json.load(open(path, encoding="utf-8"))
    with tempfile.TemporaryDirectory() as tmp:
        if args.refresh == "formats":
            data["formats"] = format_records(os.path.join(args.out, "music-render"), tmp)
        else:
            data["snapshots"] = snapshot_records(os.path.join(os.path.abspath(args.build), APP_REL), tmp)
    json.dump(data, open(path, "w", encoding="utf-8"), indent=1, ensure_ascii=False, sort_keys=True)
    print(json.dumps({"ok": True, "refreshed": args.refresh}))
    return 0


def make(args):
    if args.refresh:
        return refresh(args)
    build = os.path.abspath(args.build)
    engine = os.path.join(build, "engine", "music-render")
    os.makedirs(args.out, exist_ok=True)
    ref = os.path.join(args.out, "music-render")
    shutil.copy2(engine, ref)
    head = subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=ROOT).stdout.strip()
    data = {"commit": head, "made": time.strftime("%Y-%m-%d %H:%M:%S"), "songs": {}}
    with tempfile.TemporaryDirectory() as tmp:
        for rel in songs(args.only):
            print("song", rel, flush=True)
            data["songs"][rel] = song_record(ref, rel, tmp, keep=os.path.join(args.out, "renders", render_name(rel)))
        if not args.only:
            print("formats", flush=True)
            data["formats"] = format_records(ref, tmp)
            app = os.path.join(build, APP_REL)
            if os.path.exists(app):
                print("snapshots", flush=True)
                data["snapshots"] = snapshot_records(app, tmp)
    path = os.path.join(args.out, "baseline.json")
    if args.only and os.path.exists(path):
        old = json.load(open(path, encoding="utf-8"))
        old["songs"].update(data["songs"])
        data = old
    json.dump(data, open(path, "w", encoding="utf-8"), indent=1, ensure_ascii=False, sort_keys=True)
    print(json.dumps({"ok": True, "baseline": path, "songs": len(data["songs"])}))
    return 0


def compare(name, base, now, problems):
    if base != now:
        problems.append({"item": name, "baseline": base, "now": now})


def check(args):
    build = os.path.abspath(args.build)
    engine = os.path.join(build, "engine", "music-render")
    base = json.load(open(os.path.join(args.out, "baseline.json"), encoding="utf-8"))
    problems = []
    summary = {"identical": 0, "close": []}
    with tempfile.TemporaryDirectory() as tmp:
        for rel in ([] if args.skip_songs else songs(args.only)):
            if rel not in base["songs"]:
                problems.append({"item": rel, "baseline": None, "now": "new song"})
                continue
            print("song", rel, flush=True)
            fresh = os.path.join(tmp, "now.wav")
            now = song_record(engine, rel, tmp, keep=fresh)
            old = base["songs"][rel]
            for part in ("validate", "export-midi", "export-musicxml", "format"):
                compare(f"{rel} {part}", old.get(part), now.get(part), problems)
            o, n = old.get("render", {}), now.get("render", {})
            compare(f"{rel} render.exit", o.get("exit"), n.get("exit"), problems)
            if o.get("sha256") and o.get("sha256") == n.get("sha256"):
                summary["identical"] += 1
            elif o.get("sha256") and n.get("sha256"):
                stored = os.path.join(args.out, "renders", render_name(rel))
                d = sample_diff(stored, fresh) if os.path.exists(stored) else {"sameLength": None}
                if d.get("sameLength") and (d.get("peakDb") or -999) < TOLERANCE_DB:
                    summary["close"].append({"song": rel, **d})
                else:
                    problems.append({"item": f"{rel} render samples", "baseline": o.get("levels"), "now": n.get("levels"), "diff": d})
                    continue
                for key in ("result", "levels"):
                    compare(f"{rel} render.{key}", o.get(key), n.get(key), problems)
            if os.path.exists(fresh):
                os.remove(fresh)
        if not args.only:
            print("formats", flush=True)
            now = format_records(engine, tmp)
            for k, v in base.get("formats", {}).items():
                compare(f"format {k}", v, now.get(k), problems)
            app = os.path.join(build, APP_REL)
            if os.path.exists(app) and "snapshots" in base:
                print("snapshots", flush=True)
                now = snapshot_records(app, tmp)
                for k, v in base["snapshots"].items():
                    compare(f"snapshot {k}", v, now.get(k), problems)
    print(json.dumps({"ok": not problems, "commit": base.get("commit"), "identicalRenders": summary["identical"],
                      "closeRenders": summary["close"], "problems": problems}, ensure_ascii=False, indent=1))
    return 0 if not problems else 1


def diff(args):
    """기준 엔진과 지금 엔진으로 같은 곡을 렌더링해 샘플 차이를 잰다."""
    import math
    import numpy as np
    ref = os.path.join(args.out, "music-render")
    engine = os.path.join(os.path.abspath(args.build), "engine", "music-render")
    with tempfile.TemporaryDirectory() as tmp:
        a, b = os.path.join(tmp, "base.wav"), os.path.join(tmp, "now.wav")
        for exe, out in ((ref, a), (engine, b)):
            code, _, p = run([exe, "render", os.path.abspath(args.song), "--output", out, "--bit-depth", "32", "--quiet"])
            if code != 0:
                print(p.stderr[-600:], file=sys.stderr)
                return 1
        x, cx = read_float_wav(a)
        y, cy = read_float_wav(b)
        n = min(x.size, y.size)
        d = (x[:n].astype(np.float64) - y[:n].astype(np.float64))
        peak = float(np.max(np.abs(d))) if n else 0.0
        rms = float(np.sqrt(np.mean(d ** 2))) if n else 0.0
        ref_rms = float(np.sqrt(np.mean(x[:n].astype(np.float64) ** 2))) if n else 0.0
        db = lambda v: round(20 * math.log10(v), 2) if v > 0 else None
        first = int(np.argmax(np.abs(d) > 0) // max(cx, 1)) if peak > 0 else None
        print(json.dumps({"frames": [int(x.size // max(cx, 1)), int(y.size // max(cy, 1))], "identical": peak == 0 and x.size == y.size,
                          "peakDiffDb": db(peak), "rmsDiffDb": db(rms), "rmsDiffBelowSignalDb": db(rms / ref_rms) if ref_rms else None,
                          "firstDifferentFrame": first}, indent=1))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("action", choices=["make", "check", "diff"])
    parser.add_argument("song", nargs="?")
    parser.add_argument("--out", default=os.path.join(ROOT, "build", "baseline"))
    parser.add_argument("--build", default=os.path.join(ROOT, "build"))
    parser.add_argument("--only", action="append", default=[])
    parser.add_argument("--refresh", choices=["formats", "snapshots"], help="make: 기준선의 이 부분만 다시 만든다")
    parser.add_argument("--skip-songs", action="store_true", help="check: 곡은 건너뛰고 내보내기 형식과 화면만 비교한다")
    args = parser.parse_args()
    args.out = os.path.abspath(args.out)
    try:
        os.nice(19)
    except OSError:
        pass
    if args.action == "make":
        return make(args)
    if args.action == "check":
        return check(args)
    if not args.song:
        parser.error("diff needs a song")
    return diff(args)


if __name__ == "__main__":
    sys.exit(main())
