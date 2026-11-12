#include "effects/Effects.h"

#include "plugins/PluginEffect.h"

#include "analysis/Analysis.h"

#include "core/Json.h"
#include "effects/Automation.h"
#include "core/Random.h"
#include "dsp/Freeverb.h"
#include "io/AudioFile.h"

#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <map>

namespace mr
{

namespace
{

constexpr double pi = 3.141592653589793;

// ---------------------------------------------------------------------------
// 바이쿼드 (RBJ 쿡북, 배정밀도, TDF-II)
// ---------------------------------------------------------------------------
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1[2] = { 0.0, 0.0 }, z2[2] = { 0.0, 0.0 };

    float process (int ch, float x)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void set (double b0_, double b1_, double b2_, double a0, double a1_, double a2_)
    {
        b0 = b0_ / a0; b1 = b1_ / a0; b2 = b2_ / a0; a1 = a1_ / a0; a2 = a2_ / a0;
    }

    static Biquad make (const std::string& kind, double fs, double f, double q, double gainDb = 0.0)
    {
        Biquad b;
        f = std::clamp (f, 10.0, fs * 0.49);
        const double w0 = 2.0 * pi * f / fs, c = std::cos (w0), s = std::sin (w0);
        const double alpha = s / (2.0 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        if (kind == "lowpass")
            b.set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        else if (kind == "highpass")
            b.set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
        else if (kind == "peak")
            b.set (1 + alpha * A, -2 * c, 1 - alpha * A, 1 + alpha / A, -2 * c, 1 - alpha / A);
        else
        {
            const double sq = 2.0 * std::sqrt (A) * (s / 2.0) * std::sqrt (2.0);   // 기울기 S = 1
            if (kind == "lowShelf")
                b.set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                       (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
            else // highShelf
                b.set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                       (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
        }
        return b;
    }
};

// 12 dB/oct 는 Q 0.7071 하나, 24 dB/oct 는 버터워스 두 단 (Q 0.5412, 1.3066)
void addCut (std::vector<Biquad>& out, const std::string& kind, double fs, double f, int slope)
{
    if (slope >= 24)
    {
        out.push_back (Biquad::make (kind, fs, f, 0.5411961));
        out.push_back (Biquad::make (kind, fs, f, 1.3065630));
    }
    else
        out.push_back (Biquad::make (kind, fs, f, 0.7071068));
}

double param (const juce::var& v, const char* key, double fallback)
{
    const auto p = v.getProperty (key, juce::var());
    return json::isNumber (p) ? (double) p : fallback;
}

std::string paramText (const juce::var& v, const char* key, const std::string& fallback)
{
    const auto p = v.getProperty (key, juce::var());
    return p.isString() ? p.toString().toStdString() : fallback;
}

// 매개변수 값, 없으면 표의 기본값
double value (const juce::var& v, const char* type, const char* key)
{
    const auto* info = findEffectParameter (type, key);
    jassert (info != nullptr);
    return param (v, key, info != nullptr ? info->defaultValue : 0.0);
}

// 효과를 만들 때 곡선이 쓰는 것: 템포 지도, 표본율, 매개변수마다의 곡선
struct LaneContext
{
    std::shared_ptr<const TempoMap> tempo;
    double sampleRate = 48000.0;
    std::map<std::string, AutomationLane> lanes;
};

// 고정값이거나 곡선인 매개변수. 곡선이면 격자 점의 값을 낸다.
struct Param
{
    double constant = 0.0;
    SampleLane lane;

    bool automated() const noexcept { return ! lane.empty(); }
    double atGrid (int64_t grid) const { return automated() ? lane.atGrid (grid) : constant; }
};

Param makeParam (const juce::var& v, const char* type, const char* key, const LaneContext& ctx)
{
    Param out;
    out.constant = value (v, type, key);
    if (const auto it = ctx.lanes.find (key); it != ctx.lanes.end() && ! it->second.empty())
    {
        const auto* info = findEffectParameter (type, key);
        out.lane = SampleLane (it->second, ctx.tempo, ctx.sampleRate, info != nullptr && info->logarithmic);
    }
    return out;
}

SampleLane laneFor (const LaneContext& ctx, const char* key)
{
    const auto it = ctx.lanes.find (key);
    if (it == ctx.lanes.end() || it->second.empty())
        return {};
    return SampleLane (it->second, ctx.tempo, ctx.sampleRate, false);
}

double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

// eq 의 필터 목록. 역할(roles)은 바꿔 끼울 때 같은 자리의 필터 상태를 잇는 데 쓴다 (차단 단, 대역 번호).
void buildEqFilters (const juce::var& params, double fs, std::vector<Biquad>& filters, std::vector<int>& roles)
{
    filters.clear();
    roles.clear();
    auto addStages = [&] (const std::string& kind, double f, int slope, int roleBase)
    {
        const size_t before = filters.size();
        addCut (filters, kind, fs, f, slope);
        for (size_t i = before; i < filters.size(); ++i)
            roles.push_back (roleBase + (int) (i - before));
    };
    if (params.hasProperty ("lowCutHz"))
        addStages ("highpass", value (params, "eq", "lowCutHz"), (int) value (params, "eq", "lowCutSlope"), 0);
    if (params.hasProperty ("highCutHz"))
        addStages ("lowpass", value (params, "eq", "highCutHz"), (int) value (params, "eq", "highCutSlope"), 16);
    if (auto* bands = params.getProperty ("bands", juce::var()).getArray())
        for (int i = 0; i < bands->size(); ++i)
        {
            const auto& b = (*bands)[i];
            filters.push_back (Biquad::make (paramText (b, "type", "peak"), fs, param (b, "freqHz", 1000.0),
                                             param (b, "q", 0.7071), param (b, "gainDb", 0.0)));
            roles.push_back (32 + i);
        }
}

// ---------------------------------------------------------------------------
// EQ
// ---------------------------------------------------------------------------
class EqEffect final : public Effect
{
public:
    explicit EqEffect (juce::var p) : params_ (std::move (p)) {}
    std::string type() const override { return "eq"; }

    void prepare (double fs, int) override { buildEqFilters (params_, fs, filters_, roles_); }

    void continueFrom (Effect& previous) override
    {
        auto* p = dynamic_cast<EqEffect*> (&previous);
        if (p == nullptr)
            return;
        for (size_t i = 0; i < filters_.size(); ++i)
            for (size_t j = 0; j < p->filters_.size(); ++j)
                if (p->roles_[j] == roles_[i])
                {
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        filters_[i].z1[ch] = p->filters_[j].z1[ch];
                        filters_[i].z2[ch] = p->filters_[j].z2[ch];
                    }
                    break;
                }
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        for (auto& f : filters_)
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buffer.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    x[i] = f.process (ch, x[i]);
            }
    }

private:
    juce::var params_;
    std::vector<Biquad> filters_;
    std::vector<int> roles_;
};

// ---------------------------------------------------------------------------
// 컴프레서 (피드포워드, 스테레오 연동 피크 검출, 소프트 니, dB 영역 평활)
// 사이드체인 키는 setSidechainAlignment 로 이 자리의 소리와 같은 곡 시각에 맞춘다 (키나 소리를 늦춘다).
// ---------------------------------------------------------------------------
class CompressorEffect final : public Effect
{
public:
    explicit CompressorEffect (const juce::var& p)
        : threshold_ (value (p, "compressor", "thresholdDb")), ratio_ (value (p, "compressor", "ratio")),
          attackMs_ (value (p, "compressor", "attackMs")), releaseMs_ (value (p, "compressor", "releaseMs")),
          knee_ (value (p, "compressor", "kneeDb")), makeup_ (value (p, "compressor", "makeupDb")), mix_ (value (p, "compressor", "mix")),
          sidechain_ (paramText (p, "sidechain", ""))
    {
    }
    std::string type() const override { return "compressor"; }
    std::string sidechainSource() const override { return sidechain_; }
    int latencySamples() const override { return audioDelay_; }
    void setSidechainInput (const float* left, const float* right) override
    {
        keyL_ = left;
        keyR_ = right;
    }

    void setSidechainAlignment (int keyDelay, int audioDelay) override
    {
        keyDelay_ = sidechain_.empty() ? 0 : std::max (0, keyDelay);
        audioDelay_ = sidechain_.empty() ? 0 : std::max (0, audioDelay);
        allocateDelays();
    }

    void prepare (double fs, int) override
    {
        attack_ = std::exp (-1.0 / (attackMs_ * 0.001 * fs));
        release_ = std::exp (-1.0 / (releaseMs_ * 0.001 * fs));
        gr_ = 0.0;
        allocateDelays();
    }

    void continueFrom (Effect& previous) override
    {
        auto* p = dynamic_cast<CompressorEffect*> (&previous);
        if (p == nullptr)
            return;
        gr_ = p->gr_;
        if (p->keyDelay_ == keyDelay_)
        {
            std::copy (p->keyLine_.begin(), p->keyLine_.end(), keyLine_.begin());
            keyPos_ = p->keyPos_;
        }
        if (p->audioDelay_ == audioDelay_)
        {
            std::copy (p->audioLine_.begin(), p->audioLine_.end(), audioLine_.begin());
            audioPos_ = p->audioPos_;
        }
    }

    double gainComputer (double x) const
    {
        const double over = x - threshold_;
        if (knee_ > 0.0 && 2.0 * std::abs (over) <= knee_)
            return x + (1.0 / ratio_ - 1.0) * std::pow (over + knee_ / 2.0, 2.0) / (2.0 * knee_);
        if (2.0 * over > knee_)
            return threshold_ + over / ratio_;
        return x;
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        // 사이드체인이면 키 신호를 보고, 키를 받지 못한 블록은 무음으로 본다
        const bool keyed = ! sidechain_.empty();
        const float* kl = keyed ? keyL_ : l;
        const float* kr = keyed ? keyR_ : r;
        keyL_ = keyR_ = nullptr;
        if (keyDelay_ == 0 && audioDelay_ == 0)
        {
            for (int i = 0; i < n; ++i)
            {
                const double peak = kl == nullptr ? 0.0 : std::max (std::abs ((double) kl[i]), std::abs ((double) (kr != nullptr ? kr[i] : kl[i])));
                const double wet = gainFor (peak);
                l[i] = (float) (l[i] * wet);
                r[i] = (float) (r[i] * wet);
            }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            float keyLeft = kl == nullptr ? 0.0f : kl[i];
            float keyRight = kl == nullptr ? 0.0f : (kr != nullptr ? kr[i] : kl[i]);
            if (keyDelay_ > 0)
            {
                std::swap (keyLeft, keyLine_[(size_t) (2 * keyPos_)]);
                std::swap (keyRight, keyLine_[(size_t) (2 * keyPos_ + 1)]);
                keyPos_ = (keyPos_ + 1) % keyDelay_;
            }
            float inL = l[i], inR = r[i];
            if (audioDelay_ > 0)
            {
                std::swap (inL, audioLine_[(size_t) (2 * audioPos_)]);
                std::swap (inR, audioLine_[(size_t) (2 * audioPos_ + 1)]);
                audioPos_ = (audioPos_ + 1) % audioDelay_;
            }
            const double wet = gainFor (std::max (std::abs ((double) keyLeft), std::abs ((double) keyRight)));
            l[i] = (float) (inL * wet);
            r[i] = (float) (inR * wet);
        }
    }

private:
    // 검출 값 하나로 게인 감소를 갱신하고 이 샘플의 게인을 낸다
    double gainFor (double peak)
    {
        const double level = 20.0 * std::log10 (peak + 1.0e-12);
        const double target = gainComputer (level) - level;   // <= 0
        const double coef = target < gr_ ? attack_ : release_;
        gr_ = coef * gr_ + (1.0 - coef) * target;
        const double g = std::pow (10.0, (gr_ + makeup_) / 20.0);
        return mix_ * g + (1.0 - mix_);
    }

    void allocateDelays()
    {
        keyLine_.assign ((size_t) (2 * keyDelay_), 0.0f);
        audioLine_.assign ((size_t) (2 * audioDelay_), 0.0f);
        keyPos_ = audioPos_ = 0;
    }

    double threshold_, ratio_, attackMs_, releaseMs_, knee_, makeup_, mix_;
    std::string sidechain_;
    const float* keyL_ = nullptr;
    const float* keyR_ = nullptr;
    double attack_ = 0.0, release_ = 0.0, gr_ = 0.0;
    int keyDelay_ = 0, audioDelay_ = 0;
    std::vector<float> keyLine_, audioLine_;   // 좌우를 번갈아 담은 고리
    int keyPos_ = 0, audioPos_ = 0;
};

// ---------------------------------------------------------------------------
// 디스토션: tanh 웨이브셰이퍼 뒤에 2극 저역 통과(톤). 저음 금관이나 신스를 거칠게 만들 때 (예고편의 브람)
// ---------------------------------------------------------------------------
class DistortionEffect final : public Effect
{
public:
    DistortionEffect (const juce::var& p, const LaneContext& ctx)
        : drive_ (std::pow (10.0, value (p, "distortion", "driveDb") / 20.0)), mix_ (value (p, "distortion", "mix")),
          toneHz_ (value (p, "distortion", "toneHz")), output_ (std::pow (10.0, value (p, "distortion", "outputDb") / 20.0)),
          driveDb_ (makeParam (p, "distortion", "driveDb", ctx)), mixParam_ (makeParam (p, "distortion", "mix", ctx))
    {
    }
    std::string type() const override { return "distortion"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return driveDb_.automated() || mixParam_.automated(); }

    void prepare (double fs, int) override
    {
        a_ = std::exp (-2.0 * pi * std::min (toneHz_, fs * 0.45) / fs);
        for (auto& st : state_)
            st = { 0.0, 0.0 };
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<DistortionEffect*> (&previous))
            state_ = p->state_;
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        if (! driveDb_.automated() && ! mixParam_.automated())
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buffer.getWritePointer (ch);
                auto& st = state_[(size_t) ch];
                for (int i = 0; i < n; ++i)
                    x[i] = shape (st, x[i], drive_, mix_);
            }
            return;
        }
        // 드라이브는 격자 점의 선형 게인 사이를, 비율은 그 값 사이를 직선으로 잇는다
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double d0 = dbToGain (driveDb_.atGrid (grid)), d1 = dbToGain (driveDb_.atGrid (grid + kAutomationGrid));
            const double m0 = mixParam_.atGrid (grid), m1 = mixParam_.atGrid (grid + kAutomationGrid);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buffer.getWritePointer (ch, offset);
                auto& st = state_[(size_t) ch];
                for (int j = 0; j < count; ++j)
                {
                    const double f = gridFraction (position_, offset, j, grid);
                    x[j] = shape (st, x[j], d0 + (d1 - d0) * f, m0 + (m1 - m0) * f);
                }
            }
        });
    }

