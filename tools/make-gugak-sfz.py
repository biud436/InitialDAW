#!/usr/bin/env python3
"""국악기 (가야금, 해금, 대금, 장구, 아쟁, 징) 의 녹음을 받아 InitialDAW 의 SFZ 를 쓴다 (레지스트리의 gayageum, haegeum, daegeum, janggu, ajaeng, jing).

받는 곳과 라이선스 (파일마다 SHA-256 으로 고정한다):
  가야금, 해금, 장구  국립국악원 「국악기 디지털 음원」 (공공누리 제1유형, 출처 표시). 받을 때마다 사이트의 사용 목적 양식을 함께 보낸다
          (상업용도 구분 '비상업용', 사용용도 '음악 제작', 소속 '개인': 2026-10-26 저자가 정했다. 계정은 없다).
          가야금: 확장 > 가야금의 단음 42개 (윤다영, Eb2~Ab5 의 반음마다, 한 파일에 약, 중, 강으로 세 번 뜯는다, 48 kHz 24비트).
          해금: 단음 > 해금의 기본음계 셋 (약, 중, 강, 44.1 kHz). 음계는 Ab3 Bb3 C4 Db4 Eb4 F4 G4 Ab4 Bb4 C5 Db5 Eb5 F5 G5 의 14음이고
          음마다 1.6~3.5초다 (음 사이가 조용해 크기로 나눈다. 음높이는 배음이 강해 YIN 이 옥타브를 틀리므로 이 순서로 이름을 정하고 배음의 합으로 잰다).
          장구: 단음 > 장구의 단타 18개 (덩, 기덕, 쿵, 더러러러, 덕, 더 의 강, 중, 약).
          아쟁: 확장 > 아쟁의 말총활대로 길게 그은 단음 27개 (Bb1~Eb4 반음마다, 8~15초, 48 kHz 24비트), 트레몰로 21개와 술폰티첼로 7개 가운데
          건반마다 하나 (국악 음정을 재서 맞춘다), 효과음 3개. 징: 단음 > 징의 타격음 강, 약 (기본 울림 Bb2 둘레, 14초에 걸쳐 사라진다).
  대금    jonijonii/Daegeum-Sanjo-Sampler (커밋 고정) 가 국립국악원 산조대금 음계 녹음 (공공누리 제1유형) 을 음마다 자른 것 (48 kHz 모노, 요성이
          들어 있다). 국악 조율이라 A440 에서 50센트까지 벗어나 SFZ 가 음마다 맞춘다.

SFZ 가 하는 일:
  - 가야금: 건반마다 녹음이 있다 (Eb2~Ab5, 양 끝은 2음씩 옮겨 늘인다). 벨로시티가 층을 고른다 (1~56 약, 57~96 중, 97~127 강).
    한 파일 안의 세 뜯음을 offset 과 end 로 가리킨다. 같은 층 안에서 음마다 봉우리를 층의 가운데 값 쪽으로 맞춘다 (±6 dB 안). 손을 떼면 0.8초에 사라진다.
  - 해금: 14음 사이의 반음은 이웃 녹음을 옮긴다 (G5 위로 두 음). 층은 가야금과 같다. 음의 30~85% 를 크로스페이드 루프로 이어 길게 낼 수 있다.
    같은 층 안에서 0.2~1.2초 RMS 를 층의 가운데 값 쪽으로 맞춘다 (±10 dB 안). 음높이는 녹음의 국악 음정 (±17센트) 을 음마다 A440 으로 맞춘다.
  - 장구: 35 쿵, 36 덩, 37 더, 38 덕, 39 기덕, 40 더러러러. 층은 가야금과 같고, 음표 길이와 상관없이 녹음의 끝까지 울린다.
  - 아쟁: 길게 (ajaeng.sfz, Bb1~Eb4, B1, C#2, D2 는 이웃 녹음을 옮긴다), 트레몰로 (ajaeng-tremolo.sfz, Eb3~G4 의 열 음), 술폰티첼로
    (ajaeng-sulpont.sfz, C4~A4 의 다섯 음). 0.5~2.5초 RMS 를 가운데 값 쪽으로 맞춘다 (±10 dB 안).
  - 징 (jing.sfz): 46 징 강, 45 징 약, 60~62 아쟁 효과음. 끝까지 울린다.
  - 대금: 녹음의 앞 1.5초 RMS 를 같게 맞춘다. 녹음 길이 (3~9초) 를 넘는 음은 녹음 끝에서 사라진다.

사용법:
  tools/make-gugak-sfz.py external/gugak     # build.sh --samples 가 부른다: 없는 파일만 받고, 내용이 같으면 SFZ 를 그대로 둔다
"""
import argparse
import array
import hashlib
import math
import os
import subprocess
import sys
import wave

NGC_EXTEND = "https://www.gugak.go.kr/digitaleum/cmmn/file/extend/download.do"
NGC_MONOTONE = "https://www.gugak.go.kr/digitaleum/cmmn/file/monotone/download.do"
NGC_FORM = {"usePurposeGb": "비상업용", "usePurpose": "음악 제작", "usePurposeDtl": "", "companyName": "개인"}
DAEGEUM_COMMIT = "34eb0fd5c4cd35bda1615333eaf92c40c142c7b8"
DAEGEUM = "https://raw.githubusercontent.com/jonijonii/Daegeum-Sanjo-Sampler/" + DAEGEUM_COMMIT + "/Daegeum_Final_Samples/Daegeum_{name}.wav"
LAYERS = {"p": (1, 56), "m": (57, 96), "f": (97, 127)}

