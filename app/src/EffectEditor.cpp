#include "EffectEditor.h"

#include "MixerPanel.h"
#include "MixerText.h"
#include "Theme.h"
#include "composition/Document.h"
#include "effects/Effects.h"
#include "effects/Presets.h"

#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

mr::EffectParameter bandParameter (const std::string& key)
{
    mr::EffectParameter p;
    p.key = key;
    if (key == "freqHz")
    {
        p.minimum = 20.0;
        p.maximum = 20000.0;
        p.defaultValue = 1000.0;
        p.unit = "Hz";
        p.logarithmic = true;
    }
    else if (key == "gainDb")
    {
        p.minimum = -24.0;
        p.maximum = 24.0;
        p.unit = "dB";
    }
    else
    {
        p.minimum = 0.1;
        p.maximum = 18.0;
        p.defaultValue = 0.7071;
        p.unit = "";
        p.logarithmic = true;
    }
    return p;
}

double numberOr (const juce::var& v, double fallback) { return v.isDouble() || v.isInt() || v.isInt64() ? (double) v : fallback; }

// 효과 객체의 automation 에 이 매개변수의 곡선이 있는가
bool automated (const juce::var& effect, const std::string& key)
{
    const auto lane = effect.getProperty ("automation", juce::var()).getProperty (juce::Identifier (key.c_str()), juce::var());
    return lane.isArray() && ! lane.getArray()->isEmpty();
}

const char* const bandTypes[] = { "peak", "lowShelf", "highShelf" };

} // namespace

// ---------------------------------------------------------------------------
// eq 곡선
// ---------------------------------------------------------------------------

void EqCurve::setParams (const juce::var& params)
{
    params_ = params;
    repaint();
}

void EqCurve::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (colours::background);
    g.fillRoundedRectangle (r, 4.0f);
    const double lo = std::log (20.0), hi = std::log (20000.0);
    auto xOf = [&] (double f) { return r.getX() + (float) ((std::log (f) - lo) / (hi - lo)) * r.getWidth(); };
    const float range = 18.0f;
    auto yOf = [&] (double db) { return r.getCentreY() - (float) juce::jlimit (-1.2, 1.2, db / range) * r.getHeight() * 0.45f; };

    g.setFont (monoFont (9.0f));
    for (const double f : { 100.0, 1000.0, 10000.0 })
    {
        g.setColour (colours::beatLine);
        g.fillRect (xOf (f), r.getY(), 1.0f, r.getHeight());
        g.setColour (colours::textFaint);
        g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), (int) xOf (f) + 2, (int) r.getBottom() - 12, 30, 11,
                    juce::Justification::centredLeft);
    }
    for (const double db : { -12.0, 12.0 })
    {
        g.setColour (colours::beatLine);
        g.fillRect (r.getX(), yOf (db), r.getWidth(), 1.0f);
    }
    g.setColour (colours::barLine);
    g.fillRect (r.getX(), yOf (0.0), r.getWidth(), 1.0f);

    std::vector<double> freqs;
    const int points = std::max (32, (int) r.getWidth());
    for (int i = 0; i < points; ++i)
        freqs.push_back (std::exp (lo + (hi - lo) * i / (points - 1)));
    const auto response = mr::eqResponseDb (params_, 48000.0, freqs);
    juce::Path p;
    for (int i = 0; i < points; ++i)
    {
        const float x = xOf (freqs[(size_t) i]), y = yOf (response[(size_t) i]);
        if (i == 0)
            p.startNewSubPath (x, y);
        else
            p.lineTo (x, y);
    }
    const bool bypass = (bool) params_.getProperty ("bypass", false);
    g.setColour (bypass ? colours::textFaint : colours::accent);
    g.strokePath (p, juce::PathStrokeType (1.6f));
    g.setColour (colours::textFaint);
    g.drawText ("+18 dB", r.reduced (4.0f, 2.0f).toNearestInt(), juce::Justification::topLeft);
}

// ---------------------------------------------------------------------------
// 매개변수 칸
// ---------------------------------------------------------------------------

struct EffectEditor::Row
{
    std::string key;
    int band = -1;
    mr::EffectParameter info;
    juce::Label label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ToggleButton> toggle;   // 켜기와 끄기 매개변수, 또는 선택적 매개변수를 쓰는지
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::TextButton> button;
    bool dragging = false;
    bool heading = false;
};

