#include "TransportBar.h"

#include "Theme.h"

namespace daw
{

IconButton::IconButton (const juce::String& name, Icon icon) : juce::Button (name), icon_ (icon)
{
    setTooltip (name);
}

void IconButton::setIcon (Icon icon)
{
    icon_ = icon;
    repaint();
}

void IconButton::setOn (bool on)
{
    if (on_ == on)
        return;
    on_ = on;
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool toggle = icon_ == Icon::record || icon_ == Icon::loop || icon_ == Icon::metronome;
    const bool primary = icon_ == Icon::play || icon_ == Icon::stop;
    auto fill = primary ? (icon_ == Icon::stop ? colours::accent : colours::panelRaised.brighter (0.08f)) : colours::panelRaised;
    if (toggle && on_)
        fill = icon_ == Icon::record ? colours::danger : colours::accent;
    if (! isEnabled())
        fill = colours::panel;
    else if (down)
        fill = fill.brighter (0.2f);
    else if (highlighted)
        fill = fill.brighter (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);

    const auto c = r.getCentre();
    const float s = std::min (r.getWidth(), r.getHeight()) * 0.22f;
    const bool dark = icon_ == Icon::stop || (toggle && on_);
    const auto ink = ! isEnabled() ? colours::textFaint : (dark ? juce::Colours::black : (icon_ == Icon::record ? colours::danger : colours::text));
    juce::Path p;
    switch (icon_)
    {
        case Icon::play:
            p.addTriangle (c.x - s * 0.8f, c.y - s, c.x - s * 0.8f, c.y + s, c.x + s * 1.1f, c.y);
            break;
        case Icon::stop:
            p.addRoundedRectangle (c.x - s * 0.9f, c.y - s * 0.9f, s * 1.8f, s * 1.8f, 2.0f);
            break;
        case Icon::toStart:
            p.addRectangle (c.x - s * 1.1f, c.y - s, s * 0.35f, s * 2.0f);
            p.addTriangle (c.x + s, c.y - s, c.x + s, c.y + s, c.x - s * 0.6f, c.y);
            break;
        case Icon::record:
            p.addEllipse (c.x - s, c.y - s, s * 2.0f, s * 2.0f);
            break;
        case Icon::loop:
        {
            // 두 화살표가 도는 고리
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, s * 1.1f, s * 0.85f, 0.0f, 0.2f, juce::MathConstants<float>::pi * 0.95f, true);
            arc.addCentredArc (c.x, c.y, s * 1.1f, s * 0.85f, 0.0f, juce::MathConstants<float>::pi + 0.2f, juce::MathConstants<float>::twoPi * 0.975f, true);
            g.setColour (ink);
            g.strokePath (arc, juce::PathStrokeType (1.6f));
            p.addTriangle (c.x + s * 1.1f, c.y - s * 0.75f, c.x + s * 1.1f + s * 0.55f, c.y - s * 0.1f, c.x + s * 1.1f - s * 0.55f, c.y - s * 0.1f);
            p.addTriangle (c.x - s * 1.1f, c.y + s * 0.75f, c.x - s * 1.1f - s * 0.55f, c.y + s * 0.1f, c.x - s * 1.1f + s * 0.55f, c.y + s * 0.1f);
            break;
        }
        case Icon::metronome:
        {
            // 사다리꼴 몸통과 기울어진 추
            p.addQuadrilateral (c.x - s * 0.5f, c.y - s * 1.1f, c.x + s * 0.5f, c.y - s * 1.1f, c.x + s * 1.0f, c.y + s * 1.1f, c.x - s * 1.0f, c.y + s * 1.1f);
            g.setColour (ink);
            g.fillPath (p);
            g.setColour (fill);
            g.drawLine (c.x, c.y + s * 0.8f, c.x + s * 0.6f, c.y - s * 1.0f, 1.6f);
            return;
        }
    }
    g.setColour (ink);
    g.fillPath (p);
}

