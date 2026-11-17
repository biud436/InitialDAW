// 샘플레이트 바꾸기: 창을 씌운 sinc (낮출 때는 차단 주파수를 함께 낮춘다). 오디오 클립과 리버브의 IR 파일이 쓴다
#pragma once

#include <cstdint>

namespace mr::dsp
{

// ratio 는 입력 샘플레이트 / 출력 샘플레이트. out 의 j 번 샘플은 입력의 j * ratio 자리
void resample (const float* in, int64_t inFrames, double ratio, float* out, int64_t outFrames);

} // namespace mr::dsp
