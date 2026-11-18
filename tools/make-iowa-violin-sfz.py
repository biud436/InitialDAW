#!/usr/bin/env python3
"""University of Iowa 의 독주 바이올린 녹음으로 InitialDAW 의 SFZ 를 쓴다 (레지스트리의 violin-iowa).

받는 곳과 라이선스: University of Iowa Electronic Music Studios 「Musical Instrument Samples」 의 바이올린 arco (연주 Josue Jean-Francois,
무향실, Neumann KM 84 5피트, 16비트 44.1 kHz 모노). 스튜디오는 녹음을 어떤 목적이든 제한 없이 쓸 수 있다고 밝힌다. 파일마다 SHA-256 으로 고정한다.
녹음은 셈여림 셋 (pp, mf, ff) 과 줄 넷 (G, D, A, E) 마다 반음씩 오르는 음을 한 파일에 담았다 (35 파일, 음마다 4.5~5.5초, 비브라토 없이 곧은 소리).
연주의 음정이 음마다 -65~+50 센트 벗어나 있어 음마다 재서 맞춘다.

이 도구가 하는 일:
  - 없는 녹음을 받고 (`external/iowa/violin/*.aiff`), afconvert 로 WAV 로 바꿔 (`external/iowa/violin/wav/`) 그 곁에 SFZ 를 쓴다
    (`external/iowa/InitialDAW/violin-legato.sfz`, `violin-sustain.sfz`). 내용이 같으면 파일을 건드리지 않는다.
  - 아래 NOTES 표는 --measure 가 잰 값이다: 파일마다 엔진의 `music-render notes` 로 음을 나누고 (YIN), 파일 이름의 음 범위 안에서
    시간 순서대로 반음 건반에 맞춘다 (건너뛴 음과 음정이 0.75 반음 넘게 벗어난 음은 뺀다). 음마다 0.5~2.0초 RMS, 공격이 고른 크기의 -3 dB 에
    닿는 때, 루프 (공격 0.25초 뒤의 처음으로 고른 구간 (활 바꿈처럼 20 ms 크기가 둘레보다 3 dB, 음 전체보다 4 dB 넘게 꺼지는 자리가 없는 곳) 안에서, 50 ms 크기 차이와
    스펙트럼 차이가 작은 자리 가운데 끝을 파형 상관으로 다듬고, 크로스페이드를 미리 섞어 꺼짐이 가장 작은 것 (1.5 dB 넘으면 피한다)).

SFZ 가 하는 일:
  - 건반마다 1 포지션의 줄 (G3~C#4 는 G, D4~G#4 는 D, A4~D#5 는 A, E5 위는 E) 의 녹음을 고르고, 그 줄에 없으면 아래 줄, 그다음 다른 줄.
    녹음이 없는 건반은 이웃 녹음을 옮긴다 (가운데에서 나눈다). 음높이는 음마다 tune 으로 맞춘다.
  - CC1: 음을 시작할 때 녹음 층을 고르고 (0~50 pp, 51~95 mf, 96~127 ff), 음 안에서는 크기 (24 dB) 를 바꾼다. 층마다 음을 같은 크기로 맞춰
    층이 바뀌는 자리에서 크기가 뛰지 않는다 (CC1 127 에서 RMS -17.9 dB 기준, sfizz 를 지나 약 11.5 dB 낮다).
  - 지속음은 루프 (크로스페이드 0.25초) 로 길게 이어 내고, 활의 압력처럼 느린 크기 흔들림 넷 (0.19~2.7 Hz, 음마다 위상과 빠르기가 다르다) 을 더한다.
  - 비브라토: 녹음에 없어 음마다 넣는다. 첫 음은 0.18초 뒤에 시작해 0.4초에 걸쳐 커지고, 레가토로 이어 받는 음은 0.12초에 차오른다.
    빠르기 5.3~5.9 Hz (음마다 무작위), 깊이는 CC21 이 음 안에서도 정한다 (기본 64 에서 ±13 센트, 127 에서 ±22, 0 이면 없음).
  - legato (violin-legato.sfz): 겹친 음은 녹음의 공격을 건너뛰고 루프 시작 (고른 구간) 에서 이어 받는다 (새 음 40 ms 에 차오르고 앞 음은 0.15초에 끈다).
    한 목소리다. sustain (violin-sustain.sfz): 음마다 새 활, 겹음을 낼 수 있다.
  - 건반: 55~99 (G3~D#7). 그 밖은 소리가 없다.

사용법:
  tools/make-iowa-violin-sfz.py external/iowa                                       # build.sh --samples 가 부른다
  tools/make-iowa-violin-sfz.py external/iowa --measure --engine build/engine/music-render   # NOTES 표를 다시 잰다 (numpy)
"""
import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys

BASE = "https://theremin.music.uiowa.edu/sound%20files/MIS/Strings/violin/Violin.arco.{name}.aiff"
SR = 44100
LAYERS = {"pp": (0, 50), "mf": (51, 95), "ff": (96, 127)}
OPEN = (("sulE", 76), ("sulA", 69), ("sulD", 62), ("sulG", 55))
TARGET_DB = -17.9
CC1_RANGE_DB = 24.0
LOW_KEY, HIGH_KEY = 55, 99
XFADE_S = 0.25

