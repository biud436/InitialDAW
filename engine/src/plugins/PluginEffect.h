// VST3 와 AU 효과를 효과 사슬의 칸으로. 효과 객체 { "type": "plugin", "format": "vst3", "plugin": "<파일이나 식별자>", "name", "state",
// "program", "parameters" }. 플러그인은 메시지 스레드에서 만든다. 재생 위치(곡의 샘플, 박, 템포)를 플러그인의 플레이헤드로 알린다.
#pragma once

#include "effects/Effects.h"
#include "plugins/PluginSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <string>

namespace mr
{

class PluginEffect final : public Effect, private juce::AudioPlayHead
{
public:
    PluginEffect (std::shared_ptr<juce::AudioPluginInstance> plugin, std::shared_ptr<const TempoMap> tempo, bool realtime);
    ~PluginEffect() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void process (juce::AudioBuffer<float>& buffer, int numSamples) override;
    int latencySamples() const override { return plugin_->getLatencySamples(); }
    std::string type() const override { return "plugin"; }
    void setPosition (BlockPosition position) override { position_ = position; }
    bool followsPosition() const override { return true; }

    // 창이 함께 쥔다 (사슬을 바꿔 끼워 지운 뒤에도 창을 닫을 때까지 살아 있다)
    std::shared_ptr<juce::AudioPluginInstance> instance() const { return plugin_; }

private:
    juce::Optional<PositionInfo> getPosition() const override;

    std::shared_ptr<juce::AudioPluginInstance> plugin_;
    std::shared_ptr<const TempoMap> tempo_;
    bool realtime_;
    double sampleRate_ = 48000.0;
    juce::AudioBuffer<float> scratch_;
    juce::MidiBuffer midi_;
    BlockPosition position_;
};

// 효과 객체의 플러그인 설정 (state, program, parameters)
PluginSettings pluginEffectSettings (const juce::var& params);
// 효과 객체에서 플러그인 효과를 만든다 (메시지 스레드). 실패하면 Failure("instrument" 아님, "invalid-input")
std::unique_ptr<Effect> makePluginEffect (const EffectSpec& spec, const juce::File& baseDir, double sampleRate, int blockSize,
                                          const TempoMap* tempo, bool realtime);
// 플러그인 효과 객체를 검사한다 (플러그인을 불러오지 않는다)
void validatePluginEffect (const EffectSpec& spec, Diagnostics& diagnostics);

} // namespace mr
