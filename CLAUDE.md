# InitialDAW

음악 엔진(`engine/`, music-render)과 그 위의 DAW 앱(`app/`). C++20, JUCE 8, sfizz.

- 오디오 생성 모델을 넣지 않는다. 소리는 라이선스가 분명한 샘플과 엔진의 내장 신스에서만 나오고, 음표는 사람이 보고 고칠 수 있는
  Composition JSON 으로 남는다.
- 개인의 작업 지침(곡 작업, 일하는 방식)은 커밋하지 않는 `CLAUDE.local.md` 에 둔다.

## 로드맵과 진행 상황 (필독)

- **작업을 시작하기 전에 반드시 `docs/plans/index.md`를 읽는다.** 단계 표와 진행 상황 표가 있다.
- 작업이 끝나면 단계 문서의 체크리스트와 `docs/plans/index.md`의 진행 상황 표를 갱신한다.
- **구현은 자율 검증 루프로 진행한다** (index.md 4절): 스스로 빌드하고(`./build.sh`) 시험을 돌려 통과할 때까지 반복한다.
  시험을 약하게 고쳐 통과시키는 것은 금지이며, 같은 실패가 세 번 반복되면 멈추고 알린다.
- 화면은 `--snapshot` 으로 창 없이 PNG 를 그려 확인하고, 재생 경로는 `--self-test`(-60 dB) 와 `music-render realtime` 으로 시험한다.
  앱 창을 띄웠다 닫는 시험은 하지 않는다.

## 폴더

| 폴더                                     | 내용                                                                                                                                                                                                                                                                                                                                        |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `engine/src/`                            | composition(모델, 파서, 편집 문서 `Document`, 편집 함수 `NoteEdits`, 트랙 목록과 마커와 시간 구간 편집 `Arrange`, 화음과 음계 표 `Harmony`, 곡 템플릿 `Templates`), performance(휴머나이즈, 스윙), instruments(레지스트리, SFZ, 내장 신스, 오디오 클립 재생 `AudioClips`), rendering(트랙 사슬, 오프라인 렌더러, 오디오로 굳히기 `Bounce`), notation(조와 철자 `Key`, 악보 모델 `Notation`, `MusicXml`), realtime(실시간 재생기: 반복 재생, 메트로놈, 카운트인, 일꾼 풀, 오디오 입력 녹음 `AudioTakeRecorder`), timing(템포 지도, 박자 지도 `MeterMap`), mixer, analysis(오프라인 측정, 실시간 `LoudnessMeter`, 렌더링 중의 스템 측정 `MixMeasure`, 녹음의 음 덩어리와 음마다의 높이 편집 곡선 `NoteSegments`), dsp(vDSP 의 FFT `Fft`, 알고리즘 리버브 `Freeverb`), io(오디오 파일 형식 `Codecs`: WAV, AIFF, libFLAC, libvorbis, AudioToolbox 의 MP3, MusicXML 의 `XmlWriter`), effects(효과: EQ, 압축기, 멀티밴드 압축기, 트랜지언트, 디에서, 리버브, 딜레이, 모듈레이션, 게이트 등, 매개변수 곡선과 실시간 곡 위치 기록 `Automation`), midi(표준 MIDI 파일 `Smf`, MIDI 내보내기와 가져오기 `MidiImport`, 녹음기 `MidiRecorder`), media(영상 `Video`: AVFoundation 의 Objective-C++, 정보, 프레임, 차례로 풀기, 소리, 장면 바뀜, 영상에 입히기, 시험 영상, 타임코드 `Timecode`), cli                                                                                            |
| `engine/tests/`                          | 단위 시험(`test_*.cpp`, 작은 TEST 틀), 명령행 끝까지 시험(`e2e_cli.py`)                                                                                                                                                                                                                                                                     |
| `engine/instruments/registry.json`       | 악기 정의 (SFZ 파일, 아티큘레이션, 다이내믹스, 대체 악기)                                                                                                                                                                                                                                                                                   |
| `engine/schemas/composition.schema.json` | Composition JSON 스키마                                                                                                                                                                                                                                                                                                                     |
| `engine/examples/`                       | 작은 예제 곡 (시험이 쓴다)                                                                                                                                                                                                                                                                                                                  |
| `app/src/`                               | 앱: `Project`(문서, 실행 취소, 저장), `PianoRollPanel`(편집, 음표의 가사 보이기, 드럼 보기, 음표 다듬기, 다른 트랙 함께 보기와 고치기, 조의 줄, 오디오 트랙의 음 덩어리 편집은 `PianoRollAudio`), `ScorePanel`(악보 보기, Bravura 글꼴), `AnalyzerPanel`(믹서의 마스터 분석: 스펙트럼과 라우드니스), `MixerPanel`(채널 스트립), `EffectEditor`(효과 매개변수), `MixerText`(이름, 값 표기, 페이더 눈금), `InstrumentWindow`(플러그인 악기 패널, 상태 저장), `SynthPanel`(내장 신스 패널), `Exporter`(내보내기 작업, 뒤 스레드), `Bouncer`(트랙을 오디오로 굳히기, 뒤 스레드), `ExportDialog`(내보내기 대화상자), `NewSongDialog`(템플릿에서 새 곡), `HarmonyMenu`(코드와 스케일 넣기 메뉴, 한국어 이름), `MidiInputs`(MIDI 입력 장치, 받은 순간의 재생 샘플), `ArrangementPanel`(트랙 머리 메뉴, 이름 칸, 끌어 옮기기, 마커 띠, 레인의 구간 고르기), `TransportBar`(녹음, 반복, 메트로놈 단추), `AudioEngine`, `MainComponent`(명령, 메뉴, 파일 감시, 녹음 흐름, 트랙과 마커와 구간 편집, MIDI 가져오기, 자동 저장, 영상 명령은 `MainComponentVideo`), `VideoView`(영상 레인의 섬네일 캐시, 영상 창) |
| `templates/`                             | 새 곡의 장르 템플릿 13개 (음표 없는 곡 JSON). `tools/make-templates.py` 가 곡에서 만든다, 손으로 고치지 않는다 |
| `docs/manual.md`, `docs/manual.ko.md`    | 사용 설명서 (앱, 명령줄 엔진, Music IR 의 모든 기능, 단축키, 명령) |
| `docs/plans/`                            | 계획과 진행 (엔진, D1~D32 단계, Music IR, 다음 단계 D33~D48 은 `next-stages.md`)                                                                                                                                                                                                                                                                   |
| `tools/`                                 | `music-ir.py` 와 `musicir/` (Music IR: 생성기 도우미, 조회, 고칠 범위 검사, 시험), `placement-spec.py` (곡 JSON 과 IR 에서 손으로 찍는 배치 명세: 마디.박.틱, 무늬, 북 그리드, 곡선, 자체 검증), `mix-report.py` (렌더링의 `--measure` 를 표로), `collect-licenses.sh` (릴리스용 라이선스 문서 모으기), `calibrate-keys.py` (건반마다 크기를 재는 보정표), `make-loop.py` (렌더링에서 게임용 반복 파일을 자른다), `make-templates.py` (곡에서 새 곡 템플릿을 만든다), `make-cello-sfz.py` (Karoryfer 첼로 샘플 곁에 우리 SFZ 를 쓴다, build.sh 가 부른다)                                                                                          |
| `external/`                              | JUCE, sfizz, LAME, Signalsmith Stretch 와 Linear, 악보 글꼴 Bravura, libFLAC, libogg, libvorbis, 샘플 라이브러리, 국악기 녹음 (`external/gugak`, SFZ 는 `tools/make-gugak-sfz.py`), 플러그인 (`build.sh` 가 받는다, 커밋 금지)                                                                                                                                                                                                                                                                    |
| `build/`                                 | 빌드 결과, 렌더링한 곡 `build/songs/` (커밋 금지)                                                                                                                                                                                                                                                                                           |

