# 음악 엔진(music-render): Composition JSON → 연주 처리 → 가상 악기 → 믹서 → WAV (docs/plans/engine.md)
#
# 저장소 최상위 CMakeLists.txt 가 JUCE 와 sfizz 를 더한 뒤 이 디렉터리를 부른다. ./build.sh 가 설정, 빌드, 테스트를 한 번에 한다.

# ---------------------------------------------------------------------------
# 핵심 라이브러리. JUCE 모듈(오디오 장치와 GUI 포함)을 여기에 한 번만 컴파일하고, 명령행, 테스트, DAW 앱이 이것을 링크한다.
# ---------------------------------------------------------------------------
include("${CMAKE_CURRENT_SOURCE_DIR}/codecs.cmake")

add_library(music_render_core STATIC
  src/core/Diagnostics.cpp
  src/core/Json.cpp
  src/core/Log.cpp
  src/composition/Composition.cpp
  src/timing/MeterMap.cpp
  src/timing/TempoMap.cpp
  src/performance/Performance.cpp
  src/instruments/SimpleSynth.cpp
  src/instruments/Registry.cpp
  src/instruments/QuickSampler.cpp
  src/analysis/PitchTrack.cpp
  src/analysis/NoteSegments.cpp
  src/analysis/Onsets.cpp
  src/notation/MusicXml.cpp
  src/notation/Notation.cpp
  src/notation/Key.cpp
  src/rendering/Bounce.cpp
  src/rendering/OfflineRenderer.cpp
  src/analysis/Analysis.cpp
  src/analysis/LoudnessMeter.cpp
  src/analysis/MixMeasure.cpp
  src/io/AudioFile.cpp
  src/io/XmlWriter.cpp
  src/io/Codecs.cpp
  src/midi/MidiExport.cpp
  src/midi/MidiImport.cpp
  src/midi/MidiRecorder.cpp
  src/midi/Smf.cpp
  src/plugins/PluginEffect.cpp
  src/plugins/PluginHost.cpp
  src/mixer/Mixer.cpp
  src/instruments/AudioClips.cpp
  src/instruments/Profile.cpp
  src/instruments/SfzInstrument.cpp
  src/composition/Arrange.cpp
  src/composition/Document.cpp
  src/composition/NoteEdits.cpp
  src/composition/Groove.cpp
  src/composition/Templates.cpp
  src/composition/Harmony.cpp
  src/rendering/EventBuilder.cpp
  src/rendering/TrackChain.cpp
  src/realtime/AudioTakeRecorder.cpp
  src/realtime/RealtimePlayer.cpp
  src/realtime/WorkerPool.cpp
  src/rendering/Expression.cpp
  src/effects/Effects.cpp
  src/effects/Presets.cpp
  src/effects/Automation.cpp
  src/dsp/Fft.cpp
  src/dsp/Freeverb.cpp
  src/media/Timecode.cpp
  src/media/VideoCommon.cpp)

