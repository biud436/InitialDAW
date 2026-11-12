// AVFoundation 이 없는 플랫폼: 영상을 열지 못한다
#include "media/Video.h"

namespace mr::media
{

namespace
{
const std::string unsupported = "video needs macOS";
}

bool videoSupported() { return false; }

bool writePng (const VideoImage&, const std::string&, std::string& error)
{
    error = unsupported;
    return false;
}

std::optional<VideoInfo> probeVideo (const std::string&, std::string& error)
{
    error = unsupported;
    return std::nullopt;
}

struct VideoFrames::Impl
{
    VideoInfo info;
    std::string error = unsupported;
};
VideoFrames::VideoFrames (const std::string&) : impl_ (std::make_unique<Impl>()) {}
VideoFrames::~VideoFrames() = default;
bool VideoFrames::ok() const noexcept { return false; }
const std::string& VideoFrames::error() const noexcept { return impl_->error; }
const VideoInfo& VideoFrames::info() const noexcept { return impl_->info; }
std::optional<VideoImage> VideoFrames::frameAt (double, int, int, double) { return std::nullopt; }

struct VideoDecoder::Impl
{
    std::string error = unsupported;
};
VideoDecoder::VideoDecoder (const std::string&, double, int, int) : impl_ (std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;
bool VideoDecoder::ok() const noexcept { return false; }
const std::string& VideoDecoder::error() const noexcept { return impl_->error; }
bool VideoDecoder::next (VideoImage&) { return false; }

std::optional<codec::Decoded> readVideoAudio (const std::string&, double, std::string& error)
{
    error = unsupported;
    return std::nullopt;
}

std::optional<std::vector<SceneChange>> detectSceneChanges (const std::string&, const SceneOptions&, std::string& error,
                                                            const std::function<bool (double)>&)
{
    error = unsupported;
    return std::nullopt;
}

std::optional<ScoreVideoReport> writeScoredVideo (const std::string&, const ScoreAudio&, const std::string&, const ScoreVideoOptions&,
                                                  std::string& error)
{
    error = unsupported;
    return std::nullopt;
}

std::optional<std::vector<SampleDigest>> videoSampleDigests (const std::string&, std::string& error)
{
    error = unsupported;
    return std::nullopt;
}

bool writeTestPattern (const std::string&, const TestPattern&, std::string& error)
{
    error = unsupported;
    return false;
}

} // namespace mr::media
