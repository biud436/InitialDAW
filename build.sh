#!/usr/bin/env bash
# InitialDAW 를 설정, 빌드하고 테스트를 돌린다.
#
#   ./build.sh              # 빌드와 테스트
#   ./build.sh --no-test    # 빌드만
#   ./build.sh --fetch      # 의존 소스(JUCE, sfizz, LAME, 악보 글꼴 Bravura, Signalsmith Stretch, FLAC, Ogg Vorbis)만 받는다
#   ./build.sh --samples    # VSCO 2 CE, Sonatina Symphonic Orchestra 일부, FreePats, Karoryfer 첼로도 받는다
#   ./build.sh --plugins    # 무료 VSTi Dexed 도 받는다 (macOS, external/plugins/dexed)
#
# 결과: build/app/InitialDAW_artefacts/Release/InitialDAW.app (DAW), build/engine/music-render (명령행 엔진).
# JUCE 8.0.10 과 sfizz 가 external/ 에 없으면 받는다. MP3 내보내기에 쓰는 LAME 4.0(LGPL-2.0)은 소스를 받아 SHA-256 을 확인하고
# external/lame/install 에 정적 라이브러리로 빌드한다. 앱의 악보 보기에 쓰는 SMuFL 글꼴 Bravura(SIL OFL 1.1)는 릴리스 파일을 받아
# SHA-256 을 확인하고 앱에 넣는다. 오디오 클립의 늘이기와 음높이 바꾸기는 Signalsmith Stretch 와 그 FFT 인 Signalsmith Linear
# (둘 다 MIT, 머리 파일만)를 고정한 커밋으로 받아 쓴다. FLAC 과 Ogg Vorbis 의 읽기와 쓰기는 libFLAC 1.4.3, libogg 1.3.5, libvorbis 1.3.7
# (모두 BSD) 릴리스 tar.xz 를 받아 SHA-256 을 확인하고 CMake 가 정적 라이브러리로 빌드한다.
# 샘플 악기는 --samples 로 받는다: VSCO 2 CE(CC0, 현악, 피아노, 타악기, 일부 목관, 약 2.8GB)와
# Sonatina Symphonic Orchestra(CC Sampling Plus 1.0, 현악, 합창, 호른, 트롬본, 오보에, 플루트, 클라리넷, 첼레스타, 그랜드 피아노, 약 560MB),
# FreePats 스페인 클래식 기타(CC0, 나일론 기타, 샘플 5.2MB), 드럼 세트 MuldjordKit(CC BY 4.0, 약 320MB), 일렉트릭 베이스(CC0, 12MB),
# 클린과 재즈 일렉트릭 기타(CC0, 약 400MB), 테너 색소폰(CC0, 32MB), Karoryfer x bigcat 독주 첼로(CC0, zip 133MB).
# 첼로는 우리가 쓴 SFZ 로 연주한다: tools/make-cello-sfz.py 가 샘플 곁에 쓴다 (빌드마다 다시 쓰고 내용이 같으면 그대로 둔다).
# 플러그인은 --plugins 로 받는다: Dexed 1.0.1 (GPL-3.0, DX7 FM 신스, VST3 와 AU). 앱과 링크하지 않고 실행 중에 불러온다.
# CMake 3.22 이상이 필요하다. PATH 의 cmake 가 낮으면 ~/Library/Python/*/bin/cmake (pip install --user cmake)를 찾는다.
set -euo pipefail