# 영상 (macOS 의 AVFoundation, ARC). 다른 플랫폼은 영상을 열지 못하는 자리표시
if(APPLE)
  target_sources(music_render_core PRIVATE src/media/Video.mm)
  set_source_files_properties(src/media/Video.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  target_link_libraries(music_render_core PUBLIC "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework CoreGraphics" "-framework ImageIO")
else()
  target_sources(music_render_core PRIVATE src/media/VideoStub.cpp)
endif()

target_include_directories(music_render_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
# Signalsmith Stretch 와 Linear (MIT, 머리 파일만): 오디오 클립의 늘이기와 음높이. ./build.sh 가 external/ 에 받는다. 없으면 클립을 늘이지 않는다
set(MUSIC_RENDER_STRETCH_DIR "${CMAKE_SOURCE_DIR}/external/signalsmith-stretch" CACHE PATH "Signalsmith Stretch source tree")
set(MUSIC_RENDER_LINEAR_DIR "${CMAKE_SOURCE_DIR}/external/signalsmith-linear" CACHE PATH "Signalsmith Linear source tree")
if(EXISTS "${MUSIC_RENDER_STRETCH_DIR}/include/signalsmith-stretch/signalsmith-stretch.h" AND EXISTS "${MUSIC_RENDER_LINEAR_DIR}/include/signalsmith-linear/stft.h")
  target_include_directories(music_render_core SYSTEM PRIVATE "${MUSIC_RENDER_STRETCH_DIR}/include" "${MUSIC_RENDER_LINEAR_DIR}/include")
  target_compile_definitions(music_render_core PRIVATE MUSIC_RENDER_HAS_STRETCH=1)
else()
  message(STATUS "Signalsmith Stretch not found: audio clips will not stretch or change pitch (run ./build.sh)")
endif()
target_compile_definitions(music_render_core PUBLIC
  JUCE_USE_CURL=0
  JUCE_USE_MP3AUDIOFORMAT=1
  JUCE_WEB_BROWSER=0
  JUCE_STANDALONE_APPLICATION=1
  JUCE_PLUGINHOST_VST3=1
  JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}>
  JUCE_PLUGINHOST_LADSPA=0
  JUCE_PLUGINHOST_LV2=0
  MUSIC_RENDER_VERSION="${PROJECT_VERSION}"
  MUSIC_RENDER_DEFAULT_REGISTRY="${CMAKE_CURRENT_SOURCE_DIR}/instruments/registry.json"
  MUSIC_RENDER_DEFAULT_TEMPLATES="${PROJECT_SOURCE_DIR}/templates")
target_link_libraries(music_render_core
  PRIVATE
    music_render_flac
    music_render_vorbis
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags)
if(TARGET music_render_lame)
  target_link_libraries(music_render_core PRIVATE music_render_lame)
  target_compile_definitions(music_render_core PUBLIC MUSIC_RENDER_HAS_LAME=1)
endif()
if(MUSIC_RENDER_WITH_SFIZZ)
  target_link_libraries(music_render_core PRIVATE sfizz::static)
  target_compile_definitions(music_render_core PUBLIC MUSIC_RENDER_HAS_SFIZZ=1)
endif()
# AU 호스팅은 CoreAudioKit 의 AUGenericView 를 쓴다 (juce_add_* 대상이 아니라 직접 링크한다). FFT 는 Accelerate 의 vDSP
if(APPLE)
  target_link_libraries(music_render_core PUBLIC "-framework CoreAudioKit" "-framework Accelerate" "-framework AudioToolbox")
endif()
# 모듈의 정의와 포함 경로를 소비자에게도 넘긴다 (모듈 소스는 넘기지 않는다)
target_compile_definitions(music_render_core INTERFACE
  $<TARGET_GENEX_EVAL:music_render_core,$<TARGET_PROPERTY:music_render_core,COMPILE_DEFINITIONS>>)
target_include_directories(music_render_core INTERFACE
  $<TARGET_GENEX_EVAL:music_render_core,$<TARGET_PROPERTY:music_render_core,INCLUDE_DIRECTORIES>>)

add_executable(music-render src/cli/main.cpp)
target_link_libraries(music-render PRIVATE music_render_core)

# ---------------------------------------------------------------------------
# 테스트용 플러그인 (VST3, AU). 결정적인 사인 신스로 플러그인 호스팅을 시험한다. 시스템 폴더에 설치하지 않는다.
# ---------------------------------------------------------------------------
option(MUSIC_RENDER_TEST_PLUGIN "Build the test synth plugin used by the tests" ON)
if(MUSIC_RENDER_TEST_PLUGIN)
  juce_add_plugin(MusicRenderTestSynth
    PRODUCT_NAME "MusicRender Test Synth"
    COMPANY_NAME "InitialDAW"
    BUNDLE_ID "com.biud436.initialdaw.testsynth"
    PLUGIN_MANUFACTURER_CODE Mrnd
    PLUGIN_CODE Mrts
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3)
  target_sources(MusicRenderTestSynth PRIVATE testplugins/TestSynth.cpp)
  target_compile_definitions(MusicRenderTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  target_link_libraries(MusicRenderTestSynth PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)

  juce_add_plugin(MusicRenderTestEffect
    PRODUCT_NAME "MusicRender Test Effect"
    COMPANY_NAME "InitialDAW"
    BUNDLE_ID "com.biud436.initialdaw.testeffect"
    PLUGIN_MANUFACTURER_CODE Mrnd
    PLUGIN_CODE Mrte
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3)
  target_sources(MusicRenderTestEffect PRIVATE testplugins/TestEffect.cpp)
  target_compile_definitions(MusicRenderTestEffect PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
  target_link_libraries(MusicRenderTestEffect PRIVATE juce::juce_audio_utils PUBLIC juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# 테스트
# ---------------------------------------------------------------------------
add_executable(music_render_tests
  tests/TestMain.cpp
  tests/test_composition.cpp
  tests/test_timing.cpp
  tests/test_meters.cpp
  tests/test_performance.cpp
  tests/test_registry.cpp
  tests/test_rendering.cpp
  tests/test_plugins.cpp
  tests/test_plugin_effects.cpp
  tests/test_mixer.cpp
  tests/test_mixer_automation.cpp
  tests/test_expression.cpp
  tests/test_effects.cpp
  tests/test_realtime.cpp
  tests/test_electronic.cpp
  tests/test_arrange.cpp
  tests/test_audio_clips.cpp
  tests/test_audio_take.cpp
  tests/test_document.cpp
  tests/test_note_tools.cpp
  tests/test_bounce.cpp
  tests/test_notation.cpp
  tests/test_loudness.cpp
  tests/test_measure.cpp
  tests/test_quick_sampler.cpp
  tests/test_transcribe.cpp
  tests/test_onsets.cpp
  tests/test_groove.cpp
  tests/test_presets.cpp
  tests/test_synth.cpp
  tests/test_recording.cpp
  tests/test_buildup.cpp
  tests/test_club_drums.cpp
  tests/test_parity.cpp
  tests/test_video.cpp)
target_link_libraries(music_render_tests PRIVATE music_render_core)
target_compile_definitions(music_render_tests PRIVATE
  MUSIC_RENDER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  MUSIC_RENDER_TEST_OUTPUT="${CMAKE_CURRENT_BINARY_DIR}/test-output")
if(MUSIC_RENDER_TEST_PLUGIN)
  add_dependencies(music_render_tests MusicRenderTestSynth_VST3 MusicRenderTestEffect_VST3)
  add_dependencies(music-render MusicRenderTestSynth_VST3 MusicRenderTestEffect_VST3)
  target_compile_definitions(music_render_tests PRIVATE
    MUSIC_RENDER_TEST_PLUGIN_VST3="$<TARGET_BUNDLE_DIR:MusicRenderTestSynth_VST3>"
    MUSIC_RENDER_TEST_EFFECT_VST3="$<TARGET_BUNDLE_DIR:MusicRenderTestEffect_VST3>")
endif()

enable_testing()
add_test(NAME unit COMMAND music_render_tests)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  set(MUSIC_RENDER_TEST_PLUGIN_ARG "")
  if(MUSIC_RENDER_TEST_PLUGIN)
    set(MUSIC_RENDER_TEST_PLUGIN_ARG "$<TARGET_BUNDLE_DIR:MusicRenderTestSynth_VST3>")
  endif()
  add_test(NAME cli COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e_cli.py"
           "$<TARGET_FILE:music-render>" "${CMAKE_CURRENT_BINARY_DIR}/e2e-output" "${MUSIC_RENDER_TEST_PLUGIN_ARG}")
endif()
