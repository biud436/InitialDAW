// 영상 파일의 AVFoundation 구현 (ARC). 동기 API 를 쓰고, 기다림은 세마포어로 한다 (런 루프가 필요 없다).
#include "media/Video.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace mr::media
{

namespace
{

NSURL* fileUrl (const std::string& path)
{
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

std::string describe (NSError* error, const std::string& fallback)
{
    if (error == nil)
        return fallback;
    std::string text = fallback + ": " + std::string ([[error localizedDescription] UTF8String]);
    if (NSString* reason = [error localizedFailureReason])
        text += " (" + std::string ([reason UTF8String]) + ")";
    return text;
}

std::string lowerExtension (const std::string& path)
{
    auto ext = std::filesystem::path (path).extension().string();
    std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    return ext;
}

AVURLAsset* openAsset (const std::string& path, std::string& error)
{
    std::error_code ec;
    if (! std::filesystem::is_regular_file (path, ec))
    {
        error = "file not found: " + path;
        return nil;
    }
    AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl (path) options:@{ AVURLAssetPreferPreciseDurationAndTimingKey: @YES }];
    if (asset == nil || [[asset tracksWithMediaType:AVMediaTypeVideo] count] == 0)
    {
        error = "no video track in " + path;
        return nil;
    }
    return asset;
}

std::string fourCC (FourCharCode code)
{
    std::string s;
    for (int shift = 24; shift >= 0; shift -= 8)
    {
        const char c = (char) ((code >> shift) & 0xff);
        s += (c >= 32 && c < 127) ? c : '?';
    }
    return s;
}

FrameRate rateOf (AVAssetTrack* track)
{
    const CMTime d = track.minFrameDuration;
    if (CMTIME_IS_VALID (d) && d.value > 0 && d.timescale > 0)
    {
        const double fps = (double) d.timescale / (double) d.value;
        return frameRateFromFps (fps);
    }
    return frameRateFromFps (track.nominalFrameRate);
}

// preferredTransform 의 사분 회전 (0..3) 과 보이는 크기
int quarterTurns (CGAffineTransform t)
{
    const double angle = std::atan2 (t.b, t.a);
    int q = (int) std::lround (angle / (M_PI / 2.0));
    return ((q % 4) + 4) % 4;
}

VideoInfo infoOf (AVURLAsset* asset)
{
    VideoInfo info;
    AVAssetTrack* video = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
    info.durationSeconds = CMTimeGetSeconds (asset.duration);
    if (! std::isfinite (info.durationSeconds))
        info.durationSeconds = 0.0;
    info.rate = rateOf (video);
    const double trackSeconds = CMTimeGetSeconds (CMTimeRangeGetEnd (video.timeRange));
    if (info.rate.valid() && std::isfinite (trackSeconds))
        info.frames = (int64_t) std::llround (trackSeconds * info.rate.fps());
    const CGSize natural = video.naturalSize;
    const bool sideways = quarterTurns (video.preferredTransform) % 2 == 1;
    info.width = (int) std::lround (sideways ? natural.height : natural.width);
    info.height = (int) std::lround (sideways ? natural.width : natural.height);
    if (NSArray* formats = video.formatDescriptions; formats.count > 0)
        info.codec = fourCC (CMFormatDescriptionGetMediaSubType ((__bridge CMFormatDescriptionRef) formats[0]));
    if (AVAssetTrack* audio = [[asset tracksWithMediaType:AVMediaTypeAudio] firstObject])
    {
        info.hasAudio = true;
        if (NSArray* formats = audio.formatDescriptions; formats.count > 0)
            if (const auto* asbd = CMAudioFormatDescriptionGetStreamBasicDescription ((__bridge CMAudioFormatDescriptionRef) formats[0]))
            {
                info.audioSampleRate = asbd->mSampleRate;
                info.audioChannels = (int) asbd->mChannelsPerFrame;
            }
    }
    return info;
}

// 줄인 크기 (비율을 지키고 키우지 않는다)
std::pair<int, int> fitted (int width, int height, int maxWidth, int maxHeight)
{
    double scale = 1.0;
    if (maxWidth > 0 && width > maxWidth)
        scale = std::min (scale, (double) maxWidth / width);
    if (maxHeight > 0 && height > maxHeight)
        scale = std::min (scale, (double) maxHeight / height);
    return { std::max (1, (int) std::lround (width * scale)), std::max (1, (int) std::lround (height * scale)) };
}

constexpr uint32_t rgbaInfo = (uint32_t) kCGImageAlphaPremultipliedLast | (uint32_t) kCGBitmapByteOrder32Big;
constexpr uint32_t bgraInfo = (uint32_t) kCGImageAlphaPremultipliedFirst | (uint32_t) kCGBitmapByteOrder32Little;

// CGImage 를 RGBA 로 (크기는 그대로)
VideoImage imageFromCG (CGImageRef cg, double seconds)
{
    VideoImage image;
    image.width = (int) CGImageGetWidth (cg);
    image.height = (int) CGImageGetHeight (cg);
    image.seconds = seconds;
    image.rgba.assign ((size_t) image.width * (size_t) image.height * 4, 0);
    CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate (image.rgba.data(), (size_t) image.width, (size_t) image.height, 8, (size_t) image.width * 4, space,
                                              rgbaInfo);
    CGColorSpaceRelease (space);
    if (ctx == nullptr)
        return {};
    CGContextSetBlendMode (ctx, kCGBlendModeCopy);
    CGContextDrawImage (ctx, CGRectMake (0, 0, image.width, image.height), cg);
    CGContextRelease (ctx);
    return image;
}

// BGRA 픽셀 버퍼를 width x height 의 RGBA 로 줄인다
std::vector<uint8_t> scaledFromPixelBuffer (CVPixelBufferRef pixels, int width, int height)
{
    std::vector<uint8_t> out ((size_t) width * (size_t) height * 4, 0);
    CVPixelBufferLockBaseAddress (pixels, kCVPixelBufferLock_ReadOnly);
    const size_t sw = CVPixelBufferGetWidth (pixels), sh = CVPixelBufferGetHeight (pixels);
    const size_t stride = CVPixelBufferGetBytesPerRow (pixels);
    void* base = CVPixelBufferGetBaseAddress (pixels);
    CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
    CGContextRef source = CGBitmapContextCreate (base, sw, sh, 8, stride, space, bgraInfo);
    CGImageRef cg = source != nullptr ? CGBitmapContextCreateImage (source) : nullptr;
    CVPixelBufferUnlockBaseAddress (pixels, kCVPixelBufferLock_ReadOnly);
    if (cg != nullptr)
    {
        CGContextRef target = CGBitmapContextCreate (out.data(), (size_t) width, (size_t) height, 8, (size_t) width * 4, space,
                                                     rgbaInfo);
        if (target != nullptr)
        {
            CGContextSetInterpolationQuality (target, kCGInterpolationMedium);
            CGContextSetBlendMode (target, kCGBlendModeCopy);
            CGContextDrawImage (target, CGRectMake (0, 0, width, height), cg);
            CGContextRelease (target);
        }
        CGImageRelease (cg);
    }
    if (source != nullptr)
        CGContextRelease (source);
    CGColorSpaceRelease (space);
    return out;
}

// 영상 좌표(왼쪽 위 원점)의 그림을 preferredTransform 대로 돌린다 (사분 회전과 뒤집기만)
VideoImage oriented (std::vector<uint8_t> pixels, int width, int height, CGAffineTransform t, double seconds)
{
    VideoImage image;
    image.seconds = seconds;
    const int a = (int) std::lround (t.a), b = (int) std::lround (t.b), c = (int) std::lround (t.c), d = (int) std::lround (t.d);
    const bool identity = a == 1 && b == 0 && c == 0 && d == 1;
    const bool axisAligned = std::abs (a) + std::abs (b) == 1 && std::abs (c) + std::abs (d) == 1 && std::abs (a) == std::abs (d);
    if (identity || ! axisAligned)
    {
        image.width = width;
        image.height = height;
        image.rgba = std::move (pixels);
        return image;
    }
    const bool sideways = a == 0;
    image.width = sideways ? height : width;
    image.height = sideways ? width : height;
    image.rgba.assign (pixels.size(), 0);
    // 픽셀 가운데를 옮기고 가장 작은 모서리를 0 으로
    const double cornersX[4] = { 0.0, (double) width, 0.0, (double) width }, cornersY[4] = { 0.0, 0.0, (double) height, (double) height };
    double minX = 1.0e9, minY = 1.0e9;
    for (int i = 0; i < 4; ++i)
    {
        minX = std::min (minX, a * cornersX[i] + c * cornersY[i]);
        minY = std::min (minY, b * cornersX[i] + d * cornersY[i]);
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const double X = a * (x + 0.5) + c * (y + 0.5) - minX, Y = b * (x + 0.5) + d * (y + 0.5) - minY;
            const int ox = std::clamp ((int) std::floor (X), 0, image.width - 1), oy = std::clamp ((int) std::floor (Y), 0, image.height - 1);
            std::memcpy (&image.rgba[((size_t) oy * (size_t) image.width + (size_t) ox) * 4], &pixels[((size_t) y * (size_t) width + (size_t) x) * 4], 4);
        }
    return image;
}

CMTime timeAt (double seconds)
{
    return CMTimeMakeWithSeconds (seconds, 1000000);
}

// 리더의 출력을 하나씩 받는다 (ARC 가 모르는 CMSampleBuffer 는 직접 놓는다)
struct SampleRef
{
    CMSampleBufferRef ref = nullptr;
    SampleRef() = default;
    explicit SampleRef (CMSampleBufferRef r) : ref (r) {}
    SampleRef (SampleRef&& o) noexcept : ref (o.ref) { o.ref = nullptr; }
    SampleRef& operator= (SampleRef&& o) noexcept
    {
        if (this != &o)
        {
            reset();
            ref = o.ref;
            o.ref = nullptr;
        }
        return *this;
    }
    ~SampleRef() { reset(); }
    void reset()
    {
        if (ref != nullptr)
            CFRelease (ref);
        ref = nullptr;
    }
    explicit operator bool() const noexcept { return ref != nullptr; }
    double seconds() const { return CMTimeGetSeconds (CMSampleBufferGetPresentationTimeStamp (ref)); }
};

uint64_t fnv1a (const uint8_t* data, size_t size, uint64_t h = 1469598103934665603ull)
{
    for (size_t i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

AudioChannelLayout stereoLayout()
{
    AudioChannelLayout layout {};
    layout.mChannelLayoutTag = kAudioChannelLayoutTag_Stereo;
    return layout;
}

CMAudioFormatDescriptionRef floatFormat (double sampleRate, int channels)
{
    AudioStreamBasicDescription asbd {};
    asbd.mSampleRate = sampleRate;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    asbd.mBytesPerPacket = asbd.mBytesPerFrame = (UInt32) (4 * channels);
    asbd.mFramesPerPacket = 1;
    asbd.mChannelsPerFrame = (UInt32) channels;
    asbd.mBitsPerChannel = 32;
    AudioChannelLayout layout = stereoLayout();
    if (channels == 1)
        layout.mChannelLayoutTag = kAudioChannelLayoutTag_Mono;
    CMAudioFormatDescriptionRef format = nullptr;
    CMAudioFormatDescriptionCreate (kCFAllocatorDefault, &asbd, sizeof (layout), &layout, 0, nullptr, nullptr, &format);
    return format;
}

// 섞인 float 프레임으로 CMSampleBuffer (시작 프레임을 시각으로)
CMSampleBufferRef audioBuffer (const std::vector<float>& interleaved, int frames, int channels, double sampleRate, int64_t start,
                               CMAudioFormatDescriptionRef format)
{
    const size_t bytes = (size_t) frames * (size_t) channels * sizeof (float);
    CMBlockBufferRef block = nullptr;
    if (CMBlockBufferCreateWithMemoryBlock (kCFAllocatorDefault, nullptr, bytes, kCFAllocatorDefault, nullptr, 0, bytes,
                                            kCMBlockBufferAssureMemoryNowFlag, &block) != kCMBlockBufferNoErr)
        return nullptr;
    CMBlockBufferReplaceDataBytes (interleaved.data(), block, 0, bytes);
    CMSampleBufferRef sample = nullptr;
    CMAudioSampleBufferCreateReadyWithPacketDescriptions (kCFAllocatorDefault, block, format, frames, CMTimeMake (start, (int32_t) sampleRate),
                                                          nullptr, &sample);
    CFRelease (block);
    return sample;
}

NSDictionary* aacSettings (double sampleRate, int bitrate)
{
    AudioChannelLayout layout = stereoLayout();
    return @{ AVFormatIDKey: @(kAudioFormatMPEG4AAC),
              AVSampleRateKey: @(sampleRate),
              AVNumberOfChannelsKey: @2,
              AVEncoderBitRateKey: @(bitrate),
              AVChannelLayoutKey: [NSData dataWithBytes:&layout length:sizeof (layout)] };
}

AVFileType fileTypeFor (const std::string& path, std::string& name)
{
    const auto ext = lowerExtension (path);
    if (ext == ".mov")
    {
        name = "mov";
        return AVFileTypeQuickTimeMovie;
    }
    if (ext == ".m4v")
    {
        name = "m4v";
        return AVFileTypeAppleM4V;
    }
    if (ext == ".mp4")
    {
        name = "mp4";
        return AVFileTypeMPEG4;
    }
    return nil;
}

// 쓰기 입력마다 준비될 때 채운다. fill 은 더 쓸 것이 없으면 false
struct Feed
{
    AVAssetWriterInput* input = nil;
    std::function<bool()> fill;
};

bool runWriter (AVAssetWriter* writer, std::vector<Feed>& feeds, std::string& error)
{
    if (! [writer startWriting])
    {
        error = describe (writer.error, "cannot start writing");
        return false;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];
    dispatch_group_t group = dispatch_group_create();
    for (size_t i = 0; i < feeds.size(); ++i)
    {
        dispatch_group_enter (group);
        auto* feed = &feeds[i];
        auto finished = std::make_shared<std::atomic<bool>> (false);
        dispatch_queue_t queue = dispatch_queue_create ("initialdaw.video.feed", DISPATCH_QUEUE_SERIAL);
        [feed->input requestMediaDataWhenReadyOnQueue:queue usingBlock:^{
            if (finished->load())
                return;
            while (feed->input.readyForMoreMediaData)
            {
                bool more = false;
                @autoreleasepool { more = writer.status == AVAssetWriterStatusWriting && feed->fill(); }
                if (! more)
                {
                    [feed->input markAsFinished];
                    finished->store (true);
                    dispatch_group_leave (group);
                    break;
                }
            }
        }];
    }
    dispatch_group_wait (group, DISPATCH_TIME_FOREVER);
    if (writer.status == AVAssetWriterStatusFailed)
    {
        error = describe (writer.error, "cannot write the movie");
        return false;
    }
    dispatch_semaphore_t done = dispatch_semaphore_create (0);
    [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal (done); }];
    dispatch_semaphore_wait (done, DISPATCH_TIME_FOREVER);
    if (writer.status != AVAssetWriterStatusCompleted)
    {
        error = describe (writer.error, "cannot finish the movie");
        return false;
    }
    return true;
}

AVAssetWriter* newWriter (const std::string& outputPath, std::string& typeName, std::string& error)
{
    AVFileType type = fileTypeFor (outputPath, typeName);
    if (type == nil)
    {
        error = "the movie must end in .mp4, .mov or .m4v";
        return nil;
    }
    std::error_code ec;
    std::filesystem::remove (outputPath, ec);
    NSError* err = nil;
    AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:fileUrl (outputPath) fileType:type error:&err];
    if (writer == nil)
        error = describe (err, "cannot create " + outputPath);
    else
        writer.shouldOptimizeForNetworkUse = YES;
    return writer;
}

// ---- 장면 바뀜의 차이: 네 사분면의 밝기 히스토그램 (32칸) ----
constexpr int histogramBins = 32;
using Histogram = std::array<float, 4 * histogramBins>;

Histogram lumaHistogram (const uint8_t* bgra, size_t width, size_t height, size_t stride)
{
    Histogram h {};
    std::array<int, 4> counts {};
    const size_t step = std::max<size_t> (1, std::min (width, height) / 120);
    for (size_t y = 0; y < height; y += step)
    {
        const uint8_t* row = bgra + y * stride;
        const int qy = y * 2 < height ? 0 : 2;
        for (size_t x = 0; x < width; x += step)
        {
            const uint8_t* p = row + x * 4;
            const int luma = (19 * p[0] + 183 * p[1] + 54 * p[2]) >> 8;   // Rec. 709
            const int q = qy + (x * 2 < width ? 0 : 1);
            h[(size_t) (q * histogramBins + luma * histogramBins / 256)] += 1.0f;
            ++counts[(size_t) q];
        }
    }
    for (int q = 0; q < 4; ++q)
        if (counts[(size_t) q] > 0)
            for (int b = 0; b < histogramBins; ++b)
                h[(size_t) (q * histogramBins + b)] /= (float) counts[(size_t) q];
    return h;
}

double histogramDistance (const Histogram& a, const Histogram& b)
{
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += std::abs ((double) a[i] - (double) b[i]);
    return sum / 8.0;   // 사분면마다 0..2 를 넷 평균 내어 0..1
}

// ---- 시험 영상의 그림 ----
constexpr int patternBits = TestPattern::numberBits;

void drawPattern (uint8_t* bgra, int width, int height, size_t stride, int64_t frame, int scene)
{
    const int rowHeight = height / 9;
    // 바탕 (장면마다 밝기 분포와 무늬가 다르다)
    for (int y = rowHeight; y < height; ++y)
    {
        uint8_t* row = bgra + (size_t) y * stride;
        for (int x = 0; x < width; ++x)
        {
            int v = 0;
            switch (scene % 4)
            {
                case 0: v = 20 + 70 * (y - rowHeight) / std::max (1, height - rowHeight); break;
                case 1: v = 150 + 80 * x / std::max (1, width); break;
                case 2: v = ((x / 30 + y / 30) % 2 == 0) ? 40 : 200; break;
                default: v = ((x + y) / 12 % 2 == 0) ? 90 : 170; break;
            }
            const int tint = (scene * 37) % 60 - 30;
            row[x * 4 + 0] = (uint8_t) std::clamp (v - tint, 0, 255);
            row[x * 4 + 1] = (uint8_t) std::clamp (v, 0, 255);
            row[x * 4 + 2] = (uint8_t) std::clamp (v + tint, 0, 255);
            row[x * 4 + 3] = 255;
        }
    }
    // 움직이는 네모 (프레임마다 4 픽셀)
    const int size = std::max (8, height / 6);
    const int sx = (int) ((frame * 4) % std::max<int64_t> (1, width - size)), sy = height / 2;
    for (int y = sy; y < std::min (height, sy + size); ++y)
        for (int x = sx; x < sx + size; ++x)
        {
            uint8_t* p = bgra + (size_t) y * stride + (size_t) x * 4;
            p[0] = p[1] = p[2] = 250;
        }
    // 맨 위 줄: 프레임 번호의 그레이 부호 (왼쪽이 높은 자리)
    const uint32_t gray = (uint32_t) (frame ^ (frame >> 1));
    for (int y = 0; y < rowHeight; ++y)
    {
        uint8_t* row = bgra + (size_t) y * stride;
        for (int x = 0; x < width; ++x)
        {
            const int bit = std::min (patternBits - 1, x * patternBits / width);
            const bool on = ((gray >> (patternBits - 1 - bit)) & 1u) != 0;
            const uint8_t v = on ? 235 : 16;
            row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = v;
            row[x * 4 + 3] = 255;
        }
    }
}

} // namespace

bool videoSupported()
{
    return true;
}

bool writePng (const VideoImage& image, const std::string& path, std::string& error)
{
    if (image.empty())
    {
        error = "no picture to write";
        return false;
    }
    @autoreleasepool
    {
        CGColorSpaceRef space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        CGDataProviderRef provider = CGDataProviderCreateWithData (nullptr, image.rgba.data(), image.rgba.size(), nullptr);
        CGImageRef cg = CGImageCreate ((size_t) image.width, (size_t) image.height, 8, 32, (size_t) image.width * 4, space, rgbaInfo, provider,
                                       nullptr, false, kCGRenderingIntentDefault);
        CGDataProviderRelease (provider);
        CGColorSpaceRelease (space);
        std::error_code ec;
        std::filesystem::remove (path, ec);
        CGImageDestinationRef destination = CGImageDestinationCreateWithURL ((__bridge CFURLRef) fileUrl (path), CFSTR ("public.png"), 1, nullptr);
        bool ok = false;
        if (destination != nullptr && cg != nullptr)
        {
            CGImageDestinationAddImage (destination, cg, nullptr);
            ok = CGImageDestinationFinalize (destination);
        }
        if (destination != nullptr)
            CFRelease (destination);
        if (cg != nullptr)
            CGImageRelease (cg);
        if (! ok)
            error = "cannot write " + path;
        return ok;
    }
}

std::optional<VideoInfo> probeVideo (const std::string& path, std::string& error)
{
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (path, error);
        if (asset == nil)
            return std::nullopt;
        return infoOf (asset);
    }
}