## 빌드와 시험

```bash
./build.sh                    # JUCE 8.0.10, sfizz(고정 커밋), LAME 4.0(SHA-256 고정, MP3 인코더), Bravura 1.482(SHA-256 고정, 악보 글꼴), libFLAC 1.4.3, libogg 1.3.5, libvorbis 1.3.7(SHA-256 고정)을 받고 빌드, CTest (music_ir, unit, cli, app_snapshot, app_score_snapshot, app_mixer_snapshot, app_synth_snapshot, app_plugin, app_plugin_effect, app_export, app_new, app_edit)
./build.sh --samples          # 샘플 라이브러리(VSCO 2 CE, Sonatina Symphonic Orchestra, FreePats, Karoryfer 첼로)도 받는다
./build.sh --gugak            # 국악기 녹음도 받는다 (국립국악원 사이트의 사용 목적 양식 값은 받는 사람이 정한다: GUGAK_USE_TYPE, GUGAK_USE_PURPOSE, GUGAK_AFFILIATION, 없으면 묻는다)
./build.sh --plugins          # 무료 VSTi Dexed 1.0.1 (SHA-256 고정)을 external/plugins/dexed 에 받는다
APP=build/app/InitialDAW_artefacts/Release/InitialDAW.app/Contents/MacOS/InitialDAW
$APP --snapshot out.png --size 1440x900 --track 3 --at 40 --bars 9-16 --lane cc1 song.json   # 창 없이 화면을 PNG 로
$APP --snapshot out.png --size 1440x900 --track 13 --at 75 --effect 13:0 song.json   # 믹서와 효과 매개변수 칸 (--mixer, --effect master:0)
$APP --self-test --at 60 song.json      # 오디오 장치를 열고 멈춘 채 음 누르기와 자판 연주가 들리는지, -60 dB 로 3초 재생, 두 박 구간 재생, MIDI 입력 길의 녹음
$APP --edit-test song.json              # 편집 경로와 피아노 롤, 믹서의 마우스 동작, 녹음기 결과 넣기, 눈금자의 반복 구간 끌기를 창 없이 시험
$APP --plugin-test <플러그인.vst3>      # 악기 패널을 창 없이: 바꾼 소리가 곡에 쓰이고 실행 취소로 돌아오는지
$APP --plugin-effect-test <효과.vst3>   # 효과 칸의 플러그인 창을 창 없이: 바꾼 소리가 효과의 state 로, 같은 플러그인에 반영
$APP --snapshot out.png --instrument 8 song.json   # 8번 트랙의 내장 신스 패널을 PNG 로
$APP --snapshot out.png --track 0 --score song.json   # 0번 트랙의 악보 보기
$APP --snapshot out.png --export song.json         # 내보내기 대화상자를 PNG 로
$APP --snapshot out.png --new --track 4            # 새 곡 대화상자 (다섯째 템플릿을 고른 채)
$APP --snapshot out.png --track 0 --companions 1,+3 song.json   # 피아노 롤에 1번 트랙을 흐리게, 3번 트랙을 함께 고치기로
$APP --snapshot out.png --video-window --at 5 song.json   # 영상 창 (곡 5초에 보이는 프레임), --export-video 는 영상 형식의 내보내기 대화상자
$APP --new-test [--png out.png]                    # 새 곡 대화상자의 만들기로 곡을 만들어 열기, 덮어쓰지 않기
build/engine/music-render new orchestral-battle --output songs/<곡>/<곡>.json --title "제목"   # 템플릿에서 새 곡 (music-render templates 가 목록)
$APP --export-test <플러그인.vst3> [--png 끝난화면.png]   # 대화상자의 내보내기를 창 없이: 엔진 렌더와 바이트까지 같은지, 반복, 마디 구간, 멈추기, 플러그인 곡
build/engine/music-render export-musicxml song.json --output song.musicxml    # 악보 (MuseScore 등이 읽는다)
build/engine/music-render sampler bell.wav --output samples/bell.sfz [--root C4] [--one-shot]   # 소리 하나로 SFZ 악기 (높이를 재서 맞춘다)
build/engine/music-render transcribe hum.wav --output melody.json --bpm 96   # 부르거나 연주한 단선율을 음표로 (앱은 클립 메뉴)
build/engine/music-render tempo loop.wav; build/engine/music-render slice loop.wav --output kit.sfz   # 루프의 템포, 어택마다 조각 (앱은 클립 메뉴)
build/engine/music-render arrange song.json quantize-audio Drums --grid 1/16 [--mode slice]   # 오디오 트랙의 어택을 격자로 (워프 마커, unwarp 은 지우기)
build/engine/music-render arrange song.json tune Vocal --key "A minor" [--speed 30]   # 오디오 트랙 클립의 음높이 교정 (untune 은 지우기)
build/engine/music-render notes take.wav; build/engine/music-render arrange song.json pitch-edit Vocal --note 3 --to E4 [--flatten 100] [--vibrato 0]   # 녹음의 음 목록, 음 하나의 높이 (앱은 피아노 롤)
$APP --snapshot out.png --track 0 --audio-note 0:2 song.json   # 오디오 트랙의 음 덩어리 (0번 클립의 셋째 음을 고른 채)
build/engine/music-render arrange song.json reverse Vocal --clip 2; build/engine/music-render arrange song.json crossfade Vocal --ms 20   # 거꾸로 재생, 이웃 클립의 크로스페이드 (fade 는 페이드 길이와 모양)
build/engine/music-render groove list; build/engine/music-render arrange song.json groove Hat --groove swing-16-58   # 그루브 (extract, extract-audio 로 뽑기)
build/engine/music-render beats take.wav; build/engine/music-render arrange song.json follow-tempo Drums   # 메트로놈 없는 연주의 박, 곡 템포를 그 연주에
build/engine/music-render arrange song.json add-track --template vocal   # 트랙 템플릿 (track-template list, track-template save)
build/engine/music-render render song.json --output song.ogg --loop    # 형식은 확장자로 (.wav .flac .ogg .mp3), --loop, --bars 17-32 --fade 2, --quality
build/engine/music-render import-midi sketch.mid --output songs/<곡>/<곡>.json   # MIDI 파일을 새 곡으로 (트랙, 템포, 박자, 마커, 가사, 곡선)
build/engine/music-render arrange song.json insert-bars --at 17 --count 4     # delete-bars, duplicate, copy, clear, add-track, rename-track, move-track, add-marker ...
build/engine/music-render arrange song.json move-section 후렴 --before 벌스     # duplicate-section, delete-section (구간은 마커에서 다음 마커까지)
build/engine/music-render realtime song.json --from 60 --seconds 15     # 실시간 부하
build/engine/music-render render song.json --output scored.mp4 [--video promo.mp4] [--video-offset 2] [--video-sound mix]   # 음악을 영상에 입힌다 (화면은 그대로)
build/engine/music-render video-info promo.mp4; build/engine/music-render video-scenes promo.mp4 --song song.json   # 영상 정보, 장면 바뀜 (video-frame, video-audio, video-pattern)
```

