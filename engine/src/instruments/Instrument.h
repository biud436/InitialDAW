// 렌더러가 보는 악기. 블록마다 MIDI 이벤트(블록 안의 샘플 위치)를 받아 스테레오 오디오를 쓴다.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace juce
{
class AudioPluginInstance;
}

namespace mr
{

struct AudioClip;
class TempoMap;

class Instrument
{
public:
    virtual ~Instrument() = default;

    virtual void prepare (double sampleRate, int maxBlockSize) = 0;

    // buffer 는 2채널이고 numSamples 이상이다. 악기는 [0, numSamples) 를 덮어쓴다.
    // blockStart 는 곡의 처음부터 센 이 블록의 첫 샘플이다 (플러그인의 플레이헤드가 쓴다).
    virtual void render (juce::AudioBuffer<float>& buffer, int numSamples, const juce::MidiBuffer& midi, int64_t blockStart) = 0;

    // 플러그인이 알리는 처리 지연(샘플). 렌더러가 그만큼 당겨 섞는다.
    virtual int latencySamples() const { return 0; }
    virtual std::string description() const = 0;
    // 플러그인 악기면 그 인스턴스 (편집기 창, 상태 저장). 아니면 nullptr.
    virtual juce::AudioPluginInstance* plugin() { return nullptr; }
    // 레지스트리 설정만 넣었을 때의 플러그인 상태 (base64). 트랙 설정을 지우면 이 상태로 돌아간다.
    virtual std::string pluginBaseState() const { return {}; }
    // 내장 신스면 프리셋 위에 이 값들을 덮은 소리로 바꾼다 (한 스레드에서, 오디오 스레드는 다음 블록에서 바꿔 끼운다). 아니면 false.
    virtual bool setSynthParameters (const std::map<std::string, double>& values) { (void) values; return false; }
    // 오디오 클립을 트는 악기면 클립 목록을 바꾼다 (메시지 스레드에서, 오디오 스레드는 다음 블록에서). 아니면 false.
    virtual bool setClips (const std::vector<AudioClip>& clips, const TempoMap& tempo, std::vector<std::string>& warnings)
    {
        (void) clips; (void) tempo; (void) warnings;
        return false;
    }
    // 실시간 재생기가 블록마다 알린다: 멈춰 있는가 (곡 위치를 따라 소리를 내는 악기는 멈추면 조용하다)
    virtual void setTransportPlaying (bool playing) { (void) playing; }
    // 앱의 재생기: setClips 의 준비(늘이기와 높이 렌더)를 뒤 스레드에서 한다. 준비하는 동안은 앞의 클립이 들린다
    virtual void prepareClipsInBackground (bool on) { (void) on; }
    virtual bool clipsPending() const { return false; }   // 뒤 스레드가 아직 준비 중이다
};

} // namespace mr
