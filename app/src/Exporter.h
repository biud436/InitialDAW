// 내보내기: 엔진의 오프라인 렌더(재생과 같은 트랙 사슬)로 곡을 오디오 파일로 쓴다. 뒤 스레드에서 돌리고 진행을 알린다.
#pragma once

#include "composition/Composition.h"
#include "io/AudioFile.h"
#include "media/Video.h"
#include "rendering/OfflineRenderer.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>

namespace daw
{

enum class ExportRange { song, loop, bars };

struct ExportSettings
{
    juce::File output;
    mr::AudioFileType type = mr::AudioFileType::wav;
    int bitDepth = 24;                    // WAV 16, 24, 32, FLAC 16, 24
    int quality = -1;                     // Ogg 0~10, MP3 kbps (0 아래면 기본값)
    double sampleRate = 48000.0;
    ExportRange range = ExportRange::song;
    int firstBar = 1, lastBar = 1;        // 마디 구간: 1부터, 끝 마디 포함
    double fadeSeconds = 2.0;             // 마디 구간 끝 뒤에 줄이는 길이
    bool stems = false;                   // 곡 전체일 때만: output 곁의 <이름>_stems 폴더에 트랙과 버스마다 WAV
    bool midi = false;                    // output 곁의 <이름>.mid
    // 곡의 영상에 음악을 입힌 .mp4 (영상 샘플은 그대로, 소리는 AAC, quality 는 kbps). 범위는 영상의 길이, 스템은 없다
    bool video = false;
    bool videoMixOriginal = false;        // 영상의 원래 소리를 섞는다
};

const char* extensionFor (mr::AudioFileType type);
juce::String extensionFor (const ExportSettings& settings);
// 곡에 영상이 있고 열 수 있으면 그 파일 (아니면 빈 File)
juce::File exportableVideo (const mr::Composition& composition);
juce::File stemsFolderFor (const juce::File& output);
juce::File midiFileFor (const juce::File& output);
mr::RenderOptions renderOptionsFor (const mr::Composition& composition, const ExportSettings& settings);
bool usesPluginInstruments (const mr::Composition& composition);

struct ExportResult
{
    bool ok = false;
    bool cancelled = false;
    juce::String error;
    mr::RenderReport report;
    juce::File output, stems, midi;
    std::optional<mr::media::ScoreVideoReport> video;
};

// 플러그인 악기가 있으면 악기 만들기를 start() 안(메시지 스레드)에서 하고, 렌더링은 뒤 스레드에서 한다.
// 끝나면 onDone 을 메시지 스레드에서 부른다. 지우면 멈추고 기다린다 (악기는 지우는 스레드에서 지운다).
class ExportJob final : private juce::Thread
{
public:
    ExportJob (mr::Composition composition, ExportSettings settings, std::function<void (const ExportResult&)> onDone);
    ~ExportJob() override;

    void start();
    void cancel() noexcept { cancel_.store (true); }
    float progress() const noexcept { return progress_.load(); }
    bool builtOnMessageThread() const noexcept { return builtOnMessageThread_; }

private:
    void run() override;
    mr::RenderOptions options();   // 멈추기, 진행, 영상에 입히기

    mr::Composition composition_;
    std::optional<mr::media::ScoreVideoReport> video_;
    ExportSettings settings_;
    std::function<void (const ExportResult&)> onDone_;
    std::unique_ptr<mr::OfflineRender> render_;
    juce::String buildError_;
    bool builtOnMessageThread_ = false;
    bool stemsExisted_ = false;
    std::atomic<bool> cancel_ { false };
    std::atomic<float> progress_ { 0.0f };
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
};

} // namespace daw