// ---------------------------------------------------------------------------
// VideoFrames
// ---------------------------------------------------------------------------

struct VideoFrames::Impl
{
    AVURLAsset* asset = nil;
    AVAssetImageGenerator* generator = nil;
    VideoInfo info;
    std::string error;
};

VideoFrames::VideoFrames (const std::string& path) : impl_ (std::make_unique<Impl>())
{
    @autoreleasepool
    {
        impl_->asset = openAsset (path, impl_->error);
        if (impl_->asset == nil)
            return;
        impl_->info = infoOf (impl_->asset);
        impl_->generator = [AVAssetImageGenerator assetImageGeneratorWithAsset:impl_->asset];
        impl_->generator.appliesPreferredTrackTransform = YES;
    }
}

VideoFrames::~VideoFrames() = default;

bool VideoFrames::ok() const noexcept { return impl_->generator != nil; }
const std::string& VideoFrames::error() const noexcept { return impl_->error; }
const VideoInfo& VideoFrames::info() const noexcept { return impl_->info; }

std::optional<VideoImage> VideoFrames::frameAt (double seconds, int maxWidth, int maxHeight, double toleranceSeconds)
{
    if (! ok() || seconds < 0.0 || seconds >= impl_->info.durationSeconds)
        return std::nullopt;
    @autoreleasepool
    {
        AVAssetImageGenerator* g = impl_->generator;
        const CMTime tolerance = toleranceSeconds > 0.0 ? timeAt (toleranceSeconds) : kCMTimeZero;
        g.requestedTimeToleranceBefore = tolerance;
        g.requestedTimeToleranceAfter = tolerance;
        g.maximumSize = (maxWidth > 0 || maxHeight > 0) ? CGSizeMake (maxWidth > 0 ? maxWidth : 100000, maxHeight > 0 ? maxHeight : 100000)
                                                       : CGSizeZero;
        CMTime actual = kCMTimeInvalid;
        NSError* err = nil;
        CGImageRef cg = [g copyCGImageAtTime:timeAt (seconds) actualTime:&actual error:&err];
        if (cg == nullptr)
        {
            impl_->error = describe (err, "cannot read the frame");
            return std::nullopt;
        }
        auto image = imageFromCG (cg, CMTIME_IS_VALID (actual) ? CMTimeGetSeconds (actual) : seconds);
        CGImageRelease (cg);
        if (image.empty())
            return std::nullopt;
        return image;
    }
}