REPO="$(cd "$(dirname "$0")" && pwd)"
BUILD="$REPO/build"
JUCE_TAG="8.0.10"
SFIZZ_COMMIT="f5c6e29f23b8057867c08e88f5f6ac6738baa30b"   # 실행 파일을 만든 소스를 정확히 가리킬 수 있도록 고정한다 (THIRD-PARTY.md)
GUITAR_COMMIT="6f4eb1b092acc88f5448cea1a0001bd07b971af8"   # freepats/spanish-classical-guitar v1.0.0
# FreePats 저장소: 이름, 태그, 그 태그의 커밋
FREEPATS_REPOS=(
  "muldjordkit 2020-10-18 a8a3f96e3f1a88c84c90b18d0be182488b677672"
  "electric-bass-YR 2019-09-30 8dcb7ea9116f417273ef8c030d15e7b3aa654301"
  "electric-guitar-FSBS-clean 2026-08-07 192cf0d9bf2c4ba6ead8e3524ba3f78818e4fe91"
  "electric-guitar-FSBS-jazz 2026-08-07 9a5430a220725ee26f272f62de1381de8da982ed"
)
SAX_FILE="TenorSaxophone-SFZ+FLAC-20200717.tar.gz"
SAX_SHA256="ce3bac2d54ac964fc4b4d54cd20cc62135c9ccbedcf5954cd7e1046ad2a31066"   # freepats.zenvoid.org 의 Reed/TenorSaxophone 릴리스
CELLO_VERSION="1.001"
CELLO_SHA256="f340e2afc161de70b0484cda964f2e17e5b0c1e271fc18ade6a21bd0cc3c5e22"   # sfzinstruments/karoryfer-bigcat.cello 릴리스 zip (132,605,500 바이트)
LAME_VERSION="4.0"
LAME_SHA256="3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb"   # SourceForge 릴리스 tar.gz (Homebrew 의 값과 맞춰 확인)
BRAVURA_VERSION="1.482"
BRAVURA_SHA256="cdf0f893ee1fdb64b7f6713d71ee0dcfc349c0ac01429a8e451b01a9e79f5f3b"       # steinbergmedia/bravura 릴리스의 Bravura.otf (889,228 바이트)
BRAVURA_OFL_SHA256="c24929be7028026a65ee8894da1e3c36d2a4ccce0548d9ba8ba64509f46319ee"   # 같은 릴리스의 OFL.txt
STRETCH_TAG="1.4.0"
STRETCH_COMMIT="a670068d9aeb64913331d5cc29337b19a457a7df"   # Signalsmith-Audio/signalsmith-stretch (2026-09-25)
LINEAR_TAG="0.6.4"
LINEAR_COMMIT="de55e6a50ffcf6f8f43f649692d94691c7025151"    # Signalsmith-Audio/linear, Stretch 1.4.0 이 고정한 판
FLAC_VERSION="1.4.3"
FLAC_SHA256="6c58e69cd22348f441b861092b825e591d0b822e106de6eb0ee4d05d27205b70"     # github.com/xiph/flac 릴리스의 flac-1.4.3.tar.xz
OGG_VERSION="1.3.5"
OGG_SHA256="c4d91be36fc8e54deae7575241e03f4211eb102afb3fc0775fbbc1b740016705"      # downloads.xiph.org 의 libogg-1.3.5.tar.xz
VORBIS_VERSION="1.3.7"
VORBIS_SHA256="b33cc4934322bcbf6efcbacf49e3ca01aadbea4114ec9589d1b1e9d20f72954b"   # downloads.xiph.org 의 libvorbis-1.3.7.tar.xz
DEXED_VERSION="1.0.1"
DEXED_SHA256="4269448bc1312424cafd29aff615d891bc5905ebfc54b42ce70c56b9b10dc401"   # 릴리스의 artifact_md5sum.txt 와 맞춰 확인한 macOS zip
RUN_TESTS=1
FETCH_ONLY=0
FETCH_SAMPLES=0
FETCH_PLUGINS=0
for a in "$@"; do
  case "$a" in
    --no-test) RUN_TESTS=0 ;;
    --fetch) FETCH_ONLY=1 ;;
    --samples) FETCH_SAMPLES=1 ;;
    --plugins) FETCH_PLUGINS=1 ;;
    *) echo "모르는 인자: $a" >&2; exit 2 ;;
  esac
done

if [ ! -f "$REPO/external/juce/CMakeLists.txt" ]; then
  echo "[InitialDAW] JUCE $JUCE_TAG 을 external/juce 에 받습니다"
  git clone --depth 1 --branch "$JUCE_TAG" https://github.com/juce-framework/JUCE.git "$REPO/external/juce"
fi
if [ ! -f "$REPO/external/sfizz/CMakeLists.txt" ]; then
  echo "[InitialDAW] sfizz ($SFIZZ_COMMIT) 를 external/sfizz 에 받습니다 (하위 모듈 포함)"
  git init -q "$REPO/external/sfizz"
  git -C "$REPO/external/sfizz" remote add origin https://github.com/sfztools/sfizz.git
  git -C "$REPO/external/sfizz" fetch -q --depth 1 origin "$SFIZZ_COMMIT"
  git -C "$REPO/external/sfizz" checkout -q FETCH_HEAD
  git -C "$REPO/external/sfizz" submodule update -q --init --recursive --depth 1
