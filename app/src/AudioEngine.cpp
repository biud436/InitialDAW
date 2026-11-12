#include "AudioEngine.h"

namespace daw
{

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
    devices_.removeAudioCallback (this);
    devices_.closeAudioDevice();
    setPlayer (nullptr);
}

juce::String AudioEngine::open (const juce::XmlElement* savedState)
{
    auto error = devices_.initialise (0, 2, savedState, true);
    devices_.addAudioCallback (this);
    if (auto* device = devices_.getCurrentAudioDevice())
    {
        sampleRate_ = device->getCurrentSampleRate();
        bufferSize_ = device->getCurrentBufferSizeSamples();
    }
    return error;
}

juce::String AudioEngine::deviceName() const
{
    if (auto* device = devices_.getCurrentAudioDevice())
        return device->getName();
    return {};
}

void AudioEngine::setPlayer (std::shared_ptr<mr::RealtimePlayer> player)
{
    auto previous = player_;
    {
        const juce::SpinLock::ScopedLockType lock (lock_);
        active_ = player.get();
    }
    player_ = std::move (player);
    previous.reset();   // 오디오 스레드는 이미 새 재생기를 본다
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numInputs, float* const* out, int numOutputs, int numSamples,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    const juce::SpinLock::ScopedTryLockType lock (lock_);
    if (lock.isLocked() && active_ != nullptr)
    {
        const int64_t position = active_->position();
        const bool playing = active_->isPlaying();
        blockStart_.store (position, std::memory_order_relaxed);
        blockSamples_.store (numSamples, std::memory_order_relaxed);
        blockPlaying_.store (playing, std::memory_order_relaxed);
        blockTimeMs_.store (juce::Time::getMillisecondCounterHiRes(), std::memory_order_release);
        if (recorder_ != nullptr)
            recorder_->push (in, numInputs, numSamples, position, playing);   // 이 블록이 재생하는 곡 위치와 함께
        active_->process (out, numOutputs, numSamples, in, numInputs);   // 입력 모니터링 트랙이 입력을 쓴다
        return;
    }
    for (int ch = 0; ch < numOutputs; ++ch)
        if (out[ch] != nullptr)
            juce::FloatVectorOperations::clear (out[ch], numSamples);
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    const double rate = device->getCurrentSampleRate();
    const int buffer = device->getCurrentBufferSizeSamples();
    juce::MessageManager::callAsync ([this, rate, buffer, alive = std::weak_ptr<int> (alive_)]
    {
        if (alive.expired())
            return;
        const bool changed = rate != sampleRate_;
        sampleRate_ = rate;
        bufferSize_ = buffer;
        if (changed && onDeviceChanged)
            onDeviceChanged();
    });
}

void AudioEngine::audioDeviceStopped() {}

int AudioEngine::inputChannels() const
{
    if (auto* device = devices_.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

int AudioEngine::enableInput()
{
    if (inputChannels() > 0)
        return inputChannels();
    auto setup = devices_.getAudioDeviceSetup();
    setup.useDefaultInputChannels = true;
    setup.inputChannels.clear();
    setup.inputChannels.setRange (0, 2, true);
    if (setup.inputDeviceName.isEmpty())
        if (auto* type = devices_.getCurrentDeviceTypeObject())
        {
            type->scanForDevices();
            const auto inputs = type->getDeviceNames (true);
            if (! inputs.isEmpty())
                setup.inputDeviceName = inputs[type->getDefaultDeviceIndex (true)];
        }
    devices_.setAudioDeviceSetup (setup, true);
    return inputChannels();
}

void AudioEngine::setRecorder (mr::AudioTakeRecorder* recorder)
{
    const juce::SpinLock::ScopedLockType lock (lock_);
    recorder_ = recorder;
}

int AudioEngine::roundTripLatency() const
{
    if (auto* device = devices_.getCurrentAudioDevice())
        return device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    return 0;
}

int AudioEngine::outputLatency() const
{
    if (auto* device = devices_.getCurrentAudioDevice())
        return device->getOutputLatencyInSamples() + device->getCurrentBufferSizeSamples();
    return 0;
}

int64_t AudioEngine::estimatedPosition() const noexcept
{
    const double blockTime = blockTimeMs_.load (std::memory_order_acquire);
    const int64_t start = blockStart_.load (std::memory_order_relaxed);
    if (! blockPlaying_.load (std::memory_order_relaxed))
        return start;
    const double elapsed = (juce::Time::getMillisecondCounterHiRes() - blockTime) * sampleRate_ / 1000.0;
    return start + std::clamp ((int64_t) std::llround (elapsed), (int64_t) 0, (int64_t) blockSamples_.load (std::memory_order_relaxed));
}

} // namespace daw