# (파일 이름의 가운데, SHA-256)
FILES = [
    ("pp.sulG.A4Gb5", "7903cfe7c2eaacca76111189db86b6178eba6b85e8fbb863ef0a3364a5e5dad2"),
    ("pp.sulG.B3Ab4", "49d2e10936e71c5f34b94e67f5a729d453e8e8a6e8ec19a5312876e77bb44980"),
    ("pp.sulG.G3B3", "d283c884c4143c215634739453b788fea23d4f22d777aabaabda654224baf721"),
    ("pp.sulD.B4A5", "1513e2a0c5c37f71780168da639a3aa82a3360aa0b6efc4b72f62654911a4eff"),
    ("pp.sulD.Bb5Db6", "01a38549cc2c714610ce5319ddce29b7ac176dfdb4f4dd851375ccb060d9ce13"),
    ("pp.sulD.D4Bb4", "55bbfe5d1b8272758c4dbc5352a37c80113b938f1d2cd26408f5a62c827f2f62"),
    ("pp.sulA.A4B4", "e2e761ed114eec59eec000cc220d0cccef6a32a9fb06dd89d54c0c667ddaddca"),
    ("pp.sulA.Bb5Ab6", "98707404e37d8dbb26ee07156c98882403d150d1180d38f92c939712703bbd6d"),
    ("pp.sulA.C5A5", "febe4764b4d3831f5b0c6696cff60db8e905df3dd6e09159ff3d2293bc6873e0"),
    ("pp.sulE.A5G6", "790312b3f1b077dc51ad38ec84c2e60076b630efec24a23ca0c17a6352133aca"),
    ("pp.sulE.Ab6B7", "bad32f044446e441d4ce1b0cb43a0b22c91f6af24814045030b44cb7bfdab00a"),
    ("pp.sulE.E5A5", "faef815aee638d74b1464cd8e284af9aea3316a5b4dc25c11ae970543e479692"),
    ("mf.sulG.B3B4", "962d27fccc8bbdc30ef4734d99afe58060a1930308ab9c9b00867b1de54ac770"),
    ("mf.sulG.C5G5", "60a2859cde464365a5cf9c3e627dbbef531d219918f8b1cd7e13976eb95efeb9"),
    ("mf.sulG.G3B3", "50c53128788531062b239f5007fb48cf17c4e8b4474f9caf871163993d46b80d"),
    ("mf.sulD.Bb4C6", "d7dd7ee936e790f83e500c73ab2b99fedc3f15490e64ee82e8eeb60301b6a537"),
    ("mf.sulD.D4A4", "412bc9f34bc2ba15242aacc18eff105277d746cb52878c478f7704c5f6b079b6"),
    ("mf.sulD.D6", "1010427c6450fe7d4c7ef50ce02a0cd868b06b77ac5ba8e2817e0245e2b61595"),
    ("mf.sulA.A4B4", "d314f949de13cd2326e539131e59d8c63fd9c044bc1000baac0684b0d5c3b57b"),
    ("mf.sulA.C5C6", "6cd86026a7786f6052bb78467e2428fe79c2e95a128b1f89637e10d28d1e3c67"),
    ("mf.sulA.Db6Ab6", "c0da38204683b7f36bfe60c2a809b4ab3d8f51b1ed9d60f88dd2f885f5f820e4"),
    ("mf.sulE.Ab7A7", "4d68687e7ba87a770e58dbdf149f8ee22171dd704c1516f36c8eca5e4c8fdf3a"),
    ("mf.sulE.C6Gb7", "cee2b64078e54e68395234278d259a470b530568e5615003d08530359fef422b"),
    ("mf.sulE.E5B5", "aec7ba76149b5bf7fd7cdaf5631a619966bc91ea472a7effaf50726811f4812f"),
    ("ff.sulG.C4Eb5", "d17e195d627161af271c052694b41e2c09d78811cad3d58b2c0d2d1207e133a2"),
    ("ff.sulG.E5Gb5", "22def3cb5211b40040de3e683c3f2e6e7a0fdd8577e9560b234019f22617c8b4"),
    ("ff.sulG.G3B3", "39f124996a859462dccb74ff94ae810b933846cd64c76e162358fca91c3e162a"),
    ("ff.sulD.C5D6", "f270df9c5845ebd632db6714bde6a552460b9a4f8da3e877b988db5b0ba4d48d"),
    ("ff.sulD.D4B4", "042b27a88302fa9c03579d8a693ec9ae9a3e8ad8afad0a7d0fb6da3622a9304e"),
    ("ff.sulA.A4B4", "a16a1281b31a703ec275baa5aed7e538692d049e9f1b01b163c49a40baef24e2"),
    ("ff.sulA.C5Db6", "bea3ed0ee046eccf93df15e4352ce55a7cc6c4de692e0f0d2b0ec743a769472f"),
    ("ff.sulA.D6G6", "b8b9c1735324e4454ea4cbb4f77ec932de23dba3800c6cce45981e26586e3279"),
    ("ff.sulE.Ab7B7", "031d5079b7329dc14f2c53557bfbfb2ede172d6d79eb6b30a6641e343f85d684"),
    ("ff.sulE.C6Gb7", "218b4f84f0f505a86839bc58b499226e13eaa6b34d6ecb2789f00951db4594a0"),
    ("ff.sulE.E5B5", "6b9f797cb5892192d3a28048c6c6903c2b50d3dcd3ebf26c9be9def68eefffb6"),]