EffectEditor::EffectEditor (MixerPanel& panel, Project& project) : panel_ (panel), project_ (project)
{
    title_.setFont (uiFont (14.0f, true));
    title_.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (title_);
    bypass_.setButtonText (u8 ("우회"));
    bypass_.setTooltip (u8 ("처리하지 않고 지연만 지킵니다."));
    bypass_.onClick = [this]
    {
        if (! updating_)
            panel_.toggleBypass (channel_, index_);
    };
    addAndMakeVisible (bypass_);
    remove_.setButtonText (u8 ("지우기"));
    remove_.onClick = [this] { panel_.removeEffect (channel_, index_); };
    addAndMakeVisible (remove_);
    close_.setButtonText (u8 ("닫기"));
    close_.onClick = [this] { if (onClose) onClose(); };
    preset_.setButtonText (u8 ("프리셋"));
    preset_.setTooltip (u8 ("기본 프리셋과 내 프리셋을 고르거나 지금 값을 프리셋으로 저장합니다"));
    preset_.onClick = [this] { showPresetMenu(); };
    addAndMakeVisible (preset_);
    addAndMakeVisible (close_);
    addChildComponent (curve_);
    viewport_.setViewedComponent (&content_, false);
    viewport_.setScrollBarsShown (true, false);
    viewport_.setScrollBarThickness (8);
    addAndMakeVisible (viewport_);
}

EffectEditor::~EffectEditor() = default;

bool EffectEditor::valid() const
{
    if (! project_.loaded() || index_ < 0)
        return false;
    if (channel_.kind == ChannelRef::Kind::track && channel_.index >= project_.trackCount())
        return false;
    if (channel_.kind == ChannelRef::Kind::bus && channel_.index >= project_.busCount())
        return false;
    return index_ < (int) project_.effectsOf (channel_).size();
}

juce::var EffectEditor::effectJson() const
{
    if (! valid())
        return {};
    return mr::effectsJson (project_.channelJson (channel_))[index_];
}

std::string EffectEditor::effectType() const { return effectJson().getProperty ("type", "").toString().toStdString(); }

juce::String EffectEditor::shapeOf (const juce::var& effect) const
{
    juce::String s = effect.getProperty ("type", "").toString() + "|" + effect.getProperty ("mode", "").toString();
    if (const auto* lanes = effect.getProperty ("automation", juce::var()).getDynamicObject())
        for (const auto& lane : lanes->getProperties())
            s << "|lane:" << lane.name.toString();
    if (const auto* bands = effect.getProperty ("bands", juce::var()).getArray())
        s << "|bands" << bands->size();
    for (const auto& p : mr::effectParameters (effect.getProperty ("type", "").toString().toStdString()))
        if (p.optional)
            s << "|" << p.key << (effect.hasProperty (juce::Identifier (p.key)) ? "1" : "0");
    s << "|" << (int) project_.trackCount();
    return s;
}

void EffectEditor::show (ChannelRef channel, int index)
{
    channel_ = channel;
    index_ = index;
    build();
}

void EffectEditor::refresh()
{
    if (! valid())
        return;
    if (shapeOf (effectJson()) != shape_)
        build();
    else
        updateValues();
}

void EffectEditor::addHeadingRow (const juce::String& text)
{
    auto row = std::make_unique<Row>();
    row->heading = true;
    row->label.setText (text, juce::dontSendNotification);
    row->label.setFont (uiFont (12.5f, true));
    row->label.setColour (juce::Label::textColourId, colours::textDim);
    content_.addAndMakeVisible (row->label);
    rows_.push_back (std::move (row));
}

