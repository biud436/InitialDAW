#include "MidiLearn.h"

#include "MixerPanel.h"
#include "MixerText.h"
#include "Theme.h"

#include "composition/Document.h"
#include "effects/Effects.h"

#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

std::string channelText (ChannelRef c)
{
    switch (c.kind)
    {
        case ChannelRef::Kind::track: return "track:" + std::to_string (c.index);
        case ChannelRef::Kind::bus: return "bus:" + std::to_string (c.index);
        case ChannelRef::Kind::master: break;
    }
    return "master";
}

constexpr double kGestureMs = 400.0;

} // namespace

std::string LearnTarget::text() const
{
    switch (kind)
    {
        case Kind::vcaFader: return "vca:" + std::to_string (vca) + "/fader";
        case Kind::vcaMute: return "vca:" + std::to_string (vca) + "/mute";
        case Kind::fader: return channelText (channel) + "/fader";
        case Kind::pan: return channelText (channel) + "/pan";
        case Kind::mute: return channelText (channel) + "/mute";
        case Kind::solo: return channelText (channel) + "/solo";
        case Kind::effect: return channelText (channel) + "/effect:" + std::to_string (effect) + "/" + key;
    }
    return {};
}

std::optional<LearnTarget> LearnTarget::parse (const std::string& text)
{
    const auto parts = juce::StringArray::fromTokens (juce::String (text), "/", "");
    if (parts.size() < 2)
        return std::nullopt;
    LearnTarget t;
    const auto head = parts[0];
    const int number = head.fromFirstOccurrenceOf (":", false, false).getIntValue();
    if (head.startsWith ("vca:"))
    {
        t.vca = (size_t) std::max (0, number);
        if (parts[1] == "fader") t.kind = Kind::vcaFader;
        else if (parts[1] == "mute") t.kind = Kind::vcaMute;
        else return std::nullopt;
        return t;
    }
    if (head.startsWith ("track:"))
        t.channel = ChannelRef::track ((size_t) std::max (0, number));
    else if (head.startsWith ("bus:"))
        t.channel = ChannelRef::bus ((size_t) std::max (0, number));
    else if (head != "master")
        return std::nullopt;
    if (parts[1] == "fader") t.kind = Kind::fader;
    else if (parts[1] == "pan") t.kind = Kind::pan;
    else if (parts[1] == "mute") t.kind = Kind::mute;
    else if (parts[1] == "solo") t.kind = Kind::solo;
    else if (parts[1].startsWith ("effect:") && parts.size() >= 3)
    {
        t.kind = Kind::effect;
        t.effect = parts[1].fromFirstOccurrenceOf (":", false, false).getIntValue();
        t.key = parts[2].toStdString();
    }
    else
        return std::nullopt;
    return t;
}

MidiLearn::MidiLearn (MixerPanel& mixer, Project& project) : mixer_ (mixer), project_ (project) {}

MidiLearn::~MidiLearn() { stopTimer(); }

void MidiLearn::setLearning (bool on)
{
    learning_ = on;
    armed_.reset();
    if (onChanged)
        onChanged();
}

void MidiLearn::arm (const LearnTarget& target)
{
    armed_ = target;
    status (describe (target) + u8 (": 묶을 컨트롤러를 움직입니다."));
    if (onChanged)
        onChanged();
}

std::optional<LearnBinding> MidiLearn::bindingOf (const LearnTarget& target) const
{
    for (const auto& b : bindings_)
        if (b.target == target)
            return b;
    return std::nullopt;
}

void MidiLearn::unbind (const LearnTarget& target)
{
    bindings_.erase (std::remove_if (bindings_.begin(), bindings_.end(), [&] (const LearnBinding& b) { return b.target == target; }), bindings_.end());
    if (onChanged)
        onChanged();
}

void MidiLearn::clear()
{
    bindings_.clear();
    if (onChanged)
        onChanged();
}

