#include "SynthPanel.h"

#include "MixerText.h"
#include "Theme.h"
#include "composition/Document.h"

#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

constexpr int headerHeight = 66;
constexpr int margin = 14;
constexpr int gap = 12;
constexpr int titleHeight = 24;
constexpr int graphHeight = 96;
constexpr int knobWidth = 70;
constexpr int knobHeight = 98;

juce::String engineLabel (const std::string& engine)
{
    if (engine == "piano")
        return u8 ("모델링 피아노 (배음 합성)");
    if (engine == "pluck")
        return u8 ("뜯는 현 (카플러스 스트롱)");
    if (engine == "drums")
        return u8 ("드럼 키트 (합성)");
    return u8 ("감산 합성 (오실레이터, 필터, 엔벨로프)");
}

// 어느 단면이 어느 줄에 놓이고 그래프를 갖는가
std::vector<std::vector<std::string>> rowsFor (const std::string& engine)
{
    if (engine == "subtractive")
        return { { "oscillator", "filter", "amp" }, { "vibrato", "pitch", "output" } };
    if (engine == "drums")
        return { { "pitch", "output" } };
    if (engine == "piano")
        return { { "decay", "pitch", "output" } };
    return { { "decay", "output" } };
}

bool hasGraph (const std::string& engine, const std::string& section)
{
    return section == "oscillator" || section == "filter" || section == "amp" || section == "decay" || (engine != "subtractive" && section == "output");
}

// 표시창과 트랙 이름, 초기화 단추가 들어가는 폭
constexpr int minimumWidth = 640;

} // namespace

struct SynthPanel::Knob
{
    mr::ParameterInfo info;
    juce::Slider slider;
    juce::Rectangle<int> cell;
    bool dragging = false;
};

// 고르는 매개변수 (필터 방식): 단면 머리의 목록
struct SynthPanel::Choice
{
    mr::ParameterInfo info;
    juce::ComboBox combo;
};

