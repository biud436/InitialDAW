# JUCE 걷어내기와 자체 프레임워크 (장기 계획, 2026-11-03 중단)

> **중단 (2026-11-03).** 결정: JUCE 를 계속 쓴다. 이유: JUCE 를 대체하려고 JUCE 코드를 참고하면서 의도적으로 비껴 쓰는 것은 부적절하다.
> 2단계를 시작하며 JUCE 소스를 읽어 동작을 똑같이 맞추고 코드는 다르게 쓰려 했다. 1단계의 직접 구현 (FFT, 리버브, 표준 MIDI 파일, XML 쓰기,
> 오디오 파일 형식, 녹음기의 쓰기 스레드) 도 JUCE 구현에 맞춰 쓴 것이지만 저자는 그대로 두기로 했다. 실행 파일의 라이선스는 지금처럼 AGPLv3 다.
> 2026-11-12 에 저장소 코드의 라이선스도 MIT 에서 AGPLv3 로 바꿨다 (아래 1절의 MIT 목표는 그 전의 것이다).
> 아래는 그때까지의 계획과 기록이다.

> 작성일: 2026-10-25. 계기: JUCE를 걷어내고 필요한 부분을 직접 개발하는 장기 계획. 작업 브랜치는 `own-framework`이고, 별도 작업 폴더(`../InitialDAW-own`, git worktree)에서 다룬다.

## 1. 목표

- 엔진(`music-render`)과 앱이 JUCE 없이 빌드되고, 지금과 같은 결과를 낸다.
- 저장소의 코드와 실행 파일 모두 MIT(및 BSD, zlib 같은 허용 라이선스 의존성)로 배포할 수 있게 된다. JUCE 8은 모든 모듈이
  AGPLv3 또는 상용 라이선스이므로(`external/juce/LICENSE.md`), 하나라도 남아 있으면 이 목표를 이루지 못한다.
- 지금의 기능과 시험을 하나도 잃지 않는다. 특히 창 없이 화면을 PNG로 그리는 시험(`--snapshot`, `--edit-test`)은 새 화면 도구에서도
  가장 먼저 갖춘다. 이것이 없으면 화면 작업을 검증할 방법이 없다.

## 2. 원칙

1. **기준선과 같아야 다음으로 넘어간다.** 0단계에서 지금 엔진의 결과(곡마다 렌더링, 내보낸 파일, 검사 결과, 저장 형식, 화면 PNG)를
   기준선으로 저장한다. 각 단계는 기준선과 비트 단위로 같거나, 다를 수밖에 없는 곳은 차이를 재서 이 문서에 이유와 함께 적는다.
2. **엔진 단계는 main에 합친다.** 1~4단계는 단계마다 기준선과 같음을 확인한 뒤 main에 합친다. 오래 갈라진 브랜치로 두지 않는다.
   화면 도구(5~6단계)만 이 브랜치에서 오래 진행하고, 다 되면 합친다.
3. **바꾸는 동안 앱은 계속 빌드되고 동작한다.** 엔진의 타입이 바뀌면 같은 커밋에서 앱의 사용처도 고친다.
4. **macOS 시스템 프레임워크는 쓴다.** Accelerate(vDSP), AudioToolbox, CoreAudio, CoreMIDI, AppKit, CoreGraphics, CoreText, ImageIO는
   운영체제의 일부라 라이선스 부담이 없다. 플랫폼은 지금처럼 macOS 하나다.
5. **새 의존성은 허용 라이선스만, 고정해서 받는다.** `build.sh`에서 태그나 커밋, 체크섬으로 고정하고 `THIRD-PARTY.md`와
   `tools/collect-licenses.sh`에 적는다 (지금 규칙과 같다). 저장소에 제3자 코드를 넣지 않는다.

## 3. 지금 JUCE를 쓰는 곳 (2026-10-25 소스에서 셈)

| 범위 | 줄 수 | JUCE를 쓰는 파일 | 사용 횟수 |
|---|---|---|---|
| `engine/src` | 24,489 | 96개 중 63개 | 1,651 |
| `app/src` | 24,412 | 52개 전부 | 4,108 |
| `engine/tests` | | 31개 중 27개 | 약 370 |

엔진의 갈래별 사용처와 대체 방법:

