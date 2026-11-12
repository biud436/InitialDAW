#!/usr/bin/env python3
"""렌더링의 측정 (music-render render --measure) 을 표로 읽는다. 스템 파일 없이 구간의 크기, 선율 대 나머지, 대역 몫, 저역,
마디마다의 크기, 선율 음의 크기와 갑자기 사라진 음, 스테레오 폭을 본다.

사용법:
  build/engine/music-render render song.json --output build/songs/song.mp3 --measure /tmp/song.measure.json
  tools/mix-report.py /tmp/song.measure.json --melody Cello --sections a:1-4 b:5-8 [--hp 400]

크기는 모두 dB (두 채널의 평균 전력). 트랙과 버스는 목표 LUFS 에 맞추기 전, 마스터는 맞춘 뒤다.
선율 대 나머지의 나머지는 선율 트랙을 뺀 모든 트랙과 버스 (잔향 버스 포함) 의 합이다.
"""
import argparse
import json
import math
import sys

GROUPS = [(30, 120), (120, 250), (250, 500), (500, 1000), (1000, 2000), (2000, 4000), (4000, 8000), (8000, 16000)]


def power(db):
    return 0.0 if db <= -119.99 else 10 ** (db / 10)


def db(p):
    return 10 * math.log10(p) if p > 1e-12 else -120.0


class Measure:
    def __init__(self, path):
        self.m = json.load(open(path))
        self.edges = self.m["bandEdgesHz"]
        self.starts = self.m["barStartSeconds"]
        self.bars = len(self.starts) - 1
        self.dur = [self.starts[i + 1] - self.starts[i] for i in range(self.bars)]
        self.stems = {s["name"]: s for s in self.m["stems"] if s["kind"] != "master"}
        self.master = next(s for s in self.m["stems"] if s["kind"] == "master")

    def bar_range(self, first, last):
        return range(max(1, first) - 1, min(self.bars, last))

    def level(self, stem, first, last, band=None):
        """마디 [first, last] 의 평균 전력. band 는 (낮은 Hz, 높은 Hz) 로 그 사이의 대역을 더한다"""
        num = den = 0.0
        for i in self.bar_range(first, last):
            if band is None:
                p = power(stem["barDb"][i])
            else:
                p = sum(power(v) for k, v in enumerate(stem["barBandDb"][i])
                        if self.edges[k] >= band[0] - 1e-6 and self.edges[k + 1] <= band[1] + 1e-6)
            num += p * self.dur[i]
            den += self.dur[i]
        return num / den if den else 0.0

    def side(self, stem, first, last):
        num = den = 0.0
        for i in self.bar_range(first, last):
            num += power(stem["barSideDb"][i]) * self.dur[i]
            den += self.dur[i]
        return num / den if den else 0.0


def parse_sections(items, bars):
    if not items:
        return [("전체", 1, bars)]
    out = []
    for it in items:
        name, span = it.split(":")
        a, b = span.split("-")
        out.append((name, int(a), int(b)))
    return out


