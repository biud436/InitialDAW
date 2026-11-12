// 오디오 파일 쓰기 (WAV, FLAC, Ogg Vorbis, MP3)와 읽기 (WAV, AIFF, FLAC, Ogg). 쓰기에는 날짜 같은 메타데이터를 넣지 않아
// 같은 오디오는 언제나 같은 바이트가 된다.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>


namespace mr
{

namespace codec { class WavFileWriter; }

enum class AudioFileType { wav, flac, ogg, mp3 };

// bitDepth 는 WAV(16, 24 정수, 32 부동소수점)와 FLAC(16, 24). quality 는 Ogg 의 VBR 품질 0~10, MP3 의 CBR kbps. 0 아래면 기본값.
struct AudioFileFormat
{
    AudioFileType type = AudioFileType::wav;
    int bitDepth = 24;
    int quality = -1;
};

struct AudioFileTags
{
    std::string title;                     // Ogg 와 FLAC 의 TITLE, MP3 의 ID3v2.4 TIT2 (WAV 에는 쓰지 않는다)
    std::optional<int64_t> loopStart;      // 반복 구간 [loopStart, loopEnd) 샘플: WAV smpl 청크, Ogg 와 FLAC 의 LOOPSTART, LOOPLENGTH
    std::optional<int64_t> loopEnd;
};

// 확장자(.wav, .flac, .ogg, .mp3)로 형식을 고른다. 모르는 확장자면 비어 있다.
std::optional<AudioFileType> audioFileTypeFor (const juce::File& file);
const char* audioFileTypeName (AudioFileType type);
int defaultQuality (AudioFileType type);   // Ogg 6, MP3 320
bool mp3EncoderAvailable();                // LAME 와 함께 빌드했는지

// 쓰기 전에 형식과 값을 검사한다 (렌더링 전에 부른다). 맞지 않으면 Failure("invalid-input", ...) 를 던진다.
void checkAudioFileFormat (const AudioFileFormat& format, double sampleRate, int channels, bool loop);

// 형식이나 값이 맞지 않으면 Failure("invalid-input", ...), 쓰지 못하면 Failure("io", ...) 를 던진다.
void writeAudioFile (const juce::File& file, const juce::AudioBuffer<float>& buffer, int start, int numSamples, double sampleRate,
                     const AudioFileFormat& format, const AudioFileTags& tags = {});

// bitDepth 16, 24 는 정수, 32 는 부동소수점. 실패하면 Failure("io", ...) 를 던진다.
void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buffer, int numSamples, double sampleRate, int bitDepth);

// 블록마다 이어 쓰는 WAV (스템). close() 나 소멸자가 헤더를 마무리한다.
class StreamingWavWriter
{
public:
    StreamingWavWriter (const juce::File& file, double sampleRate, int channels, int bitDepth);
    ~StreamingWavWriter();
    void write (const juce::AudioBuffer<float>& buffer, int start, int numSamples);
    void close();
    const juce::File& file() const noexcept { return file_; }
    int64_t written() const noexcept { return written_; }

private:
    juce::File file_;
    std::unique_ptr<codec::WavFileWriter> writer_;
    int64_t written_ = 0;
};

struct AudioData
{
    juce::AudioBuffer<float> buffer;
    double sampleRate = 0.0;
    int bitDepth = 0;
    bool floatingPoint = false;
    int64_t samples = 0;
    std::string format;
};

AudioData readAudioFile (const juce::File& file);

// Ogg Vorbis 와 FLAC 파일의 Vorbis 주석 ("KEY=value"). 시험과 검사가 쓴다. 읽지 못하면 비어 있다.
std::vector<std::string> readVorbisComments (const juce::File& file);

} // namespace mr
