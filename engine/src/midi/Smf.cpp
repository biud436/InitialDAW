#include "midi/Smf.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace mr::midi
{

namespace
{
uint8_t statusByte (int type, int channel) { return (uint8_t) (type | std::clamp (channel - 1, 0, 15)); }

void appendVarLen (std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = (uint8_t) (v & 0x7f);
    while ((v >>= 7) != 0)
        tmp[n++] = (uint8_t) ((v & 0x7f) | 0x80);
    while (n > 0)
        out.push_back (tmp[--n]);
}

// 가변 길이 값을 읽는다. 다 읽지 못하면 false.
bool readVarLen (const uint8_t*& p, const uint8_t* end, uint32_t& value)
{
    value = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (p >= end)
            return false;
        const uint8_t b = *p++;
        value = (value << 7) | (b & 0x7f);
        if ((b & 0x80) == 0)
            return true;
    }
    return false;
}

void appendBig (std::vector<uint8_t>& out, uint32_t v, int bytes)
{
    for (int i = bytes - 1; i >= 0; --i)
        out.push_back ((uint8_t) (v >> (8 * i)));
}

bool readBig (const uint8_t*& p, const uint8_t* end, int bytes, uint32_t& v)
{
    if (end - p < bytes)
        return false;
    v = 0;
    for (int i = 0; i < bytes; ++i)
        v = (v << 8) | *p++;
    return true;
}

int channelMessageLength (uint8_t status)
{
    switch (status & 0xf0)
    {
        case 0xc0:
        case 0xd0: return 2;
        default: return 3;
    }
}

Event meta (int type, const std::vector<uint8_t>& data, double time)
{
    Event e;
    e.time = time;
    e.bytes = { 0xff, (uint8_t) type };
    appendVarLen (e.bytes, (uint32_t) data.size());
    e.bytes.insert (e.bytes.end(), data.begin(), data.end());
    return e;
}

// 트랙 청크의 이벤트를 시각을 더해 가며 읽는다 (러닝 스테이터스, 메타, 시스템 익스클루시브)
Sequence readTrack (const uint8_t* p, const uint8_t* end)
{
    Sequence seq;
    double time = 0.0;
    uint8_t running = 0;
    while (p < end)
    {
        uint32_t delta = 0;
        if (! readVarLen (p, end, delta) || p >= end)
            break;
        time += delta;
        Event e;
        e.time = time;
        uint8_t status = *p;
        if (status < 0x80)
        {
            if (running == 0)
                break;
            status = running;
        }
        else
            ++p;
        if (status == 0xff)
        {
            if (p >= end)
                break;
            const uint8_t type = *p++;
            uint32_t length = 0;
            const uint8_t* lengthStart = p;
            if (! readVarLen (p, end, length))
                break;
            const auto available = (uint32_t) (end - p);
            length = std::min (length, available);
            e.bytes.push_back (0xff);
            e.bytes.push_back (type);
            e.bytes.insert (e.bytes.end(), lengthStart, p + length);
            p += length;
        }
        else if (status == 0xf0 || status == 0xf7)
        {
            uint32_t length = 0;
            if (! readVarLen (p, end, length))
                break;
            length = std::min (length, (uint32_t) (end - p));
            e.bytes.push_back (status);
            e.bytes.insert (e.bytes.end(), p, p + length);
            p += length;
        }
        else
        {
            const int length = channelMessageLength (status);
            e.bytes.push_back (status);
            for (int i = 1; i < length && p < end; ++i)
                e.bytes.push_back (*p++);
            running = status;
        }
        seq.add (std::move (e));
    }
    return seq;
}
} // namespace

int Event::channel() const noexcept
{
    const uint8_t s = status();
    return s >= 0x80 && (s & 0xf0) != 0xf0 ? (s & 0x0f) + 1 : 0;
}

int Event::metaType() const noexcept { return bytes.size() >= 2 && bytes[0] == 0xff ? bytes[1] : -1; }

std::vector<uint8_t> Event::metaData() const
{
    if (metaType() < 0)
        return {};
    const uint8_t* p = bytes.data() + 2;
    const uint8_t* end = bytes.data() + bytes.size();
    uint32_t length = 0;
    if (! readVarLen (p, end, length))
        return {};
    length = std::min (length, (uint32_t) (end - p));
    return { p, p + length };
}

std::string Event::text() const
{
    const auto d = metaData();
    return { d.begin(), d.end() };
}