// ---------------------------------------------------------------------------
// VideoDecoder
// ---------------------------------------------------------------------------

struct VideoDecoder::Impl
{
    AVAssetReader* reader = nil;
    AVAssetReaderTrackOutput* output = nil;
    CGAffineTransform transform = CGAffineTransformIdentity;
    int width = 0, height = 0;          // 돌리기 전 (영상 좌표) 의 출력 크기
    double start = 0.0;
    bool started = false;
    SampleRef held;                     // 시작 시각에 보이는 프레임을 찾으며 미리 읽은 것
    std::string error;

    SampleRef read()
    {
        if (reader.status != AVAssetReaderStatusReading)
            return {};
        return SampleRef ([output copyNextSampleBuffer]);
    }
};

VideoDecoder::VideoDecoder (const std::string& path, double startSeconds, int maxWidth, int maxHeight) : impl_ (std::make_unique<Impl>())
{
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (path, impl_->error);
        if (asset == nil)
            return;
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        NSError* err = nil;
        impl_->reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
        if (impl_->reader == nil)
        {
            impl_->error = describe (err, "cannot read " + path);
            return;
        }
        impl_->transform = track.preferredTransform;
        const bool sideways = quarterTurns (impl_->transform) % 2 == 1;
        const CGSize natural = track.naturalSize;
        const int displayW = (int) std::lround (sideways ? natural.height : natural.width);
        const int displayH = (int) std::lround (sideways ? natural.width : natural.height);
        const auto [w, h] = fitted (displayW, displayH, maxWidth, maxHeight);
        impl_->width = sideways ? h : w;
        impl_->height = sideways ? w : h;
        impl_->output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
                                                                   outputSettings:@{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) }];
        impl_->output.alwaysCopiesSampleData = NO;
        impl_->start = std::max (0.0, startSeconds);
        // 시작 시각에 보이는 프레임이 그 앞에서 시작하므로 한 프레임 앞부터 읽는다
        const double lead = CMTIME_IS_VALID (track.minFrameDuration) ? 2.0 * CMTimeGetSeconds (track.minFrameDuration) : 0.1;
        impl_->reader.timeRange = CMTimeRangeMake (timeAt (std::max (0.0, impl_->start - lead)), kCMTimePositiveInfinity);
        [impl_->reader addOutput:impl_->output];
        if (! [impl_->reader startReading])
        {
            impl_->error = describe (impl_->reader.error, "cannot decode " + path);
            impl_->reader = nil;
        }
    }
}