| 갈래 | 쓰는 곳 | JUCE 타입 (횟수) | 대체 |
|---|---|---|---|
| JSON 트리 | composition(576), cli(275), effects, midi | `var` 605, `DynamicObject` 86, `Array` 74, `JSON` 25, `Identifier` 22 | 자체 `mr::Json` (키 순서를 지키는 객체, 값 의미론). 파서와 저장 형식(`writeJson`)은 지금 출력과 바이트가 같아야 한다 |
| 문자열, 파일 | 거의 전부 | `String` 255, `File` 205, `StringArray`, `MemoryBlock`, 스트림 | `std::string`(UTF-8), `std::filesystem::path`, 작은 파일 도우미 |
| 오디오 버퍼 | effects, rendering, instruments, realtime | `AudioBuffer` 122, `FloatVectorOperations`, `Decibels` | 자체 `mr::AudioBuffer` (채널 포인터 배열, 바깥 메모리를 가리키는 모드) |
| MIDI | rendering, midi, realtime, instruments | `MidiMessage` 44, `MidiBuffer` 11, `MidiMessageSequence`, `MidiFile` | 자체 MIDI 이벤트와 표준 MIDI 파일 읽기, 쓰기 |
| FFT | analysis, effects | `dsp::FFT` | Accelerate vDSP (JUCE도 macOS에서는 vDSP를 쓰므로 같은 결과를 기대한다) |
| 리버브 | effects | `juce::Reverb` | 같은 알고리즘(Freeverb 계열)을 직접 구현. 연산 순서까지 같게 해 비트 단위로 맞춘다 |
| 오디오 파일 | io, instruments(AudioClips), realtime(녹음) | `AudioFormatManager`, 읽기와 쓰기, `ThreadedWriter` | WAV, AIFF는 직접 구현, FLAC은 libFLAC, Ogg는 libogg와 libvorbis (모두 BSD, JUCE에 들어 있던 판과 같은 판). MP3 디코딩은 AudioToolbox 또는 minimp3 (CC0) |
| MusicXML | notation | `XmlElement` | 직접 쓰는 XML 출력 |
| 플러그인 호스팅 | plugins, realtime, instruments | `AudioPluginInstance`, `PluginDescription`, `AudioPluginFormatManager`, `AudioPlayHead`, 메시지 스레드 | VST3 SDK 직접 호스팅 (3.8부터 MIT), AU는 AudioToolbox. 곡에 저장된 기존 `plugin.state`(JUCE의 상태 형식)를 읽고 쓸 수 있어야 한다 |
| 그 밖 | 곳곳 | `Random`, `MathConstants`, `jlimit`, `roundToInt`, `ScopedJuceInitialiser` | 표준 라이브러리와 작은 도우미 |

앱의 사용처는 화면(`Component` 112, `Graphics` 107, `PopupMenu` 117, `Slider` 57, 마우스와 키 이벤트, 파일 대화상자, 창),
오디오 장치(`AudioDeviceManager`), MIDI 입력(`MidiInput`), 화면 밖 그리기(스냅숏), 플러그인 편집기 창이다.

## 4. 단계

| 단계 | 내용 | 검증 기준 | 합칠 곳 |
|---|---|---|---|
| 0 | 기준선과 도구: 곡마다 렌더링 해시, 내보낸 파일, 검사 결과, 저장 형식 왕복, 화면 PNG를 저장하고 비교하는 도구. 렌더링의 결정성 확인 | 같은 커밋으로 두 번 만든 기준선이 같다 | main |
| 1 | 엔진의 말단 교체: FFT, 리버브, 오디오 파일 형식, MIDI 파일, MusicXML, base64 | 모든 곡의 렌더링과 내보낸 파일이 기준선과 같다 | main |
| 2 | 엔진의 기초 타입 교체: `mr::Json`, 문자열, 경로, 오디오 버퍼, MIDI 이벤트. 앱의 사용처도 함께 | 렌더링, 저장 형식 왕복, `validate` 출력, 명령줄 시험, 앱 시험이 모두 같다 | main |
| 3 | 플러그인 호스팅 직접 구현: VST3, AU, 별도 프로세스 검사, JUCE 상태 형식 호환, 시험용 플러그인을 VST3 SDK로 다시 만들기 | 시험 플러그인과 Dexed 곡의 렌더링이 같다. 실제 플러그인 창은 저자 확인 | main |
| 4 | 앱의 오디오와 MIDI 입출력: CoreAudio, CoreMIDI | `--self-test` 통과, 지연과 끊김을 재서 기록. 실제 장치는 저자 확인 | main |
| 5 | 화면 도구 직접 개발: AppKit 창 위에 CoreGraphics와 CoreText로 그리는 부품 모음. 화면 밖 PNG 그리기, 고해상도, 한국어 입력(NSTextInputClient), 메뉴와 파일 대화상자는 시스템 것 | 부품마다 PNG 스냅숏 시험 | 이 브랜치 |
| 6 | 패널 옮기기: 트랙 보기, 피아노 롤, 믹서, 효과 편집기, 신스 패널, 악보 보기, 대화상자. 옮기는 동안 JUCE 앱과 새 앱을 함께 빌드한다 | `--edit-test`의 모든 항목, 스냅숏, 저자의 사용 확인 | 다 되면 main |
| 7 | JUCE 제거: `build.sh`, CMake, `THIRD-PARTY.md`, `LICENSES/AGPL-3.0.txt`, README 정리 | JUCE 없이 처음부터 빌드하고 모든 시험 통과 | main |