def main():
    ap = argparse.ArgumentParser(description="렌더링의 측정을 표로 읽는다")
    ap.add_argument("measure")
    ap.add_argument("--melody", action="append", default=[], help="선율 트랙 이름 (여럿이면 더한다)")
    ap.add_argument("--sections", nargs="*", default=[], help="이름:첫마디-끝마디")
    ap.add_argument("--hp", type=float, default=400.0, help="선율 대 나머지를 볼 대역의 아래 경계 (Hz, 대역 경계 가운데 하나)")
    a = ap.parse_args()
    M = Measure(a.measure)
    secs = parse_sections(a.sections, M.bars)
    for name in a.melody:
        if name not in M.stems:
            sys.exit(f"선율 트랙 {name} 이 측정에 없다 (있는 것: {', '.join(M.stems)})")
    if not any(abs(e - a.hp) < 1e-6 for e in M.edges):
        sys.exit(f"--hp 는 대역 경계 가운데 하나여야 한다: {M.edges}")
    print(f"{M.m.get('title', '')}: {M.bars} 마디, {M.starts[-1]:.1f} 초, 대역 경계 {M.edges[1:-1]} Hz")

    print("\n[구간 크기 dB]")
    print("stem".ljust(18) + "".join(n[:10].rjust(11) for n, _, _ in secs))
    for name, st in list(M.stems.items()) + [("master", M.master)]:
        print(name[:17].ljust(18) + "".join(f"{db(M.level(st, f, l)):11.1f}" for _, f, l in secs))

    if a.melody:
        hi = (a.hp, 1e9)
        print(f"\n[{a.hp:.0f} Hz 위 선율 대 나머지 (잔향 버스 포함)] 와 [전체 대역]")
        for name, f, l in secs:
            mel = sum(M.level(M.stems[m], f, l, hi) for m in a.melody)
            rest = sum(M.level(st, f, l, hi) for k, st in M.stems.items() if k not in a.melody)
            mel_all = sum(M.level(M.stems[m], f, l) for m in a.melody)
            rest_all = sum(M.level(st, f, l) for k, st in M.stems.items() if k not in a.melody)
            print(f"  {name}: {db(mel) - db(rest):+.1f} dB (선율 {db(mel):.1f}, 나머지 {db(rest):.1f}), 전체 {db(mel_all) - db(rest_all):+.1f} dB")

    head = ", ".join(f"{lo // 1000 if lo >= 1000 else lo}{'k' if lo >= 1000 else ''}-{hi // 1000 if hi >= 1000 else hi}{'k' if hi >= 1000 else ''}"
                     for lo, hi in GROUPS)
    print(f"\n[마스터 대역 몫 dB] {head}")
    for name, f, l in secs:
        tot = M.level(M.master, f, l)
        print(f"  {name}: " + ", ".join(f"{db(M.level(M.master, f, l, g)) - db(tot):.1f}" for g in GROUPS))
    loud = sorted(range(M.bars), key=lambda i: M.master["barDb"][i], reverse=True)[:max(1, M.bars // 5)]
    tot = sum(power(M.master["barDb"][i]) * M.dur[i] for i in loud)
    vals = []
    for g in GROUPS:
        p = sum(sum(power(v) for k, v in enumerate(M.master["barBandDb"][i]) if M.edges[k] >= g[0] and M.edges[k + 1] <= g[1]) * M.dur[i]
                for i in loud)
        vals.append(db(p) - db(tot))
    print("  가장 큰 20% 마디: " + ", ".join(f"{v:.1f}" for v in vals))

    print("\n[30~120 Hz 트랙별 dB]")
    for name, st in M.stems.items():
        vals = [db(M.level(st, f, l, (30, 120))) for _, f, l in secs]
        if max(vals) > -60:
            print("  " + name[:16].ljust(17) + "".join(f"{v:9.1f}" for v in vals))

    print("\n[마디마다 마스터 dB]")
    print("  " + " ".join(f"{v:.1f}" for v in M.master["barDb"]))

    for m in a.melody:
        notes = M.stems[m].get("notes", [])
        if not notes:
            continue
        top = {}
        for beat, pitch, sec, att, early, late in notes:      # 함께 친 음은 가장 높은 음만 (옥타브 겹침)
            key = round(beat, 2)
            if key not in top or pitch > top[key][1]:
                top[key] = (beat, pitch, sec, att, early, late)
        line = [top[k] for k in sorted(top)]
        diffs = [(abs(b[3] - x[3]), x, b) for x, b in zip(line, line[1:]) if b[0] - x[0] < 2.5]
        print(f"\n[선율 음 크기: {m}] 머리 0.03~0.25 초, 맨 위 음 {len(line)} 개")
        if diffs:
            ds = sorted(d for d, _, _ in diffs)
            print(f"  이웃 음 차이 중앙값 {ds[len(ds) // 2]:.2f}, 90% {ds[int(len(ds) * 0.9)]:.2f}, 최대 {ds[-1]:.2f} dB")
            for d, x, y in sorted(diffs, key=lambda q: -q[0])[:4]:
                print(f"     {d:.1f} dB: 박 {x[0]:.2f} 음 {x[1]} ({x[3]:.1f}) -> 박 {y[0]:.2f} 음 {y[1]} ({y[3]:.1f})")
        drops = [(round(n[0], 2), n[1], round(n[4] - n[5], 1)) for n in notes if n[2] > 0.5 and n[4] - n[5] > 15]
        print(f"  갑자기 사라진 음 (0.5 초 넘는 음에서 0.05~0.1 초 대비 0.25~0.35 초가 15 dB 넘게 작다): {drops if drops else '없음'}")

    side = M.side(M.master, 1, M.bars)
    total = M.level(M.master, 1, M.bars)
    print(f"\n[스테레오] 마스터의 사이드 대 미드 {db(side) - db(max(1e-12, total - side)):.1f} dB")


if __name__ == "__main__":
    main()