private:
    float shape (std::array<double, 2>& st, float in, double drive, double mix) const
    {
        const double dry = in;
        double wet = std::tanh (drive * dry);
        st[0] = (1.0 - a_) * wet + a_ * st[0];   // 1극 둘을 이어 12 dB/oct
        st[1] = (1.0 - a_) * st[0] + a_ * st[1];
        wet = st[1];
        return (float) ((dry * (1.0 - mix) + wet * mix) * output_);
    }

    double drive_, mix_, toneHz_, output_;
    Param driveDb_, mixParam_;
    BlockPosition position_;
    double a_ = 0.0;
    std::array<std::array<double, 2>, 2> state_ {};
};

// ---------------------------------------------------------------------------
// 딜레이: 스테레오 또는 핑퐁. 되먹임 경로에 저역 차단과 고역 차단(1극)이 있어 반복할수록 얇고 어두워진다.
// 핑퐁은 입력의 합을 왼쪽에 넣고, 왼쪽의 메아리가 오른쪽으로, 오른쪽이 다시 왼쪽으로 간다.
// timeBeats 이면 메아리는 언제나 그 박 수 앞의 소리다 (템포 램프에서는 길이가 격자마다 바뀌고 4점 보간으로 읽는다).
// ---------------------------------------------------------------------------
class DelayEffect final : public Effect
{
public:
    DelayEffect (const juce::var& p, const LaneContext& ctx)
        : timeMs_ (value (p, "delay", "timeMs")), feedback_ (makeParam (p, "delay", "feedback", ctx)), mix_ (makeParam (p, "delay", "mix", ctx)),
          pingPong_ (value (p, "delay", "pingPong") >= 0.5), lowCutHz_ (value (p, "delay", "lowCutHz")),
          highCutHz_ (value (p, "delay", "highCutHz")), timeBeats_ (p.hasProperty ("timeBeats") ? value (p, "delay", "timeBeats") : 0.0),
          tempo_ (ctx.tempo)
    {
    }
    std::string type() const override { return "delay"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return timeBeats_ > 0.0 || feedback_.automated() || mix_.automated(); }

    // 박에 맞춘 지연이 가장 느린 템포에서 필요한 길이 (초)
    static double longestSeconds (double timeBeats, const TempoMap& tempo) { return timeBeats * 60.0 / tempo.slowestBpm(); }

    void prepare (double fs, int) override
    {
        fs_ = fs;
        if (timeBeats_ > 0.0)
            length_ = (int) std::ceil (longestSeconds (timeBeats_, *tempo_) * fs) + 8;
        else
            length_ = std::max (1, (int) std::lround (timeMs_ * 0.001 * fs));
        for (auto& line : lines_)
            line.assign ((size_t) length_, 0.0f);
        pos_ = 0;
        lowA_ = std::exp (-2.0 * pi * lowCutHz_ / fs);
        highA_ = std::exp (-2.0 * pi * highCutHz_ / fs);
        for (int ch = 0; ch < 2; ++ch)
            hpX_[ch] = hpY_[ch] = lpY_[ch] = 0.0;
    }

    // 줄의 길이가 달라도 가장 최근의 샘플부터 옮긴다 (짧아지면 오래된 쪽을 버린다)
    void continueFrom (Effect& previous) override
    {
        auto* p = dynamic_cast<DelayEffect*> (&previous);
        if (p == nullptr)
            return;
        const int count = std::min (length_, p->length_);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int k = 1; k <= count; ++k)
                lines_[ch][(size_t) ((pos_ - k + length_) % length_)] = p->lines_[ch][(size_t) ((p->pos_ - k + p->length_) % p->length_)];
            hpX_[ch] = p->hpX_[ch];
            hpY_[ch] = p->hpY_[ch];
            lpY_[ch] = p->lpY_[ch];
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        if (timeBeats_ <= 0.0 && ! feedback_.automated() && ! mix_.automated())
        {
            for (int i = 0; i < n; ++i)
            {
                const double echo[2] = { lines_[0][(size_t) pos_], lines_[1][(size_t) pos_] };
                step (io, i, echo, feedback_.constant, mix_.constant);
            }
            return;
        }
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double f0 = feedback_.atGrid (grid), f1 = feedback_.atGrid (grid + kAutomationGrid);
            const double m0 = mix_.atGrid (grid), m1 = mix_.atGrid (grid + kAutomationGrid);
            const double l0 = timeBeats_ > 0.0 ? delaySamplesAt (grid) : 0.0;
            const double l1 = timeBeats_ > 0.0 ? delaySamplesAt (grid + kAutomationGrid) : 0.0;
            for (int j = 0; j < count; ++j)
            {
                const int i = offset + j;
                const double f = gridFraction (position_, offset, j, grid);
                double echo[2];
                if (timeBeats_ > 0.0)
                {
                    const double length = std::clamp (l0 + (l1 - l0) * f, 3.0, (double) length_ - 4.0);
                    echo[0] = readFractional (lines_[0], length);
                    echo[1] = readFractional (lines_[1], length);
                }
                else
                {
                    echo[0] = lines_[0][(size_t) pos_];
                    echo[1] = lines_[1][(size_t) pos_];
                }
                step (io, i, echo, f0 + (f1 - f0) * f, m0 + (m1 - m0) * f);
            }
        });
    }

private:
    // 메아리 하나를 내고 지연선에 새 샘플을 쓴다
    void step (float* const* io, int i, const double (&echo)[2], double feedback, double mix)
    {
        double fb[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            // 되먹임 경로의 색: 1극 고역 통과(저역 차단) 뒤 1극 저역 통과(고역 차단)
            const double hp = hpA (ch, echo[ch]);
            lpY_[ch] = (1.0 - highA_) * hp + highA_ * lpY_[ch];
            fb[ch] = lpY_[ch] * feedback;
        }
        const double inL = io[0][i], inR = io[1][i];
        if (pingPong_)
        {
            lines_[0][(size_t) pos_] = (float) (0.5 * (inL + inR) + fb[1]);
            lines_[1][(size_t) pos_] = (float) fb[0];
        }
        else
        {
            lines_[0][(size_t) pos_] = (float) (inL + fb[0]);
            lines_[1][(size_t) pos_] = (float) (inR + fb[1]);
        }
        io[0][i] = (float) (inL * (1.0 - mix) + echo[0] * mix);
        io[1][i] = (float) (inR * (1.0 - mix) + echo[1] * mix);
        pos_ = (pos_ + 1) % length_;
    }

    // 곡의 샘플에서 timeBeats 앞의 박까지의 시간 (샘플)
    double delaySamplesAt (int64_t sample) const
    {
        const double t = (double) sample / fs_;
        return (t - tempo_->secondsAt (tempo_->beatAt (t) - timeBeats_)) * fs_;
    }

    // length 샘플 앞의 값 (4점 3차 에르미트 보간). length 는 3 이상이라 아직 쓰지 않은 칸을 읽지 않는다.
    double readFractional (const std::vector<float>& line, double length) const
    {
        const double at = (double) pos_ - length + (double) length_;
        const int i1 = (int) std::floor (at);
        const double f = at - (double) i1;
        auto x = [&] (int i) { return (double) line[(size_t) (((i % length_) + length_) % length_)]; };
        const double xm1 = x (i1 - 1), x0 = x (i1), x1 = x (i1 + 1), x2 = x (i1 + 2);
        const double c1 = 0.5 * (x1 - xm1);
        const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
        const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    double hpA (int ch, double x)
    {
        const double y = lowA_ * (hpY_[ch] + x - hpX_[ch]);
        hpX_[ch] = x;
        hpY_[ch] = y;
        return y;
    }

    double timeMs_;
    Param feedback_, mix_;
    bool pingPong_;
    double lowCutHz_, highCutHz_, timeBeats_;
    std::shared_ptr<const TempoMap> tempo_;
    double fs_ = 48000.0;
    BlockPosition position_;
    int length_ = 1, pos_ = 0;
    std::vector<float> lines_[2];
    double lowA_ = 0.0, highA_ = 0.0;
    double hpX_[2] {}, hpY_[2] {}, lpY_[2] {};
};

// ---------------------------------------------------------------------------
// 룩어헤드 브릭월 리미터. 필요 게인 g_req 의 [-L, +L] 최솟값을 다시 [-L, +L] 로 평균 내므로 각 샘플의
// 게인은 그 샘플의 g_req 보다 크지 않다 (넘침이 없다). 회복은 releaseMs 로 천천히. 지연 = 2L.
// ---------------------------------------------------------------------------
class LimiterEffect final : public Effect
{
public:
    explicit LimiterEffect (const juce::var& p)
        : ceiling_ (std::pow (10.0, value (p, "limiter", "ceilingDb") / 20.0) * 0.99999), releaseMs_ (value (p, "limiter", "releaseMs")),
          lookaheadMs_ (value (p, "limiter", "lookaheadMs")), truePeak_ (value (p, "limiter", "truePeak") >= 0.5)
    {
    }
    std::string type() const override { return "limiter"; }
    // truePeak: 샘플 사이의 봉우리(4배 보간)까지 보고 깎는다. 검출이 보간 탭의 절반(6샘플)만큼 늦어 지연도 그만큼 늘어난다.
    int latencySamples() const override { return 2 * look_ + interpDelay(); }
    int interpDelay() const { return truePeak_ ? 6 : 0; }

    void prepare (double fs, int) override
    {
        look_ = std::max (1, (int) std::lround (lookaheadMs_ * 0.001 * fs));
        release_ = 1.0 - std::exp (-1.0 / (releaseMs_ * 0.001 * fs));
        const int d = 2 * look_ + interpDelay();
        delay_.assign (2, std::vector<float> ((size_t) d, 0.0f));   // 지연 = 2L (+ 보간 지연): 게인을 계산한 샘플과 맞물린다
        for (auto& h : history_)
            h.fill (0.0f);
        historyPos_ = 0;
        boxRing_.assign ((size_t) (2 * look_ + 1), 1.0);
        boxSum_ = (double) boxRing_.size();
        minT_.assign ((size_t) (2 * look_ + 3), 0);
        minV_.assign (minT_.size(), 0.0);
        minHead_ = minCount_ = 0;
        t_ = 0;
        out_ = 1.0;
        delayPos_ = 0;
        boxPos_ = 0;
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        const int d = 2 * look_;
        const size_t ringSize = (size_t) (d + interpDelay());
        for (int i = 0; i < n; ++i, ++t_)
        {
            double peak = std::max (std::abs ((double) l[i]), std::abs ((double) r[i]));
            if (truePeak_)
            {
                // 최근 12샘플로 6샘플 앞의 샘플과 그 다음 샘플 사이 세 점을 보간한다
                history_[0][(size_t) historyPos_] = l[i];
                history_[1][(size_t) historyPos_] = r[i];
                historyPos_ = (historyPos_ + 1) % 12;
                const auto& phases = truePeakInterpolator();
                peak = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto at = [&] (int k) { return (double) history_[(size_t) ch][(size_t) ((historyPos_ + k) % 12)]; };
                    peak = std::max (peak, std::abs (at (5)));
                    for (const auto& c : phases)
                    {
                        double y = 0.0;
                        for (int j = 0; j < 12; ++j)
                            y += at (j) * c[(size_t) j];
                        peak = std::max (peak, std::abs (y));
                    }
                }
            }
            const double req = peak > ceiling_ ? ceiling_ / peak : 1.0;
            // [t-2L, t] 의 최솟값 = 가운데 t-L 의 g_min (값이 오름차순인 대기열, 고정 크기 고리)
            const int cap = (int) minT_.size();
            while (minCount_ > 0 && minV_[(size_t) ((minHead_ + minCount_ - 1) % cap)] >= req)
                --minCount_;
            minT_[(size_t) ((minHead_ + minCount_) % cap)] = t_;
            minV_[(size_t) ((minHead_ + minCount_) % cap)] = req;
            ++minCount_;
            while (minT_[(size_t) minHead_] < t_ - d)
            {
                minHead_ = (minHead_ + 1) % cap;
                --minCount_;
            }
            const double gMin = minV_[(size_t) minHead_];
            // g_min 을 2L+1 로 평균 = t-2L 의 평활 게인
            boxSum_ += gMin - boxRing_[(size_t) boxPos_];
            boxRing_[(size_t) boxPos_] = gMin;
            boxPos_ = (boxPos_ + 1) % (int) boxRing_.size();
            if ((t_ & 0xFFFF) == 0)
            {
                boxSum_ = 0.0;
                for (double v : boxRing_)
                    boxSum_ += v;
            }
            const double smooth = std::min (1.0, boxSum_ / (double) boxRing_.size());
            out_ = smooth < out_ ? smooth : std::min (smooth, out_ + (smooth - out_) * release_);

            // t-2L 의 입력을 꺼내고 지금 입력을 넣는다
            const size_t readPos = (size_t) delayPos_;
            const float dl = delay_[0][readPos], dr = delay_[1][readPos];
            delay_[0][readPos] = l[i];
            delay_[1][readPos] = r[i];
            delayPos_ = (int) ((readPos + 1) % ringSize);
            l[i] = (float) (dl * out_);
            r[i] = (float) (dr * out_);
        }
    }

    // 미리 보기 길이와 truePeak 가 같을 때만 잇는다 (지연이 같다)
    void continueFrom (Effect& previous) override
    {
        auto* p = dynamic_cast<LimiterEffect*> (&previous);
        if (p == nullptr || p->look_ != look_ || p->truePeak_ != truePeak_)
            return;
        history_ = p->history_;
        historyPos_ = p->historyPos_;
        for (size_t ch = 0; ch < delay_.size(); ++ch)
            std::copy (p->delay_[ch].begin(), p->delay_[ch].end(), delay_[ch].begin());
        std::copy (p->boxRing_.begin(), p->boxRing_.end(), boxRing_.begin());
        boxSum_ = p->boxSum_;
        std::copy (p->minT_.begin(), p->minT_.end(), minT_.begin());
        std::copy (p->minV_.begin(), p->minV_.end(), minV_.begin());
        minHead_ = p->minHead_;
        minCount_ = p->minCount_;
        t_ = p->t_;
        out_ = p->out_;
        delayPos_ = p->delayPos_;
        boxPos_ = p->boxPos_;
    }

