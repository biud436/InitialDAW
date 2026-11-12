// 오디오 파일 형식의 인코딩과 디코딩: WAV (쓰기, 읽기), AIFF (읽기), FLAC (libFLAC), Ogg Vorbis (libvorbis), MP3 (macOS 의 AudioToolbox 로 읽기).
// 정수 샘플로 바꾸는 규칙과 헤더 배치는 지금까지 쓰던 JUCE 의 쓰기와 같아, 같은 오디오가 같은 바이트가 된다.
#pragma once

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace mr::codec
{

// 읽은 오디오 (채널마다 float, -1..1)
struct Decoded
{
    std::vector<std::vector<float>> channels;
    double sampleRate = 0.0;
    int bitDepth = 0;
    bool floatingPoint = false;
    int64_t frames = 0;
    std::string format;   // "WAV file", "AIFF file", "FLAC file", "Ogg-Vorbis file", "MP3 file"
};

// 길이와 형식만 (전체를 디코딩하지 않는다, MP3 는 AudioToolbox 의 값)
struct Probe
{
    double sampleRate = 0.0;
    int64_t frames = 0;
    int channels = 0;
    int bitDepth = 0;
    bool floatingPoint = false;
    std::string format;
};

std::optional<Probe> probeFile (const std::string& path);
// 읽지 못하면 nullopt 와 error
std::optional<Decoded> decodeFile (const std::string& path, std::string& error);

// float 를 32비트 정수로: 양 끝을 자르고 INT_MAX 를 곱해 가장 가까운 정수로 (가운데는 짝수 쪽)
int32_t floatToInt32 (float sample) noexcept;

struct WavLoop
{
    int64_t start = 0;            // 첫 샘플
    int64_t endInclusive = 0;     // 마지막 샘플 (포함)
};

// bitDepth 16, 24 는 정수, 32 는 부동소수점
std::vector<uint8_t> encodeWav (const float* const* channels, int numChannels, int64_t numFrames, double sampleRate, int bitDepth,
                                const std::optional<WavLoop>& loop = std::nullopt);

// 블록마다 이어 쓰는 WAV 파일. close() 가 헤더의 크기를 고친다.
class WavFileWriter
{
public:
    WavFileWriter (const std::string& path, double sampleRate, int numChannels, int bitDepth);
    ~WavFileWriter();
    WavFileWriter (const WavFileWriter&) = delete;
    WavFileWriter& operator= (const WavFileWriter&) = delete;

    bool ok() const noexcept { return file_ != nullptr; }
    bool write (const float* const* channels, int numFrames);
    bool close();
    int64_t frames() const noexcept { return frames_; }

private:
    std::FILE* file_ = nullptr;
    double sampleRate_;
    int channels_, bitDepth_;
    int64_t frames_ = 0;
    std::vector<uint8_t> scratch_;
};

// FLAC 16, 24비트 (compression 은 libFLAC 의 압축 단계 0~8). 실패하면 빈 벡터.
std::vector<uint8_t> encodeFlac (const float* const* channels, int numChannels, int64_t numFrames, double sampleRate, int bitDepth,
                                 int compression);

// Ogg Vorbis VBR (quality 0~10). 스트림 번호는 serial. 실패하면 빈 벡터.
std::vector<uint8_t> encodeVorbis (const float* const* channels, int numChannels, int64_t numFrames, double sampleRate, int quality,
                                   uint32_t serial);

} // namespace mr::codec
