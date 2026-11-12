# D6. 내보내기

> 작성일: 2026-10-11. 계기: 엔진 명령행은 WAV, 스템, MIDI 를 내지만 앱에는 열기와 저장뿐이다. 곡을 듣는 파일로 만들려면
> `music-render render` 를 치고 ffmpeg 로 MP3 를 만들고, 게임용 반복 파일은 `tools/make-loop.py`(Python 과 ffmpeg)로 잘라야 한다.
> 앱에서 고친 곡을 앱만으로 음악 파일로 낼 수 있어야 한다.

## 1. 목표

앱의 파일 메뉴에서 곡을 WAV, FLAC, Ogg Vorbis, MP3 로 내보낸다. 범위는 곡 전체, 반복 구간(`metadata.loop`), 마디 구간이다.
스템과 MIDI 도 함께 낼 수 있다. 내보내기는 엔진의 오프라인 렌더(재생과 같은 트랙 사슬)를 그대로 쓰므로 앱에서 들은 소리와 같고,
명령행 `music-render render` 로도 같은 파일이 나온다.

## 2. 결정

| 항목 | 결정 | 근거 |
|---|---|---|
| 형식을 넓히는 자리 | 엔진의 `writeAudioFile` (확장자로 형식을 고른다). 앱은 엔진을 부른다 | 엔진에 없는 것을 앱에서 흉내 내지 않는다. 명령행과 앱이 같은 파일을 낸다 |
| WAV, FLAC, Ogg | JUCE 에 들어 있는 코덱 (`JUCE_USE_FLAC`, `JUCE_USE_OGGVORBIS`). FLAC 은 16, 24비트, Ogg 는 VBR 품질 0~10 (기본 6, 약 192 kbps) | 새 의존성이 없다 |
| MP3 | LAME 4.0 (LGPL-2.0, 2026-07 릴리스)을 `build.sh` 가 받아 SHA-256 을 확인하고 정적 라이브러리로 빌드한다. CBR 128~320 kbps (기본 320). 앞에 LAME 정보 프레임(인코더 지연과 덧붙인 샘플)을 써서 이를 읽는 재생기는 앞뒤 빈 샘플을 지운다 | JUCE 에는 MP3 인코더가 없다 (`LAMEEncoderAudioFormat` 은 밖의 실행 파일을 부른다). LGPL 은 AGPLv3 실행 파일에 정적으로 넣을 수 있다 (소스를 함께 공개하므로 다시 링크할 수 있다) |
| 제목 태그 | 곡의 `title` 을 Ogg, FLAC 의 Vorbis 주석 `TITLE`, MP3 의 ID3v2.4 `TIT2`(UTF-8)로. WAV 에는 넣지 않는다 | 한국어 제목이 깨지지 않는 형식에만 넣는다 |
| 같은 바이트 | Ogg 는 JUCE 가 무작위로 고르는 스트림 번호를 고정값으로 바꿔 쓴다. 날짜를 넣지 않는다 | 같은 곡은 언제나 같은 파일이 된다 (지금의 WAV 원칙을 잇는다) |
| 반복 구간 내보내기 | 곡 전체를 렌더링한 뒤 처음부터 반복 끝까지 자르고, 끝 20ms 를 반복 시작 앞의 소리로 크로스페이드한다 (`make-loop.py` 와 같은 계산). WAV 는 smpl 청크(끝 샘플 포함), Ogg 와 FLAC 은 `LOOPSTART`, `LOOPLENGTH` 주석(샘플 단위). 이음매 앞 0.5초의 차이(dB)를 리포트에 적는다. MP3 는 받지 않는다 | 게임 엔진이 읽는 표시를 그대로 쓴다. MP3 는 앞뒤에 빈 샘플이 붙어 샘플 단위로 되풀이할 수 없다 |
| 마디 구간 내보내기 | 곡 전체를 렌더링한 뒤 구간 처음부터 구간 끝 뒤 페이드 길이(기본 2초)까지 자르고, 그 페이드 동안 줄인다. 처음이 0이 아니면 10ms 페이드인 | 목표 LUFS 맞춤이 곡 전체 기준이라 구간이 곡 안에서와 같은 크기로 들린다. 구간 끝 뒤의 울림과 다음 음은 페이드 안에서 줄어든다 |
| 스템 | 늘 곡 전체, WAV. 구간이나 반복을 고르면 앱에서는 스템을 끈다 | 스템은 렌더링 중에 블록마다 쓴다. 믹스를 다시 맞추는 용도라 곡 전체가 맞다 |
| 렌더러 나누기 | `OfflineRender` 가 만들기(악기와 트랙 사슬)와 돌리기(블록 처리, 버스, 마스터, 쓰기)를 나눈다. `renderComposition` 은 둘을 잇는다 | 플러그인 악기는 메시지 스레드에서만 만들 수 있다. 앱은 메시지 스레드에서 만들고 뒤 스레드에서 돌린다 |
| 진행 표시와 취소 | `RenderOptions::progress`(0~1, 블록 처리가 0.9 까지)를 앱이 타이머로 읽는다. 취소는 지금의 `cancel` | 긴 곡은 렌더링에 수십 초가 걸린다 |
| 앱의 대화상자 | 파일 > 내보내기 (⌘E). 형식, 품질(비트 깊이, kbps), 샘플레이트(44100, 48000), 범위, 스템, MIDI. 저장할 곳은 대화상자의 단추로 고른다 (기본: 곡 폴더, 곡 파일 이름). 끝나면 길이, LUFS, 트루 피크, 클리핑, 반복 이음매를 대화상자에 보이고 Finder 에서 보기 | 저자가 앱만으로 곡을 낸다 |
| 무엇을 내보내나 | 앱의 지금 문서(저장하지 않은 편집 포함) | 들은 소리를 낸다 |
| 창 없는 시험 | `--export-test 곡.json`: 앱의 내보내기 경로로 WAV 를 내고 엔진의 `renderComposition` 과 샘플 단위로 같은지, 다른 형식과 반복, 구간, 취소를 본다. `--snapshot --export` 가 대화상자를 그린다 | 창을 띄우지 않고 확인한다 |