private:
    double ceiling_, releaseMs_, lookaheadMs_;
    bool truePeak_ = false;
    std::array<std::array<float, 12>, 2> history_ {};
    int historyPos_ = 0;
    int look_ = 1;
    double release_ = 0.01;
    std::vector<std::vector<float>> delay_;
    std::vector<double> boxRing_;
    double boxSum_ = 0.0;
    std::vector<int64_t> minT_;
    std::vector<double> minV_;
    int minHead_ = 0, minCount_ = 0;
    int64_t t_ = 0;
    double out_ = 1.0;
    int delayPos_ = 0, boxPos_ = 0;
};

// 스테레오 배치: 폭(width)으로 옆 성분을 줄이고, 가운데 성분을 등전력 팬으로 무대 위치(pan)에 놓는다.
// 밸런스와 달리 한쪽 채널을 끄지 않고 소리의 상 자체를 옮긴다 (스테레오로 녹음한 섹션을 무대에 앉힐 때).
class StereoEffect final : public Effect
{
public:
    StereoEffect (const juce::var& p, const LaneContext& ctx)
        : width_ (value (p, "stereo", "width")), pan_ (value (p, "stereo", "pan")), widthParam_ (makeParam (p, "stereo", "width", ctx))
    {
    }
    std::string type() const override { return "stereo"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return widthParam_.automated(); }
    void prepare (double, int) override
    {
        const double theta = (std::clamp (pan_, -1.0, 1.0) + 1.0) * pi / 4.0;
        gainL_ = std::cos (theta) * std::sqrt (2.0);
        gainR_ = std::sin (theta) * std::sqrt (2.0);
    }
    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        if (! widthParam_.automated())
        {
            for (int i = 0; i < n; ++i)
                place (l[i], r[i], width_);
            return;
        }
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double w0 = widthParam_.atGrid (grid), w1 = widthParam_.atGrid (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
                place (l[offset + j], r[offset + j], w0 + (w1 - w0) * gridFraction (position_, offset, j, grid));
        });
    }

private:
    void place (float& l, float& r, double width) const
    {
        const double mid = 0.5 * (l + r);
        const double side = 0.5 * (l - r) * width;
        l = (float) (mid * gainL_ + side);
        r = (float) (mid * gainR_ - side);
    }

    double width_, pan_;
    Param widthParam_;
    BlockPosition position_;
    double gainL_ = 1.0, gainR_ = 1.0;
};

class GainEffect final : public Effect
{
public:
    GainEffect (const juce::var& p, const LaneContext& ctx)
        : gain_ ((float) std::pow (10.0, value (p, "gain", "gainDb") / 20.0)), gainDb_ (makeParam (p, "gain", "gainDb", ctx))
    {
    }
    std::string type() const override { return "gain"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return gainDb_.automated(); }
    void prepare (double, int) override {}
    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        if (! gainDb_.automated())
        {
            buffer.applyGain (0, n, gain_);
            return;
        }
        // 격자 점의 선형 게인 사이를 직선으로 잇는다
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double g0 = dbToGain (gainDb_.atGrid (grid)), g1 = dbToGain (gainDb_.atGrid (grid + kAutomationGrid));
            for (int ch = 0; ch < 2; ++ch)
            {
                float* x = buffer.getWritePointer (ch, offset);
                for (int j = 0; j < count; ++j)
                    x[j] = (float) (x[j] * (g0 + (g1 - g0) * gridFraction (position_, offset, j, grid)));
            }
        });
    }

private:
    float gain_;
    Param gainDb_;
    BlockPosition position_;
};

// ---------------------------------------------------------------------------
// 리버브
// ---------------------------------------------------------------------------
// 리버브의 원음과 잔향 크기. dryDb 와 wetDb 는 있으면(고정값이나 곡선) mix 대신 쓴다. 곡선이면 격자 점의 선형 게인 사이를 잇는다.
class ReverbLevels
{
public:
    ReverbLevels (const juce::var& p, const LaneContext& ctx)
        : mix_ (makeParam (p, "reverb", "mix", ctx)), dryDb_ (makeParam (p, "reverb", "dryDb", ctx)), wetDb_ (makeParam (p, "reverb", "wetDb", ctx)),
          dryInDb_ (p.hasProperty ("dryDb") || dryDb_.automated()), wetInDb_ (p.hasProperty ("wetDb") || wetDb_.automated())
    {
    }
    bool automated() const noexcept
    {
        return (dryInDb_ ? dryDb_.automated() : mix_.automated()) || (wetInDb_ ? wetDb_.automated() : mix_.automated());
    }
    double dryAt (int64_t grid) const { return dryInDb_ ? dbToGain (dryDb_.atGrid (grid)) : 1.0 - mix_.atGrid (grid); }
    double wetAt (int64_t grid) const { return wetInDb_ ? dbToGain (wetDb_.atGrid (grid)) : mix_.atGrid (grid); }

    // 블록의 샘플마다 fn (i, 원음 게인, 잔향 게인)
    template <typename Fn>
    void forEach (BlockPosition position, int n, Fn&& fn) const
    {
        forEachGridStep (position, n, [&] (int offset, int count, int64_t grid)
        {
            const double d0 = dryAt (grid), d1 = dryAt (grid + kAutomationGrid);
            const double w0 = wetAt (grid), w1 = wetAt (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
            {
                const double f = gridFraction (position, offset, j, grid);
                fn (offset + j, d0 + (d1 - d0) * f, w0 + (w1 - w0) * f);
            }
        });
    }

private:
    Param mix_, dryDb_, wetDb_;
    bool dryInDb_, wetInDb_;
};

class ConvolutionReverb final : public Effect
{
public:
    ConvolutionReverb (juce::AudioBuffer<float> impulse, const juce::var& p, int partition, const LaneContext& ctx)
        : impulse_ (std::move (impulse)), partition_ (partition), levels_ (p, ctx)
    {
        const double mix = value (p, "reverb", "mix");
        dry_ = p.hasProperty ("dryDb") ? std::pow (10.0, param (p, "dryDb", 0.0) / 20.0) : 1.0 - mix;
        wet_ = p.hasProperty ("wetDb") ? std::pow (10.0, param (p, "wetDb", 0.0) / 20.0) : mix;
        lowCut_ = value (p, "reverb", "lowCutHz");
        highCut_ = value (p, "reverb", "highCutHz");
    }
    std::string type() const override { return "reverb"; }
    int latencySamples() const override { return conv_.latencySamples(); }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return levels_.automated(); }

    void prepare (double fs, int maxBlock) override
    {
        conv_.prepare (impulse_, partition_);
        filters_.clear();
        filters_.push_back (Biquad::make ("highpass", fs, lowCut_, 0.7071));
        filters_.push_back (Biquad::make ("lowpass", fs, highCut_, 0.7071));
        mono_.assign ((size_t) maxBlock, 0.0f);
        wetL_.assign ((size_t) maxBlock, 0.0f);
        wetR_.assign ((size_t) maxBlock, 0.0f);
        dryDelay_.assign (2, std::vector<float> ((size_t) std::max (1, partition_), 0.0f));
        dryPos_ = 0;
    }

    // 입력의 주파수 영역 기록을 옮겨 새 IR 로 지난 소리의 꼬리를 이어 만든다
    void continueFrom (Effect& previous) override
    {
        auto* p = dynamic_cast<ConvolutionReverb*> (&previous);
        if (p == nullptr || p->partition_ != partition_ || p->filters_.size() != filters_.size())
            return;
        conv_.continueFrom (p->conv_);
        for (size_t i = 0; i < filters_.size(); ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                filters_[i].z1[ch] = p->filters_[i].z1[ch];
                filters_[i].z2[ch] = p->filters_[i].z2[ch];
            }
        for (size_t ch = 0; ch < dryDelay_.size(); ++ch)
            std::copy (p->dryDelay_[ch].begin(), p->dryDelay_[ch].end(), dryDelay_[ch].begin());
        dryPos_ = p->dryPos_;
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        if ((int) mono_.size() < n)
        {
            mono_.resize ((size_t) n);
            wetL_.resize ((size_t) n);
            wetR_.resize ((size_t) n);
        }
        for (int i = 0; i < n; ++i)
            mono_[(size_t) i] = 0.5f * (l[i] + r[i]);
        conv_.process (mono_.data(), wetL_.data(), wetR_.data(), n);
        for (auto& f : filters_)
            for (int i = 0; i < n; ++i)
            {
                wetL_[(size_t) i] = f.process (0, wetL_[(size_t) i]);
                wetR_[(size_t) i] = f.process (1, wetR_[(size_t) i]);
            }
        // 젖은 소리가 분할 크기만큼 늦으므로 마른 소리도 같이 늦춘다. 곡선은 나가는 소리의 곡 위치(position_)에서 읽는다.
        auto mixOne = [&] (int i, double dry, double wet)
        {
            const float dl = dryDelay_[0][(size_t) dryPos_], dr = dryDelay_[1][(size_t) dryPos_];
            dryDelay_[0][(size_t) dryPos_] = l[i];
            dryDelay_[1][(size_t) dryPos_] = r[i];
            dryPos_ = (dryPos_ + 1) % partition_;
            l[i] = (float) (dl * dry + wetL_[(size_t) i] * wet);
            r[i] = (float) (dr * dry + wetR_[(size_t) i] * wet);
        };
        if (! levels_.automated())
            for (int i = 0; i < n; ++i)
                mixOne (i, dry_, wet_);
        else
            levels_.forEach (position_, n, mixOne);
    }

private:
    juce::AudioBuffer<float> impulse_;
    int partition_;
    ReverbLevels levels_;
    BlockPosition position_;
    double dry_ = 0.75, wet_ = 0.25, lowCut_ = 120.0, highCut_ = 9000.0;
    StereoConvolver conv_;
    std::vector<Biquad> filters_;
    std::vector<float> mono_, wetL_, wetR_;
    std::vector<std::vector<float>> dryDelay_;
    int dryPos_ = 0;
};

class AlgorithmicReverb final : public Effect
{
public:
    AlgorithmicReverb (const juce::var& p, const LaneContext& ctx) : reverb_ (std::make_unique<dsp::Freeverb>()), levels_ (p, ctx)
    {
        const double mix = value (p, "reverb", "mix");
        params_.roomSize = (float) value (p, "reverb", "roomSize");
        params_.damping = (float) value (p, "reverb", "damping");
        params_.width = (float) value (p, "reverb", "width");
        params_.wetLevel = (float) (p.hasProperty ("wetDb") ? std::pow (10.0, param (p, "wetDb", 0.0) / 20.0) : mix);
        params_.dryLevel = (float) (p.hasProperty ("dryDb") ? std::pow (10.0, param (p, "dryDb", 0.0) / 20.0) : 1.0 - mix);
        if (levels_.automated())
        {
            // 곡선이면 잔향만 단위 크기(잔향 배율 3 을 1 로)로 내고 원음과 잔향의 크기는 따로 곱한다
            params_.wetLevel = 1.0f / 3.0f;
            params_.dryLevel = 0.0f;
        }
    }
    std::string type() const override { return "reverb"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return levels_.automated(); }
    void prepare (double fs, int maxBlock) override
    {
        if (levels_.automated())
        {
            reverb_->setParameters (params_);   // 곡선이면 처음부터 목표 크기 (번지지 않는다)
            dryL_.assign ((size_t) maxBlock, 0.0f);
            dryR_.assign ((size_t) maxBlock, 0.0f);
        }
        reverb_->setSampleRate (fs);
        reverb_->setParameters (params_);
        reverb_->reset();
    }
    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        if (! levels_.automated())
        {
            reverb_->processStereo (l, r, n);
            return;
        }
        if ((int) dryL_.size() < n)
        {
            dryL_.resize ((size_t) n);
            dryR_.resize ((size_t) n);
        }
        std::copy (l, l + n, dryL_.begin());
        std::copy (r, r + n, dryR_.begin());
        reverb_->processStereo (l, r, n);
        // 원음 배율 2 와 잔향 배율 3 을 고정값일 때와 같게 곱한다
        levels_.forEach (position_, n, [&] (int i, double dry, double wet)
        {
            l[i] = (float) (l[i] * 3.0 * wet + dryL_[(size_t) i] * 2.0 * dry);
            r[i] = (float) (r[i] * 3.0 * wet + dryR_[(size_t) i] * 2.0 * dry);
        });
    }
    // 앞 효과의 잔향 상태를 통째로 넘겨받고 새 매개변수를 준다
    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<AlgorithmicReverb*> (&previous))
        {
            std::swap (reverb_, p->reverb_);
            reverb_->setParameters (params_);
        }
    }

private:
    std::unique_ptr<dsp::Freeverb> reverb_;
    dsp::Freeverb::Parameters params_;
    ReverbLevels levels_;
    BlockPosition position_;
    std::vector<float> dryL_, dryR_;
};

// ---------------------------------------------------------------------------
// 필터: TPT 상태 변수 필터 (지연 없음). 12 dB/oct 는 한 단, 24 dB/oct 는 두 단이다.
// resonance 0..1 은 Q 0.7071..10 (지수): 0 이면 저역과 고역 통과가 버터워스(차단 주파수에서 -3 dB), 1 이면 12 dB 저역 통과의 봉우리가 +20 dB.
// 곡선이면 격자 점마다 계수 g = tan(pi fc / fs) 와 k = 1/Q 를 구하고 그 사이 샘플은 g 와 k 를 직선으로 잇는다.
// ---------------------------------------------------------------------------
enum class FilterMode { lowpass, highpass, bandpass, notch };

FilterMode filterModeOf (const std::string& name)
{
    if (name == "highpass") return FilterMode::highpass;
    if (name == "bandpass") return FilterMode::bandpass;
    if (name == "notch") return FilterMode::notch;
    return FilterMode::lowpass;
}

constexpr double kButterworthQ = 0.7071067811865476;

double resonanceQ (double resonance) { return kButterworthQ * std::pow (10.0 / kButterworthQ, std::clamp (resonance, 0.0, 1.0)); }

struct FilterCoefficients
{
    double g = 0.0, k1 = 1.0, k2 = 1.0, mix = 1.0;
};

// 단마다의 k: 24 dB 의 저역과 고역 통과는 버터워스 4차(Q 0.5412, 1.3066)에서 시작해 둘째 단의 Q 를 공진만큼 키운다
FilterCoefficients filterCoefficients (FilterMode mode, int slope, double cutoffHz, double resonance, double mix, double fs)
{
    FilterCoefficients c;
    const double fc = std::clamp (cutoffHz, 20.0, std::min (20000.0, 0.49 * fs));
    c.g = std::tan (pi * fc / fs);
    const double q = resonanceQ (resonance);
    if (slope >= 24 && (mode == FilterMode::lowpass || mode == FilterMode::highpass))
    {
        c.k1 = 1.0 / 0.5411961001461970;
        c.k2 = 1.0 / (1.3065629648763766 * q / kButterworthQ);
    }
    else
        c.k1 = c.k2 = 1.0 / q;
    c.mix = std::clamp (mix, 0.0, 1.0);
    return c;
}