# 가야금: (확장 번호, 건반, 세 뜯음의 시작 샘플 (48 kHz), 세 뜯음의 음높이 센트, 세 뜯음의 봉우리 dB, SHA-256)
GAYAGEUM = [
    (27, 39, (46560, 432000, 815040), (-2, -2, -1), (-26.4, -19.2, -9.5), "792eb2a5737a0f92ad82ac056c8b89839dc0ff1c018f98cfab67a2aec7e33557"),
    (23, 40, (45360, 431520, 816240), (0, 1, 0), (-21.2, -13.9, -9.2), "7be9c97d2660c6d4dfa5cb3a1535489732e3bd3ad03ea90bd988b2832deb83af"),
    (35, 41, (45840, 430800, 815760), (1, 1, 1), (-18.0, -12.9, -6.4), "5ef776e3dfa7ff9d96677289b420733da8e0ce2c0abdc70aebe651ce029ffe89"),
    (31, 42, (48000, 432240, 814320), (1, 2, 2), (-24.4, -15.0, -8.7), "347a7bc7622e1c39154a44fc38ab1ff877615f41793a2a3600e59090a5de136f"),
    (39, 43, (45840, 430560, 815040), (-3, -3, -3), (-20.3, -17.0, -8.6), "ad9563c32ef40d26fdd596402b8a73f0c13dfd643f97b0fa387f6394abaf0ad6"),
    (4, 44, (48240, 431040, 814800), (-1, -1, -1), (-19.9, -8.6, -6.1), "e4b9ad6bc8c183ae6a9e94b68a496353459d373458eb370523933d6c469c5050"),
    (1, 45, (47520, 431760, 814560), (1, 0, -2), (-21.5, -14.0, -7.9), "7179b53927cbfdbca28752202a46aa900207440acd743040c3bccb6ddde20f39"),
    (11, 46, (46320, 428400, 816240), (-5, -5, -5), (-21.3, -10.8, -7.5), "6ce3c33f8b18a158253342a362d610c37b445fdbd310c5770913635a184103a8"),
    (8, 47, (48000, 429120, 815040), (-3, -5, -6), (-27.1, -14.1, -9.5), "929944476767b08c0852cf055603069bf9e6823be87d60b558b9e9ecd20d3f34"),
    (14, 48, (45840, 433200, 815760), (-3, -3, -4), (-22.6, -12.3, -8.3), "d3659483db0c4191bd2c08759f87fe54900341c864fb6ab85151a85c525c9ef1"),
    (20, 49, (48000, 429600, 816240), (-1, 0, 0), (-26.2, -15.0, -7.5), "e6bdf57df3b8ca346a43c8d624967eafb7a73a445884fde6f97fd025d7bcf70f"),
    (17, 50, (47040, 434640, 812880), (-3, -1, -1), (-22.3, -13.6, -8.6), "d9c9835c2ca4a76aca4496d7aaf2f85987e26019a0ead784c6f2fedf81549c48"),
    (28, 51, (49200, 431520, 813600), (-2, -2, -4), (-20.0, -11.5, -8.5), "45f03c57d8d425c74a3dfab54bcf0eb59729eb04ec3ea840968e449cd737f681"),
    (24, 52, (48960, 430560, 813120), (0, -1, -1), (-19.9, -10.8, -8.8), "35c065027053efdef43d49cbde260d84ef8516eff65aede12e4f24d6161d9d6e"),
    (36, 53, (44640, 431760, 814080), (-1, 0, 0), (-17.4, -10.0, -6.4), "bc91b51aa0a7db5f01c359378578ddc6c267b0094be49df4625297145fcc0fe0"),
    (32, 54, (46560, 432000, 814800), (-1, -2, -2), (-27.4, -12.6, -8.3), "dd2d01e9b95479002275daa7efb7cad46b134b6a33ec3dc0dfc64944a6f11ff1"),
    (40, 55, (48000, 430560, 811200), (3, 2, 1), (-23.4, -11.5, -6.7), "f5a21409ea0d48c25062f18c8494b0f1449a53ca0f925b99a02d4155d55dec74"),
    (5, 56, (47520, 432960, 813120), (3, 2, 1), (-19.8, -16.1, -9.6), "6b139e64a6d35002445a8e36827c35f005b7076e17949b95ccb68f35d156afcf"),
    (2, 57, (47760, 431040, 813360), (2, 2, 1), (-23.9, -13.9, -9.7), "337eb0d58e476d6722e279af9d38cc9cc746cc47c61bebb141175bbfba88f4ba"),
    (12, 58, (48000, 431040, 810960), (0, 0, 0), (-22.7, -12.6, -8.1), "0256aa406c7e89fdb32cfbb17534949310660bab99de7898f23d2705a2833d83"),
    (9, 59, (50400, 431040, 818640), (-4, -4, -5), (-19.9, -12.1, -7.6), "0d15f33ae7e1a9e0d96570d992b6ad81d63a6bd227735dad2411e988d75a2053"),
    (15, 60, (44160, 428640, 816000), (2, 2, 2), (-22.5, -13.2, -9.8), "4a7330002808ac71906c5035bef17a8867acda82accd3897dd0e2f78a6a4e131"),
    (21, 61, (47280, 433920, 814800), (-1, 0, 0), (-19.5, -10.5, -7.1), "62baa318622bd8c24ac076df774c0d76cc4a92210a42ac341c3613802e7db622"),
    (18, 62, (45840, 429360, 817920), (0, 0, -4), (-21.0, -9.7, -8.1), "dd45e86d16264ed277854b383b7bc4d7710ea45c299054218a10e764217f1973"),
    (29, 63, (44880, 431520, 812640), (0, -1, -1), (-22.3, -10.6, -9.4), "0406e5b27db0ede9bac939103869a5a94c4e30237a401e0e3d23f3b9197cb3b9"),
    (25, 64, (44400, 429600, 813360), (1, 0, -12), (-19.7, -9.3, -8.1), "dcf4d1b6f0c101d0b5b43358624671506b74686bb89938bcd417587421348c3a"),
    (37, 65, (46800, 432960, 816000), (-3, -3, -4), (-18.2, -11.5, -8.5), "f4e5f2e22e386ba93787ded5d54c345e424199d80e47e81a67c0a31ae0753795"),
    (33, 66, (48480, 431040, 815040), (-1, -2, -2), (-18.6, -12.0, -8.7), "12edd242ad16a267802fa722d6262c3038dcead44c18608e398d53e9eda30339"),
    (41, 67, (47040, 430800, 813120), (5, 4, 1), (-21.9, -12.8, -8.5), "d3c085225450607751d222bfee34d3f67806ddfa7c30dfb6b809b3ee42b18fd8"),
    (6, 68, (47040, 432240, 815280), (0, 3, 4), (-20.3, -12.4, -7.8), "5e2c79d43526c075c76bc39e46f1db6118c520e22ea44cf70b9e371d1c3aacf3"),
    (3, 69, (48240, 430800, 814320), (4, 3, 0), (-21.5, -12.6, -6.5), "4510997a1744585291d0fd5b6224073e56937c9194fa96bd305d4c3426331001"),
    (13, 70, (48240, 431760, 812160), (-10, 0, -9), (-25.8, -11.1, -7.6), "c39ebdb0f5cf84480a8511fdc863e2a301a7756b5af20f5cccdf717bc8a7525f"),
    (10, 71, (49680, 432720, 813600), (9, -10, -10), (-22.8, -14.3, -8.7), "bbb79701abf49ae326518574290ab316e65b2827e8fdbaa6eaca55dab428f155"),
    (16, 72, (47520, 435600, 812880), (7, 7, 7), (-21.3, -11.8, -7.6), "d02adf294cf64fd2525cdf4b26381d81daf15d197a7c708f85ab4a9363b8500d"),
    (22, 73, (44640, 429840, 812640), (-3, 2, -5), (-20.7, -13.7, -8.6), "ee0044b10967f9b340fc005a753943212fc5619a1f2d74de388ec806f049b9d2"),
    (19, 74, (46560, 429840, 812880), (1, 1, 0), (-26.0, -11.8, -7.2), "5958cf3abe455981ab43570fd144561fc754042bb678623c5c4b3d97f77328fd"),
    (30, 75, (45840, 431760, 814800), (8, 8, 7), (-21.8, -15.1, -7.0), "f98e3123dabf84833d7e179793e6506b6957bce3ca611257a3f2f3e58218db08"),
    (26, 76, (45360, 430080, 815520), (-3, 0, -2), (-23.3, -12.3, -8.8), "968374c38fbeb371e69d270b262cf49e1d17855e56e87d78be91b94081ee0f06"),
    (38, 77, (49680, 428640, 812640), (7, 7, 7), (-20.3, -7.9, -5.3), "b62d1e0231cd2c9c5844a5cfea69e9a4e4315a4556543c3906a0b7c2dd8c8263"),
    (34, 78, (46800, 431520, 814560), (-6, -1, -4), (-20.3, -11.3, -8.9), "b139d846dcff14b9d9e53291427baf68f37c2806686504869ce84a442bfbe5c0"),
    (42, 79, (44880, 429840, 816000), (-4, -4, -4), (-25.6, -11.2, -7.4), "d8ac65a386d56523089c16ac6367bbe0c24248c26e9d650c6dce278523b9abdf"),
    (7, 80, (49200, 431520, 815040), (-5, -5, -5), (-22.5, -12.6, -6.9), "b31f2bb2ffdec7538affe69a1bb31759c90dda5696f601152f2494a3e550f610"),
]
# 해금 음계: (단음 번호, 층, SHA-256, [(건반, 시작 초, 길이 초, 음높이 센트, 0.2~1.2초 RMS dB)]) (44.1 kHz)
HAEGEUM = [
    (2474, "p", "2b73a301d2db4ecf53298d69f53f08e21a8ff64036f0e4a8e03e66ac0d109d14", [
        (56, 0.17, 3.09, 4, -27.7),
        (58, 3.62, 2.93, -1, -33.0),
        (60, 7.12, 2.16, -1, -28.6),
        (61, 10.6, 2.45, -11, -26.6),
        (63, 14.1, 1.63, -8, -20.9),
        (65, 17.61, 3.53, -3, -25.2),
        (67, 21.4, 3.27, 8, -18.9),
        (68, 24.9, 3.13, -7, -22.5),
        (70, 28.39, 3.06, 4, -23.4),
        (72, 31.91, 2.23, 4, -16.1),
        (73, 35.4, 2.06, 5, -21.3),
        (75, 38.94, 2.34, 6, -30.7),
        (77, 42.42, 2.53, 1, -26.6),
        (79, 45.89, 2.52, -5, -23.3),
    ]),
    (2406, "m", "5f1d381ea0e249c70bc3f6fb10aab2a5d35fe3c9bef61d5fabd82e5207ea3929", [
        (56, 0.11, 2.81, -1, -23.1),
        (58, 3.3, 2.35, -5, -25.5),
        (60, 6.01, 1.65, -4, -23.6),
        (61, 8.2, 2.37, -7, -18.0),
        (63, 11.09, 1.76, -16, -14.2),
        (65, 13.69, 3.0, 6, -17.7),
        (67, 17.09, 2.88, 0, -16.2),
        (68, 20.4, 2.79, -3, -17.1),
        (70, 23.5, 2.64, 10, -20.2),
        (72, 26.5, 1.94, 3, -10.8),
        (73, 29.1, 1.8, 6, -15.4),
        (75, 31.7, 2.25, 4, -25.9),
        (77, 34.7, 2.37, -3, -20.0),
        (79, 37.8, 2.37, -5, -15.9),
    ]),
    (2482, "f", "7e598ec9156eeeaf423ac421501df3a6da0302169e96eaaa95d282e0fee1d735", [
        (56, 0.09, 1.62, 17, -19.7),
        (58, 2.98, 1.94, -11, -22.3),
        (60, 5.92, 1.67, -11, -22.1),
        (61, 8.79, 1.96, 2, -13.1),
        (63, 11.69, 1.8, 12, -10.8),
        (65, 14.59, 2.69, -9, -11.8),
        (67, 17.95, 2.78, 4, -13.6),
        (68, 21.69, 2.24, -13, -11.2),
        (70, 24.58, 2.53, 1, -18.7),
        (72, 27.68, 2.0, 1, -11.6),
        (73, 30.6, 1.9, 7, -13.2),
        (75, 33.49, 2.04, 4, -20.4),
        (77, 36.4, 2.26, -1, -18.9),
        (79, 39.29, 2.16, 3, -11.8),
    ]),
]

