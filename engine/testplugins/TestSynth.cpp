// music-render 의 자동 테스트용 VST3, AU 악기. 결정적인 사인파 신스다.
//   Level (0..1), Attack (ms), Release (ms), Latency (샘플): Latency 만큼 출력을 늦추고 호스트에 알린다
//   프로그램 둘: Default (Level 0.8), Quiet (Level 0.25). 상태는 매개변수 값이다.

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>
#include <vector>

namespace
{

class TestSynth final : public juce::AudioProcessor
{
public:
    TestSynth()
        : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state_ (*this, nullptr, "MusicRenderTestSynth", createLayout())
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "level", 1 }, "Level", 0.0f, 1.0f, 0.8f));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "attack", 1 }, "Attack",
                                                                 juce::NormalisableRange<float> (0.0f, 1000.0f), 5.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "release", 1 }, "Release",
                                                                 juce::NormalisableRange<float> (1.0f, 2000.0f), 50.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("ms")));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "latency", 1 }, "Latency", 0, 4096, 0));
        return layout;
    }

    const juce::String getName() const override { return "MusicRender Test Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }

    int getNumPrograms() override { return 2; }
    int getCurrentProgram() override { return program_; }
    const juce::String getProgramName (int i) override { return i == 1 ? "Quiet" : "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void setCurrentProgram (int i) override
    {
        program_ = juce::jlimit (0, 1, i);
        if (auto* p = state_.getParameter ("level"))
            p->setValueNotifyingHost (program_ == 1 ? 0.25f : 0.8f);
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
    }

    void prepareToPlay (double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        latency_ = (int) state_.getRawParameterValue ("latency")->load();
        setLatencySamples (latency_);
        delay_.assign ((size_t) latency_ + 1, 0.0f);
        delayPos_ = 0;
        for (auto& v : voices_)
            v = Voice {};
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        const int n = buffer.getNumSamples();
        buffer.clear();
        const float level = state_.getRawParameterValue ("level")->load();
        const double attack = std::max (1.0, state_.getRawParameterValue ("attack")->load() * 0.001 * sampleRate_);
        const double releaseMul = std::exp (-6.907755 / (state_.getRawParameterValue ("release")->load() * 0.001 * sampleRate_));

        auto it = midi.cbegin();
        for (int i = 0; i < n; ++i)
        {
            for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    auto& v = voices_[(size_t) (next_++ % voices_.size())];
                    v = Voice { true, false, m.getNoteNumber(), m.getFloatVelocity(), 0.0, 0.0,
                                440.0 * std::pow (2.0, (m.getNoteNumber() - 69) / 12.0) / sampleRate_ };
                }
                else if (m.isNoteOff())
                    for (auto& v : voices_)
                        if (v.active && ! v.released && v.note == m.getNoteNumber())
                            v.released = true;
            }
            double s = 0.0;
            for (auto& v : voices_)
            {
                if (! v.active)
                    continue;
                if (v.released)
                {
                    v.env *= releaseMul;
                    if (v.env < 1.0e-5)
                        v.active = false;
                }
                else
                    v.env = std::min (1.0, v.env + 1.0 / attack);
                s += std::sin (6.283185307179586 * v.phase) * v.env * v.velocity;
                v.phase += v.increment;
                if (v.phase >= 1.0)
                    v.phase -= 1.0;
            }
            float out = (float) (s * level * 0.5);
            if (latency_ > 0)
            {
                const float delayed = delay_[(size_t) delayPos_];
                delay_[(size_t) delayPos_] = out;
                delayPos_ = (delayPos_ + 1) % latency_;
                out = delayed;
            }
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample (ch, i, out);
        }
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = state_.copyState().createXml())
            copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (state_.state.getType()))
                state_.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    struct Voice
    {
        bool active = false;
        bool released = false;
        int note = 0;
        double velocity = 0.0;
        double env = 0.0;
        double phase = 0.0;
        double increment = 0.0;
    };

    juce::AudioProcessorValueTreeState state_;
    std::array<Voice, 16> voices_ {};
    size_t next_ = 0;
    double sampleRate_ = 48000.0;
    int latency_ = 0;
    std::vector<float> delay_;
    int delayPos_ = 0;
    int program_ = 0;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestSynth();
}