juce::String MidiLearn::describe (const LearnTarget& t) const
{
    auto channelName = [&] (ChannelRef c)
    {
        juce::String text;
        switch (c.kind)
        {
            case ChannelRef::Kind::track: text = u8 ("트랙 ") + juce::String ((int) c.index + 1); break;
            case ChannelRef::Kind::bus: text = u8 ("버스 ") + juce::String ((int) c.index + 1); break;
            case ChannelRef::Kind::master: return u8 ("마스터");
        }
        if (project_.loaded() && ((c.kind == ChannelRef::Kind::track && c.index < project_.trackCount()) || (c.kind == ChannelRef::Kind::bus && c.index < project_.busCount())))
            text << " (" << project_.channelName (c) << ")";
        return text;
    };
    switch (t.kind)
    {
        case LearnTarget::Kind::fader: return channelName (t.channel) + u8 (" 페이더");
        case LearnTarget::Kind::pan: return channelName (t.channel) + u8 (" 밸런스");
        case LearnTarget::Kind::mute: return channelName (t.channel) + u8 (" 뮤트");
        case LearnTarget::Kind::solo: return channelName (t.channel) + u8 (" 솔로");
        case LearnTarget::Kind::vcaFader:
        case LearnTarget::Kind::vcaMute:
        {
            juce::String name = "VCA " + juce::String ((int) t.vca + 1);
            if (project_.loaded() && t.vca < project_.composition().vcas.size())
                name << " (" << juce::String::fromUTF8 (project_.composition().vcas[t.vca].name.c_str()) << ")";
            return name + (t.kind == LearnTarget::Kind::vcaFader ? u8 (" 페이더") : u8 (" 뮤트"));
        }
        case LearnTarget::Kind::effect:
        {
            juce::String text = channelName (t.channel) + u8 (" 효과 ") + juce::String (t.effect + 1);
            if (project_.loaded())
            {
                const auto& effects = project_.effectsOf (t.channel);
                if (t.effect >= 0 && (size_t) t.effect < effects.size())
                    text << " " << parameterLabel (effects[(size_t) t.effect].type, t.key);
            }
            return text;
        }
    }
    return {};
}

bool MidiLearn::handle (const juce::MidiMessage& m)
{
    if (! m.isController())
        return false;
    const int channel = m.getChannel(), cc = m.getControllerNumber(), value = m.getControllerValue();
    if (learning_ && armed_)
    {
        // 같은 컨트롤러나 같은 자리의 앞 묶음은 지운다
        bindings_.erase (std::remove_if (bindings_.begin(), bindings_.end(), [&] (const LearnBinding& b)
                                         { return (b.midiChannel == channel && b.controller == cc) || b.target == *armed_; }),
                         bindings_.end());
        bindings_.push_back ({ channel, cc, *armed_ });
        status ("CC " + juce::String (cc) + u8 (" (채널 ") + juce::String (channel) + u8 (") 을 ") + describe (*armed_) + u8 ("에 묶었습니다."));
        armed_.reset();
        if (onChanged)
            onChanged();
        return true;
    }
    const auto key = std::to_string (channel) + ":" + std::to_string (cc);
    const int previous = lastValues_.count (key) ? lastValues_[key] : -1;
    lastValues_[key] = value;
    bool used = false;
    for (const auto& b : bindings_)
        if (b.midiChannel == channel && b.controller == cc)
        {
            move (b.target, value, previous);
            used = true;
        }
    return used;
}