# 장구: (단음 번호, 건반, 소리, 층, 봉우리 dB, SHA-256). 모두 0.2초 뒤에 친다 (44.1 kHz). 세기마다 봉우리를 거의 0 dB 로 맞춘 녹음이라
# 크기는 층마다 정한다 (강 -1, 중 -6, 약 -12 dB 의 봉우리)
JANGGU = [
    (2288, 35, "쿵", "f", -0.5, "80aa045a8c6d0acf3e8f0a9773751f4873382a4642293aa6f054d27fd7b42fc6"),
    (2289, 35, "쿵", "m", -6.0, "391ffe212c40895801d35913188822001c446e97a504fea379d6a5f7bcb564d3"),
    (2290, 35, "쿵", "p", -12.5, "01cb82b2ff759f8c517b652d0f68b85ce4e1377d714a56d36b646f5ffb56e44e"),
    (2282, 36, "덩", "f", -0.3, "963cc19eaa7b7c22fcb5967710341f0066614f81c6c92345a0918409eee80330"),
    (2283, 36, "덩", "m", -0.6, "1eddfb4166b847aed412d443e9ec79d45cdc9fd4ce1ad597bc0b1e920dd64cf4"),
    (2284, 36, "덩", "p", -6.6, "ec68547dad73c12394342fc1257e2edd193f0743e6ba48264d22043a06cf9352"),
    (2297, 37, "더", "f", -0.6, "dfefdf407ec8891dce9741acdd1b1f63bf0e8e8b0cd317470c2d58ee84e8f3ec"),
    (2298, 37, "더", "m", -3.1, "80ec782d2fb0cfa0f6bd660691fb30d96b56466fecd02d1b5e3e012607fd065c"),
    (2299, 37, "더", "p", -16.1, "95b4036e0fa389b28d94fe1c8d9418bf9dd2044c2c536bdb84fc565ac74aaedb"),
    (2294, 38, "덕", "f", -0.2, "edaa4d0f288d3636fca3bb1b0399f8411661d502f0388535ecf640c6ae4f9f91"),
    (2295, 38, "덕", "m", -0.4, "cb84fdaa865bc6ca3a805214ae9f7fbf45b36ea99b514b7e16394d84c582d346"),
    (2296, 38, "덕", "p", -3.3, "30255a1201ca011fab8fd59db5be304a561814b6f0cf6aed24aa8957a4a8e0ae"),
    (2285, 39, "기덕", "f", -0.4, "1f79d415e5067a32ff40cc0dd5d9e63b3eba10cdd1b1dcba82f03d9bde26f807"),
    (2286, 39, "기덕", "m", -0.3, "8425a30b1b10b9ef9f0032e657665ea7d0e3043a12fe1ff12829da6a88bb5866"),
    (2287, 39, "기덕", "p", -0.9, "c8c4947d46bc957276d9b170f8afd0f319e1b87a12fd248607338513801e0009"),
    (2291, 40, "더러러러", "f", -2.9, "a119f01f9f5cdccd0279585aad2c43de5a20d78b48a9c4651f788cf62e856076"),
    (2292, 40, "더러러러", "m", -11.8, "ec1d6d318834871d61d003b2590c2b056318008784ad5be108195e95659edd30"),
    (2293, 40, "더러러러", "p", -10.3, "5899af0c792cb490ce361c8d866a1403fecf012282dc3a689fa19453a59e97b7"),
]