- 창을 띄울 때는 절대 경로로: `open -a "$PWD/build/app/InitialDAW_artefacts/Release/InitialDAW.app" <곡.json>`.
  실행 중인 앱에 파일을 넘기면 그 창에서 연다 (저장하지 않은 편집이 있으면 묻는다).

## 설계 원칙

- 곡의 형식은 Composition JSON 하나다. 앱의 편집도, 작곡 생성기도, 에이전트도 같은 파일을 쓴다. 모르는 필드는 지운다고 가정하지 말고 보존한다.
- 앱의 편집은 엔진의 `CompositionDocument` 가 들고 있는 JSON 트리를 고치고 다시 파싱한다. 저장은 원래의 모르는 필드, 주석, 키 순서를 지키고
  음표 하나를 한 줄에 쓴다 (`writeJson`). 편집은 재생기의 악기 프로필로 검사한 뒤 실행 취소 기록에 넣는다.
- 믹서(페이더, 밸런스, 뮤트, 솔로, 센드, 효과)도 곡 JSON 이다. 모든 편집(실행 취소 포함)은 `RealtimePlayer::apply` 로 재생에 반영하고,
  바꿔 끼울 수 없는 변경(템포, 트랙과 버스 목록, 트랙 효과의 지연, 사이드체인)이면 재생기를 다시 만든다. 효과를 바꿔 끼우면 앞 효과의 상태를 잇는다.
