#include "MixerPanel.h"

#include "plugins/PluginHost.h"

#include "EffectEditor.h"
#include "MixerText.h"
#include "Theme.h"
#include "composition/Document.h"

#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

const juce::Colour busColour { 0xff3fb8a8 };

float gainToDb (float gain) { return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f; }

mr::MixerSettings& mixerIn (mr::Composition& c, ChannelRef channel)
{
    switch (channel.kind)
    {
        case ChannelRef::Kind::track: return c.tracks.at (channel.index).mixer;
        case ChannelRef::Kind::bus: return c.buses.at (channel.index).mixer;
        case ChannelRef::Kind::master: break;
    }
    return c.master.mixer;
}

std::vector<mr::EffectSpec>& effectsIn (mr::Composition& c, ChannelRef channel)
{
    switch (channel.kind)
    {
        case ChannelRef::Kind::track: return c.tracks.at (channel.index).effects;
        case ChannelRef::Kind::bus: return c.buses.at (channel.index).effects;
        case ChannelRef::Kind::master: break;
    }
    return c.master.effects;
}

std::string basePathOf (const mr::Composition& c, ChannelRef channel)
{
    switch (channel.kind)
    {
        case ChannelRef::Kind::track: return c.tracks.at (channel.index).path;
        case ChannelRef::Kind::bus: return c.buses.at (channel.index).path;
        case ChannelRef::Kind::master: break;
    }
    return "$.master";
}

bool sameMixer (const mr::MixerSettings& a, const mr::MixerSettings& b)
{
    return a.gainDb == b.gainDb && a.pan == b.pan && a.mute == b.mute && a.solo == b.solo;
}

} // namespace

// ---------------------------------------------------------------------------
// 미터
// ---------------------------------------------------------------------------

bool MeterState::update (mr::StereoPeak peak, double now)
{
    const double dt = last > 0.0 ? juce::jlimit (0.0, 0.5, now - last) : 0.0;
    last = now;
    const float fall = (float) (24.0 * dt);
    const float values[2] { peak.left, peak.right };
    bool changed = false;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float db = gainToDb (values[ch]);
        const float next = std::max (-120.0f, db >= level[ch] ? db : std::max (db, level[ch] - fall));
        changed |= std::abs (next - level[ch]) > 0.01f;
        level[ch] = next;
        if (db >= hold[ch])
        {
            changed |= db != hold[ch];
            hold[ch] = db;
            holdUntil[ch] = now + 1.5;
        }
        else if (now > holdUntil[ch])
        {
            const float h = std::max (level[ch], hold[ch] - fall);
            changed |= h != hold[ch];
            hold[ch] = h;
        }
        if (values[ch] > maxPeak)
        {
            maxPeak = values[ch];
            changed = true;
        }
        if (values[ch] >= 1.0f && ! clipped)
        {
            clipped = true;
            changed = true;
        }
    }
    return changed;
}

void MeterState::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        level[ch] = hold[ch] = -120.0f;
        holdUntil[ch] = 0.0;
    }
    maxPeak = 0.0f;
    clipped = false;
}

// ---------------------------------------------------------------------------
// 채널 스트립
// ---------------------------------------------------------------------------

ChannelStrip::ChannelStrip (MixerPanel& panel, Project& project, ChannelRef channel)
    : panel_ (panel), project_ (project), channel_ (channel)
{
    juce::NormalisableRange<double> range (
        faderlaw::minDb, faderlaw::maxDb,
        [] (double, double, double p) { return faderlaw::toDb (p); },
        [] (double, double, double db) { return faderlaw::toPosition (db); },
        [] (double, double, double db) { return db <= faderlaw::minDb + 0.05 ? faderlaw::minDb : std::round (db * 10.0) / 10.0; });
    fader_.setSliderStyle (juce::Slider::LinearVertical);
    fader_.setNormalisableRange (range);
    fader_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader_.setDoubleClickReturnValue (true, 0.0);
    fader_.setSliderSnapsToMousePosition (false);
    fader_.setTooltip (u8 ("게인 (두 번 클릭하면 0 dB)"));
    fader_.onDragStart = [this] { faderDragging_ = true; beginWriting ("gainDb"); };
    fader_.onValueChange = [this]
    {
        if (updating_)
            return;
        repaint (0, getHeight() - buttonHeight - readoutHeight - 2, getWidth(), readoutHeight);
        if (faderDragging_)
        {
            auto m = faderSettings();
            if (writing_)
            {
                recordWriting (m.gainDb);
                m.gainLane = {};   // 쓰는 동안은 손의 값이 들린다
            }
            panel_.previewMixer (channel_, m);
        }
        else
            commitFader (u8 ("게인"));
    };
    fader_.onDragEnd = [this]
    {
        faderDragging_ = false;
        if (! finishWriting())
            commitFader (u8 ("게인"));
    };
    addAndMakeVisible (fader_);

    pan_.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pan_.setRange (-1.0, 1.0, 0.01);
    pan_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    pan_.setDoubleClickReturnValue (true, 0.0);
    pan_.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    pan_.setMouseDragSensitivity (160);
    pan_.setTooltip (u8 ("밸런스 (두 번 클릭하면 가운데)"));
    pan_.onDragStart = [this] { panDragging_ = true; beginWriting ("pan"); };
    pan_.onValueChange = [this]
    {
        if (updating_)
            return;
        repaint();
        if (panDragging_)
        {
            auto m = faderSettings();
            if (writing_)
            {
                recordWriting (m.pan);
                m.panLane = {};
            }
            panel_.previewMixer (channel_, m);
        }
        else
            commitFader (u8 ("밸런스"));
    };
    pan_.onDragEnd = [this]
    {
        panDragging_ = false;
        if (! finishWriting())
            commitFader (u8 ("밸런스"));
    };
    addAndMakeVisible (pan_);

    for (auto* b : { &mute_, &solo_ })
    {
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        addAndMakeVisible (*b);
    }
    mute_.setColour (juce::TextButton::buttonOnColourId, colours::mute);
    solo_.setColour (juce::TextButton::buttonOnColourId, colours::solo);
    mute_.setTooltip (u8 ("뮤트"));
    solo_.setTooltip (u8 ("솔로"));
    mute_.onClick = [this]
    {
        auto m = project_.mixerOf (channel_);
        m.mute = ! m.mute;
        panel_.commitMixer (channel_, m, u8 (m.mute ? "뮤트" : "뮤트 풀기") + ": " + project_.channelName (channel_));
    };
    solo_.onClick = [this]
    {
        auto m = project_.mixerOf (channel_);
        m.solo = ! m.solo;
        panel_.commitMixer (channel_, m, u8 (m.solo ? "솔로" : "솔로 풀기") + ": " + project_.channelName (channel_));
    };
    solo_.setVisible (isTrack());
    mute_.setVisible (! isMaster());
}

void ChannelStrip::setRows (int effectRows, int sendRows)
{
    if (effectRows == effectRows_ && sendRows == sendRows_)
        return;
    effectRows_ = effectRows;
    sendRows_ = sendRows;
    resized();
    repaint();
}

void ChannelStrip::showAutomationAt (double beat)
{
    if (! project_.loaded())
        return;
    const auto& m = project_.mixerOf (channel_);
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    if (! m.gainLane.empty())
    {
        const double db = std::round (m.gainLane.valueAt (beat) * 10.0) / 10.0;
        if (db != fader_.getValue())
        {
            fader_.setValue (db, juce::dontSendNotification);
            repaint();
        }
    }
    if (! m.panLane.empty())
        pan_.setValue (m.panLane.valueAt (beat), juce::dontSendNotification);
}

void ChannelStrip::update()
{
    if (! project_.loaded())
        return;
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    const auto& m = project_.mixerOf (channel_);
    if (! faderDragging_)
        fader_.setValue (m.gainLane.empty() ? m.gainDb : m.gainLane.valueAt (0.0), juce::dontSendNotification);
    if (! panDragging_)
        pan_.setValue (m.panLane.empty() ? m.pan : m.panLane.valueAt (0.0), juce::dontSendNotification);
    // 곡선이 정하는 값은 끌어서 바꾸지 않는다 (재생 위치를 따라 움직인다). 곡선 쓰기 모드에서는 끌어서 다시 쓴다
    const bool writable = panel_.writeMode;
    fader_.setEnabled (m.gainLane.empty() || writable);
    pan_.setEnabled (m.panLane.empty() || writable);
    fader_.setTooltip (m.gainLane.empty() ? u8 ("게인 (두 번 클릭하면 0 dB)")
                                          : u8 ("곡의 곡선(automation)이 게인을 정합니다. 피아노 롤 아래 칸의 볼륨 곡선을 고치거나 지웁니다."));
    pan_.setTooltip (m.panLane.empty() ? u8 ("밸런스 (두 번 클릭하면 가운데)")
                                       : u8 ("곡의 곡선(automation)이 밸런스를 정합니다. 피아노 롤 아래 칸의 밸런스 곡선을 고치거나 지웁니다."));
    mute_.setToggleState (m.mute, juce::dontSendNotification);
    solo_.setToggleState (m.solo, juce::dontSendNotification);
    solo_.setVisible (isTrack());
    mute_.setVisible (! isMaster());
    setTooltip (project_.channelName (channel_));
    repaint();
}

void ChannelStrip::setSelected (bool selected)
{
    if (selected == selected_)
        return;
    selected_ = selected;
    repaint (nameArea());
}

void ChannelStrip::setLevel (mr::StereoPeak peak, double now)
{
    if (meter_.update (peak, now))
        repaint (meterArea().getUnion (peakArea()).expanded (2));
}

void ChannelStrip::clearLevel()
{
    meter_.reset();
    repaint();
}

void ChannelStrip::beginWriting (const std::string& parameter)
{
    writing_.reset();
    if (! panel_.writeBeat || ! panel_.writeBeat())
        return;
    const auto& m = project_.mixerOf (channel_);
    Writing w;
    w.parameter = parameter;
    w.before = parameter == "pan" ? m.panLane : m.gainLane;
    w.fixed = parameter == "pan" ? m.pan : m.gainDb;
    writing_ = std::move (w);
    recordWriting (parameter == "pan" ? faderSettings().pan : faderSettings().gainDb);
}

void ChannelStrip::recordWriting (double value)
{
    if (! writing_ || ! panel_.writeBeat)
        return;
    const auto beat = panel_.writeBeat();
    if (! beat)
        return;
    auto& points = writing_->points;
    if (! points.empty() && *beat <= points.back().first)
        points.back().second = value;   // 같은 자리 (멈췄거나 반복으로 돌아왔다)
    else
        points.push_back ({ *beat, value });
}