// 아날로그 원형의 응답 (s = j w, w 는 차단 주파수로 나눈 쌍선형 변환 주파수)
std::complex<double> svfResponse (FilterMode mode, double w, double k)
{
    const std::complex<double> s (0.0, w);
    const auto den = s * s + k * s + 1.0;
    switch (mode)
    {
        case FilterMode::highpass: return s * s / den;
        case FilterMode::bandpass: return k * s / den;
        case FilterMode::notch: return (s * s + 1.0) / den;
        case FilterMode::lowpass:
        default: return 1.0 / den;
    }
}

class FilterEffect final : public Effect
{
public:
    FilterEffect (const juce::var& p, const LaneContext& ctx)
        : mode_ (filterModeOf (paramText (p, "mode", "lowpass"))), slope_ ((int) value (p, "filter", "slope") >= 24 ? 24 : 12),
          cutoff_ (makeParam (p, "filter", "cutoffHz", ctx)), resonance_ (makeParam (p, "filter", "resonance", ctx)),
          mix_ (makeParam (p, "filter", "mix", ctx))
    {
    }
    std::string type() const override { return "filter"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return cutoff_.automated() || resonance_.automated() || mix_.automated(); }

    void prepare (double fs, int) override
    {
        fs_ = fs;
        fixed_ = coefficientsAt (0);
        for (auto& st : state_)
            st = {};
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<FilterEffect*> (&previous))
            state_ = p->state_;
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        if (! cutoff_.automated() && ! resonance_.automated() && ! mix_.automated())
        {
            const auto stage1 = stageOf (fixed_.g, fixed_.k1), stage2 = stageOf (fixed_.g, fixed_.k2);
            for (int i = 0; i < n; ++i)
                filterSample (io, i, stage1, stage2, fixed_.k1, fixed_.k2, fixed_.mix);
            return;
        }
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const auto a = coefficientsAt (grid), b = coefficientsAt (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
            {
                const double f = gridFraction (position_, offset, j, grid);
                const double g = a.g + (b.g - a.g) * f, k1 = a.k1 + (b.k1 - a.k1) * f, k2 = a.k2 + (b.k2 - a.k2) * f;
                filterSample (io, offset + j, stageOf (g, k1), stageOf (g, k2), k1, k2, a.mix + (b.mix - a.mix) * f);
            }
        });
    }

private:
    struct Stage
    {
        double a1, a2, a3;
    };
    static Stage stageOf (double g, double k)
    {
        const double a1 = 1.0 / (1.0 + g * (g + k));
        return { a1, g * a1, g * g * a1 };
    }

    FilterCoefficients coefficientsAt (int64_t grid) const
    {
        return filterCoefficients (mode_, slope_, cutoff_.atGrid (grid), resonance_.atGrid (grid), mix_.atGrid (grid), fs_);
    }

    double runStage (int stage, int ch, const Stage& c, double k, double x)
    {
        double& ic1 = state_[(size_t) stage][(size_t) ch][0];
        double& ic2 = state_[(size_t) stage][(size_t) ch][1];
        const double v3 = x - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        switch (mode_)
        {
            case FilterMode::highpass: return x - k * v1 - v2;
            case FilterMode::bandpass: return k * v1;   // 가운데 주파수에서 0 dB
            case FilterMode::notch: return x - k * v1;
            case FilterMode::lowpass:
            default: return v2;
        }
    }

    void filterSample (float* const* io, int i, const Stage& s1, const Stage& s2, double k1, double k2, double mix)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const double x = io[ch][i];
            double y = runStage (0, ch, s1, k1, x);
            if (slope_ >= 24)
                y = runStage (1, ch, s2, k2, y);
            io[ch][i] = (float) (mix >= 1.0 ? y : y * mix + x * (1.0 - mix));
        }
    }

    FilterMode mode_;
    int slope_;
    Param cutoff_, resonance_, mix_;
    BlockPosition position_;
    double fs_ = 48000.0;
    FilterCoefficients fixed_;
    std::array<std::array<std::array<double, 2>, 2>, 2> state_ {};   // [단][채널][적분기]
};

// ---------------------------------------------------------------------------
// 박에 맞춘 덕킹: 키 트랙 없이 rateBeats 마다 크기를 depthDb 까지 내렸다 올린다. 크기는 곡 위치만의 함수라
// 위치를 옮겨도, 실시간에서도 같다. 한 주기: attackMs 동안 0 dB 에서 depthDb 로(dB 직선), holdMs 동안 머물고,
// releaseBeats 동안 (1 - r)^curve 의 몫으로 돌아온다. 앞 주기가 아직 돌아오는 중이면 더 깊은 쪽을 쓴다.
// ---------------------------------------------------------------------------
class DuckShape
{
public:
    DuckShape (const juce::var& p, std::shared_ptr<const TempoMap> tempo, double sampleRate)
        : tempo_ (std::move (tempo)), sampleRate_ (sampleRate), rate_ (value (p, "ducker", "rateBeats")),
          offset_ (value (p, "ducker", "offsetBeats")), attack_ (value (p, "ducker", "attackMs") * 0.001),
          hold_ (value (p, "ducker", "holdMs") * 0.001), release_ (value (p, "ducker", "releaseBeats")), curve_ (value (p, "ducker", "curve"))
    {
        for (auto& c : cache_)
            c.k = std::numeric_limits<int64_t>::min();
    }

    // 곡의 샘플에서 내리는 깊이의 몫 (0..1)
    double at (int64_t sample) const
    {
        const double t = (double) sample / sampleRate_;
        const double beat = tempo_->beatAt (t);
        const auto k = (int64_t) std::floor ((beat - offset_) / rate_);
        double shape = 0.0;
        for (int64_t j = 0; j < kPeriods; ++j)
        {
            const auto& p = period (k - j);
            if (t >= p.end)
                break;   // 더 앞의 주기는 더 일찍 끝났다
            double v = 0.0;
            if (t < p.start)
                v = 0.0;
            else if (t < p.start + attack_)
                v = (t - p.start) / attack_;
            else if (t < p.holdEnd)
                v = 1.0;
            else
            {
                const double r = (beat - p.holdEndBeat) / release_;
                v = r >= 1.0 ? 0.0 : (curve_ == 1.0 ? 1.0 - std::max (0.0, r) : std::pow (1.0 - std::max (0.0, r), curve_));
            }
            shape = std::max (shape, v);
        }
        return shape;
    }

private:
    struct Period
    {
        int64_t k = 0;
        double start = 0.0, holdEnd = 0.0, holdEndBeat = 0.0, end = 0.0;
    };

    const Period& period (int64_t k) const
    {
        auto& p = cache_[(size_t) (((k % kPeriods) + kPeriods) % kPeriods)];
        if (p.k != k)
        {
            p.k = k;
            p.start = tempo_->secondsAt (offset_ + (double) k * rate_);
            p.holdEnd = p.start + attack_ + hold_;
            p.holdEndBeat = tempo_->beatAt (p.holdEnd);
            p.end = tempo_->secondsAt (p.holdEndBeat + release_);
        }
        return p;
    }

    // 한 샘플에서 보는 주기 수의 한도. 캐시도 같은 크기라 겹친 주기끼리 서로 밀어내지 않는다.
    static constexpr int64_t kPeriods = 64;
    std::shared_ptr<const TempoMap> tempo_;
    double sampleRate_, rate_, offset_, attack_, hold_, release_, curve_;
    mutable std::array<Period, (size_t) kPeriods> cache_;
};

class DuckerEffect final : public Effect
{
public:
    DuckerEffect (const juce::var& p, const LaneContext& ctx)
        : shape_ (p, ctx.tempo, ctx.sampleRate), depthDb_ (makeParam (p, "ducker", "depthDb", ctx)), mix_ (makeParam (p, "ducker", "mix", ctx))
    {
    }
    std::string type() const override { return "ducker"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return true; }
    void prepare (double, int) override {}

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double d0 = depthDb_.atGrid (grid), d1 = depthDb_.atGrid (grid + kAutomationGrid);
            const double m0 = mix_.atGrid (grid), m1 = mix_.atGrid (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
            {
                const int i = offset + j;
                const double f = gridFraction (position_, offset, j, grid);
                const double depth = d0 + (d1 - d0) * f, mix = m0 + (m1 - m0) * f;
                const double shape = shape_.at (position_.at (i));
                const double g = shape > 0.0 ? dbToGain (depth * shape) : 1.0;
                const double w = 1.0 - mix + mix * g;
                l[i] = (float) (l[i] * w);
                r[i] = (float) (r[i] * w);
            }
        });
    }

private:
    DuckShape shape_;
    Param depthDb_, mix_;
    BlockPosition position_;
};

// ---------------------------------------------------------------------------
// 곡 위치만의 함수인 LFO: 곡의 샘플까지 지난 주기 수. rateBeats 가 있으면 템포 지도의 박으로, 없으면 rateHz 로 센다.
// 위치를 옮겨도, 실시간에서도 같은 자리는 같은 위상이다. 블록 안에서는 32 샘플 격자 점 사이를 직선으로 잇는다.
// ---------------------------------------------------------------------------
class PositionLfo
{
public:
    PositionLfo (const juce::var& p, const char* type, std::shared_ptr<const TempoMap> tempo, double sampleRate)
        : rateHz_ (value (p, type, "rateHz")), rateBeats_ (p.hasProperty ("rateBeats") ? value (p, type, "rateBeats") : 0.0),
          tempo_ (std::move (tempo)), fs_ (sampleRate)
    {
    }
    void setSampleRate (double fs) { fs_ = fs; }
    double cyclesAt (int64_t sample) const
    {
        const double t = (double) sample / fs_;
        return rateBeats_ > 0.0 ? tempo_->beatAt (t) / rateBeats_ : rateHz_ * t;
    }

private:
    double rateHz_, rateBeats_;
    std::shared_ptr<const TempoMap> tempo_;
    double fs_;
};

inline double fractionOf (double cycles) { return cycles - std::floor (cycles); }

// 지연선에서 delay 샘플 앞의 값 (4점 3차 에르미트). delay 는 3 이상이라 아직 쓰지 않은 칸(pos)을 읽지 않는다.
double readDelayLine (const std::vector<float>& line, int pos, double delay)
{
    const int length = (int) line.size();
    const double at = (double) pos - delay + (double) length;
    const int i1 = (int) std::floor (at);
    const double f = at - (double) i1;
    auto x = [&] (int i) { return (double) line[(size_t) (((i % length) + length) % length)]; };
    const double xm1 = x (i1 - 1), x0 = x (i1), x1 = x (i1 + 1), x2 = x (i1 + 2);
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}

// ---------------------------------------------------------------------------
// 코러스와 플랜저: LFO 가 지연 시간을 delayMs 에서 delayMs + depthMs 사이로 흔든다 (0.5 - 0.5 cos).
// 오른쪽 채널의 LFO 는 phaseDeg 만큼 앞선다. 되먹임은 지연음을 지연선으로 돌린다.
// ---------------------------------------------------------------------------
class ModDelayEffect final : public Effect
{
public:
    ModDelayEffect (const std::string& type, const juce::var& p, const LaneContext& ctx)
        : type_ (type), lfo_ (p, type.c_str(), ctx.tempo, ctx.sampleRate), delayMs_ (value (p, type.c_str(), "delayMs")),
          depthMs_ (makeParam (p, type.c_str(), "depthMs", ctx)), feedback_ (makeParam (p, type.c_str(), "feedback", ctx)),
          mix_ (makeParam (p, type.c_str(), "mix", ctx)), spread_ (value (p, type.c_str(), "phaseDeg") / 360.0)
    {
    }
    std::string type() const override { return type_; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return true; }

    void prepare (double fs, int) override
    {
        fs_ = fs;
        lfo_.setSampleRate (fs);
        const auto* depth = findEffectParameter (type_, "depthMs");
        const double longest = delayMs_ + (depth != nullptr ? depth->maximum : 20.0);
        const int length = (int) std::ceil (longest * 0.001 * fs) + 8;
        for (auto& line : lines_)
            line.assign ((size_t) length, 0.0f);
        pos_ = 0;
    }

    void continueFrom (Effect& previous) override
    {
        auto* p = dynamic_cast<ModDelayEffect*> (&previous);
        if (p == nullptr)
            return;
        const int length = (int) lines_[0].size(), previousLength = (int) p->lines_[0].size();
        const int count = std::min (length, previousLength);
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 1; k <= count; ++k)
                lines_[ch][(size_t) ((pos_ - k + length) % length)] = p->lines_[ch][(size_t) ((p->pos_ - k + previousLength) % previousLength)];
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        const int length = (int) lines_[0].size();
        const double toSamples = 0.001 * fs_;
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double c0 = lfo_.cyclesAt (grid), c1 = lfo_.cyclesAt (grid + kAutomationGrid);
            const double d0 = depthMs_.atGrid (grid), d1 = depthMs_.atGrid (grid + kAutomationGrid);
            const double f0 = feedback_.atGrid (grid), f1 = feedback_.atGrid (grid + kAutomationGrid);
            const double m0 = mix_.atGrid (grid), m1 = mix_.atGrid (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
            {
                const int i = offset + j;
                const double f = gridFraction (position_, offset, j, grid);
                const double cycles = c0 + (c1 - c0) * f, depth = d0 + (d1 - d0) * f;
                const double feedback = f0 + (f1 - f0) * f, mix = m0 + (m1 - m0) * f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double phase = fractionOf (cycles + (ch == 1 ? spread_ : 0.0));
                    const double sweep = 0.5 - 0.5 * std::cos (2.0 * pi * phase);
                    const double delay = std::clamp ((delayMs_ + depth * sweep) * toSamples, 3.0, (double) length - 4.0);
                    const double wet = readDelayLine (lines_[ch], pos_, delay);
                    const double x = io[ch][i];
                    lines_[ch][(size_t) pos_] = (float) (x + feedback * wet);
                    io[ch][i] = (float) (x * (1.0 - mix) + wet * mix);
                }
                pos_ = (pos_ + 1) % length;
            }
        });
    }

private:
    std::string type_;
    PositionLfo lfo_;
    double delayMs_;
    Param depthMs_, feedback_, mix_;
    double spread_;
    double fs_ = 48000.0;
    BlockPosition position_;
    std::vector<float> lines_[2];
    int pos_ = 0;
};

