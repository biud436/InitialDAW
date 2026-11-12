#include "effects/Automation.h"

#include <algorithm>
#include <cmath>

namespace mr
{

SampleLane::SampleLane (const AutomationLane& lane, std::shared_ptr<const TempoMap> tempo, double sampleRate, bool logarithmic)
    : lane_ (lane), tempo_ (std::move (tempo)), sampleRate_ (sampleRate), log_ (logarithmic)
{
    if (log_)
        for (auto& p : lane_.points)
        {
            p.value = std::log (std::max (p.value, 1.0e-9));
            if (p.curve == CurveShape::exponential)
                p.curve = CurveShape::linear;   // 로그 영역의 직선이 곧 지수 곡선이다
        }
}

double SampleLane::atGrid (int64_t grid) const
{
    if (lane_.empty())
        return 0.0;
    for (int k = 0; k < 2; ++k)
        if (cachedGrid_[k] == grid)
            return cachedValue_[k];
    const double beat = tempo_ != nullptr ? tempo_->beatAt ((double) grid / sampleRate_) : (double) grid / sampleRate_ * 2.0;
    double v = lane_.valueAt (beat);
    if (log_)
        v = std::exp (v);
    const int slot = (int) ((grid / kAutomationGrid) & 1);
    cachedGrid_[slot] = grid;
    cachedValue_[slot] = v;
    return v;
}

double SampleLane::at (int64_t sample) const
{
    const int64_t grid = automationGridStart (sample);
    const double a = atGrid (grid), b = atGrid (grid + kAutomationGrid);
    return a + (b - a) * ((double) (sample - grid) * (1.0 / kAutomationGrid));
}

} // namespace mr