void EffectEditor::addNumberRow (const std::string& key, const mr::EffectParameter& info, const juce::String& label, int band)
{
    auto row = std::make_unique<Row>();
    auto* r = row.get();
    r->key = key;
    r->band = band;
    r->info = info;
    r->label.setText (label, juce::dontSendNotification);
    r->label.setFont (uiFont (12.5f));
    r->label.setColour (juce::Label::textColourId, colours::text);
    r->label.setTooltip (parameterTooltip (effectType(), key));
    content_.addAndMakeVisible (r->label);

    if (info.toggle)
    {
        r->toggle = std::make_unique<juce::ToggleButton>();
        r->toggle->setTooltip (r->label.getTooltip());
        r->toggle->onClick = [this, r]
        {
            if (! updating_)
                commit (r->key, r->toggle->getToggleState() ? 1 : 0, r->band, r->label.getText());
        };
        content_.addAndMakeVisible (*r->toggle);
        rows_.push_back (std::move (row));
        return;
    }
    if (info.optional)
    {
        r->toggle = std::make_unique<juce::ToggleButton>();
        r->toggle->setTooltip (u8 ("켜면 이 값을 씁니다."));
        r->toggle->onClick = [this, r]
        {
            if (updating_)
                return;
            if (effectType() == "delay" && r->key == "timeBeats")
            {
                setDelayBeats (r->toggle->getToggleState(), roundParameter (r->info, r->slider->getValue()), r->label.getText());
                return;
            }
            if (r->key == "rateBeats" && mr::findEffectParameter (effectType(), "rateHz") != nullptr)
            {
                setRateBeats (r->toggle->getToggleState(), roundParameter (r->info, r->slider->getValue()), r->label.getText());
                return;
            }
            if (r->toggle->getToggleState())
                commit (r->key, roundParameter (r->info, r->slider->getValue()), r->band, r->label.getText());
            else
                commit (r->key, juce::var(), r->band, r->label.getText());
        };
        content_.addAndMakeVisible (*r->toggle);
    }

    r->slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
    auto* s = r->slider.get();
    const auto p = info;
    if (p.logarithmic && p.minimum > 0.0)
        s->setNormalisableRange (juce::NormalisableRange<double> (
            p.minimum, p.maximum,
            [] (double lo, double hi, double x) { return lo * std::pow (hi / lo, x); },
            [] (double lo, double hi, double v) { return std::log (v / lo) / std::log (hi / lo); },
            [p] (double, double, double v) { return roundParameter (p, v); }));
    else
        s->setNormalisableRange (juce::NormalisableRange<double> (
            p.minimum, p.maximum,
            [] (double lo, double hi, double x) { return lo + (hi - lo) * x; },
            [] (double lo, double hi, double v) { return (v - lo) / (hi - lo); },
            [p] (double, double, double v) { return roundParameter (p, v); }));
    s->textFromValueFunction = [p] (double v) { return formatParameter (p, v); };
    s->valueFromTextFunction = [p] (const juce::String& t) { return parseParameter (p, t); };
    s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 22);
    s->setTextBoxIsEditable (true);
    s->updateText();
    s->setDoubleClickReturnValue (true, p.defaultValue);
    s->setTooltip (r->label.getTooltip());
    s->onDragStart = [r] { r->dragging = true; };
    s->onValueChange = [this, r]
    {
        if (updating_)
            return;
        const double v = roundParameter (r->info, r->slider->getValue());
        if (r->info.optional && ! r->toggle->getToggleState())
            return;
        if (r->dragging)
            preview (r->key, v, r->band);
        else
            commit (r->key, v, r->band, r->label.getText());
    };
    s->onDragEnd = [this, r]
    {
        r->dragging = false;
        if (r->info.optional && ! r->toggle->getToggleState())
            return;
        commit (r->key, roundParameter (r->info, r->slider->getValue()), r->band, r->label.getText());
    };
    content_.addAndMakeVisible (*s);
    rows_.push_back (std::move (row));
}

