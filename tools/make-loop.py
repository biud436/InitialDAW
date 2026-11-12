#!/usr/bin/env python3
"""렌더링한 곡에서 게임용 반복 파일을 만든다.

곡 JSON 의 metadata.loop (startBeat, endBeat) 를 템포 지도로 샘플 위치로 바꾸고, 렌더링한 WAV 를 반복 끝에서 자른다.
되풀이가 매끄러우려면 반복 끝 바로 앞의 소리가 반복 시작 바로 앞의 소리와 같아야 한다 (곡이 반복 시작 앞의 마디를
반복 끝 앞에 같은 음, 같은 컨트롤러, 같은 템포로 두면 된다). 이 도구는 두 자리의 차이를 재서 알리고, 끝의 짧은 구간을
반복 시작 앞의 소리로 크로스페이드해 남은 차이(잡음, 신스의 무작위 위상)를 없앤다.

출력:
  --output <loop.wav>    처음부터 반복 끝까지. smpl 청크에 반복 시작과 끝 샘플을 적는다
  --ogg <loop.ogg>       같은 소리의 Ogg Vorbis. LOOPSTART, LOOPLENGTH 주석 (샘플 단위)
  --preview <out.wav>    처음부터 반복 끝까지 듣고 반복 시작으로 돌아가 --preview-beats 박 더 듣는 판 (끝은 --fade 초 페이드아웃)
  --stems <dir>          스템마다 이음매의 차이를 잰다 (어느 트랙이 다른지 찾을 때)

사용법:
  tools/make-loop.py songs/<곡>/<곡>.json build/songs/<곡>.wav --output build/songs/<곡>_loop.wav --ogg build/songs/<곡>_loop.ogg \\
      --preview build/songs/<곡>_preview.wav --preview-beats 40 --fade 12 --stems build/songs/<곡>_stems
"""
import argparse
import json
import math
import os
import struct
import subprocess
import sys

import numpy as np


def tempo_points(data):
    tempo = data["tempo"]
    return tempo if isinstance(tempo, list) else [{"beat": 0.0, "bpm": float(tempo)}]


def seconds_at(points, beat):
    """엔진의 TempoMap 과 같은 계산: ramp 는 박에 대해 BPM 이 선형으로 바뀐다"""
    sec = 0.0
    for i, p in enumerate(points):
        last = i + 1 == len(points)
        b0, v0 = float(p["beat"]), float(p["bpm"])
        b1 = float(points[i + 1]["beat"]) if not last else math.inf
        ramp = not last and p.get("ramp") and points[i + 1]["bpm"] != p["bpm"]
        x = min(beat, b1) - b0
        if x <= 0:
            break
        if ramp:
            k = (float(points[i + 1]["bpm"]) - v0) / (b1 - b0)
            sec += 60.0 / k * math.log((v0 + k * x) / v0)
        else:
            sec += 60.0 * x / v0
        if beat <= b1:
            break
    return sec


def read_audio(path):
    """(샘플 [n, 채널] float32, 샘플레이트, 채널 수). ffmpeg 로 읽는다"""
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate,channels",
                            "-of", "json", path], capture_output=True, text=True, check=True)
    info = json.loads(probe.stdout)["streams"][0]
    sr, ch = int(info["sample_rate"]), int(info["channels"])
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-"], capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, ch), sr, ch


def write_wav24(path, x, sr, loop=None):
    """24비트 PCM WAV. loop=(시작, 끝) 이면 smpl 청크 (끝 샘플을 포함하는 정방향 무한 반복)"""
    pcm = np.clip(np.round(x.astype(np.float64) * 8388608.0), -8388608, 8388607).astype("<i4")
    body = pcm.reshape(-1).view(np.uint8).reshape(-1, 4)[:, :3].tobytes()
    ch = x.shape[1]
    fmt = struct.pack("<HHIIHH", 1, ch, sr, sr * ch * 3, ch * 3, 24)
    chunks = [b"fmt " + struct.pack("<I", len(fmt)) + fmt]
    data = b"data" + struct.pack("<I", len(body)) + body + (b"\0" if len(body) % 2 else b"")
    chunks.append(data)
    if loop:
        smpl = struct.pack("<9I", 0, 0, int(round(1e9 / sr)), 60, 0, 0, 0, 1, 0)
        smpl += struct.pack("<6I", 0, 0, loop[0], loop[1] - 1, 0, 0)
        chunks.append(b"smpl" + struct.pack("<I", len(smpl)) + smpl)
    payload = b"WAVE" + b"".join(chunks)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", len(payload)) + payload)


def residual_db(x, start, end, window):
    """반복 끝 바로 앞과 반복 시작 바로 앞의 차이 (끝 쪽 크기에 대한 dB). 같으면 -inf"""
    a = x[end - window:end].astype(np.float64)
    c = x[start - window:start].astype(np.float64)
    power = float(np.sum(a * a))
    diff = float(np.sum((a - c) ** 2))
    if power <= 0.0:
        return None, None
    level = 10.0 * math.log10(power / a.size + 1e-30)
    return (10.0 * math.log10(diff / power) if diff > 0 else -math.inf), level


def db_text(v):
    return None if v is None else ("-inf" if v == -math.inf else round(v, 1))