VideoDecoder::~VideoDecoder()
{
    @autoreleasepool
    {
        impl_->held.reset();
        if (impl_->reader != nil && impl_->reader.status == AVAssetReaderStatusReading)
            [impl_->reader cancelReading];
    }
}

bool VideoDecoder::ok() const noexcept { return impl_->reader != nil; }
const std::string& VideoDecoder::error() const noexcept { return impl_->error; }

bool VideoDecoder::next (VideoImage& out)
{
    if (! ok())
        return false;
    @autoreleasepool
    {
        SampleRef sample;
        if (! impl_->started)
        {
            impl_->started = true;
            // 시작 시각에 보이는 프레임: 시각이 시작보다 늦지 않은 마지막 프레임
            SampleRef last;
            for (;;)
            {
                SampleRef s = impl_->read();
                if (! s)
                    break;
                if (CMSampleBufferGetImageBuffer (s.ref) == nullptr)
                    continue;
                if (s.seconds() > impl_->start + 1.0e-6)
                {
                    impl_->held = std::move (s);
                    break;
                }
                last = std::move (s);
            }
            sample = last ? std::move (last) : std::move (impl_->held);
        }
        else if (impl_->held)
            sample = std::move (impl_->held);
        else
        {
            do
                sample = impl_->read();
            while (sample && CMSampleBufferGetImageBuffer (sample.ref) == nullptr);
        }
        if (! sample)
            return false;
        auto pixels = scaledFromPixelBuffer (CMSampleBufferGetImageBuffer (sample.ref), impl_->width, impl_->height);
        out = oriented (std::move (pixels), impl_->width, impl_->height, impl_->transform, sample.seconds());
        return true;
    }
}

