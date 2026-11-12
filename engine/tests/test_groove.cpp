#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Groove.h"
#include "instruments/AudioClips.h"

#include <cmath>

using namespace mr;

namespace
{
std::vector<EditNote> editable (const std::vector<Note>& notes)
{
    std::vector<EditNote> out;
    for (const auto& n : notes)
        out.push_back ({ n, juce::var() });
    return out;
}

Note note (double start, int velocity)
{
    Note n;
    n.pitch = 60;
    n.startBeat = start;
    n.durationBeat = 0.2;
    n.velocity = velocity;
    return n;
}
} // namespace

TEST (factory_grooves_place_the_offbeats)
{
    const auto* swing = findFactoryGroove ("swing-16-66");
    REQUIRE (swing != nullptr);
    CHECK_NEAR (grooveTarget (*swing, 0.26), 0.25 + 0.16 * 0.5, 1.0e-9);   // 짝(반 박)의 66% 자리
    CHECK_NEAR (grooveTarget (*swing, 0.49), 0.5, 1.0e-9);
    CHECK_NEAR (grooveTarget (*swing, 3.74), 3.75 + 0.08, 1.0e-9);
    const auto* eighth = findFactoryGroove ("swing-8-67");
    REQUIRE (eighth != nullptr);
    CHECK_NEAR (grooveTarget (*eighth, 1.5), 1.5 + 0.17, 1.0e-9);
    const auto* backbeat = findFactoryGroove ("backbeat-accent");
    REQUIRE (backbeat != nullptr);
    CHECK_NEAR (grooveVelocity (*backbeat, 1.0), 1.2, 1.0e-9);
    CHECK_NEAR (grooveVelocity (*backbeat, 2.0), 0.9, 1.0e-9);
    CHECK (factoryGrooves().size() >= 10);
    for (const auto& g : factoryGrooves())
    {
        std::string error;
        const auto back = grooveFromJson (grooveJson (g), error);
        CHECK (back.has_value() && back->steps == g.steps && back->grid == g.grid);
    }
}

TEST (grooves_come_from_notes_and_go_onto_notes)
{
    // 두 마디의 16분음표: 둘째와 넷째 칸이 0.06 박 늦고 (62% 스윙) 세기는 100 과 70
    std::vector<Note> played;
    for (int k = 0; k < 32; ++k)
        played.push_back (note (k * 0.25 + (k % 2 == 1 ? 0.06 : 0.0), k % 2 == 0 ? 100 : 70));
    const auto g = grooveFromNotes (played, 0.0, 8.0, 0.25, 1.0);
    REQUIRE (g.steps.size() == 4);
    CHECK_NEAR (g.steps[0].offset, 0.0, 1.0e-6);
    CHECK_NEAR (g.steps[1].offset, 0.06, 1.0e-6);
    CHECK_NEAR (g.steps[3].offset, 0.06, 1.0e-6);
    CHECK_NEAR (g.steps[0].velocity, 100.0 / 85.0, 1.0e-4);
    CHECK_NEAR (g.steps[1].velocity, 70.0 / 85.0, 1.0e-4);
    // 곧은 16분음표 (세기 80)에 입히면 같은 느낌이 된다
    std::vector<Note> straight;
    for (int k = 0; k < 8; ++k)
        straight.push_back (note (4.0 + k * 0.25, 80));
    const auto full = applyGroove (editable (straight), {}, g, 1.0, 1.0);
    CHECK_NEAR (full.notes[1].note.startBeat, 4.31, 1.0e-6);
    CHECK_NEAR (full.notes[2].note.startBeat, 4.5, 1.0e-6);
    CHECK_EQ (full.notes[0].note.velocity, 94);   // 80 x 1.176
    CHECK_EQ (full.notes[1].note.velocity, 66);
    // 반만: 시간과 세기가 그 쪽으로 반
    const auto half = applyGroove (editable (straight), {}, g, 0.5, 0.5);
    CHECK_NEAR (half.notes[1].note.startBeat, 4.28, 1.0e-6);
    CHECK_EQ (half.notes[0].note.velocity, 87);
    // 고른 음만
    const auto chosen = applyGroove (editable (straight), { 1 }, g, 1.0, 0.0);
    CHECK_NEAR (chosen.notes[1].note.startBeat, 4.31, 1.0e-6);
    CHECK_NEAR (chosen.notes[3].note.startBeat, 4.75, 1.0e-6);
    CHECK_EQ (chosen.notes[1].note.velocity, 80);
}

TEST (grooves_come_from_hits_and_pull_audio_attacks)
{
    // 어택의 박과 크기: 둘째 칸이 0.04 박 늦고 6 dB 작다
    std::vector<double> beats, levels;
    for (int k = 0; k < 16; ++k)
    {
        beats.push_back (k * 0.5 + (k % 2 == 1 ? 0.04 : -0.005));
        levels.push_back (k % 2 == 1 ? -18.0 : -12.0);
    }
    const auto g = grooveFromHits (beats, levels, 0.5, 1.0);
    REQUIRE (g.steps.size() == 2);
    CHECK_NEAR (g.steps[1].offset, 0.04, 1.0e-6);
    CHECK_NEAR (g.steps[0].offset, -0.005, 1.0e-6);
    CHECK_NEAR (g.steps[0].velocity / g.steps[1].velocity, std::pow (10.0, 6.0 / 40.0), 1.0e-3);
    // 오디오 퀀타이즈가 어택을 그루브 자리로 끈다 (120 BPM, 클립은 2 박에서)
    AudioClip clip;
    clip.startBeat = 2.0;
    const TempoMap tempo ({ { 0.0, 120.0, false } });
    std::vector<double> onsets;
    for (int k = 1; k < 8; ++k)
        onsets.push_back (k * 0.25 + 0.007 * ((k % 3) - 1));   // 8분음표에서 ±7 ms
    AudioQuantizeOptions options;
    options.groove = *findFactoryGroove ("swing-8-60");
    const auto warp = quantizedWarp (clip, tempo, 2.2, onsets, options);
    REQUIRE (warp.size() == 9);
    clip.warp = warp;
    for (size_t k = 1; k + 1 < warp.size(); ++k)
        CHECK_NEAR (warpMarkerSongBeat (clip, k), grooveTarget (*options.groove, 2.0 + 0.5 * (double) k), 1.0e-6);
    std::string error;
    CHECK (! grooveFromJson (juce::JSON::parse (R"({ "grid": 0.25, "steps": [ { "offset": 0.3 } ] })"), error).has_value());
    CHECK (! grooveFromJson (juce::JSON::parse (R"({ "grid": 0, "steps": [ {} ] })"), error).has_value());
}