bool ChannelStrip::finishWriting()
{
    if (! writing_)
        return false;
    auto w = std::move (*writing_);
    writing_.reset();
    recordWriting (w.parameter == "pan" ? faderSettings().pan : faderSettings().gainDb);
    if (w.points.empty())
        return false;
    return panel_.commitWrittenLane (channel_, w.parameter, w.before, w.fixed, w.points);
}

mr::MixerSettings ChannelStrip::faderSettings() const
{
    auto m = project_.mixerOf (channel_);
    const double db = fader_.getValue();
    m.gainDb = db <= faderlaw::minDb + 0.05 ? faderlaw::minDb : std::round (db * 10.0) / 10.0;
    m.pan = std::round (pan_.getValue() * 100.0) / 100.0;
    if (std::abs (m.gainDb) < 1.0e-9)
        m.gainDb = 0.0;
    if (std::abs (m.pan) < 1.0e-9)
        m.pan = 0.0;
    return m;
}

void ChannelStrip::commitFader (const juce::String& what)
{
    const auto m = faderSettings();
    if (sameMixer (m, project_.mixerOf (channel_)))
        return;
    panel_.commitMixer (channel_, m, what + ": " + project_.channelName (channel_));
}

// ---- 자리 ----

juce::Rectangle<int> ChannelStrip::effectSlot (int row) const
{
    return { 4, nameHeight + 4 + row * slotHeight, getWidth() - 8, slotHeight - 2 };
}

juce::Rectangle<int> ChannelStrip::sendSlot (int row) const
{
    const int top = nameHeight + 4 + effectRows_ * slotHeight + 6;
    return { 4, top + row * slotHeight, getWidth() - 8, slotHeight - 2 };
}

juce::Rectangle<int> ChannelStrip::faderArea() const
{
    const int top = nameHeight + 4 + (effectRows_ + sendRows_) * slotHeight + 12 + panHeight;
    const int bottom = getHeight() - buttonHeight - readoutHeight - 6;
    return { 6, top, 28, std::max (40, bottom - top) };
}

juce::Rectangle<int> ChannelStrip::meterArea() const
{
    const auto f = faderArea();
    return { getWidth() - 26, f.getY(), 18, f.getHeight() };
}

juce::Rectangle<int> ChannelStrip::peakArea() const
{
    return { getWidth() / 2, getHeight() - buttonHeight - readoutHeight - 2, getWidth() / 2 - 4, readoutHeight - 2 };
}

int ChannelStrip::effectAt (juce::Point<int> p) const
{
    for (int r = 0; r < effectRows_; ++r)
        if (effectSlot (r).contains (p))
            return r;
    return -1;
}

int ChannelStrip::sendAt (juce::Point<int> p) const
{
    if (! isTrack())
        return -1;
    for (int r = 0; r < sendRows_; ++r)
        if (sendSlot (r).contains (p))
            return r;
    return -1;
}

void ChannelStrip::resized()
{
    const int panTop = nameHeight + 4 + (effectRows_ + sendRows_) * slotHeight + 12;
    pan_.setBounds (getWidth() / 2 - 19, panTop + 1, 38, 36);
    fader_.setBounds (faderArea());
    auto buttons = juce::Rectangle<int> (4, getHeight() - buttonHeight - 2, getWidth() - 8, buttonHeight - 2);
    if (isTrack())
    {
        mute_.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 2));
        buttons.removeFromLeft (4);
        solo_.setBounds (buttons);
    }
    else
        mute_.setBounds (buttons.withSizeKeepingCentre (buttons.getWidth() / 2, buttons.getHeight()));
}

// ---- 그리기 ----

void ChannelStrip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    g.setColour (isMaster() ? colours::panelRaised : colours::panel);
    g.fillRect (bounds.reduced (1, 0));
    g.setColour (colours::outline);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    if (! project_.loaded())
        return;

    // 이름
    auto name = nameArea().reduced (2, 2);
    g.setColour (selected_ ? colours::panelRaised.brighter (0.12f) : colours::panelRaised);
    g.fillRoundedRectangle (name.toFloat(), 4.0f);
    if (selected_)
    {
        g.setColour (colours::accent);
        g.drawRoundedRectangle (name.toFloat().reduced (0.5f), 4.0f, 1.0f);
    }
    juce::Colour colour = colours::accent;
    juce::String sub;
    const auto& c = project_.composition();
    if (isTrack())
    {
        colour = project_.summary (channel_.index).colour;
        sub = juce::String::fromUTF8 (c.tracks[channel_.index].instrument.c_str());
        if (const auto& out = c.tracks[channel_.index].output; ! out.empty())
            sub = juce::String::fromUTF8 ("→ ") + juce::String::fromUTF8 (out.c_str()) + "  " + sub;   // 서브그룹 버스로 나간다
    }
    else if (isBus())
    {
        colour = busColour;
        sub = u8 ("버스");
    }
    if (isTrack())
        setTooltip (project_.channelName (channel_) + u8 (": 악기 줄을 누르면 악기 창, 오른쪽 클릭은 악기 바꾸기"));
    else if (isMaster())
        sub = c.master.targetLufs ? u8 ("목표 ") + juce::String (*c.master.targetLufs, 1) + " LUFS" : u8 ("마스터");
    // 속한 VCA
    if (! isMaster())
        if (const auto& vca = isTrack() ? c.tracks[channel_.index].vca : c.buses[channel_.index].vca; ! vca.empty())
            sub = "[" + juce::String::fromUTF8 (vca.c_str()) + "] " + sub;
    g.setColour (colour);
    g.fillRect (name.getX() + 3, name.getY() + 3, name.getWidth() - 6, 3);
    g.setColour (colours::text);
    g.setFont (uiFont (13.0f, true));
    g.drawFittedText (project_.channelName (channel_), name.getX() + 4, name.getY() + 8, name.getWidth() - 8, 18, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText (sub, name.getX() + 4, name.getY() + 25, name.getWidth() - 8, 14, juce::Justification::centredLeft, 1, 0.8f);

    paintEffects (g);
    paintSends (g);

    // 밸런스 값
    const int panTop = nameHeight + 4 + (effectRows_ + sendRows_) * slotHeight + 12;
    g.setColour (colours::textDim);
    g.setFont (monoFont (10.0f));
    g.drawText (formatPan (pan_.getValue()), 0, panTop + 36, getWidth(), 12, juce::Justification::centred);

    paintScale (g);
    paintMeter (g);

    // 게인과 최고값
    const int readY = getHeight() - buttonHeight - readoutHeight - 2;
    g.setFont (monoFont (11.0f));
    g.setColour (colours::text);
    g.drawText (formatGainDb (fader_.getValue()), 4, readY, getWidth() / 2 - 6, readoutHeight - 2, juce::Justification::centred);
    const auto peak = peakArea();
    g.setColour (meter_.clipped ? colours::danger.withAlpha (0.25f) : colours::background);
    g.fillRoundedRectangle (peak.toFloat(), 3.0f);
    g.setColour (meter_.clipped ? colours::danger : colours::textDim);
    g.drawText (formatPeakDb (meter_.maxPeak), peak, juce::Justification::centred);
}