void EffectEditor::addChoiceRow (const std::string& key, const juce::String& label, const juce::StringArray& items, int band)
{
    auto row = std::make_unique<Row>();
    auto* r = row.get();
    r->key = key;
    r->band = band;
    r->label.setText (label, juce::dontSendNotification);
    r->label.setFont (uiFont (12.5f));
    r->label.setColour (juce::Label::textColourId, colours::text);
    content_.addAndMakeVisible (r->label);
    r->combo = std::make_unique<juce::ComboBox>();
    r->combo->addItemList (items, 1);
    r->combo->onChange = [this, r]
    {
        if (updating_)
            return;
        const int i = r->combo->getSelectedItemIndex();
        juce::var value;
        if (r->key == "mode" && effectType() == "filter")
            value = i <= 0 ? juce::var() : juce::var (juce::String (mr::filterModes()[(size_t) juce::jlimit (0, 3, i)]));
        else if (r->key == "mode")
            value = i == 1 ? juce::var ("algorithmic") : juce::var();
        else if (r->key == "sidechain")
            value = i <= 0 ? juce::var() : juce::var (r->combo->getItemText (i));
        else if (r->key == "type")
            value = juce::String (bandTypes[juce::jlimit (0, 2, i)]);
        else if (r->key == "shape")
            value = i <= 0 ? juce::var() : juce::var (juce::String (mr::tremoloShapes()[(size_t) juce::jlimit (0, 2, i)]));
        else if (! r->info.choices.empty())
            value = r->info.choices[(size_t) juce::jlimit (0, (int) r->info.choices.size() - 1, i)];
        else
            value = r->combo->getItemText (i).getIntValue();
        commit (r->key, value, r->band, r->label.getText());
    };
    content_.addAndMakeVisible (*r->combo);
    rows_.push_back (std::move (row));
}

void EffectEditor::addButtonRow (const juce::String& text, std::function<void()> click)
{
    auto row = std::make_unique<Row>();
    row->button = std::make_unique<juce::TextButton> (text);
    row->button->onClick = std::move (click);
    content_.addAndMakeVisible (*row->button);
    rows_.push_back (std::move (row));
}

void EffectEditor::build()
{
    rows_.clear();
    content_.removeAllChildren();
    if (! valid())
        return;
    const auto effect = effectJson();
    const auto type = effectType();
    shape_ = shapeOf (effect);

    if (type == "reverb")
        addChoiceRow ("mode", u8 ("방식"), { u8 ("컨볼루션 (만든 홀 IR)"), u8 ("알고리즘") });
    if (type == "filter")
        addChoiceRow ("mode", u8 ("방식"), { u8 ("저역 통과"), u8 ("고역 통과"), u8 ("대역 통과"), u8 ("노치") });
    if (type == "tremolo")
        addChoiceRow ("shape", u8 ("모양"), { u8 ("사인"), u8 ("삼각"), u8 ("사각") });
    if (type == "compressor")
    {
        juce::StringArray items { u8 ("없음 (자기 신호)") };
        for (size_t t = 0; t < project_.trackCount(); ++t)
            if (! (channel_.kind == ChannelRef::Kind::track && channel_.index == t))
                items.add (juce::String::fromUTF8 (project_.composition().tracks[t].name.c_str()));
        addChoiceRow ("sidechain", u8 ("사이드체인 키"), items);
    }
    const auto mode = effect.getProperty ("mode", "convolution").toString().toStdString();
    for (const auto& p : mr::effectParameters (type))
    {
        if (p.hidden || (! p.mode.empty() && p.mode != mode))
            continue;
        if (! p.choices.empty())
        {
            juce::StringArray items;
            for (const double c : p.choices)
                items.add (formatParameter (p, c));
            addChoiceRow (p.key, parameterLabel (type, p.key), items);
            rows_.back()->info = p;
        }
        else
            addNumberRow (p.key, p, parameterLabel (type, p.key) + (automated (effect, p.key) ? u8 (" (곡선)") : juce::String()));
    }
    if (type == "eq")
    {
        const auto* bands = effect.getProperty ("bands", juce::var()).getArray();
        const int count = bands != nullptr ? bands->size() : 0;
        for (int b = 0; b < count; ++b)
        {
            addHeadingRow (u8 ("대역 ") + juce::String (b + 1));
            auto* heading = rows_.back().get();
            heading->band = b;
            heading->button = std::make_unique<juce::TextButton> (u8 ("지우기"));
            heading->button->onClick = [this, b]
            {
                auto effects = mr::effectsJson (project_.channelJson (channel_));
                auto e = effects[index_];
                auto list = e.getProperty ("bands", juce::var()).clone();
                if (auto* arr = list.getArray(); arr != nullptr && b < arr->size())
                    arr->remove (b);
                effects.getArray()->set (index_, mr::withEffectParameter (e, "bands", list.getArray()->isEmpty() ? juce::var() : list));
                commitEffects (*effects.getArray(), u8 ("EQ 대역 지우기"));
            };
            content_.addAndMakeVisible (*heading->button);
            addChoiceRow ("type", u8 ("모양"), { bandTypeName ("peak"), bandTypeName ("lowShelf"), bandTypeName ("highShelf") }, b);
            for (const char* key : { "freqHz", "gainDb", "q" })
                addNumberRow (key, bandParameter (key), parameterLabel ("band", key), b);
        }
        addButtonRow (u8 ("대역 추가"), [this]
        {
            auto effects = mr::effectsJson (project_.channelJson (channel_));
            auto e = effects[index_];
            auto list = e.getProperty ("bands", juce::var()).clone();
            if (list.getArray() == nullptr)
                list = juce::var (juce::Array<juce::var>());
            list.append (juce::JSON::parse (R"({ "type": "peak", "freqHz": 1000, "gainDb": 0, "q": 1 })"));
            effects.getArray()->set (index_, mr::withEffectParameter (e, "bands", list));
            commitEffects (*effects.getArray(), u8 ("EQ 대역 추가"));
        });
    }
    if (type == "reverb" && effect.hasProperty ("ir"))
        addHeadingRow (u8 ("IR 파일: ") + effect.getProperty ("ir", "").toString());
    curve_.setVisible (type == "eq");
    updateValues();
    resized();
}

