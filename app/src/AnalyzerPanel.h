// 마스터 분석: 스펙트럼 (4096 점 FFT, 한 창, 20 Hz ~ 20 kHz 로그 눈금)과 라우드니스 (순간, 단기, 통합 LUFS, 트루 피크, 곡의 목표와의 차이).
// 값은 재생기의 마스터(리미터 뒤, 메트로놈 앞)에서 온다. update 를 메시지 스레드의 타이머가 부른다.
#pragma once

#include "realtime/RealtimePlayer.h"

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <optional>
#include <vector>

namespace daw
{

class AnalyzerPanel final : public juce::Component
{
public:
    static constexpr int width = 250;

    AnalyzerPanel();
    void update (mr::RealtimePlayer* player, std::optional<double> targetLufs);
    std::function<void()> onReset;

    struct Reading
    {
        float momentary = mr::LoudnessMeter::silent, shortTerm = mr::LoudnessMeter::silent, integrated = mr::LoudnessMeter::silent;
        float truePeak = mr::LoudnessMeter::silent;
        double seconds = 0.0;
    };
    const Reading& reading() const noexcept { return reading_; }
    const std::vector<float>& spectrum() const noexcept { return shown_; }   // 표시 칸마다의 dB (시험)

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::Rectangle<int> spectrumArea() const;

    static constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft_ { order };
    juce::dsp::WindowingFunction<float> window_ { (size_t) size, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> samples_ = std::vector<float> ((size_t) size, 0.0f);
    std::vector<float> work_ = std::vector<float> ((size_t) size * 2, 0.0f);
    std::vector<float> shown_;       // 칸마다 (가로 픽셀 2 개씩)의 dB, 천천히 내려온다
    Reading reading_;
    std::optional<double> target_;
    juce::TextButton reset_;
};

} // namespace daw
