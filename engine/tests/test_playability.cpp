#include "TestFramework.h"
#include "TestUtil.h"

#include "analysis/Playability.h"

using namespace mr;

namespace
{

std::string n (int pitch, double start, double length)
{
    return "{ \"pitch\": " + std::to_string (pitch) + ", \"startBeat\": " + juce::String (start).toStdString() + ", \"durationBeat\": "
           + juce::String (length).toStdString() + ", \"velocity\": 80 }";
}

// 손 둘 (오른손, 왼손) 의 곡. 120 BPM 이라 한 박이 0.5초
Composition hands (const std::string& right, const std::string& left, const std::string& leftExtra = {})
{
    auto r = mrtest::parse (R"({ "tempo": 120, "timeSignature": [4, 4], "tracks": [
        { "name": "Right", "instrument": "synth.piano", "scorePart": "Piano", "notes": [ )" + right + R"( ] },
        { "name": "Left", "instrument": "synth.piano", "scorePart": "Piano")" + leftExtra + R"(, "notes": [ )" + left + " ] } ] }");
    if (! r.composition)
        for (const auto& e : r.diagnostics.errors)
            std::fprintf (stderr, "    parse error %s: %s\n", e.path.c_str(), e.message.c_str());
    REQUIRE (r.composition.has_value());
    return *r.composition;
}

std::string kinds (const PlayabilityReport& r)
{
    std::string s;
    for (const auto& i : r.issues)
        s += (s.empty() ? "" : " ") + i.kind + "@" + std::to_string (i.bar);
    return s;
}

} // namespace

TEST (playability_accepts_idiomatic_piano_writing)
{
    // 옥타브 선율, 10도의 분산화음, 굴리는 10도, 베이스에서 화음으로 뛰는 스트라이드 (한 박 0.5초), 앞꾸밈음
    const auto c = hands (n (72, 0, 1) + ", " + n (84, 0, 1) + ", " + n (74, 1, 1) + ", " + n (86, 1, 1) + ", " + n (75, 1.91, 0.08) + ", " + n (76, 2, 2),
                          n (36, 0, 0.5) + ", " + n (43, 0.5, 0.5) + ", " + n (52, 1, 1) + ", " + n (36, 2, 1.9) + ", " + n (43, 2.045, 1.9) + ", " + n (52, 2.09, 1.9)
                              + ", " + n (33, 4, 0.9) + ", " + n (57, 5, 0.9) + ", " + n (60, 5, 0.9) + ", " + n (64, 5, 0.9));
    const auto r = checkPlayability (c);
    CHECK (kinds (r).empty());
    REQUIRE (r.hands.size() == 2);
    CHECK (r.hands[0].widestSpan == 12 && r.hands[1].widestSpan == 16);
}

TEST (playability_finds_what_a_hand_cannot_do)
{
    // 함께 치는 12도, 여섯 음, 0.1초에 두 옥타브, 0.05초의 이은 음, 같은 건반을 0.06초에
    const auto span = checkPlayability (hands (n (60, 0, 1) + ", " + n (79, 0, 1), n (36, 0, 1)));
    CHECK (kinds (span) == "span@1" && span.issues[0].value == 19 && span.issues[0].limit == 14);
    const auto six = checkPlayability (hands (n (60, 0, 1) + ", " + n (62, 0, 1) + ", " + n (64, 0, 1) + ", " + n (65, 0, 1) + ", " + n (67, 0, 1) + ", " + n (69, 0, 1), n (36, 0, 1)));
    CHECK (kinds (six) == "notes@1");
    const auto shift = checkPlayability (hands (n (72, 0, 1), n (36, 4, 0.2) + ", " + n (60, 4.2, 0.5)));
    REQUIRE (shift.issues.size() == 1);
    CHECK (shift.issues[0].kind == "shift" && shift.issues[0].bar == 2 && shift.issues[0].semitones == 24 && std::abs (shift.issues[0].value - 0.1) < 1.0e-6);
    const auto fast = checkPlayability (hands (n (72, 0, 0.1) + ", " + n (74, 0.15, 0.5), n (36, 0, 1)));
    CHECK (kinds (fast) == "fast@1");
    const auto repeat = checkPlayability (hands (n (72, 0, 0.1) + ", " + n (72, 0.18, 0.5), n (36, 0, 1)));
    CHECK (kinds (repeat) == "repeat@1");
    // 손 크기를 넓히면 12도가 통과한다
    PlayabilityOptions wide;
    wide.span = 19;
    wide.rolledSpan = 19;
    CHECK (checkPlayability (hands (n (60, 0, 1) + ", " + n (79, 0, 1), n (36, 0, 1)), wide).issues.empty());
}

TEST (playability_held_notes_need_the_pedal_and_hands_do_not_cross)
{
    // 붙든 낮은 음 위로 두 옥타브 위의 음: 페달이 없으면 칠 수 없고, 페달을 밟으면 된다
    const std::string left = n (36, 0, 4) + ", " + n (64, 2, 1);
    CHECK (kinds (checkPlayability (hands (n (84, 0, 4), left))) == "held@1");
    const auto pedalled = hands (n (84, 0, 4), left, R"(, "automation": { "cc64": [ { "beat": 0, "value": 127, "curve": "step" } ] })");
    CHECK (kinds (checkPlayability (pedalled)).empty());
    // 같은 순간에 왼손이 오른손의 음 위로 올라간다
    const auto crossed = checkPlayability (hands (n (60, 4, 1) + ", " + n (64, 4, 1), n (48, 4, 1) + ", " + n (62, 4, 1)));
    CHECK (kinds (crossed) == "overlap@2");
    // 트랙을 고르면 그 손만 본다
    PlayabilityOptions one;
    one.tracks = { 0 };
    CHECK (checkPlayability (hands (n (60, 4, 1) + ", " + n (64, 4, 1), n (48, 4, 1) + ", " + n (62, 4, 1)), one).issues.empty());
}