# 아쟁 길게: (확장 번호, 건반, 시작 초, 끝 초, 음높이 센트, 0.5~2.5초 RMS dB, SHA-256) (말총활대, 48 kHz 24비트)
AJAENG = [
    (805, 34, 0.77, 13.57, -2, -43.9, "41cd30c4aa11852456b43b1ae9135a80d9e76567da527735be0169e228ac3902"),
    (810, 36, 0.52, 15.33, -5, -44.3, "e75feef1fbc04bed8fae8f7fb2421fda7bc8bce26071af577a8a273e7977e02b"),
    (817, 39, 0.72, 15.34, 1, -42.6, "f9d354c82042c2859ae5b47fc8f9f021dc21f8653f3c2c2f1d52a91a3ed8be4d"),
    (820, 40, 0.52, 15.32, -3, -41.6, "9c842c591914f15f3ad6774574fe14b8178c4bcc0afd95082518fbdfb443a8dc"),
    (822, 41, 0.67, 12.79, 1, -42.4, "d629a37513ace9192e193975cf8d9ff4df1e683931d293949ce11fc5fb80a7d3"),
    (824, 42, 0.61, 12.7, 0, -39.7, "5186e65cc6f89561f4b98b73f68ac147d6b50275660b9454fba601aab7e3f894"),
    (826, 43, 0.73, 13.1, 1, -37.2, "df7ee3f7265da599cc4683ee433f69e1a9fbecf0e3238d524aa026289ddf311c"),
    (801, 44, 0.74, 11.82, 3, -36.7, "c35719a6d5e1acfddbff2ac8076ef6fa2a98cee25e09788bd921fae40a6bdcb2"),
    (803, 45, 0.41, 14.25, -2, -44.2, "471b564284394aab38f6f8d423732af8b56ace08b936cbf844e1c04281b3255f"),
    (806, 46, 0.54, 13.23, 5, -39.3, "0252cf2c7149b1347fba8fa51c99fdab482aa35c2acccb1f940d31b76896202c"),
    (808, 47, 0.55, 14.74, -1, -41.0, "b5afddf8e46758070428b9a1b626a860bdd72f0b399ae2610d7cd6a2532781bd"),
    (811, 48, 0.73, 14.55, -3, -42.1, "0812b24e8a3a5b3f8c5bdb52d9e29d688ae13bc273e140491dcb0ca13751a485"),
    (813, 49, 0.7, 13.47, -8, -41.2, "1a54c98d86361ebe3c216a3dd45011d21c13704ec01521da5c6a300c3c7164dc"),
    (815, 50, 0.69, 13.15, 0, -36.6, "3f30d60ed7725ce4800cec8a50db69e8bed666ecd46d165050334c34b9507159"),
    (818, 51, 0.73, 14.21, -2, -33.5, "5b825b8fef5e6f695e1c8fb028f9a7797fec0745a579283db61659e1c1827b25"),
    (821, 52, 0.75, 10.6, -3, -35.4, "f8a75c262eabec086d309cd6c211590fdf3c15ebc0c4da297e97ffa358674855"),
    (823, 53, 0.7, 12.36, 2, -33.3, "045904b07477e6befd462fc62dd7de6c9237deb215b9c5687770f31553b0edde"),
    (825, 54, 0.75, 10.47, 6, -36.2, "130751f24d2c5b10132f7fa06e2832e022e2553788a388ccec17e72ae5762240"),
    (827, 55, 0.76, 10.87, 7, -33.3, "cc437d31598fed1f612aa43e9da83788195fd91c7f7df5b12931f51bb57d186e"),
    (802, 56, 0.66, 12.13, 2, -33.1, "7201b1d7a19f040ea35db2851904a1e9af2957ff90ee26a52ed6e6ea7746006e"),
    (804, 57, 0.47, 11.69, -11, -39.1, "6787d09db8d3245f09eec89346d4e83ca18a9bf120e4e63238e54cddabe8105a"),
    (807, 58, 0.71, 12.78, 10, -39.0, "f8ce99e4177ebe7053c7514d4d46f1bb93467c5d686d597deb43b094415f5b5c"),
    (809, 59, 0.66, 9.2, -2, -34.0, "9de2fe88ba929be574a0c20b26819b0464eb5604d77df43b4c39859400cef81d"),
    (812, 60, 0.61, 10.72, -4, -37.6, "03a13ed7684df4c365106bb5dace58963ad1c8a3c9fd61c3b6424a3c367ff4fe"),
    (814, 61, 0.67, 8.63, 3, -37.0, "a82126b2fe726fcc16db75eed7151e28aa703615fad2a0aa83a4ce8bee93cef6"),
    (816, 62, 0.74, 10.51, 0, -36.2, "43a36b65ccc410e188624a5b9017ab1ce8271dc0fce22533eb437c247faac151"),
    (819, 63, 0.74, 8.84, 6, -34.5, "6f02f20293d061b8e1243aa6374d1c9a54d5a9647859a44ab2e6901eaa6f07ff"),
]
# 아쟁 트레몰로: (확장 번호, 건반, 시작 초, 끝 초, 음높이 센트, RMS dB, SHA-256). 국악 음정이라 건반마다 하나를 골라 맞춘다
AJAENG_TREMOLO = [
    (993, 51, 0.7, 4.74, 12, -28.7, "0c41d52cdcdcdf17201834bba503e0a3b55bfaf562644a2e7d700691fff567de"),
    (983, 53, 0.75, 6.22, 32, -41.1, "dbcbfbb7fb5ed1b4f4149b2626d446207d9b0841024b26c722044b9defd6335a"),
    (984, 54, 0.63, 5.49, -46, -30.1, "5fb43fdc7fd638d4703557af4e78a4bea430dc114e9f40072fe3c8bfbe0df66f"),
    (985, 55, 0.71, 6.36, 40, -38.7, "998050777ee39afc2b5dfc15e3152ce43595b66382de468fc99df9df74e3c59f"),
    (999, 58, 0.75, 4.01, 12, -27.9, "09fa45e2dd39f2a25a4e02b19fca66967227782410a5eee7dcfd5ea11158aaa1"),
    (982, 59, 0.63, 4.15, -24, -34.2, "34a7945343cc54dfd72ea24ef74043bdb4a2b36d54401c4203606bb520e07292"),
    (1001, 60, 0.72, 5.2, 22, -29.3, "069c88a164042b17b65e1f741514349fc097ead3f638e20fcda57f362bfcbc74"),
    (992, 63, 0.75, 5.2, -10, -38.1, "c235b6598b225aba8132581820e63ac7d45d6bfb223233d868c47abaf1777c28"),
    (994, 65, 0.74, 4.0, 18, -35.7, "b9d73d7a35188938db87ddbbe28209863929fc27e3379f97c542595d45b014c9"),
    (981, 67, 0.64, 7.2, -4, -47.4, "3f535ff403a9fca95dfd883aff2448d04deeccd5e034ba80b9aa18311d625e82"),
]
# 아쟁 술폰티첼로: (확장 번호, 건반, 시작 초, 끝 초, 음높이 센트, RMS dB, SHA-256). 국악 음정이라 건반마다 하나를 골라 맞춘다
AJAENG_SULPONT = [
    (979, 60, 0.66, 7.94, 38, -47.0, "e88b28a3eb82888884d1b87bab9bd43c797ace5ae7e6d61f0c7c1f7da721b836"),
    (974, 65, 0.45, 8.96, -36, -50.2, "aed11b72ec13c589b1b3449c553ed720fd147b096ad3c996728fbc3adb522d02"),
    (976, 67, 0.68, 9.51, 2, -48.5, "08e47386690b0e9fc7e5beb6610edbb47f114dd97ce00abaf9ed79185859ef05"),
    (977, 68, 0.55, 8.25, 30, -51.4, "31237fabd2531cf90f966f048fbed795effcc5b35d195fcd81b8f40c58da7cdb"),
    (980, 69, 0.7, 4.85, -46, -37.9, "b43f30a54ed74ee31d52d72f6997c1d06b96077d4b020bb8ed3e93496645fb5d"),
]
# 징 키트: (번호, 종류 extend 또는 monotone, 건반, 이름, 시작 초, 봉우리 dB, SHA-256)
JING = [
    (2334, "monotone", 46, "징 강", 0.2, -0.3, "27c87a9b93a4b8483a785f23b09f312f896307b7177779fd7f962d27fa719718"),
    (2335, "monotone", 45, "징 약", 0.22, -2.1, "90ee1200ae23f1af32f2de1606a520d9020415fab41bcaae19852d19dd9ad321"),
    (1002, "extend", 60, "아쟁 효과음 1", 0.83, -36.1, "094cb8478a1e6b18c7a15f8f9a3e431d9dd795ca265c3cd133ee33f64a8c498d"),
    (1003, "extend", 61, "아쟁 효과음 2", 0.8, -39.1, "c8f8002f494b9065ff621578bdf1080fdacb8b881ffbf58bc79dbabc729c1bd2"),
    (1004, "extend", 62, "아쟁 효과음 3", 0.94, -30.3, "dbd5a8391b8260031768a9660f9d0395344cb7c410a6c96d54934fc15844e0a2"),
]