void EffectEditor::updateValues()
{
    if (! valid())
        return;
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    const auto effect = effectJson();
    const auto type = effectType();
    const auto& specs = project_.effectsOf (channel_);
    title_.setText (project_.channelName (channel_) + ": " + effectName (type) + " (" + juce::String (index_ + 1) + "/" + juce::String ((int) specs.size()) + ")",
                    juce::dontSendNotification);
    bypass_.setToggleState ((bool) effect.getProperty ("bypass", false), juce::dontSendNotification);
    curve_.setParams (effect);
    const auto* bands = effect.getProperty ("bands", juce::var()).getArray();
    for (auto& rp : rows_)
    {
        auto& r = *rp;
        const juce::var source = r.band >= 0 && bands != nullptr && r.band < bands->size() ? (*bands)[r.band] : effect;
        const juce::Identifier key (r.key.empty() ? "_" : r.key.c_str());
        if (r.slider != nullptr && ! r.dragging)
        {
            const bool present = source.hasProperty (key);
            r.slider->setValue (numberOr (source.getProperty (key, juce::var()), r.info.defaultValue), juce::dontSendNotification);
            if (r.info.optional)
            {
                r.toggle->setToggleState (present, juce::dontSendNotification);
                r.slider->setEnabled (present);
            }
            if (r.band < 0 && automated (effect, r.key))
            {
                r.slider->setEnabled (false);   // 곡선이 값을 정한다 (손잡이는 곡선이 없을 때의 값)
                r.slider->setTooltip (u8 ("곡의 곡선(automation)이 이 값을 정합니다. 손잡이는 곡선이 없을 때의 값을 보이고, 곡선을 지우기 전에는 바꿀 수 없습니다."));
                r.label.setTooltip (r.slider->getTooltip());
            }
            if (type == "delay" && r.key == "timeMs" && effect.hasProperty ("timeBeats"))
                r.slider->setEnabled (false);   // 박으로 정한 지연이 시간을 대신한다
            if (r.key == "rateHz" && effect.hasProperty ("rateBeats"))
                r.slider->setEnabled (false);   // 박으로 정한 주기가 빠르기를 대신한다
        }
        else if (r.toggle != nullptr && r.info.toggle)
            r.toggle->setToggleState (numberOr (source.getProperty (key, juce::var()), r.info.defaultValue) >= 0.5, juce::dontSendNotification);
        else if (r.combo != nullptr)
        {
            const auto v = source.getProperty (key, juce::var());
            int selected = 0;
            if (r.key == "mode" && type == "filter")
            {
                const auto& modes = mr::filterModes();
                for (size_t m = 0; m < modes.size(); ++m)
                    if (v.toString().toStdString() == modes[m])
                        selected = (int) m;
            }
            else if (r.key == "mode")
                selected = v.toString() == "algorithmic" ? 1 : 0;
            else if (r.key == "sidechain")
            {
                for (int i = 1; i < r.combo->getNumItems(); ++i)
                    if (r.combo->getItemText (i) == v.toString())
                        selected = i;
            }
            else if (r.key == "type")
            {
                for (int i = 0; i < 3; ++i)
                    if (v.toString() == bandTypes[i])
                        selected = i;
            }
            else if (r.key == "shape")
            {
                const auto& shapes = mr::tremoloShapes();
                for (size_t m = 0; m < shapes.size(); ++m)
                    if (v.toString().toStdString() == shapes[m])
                        selected = (int) m;
            }
            else if (! r.info.choices.empty())
            {
                // 값에 가장 가까운 선택지
                const double x = numberOr (v, r.info.defaultValue);
                for (size_t c = 0; c < r.info.choices.size(); ++c)
                    if (std::abs (r.info.choices[c] - x) < std::abs (r.info.choices[(size_t) selected] - x))
                        selected = (int) c;
            }
            r.combo->setSelectedItemIndex (selected, juce::dontSendNotification);
        }
    }
}