- 효과 매개변수와 센드 크기의 곡선은 효과 객체의 `"automation": {"<매개변수>": [점]}` 과 센드의 `automation.levelDb`, 페이더와 밸런스의 곡선은 믹서 객체의
  `automation.gainDb`, `automation.pan` (트랙, 버스, 마스터)이다. 곡선 하나를 가리키는 열쇠(`cc1`, `mixer/gainDb`, `sends/<버스>`, `effects/<번호>:<종류>/<매개변수>`)는
  엔진의 `trackAutomationLanes`, `withTrackAutomation` 과 앱의 `AutomationTargets` 가 함께 쓴다. 점은 컨트롤러 곡선과 같고 효과는
  매개변수 표의 `automatable` 만이다. 곡선은 곡 위치의 32 샘플 격자에서 읽는 상태 없는 함수(`SampleLane`)이고 `ducker` 도 곡 위치만의 함수다. 효과는 `setPosition` 으로
  자기가 내보내는 소리의 곡 위치(앞 효과와 자기의 지연을 뺀 자리)를 받는다. 효과의 곡선은 곡 길이를 늘이지 않는다.
- 실시간 재생기는 시계(처리한 샘플 수)와 곡 위치의 기록(`SongTimeline`)을 남겨, 지연된 신호(트랙 정렬, 효과의 지연, 버스, 마스터, 센드)가 시계를 지연만큼
  거슬러 제 곡 위치를 읽는다. 멈춘 동안과 카운트인 동안은 곡선, 덕킹, 표현 단계를 그 자리의 값에 붙든다 (`BlockPosition.held`). 위치가 끊긴 블록에서는
  위치를 따르는 효과(`followsPosition`)만 나눠 처리한다. 새 효과가 위치를 쓰면 `followsPosition` 을 켜고 `forEachGridStep`, `gridFraction` 을 따른다.
- 사이드체인 키는 압축기 자리의 소리에 맞춘다 (`EffectChain::alignSidechains`, `alignTrackSidechains`). 트랙 사이는 키 트랙의 정렬 전 신호를 늦추고(키가 더 늦으면
  압축기 앞에서 소리를 늦추고 트랙 지연이 는다), 버스와 마스터는 정렬된 트랙 신호를 앞 효과의 지연만큼 늦춘다.
- 플러그인 효과는 효과 객체 `{ "type": "plugin", "format", "plugin", "name", "state", "program", "parameters" }` 다 (`plugins/PluginEffect`). 메시지 스레드에서 만들고,
  곡의 박과 템포를 플레이헤드로 알린다. 설정만 바뀐 편집은 `apply` 가 같은 플러그인에 넣고(창이 계속 본다), 그 밖의 바뀜은 사슬을 바꿔 새 플러그인이 된다.
  재생기는 효과 칸의 플러그인을 메시지 스레드 쪽 표(`effectPlugin`)로 내놓는다 (오디오 스레드의 사슬을 읽지 않는다).
- 곡과 레지스트리가 가리키는 VST3 는 `PluginHost::load` 가 설치된 플러그인 폴더, `external/plugins`, `MUSIC_RENDER_PLUGIN_PATH`, 플러그인 캐시 안의 것만
  불러온다 (`pluginLocationAllowed`, 링크와 .. 을 푼 실제 경로로 견준다). 명령줄 인자와 시험이 직접 준 플러그인은 `allowLocation` 으로 허용한다.
  곡 폴더에 딸려 온 플러그인이 곡을 열 때 실행되면 안 된다.
- 내보내기는 엔진의 `OfflineRender` 다: 만들기(악기와 트랙 사슬, 버스와 마스터 효과 사슬, 플러그인은 메시지 스레드에서)와 돌리기(뒤 스레드)를 나눈다. 형식 쓰기는 엔진의 `writeAudioFile`
  (WAV smpl, Ogg 와 FLAC 의 Vorbis 주석, MP3 는 LAME). 앱의 내보내기 결과는 `renderComposition` 과 바이트까지 같아야 한다 (`--export-test`).
- 녹음도 엔진이 맡는다: `RealtimePlayer` 의 반복 재생(끝 샘플에서 되돌아가며 울리는 음을 끄고 컨트롤러를 다시 보낸다), 메트로놈(리미터와 모니터 게인 뒤, 내보내기에는 없다),
  카운트인, `auditionController`, 그리고 `MidiRecorder`(박 단위의 음과 곡선 점, 격자 맞추기, 펀치, 반복 끝). 앱은 MIDI 장치(`MidiInputs`)와 받은 순간의 재생 샘플
  어림(`AudioEngine::estimatedPosition`)만 맡고, 녹음 한 번은 편집 하나(`humanize: false`)다. 반복 구간은 곡 JSON 의 `metadata.loop` 하나이고 눈금자에서 고친다 (⌥ 끌기).
- 실시간 재생기는 버스 효과의 지연도 맞춘다: 트랙 직접 신호는 가장 긴 버스 지연만큼, 버스는 그 차이만큼 늦춰 마스터에서 만나고, 마스터는 그만큼 늦은 곡 위치를 읽는다
  (출력 지연에 들어간다). 버스 효과의 지연이 바뀌면 재생기를 다시 만든다.
