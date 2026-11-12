# DAW 앱. 엔진의 핵심 라이브러리(music_render_core)를 링크한다. JUCE 모듈은 그 라이브러리에 한 번만 컴파일되어 있다.

juce_add_gui_app(InitialDAW
  PRODUCT_NAME "InitialDAW"
  COMPANY_NAME "InitialDAW"
  BUNDLE_ID "com.biud436.initialdaw"
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "오디오 트랙에 녹음할 때 오디오 입력(마이크, 오디오 인터페이스)을 씁니다."
  VERSION "${PROJECT_VERSION}")

target_sources(InitialDAW PRIVATE
  src/Main.cpp
  src/Theme.cpp
  src/Project.cpp
  src/AudioEngine.cpp
  src/MidiInputs.cpp
  src/MidiLearn.cpp
  src/Grooves.cpp
  src/TransportBar.cpp
  src/ArrangementPanel.cpp
  src/HarmonyMenu.cpp
  src/PianoRollPanel.cpp
  src/PianoRollAudio.cpp
  src/ScorePanel.cpp
  src/TypingKeyboard.cpp
  src/MixerText.cpp
  src/AutomationTargets.cpp
  src/MixerPanel.cpp
  src/AnalyzerPanel.cpp
  src/EffectEditor.cpp
  src/SynthPanel.cpp
  src/InstrumentWindow.cpp
  src/Bouncer.cpp
  src/Exporter.cpp
  src/ExportDialog.cpp
  src/NewSongDialog.cpp
  src/VideoView.cpp
  src/MainComponent.cpp
  src/MainComponentVideo.cpp)

if(APPLE)
  target_sources(InitialDAW PRIVATE src/NativeKeys.mm)   # 자판 연주의 자판을 JUCE 보다 먼저 받는다
endif()

# 악보 보기의 SMuFL 글꼴 (./build.sh 가 external/bravura 에 받는다). 없으면 악보 보기가 글꼴이 없다고 알린다
set(INITIALDAW_BRAVURA "${CMAKE_SOURCE_DIR}/external/bravura/Bravura.otf")
if(EXISTS "${INITIALDAW_BRAVURA}")
  juce_add_binary_data(InitialDAWFonts NAMESPACE InitialDAWFonts SOURCES "${INITIALDAW_BRAVURA}")
  target_link_libraries(InitialDAW PRIVATE InitialDAWFonts)
  target_compile_definitions(InitialDAW PRIVATE INITIALDAW_HAS_BRAVURA=1)
endif()

target_include_directories(InitialDAW PRIVATE src)
target_compile_definitions(InitialDAW PRIVATE
  INITIALDAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(InitialDAW PRIVATE music_render_core)

# 화면 시험: 예제 프로젝트를 열어 창 없이 PNG 로 그린다 (앱이 뜨고, 프로젝트를 읽고, 그리는지)
add_test(NAME app_snapshot
         COMMAND InitialDAW --snapshot "${CMAKE_BINARY_DIR}/app-snapshot.png" --size 1280x800 --track 1
                 "${CMAKE_SOURCE_DIR}/engine/examples/orchestral_demo.json")

# 믹서 화면 시험: 믹서와 효과 매개변수 칸을 그린다 (재생기를 장치 없이 돌려 미터를 채운다)
add_test(NAME app_mixer_snapshot
         COMMAND InitialDAW --snapshot "${CMAKE_BINARY_DIR}/app-mixer-snapshot.png" --size 1280x800 --track 0 --at 4 --effect master:0
                 "${CMAKE_SOURCE_DIR}/engine/examples/orchestral_demo.json")

# 악보 보기 화면 시험: 고른 트랙의 악보를 그린다 (악보 모델, 줄 나누기, Bravura 글꼴)
add_test(NAME app_score_snapshot
         COMMAND InitialDAW --snapshot "${CMAKE_BINARY_DIR}/app-score-snapshot.png" --size 1280x800 --track 0 --score
                 "${CMAKE_SOURCE_DIR}/engine/examples/orchestral_demo.json")

# 신스 패널 화면 시험: 내장 신스 트랙의 패널을 창 없이 그린다
add_test(NAME app_synth_snapshot
         COMMAND InitialDAW --snapshot "${CMAKE_BINARY_DIR}/app-synth-snapshot.png" --instrument 1
                 "${CMAKE_SOURCE_DIR}/engine/examples/dexed_demo.json")

# 악기 패널 시험: 시험용 신스(VST3)를 곡 안의 정의로 쓰고, 패널의 슬라이더로 바꾼 소리가 곡에 쓰이고 되돌아가는지 본다
if(TARGET MusicRenderTestSynth_VST3)
  add_dependencies(InitialDAW MusicRenderTestSynth_VST3)
  add_test(NAME app_plugin
           COMMAND InitialDAW --plugin-test "$<TARGET_BUNDLE_DIR:MusicRenderTestSynth_VST3>")
endif()

# 플러그인 효과 시험: 시험용 효과(VST3)를 트랙과 버스의 효과 칸에 두고, 창의 매개변수로 바꾼 소리가 효과의 state 로 쓰이고 되돌아가는지 본다
if(TARGET MusicRenderTestEffect_VST3)
  add_dependencies(InitialDAW MusicRenderTestEffect_VST3)
  add_test(NAME app_plugin_effect
           COMMAND InitialDAW --plugin-effect-test "$<TARGET_BUNDLE_DIR:MusicRenderTestEffect_VST3>")
endif()

# 내보내기 시험: 대화상자의 내보내기가 엔진의 렌더와 바이트까지 같은지, 반복, 마디 구간, 멈추기, 플러그인 악기 (창 없이)
if(TARGET MusicRenderTestSynth_VST3)
  add_test(NAME app_export
           COMMAND InitialDAW --export-test "$<TARGET_BUNDLE_DIR:MusicRenderTestSynth_VST3>" --png "${CMAKE_BINARY_DIR}/app-export-snapshot.png")
else()
  add_test(NAME app_export COMMAND InitialDAW --export-test --png "${CMAKE_BINARY_DIR}/app-export-snapshot.png")
endif()

# 새 곡 시험: 템플릿 목록, 대화상자의 만들기로 곡을 만들어 열기, 있는 파일을 덮어쓰지 않기 (창 없이)
add_test(NAME app_new COMMAND InitialDAW --new-test --png "${CMAKE_BINARY_DIR}/app-new-snapshot.png")

# 편집 시험: 예제 프로젝트의 사본을 편집하고, 되돌리고, 다시 실행하고, 저장해 다시 읽는다
add_test(NAME app_edit
         COMMAND InitialDAW --edit-test "${CMAKE_SOURCE_DIR}/engine/examples/orchestral_demo.json")