# 대금: (파일 이름의 음, 잰 주파수 Hz, SHA-256). 주파수는 0.3~2.5초의 YIN (10 ms 마다) 가운데 값이다 (요성의 폭이 0.1~0.6 반음).
# 같은 소리를 두 이름으로 둔 파일은 하나만 쓴다
DAEGEUM_FILES = [
    ("C4", 266.65, "9dca6c348d958e1027ad1d68569a779f13fa1febd2b58833235f81a0dc0660e2"),
    ("D#4", 318.47, "6ffb16f68c6d88574c9a37e1aa20482507ccbe528327ddd1511a2c72a04d25ed"),
    ("F4", 345.45, "cb22f772644c3fcfa0d0f484e1c8959daf5b93d9e6cb06d6e3acb276dcabc9e7"),
    ("F#4", 369.83, "598b89f32f697d55edf3132b4ffcc66cb05fa7dffc01882f78be289e4a7c740a"),
    ("G#4", 418.05, "2244a6b4d7c6e35e32e5387684380712fcd80c9f709ede4ddd9808f6cf0efc69"),
    ("A#4", 460.81, "cc7ca5cfcc975d32637b5b90ec27a04c6de12eb9be79c83b011c42b54bc0f939"),
    ("B4", 504.19, "242b562da479b7030a7f032c1be32bbea2eef5404462f179e530427af49e432e"),
    ("C5", 532.62, "5cbe42374a5d8b1a6374967008bd7baa38a5df2a13c41a2707b27c4ab98e9c32"),
    ("D#5", 624.84, "c52f4363d145a8ebe489462e2fe45d42f4d8e2a9a92869c58b899446f49899b5"),
    ("F5", 685.35, "fde8abc5864f7b73dc95d1e8584d56a2b7cf3ffec591ae947cabf8576b59d2e7"),
    ("F#5", 736.37, "81b61c4c6784f9592fb7a2374e941fa07234d400f645442e98c94f3a41684fb1"),
    ("G#5", 827.69, "e27ba818925497e7888d7897f33381c918b101b1b77a3f89542fb18cfd911421"),
    ("A#5", 937.71, "6bff21edc0db94569788243b2dc1dffa83d406278ee292956b0733b7ecbf36a0"),
    ("B5", 1014.94, "f79b4b4e93772aa74225a6055aeaaa275f56d911b91bce7db51231483ae982e2"),
    ("C6", 1070.50, "57de61401b09daa8a893964b006874479aa2d5f0d4c898ed1ade024aaf9342ef"),
    ("D6", 1204.92, "5fbcc2063bf4f05bf90f2c14e94d28e8db6bb55d4cee5282f895b1b21eada0a1"),
    ("E6", 1346.75, "9921ce54e72f7fae19932b46b1b4106eaf5679c696a5d263665b9eb80cfdaf26"),
    ("F#6", 1525.67, "b00b2cd244b5cf736f09c71706154e3d29297c753697040ec94632ce9e7f9331"),
]