SynthPanel::SynthPanel (Project& project, size_t track, std::string preset, mr::SynthValues base)
    : project_ (project), track_ (track), preset_ (std::move (preset)), engine_ (mr::synthEngineName (preset_)), base_ (std::move (base))
{
    accent_ = project_.summary (track_).colour;
    params_ = mr::synthParameters (preset_, &base_);
    for (const auto& p : params_)
    {
        if (p.toggle)   // 켜고 끄는 값(필터 켜기, 모노)은 단면 머리의 단추
        {
            auto button = std::make_unique<juce::ToggleButton> (parameterLabel ("synth", p.key));
            auto* b = button.get();
            b->setColour (juce::ToggleButton::tickColourId, accent_);
            if (const auto tip = parameterTooltip ("synth", p.key); tip.isNotEmpty())
                b->setTooltip (tip);
            const auto key = p.key;
            b->onClick = [this, b, key]
            {
                if (! updating_)
                    commit (key, b->getToggleState() ? 1.0 : 0.0, key == "filter" ? u8 ("필터") : parameterLabel ("synth", key));
            };
            addAndMakeVisible (*b);
            switches_.push_back ({ p, std::move (button) });
            continue;
        }
        if (! p.choices.empty())   // 필터 방식은 단면 머리의 목록
        {
            auto choice = std::make_unique<Choice>();
            auto* c = choice.get();
            c->info = p;
            for (size_t i = 0; i < p.choices.size(); ++i)
                c->combo.addItem (synthChoiceLabel (p.key, i < p.choiceNames.size() ? p.choiceNames[i] : std::string {}), (int) i + 1);
            c->combo.setTooltip (parameterLabel ("synth", p.key) + ": " + parameterTooltip ("synth", p.key));
            c->combo.onChange = [this, c]
            {
                const int i = c->combo.getSelectedItemIndex();
                if (! updating_ && i >= 0 && i < (int) c->info.choices.size())
                    commit (c->info.key, c->info.choices[(size_t) i], u8 ("필터 방식"));
            };
            addAndMakeVisible (c->combo);
            choices_.push_back (std::move (choice));
            continue;
        }
        auto knob = std::make_unique<Knob>();
        auto* k = knob.get();
        k->info = p;
        auto& s = k->slider;
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.setMouseDragSensitivity (220);
        const auto info = p;
        if (p.logarithmic && p.minimum > 0.0)
            s.setNormalisableRange (juce::NormalisableRange<double> (
                p.minimum, p.maximum,
                [] (double lo, double hi, double x) { return lo * std::pow (hi / lo, x); },
                [] (double lo, double hi, double v) { return std::log (v / lo) / std::log (hi / lo); },
                [info] (double, double, double v) { return roundParameter (info, v); }));
        else
        {
            juce::NormalisableRange<double> range (p.minimum, p.maximum);   // 반올림은 값을 쓸 때 한다 (roundParameter)
            if (p.skewMid > p.minimum && p.skewMid < p.maximum)
                range.setSkewForCentre (p.skewMid);
            s.setNormalisableRange (range);
        }
        s.setDoubleClickReturnValue (true, roundParameter (p, p.defaultValue));
        s.setColour (juce::Slider::trackColourId, accent_);
        s.getProperties().set ("arcFrom", p.minimum < 0.0 && p.maximum > 0.0 ? s.valueToProportionOfLength (0.0) : 0.0);   // 레벨과 음높이는 0 에서
        auto tip = parameterTooltip ("synth", p.key);
        s.setTooltip (parameterLabel ("synth", p.key) + (tip.isNotEmpty() ? ": " + tip : juce::String()) + u8 (" (두 번 클릭하면 기본값)"));
        s.onDragStart = [k] { k->dragging = true; };
        s.onValueChange = [this, k]
        {
            if (updating_)
                return;
            const double v = roundParameter (k->info, k->slider.getValue());
            if (k->dragging)
                preview (k->info.key, v);
            else
                commit (k->info.key, v, parameterLabel ("synth", k->info.key));
        };
        s.onDragEnd = [this, k]
        {
            k->dragging = false;
            commit (k->info.key, roundParameter (k->info, k->slider.getValue()), parameterLabel ("synth", k->info.key));
        };
        addAndMakeVisible (s);
        knobs_.push_back (std::move (knob));
    }
    reset_.setButtonText (u8 ("초기화"));
    reset_.setTooltip (u8 ("이 트랙의 신스 값을 지우고 프리셋 그대로 돌아갑니다"));
    reset_.onClick = [this] { reset(); };
    addAndMakeVisible (reset_);

    // 단면과 손잡이를 줄마다 놓는다
    for (const auto& row : rowsFor (engine_))
        for (const auto& name : row)
        {
            Section s;
            s.name = name;
            for (auto& k : knobs_)
                if (k->info.section == name)
                    s.knobs.push_back (k.get());
            sections_.push_back (s);
        }
    layoutSections();
    project_.addListener (this);
    updateValues();
}

SynthPanel::~SynthPanel() { project_.removeListener (this); }

juce::Slider* SynthPanel::knob (const juce::String& key)
{
    for (auto& k : knobs_)
        if (juce::String (k->info.key) == key)
            return &k->slider;
    return nullptr;
}

juce::Button* SynthPanel::toggle (const juce::String& key)
{
    for (auto& sw : switches_)
        if (juce::String (sw.info.key) == key)
            return sw.button.get();
    return nullptr;
}

juce::ComboBox* SynthPanel::choice (const juce::String& key)
{
    for (auto& c : choices_)
        if (juce::String (c->info.key) == key)
            return &c->combo;
    return nullptr;
}