juce::Array<juce::var> EffectEditor::withValue (const std::string& key, const juce::var& value, int band) const
{
    auto effects = mr::effectsJson (project_.channelJson (channel_));
    auto e = effects[index_];
    if (band < 0)
        e = mr::withEffectParameter (e, juce::Identifier (key), value);
    else
    {
        auto list = e.getProperty ("bands", juce::var()).clone();
        if (auto* arr = list.getArray(); arr != nullptr && band < arr->size())
            arr->set (band, mr::withEffectParameter ((*arr)[band], juce::Identifier (key), value));
        e = mr::withEffectParameter (e, "bands", list);
    }
    effects.getArray()->set (index_, e);
    return *effects.getArray();
}

void EffectEditor::preview (const std::string& key, const juce::var& value, int band)
{
    if (! valid())
        return;
    const auto effects = withValue (key, value, band);
    curve_.setParams (effects[index_]);
    panel_.previewEffects (channel_, effects);
}

void EffectEditor::commit (const std::string& key, const juce::var& value, int band, const juce::String& what)
{
    if (! valid())
        return;
    const auto effects = withValue (key, value, band);
    if (juce::JSON::toString (effects[index_], true) == juce::JSON::toString (effectJson(), true))
        return;
    commitEffects (effects, effectName (effectType()) + " " + what);
}

void EffectEditor::commitEffects (juce::Array<juce::var> effects, const juce::String& what)
{
    panel_.commitEffects (channel_, effects, u8 ("효과 매개변수: ") + what);
}

void EffectEditor::setDelayBeats (bool on, double beats, const juce::String& what)
{
    if (! valid())
        return;
    auto effects = mr::effectsJson (project_.channelJson (channel_));
    auto e = effects[index_];
    if (on)
    {
        // 두 값이 함께 있으면 timeMs 는 쓰이지 않는다 (검사가 경고한다)
        e = mr::withEffectParameter (e, "timeBeats", beats);
        e = mr::withEffectParameter (e, "timeMs", juce::var());
    }
    else
    {
        const auto& tempo = project_.composition().tempo;
        const double bpm = tempo.empty() ? 120.0 : tempo.front().bpm;
        if (const auto* info = mr::findEffectParameter ("delay", "timeMs"))
            e = mr::withEffectParameter (e, "timeMs", roundParameter (*info, juce::jlimit (info->minimum, info->maximum, beats * 60000.0 / bpm)));
        e = mr::withEffectParameter (e, "timeBeats", juce::var());
    }
    effects.getArray()->set (index_, e);
    commitEffects (*effects.getArray(), effectName ("delay") + " " + what);
}

void EffectEditor::setRateBeats (bool on, double beats, const juce::String& what)
{
    if (! valid())
        return;
    const auto type = effectType();
    auto effects = mr::effectsJson (project_.channelJson (channel_));
    auto e = effects[index_];
    if (on)
    {
        // 두 값이 함께 있으면 rateHz 는 쓰이지 않는다 (검사가 경고한다)
        e = mr::withEffectParameter (e, "rateBeats", beats);
        e = mr::withEffectParameter (e, "rateHz", juce::var());
    }
    else
    {
        // 첫 템포에서 같은 주기의 빠르기
        const auto& tempo = project_.composition().tempo;
        const double bpm = tempo.empty() ? 120.0 : tempo.front().bpm;
        if (const auto* info = mr::findEffectParameter (type, "rateHz"))
            e = mr::withEffectParameter (e, "rateHz", roundParameter (*info, juce::jlimit (info->minimum, info->maximum, bpm / (60.0 * beats))));
        e = mr::withEffectParameter (e, "rateBeats", juce::var());
    }
    effects.getArray()->set (index_, e);
    commitEffects (*effects.getArray(), effectName (type) + " " + what);
}

