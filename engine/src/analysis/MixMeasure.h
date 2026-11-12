// 렌더링하면서 스템마다 크기를 잰다 (스템 파일 없이). 10 ms 칸의 전력과 8192 샘플 프레임의 대역 전력을 모아 두고,
// 끝에 마디와 음표 머리의 창으로 묶는다. 전력은 두 채널의 평균 제곱이다.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mr
{

constexpr int measureBandCount = 12;

// 대역의 경계 (Hz): 0, 30, 60, 120, 250, 400, 500, 1000, 2000, 4000, 8000, 16000, 나이퀴스트
const std::array<double, measureBandCount + 1>& measureBandEdges (double sampleRate);

class StemMeter
{
public:
    explicit StemMeter (double sampleRate);

    // 곡의 처음부터 이어지는 샘플을 차례로 넣는다 (두 채널, 하나면 같은 신호로 본다)
    void add (const juce::AudioBuffer<float>& buffer, int start, int count);
    void finish();

    // 샘플 [s0, s1) 의 평균 전력, 대역마다의 전력 (합이 평균 전력과 같다), 사이드 ((L-R)/2) 의 평균 전력. 10 ms 칸 단위로 묶는다
    double power (int64_t s0, int64_t s1) const;
    std::array<double, measureBandCount> bandPower (int64_t s0, int64_t s1) const;
    double sidePower (int64_t s0, int64_t s1) const;
    int64_t samples() const noexcept { return written_; }

private:
    void flushHop();
    void flushFrame();

    double sampleRate_;
    int hop_;                               // 10 ms
    int frame_ = 8192;
    std::vector<double> hopPower_, hopSide_;
    std::vector<int64_t> frameCentre_;
    std::vector<std::array<double, measureBandCount>> frameEnergy_;   // 프레임 안의 대역 에너지 (제곱합, 두 채널의 평균)
    std::vector<int> frameLength_;
    std::array<std::vector<float>, 2> pending_;
    double hopSum_ = 0.0, hopSideSum_ = 0.0;
    int hopFill_ = 0;
    int64_t written_ = 0;
    std::vector<float> fftData_;
    std::vector<float> window_;
    std::array<double, measureBandCount + 1> edges_ {};
    bool finished_ = false;
};

struct NoteLevel
{
    double beat = 0.0;
    int pitch = 0;
    double seconds = 0.0;       // 음 켜짐에서 음 끔까지 (연주 처리 뒤)
    double attackDb = -120.0;   // 음 머리 0.03~0.25 초
    double earlyDb = -120.0;    // 0.05~0.1 초
    double lateDb = -120.0;     // 0.25~0.35 초 (머리 뒤에 갑자기 사라지는 음을 찾는다)
};

struct MeasuredStem
{
    std::string name;
    std::string kind;                                           // track, bus, master
    std::vector<double> barDb;                                  // 마디마다 평균 전력 (dB, 무음은 -120)
    std::vector<double> barSideDb;
    std::vector<std::array<double, measureBandCount>> barBandDb;
    std::vector<NoteLevel> notes;
};

struct MeasureReport
{
    bool applied = false;
    double sampleRate = 0.0;
    std::array<double, measureBandCount + 1> bandEdges {};
    std::vector<double> barStartBeats;     // 마디 수 + 1 (끝)
    std::vector<double> barStartSeconds;
    std::vector<MeasuredStem> stems;
};

double powerDb (double power);   // 10 log10, 무음은 -120

} // namespace mr