// ---------------------------------------------------------------------------
// 페이저: 1차 전역 통과 stages 단의 꺾임 주파수를 LFO 가 lowHz 와 highHz 사이에서 (로그로) 흔든다.
// 원음과 섞으면 위상이 반 바퀴 어긋난 주파수가 지워진다 (2단이면 꺾임 주파수에서). 되먹임은 마지막 단의 출력을 입력에 더한다.
// ---------------------------------------------------------------------------
class PhaserEffect final : public Effect
{
public:
    PhaserEffect (const juce::var& p, const LaneContext& ctx)
        : lfo_ (p, "phaser", ctx.tempo, ctx.sampleRate), lowHz_ (value (p, "phaser", "lowHz")), highHz_ (value (p, "phaser", "highHz")),
          stages_ (std::clamp ((int) value (p, "phaser", "stages"), 1, kMaxStages)), feedback_ (makeParam (p, "phaser", "feedback", ctx)),
          mix_ (makeParam (p, "phaser", "mix", ctx)), spread_ (value (p, "phaser", "phaseDeg") / 360.0)
    {
    }
    std::string type() const override { return "phaser"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return true; }

    void prepare (double fs, int) override
    {
        fs_ = fs;
        lfo_.setSampleRate (fs);
        state_ = {};
        last_ = {};
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<PhaserEffect*> (&previous))
        {
            state_ = p->state_;
            last_ = p->last_;
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double c0 = lfo_.cyclesAt (grid), c1 = lfo_.cyclesAt (grid + kAutomationGrid);
            double a0[2], a1[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const double shift = ch == 1 ? spread_ : 0.0;
                a0[ch] = coefficient (c0 + shift);
                a1[ch] = coefficient (c1 + shift);
            }
            const double f0 = feedback_.atGrid (grid), f1 = feedback_.atGrid (grid + kAutomationGrid);
            const double m0 = mix_.atGrid (grid), m1 = mix_.atGrid (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
            {
                const int i = offset + j;
                const double f = gridFraction (position_, offset, j, grid);
                const double feedback = f0 + (f1 - f0) * f, mix = m0 + (m1 - m0) * f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double a = a0[ch] + (a1[ch] - a0[ch]) * f;
                    const double x = io[ch][i];
                    double y = x + feedback * last_[(size_t) ch];
                    for (int s = 0; s < stages_; ++s)
                    {
                        double& z = state_[(size_t) ch][(size_t) s];
                        const double out = a * y + z;
                        z = y - a * out;
                        y = out;
                    }
                    last_[(size_t) ch] = y;
                    io[ch][i] = (float) (x * (1.0 - mix) + y * mix);
                }
            }
        });
    }

private:
    // 1차 전역 통과 (a + z^-1) / (1 + a z^-1) 의 계수: 꺾임 주파수에서 위상이 -90 도 (쌍선형, 미리 휜 주파수)
    double coefficient (double cycles) const
    {
        const double sweep = 0.5 - 0.5 * std::cos (2.0 * pi * fractionOf (cycles));
        const double fc = std::clamp (lowHz_ * std::pow (highHz_ / lowHz_, sweep), 10.0, fs_ * 0.45);
        const double t = std::tan (pi * fc / fs_);
        return (t - 1.0) / (t + 1.0);
    }

    static constexpr int kMaxStages = 12;
    PositionLfo lfo_;
    double lowHz_, highHz_;
    int stages_;
    Param feedback_, mix_;
    double spread_;
    double fs_ = 48000.0;
    BlockPosition position_;
    std::array<std::array<double, (size_t) kMaxStages>, 2> state_ {};
    std::array<double, 2> last_ {};
};

// 트레몰로의 모양 (화면 순서)
const std::vector<std::string>& tremoloShapeNames()
{
    static const std::vector<std::string> shapes { "sine", "triangle", "square" };
    return shapes;
}

// ---------------------------------------------------------------------------
// 트레몰로와 오토팬: 크기를 1 - depth * w(위상) 로 흔든다 (w 는 0..1, 위상 0 에서 0). 오른쪽은 phaseDeg 만큼 앞서서
// 180 이면 좌우가 번갈아 (사인과 삼각에서 두 채널 게인의 합이 늘 2 - depth) 오토팬이 된다. 상태가 없어 곡 위치만의 함수다.
// ---------------------------------------------------------------------------
class TremoloEffect final : public Effect
{
public:
    TremoloEffect (const juce::var& p, const LaneContext& ctx)
        : lfo_ (p, "tremolo", ctx.tempo, ctx.sampleRate), depth_ (makeParam (p, "tremolo", "depth", ctx)),
          spread_ (value (p, "tremolo", "phaseDeg") / 360.0)
    {
        const auto name = paramText (p, "shape", "sine");
        const auto& names = tremoloShapeNames();
        shape_ = (int) (std::find (names.begin(), names.end(), name) - names.begin());
        if (shape_ >= (int) names.size())
            shape_ = 0;
    }
    std::string type() const override { return "tremolo"; }
    void setPosition (BlockPosition s) override { position_ = s; }
    bool followsPosition() const override { return true; }
    void prepare (double fs, int) override { lfo_.setSampleRate (fs); }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* io[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        forEachGridStep (position_, n, [&] (int offset, int count, int64_t grid)
        {
            const double c0 = lfo_.cyclesAt (grid), c1 = lfo_.cyclesAt (grid + kAutomationGrid);
            const double d0 = depth_.atGrid (grid), d1 = depth_.atGrid (grid + kAutomationGrid);
            for (int j = 0; j < count; ++j)
            {
                const int i = offset + j;
                const double f = gridFraction (position_, offset, j, grid);
                const double cycles = c0 + (c1 - c0) * f, depth = d0 + (d1 - d0) * f;
                for (int ch = 0; ch < 2; ++ch)
                    io[ch][i] = (float) (io[ch][i] * (1.0 - depth * wave (cycles + (ch == 1 ? spread_ : 0.0))));
            }
        });
    }

private:
    double wave (double cycles) const
    {
        const double phase = fractionOf (cycles);
        switch (shape_)
        {
            case 1: return phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase;
            case 2:
            {
                // 모서리를 둥글린 사각 (튀는 소리가 나지 않게)
                constexpr double k = 8.0;
                return 0.5 - 0.5 * std::tanh (k * std::cos (2.0 * pi * phase)) / std::tanh (k);
            }
            default: return 0.5 - 0.5 * std::cos (2.0 * pi * phase);
        }
    }

    PositionLfo lfo_;
    Param depth_;
    double spread_;
    int shape_ = 0;
    BlockPosition position_;
};

// ---------------------------------------------------------------------------
// 노이즈 게이트: 두 채널의 봉우리(10 ms 로 내려오는 엔벨로프)가 thresholdDb 를 넘으면 열고, 3 dB 아래로 내려가
// holdMs 가 지나면 닫는다. 닫히면 rangeDb 까지 내린다. 여는 데 attackMs, 닫는 데 releaseMs (1극).
// ---------------------------------------------------------------------------
class GateEffect final : public Effect
{
public:
    explicit GateEffect (const juce::var& p)
        : thresholdDb_ (value (p, "gate", "thresholdDb")), rangeDb_ (value (p, "gate", "rangeDb")), attackMs_ (value (p, "gate", "attackMs")),
          holdMs_ (value (p, "gate", "holdMs")), releaseMs_ (value (p, "gate", "releaseMs"))
    {
    }
    std::string type() const override { return "gate"; }

    void prepare (double fs, int) override
    {
        open_ = dbToGain (thresholdDb_);
        close_ = dbToGain (thresholdDb_ - 3.0);
        floor_ = rangeDb_ <= -96.0 ? 0.0 : dbToGain (rangeDb_);
        envFall_ = std::exp (-1.0 / (0.010 * fs));
        attack_ = 1.0 - std::exp (-1.0 / (std::max (0.01, attackMs_) * 0.001 * fs));
        release_ = 1.0 - std::exp (-1.0 / (std::max (1.0, releaseMs_) * 0.001 * fs));
        holdSamples_ = (int) std::lround (holdMs_ * 0.001 * fs);
        env_ = 0.0;
        gain_ = floor_;
        isOpen_ = false;
        holdLeft_ = 0;
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<GateEffect*> (&previous))
        {
            env_ = p->env_;
            gain_ = p->gain_;
            isOpen_ = p->isOpen_;
            holdLeft_ = std::min (p->holdLeft_, holdSamples_);
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            env_ = std::max ((double) std::max (std::abs (l[i]), std::abs (r[i])), env_ * envFall_);
            if (env_ >= open_ || (isOpen_ && env_ >= close_))
            {
                isOpen_ = true;
                holdLeft_ = holdSamples_;
            }
            else if (isOpen_)
            {
                if (holdLeft_ > 0)
                    --holdLeft_;
                else
                    isOpen_ = false;
            }
            const double target = isOpen_ ? 1.0 : floor_;
            gain_ += (target - gain_) * (target > gain_ ? attack_ : release_);
            l[i] = (float) (l[i] * gain_);
            r[i] = (float) (r[i] * gain_);
        }
    }

private:
    double thresholdDb_, rangeDb_, attackMs_, holdMs_, releaseMs_;
    double open_ = 0.0, close_ = 0.0, floor_ = 0.0, envFall_ = 0.0, attack_ = 1.0, release_ = 1.0;
    int holdSamples_ = 0, holdLeft_ = 0;
    double env_ = 0.0, gain_ = 0.0;
    bool isOpen_ = false;
};

// 링크위츠-라일리 4차 (버터워스 2차 두 단) 나누기: 낮은 쪽과 높은 쪽을 더하면 크기가 평평한 올패스다
struct Crossover
{
    Biquad low[2], high[2];
    void set (double fs, double f)
    {
        for (auto& b : low)
            b = Biquad::make ("lowpass", fs, f, 0.7071068);
        for (auto& b : high)
            b = Biquad::make ("highpass", fs, f, 0.7071068);
    }
    void split (int ch, float x, float& lo, float& hi)
    {
        lo = low[1].process (ch, low[0].process (ch, x));
        hi = high[1].process (ch, high[0].process (ch, x));
    }
};

// 한 대역의 압축: 링크한 피크 검출, 니가 있는 게인 계산, dB 에서 어택과 릴리스
struct BandCompressor
{
    double threshold = -20.0, ratio = 2.0, knee = 6.0, makeup = 0.0, attack = 0.0, release = 0.0, gr = 0.0;
    double gain (double peak)
    {
        const double level = 20.0 * std::log10 (peak + 1.0e-12), over = level - threshold;
        double out = level;
        if (knee > 0.0 && 2.0 * std::abs (over) <= knee)
            out = level + (1.0 / ratio - 1.0) * (over + knee / 2.0) * (over + knee / 2.0) / (2.0 * knee);
        else if (2.0 * over > knee)
            out = threshold + over / ratio;
        const double target = out - level;
        const double coef = target < gr ? attack : release;
        gr = coef * gr + (1.0 - coef) * target;
        return std::pow (10.0, (gr + makeup) / 20.0);
    }
};

// 세 대역 압축: lowHz 와 highHz 에서 나눈 대역마다 압축하고 게인을 더해 합친다. 낮은 대역은 둘째 나누기의 올패스를 지나 위상이 맞는다
class MultibandEffect final : public Effect
{
public:
    explicit MultibandEffect (const juce::var& p)
        : lowHz_ (value (p, "multiband", "lowHz")), highHz_ (value (p, "multiband", "highHz")),
          attackMs_ (value (p, "multiband", "attackMs")), releaseMs_ (value (p, "multiband", "releaseMs"))
    {
        const char* names[3][3] = { { "lowThresholdDb", "lowRatio", "lowGainDb" }, { "midThresholdDb", "midRatio", "midGainDb" },
                                    { "highThresholdDb", "highRatio", "highGainDb" } };
        const double knee = value (p, "multiband", "kneeDb");
        for (int b = 0; b < 3; ++b)
        {
            bands_[b].threshold = value (p, "multiband", names[b][0]);
            bands_[b].ratio = value (p, "multiband", names[b][1]);
            bands_[b].makeup = value (p, "multiband", names[b][2]);
            bands_[b].knee = knee;
        }
    }
    std::string type() const override { return "multiband"; }

    void prepare (double fs, int) override
    {
        first_.set (fs, lowHz_);
        second_.set (fs, std::max (highHz_, lowHz_ * 1.01));
        align_.set (fs, std::max (highHz_, lowHz_ * 1.01));
        for (auto& b : bands_)
        {
            b.attack = std::exp (-1.0 / (attackMs_ * 0.001 * fs));
            b.release = std::exp (-1.0 / (releaseMs_ * 0.001 * fs));
            b.gr = 0.0;
        }
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<MultibandEffect*> (&previous); p != nullptr && p->lowHz_ == lowHz_ && p->highHz_ == highHz_)
        {
            first_ = p->first_;
            second_ = p->second_;
            align_ = p->align_;
            for (int b = 0; b < 3; ++b)
                bands_[b].gr = p->bands_[b].gr;
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float band[3][2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = ch == 0 ? l[i] : r[i];
                float low, rest, alo, ahi;
                first_.split (ch, x, low, rest);
                second_.split (ch, rest, band[1][ch], band[2][ch]);
                align_.split (ch, low, alo, ahi);
                band[0][ch] = alo + ahi;
            }
            float out[2] = { 0.0f, 0.0f };
            for (int b = 0; b < 3; ++b)
            {
                const double g = bands_[b].gain (std::max (std::abs ((double) band[b][0]), std::abs ((double) band[b][1])));
                out[0] += (float) (band[b][0] * g);
                out[1] += (float) (band[b][1] * g);
            }
            l[i] = out[0];
            r[i] = out[1];
        }
    }

private:
    double lowHz_, highHz_, attackMs_, releaseMs_;
    Crossover first_, second_, align_;
    BandCompressor bands_[3];
};

// 트랜지언트: 5 ms RMS 를 따르는 빠른 엔벨로프와 느린 엔벨로프의 차이로 어택(소리가 차오르는 자리)과 서스테인(잦아드는 자리)을 찾아
// 각각 키우거나 줄인다 (차이 1~12 dB 에서 0 에서 끝까지)
class TransientEffect final : public Effect
{
public:
    explicit TransientEffect (const juce::var& p)
        : attackDb_ (value (p, "transient", "attackDb")), sustainDb_ (value (p, "transient", "sustainDb")), outputDb_ (value (p, "transient", "outputDb"))
    {
    }
    std::string type() const override { return "transient"; }