elif [ "$(git -C "$REPO/external/sfizz" rev-parse HEAD 2>/dev/null)" != "$SFIZZ_COMMIT" ]; then
  echo "[InitialDAW] 경고: external/sfizz 가 고정한 커밋($SFIZZ_COMMIT)이 아닙니다. 지우고 다시 받으면 맞춰집니다" >&2
fi
LAME="$REPO/external/lame"
if [ ! -f "$LAME/src/configure" ]; then
  echo "[InitialDAW] LAME $LAME_VERSION 을 external/lame 에 받습니다"
  mkdir -p "$LAME"
  tarball="$LAME/lame-$LAME_VERSION.tar.gz"
  if [ ! -f "$tarball" ]; then
    curl -fL --retry 3 -o "$tarball.part" "https://downloads.sourceforge.net/project/lame/lame/$LAME_VERSION/lame-$LAME_VERSION.tar.gz"
    mv "$tarball.part" "$tarball"
  fi
  if ! echo "$LAME_SHA256  $tarball" | shasum -a 256 -c - >/dev/null; then
    echo "[InitialDAW] LAME tar.gz 의 SHA-256 이 맞지 않습니다: $tarball (지우고 다시 받으십시오)" >&2
    exit 1
  fi
  rm -rf "$LAME/src"
  mkdir -p "$LAME/src"
  tar -xzf "$tarball" -C "$LAME/src" --strip-components 1
fi
BRAVURA="$REPO/external/bravura"
mkdir -p "$BRAVURA"
for f in Bravura.otf OFL.txt; do
  if [ ! -f "$BRAVURA/$f" ]; then
    echo "[InitialDAW] 악보 글꼴 Bravura $BRAVURA_VERSION 의 $f 를 external/bravura 에 받습니다"
    curl -fL --retry 3 -o "$BRAVURA/$f.part" "https://github.com/steinbergmedia/bravura/releases/download/bravura-$BRAVURA_VERSION/$f"
    mv "$BRAVURA/$f.part" "$BRAVURA/$f"
  fi
done
if ! printf '%s  %s\n%s  %s\n' "$BRAVURA_SHA256" "$BRAVURA/Bravura.otf" "$BRAVURA_OFL_SHA256" "$BRAVURA/OFL.txt" | shasum -a 256 -c - >/dev/null; then
  echo "[InitialDAW] Bravura 파일의 SHA-256 이 맞지 않습니다: $BRAVURA (지우고 다시 받으십시오)" >&2
  exit 1