TARGET_DB = -20.0   # 대금 녹음을 맞추는 RMS


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(path, digest, url, form=None):
    """없거나 체크섬이 다르면 curl 로 받는다. form 이 있으면 그 값을 POST 로 보낸다 (국립국악원의 사용 목적 양식)"""
    if os.path.exists(path) and sha256(path) == digest:
        return False
    os.makedirs(os.path.dirname(path), exist_ok=True)
    cmd = ["curl", "-fsSL", "--retry", "3", "-A", "InitialDAW build", "-o", path + ".part"]
    for key, value in (form or {}).items():
        cmd += ["--data-urlencode", f"{key}={value}"]
    subprocess.run(cmd + [url], check=True)
    if sha256(path + ".part") != digest:
        os.remove(path + ".part")
        sys.exit(f"[InitialDAW] {os.path.basename(path)} 의 SHA-256 이 맞지 않습니다 ({url})")
    os.replace(path + ".part", path)
    return True


def ngc(kind, number):
    return (NGC_EXTEND if kind == "extend" else NGC_MONOTONE), dict(NGC_FORM, id=str(number), arrId="")


def read_wav(path, start=0.0, end=None):
    """16비트 PCM WAV 의 [start, end) 초 -> (채널마다의 실수 목록, 샘플레이트)"""
    with wave.open(path) as w:
        ch, rate, width, frames = w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()
        if width != 2:
            sys.exit(f"[InitialDAW] 16비트 WAV 가 아닙니다: {path}")
        a = max(0, min(frames, int(start * rate)))
        b = frames if end is None else max(a, min(frames, int(end * rate)))
        w.setpos(a)
        data = array.array("h")
        data.frombytes(w.readframes(b - a))
    if sys.byteorder == "big":
        data.byteswap()
    return [[v / 32768.0 for v in data[c::ch]] for c in range(ch)], rate


