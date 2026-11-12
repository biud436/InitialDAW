#include "AnalyzerPanel.h"

#include "Theme.h"

#include <cmath>

namespace daw
{

namespace
{

constexpr float lowDb = -90.0f, highDb = 0.0f;
constexpr double lowHz = 20.0, highHz = 20000.0;

juce::String lufsText (float v) { return v <= mr::LoudnessMeter::silent + 1.0f ? juce::String ("-") : juce::String (v, 1); }

} // namespace

AnalyzerPanel::AnalyzerPanel()
{
    reset_.setButtonText (juce::String::fromUTF8 ("다시 재기"));
    reset_.setTooltip (juce::String::fromUTF8 ("통합 라우드니스와 트루 피크를 처음부터 다시 잽니다"));
    reset_.setWantsKeyboardFocus (false);
    reset_.onClick = [this] { if (onReset) onReset(); };
    addAndMakeVisible (reset_);
}

juce::Rectangle<int> AnalyzerPanel::spectrumArea() const { return getLocalBounds().reduced (10, 0).withTrimmedTop (30).withHeight (std::max (60, getHeight() / 2 - 30)); }

void AnalyzerPanel::resized()
{
    reset_.setBounds (getLocalBounds().reduced (10, 10).removeFromBottom (26));
    shown_.assign ((size_t) std::max (1, spectrumArea().getWidth() / 2), lowDb);
}

void AnalyzerPanel::update (mr::RealtimePlayer* player, std::optional<double> targetLufs)
{
    target_ = targetLufs;
    if (player == nullptr)
    {
        reading_ = {};
        for (auto& v : shown_)
            v = std::max (lowDb, v - 3.0f);
        repaint();
        return;
    }
    auto& meter = player->loudness();
    reading_ = { meter.momentary(), meter.shortTerm(), meter.integrated(), meter.truePeakDb(), meter.measuredSeconds() };
    // 스펙트럼: 마지막 4096 샘플에 한 창을 씌워 크기를 dB 로 (0 dBFS 사인파가 0 dB)
    player->copyMasterSamples (samples_.data(), size);
    std::copy (samples_.begin(), samples_.end(), work_.begin());
    std::fill (work_.begin() + size, work_.end(), 0.0f);
    window_.multiplyWithWindowingTable (work_.data(), (size_t) size);
    fft_.performFrequencyOnlyForwardTransform (work_.data(), true);
    const double rate = player->sampleRate();
    const int columns = (int) shown_.size();
    for (int c = 0; c < columns; ++c)
    {
        const double f0 = lowHz * std::pow (highHz / lowHz, (double) c / columns);
        const double f1 = lowHz * std::pow (highHz / lowHz, (double) (c + 1) / columns);
        const int b0 = std::clamp ((int) std::floor (f0 / rate * size), 1, size / 2 - 1);
        const int b1 = std::clamp ((int) std::ceil (f1 / rate * size), b0 + 1, size / 2);
        float m = 0.0f;
        for (int b = b0; b < b1; ++b)
            m = std::max (m, work_[(size_t) b]);
        const float db = juce::Decibels::gainToDecibels (m * 4.0f / (float) size, lowDb);   // 한 창의 이득 0.5 를 되돌린다
        auto& v = shown_[(size_t) c];
        v = db > v ? db : std::max (db, v - 1.5f);   // 오를 때는 바로, 내릴 때는 천천히
    }
    repaint();
}

void AnalyzerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::outline);
    g.drawRect (getLocalBounds(), 1);
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f, true));
    g.drawText (juce::String::fromUTF8 ("마스터 분석"), getLocalBounds().reduced (10, 6).removeFromTop (18), juce::Justification::centredLeft);

    // 스펙트럼과 눈금
    const auto area = spectrumArea();
    g.setColour (colours::laneA);
    g.fillRect (area);
    g.setFont (monoFont (9.0f));
    for (const double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = (float) area.getX() + (float) area.getWidth() * (float) (std::log (f / lowHz) / std::log (highHz / lowHz));
        g.setColour (colours::beatLine);
        g.fillRect (x, (float) area.getY(), 1.0f, (float) area.getHeight());
        g.setColour (colours::textFaint);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 14.0f, (float) area.getBottom() + 2.0f, 28.0f, 10.0f),
                    juce::Justification::centred, false);
    }
    for (const float db : { -12.0f, -24.0f, -36.0f, -48.0f, -60.0f, -72.0f })
    {
        const float y = juce::jmap (db, lowDb, highDb, (float) area.getBottom(), (float) area.getY());
        g.setColour (colours::beatLine);
        g.fillRect ((float) area.getX(), y, (float) area.getWidth(), 1.0f);
    }
    if (! shown_.empty())
    {
        juce::Path p;
        const float colWidth = (float) area.getWidth() / (float) shown_.size();
        p.startNewSubPath ((float) area.getX(), (float) area.getBottom());
        for (size_t c = 0; c < shown_.size(); ++c)
            p.lineTo ((float) area.getX() + ((float) c + 0.5f) * colWidth, juce::jmap (juce::jlimit (lowDb, highDb, shown_[c]), lowDb, highDb, (float) area.getBottom(), (float) area.getY()));
        p.lineTo ((float) area.getRight(), (float) area.getBottom());
        p.closeSubPath();
        g.setColour (colours::accent.withAlpha (0.25f));
        g.fillPath (p);
        g.setColour (colours::accent);
        g.strokePath (p, juce::PathStrokeType (1.2f));
    }

    // 라우드니스
    auto rows = getLocalBounds().reduced (10, 0).withTop (area.getBottom() + 18).withTrimmedBottom (44);
    const auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour colour)
    {
        auto r = rows.removeFromTop (22);
        g.setColour (colours::textDim);
        g.setFont (uiFont (12.0f));
        g.drawText (label, r.removeFromLeft (100), juce::Justification::centredLeft);
        g.setColour (colour);
        g.setFont (monoFont (14.0f));
        g.drawText (value, r, juce::Justification::centredRight);
    };
    row (juce::String::fromUTF8 ("순간 (400 ms)"), lufsText (reading_.momentary) + " LUFS", colours::text);
    row (juce::String::fromUTF8 ("단기 (3 초)"), lufsText (reading_.shortTerm) + " LUFS", colours::text);
    juce::Colour integratedColour = colours::text;
    juce::String targetText;
    if (target_ && reading_.integrated > mr::LoudnessMeter::silent + 1.0f)
    {
        const double diff = reading_.integrated - *target_;
        integratedColour = std::abs (diff) <= 1.0 ? colours::solo : colours::mute;
        targetText = juce::String::fromUTF8 ("목표 ") + juce::String (*target_, 1) + juce::String::fromUTF8 (" 에서 ") + (diff >= 0.0 ? "+" : "") + juce::String (diff, 1);
    }
    row (juce::String::fromUTF8 ("통합"), lufsText (reading_.integrated) + " LUFS", integratedColour);
    row (juce::String::fromUTF8 ("트루 피크"), lufsText (reading_.truePeak) + " dBTP", reading_.truePeak > -1.0f ? colours::danger : colours::text);
    auto info = rows.removeFromTop (34);
    g.setColour (colours::textFaint);
    g.setFont (uiFont (11.0f));
    g.drawFittedText (juce::String::fromUTF8 ("잰 시간 ") + juce::String ((int) reading_.seconds / 60) + ":" + juce::String ((int) reading_.seconds % 60).paddedLeft ('0', 2)
                          + (targetText.isNotEmpty() ? ",  " + targetText : juce::String()),
                      info, juce::Justification::centredLeft, 2);
}

} // namespace daw