fi
# 사용: fetch_tarball <폴더> <URL> <SHA-256>. tar.xz 를 받아 확인하고 <폴더>/src 에 푼다
fetch_tarball() {
  local dir="$1" url="$2" sum="$3"
  local tarball="$dir/$(basename "$url")"
  if [ ! -f "$dir/src/.unpacked-$sum" ]; then
    echo "[InitialDAW] $(basename "$url") 을 $dir 에 받습니다"
    mkdir -p "$dir"
    if [ ! -f "$tarball" ]; then
      curl -fL --retry 3 -o "$tarball.part" "$url"
      mv "$tarball.part" "$tarball"
    fi
    if ! echo "$sum  $tarball" | shasum -a 256 -c - >/dev/null; then
      echo "[InitialDAW] $(basename "$url") 의 SHA-256 이 맞지 않습니다: $tarball (지우고 다시 받으십시오)" >&2
      exit 1
    fi
    rm -rf "$dir/src"
    mkdir -p "$dir/src"
    tar -xJf "$tarball" -C "$dir/src" --strip-components 1
    touch "$dir/src/.unpacked-$sum"
  fi
}
fetch_tarball "$REPO/external/flac" "https://github.com/xiph/flac/releases/download/$FLAC_VERSION/flac-$FLAC_VERSION.tar.xz" "$FLAC_SHA256"
fetch_tarball "$REPO/external/libogg" "https://downloads.xiph.org/releases/ogg/libogg-$OGG_VERSION.tar.xz" "$OGG_SHA256"
fetch_tarball "$REPO/external/libvorbis" "https://downloads.xiph.org/releases/vorbis/libvorbis-$VORBIS_VERSION.tar.xz" "$VORBIS_SHA256"
# 사용: fetch_tag <폴더> <URL> <태그> <커밋>
fetch_tag() {
  local dir="$1" url="$2" tag="$3" commit="$4"
  if [ ! -d "$dir/.git" ]; then
    echo "[InitialDAW] $url ($tag) 을 $dir 에 받습니다"
    git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "$url" "$dir"
  fi
  if [ "$(git -C "$dir" rev-parse HEAD)" != "$commit" ]; then
    echo "[InitialDAW] $dir 가 고정한 커밋($commit)이 아닙니다 (지우고 다시 받으십시오)" >&2
    exit 1
  fi
}
fetch_tag "$REPO/external/signalsmith-stretch" https://github.com/Signalsmith-Audio/signalsmith-stretch.git "$STRETCH_TAG" "$STRETCH_COMMIT"
fetch_tag "$REPO/external/signalsmith-linear" https://github.com/Signalsmith-Audio/linear.git "$LINEAR_TAG" "$LINEAR_COMMIT"
# 샘플 저장소를 필요한 폴더만 받는다. 이미 받았으면 폴더 목록만 맞춘다 (목록이 늘면 새 폴더가 들어온다).
# 사용: fetch_sparse <폴더> <URL> <브랜치> <경로...>
fetch_sparse() {
  local dir="$1" url="$2" branch="$3"
  shift 3
  if [ ! -d "$dir/.git" ]; then
    git clone --filter=blob:none --no-checkout --depth 1 --branch "$branch" "$url" "$dir"
    git -C "$dir" sparse-checkout init --no-cone
    git -C "$dir" sparse-checkout set --no-cone "$@"
    git -C "$dir" checkout -q "$branch"
  else
    git -C "$dir" sparse-checkout set --no-cone "$@"
  fi
}
# Karoryfer x bigcat 첼로(CC0) 릴리스 zip 을 받아 체크섬을 확인하고 external/karoryfer 에 푼다. 같은 판이 풀려 있으면 받지 않는다.
fetch_karoryfer_cello() {
  local dir="$REPO/external/karoryfer" zip="Karoryfer_Bigcat_cello.v$CELLO_VERSION.zip"
  [ "$(cat "$dir/Karoryfer_Bigcat_cello/.initialdaw-version" 2>/dev/null)" = "$CELLO_VERSION" ] && return 0
  mkdir -p "$dir"
  if [ ! -f "$dir/$zip" ]; then
    curl -fL --retry 3 -o "$dir/$zip.part" "https://github.com/sfzinstruments/karoryfer-bigcat.cello/releases/download/v$CELLO_VERSION/$zip"
    mv "$dir/$zip.part" "$dir/$zip"
  fi
  if ! echo "$CELLO_SHA256  $dir/$zip" | shasum -a 256 -c - >/dev/null; then
    echo "[InitialDAW] $zip 의 SHA-256 이 맞지 않습니다" >&2
    rm -f "$dir/$zip"
    exit 1
  fi
  rm -rf "$dir/Karoryfer_Bigcat_cello"
  unzip -q "$dir/$zip" -d "$dir"
  echo "$CELLO_VERSION" > "$dir/Karoryfer_Bigcat_cello/.initialdaw-version"
}
if [ "$FETCH_SAMPLES" = 1 ]; then
  echo "[InitialDAW] VSCO 2 CE (SFZ 브랜치)의 현악, 피아노, 타악기, 목관을 external/vsco2-ce 에 받습니다"
  fetch_sparse "$REPO/external/vsco2-ce" https://github.com/sgossner/VSCO-2-CE.git SFZ \
    '/*.sfz' '/LICENSE' '/README.md' '/Strings/' '/Keys/Upright Piano/' '/Keys/Upright Nr1/' '/Percussion/' \
    '/Woodwinds/Flute/' '/Woodwinds/Oboe/' '/Woodwinds/Clarinet/'
  echo "[InitialDAW] Sonatina Symphonic Orchestra 의 현악, 합창, 호른, 트롬본, 오보에, 플루트, 잉글리시 호른, 클라리넷, 첼레스타, 비브라폰, 그랜드 피아노를 external/sso 에 받습니다"
  SSO='/Sonatina Symphonic Orchestra'
  fetch_sparse "$REPO/external/sso" https://github.com/peastman/sso.git master \
    '/LICENSE' '/README.md' "$SSO/Chorus - Performance/" "$SSO/Brass - Performance/" "$SSO/Woodwinds - Performance/" \
    "$SSO/Percussion/Celeste.sfz" "$SSO/Samples/Chorus/" "$SSO/Samples-looped/Chorus/" "$SSO/Samples/Horn/" \
    "$SSO/Samples-looped/Horn/" "$SSO/Samples/Horns/" "$SSO/Samples-looped/Horns/" "$SSO/Samples/Trombones/" \
    "$SSO/Samples-looped/Trombones/" "$SSO/Samples/Oboe/" \
    "$SSO/Samples-looped/Oboe/" "$SSO/Samples/Flute/" "$SSO/Samples-looped/Flute/" "$SSO/Samples/Cor Anglais/" "$SSO/Samples-looped/Cor Anglais/" "$SSO/Samples/Clarinet/" \
    "$SSO/Samples-looped/Clarinet/" "$SSO/Samples/Celeste/" "$SSO/Strings - Performance/" \
    "$SSO/Samples/1st Violins/" "$SSO/Samples-looped/1st Violins/" "$SSO/Samples/2nd Violins/" "$SSO/Samples-looped/2nd Violins/" \
    "$SSO/Samples/Violas/" "$SSO/Samples-looped/Violas/" "$SSO/Samples/Celli/" "$SSO/Samples-looped/Celli/" \
    "$SSO/Samples/Basses/" "$SSO/Samples-looped/Basses/" "$SSO/Samples/Cello/" "$SSO/Samples-looped/Cello/" \
    "$SSO/Samples/Violin/" "$SSO/Samples-looped/Violin/" "$SSO/Percussion/Vibraphone.sfz" "$SSO/Samples/Vibraphone/" \
    "$SSO/Grand Piano/" "$SSO/Samples/Grand Piano/"
  echo "[InitialDAW] FreePats 스페인 클래식 기타를 external/freepats/spanish-classical-guitar 에 받습니다"
  GUITAR="$REPO/external/freepats/spanish-classical-guitar"
  if [ ! -d "$GUITAR/.git" ]; then
    mkdir -p "$REPO/external/freepats"
    git -c advice.detachedHead=false clone -q --depth 1 --branch v1.0.0 https://github.com/freepats/spanish-classical-guitar.git "$GUITAR"
  fi
  if [ "$(git -C "$GUITAR" rev-parse HEAD)" != "$GUITAR_COMMIT" ]; then
    echo "[InitialDAW] 경고: external/freepats/spanish-classical-guitar 가 고정한 커밋($GUITAR_COMMIT)이 아닙니다" >&2
  fi
  echo "[InitialDAW] FreePats 드럼 세트, 일렉트릭 베이스, 일렉트릭 기타 둘, 테너 색소폰을 external/freepats 에 받습니다"
  for spec in "${FREEPATS_REPOS[@]}"; do
    read -r name tag commit <<< "$spec"
    dir="$REPO/external/freepats/$name"
    if [ ! -d "$dir/.git" ]; then
      git -c advice.detachedHead=false clone -q --depth 1 --branch "$tag" "https://github.com/freepats/$name.git" "$dir"
    fi
    if [ "$(git -C "$dir" rev-parse HEAD)" != "$commit" ]; then
      echo "[InitialDAW] 경고: external/freepats/$name 이 고정한 커밋($commit)이 아닙니다" >&2
    fi
  done
  SAX="$REPO/external/freepats/tenor-saxophone"
  if [ ! -f "$SAX/TenorSaxophone-SFZ+FLAC-20200717/TenorSaxophone-20200717.sfz" ]; then
    mkdir -p "$SAX"
    curl -fL --retry 3 -o "$SAX/$SAX_FILE.part" "https://freepats.zenvoid.org/Reed/TenorSaxophone/$SAX_FILE"
    mv "$SAX/$SAX_FILE.part" "$SAX/$SAX_FILE"
    if ! echo "$SAX_SHA256  $SAX/$SAX_FILE" | shasum -a 256 -c - >/dev/null; then
      echo "[InitialDAW] $SAX_FILE 의 SHA-256 이 맞지 않습니다" >&2
      rm -f "$SAX/$SAX_FILE"
      exit 1
    fi
    tar -xzf "$SAX/$SAX_FILE" -C "$SAX"
  fi
  echo "[InitialDAW] Karoryfer x bigcat 첼로를 external/karoryfer 에 받습니다"
  fetch_karoryfer_cello
  echo "[InitialDAW] 국악기 (가야금, 해금, 대금, 장구, 아쟁, 징) 녹음을 external/gugak 에 받습니다 (파일마다 SHA-256 고정, 국립국악원 사이트에는 사용 목적 양식을 함께 보낸다)"
  mkdir -p "$REPO/external/gugak"