- 재생과 내보내기는 같은 트랙 사슬(`buildTrackChain`)을 지난다. 실시간 결과가 오프라인 렌더와 같다는 시험(`test_realtime.cpp`)을 깨지 않는다.
  편집한 트랙은 `RealtimePlayer::updateTrack` 으로 악기를 다시 읽지 않고 바꿔 끼운다 (바꿔 낀 결과도 새로 만든 재생기와 샘플 단위로 같아야 한다).
- 엔진에 없는 것을 앱에서 흉내 내지 않는다. 엔진을 먼저 넓히고 앱을 그 위에 올린다.
- 오디오 스레드에서 할당, 잠금 대기, 파일 입출력을 하지 않는다. 재생기와 트랙 내용은 메시지 스레드에서 만들어 바꿔 끼운다.
- 내장 신스의 소리는 트랙의 `"synth": {...}`(프리셋과 다른 값만)로, 플러그인 악기의 소리는 트랙의 `plugin.state`(표준 base64)로 곡에 남는다. 악기 패널의 편집기는 플러그인(재생기)보다 먼저 지운다.
  앱은 `mr::HostSession` 을 가장 늦게 닫는다. 스냅숏과 시험(`--self-test` 포함)은 앱 설정(최근 파일, 장치)을 쓰지 않는다.
- 곡의 뼈대 편집(트랙 더하기, 지우기, 복제, 이름, 순서, 색, 마커, 여러 트랙의 구간 복사와 붙이기, 시간 넣기와 지우기)은 엔진의 `composition/Arrange` 가
  곡 JSON 트리를 받아 고친 사본을 돌려주는 순수 함수다. 앱은 바뀐 최상위 속성만 편집 하나로 넣고(`Project::editRoot`), 명령행 `arrange` 도 같은 함수를 쓴다.
  트랙 이름은 사이드체인 키라서 이름을 바꾸면 압축기의 `sidechain` 이 따라가고, 지우면 그 키를 푼다. 붙이기는 그 자리의 내용을 지우고 넣는다(덮어쓰기).
  곡선은 구간 양 끝의 값을 점으로 넣어 잘라 옮겨 구간 밖의 값이 바뀌지 않는다. 시간 넣기와 지우기는 템포 점, 마커, 반복 구간, `render.endBeat` 까지 민다.
  마커는 최상위 `markers` (`[{ "beat", "name" }]`, 소리에 쓰지 않는다), 트랙 색은 트랙의 `color` ("#rrggbb").
  구간(마커에서 다음 마커까지)의 옮기기, 복제, 지우기는 `withSpansArranged` 하나가 원래 곡의 박 구간들을 이어 붙인다 (구간마다 박자, 조, 템포를 다시 세우고,
  자르며 생긴 곡선 점 가운데 소리를 바꾸지 않는 것만 지운다). 시간 넣기와 지우기를 엮어 구간을 옮기지 않는다.
- 오디오 트랙은 악기가 `audio`(레지스트리의 내장 악기)인 트랙의 `clips` 다: 자리는 박, 길이와 파일 안의 시작은 초(템포로 늘지 않는다), 파일은 곡 폴더 기준
  (앱은 곡 폴더 밖의 파일을 `audio/` 에 복사한다). 클립 재생기(`instruments/AudioClips`)는 곡 위치만의 함수이고 멈추면 조용하다(`Instrument::setTransportPlaying`).
  파일은 메시지 스레드에서 읽어 렌더 샘플레이트로 바꿔 캐시하고, 클립 편집은 `RealtimePlayer::apply` 가 `setClips` 로 바꿔 끼운다. 편곡 함수는 클립을 경계에서 나눈다.
- 오디오 클립의 늘이기와 음높이(`sourceBpm`, `stretch`, `pitchSemitones`)는 클립을 준비할 때 Signalsmith Stretch 로 클립 전체를 한 번 만든다 (`AudioClips.cpp` 의
  `renderStretched`, 씨앗 고정, 캐시). 곡의 초와 파일의 초를 오가는 곳은 모두 `clipSourceAt`, `clipSongSecondsAt` 을 쓴다 (offsetSeconds, lengthSeconds 는 파일의 초).
- MIDI 배우기(`MidiLearn`)는 앱 설정 `midiLearn` 이고 채널을 순서로 가리킨다. 묶인 컨트롤러는 슬라이더의 `onDragStart`, 값, `onDragEnd`(0.4초 조용하면)를 불러
  손으로 끄는 길을 탄다. JSON 을 `juce::JSON::parse (...)` 로 읽어 배열을 쓸 때는 값을 먼저 변수에 담는다 (잠깐 있는 값의 배열을 가리키면 사라진다).
- 그루브(`composition/Groove`)는 칸마다의 어긋남과 세기 배수이고 곡의 0 박에서 주기가 시작한다. 입힌 결과는 음표(또는 워프 마커)로 곡에 적힌다 (재생 때 입히지 않는다).
  내 그루브는 앱 데이터 폴더의 `InitialDAW/Grooves/` (시험은 `PianoRollPanel::grooveFolder` 를 시험 폴더로 바꾼다).
- 트랙 템플릿은 트랙 JSON 사본에서 음표, 클립, 곡선을 뺀 것이다 (`trackTemplateOf`, `withTrackFromTemplate`). 내 템플릿은 앱 데이터 폴더의 `InitialDAW/TrackTemplates/`
  (시험은 `MainComponent::trackTemplateFolder` 를 시험 폴더로 바꾼다).