std::vector<std::pair<std::string, juce::Slider*>> EffectEditor::learnableSliders()
{
    std::vector<std::pair<std::string, juce::Slider*>> out;
    for (auto& r : rows_)
        if (r->slider != nullptr && r->band < 0 && r->slider->isVisible()
            && viewport_.getViewArea().intersects (r->slider->getBoundsInParent()))   // 스크롤로 가려지지 않은 줄
            out.push_back ({ r->key, r->slider.get() });
    return out;
}

juce::File EffectEditor::userPresetFolder() const
{
    if (panel_.presetFolder == juce::File() || ! valid())
        return {};
    return panel_.presetFolder.getChildFile ("Effects").getChildFile (juce::String (effectType()));
}

bool EffectEditor::applyPresetObject (const juce::var& preset, const juce::String& name)
{
    if (! valid() || preset.getProperty ("type", "").toString().toStdString() != effectType())
        return false;
    auto effects = mr::effectsJson (project_.channelJson (channel_));
    effects.getArray()->set (index_, mr::withPreset (effects[index_], preset));
    return panel_.commitEffects (channel_, *effects.getArray(), u8 ("프리셋: ") + name);
}

bool EffectEditor::applyFactoryPreset (const std::string& id)
{
    if (! valid())
        return false;
    for (const auto& p : mr::effectPresets (effectType()))
        if (p.id == id)
            return applyPresetObject (p.effect, effectName (effectType()) + " " + presetName (id));
    return false;
}

juce::StringArray EffectEditor::userPresets() const
{
    juce::StringArray names;
    const auto folder = userPresetFolder();
    if (folder.isDirectory())
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
            names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool EffectEditor::applyUserPreset (const juce::String& name)
{
    const auto file = userPresetFolder().getChildFile (name + ".json");
    if (! file.existsAsFile())
        return false;
    const auto preset = juce::JSON::parse (file);
    if (! applyPresetObject (preset, name))
    {
        panel_.status (u8 ("이 효과의 프리셋이 아닙니다: ") + file.getFileName(), true);
        return false;
    }
    return true;
}

bool EffectEditor::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    const auto folder = userPresetFolder();
    if (name.isEmpty() || folder == juce::File())
        return false;
    // 지금 값 (우회, 주석, 사이드체인 키, 곡선은 프리셋에 넣지 않는다)
    auto effect = effectJson().clone();
    if (auto* o = effect.getDynamicObject())
        for (const char* key : { "bypass", "comment", "sidechain", "automation" })
            o->removeProperty (key);
    if (! folder.createDirectory() || ! folder.getChildFile (name + ".json").replaceWithText (juce::JSON::toString (effect)))
    {
        panel_.status (u8 ("프리셋을 쓰지 못했습니다: ") + folder.getFullPathName(), true);
        return false;
    }
    panel_.status (u8 ("프리셋을 저장했습니다: ") + effectName (effectType()) + " " + name);
    return true;
}

bool EffectEditor::deleteUserPreset (const juce::String& name)
{
    return userPresetFolder().getChildFile (name + ".json").deleteFile();
}

