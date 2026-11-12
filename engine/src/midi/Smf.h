// 표준 MIDI 파일(SMF) 읽기와 쓰기. 이벤트는 틱 단위 시각과 상태 바이트부터의 원래 바이트를 가진다.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mr::midi
{

struct Event
{
    double time = 0.0;              // 틱 (SMPTE 파일을 초로 바꾼 뒤에는 초)
    std::vector<uint8_t> bytes;     // 상태 바이트부터. 메타 이벤트는 FF, 종류, 가변 길이, 데이터

    uint8_t status() const noexcept { return bytes.empty() ? 0 : bytes[0]; }
    int channel() const noexcept;   // 1~16, 채널 메시지가 아니면 0
    int data1() const noexcept { return bytes.size() > 1 ? bytes[1] : 0; }
    int data2() const noexcept { return bytes.size() > 2 ? bytes[2] : 0; }
    bool isNoteOn() const noexcept { return (status() & 0xf0) == 0x90 && data2() != 0; }
    bool isNoteOff() const noexcept { return (status() & 0xf0) == 0x80 || ((status() & 0xf0) == 0x90 && data2() == 0); }
    bool isController() const noexcept { return (status() & 0xf0) == 0xb0; }
    bool isProgramChange() const noexcept { return (status() & 0xf0) == 0xc0; }
    bool isChannelPressure() const noexcept { return (status() & 0xf0) == 0xd0; }
    bool isPitchWheel() const noexcept { return (status() & 0xf0) == 0xe0; }
    int pitchWheelValue() const noexcept { return data1() | (data2() << 7); }

    int metaType() const noexcept;               // 메타 이벤트가 아니면 -1
    std::vector<uint8_t> metaData() const;       // 메타 이벤트의 데이터 부분
    bool isText() const noexcept { const int t = metaType(); return t > 0 && t < 16; }
    std::string text() const;                    // 텍스트 메타 이벤트의 UTF-8 글
    bool isTempo() const noexcept { return metaType() == 0x51; }
    double tempoSecondsPerQuarter() const;       // 템포가 아니면 0
    bool isTimeSignature() const noexcept { return metaType() == 0x58; }
    bool isKeySignature() const noexcept { return metaType() == 0x59; }
    bool isEndOfTrack() const noexcept { return metaType() == 0x2f; }

    static Event noteOn (int channel, int note, int velocity, double time);
    static Event noteOff (int channel, int note, double time);
    static Event controller (int channel, int number, int value, double time);
    static Event channelPressure (int channel, int value, double time);
    static Event pitchWheel (int channel, int value, double time);   // 0~16383
    static Event textMeta (int type, const std::string& utf8, double time);
    static Event tempo (int microsecondsPerQuarter, double time);
    static Event timeSignature (int numerator, int denominator, double time);
    static Event keySignature (int fifths, bool minor, double time);
};

// 트랙 하나의 이벤트 목록. 음 켜짐마다 짝이 되는 음 끔을 찾아 둘 수 있다.
class Sequence
{
public:
    Sequence() = default;
    Sequence (const Sequence& other);   // 음 끔의 짝도 사본 안에서 다시 잇는다
    Sequence& operator= (const Sequence& other);
    Sequence (Sequence&&) noexcept = default;
    Sequence& operator= (Sequence&&) noexcept = default;

    // 같은 시각의 이벤트 뒤, 더 늦은 이벤트 앞에 넣는다
    void add (Event e);
    // 시각순으로 정렬한다 (같은 시각은 넣은 순서를 지킨다)
    void sort();
    // 음 켜짐마다 뒤에서 처음 만나는 같은 채널, 같은 음의 음 끔을 짝으로 둔다. 끄기 전에 같은 음이 다시 켜지면
    // 그 자리에 음 끔을 넣어 짝으로 둔다.
    void matchNoteOffs();

    size_t size() const noexcept { return items_.size(); }
    const Event& operator[] (size_t i) const { return items_[i]->event; }
    Event& operator[] (size_t i) { return items_[i]->event; }
    const Event* noteOffFor (size_t i) const { return items_[i]->off; }   // matchNoteOffs 뒤, 없으면 nullptr

private:
    friend struct SequenceAccess;
    struct Item
    {
        Event event;
        const Event* off = nullptr;
    };
    std::vector<std::unique_ptr<Item>> items_;
};

struct File
{
    int format = 1;
    int timeFormat = 960;   // 양수면 4분음표당 틱, 음수면 SMPTE (상위 바이트가 -초당 프레임, 하위 바이트가 프레임당 틱)
    std::vector<Sequence> tracks;
};

// 형식 1 의 파일 바이트. 트랙마다 같은 상태 바이트가 이어지면 생략하고(러닝 스테이터스), 트랙 끝 이벤트가 없으면 붙인다.
std::vector<uint8_t> write (const File& file);

// 읽은 트랙은 시각순이고, 같은 시각에서는 음 끔이 같은 음의 켜짐보다 앞에 오며, 음 끔의 짝을 찾아 둔다. 파일이 아니면 nullopt.
std::optional<File> read (const std::vector<uint8_t>& bytes);

// SMPTE 시각(틱)을 초로 바꾼다 (timeFormat 이 음수일 때)
void convertSmpteToSeconds (File& file);

} // namespace mr::midi