void ChannelStrip::paintEffects (juce::Graphics& g)
{
    // 효과가 슬롯보다 많으면 마지막 슬롯이 "나머지", 슬롯이 남으면 빈 슬롯 하나가 "+" (가득 차면 오른쪽 클릭 메뉴로 더한다)
    const auto& effects = project_.effectsOf (channel_);
    const int count = (int) effects.size();
    const bool overflow = count > effectRows_;
    const int shown = overflow ? effectRows_ - 1 : count;
    g.setFont (uiFont (11.5f));
    for (int r = 0; r < effectRows_; ++r)
    {
        const auto slot = effectSlot (r).toFloat();
        if (r < shown)
        {
            const auto& e = effects[(size_t) r];
            const bool open = panel_.isEffectOpen (channel_, r);
            g.setColour (e.bypass ? colours::background : colours::panelRaised.brighter (0.05f));
            g.fillRoundedRectangle (slot, 3.0f);
            g.setColour (e.bypass ? colours::textFaint : colours::accent.withAlpha (0.8f));
            g.fillRect (slot.withWidth (3.0f).reduced (0.0f, 3.0f).translated (2.0f, 0.0f));
            g.setColour (open ? colours::accent : colours::outline);
            g.drawRoundedRectangle (slot.reduced (0.5f), 3.0f, open ? 1.5f : 1.0f);
            g.setColour (e.bypass ? colours::textFaint : colours::text);
            g.drawFittedText (effectSlotName (e) + (e.bypass ? u8 (" (우회)") : juce::String()), slot.toNearestInt().reduced (8, 0),
                              juce::Justification::centredLeft, 1, 0.75f);
        }
        else if (r == shown && overflow)
        {
            g.setColour (colours::panelRaised);
            g.fillRoundedRectangle (slot, 3.0f);
            g.setColour (colours::textDim);
            g.drawText (u8 ("나머지 ") + juce::String (count - shown) + u8 ("개"), slot, juce::Justification::centred);
        }
        else if (r == count)
        {
            g.setColour (colours::outline);
            g.drawRoundedRectangle (slot.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (colours::textFaint);
            g.drawText ("+", slot, juce::Justification::centred);
        }
    }
    g.setColour (colours::outline);
    const int y = nameHeight + 4 + effectRows_ * slotHeight + 2;
    g.fillRect (6, y, getWidth() - 12, 1);
}

void ChannelStrip::paintSends (juce::Graphics& g)
{
    if (! isTrack())
        return;
    const auto& spec = project_.composition().tracks[channel_.index];
    const int count = (int) spec.sends.size();
    for (int r = 0; r < sendRows_; ++r)
    {
        const auto slot = sendSlot (r).toFloat();
        if (r < count && ! (r == sendRows_ - 1 && count > sendRows_))
        {
            const auto& s = spec.sends[(size_t) r];
            const bool automated = ! s.levelLane.empty();   // 곡선이 크기를 정하므로 고정값의 막대는 그리지 않는다
            const double db = (r == sendDrag_ && sendMoved_) ? sendDb_ : s.levelDb;
            g.setColour (colours::background);
            g.fillRoundedRectangle (slot, 3.0f);
            if (! automated)
            {
                const float fill = (float) faderlaw::toPosition (db) / (float) faderlaw::toPosition (12.0);
                g.setColour ((s.preFader ? colours::mute : busColour).withAlpha (0.35f));
                g.fillRoundedRectangle (slot.withWidth (slot.getWidth() * juce::jlimit (0.0f, 1.0f, fill)), 3.0f);
            }
            g.setColour (automated ? (s.preFader ? colours::mute : busColour).withAlpha (0.6f) : colours::outline);
            g.drawRoundedRectangle (slot.reduced (0.5f), 3.0f, 1.0f);
            auto text = slot.toNearestInt().reduced (5, 0);
            g.setColour (colours::textDim);
            g.setFont (automated ? uiFont (10.5f) : monoFont (10.0f));
            g.drawText (automated ? u8 ("곡선") : formatGainDb (db), text.removeFromRight (34), juce::Justification::centredRight);
            g.setColour (colours::text);
            g.setFont (uiFont (11.0f));
            g.drawFittedText ((s.preFader ? u8 ("프리 ") : juce::String()) + juce::String::fromUTF8 (s.bus.c_str()), text,
                              juce::Justification::centredLeft, 1, 0.7f);
        }
        else if (r < count)
        {
            g.setColour (colours::textDim);
            g.setFont (uiFont (11.0f));
            g.drawText (u8 ("나머지 ") + juce::String (count - r) + u8 ("개"), slot, juce::Justification::centred);
        }
        else if (r == count)
        {
            g.setColour (colours::outline);
            g.drawRoundedRectangle (slot.reduced (0.5f), 3.0f, 1.0f);
            g.setColour (colours::textFaint);
            g.setFont (uiFont (11.0f));
            g.drawText (u8 ("+ 센드"), slot, juce::Justification::centred);
        }
    }
    g.setColour (colours::outline);
    const int y = nameHeight + 4 + (effectRows_ + sendRows_) * slotHeight + 8;
    g.fillRect (6, y, getWidth() - 12, 1);
}

void ChannelStrip::paintScale (juce::Graphics& g)
{
    const auto f = faderArea();
    const auto meter = meterArea();
    g.setFont (monoFont (9.0f));
    for (const double db : { 12.0, 6.0, 0.0, -6.0, -12.0, -20.0, -30.0, -40.0, -60.0 })
    {
        const float y = (float) fader_.getY() + (float) fader_.getPositionOfValue (db);
        g.setColour (db == 0.0 ? colours::textDim : colours::textFaint);
        g.fillRect ((float) meter.getX() - 3.0f, y, 2.0f, 1.0f);
        const auto label = db > 0.0 ? "+" + juce::String ((int) db) : juce::String ((int) db);
        g.drawText (label, f.getRight(), (int) y - 6, meter.getX() - f.getRight() - 4, 12, juce::Justification::centredRight);
    }
}

void ChannelStrip::paintMeter (juce::Graphics& g)
{
    const auto area = meterArea();
    g.setColour (colours::background);
    g.fillRect (area);
    const float top = (float) fader_.getY() + (float) fader_.getPositionOfValue (faderlaw::maxDb);
    const float bottom = (float) fader_.getY() + (float) fader_.getPositionOfValue (faderlaw::minDb);
    const float zero = (float) fader_.getY() + (float) fader_.getPositionOfValue (0.0);
    const float six = (float) fader_.getY() + (float) fader_.getPositionOfValue (-6.0);
    const float barWidth = (area.getWidth() - 2) / 2.0f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float x = (float) area.getX() + ch * (barWidth + 2.0f);
        const float y = (float) fader_.getY() + (float) fader_.getPositionOfValue (juce::jlimit (-120.0, 24.0, (double) meter_.level[ch]));
        if (meter_.level[ch] > -119.0f)
        {
            // 0 dB 위는 빨강, -6 dB 위는 노랑, 아래는 초록
            const juce::Rectangle<float> bar (x, y, barWidth, bottom - y);
            g.setColour (colours::solo);
            g.fillRect (bar.withTop (std::max (bar.getY(), six)));
            if (y < six)
            {
                g.setColour (colours::playhead);
                g.fillRect (bar.withTop (std::max (bar.getY(), zero)).withBottom (six));
            }
            if (y < zero)
            {
                g.setColour (colours::danger);
                g.fillRect (bar.withBottom (zero));
            }
        }
        if (meter_.hold[ch] > -119.0f)
        {
            const float hy = (float) fader_.getY() + (float) fader_.getPositionOfValue (juce::jlimit (-120.0, 24.0, (double) meter_.hold[ch]));
            g.setColour (meter_.hold[ch] >= 0.0f ? colours::danger : colours::text);
            g.fillRect (x, hy, barWidth, 1.5f);
        }
    }
    g.setColour (colours::outline);
    g.drawRect (area.toFloat().withTop (top).withBottom (bottom), 1.0f);
}

// ---- 마우스 ----

void ChannelStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! project_.loaded())
        return;
    const auto p = e.getPosition();
    if (nameArea().contains (p))
    {
        if (isTrack())
        {
            panel_.selectTrack (channel_.index);
            if (e.mods.isPopupMenu())
            {
                if (panel_.onInstrumentMenu)
                    panel_.onInstrumentMenu (channel_.index);
            }
            else if (p.y >= 26 && panel_.onOpenInstrument)   // 이름 아래의 악기 줄
                panel_.onOpenInstrument (channel_.index);
        }
        else if (isBus() && e.mods.isPopupMenu())
            showNameMenu();
        return;
    }
    if (peakArea().contains (p) || meterArea().contains (p))
    {
        clearLevel();
        return;
    }
    if (const int row = effectAt (p); row >= 0)
    {
        const int count = (int) project_.effectsOf (channel_).size();
        const bool overflow = count > effectRows_;
        if (overflow && row == effectRows_ - 1)
        {
            juce::PopupMenu m;
            for (int i = row; i < count; ++i)
                m.addItem (i + 1, effectSlotName (project_.effectsOf (channel_)[(size_t) i]));
            m.addSeparator();
            m.addItem (1000, u8 ("효과 추가..."));
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                             [safe = juce::Component::SafePointer<ChannelStrip> (this)] (int result)
                             {
                                 if (safe == nullptr || result == 0)
                                     return;
                                 if (result == 1000)
                                     safe->showEffectMenu (-1);
                                 else
                                     safe->panel_.openEffect (safe->channel_, result - 1);
                             });
            return;
        }
        if (row < count)
        {
            if (e.mods.isPopupMenu())
                showEffectMenu (row);
            else
                panel_.openEffect (channel_, row);
        }
        else if (row == count)
            showEffectMenu (-1);
        return;
    }
    if (const int row = sendAt (p); row >= 0)
    {
        const auto& sends = project_.composition().tracks[channel_.index].sends;
        if (row == sendRows_ - 1 && (int) sends.size() > sendRows_)
        {
            // 가려진 센드: 고르면 그 센드의 메뉴
            juce::PopupMenu m;
            for (int i = row; i < (int) sends.size(); ++i)
                m.addItem (i + 1, juce::String::fromUTF8 (sends[(size_t) i].bus.c_str()) + "  "
                                      + (sends[(size_t) i].levelLane.empty() ? formatGainDb (sends[(size_t) i].levelDb) + " dB" : u8 ("곡선")));
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                             [safe = juce::Component::SafePointer<ChannelStrip> (this)] (int result)
                             {
                                 if (safe != nullptr && result > 0)
                                     safe->showSendMenu (result - 1);
                             });
            return;
        }
        if (row < (int) sends.size())
        {
            if (e.mods.isPopupMenu())
                showSendMenu (row);
            else if (! sendAutomated (row))
            {
                sendDrag_ = row;
                sendStartDb_ = sendDb_ = sends[(size_t) row].levelDb;
                sendMoved_ = false;
            }
        }
        else if (row == (int) sends.size())
            showSendMenu (-1);
    }
}

void ChannelStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (sendDrag_ < 0)
        return;
    const double perPixel = e.mods.isShiftDown() ? 0.05 : 0.25;
    const double db = juce::jlimit (-120.0, 12.0, std::round ((sendStartDb_ + e.getDistanceFromDragStartX() * perPixel) * 10.0) / 10.0);
    if (! sendMoved_ && std::abs (e.getDistanceFromDragStartX()) < 2)
        return;
    sendMoved_ = true;
    sendDb_ = db;
    auto sends = project_.composition().tracks[channel_.index].sends;
    sends[(size_t) sendDrag_].levelDb = db;
    panel_.previewSends (channel_.index, sends);
    repaint (sendSlot (sendDrag_));
}

void ChannelStrip::mouseUp (const juce::MouseEvent&)
{
    if (sendDrag_ < 0)
        return;
    const int row = sendDrag_;
    sendDrag_ = -1;
    if (! sendMoved_)
        return;
    sendMoved_ = false;
    auto sends = project_.composition().tracks[channel_.index].sends;
    if (row < (int) sends.size() && sends[(size_t) row].levelDb != sendDb_)
    {
        sends[(size_t) row].levelDb = sendDb_;
        panel_.commitSends (channel_.index, sends, u8 ("센드 레벨: ") + project_.channelName (channel_));
    }
    repaint();
}

void ChannelStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! project_.loaded())
        return;
    const auto p = e.getPosition();
    if (isBus() && nameArea().contains (p))
    {
        panel_.askBusName (channel_.index);
        return;
    }
    if (const int row = sendAt (p); row >= 0)
    {
        auto sends = project_.composition().tracks[channel_.index].sends;
        if (row < (int) sends.size() && ! sendAutomated (row) && sends[(size_t) row].levelDb != 0.0)
        {
            sends[(size_t) row].levelDb = 0.0;
            panel_.commitSends (channel_.index, sends, u8 ("센드 레벨: ") + project_.channelName (channel_));
        }
    }
}

bool ChannelStrip::sendAutomated (int row) const
{
    if (! isTrack() || ! project_.loaded())
        return false;
    const auto& sends = project_.composition().tracks[channel_.index].sends;
    return row >= 0 && row < (int) sends.size() && ! sends[(size_t) row].levelLane.empty();
}

juce::String ChannelStrip::getTooltip()
{
    if (const int row = sendAt (getMouseXYRelative()); row >= 0 && sendAutomated (row) && ! (row == sendRows_ - 1
        && (int) project_.composition().tracks[channel_.index].sends.size() > sendRows_))
        return u8 ("곡의 곡선(automation)이 이 센드의 크기를 정합니다. 곡선을 지우기 전에는 끌어서 바꿀 수 없습니다.");
    return juce::SettableTooltipClient::getTooltip();
}