## 3. 명령행

```bash
music-render render song.json --output song.mp3                    # 확장자로 형식: .wav .flac .ogg .mp3
music-render render song.json --output song.ogg --quality 6        # Ogg 품질 0~10, MP3 는 kbps (128, 160, 192, 224, 256, 320)
music-render render song.json --output loop.ogg --loop             # metadata.loop: 처음부터 반복 끝까지, LOOPSTART 와 LOOPLENGTH
music-render render song.json --output part.wav --bars 17-32 --fade 2   # 17~32마디, 끝 뒤 2초 페이드
```

## 4. 체크리스트

- [x] 엔진: `metadata.loop` 읽기 (`Composition::loop`, 잘못되면 경고)
- [x] 엔진: `writeAudioFile` (WAV smpl, FLAC 과 Ogg 의 Vorbis 주석, Ogg 스트림 번호 고정, MP3 LAME 와 정보 프레임, ID3v2.4)
- [x] 엔진: `OfflineRender` 만들기와 돌리기, 진행 표시, 반복과 구간 자르기, 리포트(형식, 반복 이음매, 구간)
- [x] 명령행: `--quality`, `--loop`, `--bars`, `--beats`, `--fade`, 리포트
- [x] `build.sh` 가 LAME 을 받고 빌드, CMake 가 찾으면 링크 (`MUSIC_RENDER_HAS_LAME`)
- [x] 시험: 형식마다 다시 읽기, 같은 바이트, 반복 이음매와 smpl, Ogg 와 FLAC 주석, 구간 길이, 진행 표시, 만들기와 돌리기를 나눠도 같은 결과, 명령행
- [x] 앱: 내보내기 대화상자, 뒤 스레드 작업(플러그인이면 메시지 스레드에서 만들기), 진행 막대와 취소, 결과 표시, MIDI
- [x] 앱 시험: `--export-test`, `--snapshot --export`
- [x] 문서: README 두 판, CLAUDE.md, THIRD-PARTY.md, `tools/collect-licenses.sh`