def main():
    ap = argparse.ArgumentParser(description="렌더링한 곡에서 게임용 반복 파일을 만든다 (metadata.loop)")
    ap.add_argument("song")
    ap.add_argument("render")
    ap.add_argument("--start-beat", type=float)
    ap.add_argument("--end-beat", type=float)
    ap.add_argument("--output")
    ap.add_argument("--ogg")
    ap.add_argument("--ogg-quality", type=float, default=6.0)
    ap.add_argument("--preview")
    ap.add_argument("--preview-beats", type=float, help="반복 시작으로 돌아가 더 들을 박 (기본: 반복 한 번 더)")
    ap.add_argument("--fade", type=float, default=10.0, help="미리 듣기 끝의 페이드아웃 (초)")
    ap.add_argument("--crossfade-ms", type=float, default=20.0)
    ap.add_argument("--window", type=float, default=0.5, help="이음매를 견줄 길이 (초)")
    ap.add_argument("--stems")
    args = ap.parse_args()

    data = json.load(open(args.song))
    loop = (data.get("metadata") or {}).get("loop") or {}
    start_beat = args.start_beat if args.start_beat is not None else loop.get("startBeat")
    end_beat = args.end_beat if args.end_beat is not None else loop.get("endBeat")
    if start_beat is None or end_beat is None or end_beat <= start_beat:
        sys.exit("반복 구간이 없습니다: 곡 JSON 의 metadata.loop 에 startBeat 와 endBeat 를 적거나 --start-beat, --end-beat 를 주세요")
    points = tempo_points(data)
    x, sr, ch = read_audio(args.render)
    start = int(round(seconds_at(points, start_beat) * sr))
    end = int(round(seconds_at(points, end_beat) * sr))
    window = int(args.window * sr)
    if end > len(x):
        sys.exit(f"렌더링이 반복 끝({end} 샘플)보다 짧습니다 ({len(x)} 샘플)")
    if start < window:
        sys.exit("반복 시작 앞에 견줄 소리가 모자랍니다 (--window 를 줄이세요)")

    res, level = residual_db(x, start, end, window)
    report = {"ok": True, "song": args.song, "render": args.render, "sampleRate": sr,
              "loop": {"startBeat": start_beat, "endBeat": end_beat, "startSample": start, "endSample": end, "lengthSamples": end - start,
                       "startSeconds": round(start / sr, 4), "endSeconds": round(end / sr, 4)},
              "seam": {"windowSeconds": args.window, "residualDb": db_text(res), "levelDb": db_text(level),
                       "note": "반복 끝 바로 앞이 반복 시작 바로 앞과 다른 정도 (끝 쪽 크기에 대한 dB, 낮을수록 같다)"}}
    if args.stems:
        stems = {}
        for name in sorted(os.listdir(args.stems)):
            if not name.endswith(".wav") or name == "master.wav":
                continue
            s, _, _ = read_audio(os.path.join(args.stems, name))
            if len(s) >= end:
                r, lv = residual_db(s, start, end, window)
                stems[name[:-4]] = {"residualDb": db_text(r), "levelDb": db_text(lv)}
        report["seam"]["stems"] = stems

    out = x[:end].copy()
    n = min(int(args.crossfade_ms / 1000.0 * sr), end - start, start)
    if n > 0:
        w = (np.arange(1, n + 1, dtype=np.float32) / n)[:, None]
        out[end - n:end] = out[end - n:end] * (1.0 - w) + x[start - n:start] * w
    report["loop"]["crossfadeSamples"] = n
    jump = float(np.max(np.abs(out[end - 1] - out[start])))
    step = float(np.percentile(np.max(np.abs(np.diff(out[start:end], axis=0)), axis=1), 99.9))
    report["seam"]["jump"] = {"wrap": round(jump, 6), "typical": round(step, 6),
                              "note": "반복 끝 샘플에서 시작 샘플로 넘어갈 때의 차이와, 반복 안 이웃 샘플 차이의 99.9 백분위"}
    report["peakDb"] = db_text(20.0 * math.log10(float(np.max(np.abs(out))) + 1e-30))

    outputs = []
    if args.output:
        write_wav24(args.output, out, sr, (start, end))
        outputs.append(args.output)
    if args.ogg:
        src = args.output
        if not src:
            src = args.ogg + ".tmp.wav"
            write_wav24(src, out, sr)
        subprocess.run(["ffmpeg", "-y", "-v", "error", "-i", src, "-c:a", "libvorbis", "-q:a", str(args.ogg_quality),
                        "-metadata", f"LOOPSTART={start}", "-metadata", f"LOOPLENGTH={end - start}", args.ogg], check=True)
        if not args.output:
            os.remove(src)
        outputs.append(args.ogg)
    if args.preview:
        extra = end - start
        if args.preview_beats is not None:
            extra = int(round((seconds_at(points, start_beat + args.preview_beats) - seconds_at(points, start_beat)) * sr))
        parts = [out]
        left = extra
        while left > 0:
            take = min(left, end - start)
            parts.append(out[start:start + take])
            left -= take
        y = np.concatenate(parts)
        f = min(int(args.fade * sr), len(y))
        if f > 0:
            y[-f:] *= np.cos(np.linspace(0.0, math.pi / 2, f, dtype=np.float32))[:, None]
        write_wav24(args.preview, y, sr)
        report["preview"] = {"seconds": round(len(y) / sr, 2), "fadeSeconds": args.fade}
        outputs.append(args.preview)
    report["outputs"] = outputs
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
