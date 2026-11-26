#!/usr/bin/env python3
"""Versilian Community Sample Library (VCSL) 의 하프시코드와 바로크 리코더 넷으로 InitialDAW 의 SFZ 를 쓴다
(레지스트리의 harpsichord, recorder-soprano, recorder-alto, recorder-tenor, recorder-bass).

받는 곳과 라이선스: https://github.com/sgossner/VCSL (Versilian Studios, CC0 1.0). 커밋 하나로 고정하고 (tools/vcsl-manifest.json 의 commit),
파일마다 git 의 blob 해시 (SHA-1) 와 크기를 견준다. 받는 것은 이탈리아 하프시코드 (스톱 하나, 단3도마다 녹음, 놓는 소리) 와
소프라노, 알토, 테너, 베이스 리코더 (지속음, 비브라토 지속음, 스타카토) 의 WAV 205개 (226MB) 다.

이 도구가 하는 일:
  - 없는 파일을 `external/vcsl/` 아래에 저장소와 같은 경로로 받는다.
  - tools/vcsl-notes.json 의 표 (--measure 가 잰 값: 녹음마다 울리는 건반, 조율의 어긋남 (센트), 크기) 로 SFZ 를 쓴다
    (`external/vcsl/InitialDAW/<악기>.sfz`). 내용이 같으면 파일을 건드리지 않는다.

SFZ 가 하는 일:
  - 건반마다 가장 가까운 녹음을 옮겨 쓴다 (이웃 녹음의 가운데에서 나눈다). 음높이는 녹음마다 tune 으로 맞추고, 크기는 녹음마다 같은 값으로 맞춘다.
  - 하프시코드: 벨로시티가 크기를 조금만 바꾼다 (6 dB, 건반 악기에 셈여림이 없다). 건반을 놓으면 놓는 소리 (댐퍼) 가 난다.
    건반 범위는 녹음의 가장 낮은 음에서 가장 높은 음의 두 반음 위까지.
  - 리코더: 벨로시티가 크기를 바꾼다 (12 dB). 아티큘레이션 sustain (곧은 소리), vibrato (소프라노에는 없다), staccato.
    건반 범위는 녹음의 범위에서 두 반음 밖까지.

사용법:
  tools/make-vcsl-sfz.py external/vcsl             # build.sh --samples 가 부른다
  tools/make-vcsl-sfz.py external/vcsl --measure   # tools/vcsl-notes.json 을 다시 잰다 (numpy)
"""
import argparse
import hashlib
import json
import math
import os
import struct
import sys
import subprocess
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "vcsl-manifest.json")
NOTES = os.path.join(HERE, "vcsl-notes.json")
RAW = "https://raw.githubusercontent.com/sgossner/VCSL/{commit}/{path}"
TARGET_DB = -20.0            # 녹음마다 맞추는 크기 (머리 0.05~0.45초 RMS), sfizz 가 11.51 dB 줄이는 것은 volume 으로 되돌린다
SFIZZ_DB = 11.51

# 악기: (SFZ 이름, 저장소의 폴더, {아티큘레이션: 하위 폴더}, 놓는 소리의 하위 폴더, 벨로시티의 크기 폭 (%), 놓는 시간 (초))
INSTRUMENTS = [
    ("harpsichord", "Chordophones/Zithers/Harpsichord, Italian", {"sustain": "Sustains/stop1"}, "Releases/stop1", 25, 0.25),
    ("recorder-soprano", "Aerophones/Edge-blown Aerophones/Baroque Soprano Recorder", {"sustain": "Sustain", "staccato": "Staccato"}, None, 55, 0.12),
    ("recorder-alto", "Aerophones/Edge-blown Aerophones/Baroque Alto Recorder", {"sustain": "Sustain", "vibrato": "SusVib", "staccato": "Staccato"}, None, 55, 0.12),
    ("recorder-tenor", "Aerophones/Edge-blown Aerophones/Baroque Tenor Recorder", {"sustain": "Sustain", "vibrato": "SusVib", "staccato": "Staccato"}, None, 55, 0.12),
    ("recorder-bass", "Aerophones/Edge-blown Aerophones/Baroque Bass Recorder", {"sustain": "Sustain", "vibrato": "SusVib", "staccato": "Staccato"}, None, 55, 0.12),
]