double Event::tempoSecondsPerQuarter() const
{
    if (! isTempo())
        return 0.0;
    const auto d = metaData();
    if (d.size() < 3)
        return 0.0;
    return (double) (((uint32_t) d[0] << 16) | ((uint32_t) d[1] << 8) | d[2]) / 1000000.0;
}

Event Event::noteOn (int channel, int note, int velocity, double time)
{
    return { time, { statusByte (0x90, channel), (uint8_t) (note & 127), (uint8_t) std::clamp (velocity, 0, 127) } };
}

Event Event::noteOff (int channel, int note, double time)
{
    return { time, { statusByte (0x80, channel), (uint8_t) (note & 127), 0 } };
}

Event Event::controller (int channel, int number, int value, double time)
{
    return { time, { statusByte (0xb0, channel), (uint8_t) (number & 127), (uint8_t) (value & 127) } };
}

Event Event::channelPressure (int channel, int value, double time)
{
    return { time, { statusByte (0xd0, channel), (uint8_t) (value & 127) } };
}

Event Event::pitchWheel (int channel, int value, double time)
{
    return { time, { statusByte (0xe0, channel), (uint8_t) (value & 127), (uint8_t) ((value >> 7) & 127) } };
}

Event Event::textMeta (int type, const std::string& utf8, double time)
{
    return meta (type, { utf8.begin(), utf8.end() }, time);
}