1~4단계가 끝나면 `music-render`는 JUCE 없이 빌드된다. 화면 단계에서 멈추더라도 엔진은 MIT만으로 배포할 수 있다.

## 5. 0단계: 기준선

저장 위치는 `build/baseline/`(커밋하지 않는다)이다. 기준선을 만든 엔진 바이너리도 함께 보관해서, 해시가 다른 곡이 나오면 그 곡만
옛 바이너리로 다시 렌더링해 차이를 잴 수 있게 한다. 렌더링 파일 전체를 보관하지 않는 이유는 디스크다 (2026-10-25 남은 공간 17GB,
곡 38개를 32비트로 보관하면 약 2.6GB).

기준선에 넣는 것:

- 곡마다(`songs/`, `engine/examples/`, `templates/`): `validate` 출력, 32비트 부동소수점 WAV 렌더링의 해시와 길이, 피크, RMS,
  `export-midi`, `export-musicxml`의 해시, 저장 형식 왕복(읽어서 다시 저장한 JSON)의 해시
- 내보내기 형식마다(WAV 16/24/32, FLAC, Ogg, MP3, 반복 구간, 스템) 짧은 곡 하나의 파일 해시
- 앱 화면: 지금 CTest가 그리는 스냅숏과 README 스크린샷 명령의 PNG 해시
- 시험 수: 단위, 명령줄, `--edit-test`의 통과 항목 수

결정성: 2026-10-25에 「붉은 별」을 같은 바이너리로 연달아 두 번 렌더링했더니 해시가 달랐다. 다른 샘플은 2,228만 개 중 33만 개,
17.6~77초 사이의 34군데이고 가장 큰 차이는 -150.5 dBFS다. 오프라인 렌더러는 트랙을 정해진 순서로 처리하므로, 원인은 sfizz가
샘플을 뒤에서 읽어 오는 시점이다 (D8 계획 5절의 경쟁 상태와 같은 뿌리. 그때는 부하가 클 때 음이 끊겼다). sfizz 소스를 고치는 것은
저자가 정할 일로 남아 있으므로, 기준선 도구는 렌더링 파일을 보관하고 해시가 다르면 샘플 차이를 재서 -90 dBFS 아래면 같다고 본다.
같은 기준 바이너리로 다시 렌더링해도 「늦은 편지」는 -97.2 dBFS, 「세 목소리」 「한숨」 트리오판은 -119.7 dBFS 까지 달랐다 (차이가 곡 전체에
퍼지는 것은 목표 LUFS 맞춤이 곡 전체의 게인을 조금 바꾸기 때문이다). 곡 렌더링은 거친 확인이고, 정밀한 확인은 아래의 대조 시험이 한다.

**대조 시험 (`engine/tests/test_parity.cpp`).** JUCE 가 아직 빌드에 들어 있는 동안, 새 구현과 JUCE 구현에 같은 입력을 넣어 비트나 바이트까지
견준다. JUCE 를 걷어낸 뒤에는 이 시험이 쓰던 기대값을 파일로 남겨 비교한다.
음이 끊기는 경우는 차이가 훨씬 커서 이 기준에 걸린다. 기준선을 만드는 동안 부하가 높았던 곡은 비교에서 문제가 되면 그 곡만 다시 만든다
(`tools/baseline.py make --only <곡>`).

사용법:

```bash
tools/baseline.py make                 # 기준선 만들기 (곡 64개, 한 코어로 두 시간 남짓)
tools/baseline.py check                # 지금 빌드를 기준선과 비교
tools/baseline.py check --only red-star
tools/baseline.py diff songs/red-star/red_star.json   # 기준 엔진과 지금 엔진의 샘플 차이
```

## 6. 위험과 저자 확인이 필요한 것