// ---------------------------------------------------------------------------
// 소리
// ---------------------------------------------------------------------------

std::optional<codec::Decoded> readVideoAudio (const std::string& path, double sampleRate, std::string& error)
{
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (path, error);
        if (asset == nil)
            return std::nullopt;
        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeAudio];
        if (tracks.count == 0)
        {
            error = "the video has no sound";
            return std::nullopt;
        }
        const auto info = infoOf (asset);
        const double rate = sampleRate > 0.0 ? sampleRate : (info.audioSampleRate > 0.0 ? info.audioSampleRate : 48000.0);
        const int channels = info.audioChannels == 1 ? 1 : 2;
        AudioChannelLayout layout = stereoLayout();
        if (channels == 1)
            layout.mChannelLayoutTag = kAudioChannelLayoutTag_Mono;
        NSDictionary* settings = @{ AVFormatIDKey: @(kAudioFormatLinearPCM),
                                    AVLinearPCMIsFloatKey: @YES,
                                    AVLinearPCMBitDepthKey: @32,
                                    AVLinearPCMIsNonInterleaved: @NO,
                                    AVLinearPCMIsBigEndianKey: @NO,
                                    AVSampleRateKey: @(rate),
                                    AVNumberOfChannelsKey: @(channels),
                                    AVChannelLayoutKey: [NSData dataWithBytes:&layout length:sizeof (layout)] };
        NSError* err = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
        if (reader == nil)
        {
            error = describe (err, "cannot read " + path);
            return std::nullopt;
        }
        AVAssetReaderAudioMixOutput* output = [AVAssetReaderAudioMixOutput assetReaderAudioMixOutputWithAudioTracks:tracks audioSettings:settings];
        [reader addOutput:output];
        if (! [reader startReading])
        {
            error = describe (reader.error, "cannot decode the sound of " + path);
            return std::nullopt;
        }
        codec::Decoded decoded;
        decoded.channels.assign ((size_t) channels, {});
        decoded.sampleRate = rate;
        decoded.bitDepth = 32;
        decoded.floatingPoint = true;
        decoded.format = "video sound";
        std::vector<float> chunk;
        for (;;)
        {
            SampleRef s ([output copyNextSampleBuffer]);
            if (! s)
                break;
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer (s.ref);
            if (block == nullptr)
                continue;
            const size_t bytes = CMBlockBufferGetDataLength (block);
            chunk.resize (bytes / sizeof (float));
            if (CMBlockBufferCopyDataBytes (block, 0, chunk.size() * sizeof (float), chunk.data()) != kCMBlockBufferNoErr)
                continue;
            const size_t frames = chunk.size() / (size_t) channels;
            for (int ch = 0; ch < channels; ++ch)
            {
                auto& dest = decoded.channels[(size_t) ch];
                const size_t at = dest.size();
                dest.resize (at + frames);
                for (size_t i = 0; i < frames; ++i)
                    dest[at + i] = chunk[i * (size_t) channels + (size_t) ch];
            }
        }
        if (reader.status == AVAssetReaderStatusFailed)
        {
            error = describe (reader.error, "cannot decode the sound of " + path);
            return std::nullopt;
        }
        decoded.frames = (int64_t) decoded.channels[0].size();
        return decoded;
    }
}