void ChannelStrip::showEffectMenu (int row)
{
    juce::PopupMenu m;
    if (row < 0)
    {
        const auto& types = mr::effectTypes();
        for (size_t i = 0; i < types.size(); ++i)
            m.addItem ((int) i + 1, effectName (types[i]));
        // 찾은 플러그인 효과 (플러그인 찾기의 캐시)
        auto plugins = std::make_shared<juce::Array<juce::var>>();
        juce::PopupMenu found;
        const auto cache = juce::JSON::parse (mr::defaultPluginCache());
        if (const auto* list = cache.getProperty ("plugins", juce::var()).getArray())
            for (const auto& e : *list)
            {
                if ((bool) e.getProperty ("isInstrument", false))
                    continue;
                auto* o = new juce::DynamicObject();
                o->setProperty ("type", "plugin");
                o->setProperty ("format", e.getProperty ("format", "vst3"));
                o->setProperty ("plugin", e.getProperty ("plugin", ""));
                o->setProperty ("name", e.getProperty ("name", ""));
                found.addItem (1000 + plugins->size(), e.getProperty ("name", "").toString() + " (" + e.getProperty ("format", "").toString().toUpperCase()
                                                            + ", " + e.getProperty ("manufacturer", "").toString() + ")");
                plugins->add (juce::var (o));
            }
        m.addSeparator();
        m.addSubMenu (u8 ("플러그인 효과 (VST3, AU)"), found, found.getNumItems() > 0);
        // 효과 사슬 프리셋
        m.addSeparator();
        juce::PopupMenu chains;
        for (const auto& name : panel_.chainPresets())
            chains.addItem (name, [safe = juce::Component::SafePointer<ChannelStrip> (this), name]
            {
                if (safe != nullptr)
                    safe->panel_.loadChain (safe->channel_, name);
            });
        m.addSubMenu (u8 ("효과 사슬 불러오기 (바꾸기)"), chains, chains.getNumItems() > 0);
        m.addItem (u8 ("효과 사슬 저장..."), panel_.presetFolder != juce::File() && ! project_.effectsOf (channel_).empty(), false,
                   [safe = juce::Component::SafePointer<ChannelStrip> (this)]
                   {
                       if (safe != nullptr)
                           safe->panel_.askChainName (safe->channel_);
                   });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<ChannelStrip> (this), plugins] (int result)
                         {
                             if (safe == nullptr || result <= 0)
                                 return;
                             if (result >= 1000 && result - 1000 < plugins->size())
                                 safe->panel_.addPluginEffect (safe->channel_, (*plugins)[result - 1000]);
                             else if ((size_t) result <= mr::effectTypes().size())
                                 safe->panel_.addEffect (safe->channel_, mr::effectTypes()[(size_t) result - 1]);
                         });
        return;
    }
    const auto& effects = project_.effectsOf (channel_);
    const bool bypassed = effects[(size_t) row].bypass;
    m.addItem (1, u8 ("매개변수 열기"));
    m.addItem (2, u8 ("우회"), true, bypassed);
    m.addSeparator();
    m.addItem (3, u8 ("앞으로 옮기기"), row > 0);
    m.addItem (4, u8 ("뒤로 옮기기"), row + 1 < (int) effects.size());
    m.addSeparator();
    m.addItem (5, u8 ("지우기"));
    m.addItem (6, u8 ("효과 추가..."));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<ChannelStrip> (this), row] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         auto& panel = safe->panel_;
                         const auto channel = safe->channel_;
                         switch (result)
                         {
                             case 6: safe->showEffectMenu (-1); break;
                             case 1: panel.openEffect (channel, row); break;
                             case 2: panel.toggleBypass (channel, row); break;
                             case 3: panel.moveEffect (channel, row, -1); break;
                             case 4: panel.moveEffect (channel, row, 1); break;
                             case 5: panel.removeEffect (channel, row); break;
                             default: break;
                         }
                     });
}

void ChannelStrip::showSendMenu (int row)
{
    juce::PopupMenu m;
    const auto& c = project_.composition();
    const auto& sends = c.tracks[channel_.index].sends;
    if (row < 0)
    {
        for (size_t b = 0; b < c.buses.size(); ++b)
        {
            const bool used = std::any_of (sends.begin(), sends.end(), [&] (const mr::SendSpec& s) { return s.bus == c.buses[b].name; });
            m.addItem ((int) b + 1, juce::String::fromUTF8 (c.buses[b].name.c_str()), ! used);
        }
        if (c.buses.empty())
            m.addItem (-1, u8 ("버스가 없습니다"), false);
        m.addSeparator();
        m.addItem (1000, u8 ("새 버스 만들기"));
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [safe = juce::Component::SafePointer<ChannelStrip> (this)] (int result)
                         {
                             if (safe == nullptr || result == 0)
                                 return;
                             if (result == 1000)
                                 safe->panel_.addBus();
                             else if (result > 0)
                                 safe->panel_.addSend (safe->channel_.index, (size_t) result - 1);
                         });
        return;
    }
    m.addItem (1, u8 ("프리 페이더"), true, sends[(size_t) row].preFader);
    m.addItem (2, sendAutomated (row) ? u8 ("0 dB 로 (곡선이 크기를 정합니다)") : u8 ("0 dB 로"), ! sendAutomated (row));
    m.addSeparator();
    m.addItem (3, u8 ("지우기"));
    m.addItem (4, u8 ("센드 추가..."));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<ChannelStrip> (this), row] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         auto& panel = safe->panel_;
                         const size_t track = safe->channel_.index;
                         const auto& s = safe->project_.composition().tracks[track].sends;
                         if (row >= (int) s.size())
                             return;
                         if (result == 1)
                             panel.setSendPreFader (track, row, ! s[(size_t) row].preFader);
                         else if (result == 2)
                         {
                             auto sends = s;
                             sends[(size_t) row].levelDb = 0.0;
                             panel.commitSends (track, sends, juce::String::fromUTF8 ("센드 레벨"));
                         }
                         else if (result == 3)
                             panel.removeSend (track, row);
                         else if (result == 4)
                             safe->showSendMenu (-1);
                     });
}

void ChannelStrip::showNameMenu()
{
    juce::PopupMenu m;
    m.addItem (1, u8 ("이름 바꾸기..."));
    m.addItem (2, u8 ("버스 지우기"));
    m.addSubMenu ("VCA", panel_.vcaMenu (channel_));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<ChannelStrip> (this)] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         if (result == 1)
                             safe->panel_.askBusName (safe->channel_.index);
                         else if (result == 2)
                             safe->panel_.removeBus (safe->channel_.index);
                     });
}

// ---------------------------------------------------------------------------
// VCA 스트립
// ---------------------------------------------------------------------------

VcaStrip::VcaStrip (MixerPanel& panel, Project& project, size_t index) : panel_ (panel), project_ (project), index_ (index)
{
    juce::NormalisableRange<double> range (
        faderlaw::minDb, faderlaw::maxDb,
        [] (double, double, double p) { return faderlaw::toDb (p); },
        [] (double, double, double db) { return faderlaw::toPosition (db); },
        [] (double, double, double db) { return db <= faderlaw::minDb + 0.05 ? faderlaw::minDb : std::round (db * 10.0) / 10.0; });
    fader_.setSliderStyle (juce::Slider::LinearVertical);
    fader_.setNormalisableRange (range);
    fader_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    fader_.setDoubleClickReturnValue (true, 0.0);
    fader_.setSliderSnapsToMousePosition (false);
    fader_.setTooltip (u8 ("속한 채널 모두의 게인에 더합니다 (두 번 클릭하면 0 dB)"));
    fader_.onDragStart = [this] { dragging_ = true; };
    fader_.onValueChange = [this]
    {
        if (updating_)
            return;
        repaint();
        if (dragging_)
            panel_.previewVca (index_, faderDb());
        else if (index_ < project_.composition().vcas.size())
            panel_.setVca (index_, faderDb(), project_.composition().vcas[index_].mute, u8 ("VCA 게인"));
    };
    fader_.onDragEnd = [this]
    {
        dragging_ = false;
        if (index_ < project_.composition().vcas.size())
            panel_.setVca (index_, faderDb(), project_.composition().vcas[index_].mute, u8 ("VCA 게인"));
    };
    addAndMakeVisible (fader_);
    mute_.setClickingTogglesState (false);
    mute_.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    mute_.setColour (juce::TextButton::buttonOnColourId, colours::mute);
    mute_.setTooltip (u8 ("속한 채널을 모두 뮤트합니다"));
    mute_.onClick = [this]
    {
        if (index_ < project_.composition().vcas.size())
        {
            const auto& v = project_.composition().vcas[index_];
            panel_.setVca (index_, v.gainDb, ! v.mute, u8 ("VCA 뮤트"));
        }
    };
    addAndMakeVisible (mute_);
}

double VcaStrip::faderDb() const
{
    const double db = fader_.getValue();
    return db <= faderlaw::minDb + 0.05 ? faderlaw::minDb : std::round (db * 10.0) / 10.0;
}

void VcaStrip::update()
{
    if (! project_.loaded() || index_ >= project_.composition().vcas.size())
        return;
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    const auto& v = project_.composition().vcas[index_];
    if (! dragging_)
        fader_.setValue (v.gainLane.empty() ? v.gainDb : v.gainLane.valueAt (0.0), juce::dontSendNotification);
    fader_.setEnabled (v.gainLane.empty());
    mute_.setToggleState (v.mute, juce::dontSendNotification);
    setTooltip (juce::String::fromUTF8 (v.name.c_str()) + u8 (": 오른쪽 클릭은 이름 바꾸기와 지우기. 채널은 트랙이나 버스 이름 메뉴의 VCA 에서 넣습니다"));
    repaint();
}

void VcaStrip::showAutomationAt (double beat)
{
    if (! project_.loaded() || index_ >= project_.composition().vcas.size())
        return;
    const auto& v = project_.composition().vcas[index_];
    if (v.gainLane.empty())
        return;
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    const double db = std::round (v.gainLane.valueAt (beat) * 10.0) / 10.0;
    if (db != fader_.getValue())
    {
        fader_.setValue (db, juce::dontSendNotification);
        repaint();
    }
}

void VcaStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 0);
    r.removeFromTop (ChannelStrip::nameHeight + 6);
    mute_.setBounds (r.removeFromBottom (ChannelStrip::buttonHeight).withTrimmedBottom (4));
    r.removeFromBottom (ChannelStrip::readoutHeight + 2);
    fader_.setBounds (r.withSizeKeepingCentre (30, r.getHeight()));
}