- 효과 프리셋: 기본 프리셋은 엔진의 표(`effects/Presets`, 이름은 앱의 `presetName`), 입히기는 `withPreset` (값을 곡에 쓴다). 사용자 프리셋과 효과 사슬은
  앱 데이터 폴더의 `InitialDAW/Presets/` (시험은 `MixerPanel::presetFolder` 를 시험 폴더로 바꾼다).
- VCA(`vcas`, 채널의 `vca`)는 곡을 읽을 때 `resolveVcas` 가 채널의 `MixerSettings` 의 `vcaDb`, `vcaMute`, `vcaLane` 에 푼다. 게인은 `totalGainDb()`,
  뮤트는 `muted()` 로 읽는다 (채널 자신의 `gainDb`, `mute` 는 편집과 저장의 값이다). 앱이 미리 듣기로 VCA 를 바꾸면 `resolveVcas` 를 다시 부른다.
- 곡선 쓰기(⌥A)는 믹서 스트립이 재생 중의 끄는 동안 (박, 값)을 모아 놓을 때 `MixerPanel::commitWrittenLane` 으로 편집 하나를 만든다 (터치 방식).
- 대역을 나누는 효과(`multiband`, `deesser`)는 `Crossover` (링크위츠-라일리 4차)를 쓴다. 세 대역은 낮은 대역을 둘째 나누기의 올패스에 지나게 해야 다시 더한 크기가 평평하다.
- 모듈레이션 효과(chorus, flanger, phaser, tremolo)의 LFO 위상은 곡 위치만의 함수다 (`rateHz` 나 템포 지도의 `rateBeats`, `PositionLfo`). 빠르기는 곡선을 받지 않는다.
- 음높이 재기는 `analysis/PitchTrack` 하나다 (YIN, FFT 상관). 멜로디 받아 적기와 퀵 샘플러가 함께 쓴다. 받아 적은 음은 `"humanize": false`.
- sfizz 는 샘플을 11.51 dB 작게 낸다 (전체 크기 -7.35 dB 와 팬 법칙, 모노와 스테레오 모두). 앱이 쓰는 SFZ(퀵 샘플러, 조각)는 `volume=11.51` 로 녹음 크기를 지킨다.
  sfizz 는 미리 읽은 데이터에서 나중에 읽은 데이터로 바꿔 탈 때 한 샘플 걷는다 (파형을 샘플 단위로 견주는 시험은 칸마다 -1~1 샘플을 허락한다).
- 퀵 샘플러(`instruments/QuickSampler`)는 소리 하나를 SFZ 리전 하나로 쓴다. 앱은 곡 폴더의 `samples/` 에 SFZ 를 두고 곡 안의 정의(`sampler.<이름>`)와 트랙을 편집 하나로 더한다.
  앱에서 뿌리 JSON 을 고칠 때는 `project_.documentRoot().clone()` 에 쓴다 (`documentRoot()` 는 지금 문서 그 자체라 실행 취소의 앞 상태가 바뀐다).
- 워프 마커(클립의 `warp`, `[{ "seconds", "beat" }]`)의 박은 차이만 쓴다: 곡의 박은 `startBeat + beat - warpBeatAt (offsetSeconds)`. 그래서 앞 자르기와 나누기는
  마커를 고치지 않는다. 마커를 고치는 곳은 엔진의 `withWarpMarkerAdded`, `withWarpMarkerMoved`, `withWarpMarkerRemoved`, `quantizedWarp` 를 쓴다 (클립 머리의 박이 바뀌면
  `startBeat` 를 옮긴다). `warpMode: slice` 는 마커의 10 ms 앞마다 잘라 늘이지 않고 튼다 (`renderSliced`, 타악기). 마커와 고정 `stretch` 는 함께 쓰지 않는다.
- 음높이 교정(클립의 `pitchCorrection`)은 클립 렌더(`renderStretched`)가 `pitchCorrectionCurve` 로 256 샘플마다 높이를 바꾼다. 교정은 Signalsmith 의 출력 지연만큼
  뒤의 파일 자리에서 읽고(같은 자리를 읽으면 비브라토가 커진다), 포먼트를 지킬 때는 잰 기본 주파수를 `setFormantBase` 로 준다 (스스로 재게 두면 높이가 돌아간다).
- 음마다의 높이 편집(클립의 `pitchEdits`: `[{ "seconds", "lengthSeconds", "semitones", "flatten", "vibrato" }]`, 파일의 초)은 클립 렌더가 교정 곡선 위에
  `pitchEditCurve` 로 더한다 (편집한 음 안에서는 편집이 이긴다, 반음 옮김은 경계 앞뒤 10 ms, 펴기와 비브라토는 음 안의 처음과 끝 20 ms 에서 잇는다). 흔들림은
  `splitPitch` 가 0.2초 이동 평균 두 번으로 느린 쪽과 빠른 쪽(비브라토)으로 나누고, 음 머리와 꼬리의 넘어오는 높이(가운데와 반음의 반 넘게 다른 프레임)는 뺀다.
  음 덩어리는 받아 적기의 나누기(`noteSegments`, 파일마다 한 번 `fileNoteSegments`)이고, 클립의 음 번호는 파일 전체의 음 가운데 클립 구간의 것(`segmentRange`)이라
  앱과 명령행이 같다. 앱의 재생기는 고친 클립을 뒤 스레드에서 준비한다 (`PlayerOptions::prepareClipsInBackground`, 준비하는 동안 앞 소리, 시험은 `clipsPending` 을 기다린다).
  Signalsmith 의 옮김은 녹음에 따라 0~5 센트 어긋난다.