# (파일, 건반, 시작 샘플, 끝 샘플, 센트, RMS dB, 공격 초, 루프 시작 샘플, 루프 끝 샘플): --measure 가 잰 값
NOTES = [
    ("pp.sulG.A4Gb5", 70, 466754, 655017, -3.5, -42.0, 0.45, 539519, 612506),
    ("pp.sulG.A4Gb5", 71, 805089, 997365, -8.9, -48.4, 0.18, 844779, 924018),
    ("pp.sulG.A4Gb5", 72, 1200931, 1389194, -8.8, -50.9, 0.0, 1294423, 1360778),
    ("pp.sulG.A4Gb5", 73, 1605107, 1741905, 20.7, -43.4, 0.0, 1645679, 1699603),
    ("pp.sulG.A4Gb5", 74, 1933520, 2110978, 11.1, -42.1, 0.86, 2010254, 2076225),
    ("pp.sulG.A4Gb5", 75, 2293552, 2494604, -6.7, -47.0, 0.79, 2351323, 2443248),
    ("pp.sulG.A4Gb5", 76, 2665271, 2863898, -32.6, -50.1, 0.29, 2700110, 2806208),
    ("pp.sulG.A4Gb5", 77, 3015999, 3136656, -37.7, -51.1, 0.1, 3055689, 3093490),
    ("pp.sulG.A4Gb5", 78, 3376207, 3554548, -40.6, -42.8, 0.44, 3422071, 3489864),
    ("pp.sulG.B3Ab4", 59, 97901, 302526, 37.0, -44.9, 0.42, 138914, 230863),
    ("pp.sulG.B3Ab4", 60, 461241, 677772, 1.1, -42.0, 0.42, 534888, 616754),
    ("pp.sulG.B3Ab4", 61, 837150, 1076701, -37.4, -38.2, 0.44, 912120, 997942),
    ("pp.sulG.B3Ab4", 62, 1283574, 1523566, -39.4, -37.1, 0.4, 1349724, 1424958),
    ("pp.sulG.B3Ab4", 63, 2090428, 2315602, 18.6, -42.9, 0.61, 2139378, 2228804),
    ("pp.sulG.B3Ab4", 64, 2501263, 2714531, -13.5, -44.9, 0.43, 2541835, 2639191),
    ("pp.sulG.B3Ab4", 65, 2880964, 3106580, -33.9, -45.4, 0.22, 2920213, 3038742),
    ("pp.sulG.B3Ab4", 66, 3643850, 3865056, 31.5, -43.4, 0.43, 3686186, 3793179),
    ("pp.sulG.B3Ab4", 67, 4047630, 4252695, 20.4, -39.6, 0.25, 4082469, 4174506),
    ("pp.sulG.B3Ab4", 68, 4421995, 4609155, 23.3, -38.9, 0.23, 4463890, 4536160),
    ("pp.sulG.G3B3", 55, 136577, 363295, -7.5, -42.7, 0.0, 179795, 266563),
    ("pp.sulG.G3B3", 56, 593806, 806412, -46.6, -41.1, 0.3, 646726, 758839),
    ("pp.sulG.G3B3", 59, 1854184, 2089105, -55.8, -44.2, 0.41, 1895197, 2030944),
    ("pp.sulD.B4A5", 71, 43526, 249032, -52.5, -46.7, 0.61, 106589, 173904),
    ("pp.sulD.B4A5", 72, 491714, 678434, -14.4, -48.9, 0.14, 523466, 600905),
    ("pp.sulD.B4A5", 73, 895274, 1104969, -19.9, -48.4, 0.45, 978182, 1080299),
    ("pp.sulD.B4A5", 74, 1322029, 1542132, -27.5, -43.5, 0.77, 1388620, 1460776),
    ("pp.sulD.B4A5", 75, 2088443, 2306121, 18.3, -48.9, 0.3, 2150624, 2255760),
    ("pp.sulD.B4A5", 76, 2683617, 2803348, 1.8, -49.0, 0.23, 2715810, 2753664),
    ("pp.sulD.B4A5", 77, 3086250, 3236940, -15.3, -48.7, 0.01, 3110505, 3148229),
    ("pp.sulD.B4A5", 78, 3415545, 3636750, -30.8, -50.2, 0.64, 3509919, 3597405),
    ("pp.sulD.B4A5", 79, 3848254, 4041412, -22.8, -40.4, 0.05, 3894559, 3981452),
    ("pp.sulD.B4A5", 80, 4265307, 4448719, -30.4, -38.2, 0.33, 4327047, 4394771),
    ("pp.sulD.B4A5", 81, 4665074, 4869477, -6.4, -48.4, 0.43, 4743131, 4836450),
    ("pp.sulD.Bb5Db6", 82, 85289, 270465, 3.5, -52.0, 0.05, 111308, 185193),
    ("pp.sulD.D4Bb4", 62, 96799, 322415, -10.6, -43.0, 0.23, 161626, 230515),
    ("pp.sulD.D4Bb4", 63, 508296, 739645, -41.5, -44.9, 0.52, 563862, 638076),
    ("pp.sulD.D4Bb4", 64, 1032513, 1265184, -22.7, -47.9, 0.32, 1090725, 1164206),
    ("pp.sulD.D4Bb4", 65, 1478496, 1698158, -47.4, -48.3, 0.35, 1549497, 1652045),
    ("pp.sulD.D4Bb4", 66, 1960068, 2192519, -34.4, -44.8, 0.49, 2028423, 2118521),
    ("pp.sulD.D4Bb4", 67, 2786149, 3005811, -24.6, -39.7, 0.79, 2873908, 2939938),
    ("pp.sulD.D4Bb4", 68, 3216212, 3433229, -44.4, -41.9, 0.65, 3266926, 3342219),
    ("pp.sulD.D4Bb4", 70, 4003839, 4232144, -50.2, -38.9, 0.76, 4083219, 4165014),
    ("pp.sulA.A4B4", 69, 121539, 355578, -5.2, -41.6, 0.3, 208416, 291449),
    ("pp.sulA.A4B4", 70, 611490, 844867, -17.0, -40.2, 0.68, 672348, 815458),
    ("pp.sulA.A4B4", 71, 1069425, 1320883, -9.5, -49.3, 0.3, 1149246, 1275215),
    ("pp.sulA.Bb5Ab6", 82, 196906, 341201, -3.9, -49.9, 0.04, 221161, 257117),
    ("pp.sulA.Bb5Ab6", 83, 552705, 777438, -1.7, -51.0, 0.52, 677067, 743131),
    ("pp.sulA.Bb5Ab6", 84, 1007552, 1196697, -15.6, -43.8, 0.58, 1059590, 1129422),
    ("pp.sulA.Bb5Ab6", 85, 1419490, 1617676, 21.1, -36.6, 0.65, 1473733, 1545864),
    ("pp.sulA.C5A5", 72, 108309, 347155, -32.0, -46.8, 0.4, 149763, 260718),
    ("pp.sulA.C5A5", 74, 1060340, 1279341, -26.0, -44.9, 0.52, 1141484, 1208335),
    ("pp.sulA.C5A5", 75, 1517613, 1740803, -14.4, -46.4, 0.97, 1600962, 1680215),
    ("pp.sulA.C5A5", 76, 2353220, 2564679, 19.5, -49.9, 0.64, 2425544, 2539157),
    ("pp.sulA.C5A5", 77, 3184813, 3387012, 40.7, -46.0, 0.37, 3284038, 3355292),
    ("pp.sulA.C5A5", 78, 3614215, 3821926, 52.8, -44.5, 0.3, 3668899, 3761261),
    ("pp.sulA.C5A5", 79, 4055788, 4281183, 26.9, -44.8, 0.68, 4178827, 4244779),
    ("pp.sulA.C5A5", 80, 4488057, 4711908, 31.3, -41.6, 0.62, 4568319, 4641741),
    ("pp.sulA.C5A5", 81, 5001910, 5126492, 20.7, -52.2, 0.06, 5032780, 5068239),
    ("pp.sulE.A5G6", 81, 136136, 353152, 51.5, -40.5, 0.33, 207137, 296960),
    ("pp.sulE.A5G6", 82, 582120, 794682, 7.0, -42.9, 0.57, 629307, 755179),
    ("pp.sulE.A5G6", 83, 994278, 1196697, -28.1, -38.9, 0.31, 1029999, 1095982),
    ("pp.sulE.A5G6", 84, 1846687, 2018368, 22.2, -42.0, 0.27, 1904458, 1963354),
    ("pp.sulE.A5G6", 85, 2323143, 2532839, 37.4, -36.1, 0.5, 2396349, 2462317),
    ("pp.sulE.A5G6", 86, 2819754, 2991435, 51.9, -39.3, 0.9, 2890755, 2927595),
    ("pp.sulE.A5G6", 89, 4290930, 4522719, 21.6, -46.2, 0.46, 4390596, 4490152),
    ("pp.sulE.A5G6", 90, 4863789, 4991723, -0.6, -47.3, 0.02, 4929057, 4967926),
    ("pp.sulE.E5A5", 76, 102973, 350242, -2.1, -51.1, 0.7, 203962, 284936),
    ("pp.sulE.E5A5", 77, 602670, 840678, -45.7, -42.6, 0.3, 685137, 799292),
    ("pp.sulE.E5A5", 78, 1595846, 1832046, 18.1, -41.9, 0.62, 1655822, 1723048),
    ("pp.sulE.E5A5", 80, 2518286, 2761806, -21.0, -38.1, 0.89, 2612660, 2712150),
    ("pp.sulE.E5A5", 81, 3445400, 3697079, -30.3, -41.3, 0.55, 3517283, 3649408),
    ("mf.sulG.B3B4", 59, 108750, 353814, 32.0, -38.5, 0.64, 159906, 265956),
    ("mf.sulG.B3B4", 60, 560025, 801341, 14.7, -33.9, 0.6, 614709, 700436),
    ("mf.sulG.B3B4", 61, 1005568, 1237578, 42.1, -30.1, 0.36, 1054078, 1189195),
    ("mf.sulG.B3B4", 62, 1438497, 1683561, -13.4, -32.5, 0.3, 1532871, 1612934),
    ("mf.sulG.B3B4", 63, 1837602, 2073846, -22.2, -34.5, 0.29, 1884789, 2005939),
    ("mf.sulG.B3B4", 64, 2292009, 2536852, -7.3, -40.1, 0.6, 2367861, 2468685),
    ("mf.sulG.B3B4", 65, 2743284, 2983232, -4.6, -35.6, 0.41, 2793999, 2878560),
    ("mf.sulG.B3B4", 67, 3178419, 3408665, -29.9, -32.8, 0.35, 3215904, 3293406),
    ("mf.sulG.B3B4", 68, 3666826, 3886929, -37.9, -30.1, 0.53, 3716659, 3815975),
    ("mf.sulG.B3B4", 69, 4151265, 4367134, -34.8, -31.6, 0.12, 4193601, 4293838),
    ("mf.sulG.B3B4", 70, 4614932, 4823305, -3.3, -31.8, 0.54, 4662560, 4751652),
    ("mf.sulG.B3B4", 71, 5031280, 5254250, 35.8, -37.7, 0.32, 5101840, 5203623),
    ("mf.sulG.C5G5", 72, 77571, 309141, 44.7, -37.3, 0.49, 122112, 229482),
    ("mf.sulG.C5G5", 73, 478044, 710054, 16.2, -32.0, 0.17, 527877, 622983),
    ("mf.sulG.C5G5", 75, 928657, 1093944, -61.5, -35.9, 0.0, 984223, 1038157),
    ("mf.sulG.C5G5", 76, 1268977, 1491947, -48.9, -39.5, 0.03, 1326748, 1415283),
    ("mf.sulG.C5G5", 77, 1675182, 1897049, -18.4, -35.4, 0.33, 1713549, 1797127),
    ("mf.sulG.C5G5", 78, 2100174, 2317146, 29.3, -35.3, 0.04, 2135013, 2226541),
    ("mf.sulG.C5G5", 79, 2539498, 2789016, 32.4, -33.2, 0.11, 2567281, 2740800),
    ("mf.sulG.G3B3", 55, 87097, 350242, -6.9, -32.7, 0.6, 155893, 268662),
    ("mf.sulG.G3B3", 56, 517381, 767736, -9.5, -31.2, 0.44, 558835, 677795),
    ("mf.sulG.G3B3", 57, 943431, 1195595, -9.6, -30.0, 0.33, 982680, 1062739),
    ("mf.sulG.G3B3", 58, 1377066, 1636903, -35.9, -30.3, 0.09, 1413228, 1531656),
    ("mf.sulG.G3B3", 59, 1865474, 2092853, -45.7, -37.4, 0.34, 1913102, 1992857),
    ("mf.sulD.Bb4C6", 70, 108750, 311566, -35.2, -28.7, 0.37, 153291, 226368),
    ("mf.sulD.Bb4C6", 71, 441352, 672480, -13.6, -38.9, 0.5, 504856, 586949),
    ("mf.sulD.Bb4C6", 72, 881779, 1095488, -1.2, -37.8, 0.36, 926761, 1012633),
    ("mf.sulD.Bb4C6", 73, 1300376, 1525110, 21.5, -36.4, 0.21, 1332128, 1481414),
    ("mf.sulD.Bb4C6", 75, 1761354, 1978370, -30.7, -38.6, 0.19, 1855728, 1951960),
    ("mf.sulD.Bb4C6", 76, 2164472, 2386339, 3.9, -41.2, 0.46, 2210336, 2291739),
    ("mf.sulD.Bb4C6", 77, 2632329, 2858826, 14.5, -35.1, 0.35, 2673342, 2786958),
    ("mf.sulD.Bb4C6", 79, 3099965, 3339075, -46.9, -35.8, 0.2, 3142301, 3259884),
    ("mf.sulD.Bb4C6", 80, 3567822, 3782148, -36.0, -32.1, 0.45, 3612363, 3743465),
    ("mf.sulD.Bb4C6", 81, 4004059, 4225706, 18.0, -36.1, 0.31, 4073296, 4176298),
    ("mf.sulD.Bb4C6", 83, 4390816, 4605848, -13.7, -41.9, 0.16, 4482985, 4549307),
    ("mf.sulD.D4A4", 62, 101871, 352226, -12.8, -33.5, 0.49, 164052, 270648),
    ("mf.sulD.D4A4", 63, 515573, 746083, -7.4, -35.5, 0.27, 553940, 630115),
    ("mf.sulD.D4A4", 64, 932185, 1140337, -31.9, -34.6, 0.22, 964378, 1041253),
    ("mf.sulD.D4A4", 65, 1781022, 2007079, 22.7, -32.7, 0.4, 1826004, 1944176),
    ("mf.sulD.D4A4", 66, 2226564, 2435157, 7.3, -34.5, 0.6, 2287422, 2356978),
    ("mf.sulD.D4A4", 67, 2648469, 2856842, -16.6, -32.1, 0.57, 2695656, 2761683),
    ("mf.sulD.D4A4", 68, 3461320, 3690464, 2.9, -32.3, 0.43, 3527470, 3600547),
    ("mf.sulD.D4A4", 69, 3939541, 4151882, -14.9, -32.3, 0.35, 3994666, 4076571),
    ("mf.sulD.D6", 86, 165330, 282416, -17.2, -39.2, 0.0, 192672, 234810),
    ("mf.sulA.A4B4", 69, 113822, 387153, -7.1, -34.7, 0.77, 178649, 322692),
    ("mf.sulA.A4B4", 70, 546310, 807515, -19.3, -31.2, 0.65, 619075, 737213),
    ("mf.sulA.A4B4", 71, 996704, 1253939, -19.9, -30.8, 0.36, 1034630, 1169829),
    ("mf.sulA.C5C6", 72, 159112, 405940, -34.3, -30.4, 0.31, 194833, 307573),
    ("mf.sulA.C5C6", 73, 1099236, 1323088, 36.0, -33.4, 0.26, 1147746, 1228735),
    ("mf.sulA.C5C6", 74, 1496401, 1729072, -16.5, -34.2, 0.34, 1550203, 1654132),
    ("mf.sulA.C5C6", 75, 1918482, 2143877, -35.3, -32.5, 0.53, 1983309, 2054557),
    ("mf.sulA.C5C6", 76, 2356527, 2591624, -21.8, -38.1, 0.24, 2399745, 2504471),
    ("mf.sulA.C5C6", 77, 2836335, 3075181, -27.8, -39.6, 0.3, 2932914, 3007222),
    ("mf.sulA.C5C6", 78, 3290918, 3531792, -36.6, -38.1, 0.4, 3348248, 3454557),
    ("mf.sulA.C5C6", 79, 3726273, 3949022, 23.9, -35.1, 0.26, 3792423, 3873740),
    ("mf.sulA.C5C6", 81, 4159644, 4385039, -47.1, -35.1, 0.32, 4224030, 4319518),
    ("mf.sulA.C5C6", 82, 4595484, 4803416, 3.9, -35.1, 0.27, 4640907, 4739550),
    ("mf.sulA.C5C6", 84, 5076351, 5298218, -16.4, -39.4, 0.58, 5144265, 5258402),
    ("mf.sulA.Db6Ab6", 85, 99004, 334145, 48.2, -34.7, 0.16, 158098, 253607),
    ("mf.sulA.Db6Ab6", 87, 593586, 803766, 9.7, -44.7, 0.3, 672966, 742457),
    ("mf.sulA.Db6Ab6", 88, 1127107, 1344961, 36.5, -38.8, 0.0, 1181791, 1254347),
    ("mf.sulA.Db6Ab6", 90, 1621468, 1803337, -6.7, -38.2, 0.0, 1665568, 1736811),
    ("mf.sulA.Db6Ab6", 91, 2014399, 2247115, 17.8, -47.2, 0.03, 2095543, 2219597),
    ("mf.sulA.Db6Ab6", 92, 2535088, 2772214, 7.0, -48.4, 0.34, 2636959, 2714651),
    ("mf.sulE.C6Gb7", 84, 101650, 371939, -2.9, -36.9, 0.09, 127228, 308680),
    ("mf.sulE.C6Gb7", 85, 561348, 818099, 42.0, -31.9, 0.16, 620442, 687260),
    ("mf.sulE.C6Gb7", 87, 979240, 1221437, -5.6, -35.6, 0.72, 1037452, 1136867),
    ("mf.sulE.C6Gb7", 88, 1357839, 1584777, -1.9, -36.5, 0.4, 1424871, 1514240),
    ("mf.sulE.C6Gb7", 90, 1743449, 1954953, -8.4, -41.4, 0.05, 1772114, 1906696),
    ("mf.sulE.C6Gb7", 92, 2169543, 2322923, -8.6, -39.7, 0.49, 2225550, 2260973),
    ("mf.sulE.C6Gb7", 93, 2501263, 2717397, -0.6, -34.2, 0.02, 2559475, 2626293),
    ("mf.sulE.C6Gb7", 95, 2872541, 3102611, -16.2, -39.9, 0.28, 2935163, 3058285),
    ("mf.sulE.E5B5", 76, 88200, 351565, -0.7, -41.5, 0.86, 166257, 283345),
    ("mf.sulE.E5B5", 77, 521350, 767516, -18.4, -34.1, 0.38, 560158, 695824),
    ("mf.sulE.E5B5", 78, 965128, 1252395, -4.1, -38.2, 0.68, 1051564, 1186755),
    ("mf.sulE.E5B5", 79, 1429192, 1723119, -13.7, -32.1, 0.4, 1503280, 1578161),
    ("mf.sulE.E5B5", 80, 1913851, 2197370, -19.3, -31.1, 0.35, 1974268, 2093563),
    ("mf.sulE.E5B5", 81, 2380165, 2641369, 13.8, -37.3, 0.5, 2425147, 2551130),
    ("mf.sulE.E5B5", 82, 2815520, 3080252, -9.2, -32.0, 0.28, 2884316, 2972830),
    ("mf.sulE.E5B5", 83, 3274557, 3539289, 4.8, -33.6, 0.03, 3298812, 3413266),
    ("ff.sulG.C4Eb5", 60, 73382, 334366, -14.6, -25.4, 0.04, 112631, 237712),
    ("ff.sulG.C4Eb5", 61, 531713, 771485, -17.2, -22.9, 0.0, 558173, 682032),
    ("ff.sulG.C4Eb5", 62, 925350, 1166180, -38.0, -23.3, 0.1, 981798, 1078176),
    ("ff.sulG.C4Eb5", 63, 1331511, 1624335, -41.6, -26.2, 0.06, 1362381, 1526694),
    ("ff.sulG.C4Eb5", 64, 1827018, 2085533, 6.2, -28.6, 0.0, 1883466, 2001434),
    ("ff.sulG.C4Eb5", 65, 2268548, 2545452, 30.3, -31.2, 0.18, 2402171, 2480561),
    ("ff.sulG.C4Eb5", 67, 2726041, 3002239, -12.1, -27.3, 0.08, 2776315, 2905369),
    ("ff.sulG.C4Eb5", 69, 3201615, 3467891, -16.9, -25.0, 0.06, 3280554, 3355693),
    ("ff.sulG.C4Eb5", 70, 3667929, 3923621, 7.5, -31.4, 0.05, 3785235, 3854731),
    ("ff.sulG.C4Eb5", 72, 4189500, 4470372, -10.8, -29.9, 0.26, 4223016, 4289864),
    ("ff.sulG.C4Eb5", 73, 4649639, 4910799, 46.2, -25.9, 0.37, 4792082, 4864219),
    ("ff.sulG.C4Eb5", 75, 5092491, 5349506, 5.1, -29.9, 0.11, 5120274, 5303131),
    ("ff.sulG.E5Gb5", 77, 542782, 786744, 7.7, -31.2, 0.1, 593938, 708395),
    ("ff.sulG.E5Gb5", 78, 953177, 1196036, 61.1, -30.9, 0.04, 976550, 1043462),
    ("ff.sulG.G3B3", 55, 87979, 339658, -6.1, -27.0, 0.0, 119731, 229424),
    ("ff.sulG.G3B3", 56, 473148, 737440, -35.4, -25.5, 0.23, 539739, 644224),
    ("ff.sulG.G3B3", 57, 895494, 1176588, -19.5, -28.3, 0.08, 921072, 1111520),
    ("ff.sulG.G3B3", 58, 1350738, 1610179, -24.0, -25.3, 0.35, 1419975, 1541765),
    ("ff.sulG.G3B3", 59, 1775069, 2029217, -23.7, -27.5, 0.06, 1801529, 1918693),
    ("ff.sulD.C5D6", 73, 110955, 384507, -46.0, -37.0, 0.09, 144471, 304351),
    ("ff.sulD.C5D6", 74, 589617, 861669, 25.7, -32.0, 0.17, 633276, 766747),
    ("ff.sulD.C5D6", 76, 1040010, 1295481, 10.3, -37.1, 0.16, 1069116, 1145255),
    ("ff.sulD.C5D6", 77, 1489962, 1747197, 40.1, -31.7, 0.08, 1515540, 1644916),
    ("ff.sulD.C5D6", 79, 1924479, 2174835, 42.2, -29.4, 0.2, 1969461, 2051733),
    ("ff.sulD.C5D6", 81, 2366229, 2622803, 32.7, -37.8, 0.13, 2463249, 2594219),
    ("ff.sulD.C5D6", 83, 2883610, 3132864, -34.7, -37.2, 0.13, 2959021, 3038656),
    ("ff.sulD.C5D6", 85, 3383307, 3594326, -57.1, -38.6, 0.11, 3462246, 3536141),
    ("ff.sulD.C5D6", 86, 3789689, 3987918, -12.7, -36.7, 0.01, 3841286, 3917825),
    ("ff.sulD.D4B4", 62, 90184, 390681, -12.5, -27.6, 0.01, 126787, 292516),
    ("ff.sulD.D4B4", 63, 510942, 826963, -35.0, -29.7, 0.03, 568713, 707989),
    ("ff.sulD.D4B4", 64, 995160, 1295481, -29.7, -28.9, 0.02, 1020738, 1177568),
    ("ff.sulD.D4B4", 65, 1519597, 1802014, -32.1, -29.6, 0.08, 1552231, 1622230),
    ("ff.sulD.D4B4", 66, 1959407, 2273840, 4.4, -28.5, 0.02, 1982339, 2137759),
    ("ff.sulD.D4B4", 67, 2568251, 2883787, 0.2, -31.1, 0.13, 2627786, 2771375),
    ("ff.sulD.D4B4", 68, 3081619, 3379735, 2.4, -27.3, 0.1, 3110725, 3177061),
    ("ff.sulD.D4B4", 69, 3581096, 3880535, -4.0, -30.5, 0.15, 3637985, 3777687),
    ("ff.sulD.D4B4", 70, 4083659, 4394785, -18.5, -30.0, 0.0, 4199201, 4292037),
    ("ff.sulD.D4B4", 71, 4560777, 4844296, 23.2, -30.7, 0.15, 4642803, 4717063),
    ("ff.sulA.A4B4", 69, 54595, 300982, -2.0, -20.6, 0.84, 121186, 195118),
    ("ff.sulA.A4B4", 70, 408630, 663175, 3.4, -20.4, 0.19, 454053, 607769),
    ("ff.sulA.A4B4", 71, 805530, 1051696, 30.7, -24.4, 0.2, 850512, 976440),
    ("ff.sulA.C5Db6", 72, 93051, 327707, 19.4, -24.2, 0.69, 170226, 257043),
    ("ff.sulA.C5Db6", 73, 470943, 720197, 20.8, -21.0, 0.15, 533124, 663529),
    ("ff.sulA.C5Db6", 74, 880015, 1110261, 24.6, -21.5, 0.68, 942637, 1038618),
    ("ff.sulA.C5Db6", 75, 1281369, 1500767, 25.2, -24.4, 0.31, 1347960, 1436917),
    ("ff.sulA.C5Db6", 76, 1675182, 1912087, 41.5, -27.0, 0.13, 1732071, 1863394),
    ("ff.sulA.C5Db6", 78, 2089546, 2321379, -46.8, -24.7, 0.05, 2133205, 2283848),
    ("ff.sulA.C5Db6", 79, 2493325, 2725776, -26.7, -24.6, 0.38, 2536984, 2652624),
    ("ff.sulA.C5Db6", 80, 3249817, 3494219, -6.8, -18.8, 0.28, 3308911, 3413650),
    ("ff.sulA.C5Db6", 81, 3659550, 3913434, 36.0, -25.3, 0.22, 3723495, 3839767),
    ("ff.sulA.C5Db6", 83, 4098433, 4321844, 37.2, -28.5, 0.54, 4164583, 4261419),
    ("ff.sulA.C5Db6", 85, 4492511, 4734002, -42.6, -25.7, 0.2, 4531319, 4706373),
    ("ff.sulA.D6G6", 86, 79335, 288590, 23.4, -28.4, 0.05, 137988, 219083),
    ("ff.sulA.D6G6", 88, 429401, 660750, 22.7, -28.4, 0.01, 465122, 586192),
    ("ff.sulA.D6G6", 90, 799356, 1011698, 1.2, -30.9, 0.06, 897699, 965427),
    ("ff.sulE.C6Gb7", 84, 81761, 333043, -14.6, -24.8, 0.18, 122333, 291924),
    ("ff.sulE.C6Gb7", 85, 481792, 754903, 49.5, -21.9, 0.08, 529420, 678172),
    ("ff.sulE.C6Gb7", 87, 908768, 1194051, 19.5, -26.9, 0.04, 942725, 1089251),
    ("ff.sulE.C6Gb7", 89, 1332613, 1612825, -46.1, -21.4, 0.25, 1398322, 1502601),
    ("ff.sulE.C6Gb7", 91, 1745257, 1997597, -46.8, -24.1, 0.05, 1781860, 1914397),
    ("ff.sulE.C6Gb7", 93, 2153447, 2363583, -9.1, -29.6, 0.53, 2214746, 2288594),
    ("ff.sulE.C6Gb7", 95, 2485608, 2716295, -37.8, -18.7, 0.39, 2537205, 2663545),
    ("ff.sulE.E5B5", 76, 87318, 335248, 0.4, -25.2, 0.5, 164052, 263661),
    ("ff.sulE.E5B5", 77, 518483, 764208, -4.0, -23.8, 0.13, 574490, 669371),
    ("ff.sulE.E5B5", 78, 937036, 1195595, 3.5, -26.1, 0.11, 999217, 1114827),
    ("ff.sulE.E5B5", 79, 1362469, 1640696, -12.3, -21.0, 0.31, 1428178, 1550269),
    ("ff.sulE.E5B5", 80, 1786138, 2052414, -29.8, -18.4, 0.3, 1840822, 1986602),
    ("ff.sulE.E5B5", 81, 2202706, 2459501, 8.6, -24.0, 0.37, 2289583, 2360859),
    ("ff.sulE.E5B5", 82, 2608470, 2867249, -18.2, -23.2, 0.08, 2657862, 2785132),
    ("ff.sulE.E5B5", 83, 3015337, 3278967, -31.6, -23.0, 0.02, 3050617, 3225046),
]