fi
# 받은 첼로 샘플 곁에 우리 SFZ 를 쓴다 (루프 지점, 조율, 크기 보정은 tools/make-cello-sfz.py 의 표)
if [ -d "$REPO/external/karoryfer/Karoryfer_Bigcat_cello/Samples/sus" ]; then
  python3 -I "$REPO/tools/make-cello-sfz.py" "$REPO/external/karoryfer/Karoryfer_Bigcat_cello"
fi
# 국악기 녹음 곁에 우리 SFZ 를 쓴다 (없는 녹음은 받는다, 뜯음과 음의 자리, 조율, 크기는 tools/make-gugak-sfz.py 의 표)
if [ -d "$REPO/external/gugak" ]; then
  python3 -I "$REPO/tools/make-gugak-sfz.py" "$REPO/external/gugak"
fi
# 릴리스 zip 을 받아 체크섬을 확인하고 푼다. 이미 풀려 있으면 받지 않는다.
fetch_dexed() {
  local dir="$REPO/external/plugins/dexed" zip="$REPO/external/plugins/Dexed-$DEXED_VERSION-macOS.zip"
  [ -d "$dir/Dexed.vst3" ] && return 0
  mkdir -p "$REPO/external/plugins"
  if [ ! -f "$zip" ]; then
    curl -fL --retry 3 -o "$zip.part" "https://github.com/asb2m10/dexed/releases/download/v$DEXED_VERSION/Dexed-$DEXED_VERSION-macOS.zip"
    mv "$zip.part" "$zip"
  fi
  if ! echo "$DEXED_SHA256  $zip" | shasum -a 256 -c - >/dev/null; then
    echo "[InitialDAW] Dexed zip 의 SHA-256 이 맞지 않습니다: $zip (지우고 다시 받으십시오)" >&2
    exit 1
  fi
  rm -rf "$dir"
  unzip -q "$zip" -d "$dir"
}
if [ "$FETCH_PLUGINS" = 1 ]; then
  if [ "$(uname)" = "Darwin" ]; then
    echo "[InitialDAW] Dexed $DEXED_VERSION 을 external/plugins/dexed 에 받습니다"
    fetch_dexed
  else
    echo "[InitialDAW] --plugins 는 지금 macOS 에서만 받습니다" >&2
  fi
