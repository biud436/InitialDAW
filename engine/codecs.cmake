# FLAC 과 Ogg Vorbis 코덱 (BSD): ./build.sh 가 external/flac, external/libogg, external/libvorbis 에 릴리스 소스를 받는다.
# 엔진과 같은 최적화(-O3)로, 빠른 수학 계산 없이 정적 라이브러리로 빌드한다 (같은 오디오가 같은 바이트로 인코딩된다).

set(MUSIC_RENDER_FLAC_DIR "${CMAKE_SOURCE_DIR}/external/flac/src" CACHE PATH "libFLAC release source tree")
set(MUSIC_RENDER_OGG_DIR "${CMAKE_SOURCE_DIR}/external/libogg/src" CACHE PATH "libogg release source tree")
set(MUSIC_RENDER_VORBIS_DIR "${CMAKE_SOURCE_DIR}/external/libvorbis/src" CACHE PATH "libvorbis release source tree")

if(NOT EXISTS "${MUSIC_RENDER_FLAC_DIR}/src/libFLAC/stream_encoder.c"
   OR NOT EXISTS "${MUSIC_RENDER_OGG_DIR}/src/framing.c"
   OR NOT EXISTS "${MUSIC_RENDER_VORBIS_DIR}/lib/vorbisenc.c")
  message(FATAL_ERROR "FLAC, libogg or libvorbis sources are missing: run ./build.sh --fetch")
endif()

# ---- libogg ----
set(MUSIC_RENDER_OGG_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/ogg-include")
file(WRITE "${MUSIC_RENDER_OGG_GENERATED}/ogg/config_types.h.in"
"#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__
#include <stdint.h>
typedef int16_t ogg_int16_t;
typedef uint16_t ogg_uint16_t;
typedef int32_t ogg_int32_t;
typedef uint32_t ogg_uint32_t;
typedef int64_t ogg_int64_t;
typedef uint64_t ogg_uint64_t;
#endif
")
configure_file("${MUSIC_RENDER_OGG_GENERATED}/ogg/config_types.h.in" "${MUSIC_RENDER_OGG_GENERATED}/ogg/config_types.h" COPYONLY)
add_library(music_render_ogg STATIC
  "${MUSIC_RENDER_OGG_DIR}/src/bitwise.c"
  "${MUSIC_RENDER_OGG_DIR}/src/framing.c")
target_include_directories(music_render_ogg SYSTEM PUBLIC "${MUSIC_RENDER_OGG_DIR}/include" "${MUSIC_RENDER_OGG_GENERATED}")

# ---- libvorbis (인코더, 디코더, vorbisfile) ----
add_library(music_render_vorbis STATIC
  "${MUSIC_RENDER_VORBIS_DIR}/lib/analysis.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/bitrate.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/block.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/codebook.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/envelope.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/floor0.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/floor1.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/info.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/lookup.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/lpc.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/lsp.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/mapping0.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/mdct.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/psy.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/registry.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/res0.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/sharedbook.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/smallft.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/synthesis.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/vorbisenc.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/vorbisfile.c"
  "${MUSIC_RENDER_VORBIS_DIR}/lib/window.c")
target_include_directories(music_render_vorbis SYSTEM PUBLIC "${MUSIC_RENDER_VORBIS_DIR}/include" PRIVATE "${MUSIC_RENDER_VORBIS_DIR}/lib")
target_link_libraries(music_render_vorbis PUBLIC music_render_ogg)

# ---- libFLAC (Ogg 감싸기는 쓰지 않는다) ----
set(MUSIC_RENDER_FLAC_SOURCES bitmath bitreader bitwriter cpu crc fixed float format lpc lpc_intrin_neon md5 memory
    metadata_iterators metadata_object stream_decoder stream_encoder stream_encoder_framing window)
list(TRANSFORM MUSIC_RENDER_FLAC_SOURCES PREPEND "${MUSIC_RENDER_FLAC_DIR}/src/libFLAC/")
list(TRANSFORM MUSIC_RENDER_FLAC_SOURCES APPEND ".c")
add_library(music_render_flac STATIC ${MUSIC_RENDER_FLAC_SOURCES})
target_include_directories(music_render_flac SYSTEM PUBLIC "${MUSIC_RENDER_FLAC_DIR}/include" PRIVATE "${MUSIC_RENDER_FLAC_DIR}/src/libFLAC/include")
target_compile_definitions(music_render_flac
  PUBLIC FLAC__NO_DLL=1
  PRIVATE PACKAGE_VERSION="1.4.3" HAVE_LROUND=1 FLAC__HAS_OGG=0 NDEBUG=1 HAVE_STDINT_H=1)
if(APPLE)
  target_compile_definitions(music_render_flac PRIVATE FLAC__SYS_DARWIN=1)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  # 이전 JUCE 빌드와 같은 코드 경로 (NEON 내장 함수를 쓴다)
  target_compile_definitions(music_render_flac PRIVATE FLAC__CPU_ARM64=1 FLAC__HAS_NEONINTRIN=1 FLAC__HAS_A64NEONINTRIN=1)
endif()

foreach(codec music_render_ogg music_render_vorbis music_render_flac)
  set_target_properties(${codec} PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
  target_compile_options(${codec} PRIVATE -O3 -w)
endforeach()