void EffectEditor::showPresetMenu()
{
    if (! valid())
        return;
    juce::PopupMenu m;
    const auto& factory = mr::effectPresets (effectType());
    if (! factory.empty())
    {
        m.addSectionHeader (u8 ("기본 프리셋"));
        for (const auto& p : factory)
            m.addItem (presetName (p.id), [this, id = p.id] { applyFactoryPreset (id); });
    }
    const auto mine = userPresets();
    if (! mine.isEmpty())
    {
        m.addSectionHeader (u8 ("내 프리셋"));
        for (const auto& name : mine)
            m.addItem (name, [this, name] { applyUserPreset (name); });
    }
    m.addSeparator();
    const bool canSave = userPresetFolder() != juce::File();
    m.addItem (u8 ("지금 값을 프리셋으로 저장..."), canSave, false, [this]
    {
        auto dialog = std::make_shared<juce::AlertWindow> (u8 ("프리셋 저장"), u8 ("이 효과의 지금 값을 내 프리셋으로 저장합니다."), juce::MessageBoxIconType::NoIcon, this);
        dialog->addTextEditor ("name", {});
        dialog->addButton (u8 ("저장"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog->addButton (u8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        dialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<EffectEditor> (this), dialog] (int result)
        {
            if (safe != nullptr && result == 1)
                safe->saveUserPreset (dialog->getTextEditorContents ("name"));
        }), false);
    });
    if (! mine.isEmpty())
    {
        juce::PopupMenu remove;
        for (const auto& name : mine)
            remove.addItem (name, [this, name] { deleteUserPreset (name); });
        m.addSubMenu (u8 ("내 프리셋 지우기"), remove);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&preset_));
}

juce::Slider* EffectEditor::slider (const juce::String& key)
{
    const auto band = key.startsWith ("bands.") ? key.fromFirstOccurrenceOf ("bands.", false, false).getIntValue() : -1;
    const auto name = band >= 0 ? key.fromLastOccurrenceOf (".", false, false) : key;
    for (auto& r : rows_)
        if (r->slider != nullptr && r->band == band && juce::String (r->key) == name)
            return r->slider.get();
    return nullptr;
}

juce::ComboBox* EffectEditor::combo (const juce::String& key)
{
    for (auto& r : rows_)
        if (r->combo != nullptr && r->band < 0 && juce::String (r->key) == key)
            return r->combo.get();
    return nullptr;
}

juce::String EffectEditor::rowLabel (const juce::String& key) const
{
    for (const auto& r : rows_)
        if (! r->heading && r->band < 0 && juce::String (r->key) == key)
            return r->label.getText();
    return {};
}

juce::Button* EffectEditor::toggle (const juce::String& key)
{
    if (key == "bypass")
        return &bypass_;
    for (auto& r : rows_)
        if (r->toggle != nullptr && r->band < 0 && juce::String (r->key) == key)
            return r->toggle.get();
    return nullptr;
}

void EffectEditor::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::outline);
    g.fillRect (0, 0, 1, getHeight());
}

void EffectEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    title_.setBounds (r.removeFromTop (24));
    auto buttons = r.removeFromTop (28);
    bypass_.setBounds (buttons.removeFromLeft (80));
    close_.setBounds (buttons.removeFromRight (60).reduced (0, 2));
    buttons.removeFromRight (6);
    remove_.setBounds (buttons.removeFromRight (64).reduced (0, 2));
    buttons.removeFromRight (4);
    preset_.setBounds (buttons.removeFromRight (64).reduced (0, 2));
    r.removeFromTop (6);
    if (curve_.isVisible())
    {
        curve_.setBounds (r.removeFromTop (120));
        r.removeFromTop (6);
    }
    viewport_.setBounds (r);
    layoutRows();
}

void EffectEditor::layoutRows()
{
    const int w = viewport_.getWidth() - viewport_.getScrollBarThickness() - 2;
    int y = 0;
    for (auto& rp : rows_)
    {
        auto& row = *rp;
        auto line = juce::Rectangle<int> (0, y, w, 30);
        if (row.heading)
        {
            line.removeFromTop (6);
            if (row.button != nullptr)
                row.button->setBounds (line.removeFromRight (60).reduced (0, 1));
            row.label.setBounds (line);
        }
        else if (row.button != nullptr)
            row.button->setBounds (line.withSizeKeepingCentre (120, 24));
        else
        {
            if (row.toggle != nullptr && ! row.info.toggle)
                row.toggle->setBounds (line.removeFromLeft (24));
            row.label.setBounds (line.removeFromLeft (row.toggle != nullptr && ! row.info.toggle ? 100 : 124));
            if (row.slider != nullptr)
                row.slider->setBounds (line.reduced (0, 3));
            else if (row.combo != nullptr)
                row.combo->setBounds (line.reduced (0, 3));
            else if (row.toggle != nullptr)
                row.toggle->setBounds (line.removeFromLeft (40));
        }
        y += 30;
    }
    content_.setSize (w, y + 8);
}

} // namespace daw
