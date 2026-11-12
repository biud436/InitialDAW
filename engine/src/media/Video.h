// 영상 파일 (macOS 의 AVFoundation): 정보, 시각의 프레임, 차례로 풀기, 소리 꺼내기, 장면 바뀜 찾기, 음악을 입혀 쓰기.
// 곡의 소리에는 쓰지 않는다. 다른 플랫폼에서는 videoSupported() 가 false 이고 모든 함수가 실패한다.
#pragma once

#include "io/Codecs.h"
#include "media/Timecode.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr::media
{

bool videoSupported();
// 영상으로 여는 확장자 (.mp4, .mov, .m4v)
bool isVideoFileName (const std::string& path);

struct VideoInfo
{
    double durationSeconds = 0.0;
    FrameRate rate;                 // 프레임 사이 (영상 트랙의 가장 짧은 프레임 길이, 없으면 명목 빠르기)
    int64_t frames = 0;             // 영상 트랙의 프레임 수
    int width = 0, height = 0;      // 화면에 보이는 크기 (회전 반영)
    std::string codec;              // "avc1", "hvc1", "apcn" ...
    bool hasAudio = false;
    double audioSampleRate = 0.0;
    int audioChannels = 0;
};

std::optional<VideoInfo> probeVideo (const std::string& path, std::string& error);

// 8비트 RGBA (알파 255), 위 줄부터
struct VideoImage
{
    int width = 0, height = 0;
    double seconds = 0.0;           // 그 프레임이 보이기 시작하는 시각
    std::vector<uint8_t> rgba;
    bool empty() const noexcept { return rgba.empty(); }
};

// PNG 로 쓴다 (실패하면 false 와 error)
bool writePng (const VideoImage& image, const std::string& path, std::string& error);

// 시각의 프레임을 읽는다. 열어 둔 채 여러 번 읽는다 (한 번에 한 스레드).
class VideoFrames
{
public:
    explicit VideoFrames (const std::string& path);
    ~VideoFrames();
    VideoFrames (const VideoFrames&) = delete;
    VideoFrames& operator= (const VideoFrames&) = delete;

    bool ok() const noexcept;
    const std::string& error() const noexcept;
    const VideoInfo& info() const noexcept;
    // toleranceSeconds 0 이면 그 시각에 보이는 프레임. maxWidth, maxHeight (0 은 제한 없음) 안으로 비율을 지켜 줄인다
    std::optional<VideoImage> frameAt (double seconds, int maxWidth = 0, int maxHeight = 0, double toleranceSeconds = 0.0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 재생용으로 차례로 푼다: startSeconds 에 보이는 프레임부터 표시 순서로
class VideoDecoder
{
public:
    VideoDecoder (const std::string& path, double startSeconds, int maxWidth = 0, int maxHeight = 0);
    ~VideoDecoder();
    VideoDecoder (const VideoDecoder&) = delete;
    VideoDecoder& operator= (const VideoDecoder&) = delete;

    bool ok() const noexcept;
    const std::string& error() const noexcept;
    bool next (VideoImage& out);   // 끝이거나 실패하면 false

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 영상의 소리 (소리 트랙을 모두 섞는다). sampleRate 가 0 이면 원래 빠르기. 소리가 없으면 nullopt 와 error
std::optional<codec::Decoded> readVideoAudio (const std::string& path, double sampleRate, std::string& error);

// ---- 장면 바뀜 ----
struct SceneOptions
{
    double threshold = 0.2;       // 이웃 프레임 밝기 히스토그램 차이 (0..1) 의 하한
    double ratio = 3.0;           // 둘레(앞뒤 0.5초) 차이의 중앙값보다 이만큼 커야 한다
    double minGapSeconds = 0.4;   // 후보 사이의 가장 짧은 간격 (가까우면 큰 것)
};

struct SceneChange
{
    double seconds = 0.0;         // 새 장면의 첫 프레임 시각
    int64_t frame = 0;
    double score = 0.0;           // 그 자리의 차이 (0..1)
};

// 프레임마다의 차이(앞 프레임과, 0 번은 0)에서 후보를 고른다 (계산만, 시험이 쓴다)
std::vector<size_t> pickSceneChanges (const std::vector<double>& differences, double fps, const SceneOptions& options);

// progress 는 0..1 을 받고 false 를 돌려주면 그만둔다
std::optional<std::vector<SceneChange>> detectSceneChanges (const std::string& path, const SceneOptions& options, std::string& error,
                                                            const std::function<bool (double)>& progress = {});

// ---- 음악을 영상에 입혀 쓰기 ----
// 곡의 소리 (채널마다 float). 영상의 t 초에는 곡의 t + offsetSeconds 초가 놓인다 (곡 밖은 조용하다)
struct ScoreAudio
{
    const float* const* channels = nullptr;
    int numChannels = 0;
    int64_t frames = 0;
    double sampleRate = 48000.0;
    double offsetSeconds = 0.0;
};

struct ScoreVideoOptions
{
    bool mixOriginal = false;     // 영상의 원래 소리를 섞는다 (아니면 뺀다)
    double originalGainDb = 0.0;
    int aacBitrate = 256000;
};

struct ScoreVideoReport
{
    double durationSeconds = 0.0; // 영상의 길이 (소리도 이 길이)
    int64_t videoSamples = 0;     // 다시 인코딩하지 않고 옮긴 영상 샘플
    int64_t audioFrames = 0;      // 쓴 소리 프레임 (AAC 인코딩 전)
    double musicSeconds = 0.0;    // 영상 안에 들어간 곡의 길이
    bool mixedOriginal = false;
    std::string fileType;         // "mp4", "mov", "m4v"
};

// 영상 샘플은 그대로 옮기고 소리는 AAC 로 쓴다. 출력은 확장자 (.mp4, .mov, .m4v) 로 형식을 고른다
std::optional<ScoreVideoReport> writeScoredVideo (const std::string& videoPath, const ScoreAudio& audio, const std::string& outputPath,
                                                  const ScoreVideoOptions& options, std::string& error);

// 영상 트랙의 압축 샘플마다 표시 시각 (편집 목록을 지난 영상의 초, 마이크로초로 반올림) 과 바이트의 해시 (다시 인코딩하지 않았는지 견준다)
struct SampleDigest
{
    double seconds = 0.0;
    size_t bytes = 0;
    uint64_t hash = 0;
    bool operator== (const SampleDigest& o) const noexcept { return seconds == o.seconds && bytes == o.bytes && hash == o.hash; }
};
std::optional<std::vector<SampleDigest>> videoSampleDigests (const std::string& path, std::string& error);

// ---- 시험 영상 ----
// 프레임마다 맨 위 줄에 프레임 번호(그레이 부호 16비트)를 흰 칸과 검은 칸으로, 아래에 장면마다 다른 바탕과 움직이는 네모를 그린다.
// clickSeconds 가 있으면 그 자리에 짧은 클릭(1 kHz, 5 ms)이 든 AAC 소리 트랙을 넣는다.
struct TestPattern
{
    static constexpr int numberBits = 16;
    FrameRate rate { 30, 1 };
    int frames = 90;
    int width = 320, height = 180;
    std::vector<int64_t> sceneCuts;     // 새 장면이 시작하는 프레임
    std::vector<double> clickSeconds;
    double audioSampleRate = 48000.0;
    int quarterTurns = 0;               // 화면에서 시계 방향으로 90도씩 돌려 보이게 (preferredTransform)
};
bool writeTestPattern (const std::string& path, const TestPattern& pattern, std::string& error);
// 시험 영상의 그림에서 프레임 번호를 읽는다 (읽지 못하면 -1)
int64_t readPatternFrame (const VideoImage& image);

} // namespace mr::media
