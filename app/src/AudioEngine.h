// 오디오 장치와 재생기를 잇는다. 재생기는 메시지 스레드에서 바꿔 끼우고, 오디오 콜백은 잠깐 잠근 채 그것을 부른다.
#pragma once

#include "realtime/AudioTakeRecorder.h"
#include "realtime/RealtimePlayer.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace daw
{

class AudioEngine final : private juce::AudioIODeviceCallback
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // 저장된 장치 상태(없으면 기본 출력)로 연다. 실패하면 오류 문장.
    juce::String open (const juce::XmlElement* savedState);
    juce::AudioDeviceManager& devices() noexcept { return devices_; }

    // 재생기를 바꾼다. 앞의 재생기는 이 함수 안에서(메시지 스레드에서) 놓는다.
    void setPlayer (std::shared_ptr<mr::RealtimePlayer> player);
    std::shared_ptr<mr::RealtimePlayer> player() const { return player_; }

    double sampleRate() const noexcept { return sampleRate_; }
    int bufferSize() const noexcept { return bufferSize_; }
    double cpuLoad() { return devices_.getCpuUsage(); }
    juce::String deviceName() const;

    // 지금 재생 샘플의 어림값 (아무 스레드, MIDI 입력의 시각): 오디오 콜백이 마지막으로 돌린 블록의 시작 샘플에 그 뒤 지난 시간을 더한다.
    // 멈춰 있으면 그 블록의 시작 샘플.
    int64_t estimatedPosition() const noexcept;

    // ---- 오디오 입력 녹음 ----
    // 켠 입력 채널 수 (없으면 0)
    int inputChannels() const;
    // 입력이 꺼져 있으면 기본 입력 장치의 앞 두 채널을 켠다 (마이크 권한을 묻는다). 켠 채널 수
    int enableInput();
    // 오디오 콜백이 블록마다 입력을 넘길 녹음기 (nullptr 이면 뗀다). 떼고 나서 녹음기를 멈춘다
    void setRecorder (mr::AudioTakeRecorder* recorder);
    // 장치의 입력과 출력 지연의 합 (샘플)
    int roundTripLatency() const;
    // 장치의 출력 지연과 한 블록 (샘플): 재생기가 만든 소리가 들리기까지
    int outputLatency() const;

    // 장치의 샘플레이트가 바뀌면 메시지 스레드에서 부른다 (재생기를 다시 만들어야 한다)
    std::function<void()> onDeviceChanged;

private:
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels,
                                           float* const* outputChannelData, int numOutputChannels, int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager devices_;
    juce::SpinLock lock_;
    std::shared_ptr<mr::RealtimePlayer> player_;
    mr::RealtimePlayer* active_ = nullptr;   // 오디오 스레드가 보는 것 (lock_ 안에서만 바꾼다)
    mr::AudioTakeRecorder* recorder_ = nullptr;   // 같은 잠금 아래
    double sampleRate_ = 48000.0;
    int bufferSize_ = 512;
    std::atomic<int64_t> blockStart_ { 0 };     // 오디오 스레드가 마지막 블록 앞에 쓴다
    std::atomic<double> blockTimeMs_ { 0.0 };
    std::atomic<int> blockSamples_ { 0 };
    std::atomic<bool> blockPlaying_ { false };
    std::shared_ptr<int> alive_ = std::make_shared<int> (0);   // 비동기 호출이 사라진 엔진을 건드리지 않게
};

} // namespace daw