void MidiLearn::move (const LearnTarget& t, int value, int previous)
{
    if (! project_.loaded())
        return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double x = value / 127.0;
    // 단추: 64 를 넘어 오를 때 한 번 누른다
    auto press = [&] (juce::Button* b)
    {
        if (b != nullptr && value >= 64 && previous < 64 && b->onClick)
            b->onClick();
    };
    auto gesture = [&]() -> Gesture&
    {
        for (auto& g : gestures_)
            if (g.target == t)
            {
                g.lastMs = now;
                return g;
            }
        gestures_.push_back ({ t, now, {} });
        startTimer (100);
        return gestures_.back();
    };
    // 슬라이더는 손으로 끄는 것처럼: 처음 값에서 끌기를 시작하고 값을 바꾼다
    auto slide = [&] (juce::Slider* s, double v)
    {
        if (s == nullptr || ! s->isEnabled())
            return;
        const bool fresh = std::none_of (gestures_.begin(), gestures_.end(), [&] (const Gesture& g) { return g.target == t; });
        gesture();
        if (fresh && s->onDragStart)
            s->onDragStart();
        s->setValue (v, juce::sendNotificationSync);
    };
    switch (t.kind)
    {
        case LearnTarget::Kind::fader:
            if (auto* strip = mixer_.strip (t.channel))
                slide (&strip->fader(), faderlaw::toDb (x));
            break;
        case LearnTarget::Kind::pan:
            if (auto* strip = mixer_.strip (t.channel); strip != nullptr && t.channel.kind != ChannelRef::Kind::master)
                slide (&strip->panKnob(), value == 64 ? 0.0 : std::round ((x * 2.0 - 1.0) * 100.0) / 100.0);
            break;
        case LearnTarget::Kind::mute:
            if (auto* strip = mixer_.strip (t.channel))
                press (&strip->muteButton());
            break;
        case LearnTarget::Kind::solo:
            if (auto* strip = mixer_.strip (t.channel); strip != nullptr && t.channel.kind == ChannelRef::Kind::track)
                press (&strip->soloButton());
            break;
        case LearnTarget::Kind::vcaFader:
            if (auto* strip = mixer_.vcaStrip (t.vca))
                slide (&strip->fader(), faderlaw::toDb (x));
            break;
        case LearnTarget::Kind::vcaMute:
            if (auto* strip = mixer_.vcaStrip (t.vca))
                press (&strip->muteButton());
            break;
        case LearnTarget::Kind::effect:
        {
            const auto& effects = project_.effectsOf (t.channel);
            if (t.effect < 0 || (size_t) t.effect >= effects.size())
                break;
            const auto* info = mr::findEffectParameter (effects[(size_t) t.effect].type, t.key);
            if (info == nullptr)
                break;
            // 값: 주파수와 시간은 로그로, 켜기와 끄기는 반에서, 선택지는 가까운 것
            double v = info->minimum + (info->maximum - info->minimum) * x;
            if (info->logarithmic && info->minimum > 0.0)
                v = info->minimum * std::pow (info->maximum / info->minimum, x);
            if (info->toggle)
                v = x >= 0.5 ? 1.0 : 0.0;
            if (! info->choices.empty())
                v = info->choices[(size_t) std::min ((int) info->choices.size() - 1, (int) (x * (double) info->choices.size()))];
            v = roundParameter (*info, v);
            auto& g = gesture();
            g.effects = *mr::effectsJson (project_.channelJson (t.channel)).getArray();
            g.effects.set (t.effect, mr::withEffectParameter (g.effects[t.effect], juce::Identifier (t.key), v));
            mixer_.previewEffects (t.channel, g.effects);
            break;
        }
    }
}

void MidiLearn::finish (Gesture& g)
{
    switch (g.target.kind)
    {
        case LearnTarget::Kind::fader:
        case LearnTarget::Kind::pan:
            if (auto* strip = mixer_.strip (g.target.channel))
            {
                auto& s = g.target.kind == LearnTarget::Kind::fader ? strip->fader() : strip->panKnob();
                if (s.onDragEnd)
                    s.onDragEnd();
            }
            break;
        case LearnTarget::Kind::vcaFader:
            if (auto* strip = mixer_.vcaStrip (g.target.vca); strip != nullptr && strip->fader().onDragEnd)
                strip->fader().onDragEnd();
            break;
        case LearnTarget::Kind::effect:
            if (! g.effects.isEmpty())
                mixer_.commitEffects (g.target.channel, g.effects, u8 ("MIDI 컨트롤러: ") + describe (g.target));
            break;
        case LearnTarget::Kind::mute:
        case LearnTarget::Kind::solo:
        case LearnTarget::Kind::vcaMute:
            break;
    }
}

void MidiLearn::finishGestures()
{
    auto done = std::move (gestures_);
    gestures_.clear();
    for (auto& g : done)
        finish (g);
    stopTimer();
}

void MidiLearn::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    std::vector<Gesture> done;
    for (auto it = gestures_.begin(); it != gestures_.end();)
        if (now - it->lastMs >= kGestureMs)
        {
            done.push_back (std::move (*it));
            it = gestures_.erase (it);
        }
        else
            ++it;
    for (auto& g : done)
        finish (g);
    if (gestures_.empty())
        stopTimer();
}

juce::String MidiLearn::toSettings() const
{
    juce::Array<juce::var> list;
    for (const auto& b : bindings_)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("channel", b.midiChannel);
        o->setProperty ("cc", b.controller);
        o->setProperty ("target", juce::String (b.target.text()));
        list.add (juce::var (o));
    }
    return juce::JSON::toString (list, true);
}

void MidiLearn::fromSettings (const juce::String& json)
{
    bindings_.clear();
    const auto parsed = juce::JSON::parse (json);
    if (const auto* list = parsed.getArray())
        for (const auto& v : *list)
            if (auto t = LearnTarget::parse (v.getProperty ("target", "").toString().toStdString()))
                bindings_.push_back ({ juce::jlimit (1, 16, (int) v.getProperty ("channel", 1)), juce::jlimit (0, 127, (int) v.getProperty ("cc", 0)), *t });
}

} // namespace daw
