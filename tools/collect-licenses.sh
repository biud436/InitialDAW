#!/usr/bin/env bash
# 릴리스 압축 파일에 넣을 라이선스 문서를 모은다: 이 저장소, JUCE 와 그 안의 구성 요소, sfizz 와 그 라이브러리, LAME, 악보 글꼴 Bravura,
# libFLAC, libogg, libvorbis.
#
#   tools/collect-licenses.sh <폴더>
#
# external/ 에 JUCE, sfizz, LAME, Bravura 가 받아져 있어야 한다 (./build.sh). 없는 파일이 하나라도 있으면 실패한다.
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "사용법: tools/collect-licenses.sh <폴더>" >&2
  exit 2
fi
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$1"
mkdir -p "$OUT"
J="$REPO/external/juce"
S="$REPO/external/sfizz"
L="$REPO/external/lame/src"

copy() {   # copy <원본> <이름>
  if [ ! -f "$1" ]; then
    echo "없는 라이선스 파일: $1" >&2
    exit 1
  fi
  cp "$1" "$OUT/$2"
}

copy "$REPO/LICENSE" "InitialDAW-AGPL-3.0.txt"
copy "$REPO/THIRD-PARTY.md" "THIRD-PARTY.md"
copy "$J/LICENSE.md" "JUCE.md"
copy "$J/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt" "VST3_SDK.txt"
copy "$J/modules/juce_audio_formats/codecs/flac/Flac Licence.txt" "FLAC.txt"
copy "$J/modules/juce_audio_formats/codecs/oggvorbis/Ogg Vorbis Licence.txt" "OggVorbis.txt"
copy "$J/modules/juce_graphics/image_formats/pnglib/LICENSE" "libpng.txt"
copy "$J/modules/juce_graphics/image_formats/jpglib/README" "jpeglib.txt"
copy "$S/LICENSE" "sfizz.txt"
copy "$S/external/abseil-cpp/LICENSE" "abseil-cpp.txt"
copy "$S/external/atomic_queue/LICENSE" "atomic_queue.txt"
copy "$S/external/cephes/LICENSE.txt" "cephes.txt"
copy "$S/external/filesystem/LICENSE" "ghc-filesystem.txt"
copy "$S/external/invoke.hpp/LICENSE.md" "invoke.hpp.md"
copy "$S/external/jsl/LICENSE.md" "jsl.md"
copy "$S/external/simde/COPYING" "simde.txt"
copy "$S/external/st_audiofile/LICENSE.md" "st_audiofile.md"
copy "$S/external/st_audiofile/thirdparty/dr_libs/LICENSE" "dr_libs.txt"
copy "$S/external/st_audiofile/thirdparty/libaiff/LICENSE" "libaiff.txt"
copy "$S/external/st_audiofile/thirdparty/stb_vorbis/LICENSE" "stb_vorbis.txt"
copy "$S/external/st_audiofile/thirdparty/wavpack/COPYING" "wavpack.txt"
copy "$S/src/external/cpuid/LICENSE.rst" "cpuid.rst"
copy "$S/src/external/hiir/license.txt" "hiir.txt"
copy "$S/src/external/kiss_fft/COPYING" "kiss_fft.txt"
copy "$S/src/external/pugixml/LICENSE.md" "pugixml.md"
copy "$S/src/external/spline/LICENSE" "spline.txt"
copy "$S/src/external/tunings/LICENSE.md" "tunings.md"
copy "$L/COPYING" "LAME-LGPL-2.0.txt"
copy "$L/LICENSE" "LAME-LICENSE.txt"
copy "$REPO/external/bravura/OFL.txt" "Bravura-OFL-1.1.txt"
copy "$REPO/external/signalsmith-stretch/LICENSE.txt" "signalsmith-stretch.txt"
copy "$REPO/external/signalsmith-linear/LICENSE.txt" "signalsmith-linear.txt"
copy "$REPO/external/flac/src/COPYING.Xiph" "libFLAC-BSD.txt"
copy "$REPO/external/libogg/src/COPYING" "libogg-BSD.txt"
copy "$REPO/external/libvorbis/src/COPYING" "libvorbis-BSD.txt"
# threadpool 은 헤더 안에 SPDX 표시(zlib)만 있다
head -20 "$S/external/threadpool/ThreadPool.h" > "$OUT/threadpool-header.txt"

echo "$(ls "$OUT" | wc -l | tr -d ' ') files in $OUT"