def rms_db(channels, start, end):
    n, acc = 0, 0.0
    for c in channels:
        for v in c[start:end]:
            acc += v * v
        n += max(0, end - start)
    return 10 * math.log10(max(1e-12, acc / max(1, n)))


def key_ranges(keys, low_extend=2, high_extend=2):
    """정렬한 건반마다 (lokey, hikey): 이웃 녹음과의 가운데에서 나눈다"""
    out = []
    for i, k in enumerate(keys):
        lo = keys[i - 1] + (k - keys[i - 1]) // 2 + 1 if i > 0 else k - low_extend
        hi = k + (keys[i + 1] - k) // 2 if i + 1 < len(keys) else k + high_extend
        out.append((lo, hi))
    return out


def median(values):
    s = sorted(values)
    return s[len(s) // 2]


def write_text(path, text):
    if os.path.exists(path) and open(path, encoding="utf-8").read() == text:
        return False
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)
    return True


def header(name, source):
    return f"// InitialDAW {name}: tools/make-gugak-sfz.py 가 쓴다 (손으로 고치지 않는다). {source}\n"


def make_gayageum(root, out):
    keys = [g[1] for g in GAYAGEUM]
    centre = [median(g[4][i] for g in GAYAGEUM) for i in range(3)]
    lines = [header("가야금", "국립국악원 「국악기 디지털 음원」 가야금 단음 (윤다영, 공공누리 제1유형)"),
             "<global> ampeg_attack=0.001 ampeg_release=0.8 amp_veltrack=30\n"]
    for (number, key, onsets, cents, peaks, _), (lo, hi) in zip(GAYAGEUM, key_ranges(keys, 2, 2)):
        for i, layer in enumerate(("p", "m", "f")):
            start = max(0, onsets[i] - 240)
            end = onsets[i + 1] - 2400 if i < 2 else start + 7 * 48000
            vol = max(-6.0, min(6.0, centre[i] - peaks[i]))
            lines.append(f"<region> sample=../ngc/gayageum/{number}.wav offset={start} end={end} pitch_keycenter={key} tune={-cents[i]} "
                         f"volume={vol:.1f} lovel={LAYERS[layer][0]} hivel={LAYERS[layer][1]} lokey={lo} hikey={hi}\n")
    return write_text(os.path.join(out, "gayageum.sfz"), "".join(lines))


def make_haegeum(root, out):
    lines = [header("해금", "국립국악원 「국악기 디지털 음원」 해금 기본음계 (공공누리 제1유형)"),
             "<global> ampeg_attack=0.005 ampeg_release=0.25 amp_veltrack=30\n"]
    for number, layer, _, notes in HAEGEUM:
        centre = median(n[4] for n in notes)
        rate = 44100
        for (key, start, dur, cents, rms), (lo, hi) in zip(notes, key_ranges([n[0] for n in notes], 2, 2)):
            a, b = int((start - 0.02) * rate), int((start + dur + 0.05) * rate)
            ls, le = int((start + 0.3 * dur) * rate), int((start + 0.85 * dur) * rate)
            xf = min(0.15, 0.3 * (le - ls) / rate)
            vol = max(-10.0, min(10.0, centre - rms))
            lines.append(f"<region> sample=../ngc/haegeum/{number}.wav offset={a} end={b} pitch_keycenter={key} tune={-cents} volume={vol:.1f} "
                         f"lovel={LAYERS[layer][0]} hivel={LAYERS[layer][1]} lokey={lo} hikey={hi} "
                         f"loop_mode=loop_continuous loop_start={ls} loop_end={le} loop_crossfade={xf:.3f}\n")
    return write_text(os.path.join(out, "haegeum.sfz"), "".join(lines))


def make_janggu(root, out):
    target = {"f": -1.0, "m": -6.0, "p": -12.0}
    lines = [header("장구", "국립국악원 「국악기 디지털 음원」 장구 단타 (공공누리 제1유형)"),
             "<global> loop_mode=one_shot ampeg_release=0.3 amp_veltrack=30\n"]
    for number, key, _, layer, peak, _ in JANGGU:
        lines.append(f"<region> sample=../ngc/janggu/{number}.wav offset={int(0.19 * 44100)} key={key} volume={target[layer] - peak:.1f} "
                     f"lovel={LAYERS[layer][0]} hivel={LAYERS[layer][1]}\n")
    return write_text(os.path.join(out, "janggu.sfz"), "".join(lines))