// ---------------------------------------------------------------------------
// 장면 바뀜
// ---------------------------------------------------------------------------

std::optional<std::vector<SceneChange>> detectSceneChanges (const std::string& path, const SceneOptions& options, std::string& error,
                                                            const std::function<bool (double)>& progress)
{
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (path, error);
        if (asset == nil)
            return std::nullopt;
        const auto info = infoOf (asset);
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        NSError* err = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
        if (reader == nil)
        {
            error = describe (err, "cannot read " + path);
            return std::nullopt;
        }
        AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
                                                                                      outputSettings:@{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA) }];
        output.alwaysCopiesSampleData = NO;
        [reader addOutput:output];
        if (! [reader startReading])
        {
            error = describe (reader.error, "cannot decode " + path);
            return std::nullopt;
        }
        std::vector<double> times, differences;
        Histogram previous {};
        bool first = true;
        for (;;)
        {
            @autoreleasepool
            {
                SampleRef s ([output copyNextSampleBuffer]);
                if (! s)
                    break;
                CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer (s.ref);
                if (pixels == nullptr)
                    continue;
                CVPixelBufferLockBaseAddress (pixels, kCVPixelBufferLock_ReadOnly);
                const auto h = lumaHistogram ((const uint8_t*) CVPixelBufferGetBaseAddress (pixels), CVPixelBufferGetWidth (pixels),
                                              CVPixelBufferGetHeight (pixels), CVPixelBufferGetBytesPerRow (pixels));
                CVPixelBufferUnlockBaseAddress (pixels, kCVPixelBufferLock_ReadOnly);
                times.push_back (s.seconds());
                differences.push_back (first ? 0.0 : histogramDistance (previous, h));
                previous = h;
                first = false;
                if (progress && info.durationSeconds > 0.0 && ! progress (std::min (1.0, s.seconds() / info.durationSeconds)))
                {
                    [reader cancelReading];
                    error = "cancelled";
                    return std::nullopt;
                }
            }
        }
        if (reader.status == AVAssetReaderStatusFailed)
        {
            error = describe (reader.error, "cannot decode " + path);
            return std::nullopt;
        }
        std::vector<SceneChange> scenes;
        for (const size_t k : pickSceneChanges (differences, info.rate.fps(), options))
            scenes.push_back ({ times[k], frameAtSeconds (times[k], info.rate), differences[k] });
        return scenes;
    }
}

// ---------------------------------------------------------------------------
// 음악을 입혀 쓰기
// ---------------------------------------------------------------------------