void VcaStrip::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds().reduced (1, 0));
    g.setColour (colours::outline);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
    if (! project_.loaded() || index_ >= project_.composition().vcas.size())
        return;
    const auto& c = project_.composition();
    const auto& v = c.vcas[index_];
    int members = 0;
    for (const auto& t : c.tracks)
        members += t.vca == v.name ? 1 : 0;
    for (const auto& b : c.buses)
        members += b.vca == v.name ? 1 : 0;
    auto name = juce::Rectangle<int> (0, 0, getWidth(), ChannelStrip::nameHeight).reduced (2, 2);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (name.toFloat(), 4.0f);
    g.setColour (colours::accent.withRotatedHue (0.5f));
    g.fillRect (name.getX() + 3, name.getY() + 3, name.getWidth() - 6, 3);
    g.setColour (colours::text);
    g.setFont (uiFont (13.0f, true));
    g.drawFittedText (juce::String::fromUTF8 (v.name.c_str()), name.getX() + 4, name.getY() + 8, name.getWidth() - 8, 18, juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (colours::textDim);
    g.setFont (uiFont (11.0f));
    g.drawFittedText ("VCA, " + juce::String (members) + u8 ("채널"), name.getX() + 4, name.getY() + 25, name.getWidth() - 8, 14,
                      juce::Justification::centredLeft, 1, 0.8f);
    // 게인 값
    const auto readout = juce::Rectangle<int> (4, getHeight() - ChannelStrip::buttonHeight - ChannelStrip::readoutHeight - 2, getWidth() - 8, ChannelStrip::readoutHeight);
    g.setColour (colours::panelRaised);
    g.fillRoundedRectangle (readout.toFloat(), 3.0f);
    g.setColour (v.gainLane.empty() ? colours::text : colours::accent);
    g.setFont (uiFont (12.0f));
    g.drawText (formatGainDb (fader_.getValue()) + (v.gainLane.empty() ? juce::String() : u8 (" 곡선")), readout, juce::Justification::centred);
}

void VcaStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= ChannelStrip::nameHeight || ! e.mods.isPopupMenu())
        return;
    juce::PopupMenu m;
    m.addItem (1, u8 ("이름 바꾸기..."));
    m.addItem (2, u8 ("VCA 지우기 (채널은 남는다)"));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                     [safe = juce::Component::SafePointer<VcaStrip> (this)] (int result)
                     {
                         if (safe == nullptr)
                             return;
                         if (result == 1)
                             safe->panel_.askVcaName (safe->index_);
                         else if (result == 2)
                             safe->panel_.removeVca (safe->index_);
                     });
}

// ---------------------------------------------------------------------------
// MIDI 배우기 덮개: 컨트롤마다 테두리, 묶인 것은 CC 번호. 누르면 그 자리를 고르고, 오른쪽 클릭은 묶음을 지운다
// ---------------------------------------------------------------------------

class MidiLearnOverlay final : public juce::Component
{
public:
    explicit MidiLearnOverlay (MixerPanel& panel) : panel_ (panel) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.35f));
        if (panel_.learn == nullptr)
            return;
        for (const auto& [target, r] : panel_.learnableControls())
        {
            const auto binding = panel_.learn->bindingOf (target);
            const bool armed = panel_.learn->armed() && *panel_.learn->armed() == target;
            const auto box = r.toFloat().expanded (2.0f);
            g.setColour (armed ? colours::accent.withAlpha (0.55f) : binding ? colours::accent.withAlpha (0.3f) : colours::text.withAlpha (0.06f));
            g.fillRoundedRectangle (box, 3.0f);
            g.setColour (armed ? colours::accent : colours::text.withAlpha (binding ? 0.8f : 0.35f));
            g.drawRoundedRectangle (box, 3.0f, armed ? 2.0f : 1.0f);
            if (binding)
            {
                g.setColour (colours::text);
                g.setFont (uiFont (11.0f, true));
                g.drawFittedText ("CC " + juce::String (binding->controller) + (binding->midiChannel != 1 ? "/" + juce::String (binding->midiChannel) : juce::String()),
                                  r.expanded (12, 0), juce::Justification::centred, 1, 0.7f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (panel_.learn == nullptr)
            return;
        for (const auto& [target, r] : panel_.learnableControls())
            if (r.expanded (3).contains (e.getPosition()))
            {
                if (e.mods.isPopupMenu())
                    panel_.learn->unbind (target);
                else
                    panel_.learn->arm (target);
                return;
            }
    }

private:
    MixerPanel& panel_;
};

// ---------------------------------------------------------------------------
// 믹서 칸
// ---------------------------------------------------------------------------

MixerPanel::MixerPanel (Project& project, TimelineState& timeline) : project_ (project), timeline_ (timeline)
{
    addChildComponent (analyzer_);
    viewport_.setViewedComponent (&row_, false);
    viewport_.setScrollBarsShown (false, true);
    viewport_.setScrollBarThickness (10);
    addAndMakeVisible (viewport_);
    addBus_.setButtonText (u8 ("+ 버스"));
    addBus_.setTooltip (u8 ("리버브가 든 버스를 더합니다"));
    addBus_.onClick = [this] { addBus(); };
    addVca_.setButtonText (u8 ("+ VCA"));
    addVca_.setTooltip (u8 ("소리를 모으지 않고 여러 채널의 페이더를 함께 움직이는 VCA 를 더합니다"));
    addVca_.onClick = [this] { addVca(); };
    row_.addAndMakeVisible (addBus_);
    row_.addAndMakeVisible (addVca_);
    editor_ = std::make_unique<EffectEditor> (*this, project_);
    editor_->onClose = [this] { openEffect (ChannelRef::master(), -1); };
    addChildComponent (*editor_);
    timeline_.addListener (this);
    project_.addListener (this);
}

MixerPanel::~MixerPanel()
{
    project_.removeListener (this);
    timeline_.removeListener (this);
}

void MixerPanel::projectChanged()
{
    editor_->setVisible (false);
    rebuildStrips();
    resized();
}

void MixerPanel::rebuildStrips()
{
    strips_.clear();
    vcaStrips_.clear();
    master_.reset();
    if (project_.loaded())
    {
        for (size_t i = 0; i < project_.trackCount(); ++i)
            strips_.push_back (std::make_unique<ChannelStrip> (*this, project_, ChannelRef::track (i)));
        for (size_t i = 0; i < project_.busCount(); ++i)
            strips_.push_back (std::make_unique<ChannelStrip> (*this, project_, ChannelRef::bus (i)));
        for (size_t i = 0; i < project_.composition().vcas.size(); ++i)
        {
            vcaStrips_.push_back (std::make_unique<VcaStrip> (*this, project_, i));
            row_.addAndMakeVisible (*vcaStrips_.back());
        }
        master_ = std::make_unique<ChannelStrip> (*this, project_, ChannelRef::master());
        addAndMakeVisible (*master_);
    }
    for (auto& s : strips_)
        row_.addAndMakeVisible (*s);
    addBus_.setVisible (project_.loaded());
    addVca_.setVisible (project_.loaded());
    layoutRows();
    for (auto& s : strips_)
        s->update();
    for (auto& v : vcaStrips_)
        v->update();
    if (master_ != nullptr)
        master_->update();
    timelineChanged();
}

void MixerPanel::layoutRows()
{
    if (! project_.loaded())
        return;
    const auto& c = project_.composition();
    size_t maxEffects = c.master.effects.size(), maxSends = 0;
    for (const auto& t : c.tracks)
    {
        maxEffects = std::max (maxEffects, t.effects.size());
        maxSends = std::max (maxSends, t.sends.size());
    }
    for (const auto& b : c.buses)
        maxEffects = std::max (maxEffects, b.effects.size());
    // 효과 슬롯을 먼저 지킨다. 높이가 모자라면 센드의 빈 슬롯, 효과 줄, 센드 줄 순으로 줄인다 (가려진 것은 "나머지" 슬롯과 메뉴로 연다).
    int effects = juce::jlimit (2, 7, (int) maxEffects + 1);
    int sends = juce::jlimit (1, 4, (int) maxSends + 1);
    const int height = getHeight() - 10;
    const int fixed = ChannelStrip::nameHeight + ChannelStrip::panHeight + ChannelStrip::readoutHeight + ChannelStrip::buttonHeight + 30;
    auto fits = [&] { return fixed + (effects + sends) * ChannelStrip::slotHeight + 130 <= height; };
    if (! fits() && sends > std::max<int> (1, (int) maxSends))
        --sends;
    while (! fits() && effects > std::min (3, (int) maxEffects + 1))
        --effects;
    while (! fits() && sends > 1)
        --sends;
    while (! fits() && effects > 2)
        --effects;
    effectRows_ = effects;
    sendRows_ = sends;
    for (auto& s : strips_)
        s->setRows (effects, sends);
    if (master_ != nullptr)
        master_->setRows (effects, sends);
}

void MixerPanel::resized()
{
    auto r = getLocalBounds();
    analyzer_.setVisible (getWidth() >= 900);
    if (analyzer_.isVisible())
        analyzer_.setBounds (r.removeFromRight (AnalyzerPanel::width).withTrimmedBottom (10).withTrimmedRight (4));
    if (master_ != nullptr)
        master_->setBounds (r.removeFromRight (ChannelStrip::width + 6).withTrimmedLeft (6).withTrimmedBottom (10));
    if (editor_->isVisible())
        editor_->setBounds (r.removeFromRight (EffectEditor::width));
    viewport_.setBounds (r);
    const int height = std::max (200, r.getHeight() - viewport_.getScrollBarThickness());
    int x = 0;
    const size_t tracks = project_.trackCount();
    for (size_t i = 0; i < strips_.size(); ++i)
    {
        if (i == tracks && i > 0)
            x += 6;   // 트랙과 버스 사이
        strips_[i]->setBounds (x, 0, ChannelStrip::width, height);
        x += ChannelStrip::width;
    }
    if (! vcaStrips_.empty())
        x += 6;   // 버스와 VCA 사이
    for (auto& v : vcaStrips_)
    {
        v->setBounds (x, 0, VcaStrip::width, height);
        x += VcaStrip::width;
    }
    addBus_.setBounds (x + 8, 8, 64, 28);
    addVca_.setBounds (x + 8, 42, 64, 28);
    row_.setSize (x + 80, height);
    layoutRows();
    if (learnOverlay_ != nullptr)
        learnOverlay_->setBounds (getLocalBounds());
}

void MixerPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    if (! project_.loaded())
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (15.0f));
        g.drawText (u8 ("프로젝트를 열면 트랙마다 채널 스트립이 나옵니다."), getLocalBounds(), juce::Justification::centred);
    }
}

