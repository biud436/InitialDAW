// 구현된 기능의 목록. 아직 없는 기능을 JSON 에 쓰면 검증이 "not implemented" 오류로 알린다.
// 단계가 끝날 때마다 여기를 켠다 (docs/plans/mu1-music-render.md 5절).
#pragma once

namespace mr::features
{

constexpr bool mixer = true;          // 3단계: gain, pan, mute, solo, buses, sends, stems
constexpr bool automation = true;     // 4단계: cc, pitchBend, aftertouch 곡선
constexpr bool articulation = true;   // 4단계: 악기 프로필의 아티큘레이션
constexpr bool effects = true;        // 5단계: eq, compressor, reverb, limiter
constexpr bool loudnessTarget = true;  // 5단계: master.targetLufs

} // namespace mr::features
