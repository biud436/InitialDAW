#!/usr/bin/env python3
"""Surge XT 의 패치 (.fxp) 를 곡 트랙의 plugin.state 로 바꾼다 (레지스트리의 surge-xt).

Surge XT 는 VST3 프로그램 목록을 내놓지 않아 패치를 번호로 고를 수 없다. 대신 패치 파일의 몸통이 플러그인 상태와 같은 꼴이라
(Surge 의 setStateInformation 이 받는 patch 데이터), 이 도구가 JUCE 의 VST3 호스트 상태로 싸서 표준 base64 로 낸다:
  "VC2!" + 길이 + <VST3PluginState><IComponent>JUCE base64 (패치 몸통)</IComponent></VST3PluginState> + NUL
패치 파일은 fxChunkSetCustom 머리 60바이트 ('CcnK', 'FPCh', 'cjs3', chunkSize) 뒤에 chunkSize 바이트의 몸통이 온다.

사용법:
  tools/surge-patch-state.py --list pad                       # 공장 패치 가운데 이름에 pad 가 든 것
  tools/surge-patch-state.py "Pads/Pad Dark Matter"            # 패치를 찾아 plugin.state 의 base64 를 낸다
  tools/surge-patch-state.py <파일.fxp> --output state.txt
데이터 폴더는 --data, 없으면 SURGE_DATA_HOME, 그것도 없으면 external/plugins/surge-xt/SurgeXTData.
"""
import argparse
import base64
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JUCE_B64 = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
PATCH_DIRS = ("patches_factory", "patches_3rdparty")


def juce_base64(data):
    """JUCE 의 MemoryBlock::toBase64Encoding: "<바이트 수>.<6비트씩, 작은 비트부터>" """
    n = len(data)
    chars = []
    for i in range(n * 8 // 6 + 1):
        start = i * 6
        byte, offset, res, got, bits = start >> 3, start & 7, 0, 0, 6
        while bits > 0 and byte < n:
            take = min(bits, 8 - offset)
            mask = (0xFF >> (8 - take)) << offset
            res |= ((data[byte] & mask) >> offset) << got
            got += take
            bits -= take
            byte += 1
            offset = 0
        chars.append(JUCE_B64[res])
    return f"{n}." + "".join(chars)


def patch_body(path):
    raw = open(path, "rb").read()
    if len(raw) < 60 or raw[0:4] != b"CcnK" or raw[8:12] != b"FPCh" or raw[16:20] != b"cjs3":
        sys.exit(f"[InitialDAW] Surge 패치 파일이 아닙니다: {path}")
    size = struct.unpack(">i", raw[56:60])[0]
    body = raw[60:60 + size]
    if len(body) != size:
        sys.exit(f"[InitialDAW] 패치 몸통이 잘렸습니다: {path}")
    return body


def host_state(body):
    xml = ('<?xml version="1.0" encoding="UTF-8"?> <VST3PluginState><IComponent>' + juce_base64(body)
           + "</IComponent></VST3PluginState>").encode("utf-8")
    return b"VC2!" + struct.pack("<I", len(xml)) + xml + b"\0"


def data_dir(arg):
    return arg or os.environ.get("SURGE_DATA_HOME") or os.path.join(REPO, "external", "plugins", "surge-xt", "SurgeXTData")


def all_patches(root):
    for d in PATCH_DIRS:
        base = os.path.join(root, d)
        for folder, _, files in os.walk(base):
            for f in files:
                if f.endswith(".fxp"):
                    full = os.path.join(folder, f)
                    yield os.path.relpath(full, base)[:-4], full


def find_patch(name, root):
    if os.path.isfile(name):
        return name
    want = name.lower().removesuffix(".fxp")
    hits = [(rel, full) for rel, full in all_patches(root) if rel.lower() == want or rel.lower().endswith("/" + want)]
    if len(hits) != 1:
        sys.exit(f"[InitialDAW] 패치 '{name}' 를 {'찾지 못했습니다' if not hits else '여럿에서 찾았습니다: ' + ', '.join(h[0] for h in hits[:6])}")
    return hits[0][1]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("patch", nargs="?", help="패치 이름 (예: Pads/Pad Dark Matter) 이나 .fxp 경로")
    ap.add_argument("--list", metavar="QUERY", nargs="?", const="", help="이름에 QUERY 가 든 패치를 보인다")
    ap.add_argument("--data", help="Surge XT 데이터 폴더 (SurgeXTData)")
    ap.add_argument("--output", help="base64 를 쓸 파일 (없으면 표준 출력)")
    args = ap.parse_args()
    root = data_dir(args.data)
    if args.list is not None:
        for rel, _ in sorted(all_patches(root)):
            if args.list.lower() in rel.lower():
                print(rel)
        return
    if not args.patch:
        ap.error("patch 나 --list 가 필요합니다")
    text = base64.b64encode(host_state(patch_body(find_patch(args.patch, root)))).decode("ascii")
    if args.output:
        open(args.output, "w").write(text + "\n")
    else:
        print(text)


if __name__ == "__main__":
    main()