    void prepare (double fs, int) override
    {
        auto coef = [fs] (double ms) { return std::exp (-1.0 / (ms * 0.001 * fs)); };
        power_ = coef (5.0);
        fastAttack_ = coef (1.0);
        slowAttack_ = coef (25.0);
        shortRelease_ = coef (20.0);
        longRelease_ = coef (300.0);
        for (auto& e : env_)
            e = 0.0;
        level_ = 0.0;
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<TransientEffect*> (&previous))
        {
            std::copy (std::begin (p->env_), std::end (p->env_), std::begin (env_));
            level_ = p->level_;
        }
    }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        auto follow = [] (double& e, double x, double attack, double release) { e = x > e ? attack * e + (1.0 - attack) * x : release * e + (1.0 - release) * x; };
        auto db = [] (double v) { return 20.0 * std::log10 (v + 1.0e-9); };
        for (int i = 0; i < n; ++i)
        {
            // 5 ms 의 RMS 를 따라간다 (피크를 따르면 잡음과 낮은 음의 물결이 어택으로 보인다)
            level_ = power_ * level_ + (1.0 - power_) * 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
            const double x = std::sqrt (level_);
            follow (env_[0], x, fastAttack_, shortRelease_);  // 어택: 빠르게 차는 것
            follow (env_[1], x, slowAttack_, shortRelease_);  //       천천히 차는 것 (잦아들 때는 둘 다 소리를 따라 만난다)
            follow (env_[2], x, fastAttack_, shortRelease_);  // 서스테인: 빨리 잦아드는 것
            follow (env_[3], x, fastAttack_, longRelease_);   //          천천히 잦아드는 것
            // 차이가 1 dB 안이면 고른 소리로 본다
            const double attack = std::clamp ((db (env_[0]) - db (env_[1]) - 1.0) / 11.0, 0.0, 1.0);
            const double sustain = std::clamp ((db (env_[3]) - db (env_[2]) - 1.0) / 11.0, 0.0, 1.0);
            const double g = std::pow (10.0, (attackDb_ * attack + sustainDb_ * sustain + outputDb_) / 20.0);
            l[i] = (float) (l[i] * g);
            r[i] = (float) (r[i] * g);
        }
    }

private:
    double attackDb_, sustainDb_, outputDb_;
    double power_ = 0.0, fastAttack_ = 0.0, slowAttack_ = 0.0, shortRelease_ = 0.0, longRelease_ = 0.0;
    double env_[4] = { 0.0, 0.0, 0.0, 0.0 };
    double level_ = 0.0;
};

// 디에서: frequencyHz 위 대역의 피크(10 ms 로 내려온다)가 스레숄드를 넘으면 그 대역만 rangeDb 까지 줄인다 (비율 4, 어택 0.5 ms, 릴리스 60 ms)
class DeesserEffect final : public Effect
{
public:
    explicit DeesserEffect (const juce::var& p)
        : frequencyHz_ (value (p, "deesser", "frequencyHz")), thresholdDb_ (value (p, "deesser", "thresholdDb")), rangeDb_ (value (p, "deesser", "rangeDb"))
    {
    }
    std::string type() const override { return "deesser"; }

    void prepare (double fs, int) override
    {
        split_.set (fs, frequencyHz_);
        attack_ = std::exp (-1.0 / (0.5 * 0.001 * fs));
        release_ = std::exp (-1.0 / (60.0 * 0.001 * fs));
        peakFall_ = std::exp (-1.0 / (10.0 * 0.001 * fs));
        gr_ = peak_ = 0.0;
    }

    void continueFrom (Effect& previous) override
    {
        if (auto* p = dynamic_cast<DeesserEffect*> (&previous); p != nullptr && p->frequencyHz_ == frequencyHz_)
        {
            split_ = p->split_;
            gr_ = p->gr_;
            peak_ = p->peak_;
        }
    }

    double reductionDb() const noexcept { return gr_; }

    void process (juce::AudioBuffer<float>& buffer, int n) override
    {
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float lo[2], hi[2];
            split_.split (0, l[i], lo[0], hi[0]);
            split_.split (1, r[i], lo[1], hi[1]);
            peak_ = std::max (std::max (std::abs ((double) hi[0]), std::abs ((double) hi[1])), peak_ * peakFall_);   // 10 ms 로 내려오는 피크
            const double level = 20.0 * std::log10 (peak_ + 1.0e-12);
            const double target = -std::min (rangeDb_, std::max (0.0, (level - thresholdDb_) * 0.75));
            gr_ = (target < gr_ ? attack_ : release_) * gr_ + (1.0 - (target < gr_ ? attack_ : release_)) * target;
            const float g = (float) std::pow (10.0, gr_ / 20.0);
            l[i] = lo[0] + hi[0] * g;
            r[i] = lo[1] + hi[1] * g;
        }
    }

private:
    double frequencyHz_, thresholdDb_, rangeDb_;
    Crossover split_;
    double attack_ = 0.0, release_ = 0.0, gr_ = 0.0, peakFall_ = 0.0, peak_ = 0.0;
};

// 숫자 매개변수 말고 받는 키
const std::map<std::string, std::vector<std::string>>& otherKeys()
{
    static const std::map<std::string, std::vector<std::string>> keys {
        { "eq", { "bands" } }, { "compressor", { "sidechain" } }, { "reverb", { "mode", "ir" } }, { "filter", { "mode" } },
        { "tremolo", { "shape" } },
    };
    return keys;
}

} // namespace

const std::vector<std::string>& effectTypes()
{
    static const std::vector<std::string> types { "eq", "compressor", "reverb", "delay", "distortion", "stereo", "gain", "limiter",
                                                  "filter", "ducker", "chorus", "flanger", "phaser", "tremolo", "gate", "multiband", "transient",
                                                  "deesser" };
    return types;
}

const std::vector<std::string>& tremoloShapes()
{
    return tremoloShapeNames();
}

const std::vector<std::string>& filterModes()
{
    static const std::vector<std::string> modes { "lowpass", "highpass", "bandpass", "notch" };
    return modes;
}

const std::vector<EffectParameter>& effectParameters (const std::string& type)
{
    using P = EffectParameter;
    auto number = [] (const char* key, double lo, double hi, double def, const char* unit, bool log = false)
    {
        P p;
        p.key = key;
        p.minimum = lo;
        p.maximum = hi;
        p.defaultValue = def;
        p.unit = unit;
        p.logarithmic = log;
        return p;
    };
    auto toggle = [&number] (const char* key)
    {
        auto p = number (key, 0, 1, 0, "");
        p.toggle = true;
        return p;
    };
    auto optional = [] (P p)
    {
        p.optional = true;
        return p;
    };
    auto choices = [] (P p, std::vector<double> values)
    {
        p.choices = std::move (values);
        return p;
    };
    auto inMode = [] (P p, const char* mode)
    {
        p.mode = mode;
        return p;
    };
    auto hidden = [] (P p)
    {
        p.hidden = true;
        return p;
    };
    auto automatable = [] (P p)
    {
        p.automatable = true;
        return p;
    };
    static const std::map<std::string, std::vector<P>> table {
        { "eq", { optional (number ("lowCutHz", 10, 4000, 80, "Hz", true)), choices (number ("lowCutSlope", 12, 24, 12, "dB/oct"), { 12, 24 }),
                  optional (number ("highCutHz", 500, 24000, 16000, "Hz", true)), choices (number ("highCutSlope", 12, 24, 12, "dB/oct"), { 12, 24 }) } },
        { "compressor", { number ("thresholdDb", -80, 0, -18, "dB"), number ("ratio", 1, 50, 3, "ratio", true),
                          number ("attackMs", 0.05, 500, 10, "ms", true), number ("releaseMs", 1, 5000, 120, "ms", true),
                          number ("kneeDb", 0, 24, 6, "dB"), number ("makeupDb", -24, 36, 0, "dB"), number ("mix", 0, 1, 1, "percent") } },
        { "reverb", { automatable (number ("mix", 0, 1, 0.25, "percent")), inMode (number ("decaySeconds", 0.1, 12, 2.2, "s", true), "convolution"),
                      inMode (number ("preDelayMs", 0, 250, 20, "ms"), "convolution"), inMode (number ("roomSize", 0, 1, 0.7, "percent"), "algorithmic"),
                      number ("damping", 0, 1, 0.5, "percent"), number ("width", 0, 1, 1, "percent"),
                      inMode (number ("lowCutHz", 10, 2000, 120, "Hz", true), "convolution"),
                      inMode (number ("highCutHz", 1000, 24000, 9000, "Hz", true), "convolution"),
                      automatable (optional (number ("dryDb", -120, 12, 0, "dB"))), automatable (optional (number ("wetDb", -120, 12, 0, "dB"))),
                      hidden (optional (number ("seed", 0, 9.0e15, 0, ""))) } },
        { "delay", { number ("timeMs", 1, 4000, 375, "ms", true), optional (number ("timeBeats", 0.015625, 16, 0.75, "beats", true)),
                     automatable (number ("feedback", 0, 0.95, 0.35, "percent")), automatable (number ("mix", 0, 1, 0.3, "percent")),
                     toggle ("pingPong"), number ("lowCutHz", 10, 4000, 150, "Hz", true), number ("highCutHz", 500, 24000, 7000, "Hz", true) } },
        { "distortion", { automatable (number ("driveDb", 0, 48, 12, "dB")), number ("toneHz", 200, 20000, 8000, "Hz", true),
                          automatable (number ("mix", 0, 1, 1, "percent")), number ("outputDb", -48, 12, 0, "dB") } },
        { "stereo", { automatable (number ("width", 0, 2, 1, "percent")), number ("pan", -1, 1, 0, "pan") } },
        { "gain", { automatable (number ("gainDb", -96, 36, 0, "dB")) } },
        { "limiter", { number ("ceilingDb", -24, 0, -1, "dB"), number ("releaseMs", 1, 2000, 60, "ms", true),
                       number ("lookaheadMs", 0.1, 50, 5, "ms", true), toggle ("truePeak") } },
        { "filter", { automatable (number ("cutoffHz", 20, 20000, 1000, "Hz", true)), automatable (number ("resonance", 0, 1, 0, "percent")),
                      choices (number ("slope", 12, 24, 12, "dB/oct"), { 12, 24 }), automatable (number ("mix", 0, 1, 1, "percent")) } },
        { "ducker", { number ("rateBeats", 0.0625, 16, 1, "beats", true), automatable (number ("depthDb", -60, 0, -12, "dB")),
                      number ("attackMs", 0, 200, 1, "ms"), number ("holdMs", 0, 2000, 30, "ms"),
                      number ("releaseBeats", 0.01, 16, 0.5, "beats", true), number ("curve", 0.25, 4, 1, "", true),
                      number ("offsetBeats", 0, 16, 0, "beats"), automatable (number ("mix", 0, 1, 1, "percent")) } },
        { "chorus", { number ("rateHz", 0.01, 20, 0.8, "Hz", true), optional (number ("rateBeats", 0.0625, 64, 4, "beats", true)),
                      automatable (number ("depthMs", 0, 20, 3, "ms")), number ("delayMs", 0.5, 40, 12, "ms", true),
                      automatable (number ("feedback", -0.95, 0.95, 0, "percent")), automatable (number ("mix", 0, 1, 0.5, "percent")),
                      number ("phaseDeg", 0, 180, 90, "degrees") } },
        { "flanger", { number ("rateHz", 0.01, 20, 0.2, "Hz", true), optional (number ("rateBeats", 0.0625, 64, 8, "beats", true)),
                       automatable (number ("depthMs", 0, 10, 2, "ms")), number ("delayMs", 0.1, 20, 1, "ms", true),
                       automatable (number ("feedback", -0.95, 0.95, 0.6, "percent")), automatable (number ("mix", 0, 1, 0.5, "percent")),
                       number ("phaseDeg", 0, 180, 0, "degrees") } },
        { "phaser", { number ("rateHz", 0.01, 20, 0.4, "Hz", true), optional (number ("rateBeats", 0.0625, 64, 4, "beats", true)),
                      number ("lowHz", 20, 20000, 200, "Hz", true), number ("highHz", 20, 20000, 2500, "Hz", true),
                      choices (number ("stages", 2, 12, 6, "stages"), { 2, 4, 6, 8, 10, 12 }),
                      automatable (number ("feedback", 0, 0.95, 0.4, "percent")), automatable (number ("mix", 0, 1, 0.5, "percent")),
                      number ("phaseDeg", 0, 180, 90, "degrees") } },
        { "tremolo", { number ("rateHz", 0.01, 40, 4, "Hz", true), optional (number ("rateBeats", 0.0625, 64, 0.5, "beats", true)),
                       automatable (number ("depth", 0, 1, 0.5, "percent")), number ("phaseDeg", 0, 180, 0, "degrees") } },
        { "gate", { number ("thresholdDb", -96, 0, -40, "dB"), number ("rangeDb", -96, 0, -80, "dB"), number ("attackMs", 0.01, 100, 0.5, "ms", true),
                    number ("holdMs", 0, 2000, 50, "ms"), number ("releaseMs", 1, 5000, 120, "ms", true) } },
        { "multiband", { number ("lowHz", 40, 1000, 200, "Hz", true), number ("highHz", 400, 16000, 2500, "Hz", true),
                         number ("lowThresholdDb", -60, 0, -20, "dB"), number ("lowRatio", 1, 20, 2, "ratio", true), number ("lowGainDb", -24, 24, 0, "dB"),
                         number ("midThresholdDb", -60, 0, -20, "dB"), number ("midRatio", 1, 20, 2, "ratio", true), number ("midGainDb", -24, 24, 0, "dB"),
                         number ("highThresholdDb", -60, 0, -20, "dB"), number ("highRatio", 1, 20, 2, "ratio", true), number ("highGainDb", -24, 24, 0, "dB"),
                         number ("attackMs", 0.1, 200, 10, "ms", true), number ("releaseMs", 5, 2000, 150, "ms", true), number ("kneeDb", 0, 24, 6, "dB") } },
        { "transient", { number ("attackDb", -24, 24, 0, "dB"), number ("sustainDb", -24, 24, 0, "dB"), number ("outputDb", -24, 24, 0, "dB") } },
        { "deesser", { number ("frequencyHz", 2000, 16000, 6000, "Hz", true), number ("thresholdDb", -60, 0, -30, "dB"), number ("rangeDb", 0, 24, 8, "dB") } },
    };
    static const std::vector<P> none;
    const auto it = table.find (type);
    return it != table.end() ? it->second : none;
}

const EffectParameter* findEffectParameter (const std::string& type, const std::string& key)
{
    for (const auto& p : effectParameters (type))
        if (p.key == key)
            return &p;
    return nullptr;
}

