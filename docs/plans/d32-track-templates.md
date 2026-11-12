# D32. 트랙 템플릿

> 작성일: 2026-10-23. 계기: 상용 DAW 는 트랙의 악기와 소리, 효과 사슬, 믹서와 센드를 템플릿(패치, 트랙 프리셋)으로 저장해 새 트랙을 그대로 만든다.
> D25 의 효과 사슬 저장보다 넓게 트랙 전체를 담는다.

## 1. 목표

- 트랙을 템플릿으로 저장한다 (음표, 클립, 곡선은 빼고 소리와 처리만).
- 새 트랙을 템플릿으로 더한다 (기본 템플릿과 내 템플릿).
- 다른 곡에 넣을 때 그 곡에 없는 버스, 키 트랙, 악기 정의를 처리한다.

## 2. 결정

| 항목 | 결정 | 근거 |
|---|---|---|
| 형식 | `{ "name", "track", "instrument"? }`. `track` 은 트랙 JSON 의 사본에서 `notes`, `clips`, 컨트롤러 곡선(`cc1` 등), `automation`, 믹서와 효과와 센드의 `automation`, `folder` 를 뺀 것 (악기, 아티큘레이션, 조옮김, `synth`, `plugin.state`, 믹서, 효과, 센드, 출력, 색은 남는다) | 곡 JSON 과 같은 모양이라 사람이 읽고 고친다. 플러그인 악기의 소리도 `plugin.state` 로 남는다 |
| 곡 안의 악기 | 트랙의 악기가 곡의 `instruments` 에 정의돼 있으면 (퀵 샘플러, 조각 등) 그 정의를 `instrument` 에 담고 상대 경로의 `file` 은 곡 폴더 기준 절대 경로로 | 다른 곡에서도 같은 소리 |
| 넣기 | `withTrackFromTemplate` (편곡 함수): 비어 있는 이름("Lead 2"), 곡에 없는 버스로 가는 센드와 출력, 곡에 없는 트랙을 키로 쓰는 사이드체인을 빼고 그 수를 알린다. 곡에 없는 악기 정의는 곡의 `instruments` 에 더한다. 편집 하나 | 곡 검사를 통과한다 |
| 기본 템플릿 | 보컬 (오디오, 저역 차단 EQ, 디에서, 컴프레서), 드럼 세트 (트랜지언트, 병렬 압축), 그랜드 피아노, 베이스 기타, 신스 리드 (코러스, 점8분음표 딜레이), 신스 패드 (넓게, 리버브), 현악 | 센드는 넣지 않는다 (버스는 곡마다 다르다) |
| 내 템플릿 | 앱 데이터 폴더의 `InitialDAW/TrackTemplates/<이름>.json`. 트랙 머리 메뉴 > 트랙 템플릿으로 저장 (플러그인 창의 지금 소리를 먼저 받는다), 트랙 더하기 메뉴 > 템플릿에서 | |
| 명령행 | `music-render track-template list`, `track-template save <곡> <트랙> --output <파일> [--name]`, `arrange <곡> add-track --template <id 또는 파일> [--name] [--at]` | |

## 3. 체크리스트

- [x] 엔진: `trackTemplateOf`, `withTrackFromTemplate`, `factoryTrackTemplates` (7개)
- [x] 명령행: `track-template list`, `track-template save`, `arrange add-track --template`
- [x] 앱: `saveTrackTemplate`, `trackTemplates`, `addTrackFromTemplate`, 트랙 머리 메뉴, 트랙 더하기 메뉴
- [x] 시험: 엔진 단위 1건 (빼는 것, 남는 것, 다른 곡에 넣기, 기본 템플릿 모두 곡 검사 통과), 명령행 4건, `--edit-test` 6건
- [x] 문서: README 두 판, CLAUDE.md, `docs/plans/index.md`

## 4. 확인한 것

- 곡 안의 악기(`sampler.bell`, 상대 경로 SFZ)를 쓰는 트랙: 템플릿이 음표, `cc1`, 폴더, 곡선을 빼고 조옮김, 색, 믹서 -3 dB, 센드, 효과 둘을 남기며 SFZ 경로를 절대 경로로.
  버스와 키 트랙이 없는 곡에 넣으면 센드와 사이드체인 둘을 빼고 (`dropped` 2), 악기 정의를 더하고, 이름은 "Bell 2".
- 기본 템플릿 일곱 개 모두 곡에 넣어 검사의 오류와 경고가 없다.
- 앱: 신스 리드 트랙(코러스, 딜레이, -4 dB, Hall 센드)을 저장해 더하면 "Lead 2" 가 음표 없이 같은 효과, 믹서, 센드로. 기본 보컬 템플릿은 효과 셋의 오디오 트랙. 실행 취소.

## 5. 남은 것

- 여러 트랙(버스와 함께)을 한 템플릿으로 저장하는 것, 템플릿 미리 듣기, 템플릿 이름 바꾸기와 지우기 화면은 없다 (파일을 지운다).