## 5. 확인한 것

- 엔진 단위 시험 117건(검사 2791): FLAC 16, 24비트가 샘플까지 다시 읽히고 32비트는 거절, Ogg 는 샘플 수가 같고 RMS 가 0.3 dB 안,
  MP3 는 길이와 피크가 맞는다. 같은 곡을 두 번 쓰면 Ogg, FLAC, MP3 가 바이트까지 같다. 한국어 제목과 `LOOPSTART`, `LOOPLENGTH` 가
  Ogg 와 FLAC 에 들고, 70000바이트 제목으로 주석이 쪽을 여럿 넘어도 디코더가 읽는다. WAV smpl 청크의 시작, 끝(포함), 샘플 주기.
  반복 내보내기는 크로스페이드 앞이 곡 전체와 같고 마지막 샘플이 반복 시작 바로 앞의 샘플이다. 마디 구간은 길이(구간과 페이드),
  곡 안과 같은 샘플, 같은 정규화 게인, 곡 끝을 넘는 구간. 만들기와 돌리기를 나눈 렌더가 한 번에 한 렌더와 바이트까지 같고 진행이 1 에 닿는다.
- ffmpeg 로 확인: MP3 는 LAME 정보 프레임 덕에 풀면 원래 샘플 수(96000, 48000)와 정확히 같다. ID3v2.4 제목 「칼날 질주」가 읽힌다.
  `metaflac` 이 FLAC 주석을 읽는다. Ogg 의 스트림 태그(TITLE, LOOPSTART, LOOPLENGTH)를 ffprobe 가 읽는다.
- 명령행 시험 161건: 형식마다 inspect, 같은 바이트, 품질을 낮추면 파일이 작다, 반복의 샘플 위치와 smpl, MP3 반복 거절,
  `metadata.loop` 없는 곡 거절, `--bars 2-3` 의 길이, 사용법 오류 여섯 가지.
- 실제 곡: 「칼날 질주」를 `render --loop` 로 내니 반복 288000~5184000 샘플, 이음매 차이 -29.4 dB 로 작곡 기록의 make-loop.py 값과 같고,
  파일이 make-loop.py 의 `blade_rush_loop.wav` 와 샘플마다 1.2e-7(24비트 양자화) 안에서 같다. 렌더링 31초.
- 앱 `--export-test` 33건: 32비트 WAV 가 엔진 `renderComposition` 과 바이트까지 같다, 스템과 MIDI, 목표 음량 -16 LUFS, Ogg 반복
  (2~10초, LOOPSTART=96000, LOOPLENGTH=384000, 제목), MP3 마디 구간 2~3마디(6초), 멈추면 파일과 스템 폴더가 남지 않는다,
  시험 신스(VST3) 트랙이 있는 곡은 악기를 메시지 스레드에서 만들고 뒤에서 렌더링해 엔진과 바이트까지 같다.
- CTest 9건 통과 (`app_export` 추가). `build.sh` 가 LAME 을 지운 상태에서 다시 받아 빌드한다.
- 화면: `--snapshot --export` 로 「칼날 질주」의 대화상자, `--export-test --png` 로 끝난 대화상자를 그려 확인했다.

## 6. 남은 것

- 창을 띄운 실물 확인(파일 고르기, 덮어쓰기 물음, Finder 에서 보기)은 저자가 한다.
- 플러그인 악기 곡도 뒤에서 잰 모니터 게인을 쓰도록 `LoudnessJob` 을 `OfflineRender` 로 옮길 수 있다 (지금은 플러그인 곡을 재지 않는다).
- WAV 를 다시 렌더링하지 않고 MP3 나 Ogg 로 바꾸는 명령(`music-render encode`)은 아직 없다. 곡 작업 흐름은 ffmpeg 를 쓴다.