| 위험 | 대응 |
|---|---|
| JSON 파서의 숫자 해석이 JUCE와 미세하게 다르면 렌더링이 달라진다 | 모든 곡과 시험 JSON을 두 파서로 읽어 값을 비교하는 차등 시험을 먼저 만든다 |
| `juce::var`는 객체를 참조로 공유하고 `mr::Json`은 값으로 복사한다. 공유에 기대던 코드가 있으면 동작이 바뀐다 | 바꾸기 전에 `var` 객체를 수정하는 곳을 모두 찾아 목록으로 만들고 하나씩 확인한다 |
| 기존 곡에 저장된 플러그인 상태는 JUCE가 만든 형식이다 | JUCE의 VST3, AU 상태 형식을 읽고 같은 형식으로 쓰는 변환기를 만들고, `engine/examples/dexed_demo.json`으로 확인한다 |
| 플러그인 호스팅은 플러그인마다 예외가 많다 | Dexed와 시험 플러그인으로 자동 시험을 하고, 저자가 가진 플러그인으로 직접 확인한다 |
| 오디오 장치의 지연, 끊김, 장치 바꾸기 | 자동 시험으로 콜백 시간과 끊김 수를 재고, 실제 장치는 저자가 확인한다 |
| 한국어 입력(트랙 이름, 마커, 가사) | 화면 도구 첫 부품에 NSTextInputClient를 넣고, 입력 중인 글자를 그리는 스냅숏 시험을 만든다. 실제 입력은 저자 확인 |
| 화면을 옮기는 동안 main에 새 기능이 들어가면 두 쪽이 갈라진다 | 5~6단계 동안에는 main의 새 기능을 줄이고, 이 브랜치에 main을 자주 합친다 |
| 디스크 공간 | 기준선은 해시와 요약만 보관한다 |

## 7. 진행

### 0단계 체크리스트

- [x] 작업 폴더와 빌드 (`../InitialDAW-own`, `external/`은 main의 것을 링크). CTest 12개 통과
- [x] 기준선 도구 `tools/baseline.py` (만들기, 비교, 샘플 차이)
- [x] 저장 형식 왕복 명령 (`music-render format`, 명령줄 시험 포함)
- [x] 렌더링 결정성 확인: 같은 바이너리도 다르다 (「붉은 별」 -150 dBFS, 「늦은 편지」 -97.2 dBFS). 비교는 -90 dBFS 허용치로, 정밀한 확인은 대조 시험으로
- [x] 기준선 저장 (곡 64개, 렌더링 2,278초, 2.7GB). 같은 엔진으로 네 곡을 다시 렌더링: 둘은 해시까지 같고 둘은 -132.5, -144.5 dBFS

### 1단계 체크리스트

- [x] FFT: `dsp/Fft` (vDSP, JUCE vDSP 엔진과 같은 칸 배치와 배율). 컨볼루션 리버브, 어택 찾기, 음높이 재기. 대조 시험: 길이 2~32768 의 정방향, 역방향, 크기가 비트까지 같다. 컨볼루션 리버브를 쓰는 곡 45개의 렌더링이 해시까지 같다
- [x] 알고리즘 리버브: `dsp/Freeverb` (같은 상수, 같은 0.01초 선형 변화, 같은 연산 순서). 대조 시험: 44.1, 48, 96 kHz, 블록 크기와 매개변수를 바꿔 가며 비트까지 같다 (이 리버브를 쓰는 곡은 없다)
- [x] 표준 MIDI 파일: `midi/Smf` (같은 시각의 넣는 순서, 같은 음 겹침에 음 끔 넣기, 러닝 스테이터스, 읽을 때 같은 시각의 끔을 켬보다 앞으로). 대조 시험과 곡 64개의 `export-midi` 가 바이트까지 같다
- [x] MusicXML: `io/XmlWriter` (두 칸 들여쓰기, 실체 참조와 숫자 참조, 짝수 쪽 반올림의 템포). 곡 64개의 `export-musicxml` 이 바이트까지 같다
- [x] 오디오 파일 쓰기: `io/Codecs`. WAV (RIFF, 52 바이트 JUNK, fmt, smpl, data. 정수는 `round(INT_MAX × x)` 를 오른쪽으로 민다), FLAC (libFLAC 1.4.3, 압축 5 뒤 loose mid-side, STREAMINFO 를 끝에 다시 쓴다), Ogg (libogg 1.3.5, libvorbis 1.3.7, 정수로 바꿨다가 2^-31 을 곱한 입력, 4096 칸을 채널 수로 나눈 단위). 대조 시험에서 모노와 스테레오, 모든 비트 깊이, Ogg 품질 0, 6, 10 이 JUCE 와 바이트까지 같다 (Ogg 는 스트림 번호와 CRC 를 빼고). 기준선의 내보내기 12 가지가 바이트까지 같다
- [x] 오디오 파일 읽기: WAV, AIFF, FLAC (libFLAC), Ogg (vorbisfile), MP3 와 그 밖의 형식 (AudioToolbox). WAV, FLAC, Ogg 를 읽은 float 가 JUCE 와 비트까지 같다
- [x] 녹음기: JUCE 의 쓰기 스레드 대신 잠금 없는 고리와 쓰기 스레드
- [x] 엔진 소스(`engine/src`)에서 `juce_dsp`, `juce_audio_formats` 를 쓰지 않는다. 앱이 아직 쓰므로 `music_render_core` 의 링크에는 남아 있다 (5~7단계에서 뺀다)