void MixerPanel::timelineChanged()
{
    const int selected = timeline_.selectedTrack();
    bool changed = false;
    for (auto& s : strips_)
    {
        const bool on = s->channel().kind == ChannelRef::Kind::track && (int) s->channel().index == selected;
        if (on && ! s->isSelectedStrip())
            changed = true;
        s->setSelected (on);
    }
    if (changed && selected >= 0)
        scrollToShow (ChannelRef::track ((size_t) selected));
}

void MixerPanel::visibilityChanged()
{
    if (! isVisible())
        return;
    resized();
    if (editor_->isVisible())
        scrollToShow (editor_->channel());
    else if (timeline_.selectedTrack() >= 0)
        scrollToShow (ChannelRef::track ((size_t) timeline_.selectedTrack()));
}

void MixerPanel::scrollToShow (ChannelRef channel)
{
    auto* s = strip (channel);
    if (s == nullptr || s == master_.get() || viewport_.getWidth() <= 0)
        return;
    const int left = viewport_.getViewPositionX(), right = left + viewport_.getViewWidth();
    if (s->getX() < left)
        viewport_.setViewPosition (s->getX(), 0);
    else if (s->getRight() > right)
        viewport_.setViewPosition (s->getRight() - viewport_.getViewWidth(), 0);
}

void MixerPanel::projectEdited (int)
{
    if (! project_.loaded())
        return;
    if (strips_.size() != project_.trackCount() + project_.busCount() || vcaStrips_.size() != project_.composition().vcas.size())
    {
        rebuildStrips();
        resized();
    }
    else
    {
        layoutRows();
        for (auto& s : strips_)
            s->update();
        for (auto& v : vcaStrips_)
            v->update();
        if (master_ != nullptr)
            master_->update();
    }
    if (editor_->isVisible())
    {
        if (editor_->valid())
            editor_->refresh();
        else
            openEffect (ChannelRef::master(), -1);
    }
}

void MixerPanel::showAutomationAt (double beat)
{
    for (auto& s : strips_)
        s->showAutomationAt (beat);
    for (auto& v : vcaStrips_)
        v->showAutomationAt (beat);
    if (master_ != nullptr)
        master_->showAutomationAt (beat);
}

void MixerPanel::setLevels (const std::vector<mr::StereoPeak>& tracks, const std::vector<mr::StereoPeak>& buses, mr::StereoPeak master, double now)
{
    for (auto& s : strips_)
    {
        const auto c = s->channel();
        if (c.kind == ChannelRef::Kind::track && c.index < tracks.size())
            s->setLevel (tracks[c.index], now);
        else if (c.kind == ChannelRef::Kind::bus && c.index < buses.size())
            s->setLevel (buses[c.index], now);
    }
    if (master_ != nullptr)
        master_->setLevel (master, now);
}

void MixerPanel::clearLevels()
{
    for (auto& s : strips_)
        s->clearLevel();
    if (master_ != nullptr)
        master_->clearLevel();
}

ChannelStrip* MixerPanel::strip (ChannelRef channel)
{
    if (channel.kind == ChannelRef::Kind::master)
        return master_.get();
    for (auto& s : strips_)
        if (s->channel() == channel)
            return s.get();
    return nullptr;
}


// ---- 편집 ----

void MixerPanel::preview (const std::function<void (mr::Composition&)>& change)
{
    if (! onPreview || ! project_.loaded())
        return;
    auto copy = project_.composition();
    change (copy);
    onPreview (copy);
}