void SynthPanel::layoutSections()
{
    // 줄마다 단면의 너비 (손잡이 수, 그래프의 최소 너비), 뒷줄은 앞줄의 너비에 맞춰 늘인다
    const auto rows = rowsFor (engine_);
    std::vector<std::vector<int>> widths;
    int widest = 0;
    size_t index = 0;
    for (const auto& row : rows)
    {
        std::vector<int> w;
        int total = 0;
        for (size_t i = 0; i < row.size(); ++i, ++index)
        {
            const auto& s = sections_[index];
            int width = std::max (1, (int) s.knobs.size()) * knobWidth + 20;
            if (hasGraph (engine_, s.name))
                width = std::max (width, engine_ == "drums" ? 380 : 230);
            w.push_back (width);
            total += width + (i > 0 ? gap : 0);
        }
        widest = std::max (widest, total);
        widths.push_back (w);
    }
    widest = std::max (widest, minimumWidth - 2 * margin);
    int y = headerHeight;
    index = 0;
    for (size_t r = 0; r < rows.size(); ++r)
    {
        int total = 0;
        for (size_t i = 0; i < widths[r].size(); ++i)
            total += widths[r][i] + (i > 0 ? gap : 0);
        const double stretch = total > 0 ? (double) widest / total : 1.0;
        bool graphRow = false;
        for (const auto& name : rows[r])
            graphRow |= hasGraph (engine_, name);
        const int height = titleHeight + (graphRow ? graphHeight + 8 : 0) + knobHeight + 10;
        int x = margin;
        for (size_t i = 0; i < widths[r].size(); ++i, ++index)
        {
            auto& s = sections_[index];
            const int w = i + 1 == widths[r].size() ? margin + widest - x : (int) std::round (widths[r][i] * stretch);
            s.area = { x, y, w, height };
            s.graph = hasGraph (engine_, s.name) ? juce::Rectangle<int> (x + 10, y + titleHeight, w - 20, graphHeight) : juce::Rectangle<int>();
            const int knobsY = y + titleHeight + (graphRow ? graphHeight + 8 : 0);
            const int knobsWidth = (int) s.knobs.size() * knobWidth;
            int kx = x + (w - knobsWidth) / 2;
            for (auto* k : s.knobs)
            {
                k->cell = { kx, knobsY, knobWidth, knobHeight };
                kx += knobWidth;
            }
            x += w + gap;
        }
        y += height + gap;
    }
    setSize (widest + 2 * margin, y - gap + margin);
}

void SynthPanel::resized()
{
    reset_.setBounds (getWidth() - margin - 84, 18, 84, 30);
    for (auto& k : knobs_)
        k->slider.setBounds (k->cell.withTrimmedTop (16).withHeight (56).withSizeKeepingCentre (56, 56));
    for (const auto& s : sections_)
    {
        int x = s.area.getRight();
        for (auto& sw : switches_)
            if (sw.info.section == s.name)
            {
                x -= 78;
                sw.button->setBounds (x, s.area.getY() + 2, 72, titleHeight - 2);
            }
        if (s.name == "filter")
            for (auto& c : choices_)
                c->combo.setBounds (x - 112, s.area.getY() + 3, 106, titleHeight - 4);
    }
}

double SynthPanel::valueOf (const std::string& key) const
{
    if (const auto it = dragging_.find (key); it != dragging_.end())
        return it->second;
    if (project_.loaded() && track_ < project_.trackCount())
    {
        const auto& synth = project_.composition().tracks[track_].synth;
        if (const auto it = synth.find (key); it != synth.end())
            return it->second;
    }
    for (const auto& p : params_)
        if (p.key == key)
            return p.defaultValue;
    return 0.0;
}

mr::SynthValues SynthPanel::currentValues() const
{
    mr::SynthValues values;
    for (const auto& p : params_)
        values[p.key] = valueOf (p.key);
    return values;
}

void SynthPanel::updateValues()
{
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    for (auto& k : knobs_)
        if (! k->dragging)
            k->slider.setValue (valueOf (k->info.key), juce::dontSendNotification);
    for (auto& sw : switches_)
        sw.button->setToggleState (valueOf (sw.info.key) >= 0.5, juce::dontSendNotification);
    for (auto& c : choices_)
    {
        const double v = valueOf (c->info.key);
        for (size_t i = 0; i < c->info.choices.size(); ++i)
            if (std::abs (c->info.choices[i] - v) < 1.0e-9)
                c->combo.setSelectedItemIndex ((int) i, juce::dontSendNotification);
    }
    const bool edited = project_.loaded() && track_ < project_.trackCount() && ! project_.composition().tracks[track_].synth.empty();
    reset_.setEnabled (edited);
    repaint();
}

void SynthPanel::projectEdited (int)
{
    if (! project_.loaded() || track_ >= project_.trackCount())
        return;
    updateValues();
}

void SynthPanel::preview (const std::string& key, double value)
{
    dragging_[key] = value;
    repaint();
    if (! onPreview || ! project_.loaded() || track_ >= project_.trackCount())
        return;
    auto copy = project_.composition();
    copy.tracks[track_].synth[key] = value;
    onPreview (copy);
}

