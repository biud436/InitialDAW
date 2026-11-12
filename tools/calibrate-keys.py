#!/usr/bin/env python3
"""악기의 건반마다 크기를 재서 보정표를 쓴다.

샘플 악기는 건반마다 녹음한 크기가 달라서 같은 벨로시티로 쳐도 음역에 따라 몇 dB 씩 튄다 (VSCO 2 CE 업라이트 피아노는 이웃 건반 사이가
14 dB 까지 벌어진다). 이 도구는 악기를 벨로시티 일곱 단계로 건반마다 한 번씩 렌더링해 공격의 크기(0.01~0.25초 RMS)를 재고,
(좌우 채널의 에너지를 더해서 잰다. 모노로 섞으면 좌우의 위상이 어긋난 녹음이 실제보다 작게 잡힌다)
engine/instruments/calibration/<악기>.json 에 쓴다. 작곡 생성기는 이 표로 음마다 벨로시티를 고쳐 같은 세기가 같은 크기로 들리게 한다.

현악과 관악처럼 공격이 느리고 크기를 CC1 이 정하는 악기는 공격 대신 이어지는 소리를 잰다: --length 로 음을 길게 내고
--window 로 잴 구간을 옮긴다. --articulation 을 주면 그 아티큘레이션으로 재서 <악기>-<아티큘레이션>.json 에 쓴다.
--cc1 은 재는 동안 CC1 을 그 값에 두고 (Sonatina 금관처럼 CC1 이 녹음 층을 바꾸는 악기), --name 은 표의 이름을 정한다.

사용법:
  tools/calibrate-keys.py <악기> [--low 36] [--high 96]     # 예: tools/calibrate-keys.py grand-piano
  tools/calibrate-keys.py sso-violins-1 --articulation legato --low 55 --high 96 --length 1.5 --window 0.5 1.3
  tools/calibrate-keys.py horns --low 54 --high 76 --length 0.6 --window 0.06 0.32 --cc1 90 --name horns-short
"""
import argparse
import json
import math
import os
import struct
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENDER = os.path.join(REPO, "build", "engine", "music-render")
VELOCITIES = [24, 40, 56, 72, 88, 104, 120]


def read_wav(path):
    """WAV(16, 24 비트 정수 또는 32 비트 실수)를 채널마다의 실수 목록으로 읽는다"""
    data = open(path, "rb").read()
    pos, channels, bits, fmt, samples = 12, 2, 24, 1, None
    while pos < len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        if cid == b"fmt ":
            fmt, channels = struct.unpack("<HH", data[pos + 8:pos + 12])
            bits = struct.unpack("<H", data[pos + 22:pos + 24])[0]
        elif cid == b"data":
            raw = data[pos + 8:pos + 8 + size]
            if fmt == 3:
                samples = list(struct.unpack(f"<{len(raw) // 4}f", raw))
            elif bits == 16:
                samples = [v / 32768.0 for v in struct.unpack(f"<{len(raw) // 2}h", raw)]
            else:
                samples = []
                for i in range(0, len(raw) - 2, 3):
                    v = raw[i] | (raw[i + 1] << 8) | (raw[i + 2] << 16)
                    samples.append((v - (1 << 24) if v >= 1 << 23 else v) / float(1 << 23))
            break
        pos += 8 + size + (size & 1)
    return [samples[c::channels] for c in range(channels)]


def rms_db(channels, a, z):
    """채널마다의 평균 제곱을 평균 낸 크기 (dB)"""
    seg = [ch[a:z] for ch in channels]
    count = sum(len(s) for s in seg)
    if not count:
        return -120.0
    return 10.0 * math.log10(max(1e-12, sum(v * v for s in seg for v in s) / count))


def main():
    ap = argparse.ArgumentParser(description="건반마다 크기를 재서 보정표를 쓴다")
    ap.add_argument("instrument")
    ap.add_argument("--low", type=int, default=36)
    ap.add_argument("--high", type=int, default=96)
    ap.add_argument("--articulation", help="음표의 아티큘레이션 (결과는 <악기>-<아티큘레이션>.json)")
    ap.add_argument("--length", type=float, default=0.5, help="음의 길이 (초)")
    ap.add_argument("--window", type=float, nargs=2, default=[0.01, 0.25], metavar=("START", "END"), help="음의 시작에서 잴 구간 (초)")
    ap.add_argument("--registry", help="기본 레지스트리 뒤에 읽을 악기 정의 파일 (시험 중인 악기)")
    ap.add_argument("--cc1", type=float, help="재는 동안의 CC1 값")
    ap.add_argument("--name", help="보정표의 이름 (기본은 <악기> 또는 <악기>-<아티큘레이션>)")
    args = ap.parse_args()
    keys = list(range(args.low, args.high + 1))
    step = args.length + 0.5
    notes, t = [], 0.0
    for vel in VELOCITIES:
        for key in keys:
            note = {"pitch": key, "startBeat": t, "durationBeat": args.length * 2.0, "velocity": vel}   # 120 BPM
            if args.articulation:
                note["articulation"] = args.articulation
            notes.append(note)
            t += step * 2.0
    song = {"title": "calibration", "tempo": 120, "render": {"tailSeconds": 1.0},
            "tracks": [{"name": "Keys", "instrument": args.instrument, "notes": notes}]}
    if args.cc1 is not None:
        song["tracks"][0]["cc1"] = [{"beat": 0.0, "value": args.cc1}]
    with tempfile.TemporaryDirectory() as tmp:
        src, wav = os.path.join(tmp, "keys.json"), os.path.join(tmp, "keys.wav")
        json.dump(song, open(src, "w"))
        cmd = [RENDER, "render", src, "--output", wav, "--bit-depth", "32", "--sample-rate", "48000", "--quiet"]
        if args.registry:
            cmd += ["--registry", os.path.abspath(args.registry)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
        x = read_wav(wav)
    sr = 48000
    levels = {}
    i = 0
    a, z = args.window
    for vel in VELOCITIES:
        for key in keys:
            t0 = i * step
            levels.setdefault(str(key), []).append(round(rms_db(x, int((t0 + a) * sr), int((t0 + z) * sr)), 2))
            i += 1
    out_dir = os.path.join(REPO, "engine", "instruments", "calibration")
    os.makedirs(out_dir, exist_ok=True)
    name = args.name or args.instrument + (f"-{args.articulation}" if args.articulation else "")
    out = os.path.join(out_dir, name + ".json")
    what = "공격 크기" if z <= 0.25 else "이어지는 소리의 크기"
    table = {"instrument": args.instrument, "comment": f"tools/calibrate-keys.py 가 잰 건반마다의 {what} (dB, {a:g}~{z:g}초 RMS)"}
    if args.articulation:
        table["articulation"] = args.articulation
    if args.cc1 is not None:
        table["cc1"] = args.cc1
    table.update({"velocities": VELOCITIES, "levels": levels})
    json.dump(table, open(out, "w"), ensure_ascii=False, indent=1)
    spread = [max(v) - min(v) for v in zip(*levels.values())]
    print(f"{out}: {len(keys)} keys, {len(VELOCITIES)} velocities, spread across keys {min(spread):.1f}~{max(spread):.1f} dB")


if __name__ == "__main__":
    sys.exit(main())