- 클립의 `reverse` 는 파일 전체를 거꾸로 돌린 소리이고, 클립의 모든 파일의 초(시작, 길이, 워프, 높이 편집, 게인 곡선)는 거꾸로 돌린 파일의 초다 (파일 끝에서 센다).
  그래서 클립의 초를 세는 곳은 뒤집기를 몰라도 되고, 소리를 주는 곳만 안다: 재생은 `loadClipAudio (..., reversed)`, 봉우리는 `clipPeaks (file, reversed)`, 클립의 소리를
  재는 곳(어택, 템포, 받아 적기, 음 덩어리, 샘플러)은 `clipSourceFile` (뒤집은 클립은 임시 폴더의 거꾸로 돌린 사본)을 읽는다. 켜고 끄기는 `withClipReversed`.
  클립의 `gainCurve` 는 파일의 초의 데시벨 곡선 (재생기는 32 샘플마다), 페이드 모양은 `fadeInShape`, `fadeOutShape` (`fadeGain`). 크로스페이드는 `crossfadedClips` 가
  데이터만 고친다 (같은 소리가 이어지면 사인 제곱, 아니면 등전력, 겹치면 더하는 재생 규칙은 그대로).
- 클립의 `muted` 는 고르지 않은 테이크다 (재생과 렌더에서 빠진다). 반복 녹음은 한 파일에 반복마다의 테이크를 쓰고(`AudioTake::passes`) 반복마다 클립이 된다.
  테이크 고르기(`useTake`)는 겹친 클립을 경계에서 나누고 뮤트를 바꾸는 편집 하나다.
- 트랙의 `output` (버스 이름)은 페이더 뒤의 소리를 마스터 대신 그 버스로 보낸다 (서브그룹, 오프라인과 실시간, 바뀌면 재생기를 다시 만든다). `folder` 는 화면의 묶음뿐이다.
  버스 이름을 바꾸거나 지우는 곳은 센드와 출력을 함께 고친다.
- 트랙을 오디오로 굳히기는 엔진의 `rendering/Bounce` (`bounceComposition` 이 그 트랙과 사이드체인 키만 울리고 버스와 마스터 효과를 뺀다, 오프라인 렌더의
  `stemTracks` 와 `stemsPreFader`)와 앱의 `Bouncer` (`BounceJob`, `withBounce`)다. 굳힌 오디오 트랙을 넣은 곡은 원래 곡과 24비트 안에서 같아야 한다 (`test_bounce.cpp`).
- 음표 다듬기(격자의 세기와 스윙, 벨로시티, 휴머나이즈, 레가토, 음계, 화음 펼치기, 역행, 반행)는 엔진의 `NoteEdits` 함수이고 피아노 롤의 `toolsMenu` 가 부른다.
  재생 때 음을 만드는 MIDI 효과(아르페지에이터)는 두지 않는다: 펼친 음은 곡에 적혀 사람이 본다.
- 피아노 롤의 여러 트랙 편집은 트랙마다 `NoteEdits` 함수를 부르고 바뀐 트랙을 `Project::edit` 하나(`commitTracks`)로 넣는다. 고른 음은 보이는 트랙의 `selection_` 과
  함께 고치는 트랙마다의 `others_` 다. 고른 음을 고치는 새 명령은 `editSelection` 이나 `applyTool` 을 거쳐 트랙을 넘게 한다.
- 피아노 롤의 세로축은 줄 지도다 (`virtualOf`, `pitchOfVirtual`): 피아노 보기는 음높이, 드럼 보기는 이름 있는 건반과 쓴 건반의 줄. 세로 자리를 다루는 새 코드는
  `pitchY`, `pitchAt`, `movedPitch` 를 쓰고 음높이에 직접 더하지 않는다. 스텝 입력은 `MainComponent::stepNote` (자판과 MIDI 입력이 부른다).
- 조는 곡의 `key` 와 `keyChanges` (마디 줄)이고 소리에 쓰지 않는다 (악보의 조표와 철자, MIDI 조표). 없으면 `scoreKey` 가 추정한다. 악보 모델(`buildScore`)은
  MusicXML 과 앱의 악보 보기가 함께 쓴다. 모델을 고치면 모든 곡을 내보내 마디마다 보표의 길이 합이 마디 길이와 같은지 본다.
- 박자는 처음 박자 `timeSignature` 와 곡 중간의 `meterChanges` (`[{ "beat", "timeSignature" }]`, 앞 박자의 마디 줄)다. 마디를 세는 곳은 모두 `Composition::meters()`
  (`timing/MeterMap`)를 쓴다. `barBeats()` 는 처음 박자의 마디 길이일 뿐이니 새 코드에서 마디 번호를 셀 때 쓰지 않는다.