std::vector<double> eqResponseDb (const juce::var& params, double sampleRate, const std::vector<double>& frequenciesHz)
{
    std::vector<Biquad> filters;
    std::vector<int> roles;
    buildEqFilters (params, sampleRate, filters, roles);
    std::vector<double> out;
    out.reserve (frequenciesHz.size());
    for (const double f : frequenciesHz)
    {
        const double w = 2.0 * pi * std::clamp (f, 1.0, sampleRate * 0.5) / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
        double db = 0.0;
        for (const auto& b : filters)
        {
            const auto h = (b.b0 + b.b1 * z1 + b.b2 * z2) / (1.0 + b.a1 * z1 + b.a2 * z2);
            db += 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
        }
        out.push_back (db);
    }
    return out;
}

void validateEffectSpec (const EffectSpec& spec, Diagnostics& d)
{
    if (spec.type == "plugin")
    {
        validatePluginEffect (spec, d);
        return;
    }
    const auto& types = effectTypes();
    if (std::find (types.begin(), types.end(), spec.type) == types.end())
    {
        const auto s = closestMatch (spec.type, types);
        d.error (spec.path + ".type", "unknown effect '" + spec.type + "'",
                 s.empty() ? "use eq, compressor, reverb, limiter, gain, stereo, delay, distortion, filter, ducker, chorus, flanger, phaser, tremolo or gate"
                           : "did you mean '" + s + "'?");
        return;
    }
    json::ObjectReader r (spec.params, spec.path, d);
    r.known ("type");
    r.known ("comment");
    r.boolean ("bypass", false);
    std::vector<std::string> known { "type", "comment", "bypass" };
    for (const auto& p : effectParameters (spec.type))
    {
        r.number (p.key, false, p.minimum, p.maximum);
        known.push_back (p.key);
    }
    if (auto it = otherKeys().find (spec.type); it != otherKeys().end())
        for (const auto& key : it->second)
            known.push_back (key);
    if (spec.type == "eq")
    {
        for (const char* slope : { "lowCutSlope", "highCutSlope" })
            if (auto v = spec.params.getProperty (slope, juce::var()); json::isNumber (v) && (int) v != 12 && (int) v != 24)
                d.error (r.pathOf (slope), "slope must be 12 or 24 (dB per octave)");
        if (auto* bands = r.array ("bands", false))
            for (int i = 0; i < bands->size(); ++i)
            {
                json::ObjectReader b ((*bands)[i], json::indexPath (r.pathOf ("bands"), (size_t) i), d);
                if (auto t = b.string ("type", false); t && *t != "peak" && *t != "lowShelf" && *t != "highShelf")
                    d.error (b.pathOf ("type"), "band type must be peak, lowShelf or highShelf");
                b.number ("freqHz", true, 10.0, 24000.0);
                b.number ("gainDb", false, -36.0, 36.0);
                b.number ("q", false, 0.05, 20.0);
                b.warnUnknown ({ "type", "freqHz", "gainDb", "q" });
            }
    }
    if (spec.type == "compressor")
        if (auto source = r.string ("sidechain", false); source && source->empty())
            d.error (r.pathOf ("sidechain"), "sidechain needs a track name");
    if (spec.type == "reverb")
    {
        if (auto mode = r.string ("mode", false); mode && *mode != "convolution" && *mode != "algorithmic")
            d.error (r.pathOf ("mode"), "reverb mode must be convolution or algorithmic");
        r.string ("ir", false);
    }
    if (spec.type == "filter")
    {
        const auto& modes = filterModes();
        if (auto mode = r.string ("mode", false); mode && std::find (modes.begin(), modes.end(), *mode) == modes.end())
            d.error (r.pathOf ("mode"), "filter mode must be lowpass, highpass, bandpass or notch");
        if (auto v = spec.params.getProperty ("slope", juce::var()); json::isNumber (v) && (double) v != 12.0 && (double) v != 24.0)
            d.error (r.pathOf ("slope"), "slope must be 12 or 24 (dB per octave)");
    }
    if (spec.type == "tremolo")
    {
        const auto& shapes = tremoloShapes();
        if (auto shape = r.string ("shape", false); shape && std::find (shapes.begin(), shapes.end(), *shape) == shapes.end())
            d.error (r.pathOf ("shape"), "tremolo shape must be sine, triangle or square");
    }
    if (spec.type == "multiband")
    {
        const double lo = param (spec.params, "lowHz", 200.0), hi = param (spec.params, "highHz", 2500.0);
        if (hi < lo * 2.0)
            d.error (r.pathOf ("highHz"), "highHz must be at least twice lowHz (the middle band needs an octave)");
    }
    if (spec.type == "phaser")
        if (auto v = spec.params.getProperty ("stages", juce::var()); json::isNumber (v) && ((int) (double) v != (double) v || (int) (double) v % 2 != 0))
            d.error (r.pathOf ("stages"), "stages must be 2, 4, 6, 8, 10 or 12");
    if (spec.params.hasProperty ("rateBeats") && spec.params.hasProperty ("rateHz")
        && (spec.type == "chorus" || spec.type == "flanger" || spec.type == "phaser" || spec.type == "tremolo"))
        d.warning (r.pathOf ("rateHz"), "rateHz is ignored because rateBeats is set", "rateBeats follows the tempo map; remove one of them");
    if (spec.type == "delay" && spec.params.hasProperty ("timeBeats") && spec.params.hasProperty ("timeMs"))
        d.warning (r.pathOf ("timeMs"), "timeMs is ignored because timeBeats is set", "timeBeats follows the tempo map; remove one of them");
    if (const auto* lanes = r.object ("automation", false))
    {
        json::ObjectReader ar (*lanes, r.pathOf ("automation"), d);
        std::vector<std::string> automatable;
        std::vector<std::string> parameters;
        for (const auto& p : effectParameters (spec.type))
        {
            parameters.push_back (p.key);
            if (p.automatable)
                automatable.push_back (p.key);
        }
        std::string list;
        for (const auto& key : automatable)
            list += (list.empty() ? "" : ", ") + key;
        const std::string hint = automatable.empty() ? spec.type + " has no automatable parameters" : "automatable: " + list;
        for (const auto& key : ar.keys())
        {
            const auto* info = findEffectParameter (spec.type, key);
            const auto* lane = ar.get (key);
            if (info == nullptr)
            {
                const auto other = otherKeys().find (spec.type);
                if (other != otherKeys().end() && std::find (other->second.begin(), other->second.end(), key) != other->second.end())
                    d.error (ar.pathOf (key), "'" + key + "' cannot be automated", hint);
                else
                {
                    const auto close = closestMatch (key, parameters);
                    d.error (ar.pathOf (key), "unknown parameter '" + key + "' for " + spec.type, close.empty() ? hint : "did you mean '" + close + "'?");
                }
                continue;
            }
            if (! info->automatable)
            {
                d.error (ar.pathOf (key), "'" + key + "' cannot be automated", hint);
                continue;
            }
            readAutomationLane (*lane, ar.pathOf (key), info->minimum, info->maximum, d);
        }
    }
    known.push_back ("automation");
    r.warnUnknown (known);
}

std::map<std::string, AutomationLane> effectAutomation (const juce::var& params)
{
    std::map<std::string, AutomationLane> lanes;
    const auto automation = params.getProperty ("automation", juce::var());
    const auto* obj = automation.getDynamicObject();
    if (obj == nullptr)
        return lanes;
    const auto type = params.getProperty ("type", "").toString().toStdString();
    for (const auto& prop : obj->getProperties())
    {
        const auto key = prop.name.toString().toStdString();
        const auto* info = findEffectParameter (type, key);
        if (info == nullptr || ! info->automatable)
            continue;
        Diagnostics ignored;
        auto lane = readAutomationLane (prop.value, "$", info->minimum, info->maximum, ignored);
        if (! lane.empty())
            lanes[key] = std::move (lane);
    }
    return lanes;
}

std::unique_ptr<Effect> createEffect (const EffectSpec& spec, const juce::File& baseDir, double sampleRate, int blockSize, const TempoMap* tempo,
                                      bool realtime)
{
    std::unique_ptr<Effect> e;
    if (spec.type == "plugin")
    {
        e = makePluginEffect (spec, baseDir, sampleRate, blockSize, tempo, realtime);
        e->prepare (sampleRate, blockSize);
        return e;
    }
    const auto& p = spec.params;
    LaneContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.lanes = effectAutomation (p);
    const bool timeBeats = spec.type == "delay" && p.hasProperty ("timeBeats");
    const bool modulation = spec.type == "chorus" || spec.type == "flanger" || spec.type == "phaser" || spec.type == "tremolo";
    if (! ctx.lanes.empty() || spec.type == "ducker" || timeBeats || modulation)
        ctx.tempo = tempo != nullptr ? std::make_shared<const TempoMap> (*tempo) : std::make_shared<const TempoMap> (std::vector<TempoPoint> {});
    if (timeBeats)
    {
        // 지연선은 가장 느린 템포에서 필요한 길이로 미리 잡는다
        const double seconds = DelayEffect::longestSeconds (value (p, "delay", "timeBeats"), *ctx.tempo);
        if (seconds > 16.0)
            throw Failure ("invalid-input", spec.path + ".timeBeats: the echo would be " + formatNumber (seconds, 1)
                                                + " s long at the slowest tempo (at most 16 s)");
    }
    if (spec.type == "eq")
        e = std::make_unique<EqEffect> (p);
    else if (spec.type == "compressor")
        e = std::make_unique<CompressorEffect> (p);
    else if (spec.type == "limiter")
        e = std::make_unique<LimiterEffect> (p);
    else if (spec.type == "gain")
        e = std::make_unique<GainEffect> (p, ctx);
    else if (spec.type == "stereo")
        e = std::make_unique<StereoEffect> (p, ctx);
    else if (spec.type == "delay")
        e = std::make_unique<DelayEffect> (p, ctx);
    else if (spec.type == "distortion")
        e = std::make_unique<DistortionEffect> (p, ctx);
    else if (spec.type == "filter")
        e = std::make_unique<FilterEffect> (p, ctx);
    else if (spec.type == "ducker")
        e = std::make_unique<DuckerEffect> (p, ctx);
    else if (spec.type == "chorus" || spec.type == "flanger")
        e = std::make_unique<ModDelayEffect> (spec.type, p, ctx);
    else if (spec.type == "phaser")
        e = std::make_unique<PhaserEffect> (p, ctx);
    else if (spec.type == "tremolo")
        e = std::make_unique<TremoloEffect> (p, ctx);
    else if (spec.type == "gate")
        e = std::make_unique<GateEffect> (p);
    else if (spec.type == "multiband")
        e = std::make_unique<MultibandEffect> (p);
    else if (spec.type == "transient")
        e = std::make_unique<TransientEffect> (p);
    else if (spec.type == "deesser")
        e = std::make_unique<DeesserEffect> (p);
    else if (spec.type == "reverb")
    {
        if (paramText (p, "mode", "convolution") == "algorithmic")
            e = std::make_unique<AlgorithmicReverb> (p, ctx);
        else
        {
            juce::AudioBuffer<float> impulse;
            const auto irPath = paramText (p, "ir", "");
            if (! irPath.empty())
            {
                const auto file = baseDir.getChildFile (juce::String::fromUTF8 (irPath.c_str()));
                auto data = readAudioFile (file);
                if (std::abs (data.sampleRate - sampleRate) > 1.0)
                    throw Failure ("invalid-input", spec.path + ".ir: impulse response is " + std::to_string ((int) data.sampleRate)
                                   + " Hz but the render is " + std::to_string ((int) sampleRate) + " Hz");
                impulse.setSize (2, data.buffer.getNumSamples());
                impulse.copyFrom (0, 0, data.buffer, 0, 0, data.buffer.getNumSamples());
                impulse.copyFrom (1, 0, data.buffer, std::min (1, data.buffer.getNumChannels() - 1), 0, data.buffer.getNumSamples());
            }
            else
            {
                const uint64_t seed = p.hasProperty ("seed") ? (uint64_t) param (p, "seed", 0.0) : hashString (spec.path);
                impulse = generateHallImpulse (sampleRate, value (p, "reverb", "decaySeconds"), value (p, "reverb", "preDelayMs"),
                                               value (p, "reverb", "damping"), value (p, "reverb", "width"), seed);
            }
            e = std::make_unique<ConvolutionReverb> (std::move (impulse), p, 1024, ctx);
        }
    }
    else
        throw Failure ("invalid-input", spec.path + ": unknown effect '" + spec.type + "'");
    e->prepare (sampleRate, blockSize);
    return e;
}

EffectChain createEffectChain (const std::vector<EffectSpec>& specs, const juce::File& baseDir, double sampleRate, int blockSize,
                               const TempoMap* tempo, bool realtime)
{
    EffectChain chain;
    for (const auto& s : specs)
        chain.add (createEffect (s, baseDir, sampleRate, blockSize, tempo, realtime), s.bypass);
    return chain;
}

void EffectChain::add (std::unique_ptr<Effect> e, bool bypass)
{
    Slot slot;
    slot.type = e->type();
    slot.sidechain = e->sidechainSource();
    slot.bypass = bypass;
    slot.delay.prepare (2, bypass ? e->latencySamples() : 0);
    slot.effect = std::move (e);
    if (! slot.sidechain.empty() && std::find (sources_.begin(), sources_.end(), slot.sidechain) == sources_.end())
        sources_.push_back (slot.sidechain);
    effects_.push_back (std::move (slot));
    updateOffsets();
}

void EffectChain::updateOffsets()
{
    int total = 0;
    for (auto& s : effects_)
    {
        s.offset = total;
        total += s.effect->latencySamples();
    }
}

void EffectChain::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& s : effects_)
    {
        s.effect->prepare (sampleRate, maxBlockSize);
        s.delay.prepare (2, s.bypass ? s.effect->latencySamples() : 0);
    }
    updateOffsets();
}

void EffectChain::process (juce::AudioBuffer<float>& buffer, int numSamples, int64_t position)
{
    for (auto& s : effects_)
    {
        if (s.bypass)
            s.delay.process (buffer, numSamples);
        else
        {
            s.effect->setPosition ({ position - s.offset - s.effect->latencySamples(), false });
            s.effect->process (buffer, numSamples);
        }
    }
}