fi
[ "$FETCH_ONLY" = 1 ] && exit 0

# LAME 은 인코더 라이브러리만 정적으로 빌드한다 (실행 파일과 디코더는 빼고). CMake 가 install/lib/libmp3lame.a 를 찾으면 링크한다.
if [ ! -f "$LAME/install/lib/libmp3lame.a" ]; then
  echo "[InitialDAW] LAME $LAME_VERSION 을 빌드합니다"
  lame_cflags="-O2"
  [ "$(uname)" = "Darwin" ] && lame_cflags="$lame_cflags -mmacosx-version-min=11.0"
  (cd "$LAME/src" && CFLAGS="$lame_cflags" ./configure -q --prefix="$LAME/install" --disable-shared --enable-static \
     --disable-frontend --disable-decoder --disable-dependency-tracking >/dev/null \
   && make -s -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >/dev/null && make -s install >/dev/null)
fi

version_ok() {
  local v; v="$("$1" --version 2>/dev/null | head -1 | awk '{print $3}')"
  python3 -c "import sys; v=tuple(int(x) for x in '$v'.split('.')[:2]); sys.exit(0 if v >= (3,22) else 1)" 2>/dev/null
}
CMAKE=""
for c in cmake "$HOME"/Library/Python/*/bin/cmake /opt/homebrew/bin/cmake /usr/local/bin/cmake; do
  if command -v "$c" >/dev/null 2>&1 && version_ok "$c"; then CMAKE="$c"; break; fi
done
if [ -z "$CMAKE" ]; then
  echo "CMake 3.22 이상이 필요합니다 (python3 -m pip install --user 'cmake<4' 또는 brew upgrade cmake)" >&2
  exit 2
fi

"$CMAKE" -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
"$CMAKE" --build "$BUILD" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
echo "[InitialDAW] 엔진: $BUILD/engine/music-render"
[ -d "$BUILD/app/InitialDAW_artefacts/Release/InitialDAW.app" ] && echo "[InitialDAW] 앱: $BUILD/app/InitialDAW_artefacts/Release/InitialDAW.app"

if [ "$RUN_TESTS" = 1 ]; then
  CTEST="$(dirname "$CMAKE")/ctest"
  [ -x "$CTEST" ] || CTEST=ctest
  (cd "$BUILD" && "$CTEST" --output-on-failure)
fi