bool MixerPanel::commitWrittenLane (ChannelRef channel, const std::string& parameter, const mr::AutomationLane& before, double fixed,
                                    const std::vector<std::pair<double, double>>& recorded)
{
    if (recorded.empty())
        return false;
    // 촘촘한 점을 줄인다: 1/16 박 안이고 값이 거의 같으면 건너뛴다 (마지막은 남긴다)
    const double tolerance = parameter == "pan" ? 0.01 : 0.2;
    std::vector<std::pair<double, double>> points;
    for (size_t i = 0; i < recorded.size(); ++i)
    {
        const bool last = i + 1 == recorded.size();
        if (! points.empty() && ! last && recorded[i].first - points.back().first < 0.0625 && std::abs (recorded[i].second - points.back().second) < tolerance)
            continue;
        points.push_back (recorded[i]);
    }
    const double first = points.front().first, end = points.back().first;
    constexpr double edge = 0.001;
    auto valueBefore = [&] (double beat) { return before.empty() ? fixed : before.valueAt (beat); };
    mr::AutomationLane lane;
    for (const auto& p : before.points)
        if (p.beat < first - edge)
            lane.points.push_back (p);
    if (first > edge)
    {
        if (before.empty())
            lane.points.push_back ({ 0.0, fixed, mr::CurveShape::step });   // 쓰기 전에는 원래 값
        else
            lane.points.push_back ({ first - edge, valueBefore (first - edge), mr::CurveShape::linear });
    }
    for (const auto& [beat, value] : points)
        if (lane.points.empty() || beat > lane.points.back().beat + 1.0e-9)
            lane.points.push_back ({ beat, value, mr::CurveShape::linear });
    // 손을 뗀 뒤에는 원래 곡선(없으면 원래 값)으로 돌아간다
    lane.points.push_back ({ end + edge, valueBefore (end + edge), mr::CurveShape::linear });
    for (const auto& p : before.points)
        if (p.beat > end + edge)
            lane.points.push_back (p);

    auto json = project_.channelJson (channel);
    auto* o = json.getDynamicObject();
    auto mixer = o->getProperty ("mixer");
    if (! mixer.isObject())
        mixer = juce::var (new juce::DynamicObject());
    auto automation = mixer.getProperty ("automation", juce::var());
    if (! automation.isObject())
        automation = juce::var (new juce::DynamicObject());
    automation.getDynamicObject()->setProperty (juce::Identifier (parameter), mr::laneJson (lane));
    mixer.getDynamicObject()->setProperty ("automation", automation);
    o->setProperty ("mixer", mixer);
    juce::String error;
    const auto name = (parameter == "pan" ? u8 ("밸런스 곡선 쓰기: ") : u8 ("볼륨 곡선 쓰기: ")) + project_.channelName (channel);
    if (! project_.editChannel (channel, json, name, error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        if (onPreview && project_.loaded())
            onPreview (project_.composition());
        return false;
    }
    status (name + " (" + juce::String ((int) points.size()) + u8 ("점)"), false);
    return true;
}

void MixerPanel::setWriteMode (bool on)
{
    writeMode = on;
    for (auto& s : strips_)
        s->update();
    if (master_ != nullptr)
        master_->update();
}

void MixerPanel::previewMixer (ChannelRef channel, const mr::MixerSettings& mixer)
{
    preview ([&] (mr::Composition& c) { mixerIn (c, channel) = mixer; });
}

bool MixerPanel::commitMixer (ChannelRef channel, const mr::MixerSettings& mixer, const juce::String& name)
{
    juce::String error;
    if (project_.editChannel (channel, mr::withMixer (project_.channelJson (channel), mixer), name, error))
        return true;
    status (error.replaceCharacter ('\n', ' '), true);
    if (onPreview && project_.loaded())
        onPreview (project_.composition());   // 미리 듣던 값을 되돌린다
    return false;
}

void MixerPanel::previewSends (size_t track, const std::vector<mr::SendSpec>& sends)
{
    preview ([&] (mr::Composition& c) { c.tracks.at (track).sends = sends; });
}

bool MixerPanel::commitSends (size_t track, const std::vector<mr::SendSpec>& sends, const juce::String& name)
{
    juce::String error;
    if (project_.editTrack (track, mr::withSends (project_.trackJson (track), sends), name, error))
        return true;
    status (error.replaceCharacter ('\n', ' '), true);
    if (onPreview && project_.loaded())
        onPreview (project_.composition());
    return false;
}

std::vector<mr::EffectSpec> MixerPanel::specsFrom (const juce::Array<juce::var>& effects, const std::string& basePath)
{
    std::vector<mr::EffectSpec> specs;
    for (int i = 0; i < effects.size(); ++i)
    {
        mr::EffectSpec s;
        s.type = effects[i].getProperty ("type", "").toString().toStdString();
        s.params = effects[i];
        s.bypass = (bool) effects[i].getProperty ("bypass", false);
        s.path = basePath + ".effects[" + std::to_string (i) + "]";
        specs.push_back (s);
    }
    return specs;
}

void MixerPanel::previewEffects (ChannelRef channel, const juce::Array<juce::var>& effects)
{
    preview ([&] (mr::Composition& c) { effectsIn (c, channel) = specsFrom (effects, basePathOf (c, channel)); });
}

bool MixerPanel::commitEffects (ChannelRef channel, const juce::Array<juce::var>& effects, const juce::String& name)
{
    juce::String error;
    if (project_.editChannel (channel, mr::withEffects (project_.channelJson (channel), effects), name, error))
        return true;
    status (error.replaceCharacter ('\n', ' '), true);
    if (onPreview && project_.loaded())
        onPreview (project_.composition());
    return false;
}

bool MixerPanel::addEffect (ChannelRef channel, const std::string& type)
{
    auto effects = mr::effectsJson (project_.channelJson (channel));
    auto* o = new juce::DynamicObject();
    o->setProperty ("type", juce::String (type));
    if (type == "reverb" && channel.kind == ChannelRef::Kind::bus)
        o->setProperty ("mix", 1);   // 센드 버스의 리버브는 잔향만 낸다
    effects.append (juce::var (o));
    if (! commitEffects (channel, *effects.getArray(), u8 ("효과 추가: ") + effectName (type)))
        return false;
    openEffect (channel, effects.size() - 1);
    return true;
}

bool MixerPanel::addPluginEffect (ChannelRef channel, const juce::var& effect)
{
    auto effects = mr::effectsJson (project_.channelJson (channel));
    effects.append (effect.clone());
    if (! commitEffects (channel, *effects.getArray(), u8 ("효과 추가: ") + effect.getProperty ("name", "").toString()))
        return false;
    openEffect (channel, effects.size() - 1);
    return true;
}

bool MixerPanel::removeEffect (ChannelRef channel, int index)
{
    auto effects = mr::effectsJson (project_.channelJson (channel));
    if (index < 0 || index >= effects.size())
        return false;
    const auto name = effectSlotName (project_.effectsOf (channel)[(size_t) index]);
    effects.remove (index);
    const bool wasOpen = isEffectOpen (channel, index);
    if (! commitEffects (channel, *effects.getArray(), u8 ("효과 지우기: ") + name))
        return false;
    if (wasOpen)
        openEffect (channel, -1);
    return true;
}

bool MixerPanel::moveEffect (ChannelRef channel, int index, int delta)
{
    auto effects = mr::effectsJson (project_.channelJson (channel));
    const int to = index + delta;
    if (index < 0 || index >= effects.size() || to < 0 || to >= effects.size())
        return false;
    auto* arr = effects.getArray();
    arr->swap (index, to);
    const bool wasOpen = isEffectOpen (channel, index);
    if (! commitEffects (channel, *arr, u8 ("효과 순서")))
        return false;
    if (wasOpen)
        openEffect (channel, to);
    return true;
}

bool MixerPanel::toggleBypass (ChannelRef channel, int index)
{
    auto effects = mr::effectsJson (project_.channelJson (channel));
    if (index < 0 || index >= effects.size())
        return false;
    const bool bypass = ! (bool) effects[index].getProperty ("bypass", false);
    effects.getArray()->set (index, mr::withEffectParameter (effects[index], "bypass", bypass ? juce::var (true) : juce::var()));
    return commitEffects (channel, *effects.getArray(), u8 (bypass ? "우회" : "우회 끄기") + ": " + effectSlotName (project_.effectsOf (channel)[(size_t) index]));
}

bool MixerPanel::addSend (size_t track, size_t bus)
{
    if (track >= project_.trackCount() || bus >= project_.busCount())
        return false;
    auto sends = project_.composition().tracks[track].sends;
    sends.push_back ({ project_.composition().buses[bus].name, -12.0, false });
    return commitSends (track, sends, u8 ("센드 추가: ") + project_.channelName (ChannelRef::track (track)));
}

bool MixerPanel::removeSend (size_t track, int index)
{
    auto sends = project_.composition().tracks.at (track).sends;
    if (index < 0 || index >= (int) sends.size())
        return false;
    sends.erase (sends.begin() + index);
    return commitSends (track, sends, u8 ("센드 지우기: ") + project_.channelName (ChannelRef::track (track)));
}

bool MixerPanel::setSendPreFader (size_t track, int index, bool preFader)
{
    auto sends = project_.composition().tracks.at (track).sends;
    if (index < 0 || index >= (int) sends.size())
        return false;
    sends[(size_t) index].preFader = preFader;
    return commitSends (track, sends, u8 (preFader ? "프리 페이더 센드" : "포스트 페이더 센드"));
}

void MixerPanel::setLearnMode (bool on)
{
    if (learnOverlay_ == nullptr)
    {
        learnOverlay_ = std::make_unique<MidiLearnOverlay> (*this);
        addChildComponent (*learnOverlay_);
    }
    if (learn != nullptr)
        learn->setLearning (on);
    learnOverlay_->setBounds (getLocalBounds());
    learnOverlay_->setVisible (on);
    if (on)
        learnOverlay_->toFront (false);
}

bool MixerPanel::learnMode() const
{
    return learnOverlay_ != nullptr && learnOverlay_->isVisible();
}

void MixerPanel::learnChanged()
{
    if (learnOverlay_ != nullptr)
        learnOverlay_->repaint();
}

std::vector<std::pair<LearnTarget, juce::Rectangle<int>>> MixerPanel::learnableControls()
{
    std::vector<std::pair<LearnTarget, juce::Rectangle<int>>> out;
    auto area = [this] (juce::Component& c) { return getLocalArea (&c, c.getLocalBounds()); };
    const auto visible = viewport_.getBounds();
    auto add = [&] (LearnTarget t, juce::Component& c, bool inRow)
    {
        if (! c.isVisible())
            return;
        const auto r = area (c);
        if (inRow && ! visible.contains (r.getCentre()))
            return;   // 옆으로 밀려 보이지 않는 스트립
        out.push_back ({ std::move (t), r });
    };
    auto strip = [&] (ChannelStrip& s, bool inRow)
    {
        const auto ch = s.channel();
        LearnTarget t;
        t.channel = ch;
        t.kind = LearnTarget::Kind::fader;
        add (t, s.fader(), inRow);
        if (ch.kind != ChannelRef::Kind::master)
        {
            t.kind = LearnTarget::Kind::pan;
            add (t, s.panKnob(), inRow);
        }
        t.kind = LearnTarget::Kind::mute;
        add (t, s.muteButton(), inRow);
        if (ch.kind == ChannelRef::Kind::track)
        {
            t.kind = LearnTarget::Kind::solo;
            add (t, s.soloButton(), inRow);
        }
    };
    for (auto& s : strips_)
        strip (*s, true);
    for (auto& v : vcaStrips_)
    {
        LearnTarget t;
        t.vca = v->index();
        t.kind = LearnTarget::Kind::vcaFader;
        add (t, v->fader(), true);
        t.kind = LearnTarget::Kind::vcaMute;
        add (t, v->muteButton(), true);
    }
    if (master_ != nullptr)
        strip (*master_, false);
    if (editor_ != nullptr && editor_->isVisible() && editor_->valid())
        for (const auto& [key, slider] : editor_->learnableSliders())
        {
            LearnTarget t;
            t.kind = LearnTarget::Kind::effect;
            t.channel = editor_->channel();
            t.effect = editor_->index();
            t.key = key;
            out.push_back ({ t, area (*slider) });
        }
    return out;
}

juce::StringArray MixerPanel::chainPresets() const
{
    juce::StringArray names;
    const auto folder = presetFolder.getChildFile ("Chains");
    if (presetFolder != juce::File() && folder.isDirectory())
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
            names.add (f.getFileNameWithoutExtension());
    names.sort (true);
    return names;
}

bool MixerPanel::saveChain (ChannelRef channel, const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (presetFolder == juce::File() || name.isEmpty() || ! project_.loaded())
        return false;
    // 사이드체인 키와 곡선은 곡의 것이라 빼고 쓴다
    auto effects = mr::effectsJson (project_.channelJson (channel));
    if (auto* arr = effects.getArray())
        for (auto& e : *arr)
            if (auto* o = e.getDynamicObject())
                for (const char* key : { "sidechain", "automation" })
                    o->removeProperty (key);
    const auto folder = presetFolder.getChildFile ("Chains");
    if (! folder.createDirectory() || ! folder.getChildFile (name + ".json").replaceWithText (juce::JSON::toString (effects)))
    {
        status (u8 ("효과 사슬을 쓰지 못했습니다: ") + folder.getFullPathName(), true);
        return false;
    }
    status (u8 ("효과 사슬을 저장했습니다: ") + name + " (" + juce::String (effects.size()) + u8 ("개)"));
    return true;
}

bool MixerPanel::loadChain (ChannelRef channel, const juce::String& name)
{
    const auto file = presetFolder.getChildFile ("Chains").getChildFile (name + ".json");
    const auto effects = juce::JSON::parse (file);
    if (presetFolder == juce::File() || ! file.existsAsFile() || effects.getArray() == nullptr)
        return false;
    return commitEffects (channel, *effects.getArray(), u8 ("효과 사슬 불러오기: ") + name);
}

void MixerPanel::askChainName (ChannelRef channel)
{
    nameDialog_ = std::make_unique<juce::AlertWindow> (u8 ("효과 사슬 저장"), u8 ("이 채널의 효과를 순서대로 내 효과 사슬로 저장합니다."), juce::MessageBoxIconType::NoIcon, this);
    nameDialog_->addTextEditor ("name", project_.channelName (channel));
    nameDialog_->addButton (u8 ("저장"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog_->addButton (u8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog_->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MixerPanel> (this), channel] (int result)
        {
            if (safe == nullptr || safe->nameDialog_ == nullptr)
                return;
            const auto text = safe->nameDialog_->getTextEditorContents ("name");
            safe->nameDialog_.reset();
            if (result == 1)
                safe->saveChain (channel, text);
        }), false);
}

juce::var MixerPanel::vcasJson() const
{
    auto v = project_.property ("vcas");
    return v.isArray() ? v : juce::var (juce::Array<juce::var>());
}

bool MixerPanel::editVcas (const juce::var& vcas, std::vector<Project::Change> extra, const juce::String& name)
{
    std::vector<Project::Change> changes;
    changes.push_back ({ -1, "vcas", vcas.getArray() != nullptr && vcas.getArray()->isEmpty() ? juce::var() : vcas });
    for (auto& c : extra)
        changes.push_back (std::move (c));
    juce::String error;
    if (! project_.edit (changes, name, error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        if (onPreview && project_.loaded())
            onPreview (project_.composition());
        return false;
    }
    return true;
}

bool MixerPanel::addVca (std::optional<ChannelRef> member)
{
    if (! project_.loaded())
        return false;
    juce::String name;
    const auto& vcas = project_.composition().vcas;
    for (int n = (int) vcas.size() + 1;; ++n)
    {
        name = "VCA " + juce::String (n);
        if (std::none_of (vcas.begin(), vcas.end(), [&] (const mr::VcaSpec& v) { return juce::String::fromUTF8 (v.name.c_str()) == name; }))
            break;
    }
    auto list = vcasJson();
    auto* v = new juce::DynamicObject();
    v->setProperty ("name", name);
    v->setProperty ("gainDb", 0);
    list.append (juce::var (v));
    std::vector<Project::Change> extra;
    if (member)
    {
        auto json = project_.channelJson (*member);
        json.getDynamicObject()->setProperty ("vca", name);
        extra.push_back (project_.channelChange (*member, json));
    }
    if (! editVcas (list, std::move (extra), u8 ("VCA 더하기")))
        return false;
    status (name + u8 (" 를 더했습니다. 트랙이나 버스의 이름 메뉴에서 VCA 에 넣습니다."));
    return true;
}

// 이름이 old 인 VCA 에 속한 채널의 "vca" 를 바꾼다 (비면 지운다)
static std::vector<Project::Change> renameMembers (const Project& project, const std::string& old, const juce::String& now)
{
    std::vector<Project::Change> changes;
    const auto& c = project.composition();
    for (size_t t = 0; t < c.tracks.size(); ++t)
        if (c.tracks[t].vca == old)
        {
            auto json = project.trackJson (t);
            if (now.isEmpty())
                json.getDynamicObject()->removeProperty ("vca");
            else
                json.getDynamicObject()->setProperty ("vca", now);
            changes.push_back ({ (int) t, {}, json });
        }
    auto buses = project.property ("buses");
    bool touched = false;
    if (auto* arr = buses.getArray())
        for (int b = 0; b < arr->size() && b < (int) c.buses.size(); ++b)
            if (c.buses[(size_t) b].vca == old)
            {
                if (now.isEmpty())
                    (*arr)[b].getDynamicObject()->removeProperty ("vca");
                else
                    (*arr)[b].getDynamicObject()->setProperty ("vca", now);
                touched = true;
            }
    if (touched)
        changes.push_back ({ -1, "buses", buses });
    return changes;
}

bool MixerPanel::renameVca (size_t vca, const juce::String& newName)
{
    const auto& vcas = project_.composition().vcas;
    const auto name = newName.trim();
    if (vca >= vcas.size() || name.isEmpty())
        return false;
    if (std::any_of (vcas.begin(), vcas.end(), [&] (const mr::VcaSpec& v) { return juce::String::fromUTF8 (v.name.c_str()) == name; }))
    {
        status (u8 ("같은 이름의 VCA 가 있습니다: ") + name, true);
        return false;
    }
    const auto old = vcas[vca].name;
    auto list = vcasJson();
    (*list.getArray())[(int) vca].getDynamicObject()->setProperty ("name", name);
    return editVcas (list, renameMembers (project_, old, name), u8 ("VCA 이름 바꾸기: ") + name);
}

bool MixerPanel::removeVca (size_t vca)
{
    const auto& vcas = project_.composition().vcas;
    if (vca >= vcas.size())
        return false;
    const auto old = vcas[vca].name;
    auto list = vcasJson();
    list.getArray()->remove ((int) vca);
    return editVcas (list, renameMembers (project_, old, {}), u8 ("VCA 지우기: ") + juce::String::fromUTF8 (old.c_str()));
}

void MixerPanel::askVcaName (size_t vca)
{
    if (vca >= project_.composition().vcas.size())
        return;
    nameDialog_ = std::make_unique<juce::AlertWindow> (u8 ("VCA 이름"), u8 ("속한 채널이 가리키는 이름도 함께 바뀝니다."), juce::MessageBoxIconType::NoIcon, this);
    nameDialog_->addTextEditor ("name", juce::String::fromUTF8 (project_.composition().vcas[vca].name.c_str()));
    nameDialog_->addButton (u8 ("바꾸기"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog_->addButton (u8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog_->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MixerPanel> (this), vca] (int result)
        {
            if (safe == nullptr || safe->nameDialog_ == nullptr)
                return;
            const auto text = safe->nameDialog_->getTextEditorContents ("name");
            safe->nameDialog_.reset();
            if (result == 1)
                safe->renameVca (vca, text);
        }), false);
}

void MixerPanel::previewVca (size_t vca, double gainDb)
{
    preview ([&] (mr::Composition& c)
    {
        if (vca < c.vcas.size())
        {
            c.vcas[vca].gainDb = gainDb;
            mr::resolveVcas (c);
        }
    });
}

bool MixerPanel::setVca (size_t vca, double gainDb, bool mute, const juce::String& name)
{
    if (vca >= project_.composition().vcas.size())
        return false;
    auto list = vcasJson();
    auto* o = (*list.getArray())[(int) vca].getDynamicObject();
    o->setProperty ("gainDb", std::abs (gainDb) < 1.0e-9 ? juce::var (0) : juce::var (gainDb));
    if (mute || o->hasProperty ("mute"))
        o->setProperty ("mute", mute);
    if (juce::JSON::toString (list) == juce::JSON::toString (vcasJson()))
        return false;
    return editVcas (list, {}, name + ": " + juce::String::fromUTF8 (project_.composition().vcas[vca].name.c_str()));
}

bool MixerPanel::assignVca (ChannelRef channel, const std::string& vca)
{
    if (channel.kind == ChannelRef::Kind::master)
        return false;
    auto json = project_.channelJson (channel);
    if (vca.empty())
        json.getDynamicObject()->removeProperty ("vca");
    else
        json.getDynamicObject()->setProperty ("vca", juce::String::fromUTF8 (vca.c_str()));
    juce::String error;
    const auto name = (vca.empty() ? u8 ("VCA 에서 빼기: ") : u8 ("VCA 에 넣기: ")) + project_.channelName (channel);
    if (! project_.editChannel (channel, json, name, error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    return true;
}

juce::PopupMenu MixerPanel::vcaMenu (ChannelRef channel)
{
    juce::PopupMenu m;
    if (! project_.loaded() || channel.kind == ChannelRef::Kind::master)
        return m;
    const auto& c = project_.composition();
    const auto current = channel.kind == ChannelRef::Kind::track ? c.tracks[channel.index].vca : c.buses[channel.index].vca;
    m.addItem (u8 ("없음"), true, current.empty(), [this, channel] { assignVca (channel, {}); });
    for (const auto& v : c.vcas)
        m.addItem (juce::String::fromUTF8 (v.name.c_str()), true, v.name == current, [this, channel, name = v.name] { assignVca (channel, name); });
    m.addSeparator();
    m.addItem (u8 ("새 VCA 를 만들어 넣기"), [this, channel] { addVca (channel); });
    return m;
}

bool MixerPanel::addBus()
{
    if (! project_.loaded())
        return false;
    juce::String name;
    for (int n = (int) project_.busCount() + 1;; ++n)
    {
        name = u8 ("버스 ") + juce::String (n);
        const auto& buses = project_.composition().buses;
        if (std::none_of (buses.begin(), buses.end(), [&] (const mr::BusSpec& b) { return juce::String::fromUTF8 (b.name.c_str()) == name; }))
            break;
    }
    auto buses = project_.property ("buses");
    if (buses.getArray() == nullptr)
        buses = juce::var (juce::Array<juce::var>());
    auto* bus = new juce::DynamicObject();
    bus->setProperty ("name", name);
    bus->setProperty ("effects", juce::JSON::parse (R"([ { "type": "reverb", "mix": 1 } ])"));
    buses.append (juce::var (bus));
    juce::String error;
    if (! project_.edit ({ Project::Change { -1, "buses", buses } }, u8 ("버스 추가"), error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    status (name + u8 (" 을 더했습니다. 트랙의 센드 슬롯에서 이 버스로 보냅니다."));
    return true;
}

bool MixerPanel::renameBus (size_t bus, const juce::String& newName)
{
    if (bus >= project_.busCount())
        return false;
    const auto name = newName.trim();
    const auto old = project_.composition().buses[bus].name;
    const auto newText = name.toStdString();
    if (name.isEmpty() || newText == old)
        return false;
    for (const auto& b : project_.composition().buses)
        if (b.name == newText)
        {
            status (u8 ("같은 이름의 버스가 있습니다: ") + name, true);
            return false;
        }
    std::vector<Project::Change> changes;
    auto buses = project_.property ("buses");
    buses.getArray()->getReference ((int) bus).getDynamicObject()->setProperty ("name", name);
    changes.push_back ({ -1, "buses", buses });
    for (size_t t = 0; t < project_.trackCount(); ++t)
    {
        auto sends = project_.composition().tracks[t].sends;
        bool touched = false;
        for (auto& s : sends)
            if (s.bus == old)
            {
                s.bus = newText;
                touched = true;
            }
        auto json = touched ? mr::withSends (project_.trackJson (t), sends) : project_.trackJson (t);
        if (project_.composition().tracks[t].output == old)   // 이 버스로 나가는 트랙
        {
            json.getDynamicObject()->setProperty ("output", name);
            touched = true;
        }
        if (touched)
            changes.push_back ({ (int) t, {}, json });
    }
    juce::String error;
    if (! project_.edit (changes, u8 ("버스 이름 바꾸기"), error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    return true;
}

bool MixerPanel::removeBus (size_t bus)
{
    if (bus >= project_.busCount())
        return false;
    const auto old = project_.composition().buses[bus].name;
    std::vector<Project::Change> changes;
    auto buses = project_.property ("buses");
    buses.getArray()->remove ((int) bus);
    changes.push_back ({ -1, "buses", buses });
    for (size_t t = 0; t < project_.trackCount(); ++t)
    {
        auto sends = project_.composition().tracks[t].sends;
        const auto before = sends.size();
        sends.erase (std::remove_if (sends.begin(), sends.end(), [&] (const mr::SendSpec& s) { return s.bus == old; }), sends.end());
        bool touched = sends.size() != before;
        auto json = touched ? mr::withSends (project_.trackJson (t), sends) : project_.trackJson (t);
        if (project_.composition().tracks[t].output == old)   // 이 버스로 나가던 트랙은 마스터로
        {
            json.getDynamicObject()->removeProperty ("output");
            touched = true;
        }
        if (touched)
            changes.push_back ({ (int) t, {}, json });
    }
    if (editor_->isVisible() && editor_->channel().kind == ChannelRef::Kind::bus)
        openEffect (ChannelRef::master(), -1);
    juce::String error;
    if (! project_.edit (changes, u8 ("버스 지우기: ") + juce::String::fromUTF8 (old.c_str()), error))
    {
        status (error.replaceCharacter ('\n', ' '), true);
        return false;
    }
    return true;
}

void MixerPanel::askBusName (size_t bus)
{
    if (bus >= project_.busCount())
        return;
    nameDialog_ = std::make_unique<juce::AlertWindow> (u8 ("버스 이름"), u8 ("이 버스로 가는 센드의 이름도 함께 바뀝니다."),
                                                       juce::MessageBoxIconType::NoIcon, this);
    nameDialog_->addTextEditor ("name", project_.channelName (ChannelRef::bus (bus)));
    nameDialog_->addButton (u8 ("바꾸기"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    nameDialog_->addButton (u8 ("취소"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    nameDialog_->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MixerPanel> (this), bus] (int result)
        {
            if (safe == nullptr || safe->nameDialog_ == nullptr)
                return;
            const auto text = safe->nameDialog_->getTextEditorContents ("name");
            safe->nameDialog_.reset();
            if (result == 1)
                safe->renameBus (bus, text);
        }), false);
}

void MixerPanel::openEffect (ChannelRef channel, int index)
{
    // 플러그인 효과는 그 플러그인의 창으로
    if (index >= 0 && project_.loaded() && (size_t) index < project_.effectsOf (channel).size()
        && project_.effectsOf (channel)[(size_t) index].type == "plugin")
    {
        if (onOpenPluginEffect)
            onOpenPluginEffect (channel, (size_t) index);
        return;
    }
    if (index < 0)
        editor_->setVisible (false);
    else
    {
        editor_->show (channel, index);
        editor_->setVisible (true);
    }
    resized();
    if (index >= 0)
        scrollToShow (channel);
    for (auto& s : strips_)
        s->repaint();
    if (master_ != nullptr)
        master_->repaint();
}

bool MixerPanel::isEffectOpen (ChannelRef channel, int index) const
{
    return editor_->isVisible() && editor_->channel() == channel && editor_->index() == index;
}

} // namespace daw