void EffectChain::process (juce::AudioBuffer<float>& buffer, int numSamples, const SongTimeline& timeline, int64_t clock)
{
    for (auto& s : effects_)
    {
        if (s.bypass)
        {
            s.delay.process (buffer, numSamples);
            continue;
        }
        if (! s.effect->followsPosition())
        {
            s.effect->process (buffer, numSamples);
            continue;
        }
        // 나가는 소리의 시계에서 곡 위치를 읽는다. 끊긴 자리에서 나눈 조각도 같은 효과가 이어서 처리한다 (할당하지 않는다).
        timeline.forEachSpan (clock - s.offset - s.effect->latencySamples(), numSamples, [&] (int offset, int count, BlockPosition p)
        {
            s.effect->setPosition (p);
            if (offset == 0 && count == numSamples)
                s.effect->process (buffer, numSamples);
            else
            {
                juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), offset, count);
                s.effect->process (part, count);
            }
        });
    }
}

int EffectChain::latencySamples() const
{
    int total = 0;
    for (const auto& s : effects_)
        total += s.effect->latencySamples();
    return total;
}

void EffectChain::setSidechainInput (const std::string& source, const float* left, const float* right)
{
    for (auto& s : effects_)
        if (s.sidechain == source)
            s.effect->setSidechainInput (left, right);
}

void EffectChain::alignSidechains (int inputLatency, const std::function<int (const std::string&)>& keyLatency)
{
    int position = inputLatency;
    for (auto& s : effects_)
    {
        if (! s.sidechain.empty())
        {
            const int behind = position - keyLatency (s.sidechain);   // 양수면 키가 앞서 있다
            s.effect->setSidechainAlignment (std::max (0, behind), std::max (0, -behind));
            if (s.bypass)
                s.delay.prepare (2, s.effect->latencySamples());
        }
        position += s.effect->latencySamples();
    }
    updateOffsets();
}

std::unique_ptr<Effect> EffectChain::takeTrailingLimiter()
{
    if (effects_.empty() || effects_.back().type != "limiter" || effects_.back().bypass)
        return nullptr;
    auto e = std::move (effects_.back().effect);
    effects_.pop_back();
    return e;
}

void EffectChain::continueFrom (EffectChain& previous)
{
    for (size_t i = 0; i < effects_.size(); ++i)
    {
        auto& slot = effects_[i];
        if (slot.bypass)
            continue;
        int occurrence = 0;
        for (size_t j = 0; j < i; ++j)
            occurrence += effects_[j].type == slot.type ? 1 : 0;
        for (auto& old : previous.effects_)
            if (old.type == slot.type && occurrence-- == 0)
            {
                if (! old.bypass)
                    slot.effect->continueFrom (*old.effect);
                break;
            }
    }
}

void processOffline (EffectChain& chain, juce::AudioBuffer<float>& buffer, int numSamples, int blockSize, const SidechainKeys* keys)
{
    if (chain.empty())
        return;
    const int latency = chain.latencySamples();
    juce::AudioBuffer<float> work (2, blockSize);
    // 키는 입력과 같은 시각 [start, start+n) 을 준다. 곡 끝 뒤(지연을 당기는 꼬리)는 무음이다.
    const auto sources = chain.sidechainSources();
    std::vector<juce::AudioBuffer<float>> keyWork (sources.size(), juce::AudioBuffer<float> (2, blockSize));
    const int end = numSamples + latency;
    for (int start = 0; start < end; start += blockSize)
    {
        const int n = std::min (blockSize, end - start);
        work.clear();
        const int available = std::max (0, std::min (n, numSamples - start));
        for (int ch = 0; ch < 2; ++ch)
            if (available > 0)
                work.copyFrom (ch, 0, buffer, ch, start, available);
        for (size_t k = 0; k < sources.size(); ++k)
        {
            auto& kw = keyWork[k];
            kw.clear();
            const juce::AudioBuffer<float>* key = nullptr;
            if (keys != nullptr)
                if (auto it = keys->find (sources[k]); it != keys->end())
                    key = it->second;
            if (key != nullptr && available > 0)
                for (int ch = 0; ch < 2; ++ch)
                    kw.copyFrom (ch, 0, *key, std::min (ch, key->getNumChannels() - 1), start, available);
            chain.setSidechainInput (sources[k], kw.getReadPointer (0), kw.getReadPointer (1));
        }
        chain.process (work, n, start);
        // 출력 [start, start+n) 은 지연만큼 앞, 곧 [start-L, start-L+n) 에 놓인다. 쓰는 자리는 이미 읽은 자리다.
        const int dst = start - latency;
        const int from = std::max (0, -dst);
        const int count = std::min (n - from, numSamples - (dst + from));
        for (int ch = 0; ch < 2 && count > 0; ++ch)
            buffer.copyFrom (ch, dst + from, work, ch, from, count);
    }
}

std::vector<double> filterResponseDb (const juce::var& params, double sampleRate, const std::vector<double>& frequenciesHz)
{
    const auto mode = filterModeOf (paramText (params, "mode", "lowpass"));
    const int slope = (int) value (params, "filter", "slope") >= 24 ? 24 : 12;
    const auto c = filterCoefficients (mode, slope, value (params, "filter", "cutoffHz"), value (params, "filter", "resonance"),
                                       value (params, "filter", "mix"), sampleRate);
    std::vector<double> out;
    out.reserve (frequenciesHz.size());
    for (const double f : frequenciesHz)
    {
        const double w = std::tan (pi * std::clamp (f, 1.0, sampleRate * 0.499) / sampleRate) / c.g;
        auto h = svfResponse (mode, w, c.k1);
        if (slope >= 24)
            h *= svfResponse (mode, w, c.k2);
        h = h * c.mix + (1.0 - c.mix);
        out.push_back (20.0 * std::log10 (std::max (1.0e-12, std::abs (h))));
    }
    return out;
}

double duckerShape (const juce::var& params, const TempoMap& tempo, double sampleRate, int64_t musicSample)
{
    const DuckShape shape (params, std::make_shared<const TempoMap> (tempo), sampleRate);
    return shape.at (musicSample);
}

// ---------------------------------------------------------------------------
// IR 생성
// ---------------------------------------------------------------------------
juce::AudioBuffer<float> generateHallImpulse (double fs, double rt60, double preDelayMs, double damping, double width, uint64_t seed)
{
    const int pre = (int) std::lround (preDelayMs * 0.001 * fs);
    const double seconds = std::min (12.0, rt60 * 1.25 + 0.1);
    const int length = (int) (seconds * fs);
    juce::AudioBuffer<float> ir (2, pre + length);
    ir.clear();

    const double rtLow = rt60 * 1.2, rtMid = rt60, rtHigh = rt60 * (1.0 - 0.6 * std::clamp (damping, 0.0, 1.0));
    const double kLow = std::exp (-2.0 * pi * 400.0 / fs), kHigh = std::exp (-2.0 * pi * 3500.0 / fs);
    for (int ch = 0; ch < 2; ++ch)
    {
        Random rng (combineSeed (seed, (uint64_t) ch + 1));
        float* x = ir.getWritePointer (ch, pre);
        double lpLow = 0.0, lpHigh = 0.0;
        for (int i = 0; i < length; ++i)
        {
            const double t = i / fs;
            const double noise = rng.gaussian();
            lpLow = kLow * lpLow + (1.0 - kLow) * noise;
            lpHigh = kHigh * lpHigh + (1.0 - kHigh) * noise;
            const double low = lpLow, mid = lpHigh - lpLow, high = noise - lpHigh;
            const double diffuse = low * std::exp (-6.907755 * t / rtLow) + mid * std::exp (-6.907755 * t / rtMid)
                                   + high * std::exp (-6.907755 * t / rtHigh);
            x[i] = (float) (diffuse * (1.0 - std::exp (-t / 0.025)));
        }
        // 초기 반사: 5~85ms 에 14개, 늦을수록 약하다
        for (int k = 0; k < 14; ++k)
        {
            const double t = 0.005 + rng.uniform() * 0.08;
            const int at = (int) (t * fs);
            if (at < length)
                x[at] += (float) ((rng.uniform() < 0.5 ? -1.0 : 1.0) * (0.9 - t * 7.0) * 3.0);
        }
    }
    // 폭: 가운데와 옆으로 나눠 옆을 width 만큼 남긴다
    float* l = ir.getWritePointer (0);
    float* r = ir.getWritePointer (1);
    for (int i = 0; i < ir.getNumSamples(); ++i)
    {
        const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * (float) std::clamp (width, 0.0, 1.0);
        l[i] = mid + side;
        r[i] = mid - side;
    }
    double energy = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < ir.getNumSamples(); ++i)
            energy += (double) ir.getSample (ch, i) * ir.getSample (ch, i);
    if (energy > 0.0)
        ir.applyGain ((float) (1.0 / std::sqrt (energy / 2.0)));
    return ir;
}

// ---------------------------------------------------------------------------
// 컨볼루션
// ---------------------------------------------------------------------------
void StereoConvolver::prepare (const juce::AudioBuffer<float>& impulse, int partitionSize)
{
    partition_ = partitionSize;
    fftSize_ = partitionSize * 2;
    int order = 0;
    while ((1 << order) < fftSize_)
        ++order;
    fft_ = std::make_unique<dsp::Fft> (order);
    bins_ = fftSize_ / 2 + 1;
    work_.assign ((size_t) fftSize_ * 2, 0.0f);

    // 역변환의 배율은 FFT 엔진마다 다를 수 있어 델타로 잰다
    std::fill (work_.begin(), work_.end(), 0.0f);
    work_[0] = 1.0f;
    fft_->forwardReal (work_.data(), true);
    fft_->inverseReal (work_.data());
    scale_ = std::abs (work_[0]) > 1.0e-12f ? 1.0f / work_[0] : 1.0f;

    const int length = impulse.getNumSamples();
    partitions_ = std::max (1, (length + partition_ - 1) / partition_);
    for (int ch = 0; ch < 2; ++ch)
    {
        irRe_[ch].assign ((size_t) partitions_, std::vector<float> ((size_t) bins_, 0.0f));
        irIm_[ch].assign ((size_t) partitions_, std::vector<float> ((size_t) bins_, 0.0f));
        const float* h = impulse.getReadPointer (std::min (ch, impulse.getNumChannels() - 1));
        for (int p = 0; p < partitions_; ++p)
        {
            std::fill (work_.begin(), work_.end(), 0.0f);
            for (int i = 0; i < partition_ && p * partition_ + i < length; ++i)
                work_[(size_t) i] = h[p * partition_ + i];
            fft_->forwardReal (work_.data(), true);
            for (int k = 0; k < bins_; ++k)
            {
                irRe_[ch][(size_t) p][(size_t) k] = work_[(size_t) (2 * k)];
                irIm_[ch][(size_t) p][(size_t) k] = work_[(size_t) (2 * k + 1)];
            }
        }
    }
    inRe_.assign ((size_t) partitions_, std::vector<float> ((size_t) bins_, 0.0f));
    inIm_.assign ((size_t) partitions_, std::vector<float> ((size_t) bins_, 0.0f));
    head_ = 0;
    previous_.assign ((size_t) partition_, 0.0f);
    current_.assign ((size_t) partition_, 0.0f);
    accRe_.assign ((size_t) bins_, 0.0f);
    accIm_.assign ((size_t) bins_, 0.0f);
    for (auto& f : outFifo_)
        f.assign ((size_t) partition_, 0.0f);
    fifoPos_ = 0;
}

void StereoConvolver::processPartition()
{
    std::fill (work_.begin(), work_.end(), 0.0f);
    std::copy (previous_.begin(), previous_.end(), work_.begin());
    std::copy (current_.begin(), current_.end(), work_.begin() + partition_);
    fft_->forwardReal (work_.data(), true);
    auto& xr = inRe_[(size_t) head_];
    auto& xi = inIm_[(size_t) head_];
    for (int k = 0; k < bins_; ++k)
    {
        xr[(size_t) k] = work_[(size_t) (2 * k)];
        xi[(size_t) k] = work_[(size_t) (2 * k + 1)];
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (accRe_.begin(), accRe_.end(), 0.0f);
        std::fill (accIm_.begin(), accIm_.end(), 0.0f);
        for (int p = 0; p < partitions_; ++p)
        {
            const int idx = (head_ - p + partitions_) % partitions_;
            const float* ar = inRe_[(size_t) idx].data();
            const float* ai = inIm_[(size_t) idx].data();
            const float* br = irRe_[ch][(size_t) p].data();
            const float* bi = irIm_[ch][(size_t) p].data();
            float* cr = accRe_.data();
            float* ci = accIm_.data();
            for (int k = 0; k < bins_; ++k)
            {
                cr[k] += ar[k] * br[k] - ai[k] * bi[k];
                ci[k] += ar[k] * bi[k] + ai[k] * br[k];
            }
        }
        std::fill (work_.begin(), work_.end(), 0.0f);
        for (int k = 0; k < bins_; ++k)
        {
            work_[(size_t) (2 * k)] = accRe_[(size_t) k];
            work_[(size_t) (2 * k + 1)] = accIm_[(size_t) k];
        }
        fft_->inverseReal (work_.data());
        for (int i = 0; i < partition_; ++i)
            outFifo_[ch][(size_t) i] = work_[(size_t) (partition_ + i)] * scale_;
    }
    std::swap (previous_, current_);
    head_ = (head_ + 1) % partitions_;
}

void StereoConvolver::continueFrom (StereoConvolver& previous)
{
    if (previous.partition_ != partition_ || previous.bins_ != bins_)
        return;
    previous_ = previous.previous_;
    current_ = previous.current_;
    outFifo_[0] = previous.outFifo_[0];
    outFifo_[1] = previous.outFifo_[1];
    fifoPos_ = previous.fifoPos_;
    // 가장 최근의 분할부터 옮긴다: 다음에 쓸 자리 head_ 의 바로 앞이 가장 최근이다
    const int count = std::min (partitions_, previous.partitions_);
    for (int p = 1; p <= count; ++p)
    {
        const auto to = (size_t) ((head_ - p + partitions_) % partitions_);
        const auto from = (size_t) ((previous.head_ - p + previous.partitions_) % previous.partitions_);
        std::copy (previous.inRe_[from].begin(), previous.inRe_[from].end(), inRe_[to].begin());
        std::copy (previous.inIm_[from].begin(), previous.inIm_[from].end(), inIm_[to].begin());
    }
}

void StereoConvolver::process (const float* input, float* outLeft, float* outRight, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        current_[(size_t) fifoPos_] = input[i];
        outLeft[i] = outFifo_[0][(size_t) fifoPos_];
        outRight[i] = outFifo_[1][(size_t) fifoPos_];
        if (++fifoPos_ == partition_)
        {
            processPartition();
            fifoPos_ = 0;
        }
    }
}

} // namespace mr