def blob_sha(data):
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def fetch(root, manifest):
    got = 0
    for f in manifest["files"]:
        dest = os.path.join(root, f["path"])
        if os.path.exists(dest) and os.path.getsize(dest) == f["size"]:
            continue
        url = RAW.format(commit=manifest["commit"], path=urllib.parse.quote(f["path"]))
        data = subprocess.run(["curl", "-fsSL", "--retry", "3", url], check=True, capture_output=True).stdout
        if blob_sha(data) != f["blob"]:
            raise SystemExit(f"해시가 다릅니다: {f['path']}")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest + ".part", "wb") as out:
            out.write(data)
        os.replace(dest + ".part", dest)
        got += 1
    return got


def read_wav(path):
    """(샘플레이트, 채널마다의 float 목록을 담은 numpy 배열). PCM 16, 24, 32 비트와 float 32"""
    import numpy as np
    data = open(path, "rb").read()
    pos, fmt, body = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", data[pos + 8:pos + 24])
        elif cid == b"data":
            body = data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    tag, ch, sr, _, _, bits = fmt
    if bits == 16:
        x = np.frombuffer(body[:len(body) // 2 * 2], dtype="<i2") / 32768.0
    elif bits == 24:
        a = np.frombuffer(body[:len(body) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        x = ((a[:, 0] | (a[:, 1] << 8) | (a[:, 2] << 16)) - ((a[:, 2] & 0x80) << 17)) / 8388608.0
    elif bits == 32 and tag == 3:
        x = np.frombuffer(body[:len(body) // 4 * 4], dtype="<f4").astype(float)
    else:
        x = np.frombuffer(body[:len(body) // 4 * 4], dtype="<i4") / 2147483648.0
    return sr, x[:len(x) // ch * ch].reshape(-1, ch)


def measure_file(path):
    """울리는 음높이 (MIDI, 소수), 머리의 크기 (dB), 소리가 시작하는 때 (초)"""
    import numpy as np
    sr, x = read_wav(path)
    mono = x.mean(axis=1)
    env = np.abs(mono)
    peak = float(env.max()) or 1e-9
    start = int(np.argmax(env > peak * 0.1))
    seg = x[start + int(0.05 * sr):start + int(0.45 * sr)]
    level = 10 * math.log10(float((seg ** 2).mean()) + 1e-12)
    win = mono[start + int(0.08 * sr):start + int(0.08 * sr) + int(0.25 * sr)]
    if len(win) < int(0.1 * sr):
        win = mono[start:start + int(0.12 * sr)]
    win = win - win.mean()
    n = len(win)
    spec = np.fft.rfft(win, 2 * n)
    acf = np.fft.irfft(spec * np.conj(spec))[:n]
    energy = np.cumsum(win ** 2)
    lags = np.arange(1, n // 2)
    diff = energy[n - 1 - lags] + (energy[-1] - energy[lags - 1]) - 2 * acf[lags]          # YIN 의 차이 함수
    cmnd = diff * lags / np.maximum(1e-12, np.cumsum(diff))
    lo, hi = int(sr / 2800), int(sr / 38)
    cand = [k for k in range(lo, min(hi, len(cmnd) - 1)) if cmnd[k] < 0.15 and cmnd[k] <= cmnd[k - 1] and cmnd[k] <= cmnd[k + 1]]
    k = cand[0] if cand else lo + int(np.argmin(cmnd[lo:hi]))
    a, b0, c = cmnd[k - 1], cmnd[k], cmnd[k + 1]
    lag = lags[k] + (0.5 * (a - c) / (a - 2 * b0 + c) if abs(a - 2 * b0 + c) > 1e-12 else 0.0)
    return 69 + 12 * math.log2(sr / lag / 440.0), level, start / sr


def measure(root, manifest):
    table = {}
    for name, folder, arts, rel, _, _ in INSTRUMENTS:
        subs = dict(arts)
        if rel:
            subs["release"] = rel
        for art, sub in subs.items():
            rows = []
            for f in manifest["files"]:
                if not f["path"].startswith(folder + "/" + sub + "/"):
                    continue
                midi, level, start = measure_file(os.path.join(root, f["path"]))
                rows.append({"file": f["path"], "key": int(round(midi)), "cents": round((midi - round(midi)) * 100, 1), "db": round(level, 1), "start": round(start, 4)})
            if art == "release":                                     # 놓는 소리는 음높이가 없다: 같은 이름의 지속음 건반을 쓴다
                keys = {os.path.basename(r["file"]).split("_")[-2]: r["key"] for r in table[name]["sustain"]}
                for r in rows:
                    r["key"] = keys.get(os.path.basename(r["file"]).split("_")[-2], r["key"])
                    r["cents"] = 0.0
            rows.sort(key=lambda r: (r["key"], r["file"]))
            table.setdefault(name, {})[art] = rows
            print(name, art, [(r["key"], r["cents"], r["db"]) for r in rows])
    with open(NOTES, "w", encoding="utf-8") as out:
        json.dump(table, out, ensure_ascii=False, indent=1)
        out.write("\n")


def regions(rows, low_pad, high_pad, extra=""):
    """건반마다 가장 가까운 녹음 (같은 건반의 녹음이 여럿이면 라운드 로빈)"""
    keys = sorted({r["key"] for r in rows})
    lines = []
    for i, key in enumerate(keys):
        lo = keys[0] - low_pad if i == 0 else (keys[i - 1] + key) // 2 + 1
        hi = keys[-1] + high_pad if i == len(keys) - 1 else (key + keys[i + 1]) // 2
        same = [r for r in rows if r["key"] == key]
        for j, r in enumerate(same):
            rr = f" seq_length={len(same)} seq_position={j + 1}" if len(same) > 1 else ""
            volume = max(-12.0, min(30.0, TARGET_DB - r["db"])) + SFIZZ_DB
            offset = f" offset={int(max(0.0, r['start'] - 0.004) * 44100)}" if r.get("start", 0) > 0.01 else ""
            lines.append(f"<region> sample=../{r['file']} lokey={lo} hikey={hi} pitch_keycenter={key} tune={int(round(-r['cents']))} "
                         f"volume={volume:.1f}{rr}{offset}{extra}")
    return lines


def write_sfz(root, table):
    out_dir = os.path.join(root, "InitialDAW")
    os.makedirs(out_dir, exist_ok=True)
    written = []
    for name, folder, arts, rel, veltrack, release in INSTRUMENTS:
        for art in arts:
            rows = table[name][art]
            pad = 0 if name == "harpsichord" else 2
            text = [f"// {name} ({art}): VCSL (Versilian Studios, CC0 1.0) 의 녹음. tools/make-vcsl-sfz.py 가 쓴다, 손으로 고치지 않는다",
                    f"<global> amp_veltrack={veltrack} ampeg_attack=0.002 ampeg_release={release if art != 'staccato' else 0.2}"
                    + (" loop_mode=one_shot" if art == "staccato" else ""),
                    "<group>"]
            text += regions(rows, pad, 2)
            if rel and art == "sustain":
                text += ["<group> trigger=release ampeg_attack=0.001 ampeg_release=0.3 amp_veltrack=0 rt_decay=4"]
                text += regions(table[name]["release"], pad, 2, extra="")
                text = [t.replace("volume=", "volume=") for t in text]
            path = os.path.join(out_dir, name + ("" if art == "sustain" else "-" + art) + ".sfz")
            body = "\n".join(text) + "\n"
            if not os.path.exists(path) or open(path, encoding="utf-8").read() != body:
                with open(path, "w", encoding="utf-8") as out:
                    out.write(body)
                written.append(os.path.basename(path))
    return written


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--measure", action="store_true")
    args = ap.parse_args()
    manifest = json.load(open(MANIFEST, encoding="utf-8"))
    got = fetch(args.root, manifest)
    if got:
        print(f"[InitialDAW] VCSL 녹음 {got}개를 {args.root} 에 받았습니다")
    if args.measure:
        measure(args.root, manifest)
    table = json.load(open(NOTES, encoding="utf-8"))
    written = write_sfz(args.root, table)
    if written:
        print("[InitialDAW] VCSL 의 SFZ 를 썼습니다:", ", ".join(written))


if __name__ == "__main__":
    sys.exit(main())
