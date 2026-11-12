#include "TestFramework.h"
#include "TestUtil.h"

#include "composition/Harmony.h"
#include "composition/NoteEdits.h"

using namespace mr;

namespace
{

std::vector<EditNote> make (const std::vector<std::tuple<int, double, double, int>>& list)
{
    std::vector<EditNote> out;
    for (const auto& [pitch, start, length, velocity] : list)
    {
        Note n;
        n.pitch = pitch;
        n.startBeat = start;
        n.durationBeat = length;
        n.velocity = velocity;
        out.push_back ({ n, {} });
    }
    return out;
}

} // namespace

TEST (note_tools_quantize_with_strength_swing_and_ends)
{
    const auto notes = make ({ { 60, 0.1, 0.4, 80 }, { 62, 0.6, 0.3, 80 }, { 64, 1.05, 0.5, 80 } });
    QuantizeOptions o;
    o.grid = 0.5;
    auto r = quantizeNotes (notes, {}, o);
    CHECK_NEAR (r.notes[0].note.startBeat, 0.0, 1.0e-9);
    CHECK_NEAR (r.notes[1].note.startBeat, 0.5, 1.0e-9);
    CHECK_NEAR (r.notes[0].note.startBeat + r.notes[0].note.durationBeat, 0.5, 1.0e-9);   // 끝은 그대로
    o.strength = 0.5;
    r = quantizeNotes (notes, { 0 }, o);
    CHECK_NEAR (r.notes[0].note.startBeat, 0.05, 1.0e-9);
    CHECK_NEAR (r.notes[1].note.startBeat, 0.6, 1.0e-9);   // 고르지 않은 음
    o.strength = 1.0;
    o.swing = 1.0;   // 둘째 8분음표가 셋잇단의 자리로 (0.5 + 0.5/3)
    r = quantizeNotes (notes, {}, o);
    CHECK_NEAR (r.notes[1].note.startBeat, 0.5 + 0.5 / 3.0, 1.0e-6);
    CHECK_NEAR (r.notes[2].note.startBeat, 1.0, 1.0e-9);
    o.swing = 0.0;
    o.ends = true;
    r = quantizeNotes (notes, {}, o);
    CHECK_NEAR (r.notes[1].note.durationBeat, 0.5, 1.0e-9);   // 0.5 에서 시작해 1.0 에서 끝 (0.9 를 맞춘다)
    CHECK_NEAR (r.notes[2].note.durationBeat, 0.5, 1.0e-9);
}

TEST (note_tools_velocities)
{
    const auto notes = make ({ { 60, 0, 1, 60 }, { 62, 1, 1, 80 }, { 64, 2, 1, 100 }, { 67, 2, 1, 40 } });
    auto r = scaleVelocities (notes, { 0, 1, 2 }, 0.5);   // 평균 80 을 가운데로 반
    CHECK_EQ (r.notes[0].note.velocity, 70);
    CHECK_EQ (r.notes[2].note.velocity, 90);
    CHECK_EQ (r.notes[3].note.velocity, 40);
    r = rampVelocities (notes, {}, 40, 120);
    CHECK_EQ (r.notes[0].note.velocity, 40);
    CHECK_EQ (r.notes[1].note.velocity, 80);
    CHECK_EQ (r.notes[2].note.velocity, 120);
    CHECK_EQ (r.notes[3].note.velocity, 120);   // 같은 시작은 같은 값
    const auto a = humanizeNotes (notes, {}, 8, 0.05, 7), b = humanizeNotes (notes, {}, 8, 0.05, 7), c = humanizeNotes (notes, {}, 8, 0.05, 8);
    bool same = true, differs = false, within = true;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        same = same && a.notes[i].note.velocity == b.notes[i].note.velocity && a.notes[i].note.startBeat == b.notes[i].note.startBeat;
        differs = differs || a.notes[i].note.velocity != c.notes[i].note.velocity;
        within = within && std::abs (a.notes[i].note.velocity - notes[i].note.velocity) <= 8
                 && std::abs (a.notes[i].note.startBeat - notes[i].note.startBeat) <= 0.05 + 1.0e-9;
    }
    CHECK (same && differs && within);
}