Event Event::tempo (int us, double time)
{
    return meta (0x51, { (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us }, time);
}

Event Event::timeSignature (int numerator, int denominator, double time)
{
    int power = 0;
    for (int n = 1; n < denominator; n <<= 1)
        ++power;
    return meta (0x58, { (uint8_t) numerator, (uint8_t) power, 1, 96 }, time);
}

Event Event::keySignature (int fifths, bool minor, double time)
{
    return meta (0x59, { (uint8_t) (int8_t) fifths, (uint8_t) (minor ? 1 : 0) }, time);
}

Sequence::Sequence (const Sequence& other) { *this = other; }

Sequence& Sequence::operator= (const Sequence& other)
{
    if (this == &other)
        return *this;
    std::vector<std::unique_ptr<Item>> copy;
    copy.reserve (other.items_.size());
    for (const auto& item : other.items_)
    {
        copy.push_back (std::make_unique<Item>());
        copy.back()->event = item->event;
    }
    for (size_t i = 0; i < other.items_.size(); ++i)
        if (const auto* off = other.items_[i]->off)
            for (size_t j = 0; j < other.items_.size(); ++j)
                if (&other.items_[j]->event == off)
                {
                    copy[i]->off = &copy[j]->event;
                    break;
                }
    items_ = std::move (copy);
    return *this;
}

void Sequence::add (Event e)
{
    auto item = std::make_unique<Item>();
    item->event = std::move (e);
    const double t = item->event.time;
    auto pos = items_.end();
    while (pos != items_.begin() && (*std::prev (pos))->event.time > t)
        --pos;
    items_.insert (pos, std::move (item));
}

void Sequence::sort()
{
    std::stable_sort (items_.begin(), items_.end(), [] (const auto& a, const auto& b) { return a->event.time < b->event.time; });
}

void Sequence::matchNoteOffs()
{
    for (size_t i = 0; i < items_.size(); ++i)
    {
        auto& on = *items_[i];
        if (! on.event.isNoteOn())
            continue;
        on.off = nullptr;
        const int note = on.event.data1(), chan = on.event.channel();
        const size_t count = items_.size();
        for (size_t j = i + 1; j < count; ++j)
        {
            const auto& e = items_[j]->event;
            if (e.data1() != note || e.channel() != chan)
                continue;
            if (e.isNoteOff())
            {
                on.off = &items_[j]->event;
                break;
            }
            if (e.isNoteOn())
            {
                auto off = std::make_unique<Item>();
                off->event = Event::noteOff (chan, note, e.time);
                on.off = &off->event;
                items_.insert (items_.begin() + (std::ptrdiff_t) j, std::move (off));
                break;
            }
        }
    }
}

std::vector<uint8_t> write (const File& file)
{
    std::vector<uint8_t> out;
    out.insert (out.end(), { 'M', 'T', 'h', 'd' });
    appendBig (out, 6, 4);
    appendBig (out, (uint32_t) file.format, 2);
    appendBig (out, (uint32_t) file.tracks.size(), 2);
    appendBig (out, (uint32_t) (uint16_t) file.timeFormat, 2);
    for (const auto& seq : file.tracks)
    {
        std::vector<uint8_t> body;
        int lastTick = 0;
        uint8_t lastStatus = 0;
        bool ended = false;
        for (size_t i = 0; i < seq.size(); ++i)
        {
            const auto& e = seq[i];
            if (e.isEndOfTrack())
                ended = true;
            const int tick = (int) std::lround (e.time);
            appendVarLen (body, (uint32_t) std::max (0, tick - lastTick));
            lastTick = tick;
            const uint8_t status = e.status();
            size_t from = 0;
            if (status == lastStatus && (status & 0xf0) != 0xf0 && e.bytes.size() > 1 && i > 0)
                from = 1;
            else if (status == 0xf0)
            {
                body.push_back (status);
                appendVarLen (body, (uint32_t) (e.bytes.size() - 1));
                from = 1;
            }
            body.insert (body.end(), e.bytes.begin() + (std::ptrdiff_t) from, e.bytes.end());
            lastStatus = status;
        }
        if (! ended)
            body.insert (body.end(), { 0x00, 0xff, 0x2f, 0x00 });
        out.insert (out.end(), { 'M', 'T', 'r', 'k' });
        appendBig (out, (uint32_t) body.size(), 4);
        out.insert (out.end(), body.begin(), body.end());
    }
    return out;
}

std::optional<File> read (const std::vector<uint8_t>& bytes)
{
    const uint8_t* p = bytes.data();
    const uint8_t* end = p + bytes.size();
    uint32_t id = 0, length = 0, format = 0, tracks = 0, division = 0;
    if (! readBig (p, end, 4, id))
        return std::nullopt;
    if (id == 0x52494646)   // RIFF 안의 MIDI: 앞 몇 낱말 안에서 MThd 를 찾는다
    {
        bool found = false;
        for (int i = 0; i < 8 && ! found; ++i)
        {
            if (! readBig (p, end, 4, id))
                return std::nullopt;
            found = id == 0x4d546864;
        }
        if (! found)
            return std::nullopt;
    }
    else if (id != 0x4d546864)
        return std::nullopt;
    if (! readBig (p, end, 4, length) || length > (uint32_t) (end - p) || length < 6)
        return std::nullopt;
    const uint8_t* headerEnd = p + length;
    if (! readBig (p, end, 2, format) || format > 2 || ! readBig (p, end, 2, tracks) || (format == 0 && tracks != 1)
        || ! readBig (p, end, 2, division))
        return std::nullopt;
    p = headerEnd;

    File file;
    file.format = (int) format;
    file.timeFormat = (int) (int16_t) (uint16_t) division;
    for (uint32_t t = 0; t < tracks; ++t)
    {
        uint32_t chunk = 0, size = 0;
        if (! readBig (p, end, 4, chunk) || ! readBig (p, end, 4, size) || size > (uint32_t) (end - p))
            return std::nullopt;
        if (chunk == 0x4d54726b)
        {
            auto seq = readTrack (p, p + size);
            seq.sort();
            // 같은 시각에서 음 켜짐이 같은 음의 끔보다 앞이면 자리를 바꾼다 (끈 뒤에 다시 켠 것으로 읽는다)
            size_t i = 0;
            while (i < seq.size())
            {
                size_t next = i;
                while (next < seq.size() && seq[next].time == seq[i].time)
                    ++next;
                size_t from = i;
                while (true)
                {
                    size_t on = from;
                    while (on < next && ! seq[on].isNoteOn())
                        ++on;
                    if (on == next)
                        break;
                    size_t off = next;
                    for (size_t k = next; k > on + 1; --k)
                        if (seq[k - 1].isNoteOff() && seq[k - 1].channel() == seq[on].channel() && seq[k - 1].data1() == seq[on].data1())
                        {
                            off = k - 1;
                            break;
                        }
                    if (off == next)
                        break;
                    std::swap (seq[on], seq[off]);
                    from = on + 1;
                }
                i = next;
            }
            seq.matchNoteOffs();
            file.tracks.push_back (std::move (seq));
        }
        p += size;
    }
    if (p != end)
        return std::nullopt;
    return file;
}

void convertSmpteToSeconds (File& file)
{
    if (file.timeFormat >= 0)
        return;
    const double ticksPerSecond = (double) (-(file.timeFormat >> 8)) * (double) (file.timeFormat & 0xff);
    if (ticksPerSecond <= 0.0)
        return;
    for (auto& seq : file.tracks)
        for (size_t i = 0; i < seq.size(); ++i)
            seq[i].time /= ticksPerSecond;
}

} // namespace mr::midi