- 자동 저장은 앱 데이터 폴더(`~/Library/Application Support/InitialDAW/Autosave/`)에 쓴다. 곡 파일 곁에 쓰지 않는다 (파일 감시와 생성기). 시험(`persist=false`)은 쓰지 않는다.
- 영상은 곡의 최상위 `video` (`{ "file", "offsetSeconds" }`, 영상 0초가 놓인 곡의 초)이고 소리에 쓰지 않는다. 편곡 함수는 `video` 를 옮기지 않는다 (영상은 초로 흐른다).
  영상 읽기와 쓰기는 엔진의 `media/Video` (AVFoundation, JUCE 를 쓰지 않는다)만 한다. 프레임 빠르기는 분수 `FrameRate` 로 다루고 (29.97 은 30000/1001), 프레임 번호는
  `frameAtSeconds`, 타임코드는 `timecodeAt` 을 쓴다. 영상에 입히기는 렌더러의 `RenderOptions::onAudio` 로 마지막 소리를 받아 `writeScoredVideo` 가 영상 샘플을 그대로 옮긴다.
  압축 샘플의 시각은 미디어 시간축이라 견줄 때는 편집 목록을 지난 시각(`videoSampleDigests`)을 쓴다 (mp4 는 B 프레임의 지연을 편집으로 감춘다).
- 음표의 `lyric`(그 음에서 부르는 음절, `-` 는 앞 음절을 이어 부르는 음표)은 소리에 쓰지 않는다. 렌더링과 재생이 가사로 바뀌면 안 되고, `export-midi` 만
  음 켜짐 자리의 가사 메타 이벤트(FF 05, UTF-8)로 쓴다. 편집은 가사를 원래 객체에 남기고 복사와 붙여 넣기에 옮긴다.

## 라이선스 (2026-10-06 정리)

- 악보 글꼴 Bravura(SIL OFL 1.1)는 앱에 바이너리 데이터로 들어간다. 릴리스에는 `OFL.txt` 를 함께 둔다 (`tools/collect-licenses.sh`).
- 코드와 실행 파일은 AGPLv3 (`LICENSE`, AGPL-3.0-only). 목록은 `THIRD-PARTY.md`.
- 저장소에 제3자 코드를 넣지 않는다. 새 의존성은 `build.sh` 에서 태그나 커밋으로 고정해 받고, `THIRD-PARTY.md` 와 `tools/collect-licenses.sh` 에 함께 적는다.
- `external/` 은 커밋하지 않는다 (JUCE, sfizz, LAME, VSCO 2 CE, Sonatina Symphonic Orchestra, FreePats 기타, Karoryfer 첼로, 플러그인).
- MP3 인코더 LAME 4.0 은 LGPL-2.0 이상이고 실행 파일에 정적으로 링크한다. 릴리스에는 LAME 소스(`external/lame/lame-4.0.tar.gz`)나 그 링크를 함께 둔다 (`THIRD-PARTY.md`).
- Sonatina Symphonic Orchestra 는 CC Sampling Plus 1.0: 출처 표기, 광고와 홍보 사용 금지(그 샘플로 만든 곡 자체를 알리는 것만 허락). VSCO 2 CE, FreePats 스페인 클래식 기타, Karoryfer x bigcat 첼로는 CC0. 국악기 녹음 (국립국악원, 공공누리 제1유형) 은 그 녹음을 쓴 곡에 출처를 밝힌다 (문구는 `THIRD-PARTY.md`). 샘플과 곡은 릴리스에 넣지 않는다.

## 화면 문구

- 한국어. 레이블과 상태는 명사형, 문장은 합니다체 (전보체 "~함" 금지).
- 비유를 쓰지 않고 정확한 용어를 쓴다.
- 화면에 개발용 내용을 넣지 않는다: 계획, 단계 이름, API 이름, 미구현 자리표시.
- 스케일(음계) 이름과 그 묶음은 영어로 쓴다 ("Dorian", "Pentatonic"). 화음 이름은 한국어.

## 문서와 커밋

- 문서는 한국어로 쓰고, 가운뎃점(·)과 em-dash(—)를 쓰지 않는다 (쉼표나 괄호로 풀어쓴다). 단 **README.md 는 영어**이고 같은 내용의 한국어판 **README.ko.md** 를 함께 둔다. 사용 설명서 `docs/manual.md` 와 `docs/manual.ko.md` 도 같다. 하나를 고치면 다른 하나도 고친다. 계획 문서(docs/plans)와 화면 문구는 한국어.
- README.ko.md 는 한국 개발자가 쓴 문서의 말투로 쓴다: 파일은 "생성, 저장, 출력"하고 "씁니다"라고 하지 않는다. 선택, 측정, 추가, 삭제, 드래그, 더블클릭,
  우클릭, 내려받기, 명령줄, 오토메이션, 파라미터처럼 흔히 쓰는 말을 쓰고, 영어와 코드 뒤의 조사는 붙여 쓴다 ("JSON을", "`--loop`로"). 앱 메뉴 이름은 앱 문구 그대로 인용한다.
  README 에는 DAW 의 기능만 적는다: 만든 경위, 기존 DAW 를 쓰지 않은 이유, 자랑하는 문장을 넣지 않는다.
- 새 도구나 워크플로우를 만들면 사용법을 사용 설명서(`docs/manual.md`, `docs/manual.ko.md`)에 반드시 정리하고, README 에는 기능 목록의 한 줄과 대표 명령만 둔다. README 스크린샷은 `--snapshot` 으로 다시 그린다 (docs/images, 1440x900 으로 그려 1800x1125 로 줄인다).
- 코드 주석은 기능만 1~2줄, 비유 금지, 개발 과정 서술 금지.
- 커밋 메시지는 **영어** (타입 접두사 feat:, fix:, docs:, test:, chore:). push 된 이력은 재작성하지 않는다.
- Claude 와 함께 쓴 커밋은 `Co-Authored-By` 줄을 남긴다. push 된 이력은 다시 쓰지 않는다.