std::optional<ScoreVideoReport> writeScoredVideo (const std::string& videoPath, const ScoreAudio& audio, const std::string& outputPath,
                                                  const ScoreVideoOptions& options, std::string& error)
{
    if (audio.numChannels < 1 || audio.channels == nullptr || ! (audio.sampleRate > 0.0))
    {
        error = "no music to put on the video";
        return std::nullopt;
    }
    if (audio.sampleRate > 48000.0)
    {
        error = "AAC holds up to 48 kHz; render the music at 48000 or 44100";
        return std::nullopt;
    }
    if (std::filesystem::weakly_canonical (videoPath) == std::filesystem::weakly_canonical (outputPath))
    {
        error = "the output must not replace the source video";
        return std::nullopt;
    }
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (videoPath, error);
        if (asset == nil)
            return std::nullopt;
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        ScoreVideoReport report;
        const double duration = CMTimeGetSeconds (asset.duration);
        report.durationSeconds = std::isfinite (duration) ? duration : 0.0;

        std::optional<codec::Decoded> original;
        if (options.mixOriginal && [[asset tracksWithMediaType:AVMediaTypeAudio] count] > 0)
        {
            original = readVideoAudio (videoPath, audio.sampleRate, error);
            if (! original)
                return std::nullopt;
            report.mixedOriginal = true;
        }

        NSError* err = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
        if (reader == nil)
        {
            error = describe (err, "cannot read " + videoPath);
            return std::nullopt;
        }
        AVAssetReaderTrackOutput* videoOut = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
        videoOut.alwaysCopiesSampleData = NO;
        [reader addOutput:videoOut];

        AVAssetWriter* writer = newWriter (outputPath, report.fileType, error);
        if (writer == nil)
            return std::nullopt;
        if (asset.duration.timescale > 0)
            writer.movieTimeScale = asset.duration.timescale;
        CMFormatDescriptionRef hint = track.formatDescriptions.count > 0 ? (__bridge CMFormatDescriptionRef) track.formatDescriptions[0] : nullptr;
        AVAssetWriterInput* videoIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:nil sourceFormatHint:hint];
        videoIn.transform = track.preferredTransform;
        videoIn.mediaTimeScale = track.naturalTimeScale;
        videoIn.expectsMediaDataInRealTime = NO;
        AVAssetWriterInput* audioIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio
                                                                         outputSettings:aacSettings (audio.sampleRate, options.aacBitrate)];
        audioIn.expectsMediaDataInRealTime = NO;
        if (! [writer canAddInput:videoIn] || ! [writer canAddInput:audioIn])
        {
            error = "the " + report.fileType + " file cannot hold this video (" + infoOf (asset).codec + ")";
            return std::nullopt;
        }
        [writer addInput:videoIn];
        [writer addInput:audioIn];
        if (! [reader startReading])
        {
            error = describe (reader.error, "cannot read " + videoPath);
            return std::nullopt;
        }

        const double sr = audio.sampleRate;
        const int64_t total = (int64_t) std::llround (report.durationSeconds * sr);
        const int64_t shift = (int64_t) std::llround (audio.offsetSeconds * sr);   // 영상 프레임 i 의 곡 샘플은 i + shift
        const float originalGain = (float) std::pow (10.0, options.originalGainDb / 20.0);
        CMAudioFormatDescriptionRef format = floatFormat (sr, 2);
        int64_t written = 0;
        int64_t musicFrames = 0;
        std::vector<float> interleaved;
        constexpr int chunk = 4096;

        std::vector<Feed> feeds;
        feeds.push_back ({ videoIn, [&]() -> bool
        {
            SampleRef s ([videoOut copyNextSampleBuffer]);
            if (! s)
                return false;
            const bool media = CMSampleBufferGetDataBuffer (s.ref) != nullptr;
            if ([videoIn appendSampleBuffer:s.ref] && media)
                report.videoSamples += (int64_t) CMSampleBufferGetNumSamples (s.ref);
            return true;
        } });
        feeds.push_back ({ audioIn, [&]() -> bool
        {
            if (written >= total)
                return false;
            const int n = (int) std::min<int64_t> (chunk, total - written);
            interleaved.assign ((size_t) n * 2, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const int64_t song = written + i + shift;
                if (song >= 0 && song < audio.frames)
                {
                    ++musicFrames;
                    for (int ch = 0; ch < 2; ++ch)
                        interleaved[(size_t) i * 2 + (size_t) ch] = audio.channels[std::min (ch, audio.numChannels - 1)][song];
                }
                if (original)
                {
                    const int64_t at = written + i;
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const auto& source = original->channels[std::min ((size_t) ch, original->channels.size() - 1)];
                        if (at < (int64_t) source.size())
                            interleaved[(size_t) i * 2 + (size_t) ch] += originalGain * source[(size_t) at];
                    }
                }
            }
            CMSampleBufferRef sample = audioBuffer (interleaved, n, 2, sr, written, format);
            if (sample == nullptr)
                return false;
            [audioIn appendSampleBuffer:sample];
            CFRelease (sample);
            written += n;
            return true;
        } });
        const bool ok = runWriter (writer, feeds, error);
        if (format != nullptr)
            CFRelease (format);
        if (reader.status == AVAssetReaderStatusFailed)
        {
            error = describe (reader.error, "cannot read " + videoPath);
            return std::nullopt;
        }
        if (! ok)
            return std::nullopt;
        report.audioFrames = written;
        report.musicSeconds = (double) musicFrames / sr;
        return report;
    }
}

std::optional<std::vector<SampleDigest>> videoSampleDigests (const std::string& path, std::string& error)
{
    @autoreleasepool
    {
        AVURLAsset* asset = openAsset (path, error);
        if (asset == nil)
            return std::nullopt;
        AVAssetTrack* track = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
        NSError* err = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&err];
        if (reader == nil)
        {
            error = describe (err, "cannot read " + path);
            return std::nullopt;
        }
        AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track outputSettings:nil];
        [reader addOutput:output];
        if (! [reader startReading])
        {
            error = describe (reader.error, "cannot read " + path);
            return std::nullopt;
        }
        // 샘플의 시각은 미디어 시간축이다. 트랙의 편집 목록으로 영상의 시각으로 바꾼다 (mp4 는 B 프레임의 지연을 편집으로 감춘다)
        NSArray<AVAssetTrackSegment*>* segments = track.segments;
        auto presented = [segments] (CMTime pts)
        {
            for (AVAssetTrackSegment* segment in segments)
            {
                if (segment.empty)
                    continue;
                const CMTimeMapping m = segment.timeMapping;
                if (CMTimeRangeContainsTime (m.source, pts))
                {
                    const double scale = CMTimeGetSeconds (m.target.duration) / std::max (1.0e-12, CMTimeGetSeconds (m.source.duration));
                    return CMTimeGetSeconds (m.target.start) + (CMTimeGetSeconds (pts) - CMTimeGetSeconds (m.source.start)) * scale;
                }
            }
            return CMTimeGetSeconds (pts);
        };
        std::vector<SampleDigest> digests;
        std::vector<uint8_t> bytes;
        for (;;)
        {
            SampleRef s ([output copyNextSampleBuffer]);
            if (! s)
                break;
            CMBlockBufferRef block = CMSampleBufferGetDataBuffer (s.ref);
            if (block == nullptr)
                continue;
            bytes.resize (CMBlockBufferGetDataLength (block));
            CMBlockBufferCopyDataBytes (block, 0, bytes.size(), bytes.data());
            const double seconds = std::round (presented (CMSampleBufferGetPresentationTimeStamp (s.ref)) * 1.0e6) / 1.0e6;
            digests.push_back ({ seconds, bytes.size(), fnv1a (bytes.data(), bytes.size()) });
        }
        return digests;
    }
}