TransportBar::TransportBar()
{
    title_.setFont (uiFont (17.0f, true));
    title_.setColour (juce::Label::textColourId, colours::text);
    addAndMakeVisible (title_);

    toStart_.onClick = [this] { if (onToStart) onToStart(); };
    playStop_.onClick = [this] { if (onPlayStop) onPlayStop(); };
    addAndMakeVisible (toStart_);
    addAndMakeVisible (playStop_);
    record_.onClick = [this] { if (onRecord) onRecord(); };
    loop_.onClick = [this] { if (onLoop) onLoop(); };
    metronome_.onClick = [this] { if (onMetronome) onMetronome(); };
    for (auto* b : { &record_, &loop_, &metronome_ })
    {
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    }

    monitor_.setSliderStyle (juce::Slider::LinearHorizontal);
    monitor_.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    monitor_.setRange (-24.0, 24.0, 0.5);
    monitor_.setDoubleClickReturnValue (true, 0.0);
    monitor_.setTooltip (juce::String::fromUTF8 ("재생할 때만 쓰는 음량입니다. 내보내기는 프로젝트의 목표 음량을 따릅니다."));
    monitor_.onValueChange = [this]
    {
        monitorLabel_.setText (juce::String::fromUTF8 ("모니터 게인 ") + (monitor_.getValue() > 0 ? "+" : "")
                                   + juce::String (monitor_.getValue(), 1) + " dB", juce::dontSendNotification);
        if (onMonitorGainChanged)
            onMonitorGainChanged (monitor_.getValue());
    };
    addAndMakeVisible (monitor_);
    monitorLabel_.setFont (uiFont (12.0f));
    monitorLabel_.setColour (juce::Label::textColourId, colours::textDim);
    monitorLabel_.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (monitorLabel_);
    setMonitorGainDb (0.0);

    typing_.setClickingTogglesState (false);
    typing_.setWantsKeyboardFocus (false);   // 눌러도 자판 입력을 가져가지 않는다 (스페이스가 단추를 누르지 않게)
    typing_.setTooltip (juce::String::fromUTF8 ("컴퓨터 자판으로 고른 트랙을 연주합니다. Z 줄과 Q 줄이 흰건반, 그 위 줄이 검은건반입니다. ⌘K 켜고 끄기, ⌥↑ ⌥↓ 옥타브"));
    typing_.setColour (juce::TextButton::buttonOnColourId, colours::accent.withAlpha (0.8f));
    typing_.onClick = [this] { if (onTypingClicked) onTypingClicked(); };
    addAndMakeVisible (typing_);
    setTyping (false, {});

    status_.setFont (uiFont (13.0f));
    status_.setJustificationType (juce::Justification::centredRight);
    status_.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (status_);

    setPosition (1, 1, 0.0);
    setTempo (120.0, 4, 4);
}

void TransportBar::setTitle (const juce::String& title) { title_.setText (title, juce::dontSendNotification); }

void TransportBar::setPlaying (bool playing)
{
    playStop_.setIcon (playing ? IconButton::Icon::stop : IconButton::Icon::play);
    playStop_.setTooltip (juce::String::fromUTF8 (playing ? "정지 (스페이스)" : "재생 (스페이스)"));
}

void TransportBar::setPosition (int bar, int beat, double seconds)
{
    const auto text = juce::String (bar) + "." + juce::String (beat);
    const int minutes = (int) (seconds / 60.0);
    const auto time = juce::String::formatted ("%02d:%04.1f", minutes, seconds - minutes * 60.0);
    if (text == positionText_ && time == timeText_)
        return;
    positionText_ = text;
    timeText_ = time;
    repaint (positionArea_);
}

void TransportBar::setTempo (double bpm, int beatsPerBar, int beatUnit)
{
    const auto t = juce::String (bpm, bpm == std::floor (bpm) ? 0 : 1) + " BPM";
    const auto sig = juce::String (beatsPerBar) + "/" + juce::String (beatUnit);
    if (t == tempoText_ && sig == signatureText_)
        return;
    tempoText_ = t;
    signatureText_ = sig;
    repaint (tempoArea_);
}