TEST (note_tools_lengths_scales_and_transformations)
{
    const auto notes = make ({ { 60, 0, 0.25, 80 }, { 64, 0, 0.25, 80 }, { 61, 1, 0.5, 80 }, { 66, 3, 0.5, 80 } });
    auto r = legatoNotes (notes, {});
    CHECK_NEAR (r.notes[0].note.durationBeat, 1.0, 1.0e-9);
    CHECK_NEAR (r.notes[1].note.durationBeat, 1.0, 1.0e-9);
    CHECK_NEAR (r.notes[2].note.durationBeat, 2.0, 1.0e-9);
    CHECK_NEAR (r.notes[3].note.durationBeat, 0.5, 1.0e-9);   // 마지막은 그대로
    r = setLengths (notes, { 2 }, 0.125);
    CHECK_NEAR (r.notes[2].note.durationBeat, 0.125, 1.0e-9);
    // C 장조: C# 은 C 로 (같은 거리면 아래), F# 은 F 로
    const auto* major = findScaleType ("major");
    REQUIRE (major != nullptr);
    r = snapToScale (notes, {}, 0, major->intervals);
    CHECK_EQ (r.notes[2].note.pitch, 60);
    CHECK_EQ (r.notes[3].note.pitch, 65);
    CHECK_EQ (r.notes[1].note.pitch, 64);
    // 역행: [0, 3.5) 안에서 거꾸로 (마지막 음이 처음에)
    r = retrogradeNotes (notes, {});
    CHECK_NEAR (r.notes[3].note.startBeat, 0.0, 1.0e-9);
    CHECK_NEAR (r.notes[2].note.startBeat, 2.0, 1.0e-9);
    CHECK_NEAR (r.notes[0].note.startBeat, 3.25, 1.0e-9);
    // 반행: 가장 이른 낮은 음(C4)을 축으로
    r = invertNotes (notes, {});
    CHECK_EQ (r.notes[1].note.pitch, 56);
    CHECK_EQ (r.notes[2].note.pitch, 59);
    CHECK_EQ (r.notes[0].note.pitch, 60);
}

TEST (note_tools_arpeggio_and_strum)
{
    // C E G 화음 (한 박) 과 홑음
    const auto notes = make ({ { 64, 0, 1, 70 }, { 60, 0, 1, 90 }, { 67, 0, 1, 80 }, { 72, 2, 1, 100 } });
    auto pitches = [] (const NoteEditResult& r, double from, double to)
    {
        std::vector<int> out;
        for (const auto& e : r.notes)
            if (e.note.startBeat >= from - 1.0e-9 && e.note.startBeat < to - 1.0e-9)
                out.push_back (e.note.pitch);
        return out;
    };
    auto r = arpeggiateNotes (notes, {}, ArpeggioOrder::up, 0.25);
    CHECK (pitches (r, 0, 1) == std::vector<int> ({ 60, 64, 67, 60 }));
    CHECK_EQ (r.notes.size(), (size_t) 5);
    CHECK_NEAR (r.notes[1].note.startBeat, 0.25, 1.0e-9);
    CHECK_NEAR (r.notes[3].note.durationBeat, 0.25, 1.0e-9);
    CHECK_EQ (r.notes[0].note.velocity, 90);   // C 의 세기
    CHECK_EQ (r.notes[1].note.velocity, 70);
    CHECK_EQ (r.notes.back().note.pitch, 72);  // 홑음
    CHECK_EQ (r.selection.size(), (size_t) 5);
    r = arpeggiateNotes (notes, {}, ArpeggioOrder::down, 0.25);
    CHECK (pitches (r, 0, 1) == std::vector<int> ({ 67, 64, 60, 67 }));
    r = arpeggiateNotes (notes, {}, ArpeggioOrder::upDown, 1.0 / 6.0);
    CHECK (pitches (r, 0, 1) == std::vector<int> ({ 60, 64, 67, 64, 60, 64 }));
    // 셋잇단 8분 (1/3) 은 박 안에 셋
    r = arpeggiateNotes (notes, { 0, 1, 2 }, ArpeggioOrder::up, 1.0 / 3.0);
    CHECK (pitches (r, 0, 1) == std::vector<int> ({ 60, 64, 67 }));
    CHECK_NEAR (r.notes[2].note.startBeat + r.notes[2].note.durationBeat, 1.0, 1.0e-5);

    r = strumNotes (notes, {}, 0.05, true);
    CHECK_NEAR (r.notes[1].note.startBeat, 0.0, 1.0e-9);    // C
    CHECK_NEAR (r.notes[0].note.startBeat, 0.05, 1.0e-9);   // E
    CHECK_NEAR (r.notes[2].note.startBeat, 0.1, 1.0e-9);    // G
    CHECK_NEAR (r.notes[2].note.startBeat + r.notes[2].note.durationBeat, 1.0, 1.0e-9);
    CHECK_NEAR (r.notes[3].note.startBeat, 2.0, 1.0e-9);
    r = strumNotes (notes, {}, 0.05, false);
    CHECK_NEAR (r.notes[2].note.startBeat, 0.0, 1.0e-9);
    CHECK_NEAR (r.notes[1].note.startBeat, 0.1, 1.0e-9);
}