// ---------------------------------------------------------------------------
// 시험 영상
// ---------------------------------------------------------------------------

bool writeTestPattern (const std::string& path, const TestPattern& p, std::string& error)
{
    if (! p.rate.valid() || p.frames < 1 || p.width < 32 || p.height < 32 || p.width % 2 != 0 || p.height % 2 != 0)
    {
        error = "a test pattern needs a frame rate, frames and an even size of at least 32";
        return false;
    }
    @autoreleasepool
    {
        std::string typeName;
        AVAssetWriter* writer = newWriter (path, typeName, error);
        if (writer == nil)
            return false;
        writer.movieTimeScale = (p.rate.den == 1 && 600 % p.rate.num == 0) ? 600 : p.rate.num;
        NSDictionary* settings = @{ AVVideoCodecKey: AVVideoCodecTypeH264,
                                    AVVideoWidthKey: @(p.width),
                                    AVVideoHeightKey: @(p.height),
                                    AVVideoCompressionPropertiesKey: @{ AVVideoAverageBitRateKey: @(p.width * p.height * 60),
                                                                        AVVideoMaxKeyFrameIntervalKey: @(std::max (1, 2 * p.rate.nominal())) } };
        AVAssetWriterInput* videoIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:settings];
        videoIn.expectsMediaDataInRealTime = NO;
        videoIn.mediaTimeScale = p.rate.num;
        if (p.quarterTurns % 4 != 0)
        {
            const double angle = (p.quarterTurns % 4) * M_PI / 2.0;
            const double w = p.width, h = p.height;
            CGAffineTransform t = CGAffineTransformMakeRotation (angle);
            // 돌린 그림이 양의 사분면에 오게 옮긴다
            const CGRect box = CGRectApplyAffineTransform (CGRectMake (0, 0, w, h), t);
            t.tx = -box.origin.x;
            t.ty = -box.origin.y;
            videoIn.transform = t;
        }
        AVAssetWriterInputPixelBufferAdaptor* adaptor =
            [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:videoIn
                                                                             sourcePixelBufferAttributes:@{ (id) kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
                                                                                                            (id) kCVPixelBufferWidthKey: @(p.width),
                                                                                                            (id) kCVPixelBufferHeightKey: @(p.height) }];
        [writer addInput:videoIn];
        AVAssetWriterInput* audioIn = nil;
        if (! p.clickSeconds.empty())
        {
            audioIn = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeAudio outputSettings:aacSettings (p.audioSampleRate, 192000)];
            audioIn.expectsMediaDataInRealTime = NO;
            [writer addInput:audioIn];
        }

        int64_t frame = 0;
        std::vector<Feed> feeds;
        feeds.push_back ({ videoIn, [&]() -> bool
        {
            if (frame >= p.frames)
                return false;
            CVPixelBufferRef pixels = nullptr;
            if (adaptor.pixelBufferPool == nullptr
                || CVPixelBufferPoolCreatePixelBuffer (kCFAllocatorDefault, adaptor.pixelBufferPool, &pixels) != kCVReturnSuccess)
                return false;
            CVPixelBufferLockBaseAddress (pixels, 0);
            int scene = 0;
            for (const auto cut : p.sceneCuts)
                if (frame >= cut)
                    ++scene;
            drawPattern ((uint8_t*) CVPixelBufferGetBaseAddress (pixels), (int) CVPixelBufferGetWidth (pixels), (int) CVPixelBufferGetHeight (pixels),
                         CVPixelBufferGetBytesPerRow (pixels), frame, scene);
            CVPixelBufferUnlockBaseAddress (pixels, 0);
            const BOOL appended = [adaptor appendPixelBuffer:pixels withPresentationTime:CMTimeMake (frame * p.rate.den, p.rate.num)];
            CVPixelBufferRelease (pixels);
            ++frame;
            return appended == YES;
        } });

        const double sr = p.audioSampleRate;
        const int64_t audioTotal = (int64_t) std::llround (secondsAtFrame (p.frames, p.rate) * sr);
        int64_t audioWritten = 0;
        CMAudioFormatDescriptionRef format = audioIn != nil ? floatFormat (sr, 2) : nullptr;
        std::vector<float> interleaved;
        if (audioIn != nil)
            feeds.push_back ({ audioIn, [&]() -> bool
            {
                if (audioWritten >= audioTotal)
                    return false;
                const int n = (int) std::min<int64_t> (4096, audioTotal - audioWritten);
                interleaved.assign ((size_t) n * 2, 0.0f);
                const int clickLength = (int) std::lround (0.005 * sr);
                for (const double at : p.clickSeconds)
                {
                    const int64_t start = (int64_t) std::llround (at * sr);
                    for (int64_t i = std::max (start, audioWritten); i < std::min<int64_t> (start + clickLength, audioWritten + n); ++i)
                    {
                        const double t = (double) (i - start) / sr;
                        const double window = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) (i - start) / clickLength);
                        const float v = (float) (0.5 * window * std::sin (2.0 * M_PI * 1000.0 * t));
                        interleaved[(size_t) (i - audioWritten) * 2] = v;
                        interleaved[(size_t) (i - audioWritten) * 2 + 1] = v;
                    }
                }
                CMSampleBufferRef sample = audioBuffer (interleaved, n, 2, sr, audioWritten, format);
                if (sample == nullptr)
                    return false;
                [audioIn appendSampleBuffer:sample];
                CFRelease (sample);
                audioWritten += n;
                return true;
            } });
        const bool ok = runWriter (writer, feeds, error);
        if (format != nullptr)
            CFRelease (format);
        return ok && frame == p.frames;
    }
}

} // namespace mr::media
