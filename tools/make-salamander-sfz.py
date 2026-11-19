#!/usr/bin/env python3
"""Accurate-Salamander 그랜드 피아노를 받아 InitialDAW 의 SFZ 를 쓴다 (레지스트리의 salamander-piano).

받는 곳과 라이선스: Accurate-Salamander Project (Chisato Yamauchi) 6.2e 의 FLAC 판 (archive.org, SHA-256 고정, 785 MB).
원본은 Alexander Holm 의 Salamander Grand Piano V2 (Yamaha C5, AKG C414 두 개를 줄 위 12 cm 에 AB 로, 48 kHz 24비트).
라이선스는 CC-BY 3.0 (원본과 같다, 압축 안의 README.txt). 이 피아노로 만든 곡은 출처를 밝힌다.
Accurate 판은 88 건반을 다시 조율하고 건반마다 크기를 고르게 맞췄고, 벨로시티 층 사이의 크기 차이를 줄였다 (16층, 641 샘플).

이 도구가 하는 일:
  - 없거나 체크섬이 다르면 zip 을 받아 `external/salamander` 에 푼다.
  - `sfz_live/Accurate-SalamanderGrandPiano_flat.Recommended.sfz` (음 켜짐 지연 0초, 반 페달) 를 바탕으로 `InitialDAW/salamander.sfz` 를 쓴다.
    바꾸는 것은 <control> 의 기본값뿐이다: 놓는 소리 (CC20) 96, 해머 소리 (CC21) 24, 페달 소리 (CC22) 0, 페달 공명 (CC23) 127 (Accurate 판의 권장값).
    샘플 경로의 역슬래시를 슬래시로 바꾼다. 내용이 같으면 파일을 건드리지 않는다.

사용법:
  tools/make-salamander-sfz.py external/salamander      # build.sh --samples 가 부른다
"""
import hashlib
import os
import subprocess
import sys

ZIP = "AccurateSalamanderGrandPianoV6.2e_48khz24bit_flac.zip"
URL = "https://archive.org/download/httpswww.ir.isas.jaxa.jpcyamauchaccuratesalamander/" + ZIP
SHA256 = "7c74a05dd43b21233a7e55d18a5c7c1d51001c84fb8d42e43aa011e9c9ff6694"
SOURCE = "sfz_live/Accurate-SalamanderGrandPiano_flat.Recommended.sfz"
DEFAULTS = {"set_cc20=0": "set_cc20=96", "set_cc21=0": "set_cc21=24", "set_cc22=0": "set_cc22=0", "set_cc23=0": "set_cc23=127"}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit("사용법: tools/make-salamander-sfz.py external/salamander")
    root = sys.argv[1]
    os.makedirs(root, exist_ok=True)
    zpath = os.path.join(root, ZIP)
    got = False
    if not (os.path.exists(zpath) and sha256(zpath) == SHA256):
        subprocess.run(["curl", "-fL", "--retry", "3", "-o", zpath + ".part", URL], check=True)
        if sha256(zpath + ".part") != SHA256:
            os.remove(zpath + ".part")
            sys.exit(f"[InitialDAW] {ZIP} 의 SHA-256 이 맞지 않습니다")
        os.replace(zpath + ".part", zpath)
        got = True
    if got or not os.path.exists(os.path.join(root, SOURCE)):
        subprocess.run(["unzip", "-q", "-o", zpath, "-d", root], check=True)
    text = open(os.path.join(root, SOURCE), encoding="utf-8", errors="replace").read()
    for a, b in DEFAULTS.items():
        if a not in text:
            sys.exit(f"[InitialDAW] {SOURCE} 에 {a} 가 없습니다 (판이 바뀌었는지 확인한다)")
        text = text.replace(a, b, 1)
    text = text.replace("sample=..\\48khz24bit\\", "sample=../48khz24bit/")
    head = ("// InitialDAW 그랜드 피아노 (salamander-piano), tools/make-salamander-sfz.py 가 쓴 파일이다. 고치지 말고 그 도구를 고친다.\n"
            "// Accurate-Salamander Project 6.2e (Chisato Yamauchi), 원본 Salamander Grand Piano V2 (Alexander Holm), CC-BY 3.0\n")
    out = os.path.join(root, "InitialDAW", "salamander.sfz")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    body = head + text
    if os.path.exists(out) and open(out, encoding="utf-8").read() == body:
        print("[InitialDAW] Accurate-Salamander: SFZ 그대로")
        return
    with open(out, "w", encoding="utf-8") as f:
        f.write(body)
    print("[InitialDAW] Accurate-Salamander: SFZ 새로 씀")


if __name__ == "__main__":
    main()
