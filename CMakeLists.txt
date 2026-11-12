# InitialDAW: 음악 엔진(engine/)과 그 위의 DAW 앱(app/)
#
# ./build.sh 가 의존 소스를 받고, 설정하고, 빌드하고, 테스트한다. JUCE 8 과 sfizz 는 external/ 에 받아 쓴다 (커밋하지 않는다).

cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
project(InitialDAW VERSION 0.1.0 LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)   # 엔진의 영상 (AVFoundation) 과 앱의 자판 받기
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(MUSIC_RENDER_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/juce" CACHE PATH "JUCE 8 source tree")
if(NOT EXISTS "${MUSIC_RENDER_JUCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found at ${MUSIC_RENDER_JUCE_DIR}. Run ./build.sh (it clones JUCE 8.0.10) or pass -DMUSIC_RENDER_JUCE_DIR=<path>.")
endif()
add_subdirectory("${MUSIC_RENDER_JUCE_DIR}" juce EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# sfizz (SFZ 샘플러, BSD-2-Clause). external/sfizz 에 받아 정적 라이브러리로 넣는다.
# ---------------------------------------------------------------------------
option(MUSIC_RENDER_WITH_SFIZZ "Build SFZ support with sfizz" ON)
set(MUSIC_RENDER_SFIZZ_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/sfizz" CACHE PATH "sfizz source tree")
if(MUSIC_RENDER_WITH_SFIZZ)
  if(NOT EXISTS "${MUSIC_RENDER_SFIZZ_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "sfizz not found at ${MUSIC_RENDER_SFIZZ_DIR}. Run ./build.sh or configure with -DMUSIC_RENDER_WITH_SFIZZ=OFF.")
  endif()
  foreach(opt SFIZZ_JACK SFIZZ_RENDER SFIZZ_SHARED SFIZZ_TESTS SFIZZ_DEMOS SFIZZ_DEVTOOLS SFIZZ_BENCHMARKS ENABLE_LTO SFIZZ_USE_SNDFILE SFIZZ_GIT_SUBMODULE_CHECK)
    set(${opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  # sfizz 에 딸린 atomic_queue 는 새 clang 이 기본 오류로 막는 문법을 쓴다. sfizz 디렉터리에서만 끈다.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-Wno-missing-template-arg-list-after-template-kw MUSIC_RENDER_HAS_TEMPLATE_KW_FLAG)
  set(MUSIC_RENDER_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  if(MUSIC_RENDER_HAS_TEMPLATE_KW_FLAG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-template-arg-list-after-template-kw")
  endif()
  add_subdirectory("${MUSIC_RENDER_SFIZZ_DIR}" sfizz EXCLUDE_FROM_ALL)
  set(CMAKE_CXX_FLAGS "${MUSIC_RENDER_SAVED_CXX_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# LAME (MP3 인코더, LGPL-2.0). ./build.sh 가 external/lame/install 에 정적 라이브러리로 빌드한다. 없으면 MP3 내보내기를 끈다.
# ---------------------------------------------------------------------------
set(MUSIC_RENDER_LAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/lame/install" CACHE PATH "LAME install prefix")
if(EXISTS "${MUSIC_RENDER_LAME_DIR}/lib/libmp3lame.a" AND EXISTS "${MUSIC_RENDER_LAME_DIR}/include/lame/lame.h")
  add_library(music_render_lame STATIC IMPORTED)
  set_target_properties(music_render_lame PROPERTIES
    IMPORTED_LOCATION "${MUSIC_RENDER_LAME_DIR}/lib/libmp3lame.a"
    INTERFACE_INCLUDE_DIRECTORIES "${MUSIC_RENDER_LAME_DIR}/include")
else()
  message(STATUS "LAME not found at ${MUSIC_RENDER_LAME_DIR}: MP3 export is disabled (./build.sh builds it)")
endif()

enable_testing()
add_subdirectory(engine)

# Music IR (tools/musicir): 생성기가 곡 곁에 쓰는 의미 계층과 조회, 검사 명령행
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME music_ir COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/musicir/tests/test_music_ir.py"
           "$<TARGET_FILE:music-render>")
  # 시험에 쓰는 곡(songs/)이 없는 사본에서는 건너뛴다
  set_tests_properties(music_ir PROPERTIES SKIP_RETURN_CODE 77)
endif()

option(INITIALDAW_BUILD_APP "Build the DAW application" ON)
if(INITIALDAW_BUILD_APP AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
  add_subdirectory(app)
endif()