NAMES = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "Gb": 6, "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}


def midi_of(text):
    m = re.fullmatch(r"([A-G]b?)(-?\d)", text)
    return 12 * (int(m.group(2)) + 1) + NAMES[m.group(1)]


def file_range(name):
    notes = re.findall(r"[A-G]b?\d", name.split(".")[-1])
    lo = midi_of(notes[0])
    return lo, (midi_of(notes[1]) if len(notes) > 1 else lo)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(root):
    folder = os.path.join(root, "violin")
    os.makedirs(folder, exist_ok=True)
    got = 0
    for name, digest in FILES:
        path = os.path.join(folder, f"Violin.arco.{name}.aiff")
        if os.path.exists(path) and sha256(path) == digest:
            continue
        subprocess.run(["curl", "-fsSL", "--retry", "3", "-A", "InitialDAW build", "-o", path + ".part", BASE.format(name=name)], check=True)
        if sha256(path + ".part") != digest:
            os.remove(path + ".part")
            sys.exit(f"[InitialDAW] {os.path.basename(path)} 의 SHA-256 이 맞지 않습니다")
        os.replace(path + ".part", path)
        got += 1
    return got


def wav_path(root, name):
    return os.path.join(root, "violin", "wav", f"Violin.arco.{name}.wav")


def convert(root):
    """AIFF 를 16비트 리틀 엔디언 WAV 로 (afconvert, macOS). 이미 있으면 그대로 둔다"""
    os.makedirs(os.path.join(root, "violin", "wav"), exist_ok=True)
    for name, _ in FILES:
        out = wav_path(root, name)
        if not os.path.exists(out):
            src = os.path.join(root, "violin", f"Violin.arco.{name}.aiff")
            subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16", src, out + ".part.wav"], check=True)
            os.replace(out + ".part.wav", out)