void SynthPanel::commit (const std::string& key, double value, const juce::String& label)
{
    dragging_.erase (key);
    if (! project_.loaded() || track_ >= project_.trackCount())
        return;
    const mr::ParameterInfo* info = nullptr;
    for (const auto& p : params_)
        if (p.key == key)
            info = &p;
    if (info == nullptr)
        return;
    // 기본값으로 돌아가면 그 키를 지운다 (곡에는 프리셋과 다른 값만 남는다)
    const bool isDefault = std::abs (value - info->defaultValue) < 1.0e-9 || std::abs (value - roundParameter (*info, info->defaultValue)) < 1.0e-9;
    const auto& synth = project_.composition().tracks[track_].synth;
    const auto it = synth.find (key);
    if ((isDefault && it == synth.end()) || (! isDefault && it != synth.end() && it->second == value))
    {
        repaint();
        return;
    }
    // 고르는 매개변수는 이름으로 쓴다 ("filterMode": "highpass")
    juce::var written (value);
    for (size_t i = 0; i < info->choices.size() && i < info->choiceNames.size(); ++i)
        if (std::abs (info->choices[i] - value) < 1.0e-9)
            written = juce::String (info->choiceNames[i]);
    juce::String error;
    const auto json = mr::withSynthValue (project_.trackJson (track_), key, isDefault ? juce::var() : written);
    if (! project_.editTrack (track_, json, u8 ("신스: ") + label, error))
    {
        if (onStatus)
            onStatus (error.replaceCharacter ('\n', ' '), true);
        if (onPreview)
            onPreview (project_.composition());
        updateValues();
    }
}

bool SynthPanel::reset()
{
    if (! project_.loaded() || track_ >= project_.trackCount() || project_.composition().tracks[track_].synth.empty())
        return false;
    auto json = project_.trackJson (track_);
    json.getDynamicObject()->removeProperty ("synth");
    juce::String error;
    return project_.editTrack (track_, json, u8 ("신스 초기화"), error);
}

// ---- 그리기 ----

void SynthPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);

    // 머리: 프리셋 이름 표시창과 트랙
    auto display = juce::Rectangle<float> ((float) margin, 12.0f, (float) juce::jlimit (220, 330, getWidth() * 34 / 100), 44.0f);
    g.setColour (juce::Colour (0xff0d1014));
    g.fillRoundedRectangle (display, 5.0f);
    g.setColour (accent_.withAlpha (0.6f));
    g.drawRoundedRectangle (display.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (accent_);
    g.setFont (monoFont (20.0f));
    g.drawText (juce::String (preset_).toUpperCase(), display.reduced (14.0f, 0.0f).withTrimmedBottom (14.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f));
    g.drawText (engineLabel (engine_), display.reduced (14.0f, 0.0f).withTrimmedTop (24.0f).toNearestInt(), juce::Justification::centredLeft);

    if (project_.loaded() && track_ < project_.trackCount())
    {
        auto info = juce::Rectangle<int> ((int) display.getRight() + 18, 12, getWidth() - (int) display.getRight() - 18 - margin - 96, 46);
        g.setColour (colours::text);
        g.setFont (uiFont (15.0f, true));
        g.drawFittedText (project_.channelName (ChannelRef::track (track_)), info.removeFromTop (20), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (colours::textDim);
        g.setFont (uiFont (12.0f));
        const auto edited = project_.composition().tracks[track_].synth.size();
        g.drawFittedText (edited > 0 ? u8 ("프리셋과 다른 값 ") + juce::String ((int) edited) + u8 ("개. 곡의 트랙에 저장됩니다.")
                                     : u8 ("프리셋 그대로입니다. 손잡이를 돌리면 곡의 트랙에 저장됩니다."),
                          info, juce::Justification::centredLeft, 2, 0.85f);
    }

    for (const auto& s : sections_)
        paintSection (g, s);
}

void SynthPanel::paintSection (juce::Graphics& g, const Section& s)
{
    g.setColour (colours::panel);
    g.fillRoundedRectangle (s.area.toFloat(), 6.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (s.area.toFloat().reduced (0.5f), 6.0f, 1.0f);
    g.setColour (accent_);
    g.fillRoundedRectangle ((float) s.area.getX() + 10.0f, (float) s.area.getY() + 9.0f, 3.0f, 10.0f, 1.5f);
    g.setColour (colours::text);
    g.setFont (uiFont (12.5f, true));
    const int headerControls = s.name == "filter" ? 200 : 0;   // 필터 단면 머리의 방식 목록과 켜기 단추
    g.drawText (synthSectionName (s.name), s.area.getX() + 18, s.area.getY() + 2, s.area.getWidth() - 28 - headerControls, titleHeight - 2,
                juce::Justification::centredLeft);

    if (! s.graph.isEmpty())
    {
        const auto r = s.graph.toFloat();
        g.setColour (juce::Colour (0xff0f1217));
        g.fillRoundedRectangle (r, 4.0f);
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (s.graph);
        if (s.name == "oscillator")
            paintWaveform (g, r);
        else if (s.name == "filter")
            paintFilter (g, r);
        else if (s.name == "amp")
            paintEnvelope (g, r);
        else if (s.name == "decay")
            paintDecay (g, r);
        else if (engine_ == "drums")
            paintKit (g, r);
        else
            paintVelocity (g, r);
    }

    for (auto* k : s.knobs)
    {
        const auto& p = k->info;
        const bool changed = project_.loaded() && track_ < project_.trackCount()
                             && project_.composition().tracks[track_].synth.count (p.key) > 0;
        g.setColour (changed ? colours::text : colours::textDim);
        g.setFont (uiFont (11.5f, changed));
        g.drawFittedText (parameterLabel ("synth", p.key), k->cell.withHeight (16), juce::Justification::centred, 1, 0.8f);
        g.setColour (k->dragging ? accent_ : colours::textDim);
        g.setFont (monoFont (10.5f));
        g.drawFittedText (formatParameter (p, valueOf (p.key)), k->cell.withTrimmedTop (74).withHeight (16), juce::Justification::centred, 1, 0.7f);
        if (changed)
        {
            g.setColour (accent_);
            g.fillEllipse ((float) k->cell.getRight() - 12.0f, (float) k->cell.getY() + 22.0f, 5.0f, 5.0f);
        }
    }
}

void SynthPanel::paintWaveform (juce::Graphics& g, juce::Rectangle<float> r)
{
    const auto values = currentValues();
    // 두 주기를 그린다 (톱니파의 떨어지는 자리가 보이게)
    const auto cycle = mr::synthWaveform (preset_, values, std::max (32, (int) r.getWidth() / 2 + 1));
    std::vector<float> wave (cycle);
    wave.insert (wave.end(), cycle.begin(), cycle.end());
    g.setColour (colours::beatLine);
    g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
    const bool silent = std::all_of (wave.begin(), wave.end(), [] (float v) { return std::abs (v) < 1.0e-4f; });
    if (! wave.empty() && ! silent)
    {
        juce::Path p;
        for (size_t i = 0; i < wave.size(); ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / (float) (wave.size() - 1);
            const float y = r.getCentreY() - wave[i] * r.getHeight() * 0.38f;
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        g.setColour (accent_);
        g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
    }
    const double noise = values.count ("noise") > 0 ? values.at ("noise") : 0.0;
    if (noise > 0.0)
    {
        // 잡음의 양을 흐린 점으로 (결정적인 무늬)
        juce::Random rng (7);
        g.setColour (colours::text.withAlpha (0.25f));
        const int dots = (int) (noise * r.getWidth() * 1.5f);
        for (int i = 0; i < dots; ++i)
            g.fillRect (r.getX() + rng.nextFloat() * r.getWidth(), r.getCentreY() + (rng.nextFloat() - 0.5f) * r.getHeight() * 0.8f, 1.5f, 1.5f);
    }
    g.setColour (colours::textFaint);
    g.setFont (uiFont (10.0f));
    g.drawText (u8 ("두 주기"), r.reduced (6.0f, 3.0f).toNearestInt(), juce::Justification::topLeft);
}

void SynthPanel::paintFilter (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double lo = std::log (20.0), hi = std::log (20000.0);
    auto xOf = [&] (double f) { return r.getX() + (float) ((std::log (f) - lo) / (hi - lo)) * r.getWidth(); };
    auto yOf = [&] (double db) { return r.getY() + r.getHeight() * (float) ((18.0 - juce::jlimit (-42.0, 18.0, db)) / 60.0); };
    g.setFont (monoFont (9.0f));
    for (const double f : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (colours::beatLine);
        g.fillRect (xOf (f), r.getY(), 1.0f, r.getHeight());
        g.setColour (colours::textFaint);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), (int) xOf (f) + 2, (int) r.getBottom() - 12, 28, 11,
                    juce::Justification::centredLeft);
    }
    g.setColour (colours::barLine);
    g.fillRect (r.getX(), yOf (0.0), r.getWidth(), 1.0f);

    if (valueOf ("filter") < 0.5)
    {
        g.setColour (accent_);
        g.fillRect (r.getX(), yOf (0.0) - 1.0f, r.getWidth(), 2.0f);
        g.setColour (colours::textDim);
        g.setFont (uiFont (11.0f));
        g.drawText (u8 ("필터 꺼짐: 오실레이터를 그대로 냅니다"), r.reduced (8.0f, 6.0f).toNearestInt(), juce::Justification::topLeft);
        return;
    }
    // 엔진과 같은 식: (기본 + 엔벨로프 + 벨로시티) x 키 추적 (C4 에서 1), 세기 0.8 의 음
    const double held = valueOf ("cutoffHz") + valueOf ("filterVelHz") * 0.8;
    const double attack = held + valueOf ("filterEnvHz");
    const double res = valueOf ("resonance");
    const int mode = (int) std::lround (valueOf ("filterMode"));
    std::vector<double> freqs;
    const int points = std::max (32, (int) r.getWidth());
    for (int i = 0; i < points; ++i)
        freqs.push_back (std::exp (lo + (hi - lo) * i / (points - 1)));
    auto curve = [&] (double cutoff)
    {
        const auto db = mr::synthFilterResponseDb (cutoff, res, 48000.0, freqs, mode);
        juce::Path p;
        for (int i = 0; i < points; ++i)
        {
            const float x = xOf (freqs[(size_t) i]), y = yOf (db[(size_t) i]);
            if (i == 0)
                p.startNewSubPath (x, y);
            else
                p.lineTo (x, y);
        }
        return p;
    };
    if (attack > held + 1.0)
    {
        g.setColour (accent_.withAlpha (0.45f));
        juce::Path dashed;
        const float dash[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, curve (attack), dash, 2);
        g.fillPath (dashed);
    }
    auto held_ = curve (held);
    auto fill = held_;
    fill.lineTo (r.getRight(), r.getBottom());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setColour (accent_.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (accent_);
    g.strokePath (held_, juce::PathStrokeType (1.8f));
    g.setColour (colours::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText (u8 ("실선: 누르고 있을 때, 점선: 음이 시작할 때"), r.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::topRight);
}

void SynthPanel::paintEnvelope (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double a = std::max (0.001, valueOf ("attack")), d = valueOf ("decay"), s = valueOf ("sustain"), rel = std::max (0.005, valueOf ("release"));
    const double hold = std::max (0.25, (a + d + rel) * 0.3);
    const double total = a + d + hold + rel;
    auto xOf = [&] (double t) { return r.getX() + 6.0f + (r.getWidth() - 12.0f) * (float) (t / total); };
    auto yOf = [&] (double v) { return r.getBottom() - 8.0f - (r.getHeight() - 20.0f) * (float) juce::jlimit (0.0, 1.0, v); };
    auto level = [&] (double t)
    {
        if (t < a)
            return t / a;
        if (t < a + d + hold)
            return d > 0.0 ? s + (1.0 - s) * std::exp (-4.6 * (t - a) / d) : s;
        const double atRelease = d > 0.0 ? s + (1.0 - s) * std::exp (-4.6 * (d + hold) / d) : s;
        return atRelease * std::pow (10.0, -3.0 * (t - a - d - hold) / rel);   // 릴리스 시간에 60 dB
    };
    juce::Path p;
    const int points = std::max (64, (int) r.getWidth());
    for (int i = 0; i <= points; ++i)
    {
        const double t = total * i / points;
        const float x = xOf (t), y = yOf (level (t));
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    auto fill = p;
    fill.lineTo (xOf (total), yOf (0.0));
    fill.lineTo (xOf (0.0), yOf (0.0));
    fill.closeSubPath();
    g.setColour (accent_.withAlpha (0.12f));
    g.fillPath (fill);
    g.setColour (accent_);
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));
    g.setFont (uiFont (10.0f, true));
    const double marks[] = { 0.0, a, a + d, a + d + hold };
    const char* names[] = { "A", "D", "S", "R" };
    float lastLabel = -100.0f;
    for (int i = 0; i < 4; ++i)
    {
        if (i > 0)
        {
            g.setColour (colours::beatLine);
            g.fillRect (xOf (marks[i]), r.getY(), 1.0f, r.getHeight());
        }
        if (xOf (marks[i]) - lastLabel < 14.0f)
            continue;   // 짧은 어택에서 글자가 겹치지 않게
        lastLabel = xOf (marks[i]);
        g.setColour (colours::textFaint);
        g.drawText (names[i], (int) xOf (marks[i]) + 3, (int) r.getY() + 2, 14, 12, juce::Justification::centredLeft);
    }
}

void SynthPanel::paintDecay (juce::Graphics& g, juce::Rectangle<float> r)
{
    const double low = std::max (0.01, valueOf ("decayLow")), high = std::max (0.01, valueOf ("decayHigh"));
    const double top = std::log (30.0), bottom = std::log (0.05);
    auto xOf = [&] (int note) { return r.getX() + 8.0f + (r.getWidth() - 16.0f) * (float) (note - 21) / 87.0f; };
    auto yOf = [&] (double sec) { return r.getY() + 8.0f + (r.getHeight() - 22.0f) * (float) ((top - std::log (sec)) / (top - bottom)); };
    g.setFont (monoFont (9.0f));
    for (const double sec : { 0.1, 1.0, 10.0 })
    {
        g.setColour (colours::beatLine);
        g.fillRect (r.getX(), yOf (sec), r.getWidth(), 1.0f);
        g.setColour (colours::textFaint);
        g.drawText (juce::String (sec, sec < 1.0 ? 1 : 0) + "s", (int) r.getX() + 3, (int) yOf (sec) - 11, 30, 10, juce::Justification::centredLeft);
    }
    juce::Path p;
    for (int note = 21; note <= 108; ++note)
    {
        // 엔진과 같은 식: 건반에 따라 로그로 보간한 T60
        const double t = (note - 21) / 87.0;
        const double sec = std::exp (std::log (low) + (std::log (high) - std::log (low)) * t);
        if (note == 21)
            p.startNewSubPath (xOf (note), yOf (sec));
        else
            p.lineTo (xOf (note), yOf (sec));
    }
    g.setColour (accent_);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    g.setColour (colours::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText (u8 ("건반에 따라 소리가 60 dB 줄어드는 시간 (A0 에서 C8)"), r.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::bottomRight);
}

void SynthPanel::paintVelocity (juce::Graphics& g, juce::Rectangle<float> r)
{
    // 엔진과 같은 식: 크기 = 세기 ^ 벨로시티 곡선
    const double curve = std::max (0.01, valueOf ("velocityCurve"));
    auto xOf = [&] (double v) { return r.getX() + 10.0f + (r.getWidth() - 20.0f) * (float) v; };
    auto yOf = [&] (double v) { return r.getBottom() - 16.0f - (r.getHeight() - 26.0f) * (float) v; };
    g.setColour (colours::beatLine);
    g.drawLine (xOf (0.0), yOf (0.0), xOf (1.0), yOf (1.0), 1.0f);
    juce::Path p;
    for (int i = 0; i <= 64; ++i)
    {
        const double v = i / 64.0;
        if (i == 0)
            p.startNewSubPath (xOf (v), yOf (std::pow (v, curve)));
        else
            p.lineTo (xOf (v), yOf (std::pow (v, curve)));
    }
    g.setColour (accent_);
    g.strokePath (p, juce::PathStrokeType (2.0f));
    g.setColour (colours::textDim);
    g.setFont (uiFont (10.0f));
    g.drawText (u8 ("세기(가로)에 따른 크기(세로)"), r.reduced (8.0f, 4.0f).toNearestInt(), juce::Justification::topLeft);
}

void SynthPanel::paintKit (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (colours::text);
    g.setFont (uiFont (12.0f));
    g.drawFittedText (synthKitName (preset_) + u8 ("\n북마다 소리는 합성으로 정해져 있고, 키트 전체의 음높이와 레벨을 바꿉니다."),
                      r.reduced (12.0f, 10.0f).toNearestInt(), juce::Justification::centredLeft, 6);
}

} // namespace daw