void TransportBar::setStatus (const juce::String& text, bool error)
{
    statusError_ = error;
    status_.setColour (juce::Label::textColourId, error ? colours::danger : colours::textDim);
    status_.setText (text, juce::dontSendNotification);
    status_.setTooltip (text);
}

void TransportBar::setCpu (double load)
{
    const auto t = "CPU " + juce::String (juce::roundToInt (load * 100.0)) + "%";
    if (t == cpuText_)
        return;
    cpuText_ = t;
    repaint (cpuArea_);
}

void TransportBar::setMonitorGainDb (double db) { monitor_.setValue (db, juce::sendNotificationSync); }

void TransportBar::setTransportEnabled (bool enabled)
{
    playStop_.setEnabled (enabled);
    toStart_.setEnabled (enabled);
    record_.setEnabled (enabled);
}

void TransportBar::setRecording (bool on) { record_.setOn (on); }
void TransportBar::setLooping (bool on) { loop_.setOn (on); }
void TransportBar::setMetronome (bool on) { metronome_.setOn (on); }

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (colours::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colours::outline);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // 위치: 마디.박 (크게) 과 시각
    auto pos = positionArea_.toFloat();
    g.setColour (colours::background);
    g.fillRoundedRectangle (pos, 6.0f);
    g.setColour (colours::outline);
    g.drawRoundedRectangle (pos.reduced (0.5f), 6.0f, 1.0f);
    auto inner = positionArea_.reduced (12, 4);
    g.setColour (colours::text);
    g.setFont (monoFont (22.0f));
    g.drawText (positionText_, inner.removeFromLeft (inner.getWidth() * 55 / 100), juce::Justification::centredLeft);
    g.setColour (colours::textDim);
    g.setFont (monoFont (14.0f));
    g.drawText (timeText_, inner, juce::Justification::centredRight);

    auto tempo = tempoArea_;
    g.setColour (colours::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText (tempoText_, tempo.removeFromTop (tempo.getHeight() / 2 + 2), juce::Justification::bottomLeft);
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f));
    g.drawText (juce::String::fromUTF8 ("박자 ") + signatureText_, tempo, juce::Justification::topLeft);

    g.setColour (colours::textFaint);
    g.setFont (monoFont (12.0f));
    g.drawText (cpuText_, cpuArea_, juce::Justification::centredRight);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    title_.setBounds (r.removeFromLeft (220));
    r.removeFromLeft (8);
    toStart_.setBounds (r.removeFromLeft (40).withSizeKeepingCentre (40, 36));
    r.removeFromLeft (6);
    playStop_.setBounds (r.removeFromLeft (52).withSizeKeepingCentre (52, 36));
    r.removeFromLeft (6);
    record_.setBounds (r.removeFromLeft (40).withSizeKeepingCentre (40, 36));
    r.removeFromLeft (12);
    positionArea_ = r.removeFromLeft (220).withSizeKeepingCentre (220, 38);
    r.removeFromLeft (16);
    tempoArea_ = r.removeFromLeft (100);
    r.removeFromLeft (12);
    auto monitor = r.removeFromLeft (170);
    monitorLabel_.setBounds (monitor.removeFromTop (monitor.getHeight() / 2));
    monitor_.setBounds (monitor);
    r.removeFromLeft (12);
    typing_.setBounds (r.removeFromLeft (118).withSizeKeepingCentre (118, 30));
    r.removeFromLeft (8);
    loop_.setBounds (r.removeFromLeft (36).withSizeKeepingCentre (36, 30));
    r.removeFromLeft (4);
    metronome_.setBounds (r.removeFromLeft (36).withSizeKeepingCentre (36, 30));
    cpuArea_ = r.removeFromRight (70);
    r.removeFromRight (8);
    status_.setBounds (r);
}

void TransportBar::setTyping (bool on, const juce::String& range)
{
    typing_.setToggleState (on, juce::dontSendNotification);
    typing_.setButtonText (on ? juce::String::fromUTF8 ("자판 연주 ") + range : juce::String::fromUTF8 ("자판 연주 끔"));
}

void TransportBar::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (tempoArea_.contains (e.getPosition()) && onTempoClicked)
        onTempoClicked();
}

} // namespace daw