# ---------------------------------------------------------------------------
# 재기 (--measure)
# ---------------------------------------------------------------------------
def measure(root, engine):
    import wave
    import numpy as np

    def read(path):
        with wave.open(path) as w:
            x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
        return x

    def env_db(x, t, win=0.05):
        a = max(0, int(t * SR))
        seg = x[a:a + int(win * SR)]
        return 10 * math.log10(max(1e-12, float(np.mean(seg * seg)))) if len(seg) else -120.0

    def spectrum(x, t, n=2048):
        a = int(t * SR)
        seg = x[a:a + n]
        if len(seg) < n:
            return None
        sp = np.abs(np.fft.rfft(seg * np.hanning(n)))
        bands = np.array_split(sp[1:400], 16)
        return np.log10(np.array([b.mean() for b in bands]) + 1e-9)

    def env20(seg):
        hop, win = int(0.01 * SR), int(0.02 * SR)
        e = np.array([float(np.mean(seg[i:i + win] ** 2)) for i in range(0, len(seg) - win, hop)])
        return 10 * np.log10(e + 1e-12)

    def xfade_dip(seg, ls, le, xf):
        """sfizz 처럼 루프 끝 앞의 xf 를 루프 시작 앞의 xf 와 섞었을 때, 두 소리의 평균보다 꺼지는 가장 깊은 값 (dB, 20 ms)"""
        n = int(xf * SR)
        a_, b_ = seg[le - n:le], seg[ls - n:ls]
        f = np.linspace(0.0, 1.0, n)
        y = a_ * (1.0 - f) + b_ * f
        win, worst = int(0.02 * SR), 0.0
        for i in range(0, n - win, win // 2):
            ref = 0.5 * (np.mean(a_[i:i + win] ** 2) + np.mean(b_[i:i + win] ** 2))
            got = np.mean(y[i:i + win] ** 2)
            worst = max(worst, 10 * math.log10((ref + 1e-12) / (got + 1e-12)))
        return worst

    def choose_loop(seg, attack, L):
        """공격 뒤 처음으로 고른 구간 (20 ms 크기가 300 ms 이동 중앙값의 -3 dB 위이고 음 전체 중앙값의 -4 dB 위) 안에서 루프를 고른다"""
        ev = env20(seg)
        k = 15
        local = np.array([np.median(ev[max(0, i - k):i + k + 1]) for i in range(len(ev))])
        first = int((attack + 0.25) / 0.01)
        last = int((L - 0.5) / 0.01)
        clean = (ev >= local - 3.0) & (ev >= float(np.median(ev[first:last])) - 4.0)
        spans, i = [], first
        while i < last:
            if clean[i]:
                j = i
                while j < last and clean[j]:
                    j += 1
                spans.append((i * 0.01, j * 0.01))
                i = j
            else:
                i += 1
        xf = XFADE_S
        want = 1.5 if L > 4.0 else 0.8
        span = next((sp for sp in spans if sp[1] - sp[0] >= want + xf + 0.05), None) or \
            max(spans, key=lambda sp: sp[1] - sp[0], default=None)
        if span is None or span[1] - span[0] < 0.8 + xf + 0.05:
            return None
        s0, s1 = span[0] + xf, span[1]
        cands = []
        for ls in np.arange(s0, min(s1 - want, s0 + 0.8), 0.02):
            e0, p0 = env_db(seg, ls - 0.05), spectrum(seg, ls - 0.05)
            for le in np.arange(max(ls + want, s1 - 1.2), s1, 0.02):
                p1 = spectrum(seg, le - 0.05)
                if p0 is None or p1 is None:
                    continue
                cands.append((abs(env_db(seg, le - 0.05) - e0) + 2.0 * float(np.mean(np.abs(p1 - p0))), ls, le))
        best, pick = INF, None
        for cost, ls, le in sorted(cands)[:25]:
            i0, i1 = int(ls * SR), int(le * SR)
            ref = seg[i0 - 1024:i0 + 1024]
            shift = max(range(-220, 221), key=lambda d: float(np.dot(ref, seg[i1 + d - 1024:i1 + d + 1024])) if i1 + d + 1024 <= len(seg) else -INF)
            i1 += shift
            dip = xfade_dip(seg, i0, i1, xf)
            total = cost + 2.0 * dip + (10.0 if dip > 1.5 else 0.0)
            if total < best:
                best, pick = total, (i0, i1)
        return pick

    rows = []
    for name, _ in FILES:
        path = wav_path(root, name)
        x = read(path)
        out = subprocess.run([engine, "notes", path], capture_output=True, text=True, check=True).stdout
        found = [n for n in json.loads(out)["notes"] if n["endSeconds"] - n["startSeconds"] > 1.2]
        lo, hi = file_range(name)
        keys = list(range(lo, hi + 1))
        pitch = [midi_of(n["pitch"].replace("#", "")) if "#" not in n["pitch"] else midi_of(n["pitch"][0] + n["pitch"][2:]) + 1 for n in found]
        m = [p + n["cents"] / 100.0 for p, n in zip(pitch, found)]
        # 시간 순서대로 오르는 건반에 맞춘다 (음을 건너뛰면 2 반음의 값)
        INF = 1e9
        best = [[INF] * (len(keys) + 1) for _ in range(len(m) + 1)]
        back = [[None] * (len(keys) + 1) for _ in range(len(m) + 1)]
        for j in range(len(keys) + 1):
            best[0][j] = 0.0
        for i in range(1, len(m) + 1):
            for j in range(len(keys) + 1):
                if best[i - 1][j] + 2.0 < best[i][j]:
                    best[i][j], back[i][j] = best[i - 1][j] + 2.0, (i - 1, j, None)
                if j > 0:
                    if best[i][j - 1] < best[i][j]:
                        best[i][j], back[i][j] = best[i][j - 1], (i, j - 1, None)
                    d = abs(m[i - 1] - keys[j - 1])
                    if d <= 0.75 and best[i - 1][j - 1] + d < best[i][j]:
                        best[i][j], back[i][j] = best[i - 1][j - 1] + d, (i - 1, j - 1, keys[j - 1])
        pairs, i, j = [], len(m), len(keys)
        while i > 0 and back[i][j] is not None:
            pi, pj, k = back[i][j]
            if k is not None:
                pairs.append((i - 1, k))
            i, j = pi, pj
        pairs.reverse()
        for idx, key in pairs:
            n = found[idx]
            s, e = n["startSeconds"], n["endSeconds"]
            nxt = found[idx + 1]["startSeconds"] if idx + 1 < len(found) else len(x) / SR
            a = max(0, int((s - 0.015) * SR))
            b = min(len(x), int(min(e + 0.25, nxt - 0.02) * SR))
            seg = x[a:b]
            L = len(seg) / SR
            cents = round((m[idx] - key) * 100.0, 1)
            steady = float(np.median([env_db(seg, t) for t in np.arange(0.5, max(0.6, L - 1.2), 0.05)]))
            rms = 10 * math.log10(max(1e-12, float(np.mean(seg[int(0.5 * SR):int(2.0 * SR)] ** 2))))
            attack = next((t for t in np.arange(0.0, 1.0, 0.01) if env_db(seg, t) >= steady - 3.0), 0.3)
            loop = choose_loop(seg, attack, L)
            if loop is None:
                print(f"  {name} {key}: {L:.2f}초 안에 고른 구간이 짧아 뺀다", file=sys.stderr)
                continue
            ls_i, le_i = loop
            rows.append((name, key, a, b, cents, round(rms, 1), round(float(attack), 2), a + ls_i, a + le_i))
        print(f"{name}: {len(found)} 음, {len(pairs)} 건반 ({lo}~{hi})", file=sys.stderr)
    print("NOTES = [")
    for r in rows:
        print(f'    ("{r[0]}", {r[1]}, {r[2]}, {r[3]}, {r[4]}, {r[5]}, {r[6]}, {r[7]}, {r[8]}),')
    print("]")


# ---------------------------------------------------------------------------
# SFZ
# ---------------------------------------------------------------------------
def string_of(name):
    return name.split(".")[1]


def pick(layer):
    """층마다 건반 하나에 녹음 하나: 1 포지션의 줄, 없으면 아래 줄, 그다음 음정이 맞는 것"""
    rank = {s: i for i, (s, _) in enumerate(OPEN)}
    chosen = {}
    for row in NOTES:
        name, key = row[0], row[1]
        if not name.startswith(layer + "."):
            continue
        first = next(s for s, open_key in OPEN if open_key <= key)
        st = string_of(name)
        order = (0 if st == first else 1 + (rank[st] - rank[first]) if rank[st] > rank[first] else 10 + rank[first] - rank[st], abs(row[4]))
        if key not in chosen or order < chosen[key][0]:
            chosen[key] = (order, row)
    return [chosen[k][1] for k in sorted(chosen)]


def key_ranges(keys):
    out = []
    for i, k in enumerate(keys):
        lo = keys[i - 1] + (k - keys[i - 1]) // 2 + 1 if i > 0 else LOW_KEY
        hi = k + (keys[i + 1] - k) // 2 if i + 1 < len(keys) else HIGH_KEY
        out.append((max(LOW_KEY, lo), min(HIGH_KEY, hi)))
    return out


def header(kind):
    return (f"// InitialDAW 독주 바이올린 ({kind}), tools/make-iowa-violin-sfz.py 가 쓴 파일이다. 고치지 말고 그 도구를 고친다.\n"
            "// 샘플: University of Iowa Electronic Music Studios, Musical Instrument Samples, violin arco (연주 Josue Jean-Francois)\n\n"
            "<control>\ndefault_path=../violin/wav/\nlabel_cc1=Dynamics\nlabel_cc21=Vibrato\nset_cc1=80\nset_cc21=64\n\n"
            "<curve>\ncurve_index=7\nv000=0\nv064=0.6\nv127=1\n\n"
            "<global>\namp_veltrack=0\nloop_mode=loop_continuous\nfil_type=hpf_2p\ncutoff=150\n"
            "lfo03_wave=1 lfo03_freq=0.19 lfo03_freq_oncc135=0.04 lfo03_phase_oncc135=1 lfo03_volume=0.5\n"
            "lfo04_wave=1 lfo04_freq=0.47 lfo04_freq_oncc135=-0.08 lfo04_phase=0.5 lfo04_phase_oncc135=2.37 lfo04_volume=0.35\n"
            "lfo05_wave=1 lfo05_freq=1.37 lfo05_freq_oncc135=0.2 lfo05_phase_oncc135=3.71 lfo05_volume=0.3\n"
            "lfo06_wave=1 lfo06_freq=2.71 lfo06_freq_oncc135=-0.3 lfo06_phase=0.5 lfo06_phase_oncc135=5.13 lfo06_volume=0.2\n\n"
            "<master>\nlfo01_wave=1 lfo01_freq=5.3 lfo01_freq_oncc135=0.6 lfo01_phase_oncc135=1 lfo01_wave2=1 lfo01_ratio2=1.13 lfo01_scale2=0.2\n"
            "lfo01_pitch_oncc21=22 lfo01_pitch_curvecc21=7\n")


def regions(group_line, legato):
    lines = [group_line]
    for layer, (lo_cc, hi_cc) in LAYERS.items():
        rows = pick(layer)
        for row, (lo, hi) in zip(rows, key_ranges([r[1] for r in rows])):
            name, key, a, b, cents, rms, attack, ls, le = row
            vol = TARGET_DB - rms - CC1_RANGE_DB
            offset = ls - int(0.03 * SR) if legato else a          # 이어 받는 음은 고른 구간 (루프 시작) 에서
            lines.append(f"<region> sample=Violin.arco.{name}.wav lokey={lo} hikey={hi} pitch_keycenter={key} tune={-cents:.1f} "
                         f"volume={vol:.1f} gain_cc1={CC1_RANGE_DB:.0f} locc1={lo_cc} hicc1={hi_cc} offset={offset} end={b} "
                         f"loop_start={ls} loop_end={le} loop_crossfade={XFADE_S}\n")
    return lines


def write_if_changed(path, text):
    if os.path.exists(path) and open(path, encoding="utf-8").read() == text:
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


def write_sfz(root):
    out = os.path.join(root, "InitialDAW")
    first = ("<group>\ntrigger=first group=1 off_by=1 off_mode=time off_time=0.15 ampeg_attack=0.03 ampeg_release=0.45\n"
             "lfo01_delay=0.18 lfo01_delay_oncc136=0.05 lfo01_fade=0.4 lfo01_fade_oncc136=0.1\n")
    legato = ("<group>\ntrigger=legato group=1 off_by=1 off_mode=time off_time=0.15 ampeg_attack=0.04 ampeg_release=0.45\n"
              "lfo01_fade=0.12\n")
    sustain = ("<group>\nampeg_attack=0.03 ampeg_release=0.45 note_polyphony=1 note_selfmask=off\n"
               "lfo01_delay=0.18 lfo01_delay_oncc136=0.05 lfo01_fade=0.4 lfo01_fade_oncc136=0.1\n")
    changed = write_if_changed(os.path.join(out, "violin-legato.sfz"),
                               header("legato") + "".join(regions(first, False)) + "".join(regions(legato, True)))
    changed |= write_if_changed(os.path.join(out, "violin-sustain.sfz"), header("sustain") + "".join(regions(sustain, False)))
    return changed


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("root", help="external/iowa")
    ap.add_argument("--measure", action="store_true", help="NOTES 표를 다시 재서 출력한다 (numpy 가 필요하다)")
    ap.add_argument("--engine", default="build/engine/music-render")
    args = ap.parse_args()
    got = fetch(args.root)
    convert(args.root)
    if args.measure:
        measure(args.root, args.engine)
        return
    if not NOTES:
        sys.exit("[InitialDAW] NOTES 표가 비었습니다 (--measure)")
    changed = write_sfz(args.root)
    print(f"[InitialDAW] Iowa 바이올린: 녹음 {len(FILES)}개 (새로 받음 {got}), SFZ {'새로 씀' if changed else '그대로'}")


if __name__ == "__main__":
    main()