def make_ajaeng(root, out):
    """길게 (기본), 트레몰로, 술폰티첼로 의 SFZ 셋"""
    changed = False
    for name, table, attack, release, low, high in (("ajaeng", AJAENG, 0.06, 0.6, 1, 2), ("ajaeng-tremolo", AJAENG_TREMOLO, 0.05, 0.4, 2, 2),
                                                    ("ajaeng-sulpont", AJAENG_SULPONT, 0.05, 0.4, 2, 2)):
        centre = median(t[5] for t in table)
        lines = [header("아쟁", f"국립국악원 「국악기 디지털 음원」 아쟁 ({name}, 공공누리 제1유형)"),
                 f"<global> ampeg_attack={attack} ampeg_release={release} amp_veltrack=40\n"]
        for (number, key, start, end, cents, rms, _), (lo, hi) in zip(table, key_ranges([t[1] for t in table], low, high)):
            lines.append(f"<region> sample=../ngc/ajaeng/{number}.wav offset={int((start - 0.02) * 48000)} end={int((end + 0.2) * 48000)} "
                         f"pitch_keycenter={key} tune={-cents} volume={max(-10.0, min(10.0, centre - rms)):.1f} lokey={lo} hikey={hi}\n")
        changed |= write_text(os.path.join(out, name + ".sfz"), "".join(lines))
    return changed


def make_jing(root, out):
    """징 강, 약과 아쟁 효과음 (끝까지 울린다)"""
    lines = [header("징", "국립국악원 「국악기 디지털 음원」 징 타격음과 아쟁 효과음 (공공누리 제1유형)"),
             "<global> loop_mode=one_shot ampeg_release=0.5 amp_veltrack=40\n"]
    for number, kind, key, name, start, level, _ in JING:
        rate = 44100 if kind == "monotone" else 48000
        vol = (-1.0 if key == 46 else -7.0) - level if kind == "monotone" else -30.0 - level
        folder = "jing" if kind == "monotone" else "ajaeng"
        lines.append(f"<region> sample=../ngc/{folder}/{number}.wav offset={int(max(0.0, start - 0.01) * rate)} key={key} volume={vol:.1f}\n")
    return write_text(os.path.join(out, "jing.sfz"), "".join(lines))


def make_daegeum(root, out):
    entries = []
    for name, hz, _ in DAEGEUM_FILES:
        midi = 69 + 12 * math.log2(hz / 440.0)
        key = int(round(midi))
        channels, rate = read_wav(os.path.join(root, "daegeum", f"Daegeum_{name}.wav"), 0.0, 1.5)
        level = rms_db(channels, 0, len(channels[0]))
        entries.append((key, name, round(100 * (midi - key)), max(-12.0, min(12.0, TARGET_DB - level))))
    entries.sort()
    lines = [header("대금", "jonijonii/Daegeum-Sanjo-Sampler 의 국립국악원 산조대금 음계 녹음 (공공누리 제1유형)"),
             "<global> ampeg_attack=0.02 ampeg_release=0.3 amp_veltrack=70\n"]
    for (key, name, cents, vol), (lo, hi) in zip(entries, key_ranges([e[0] for e in entries], 3, 3)):
        lines.append(f"<region> sample=../daegeum/Daegeum_{name}.wav pitch_keycenter={key} tune={-cents} volume={vol:.1f} "
                     f"lokey={lo} hikey={hi}\n")
    return write_text(os.path.join(out, "daegeum.sfz"), "".join(lines))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root", help="external/gugak")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    got = 0
    for number, _, _, _, _, digest in GAYAGEUM:
        url, form = ngc("extend", number)
        got += fetch(os.path.join(root, "ngc", "gayageum", f"{number}.wav"), digest, url, form)
    for number, _, digest, _ in HAEGEUM:
        url, form = ngc("monotone", number)
        got += fetch(os.path.join(root, "ngc", "haegeum", f"{number}.wav"), digest, url, form)
    for number, _, _, _, _, digest in JANGGU:
        url, form = ngc("monotone", number)
        got += fetch(os.path.join(root, "ngc", "janggu", f"{number}.wav"), digest, url, form)
    for table in (AJAENG, AJAENG_TREMOLO, AJAENG_SULPONT):
        for number, *_, digest in table:
            url, form = ngc("extend", number)
            got += fetch(os.path.join(root, "ngc", "ajaeng", f"{number}.wav"), digest, url, form)
    for number, kind, _, _, _, _, digest in JING:
        url, form = ngc(kind, number)
        got += fetch(os.path.join(root, "ngc", "jing" if kind == "monotone" else "ajaeng", f"{number}.wav"), digest, url, form)
    for name, _, digest in DAEGEUM_FILES:
        got += fetch(os.path.join(root, "daegeum", f"Daegeum_{name}.wav"), digest, DAEGEUM.format(name=name.replace("#", "%23")))
    out = os.path.join(root, "InitialDAW")
    os.makedirs(out, exist_ok=True)
    makers = (("gayageum", make_gayageum), ("haegeum", make_haegeum), ("janggu", make_janggu), ("ajaeng", make_ajaeng), ("jing", make_jing),
              ("daegeum", make_daegeum))
    changed = [name for name, fn in makers if fn(root, out)]
    print(f"[InitialDAW] 국악기: 받은 파일 {got}개, 새로 쓴 SFZ {', '.join(changed) if changed else '없음'} ({out})")


if __name__ == "__main__":
    main()
