// music-render 의 자동 테스트용 VST3 효과. 결정적이다.
//   Gain (dB, -60..12): 곱한다. Latency (샘플): 그만큼 늦추고 호스트에 알린다.
//   Beat Gate (켜기, 끄기): 호스트의 재생 위치(4분음표 박)의 뒤 반을 끈다. 위치를 모르면 그대로 둔다.
//   상태는 매개변수 값이다.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <optional>
#include <vector>

namespace
{

class TestEffect final : public juce::AudioProcessor
{
public:
    TestEffect()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          state_ (*this, nullptr, "MusicRenderTestEffect", createLayout())
    {
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "gain", 1 }, "Gain",
                                                                 juce::NormalisableRange<float> (-60.0f, 12.0f), -6.0f,
                                                                 juce::AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "latency", 1 }, "Latency", 0, 4096, 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "gate", 1 }, "Beat Gate", false));
        return layout;
    }

    const juce::String getName() const override { return "MusicRender Test Effect"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
    }

    void prepareToPlay (double sampleRate, int) override
    {
        sampleRate_ = sampleRate;
        latency_ = (int) state_.getRawParameterValue ("latency")->load();
        setLatencySamples (latency_);
        for (auto& d : delay_)
            d.assign ((size_t) latency_ + 1, 0.0f);
        delayPos_ = 0;
    }

    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples();
        const float gain = juce::Decibels::decibelsToGain (state_.getRawParameterValue ("gain")->load(), -60.0f);
        const bool gate = state_.getRawParameterValue ("gate")->load() > 0.5f;
        std::optional<double> ppq, bpm;
        if (auto* head = getPlayHead())
            if (const auto pos = head->getPosition())
            {
                if (pos->getPpqPosition())
                    ppq = *pos->getPpqPosition();
                if (pos->getBpm())
                    bpm = *pos->getBpm();
            }
        for (int i = 0; i < n; ++i)
        {
            float g = gain;
            if (gate && ppq && bpm)
            {
                const double beat = *ppq + (double) i * *bpm / (60.0 * sampleRate_);
                if (beat - std::floor (beat) >= 0.5)
                    g = 0.0f;
            }
            for (int ch = 0; ch < std::min (2, buffer.getNumChannels()); ++ch)
            {
                float v = buffer.getSample (ch, i) * g;
                if (latency_ > 0)
                {
                    auto& line = delay_[(size_t) ch];
                    const float out = line[(size_t) delayPos_];
                    line[(size_t) delayPos_] = v;
                    v = out;
                }
                buffer.setSample (ch, i, v);
            }
            if (latency_ > 0)
                delayPos_ = (delayPos_ + 1) % latency_;
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
    juce::AudioProcessorValueTreeState state_;
    double sampleRate_ = 48000.0;
    int latency_ = 0;
    std::vector<float> delay_[2];
    int delayPos_ = 0;
};

} // namespace

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestEffect();
}
