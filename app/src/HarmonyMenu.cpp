#include "HarmonyMenu.h"

#include <map>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

const std::map<std::string, const char*>& chordNames()
{
    static const std::map<std::string, const char*> names {
        { "maj", "장3화음" }, { "m", "단3화음" }, { "dim", "감3화음" }, { "aug", "증3화음" },
        { "sus2", "2도 걸림화음" }, { "sus4", "4도 걸림화음" }, { "b5", "장3화음, 5음 내림" }, { "5", "파워 코드 (근음, 5도, 옥타브)" },
        { "6", "장6화음" }, { "m6", "단6화음" }, { "6/9", "6/9 화음" }, { "m6/9", "단 6/9 화음" },
        { "maj7", "장7화음" }, { "7", "딸림7화음" }, { "m7", "단7화음" }, { "m(maj7)", "단장7화음" }, { "m7b5", "반감7화음" },
        { "dim7", "감7화음" }, { "7#5", "증7화음" }, { "maj7#5", "증장7화음" }, { "7b5", "딸림7화음, 5음 내림" },
        { "maj7b5", "장7화음, 5음 내림" }, { "dim(maj7)", "감장7화음" }, { "7sus4", "딸림7화음, 4도 걸림" },
        { "7sus2", "딸림7화음, 2도 걸림" }, { "maj7sus4", "장7화음, 4도 걸림" },
        { "add9", "장3화음 + 9" }, { "m(add9)", "단3화음 + 9" }, { "add11", "장3화음 + 11" }, { "m(add11)", "단3화음 + 11" },
        { "add#11", "장3화음 + #11" },
        { "9", "딸림9화음" }, { "maj9", "장9화음" }, { "m9", "단9화음" }, { "m(maj9)", "단장9화음" }, { "9sus4", "딸림9화음, 4도 걸림" },
        { "m9b5", "반감9화음" },
        { "11", "딸림11화음" }, { "maj11", "장11화음" }, { "m11", "단11화음" }, { "m11b5", "반감11화음" },
        { "7#11", "딸림7화음 + #11" }, { "9#11", "딸림9화음 + #11" }, { "maj7#11", "장7화음 + #11 (리디아)" }, { "maj9#11", "장9화음 + #11" },
        { "13", "딸림13화음" }, { "maj13", "장13화음" }, { "m13", "단13화음" }, { "13sus4", "딸림13화음, 4도 걸림" },
        { "13#11", "딸림13화음 + #11" }, { "maj13#11", "장13화음 + #11" },
        { "7b9", "딸림7화음 + b9" }, { "7#9", "딸림7화음 + #9" }, { "7b13", "딸림7화음 + b13" }, { "7b9b13", "딸림7화음 + b9, b13" },
        { "7#9b13", "딸림7화음 + #9, b13" }, { "7#5#9", "증7화음 + #9" }, { "7b5b9", "딸림7화음, 5음 내림 + b9" },
        { "7b5#9", "딸림7화음, 5음 내림 + #9" }, { "13b9", "딸림13화음 + b9" }, { "7alt", "변화 딸림화음 (3, b7, b9, #9, b13)" },
        { "It+6", "이탈리아 6화음" }, { "Fr+6", "프랑스 6화음" }, { "Ger+6", "독일 6화음" },
        { "quartal", "4도 쌓기 (세 음)" }, { "quartal4", "4도 쌓기 (네 음)" },
    };
    return names;
}

juce::String lookup (const std::map<std::string, const char*>& names, const std::string& id)
{
    const auto it = names.find (id);
    return it != names.end() ? u8 (it->second) : juce::String();
}

juce::String noteName (int pitch)
{
    return juce::String (mr::pitchClassName (pitch)) + juce::String (pitch / 12 - 1);
}

} // namespace

juce::String chordGroupLabel (const std::string& group)
{
    static const std::map<std::string, const char*> names {
        { "triad", "3화음" }, { "sixth", "6화음" }, { "seventh", "7화음" }, { "added", "부가음 화음" }, { "ninth", "9화음" },
        { "eleventh", "11화음" }, { "thirteenth", "13화음" }, { "altered", "변화 딸림화음" }, { "other", "증6화음과 4도 쌓기" },
    };
    return lookup (names, group);
}

juce::String scaleGroupLabel (const std::string& group) { return juce::String (mr::scaleGroupName (group)); }

juce::String chordLabel (const std::string& id) { return lookup (chordNames(), id); }
juce::String scaleLabel (const std::string& id)
{
    const auto* scale = mr::findScaleType (id);
    return scale != nullptr ? juce::String (scale->name) : juce::String();
}

juce::StringArray missingHarmonyLabels()
{
    juce::StringArray missing;
    for (const auto& g : mr::chordGroups())
        if (chordGroupLabel (g).isEmpty())
            missing.add ("chord group " + juce::String (g));
    for (const auto& g : mr::scaleGroups())
        if (scaleGroupLabel (g).isEmpty())
            missing.add ("scale group " + juce::String (g));
    for (const auto& c : mr::chordTypes())
        if (chordLabel (c.id).isEmpty())
            missing.add ("chord " + juce::String (c.id));
    for (const auto& s : mr::scaleTypes())
        if (scaleLabel (s.id).isEmpty())
            missing.add ("scale " + juce::String (s.id));
    return missing;
}

juce::PopupMenu chordMenu (int rootPitch, std::shared_ptr<HarmonyOptions> options, std::function<void (const mr::ChordType&)> insert)
{
    juce::PopupMenu menu;
    for (const auto& group : mr::chordGroups())
    {
        juce::PopupMenu sub;
        for (const auto& c : mr::chordTypes())
            if (c.group == group)
            {
                // 화음 기호, 이름, 실제 음 (자리바꿈을 따른다)
                juce::StringArray notes;
                for (const int p : mr::chordPitches (rootPitch, c, options->inversion))
                    notes.add (noteName (p));
                sub.addItem (juce::String::fromUTF8 (mr::chordSymbol (rootPitch, c).c_str()) + "    " + chordLabel (c.id) + "    "
                                 + notes.joinIntoString (" "),
                             [insert, c] { insert (c); });
            }
        menu.addSubMenu (chordGroupLabel (group), sub);
    }
    menu.addSeparator();
    juce::PopupMenu inversion;
    const char* inversions[] { "기본형", "첫째 자리바꿈 (3음이 아래)", "둘째 자리바꿈 (5음이 아래)", "셋째 자리바꿈 (7음이 아래)" };
    for (int i = 0; i < 4; ++i)
        inversion.addItem (u8 (inversions[i]), true, options->inversion == i, [options, i] { options->inversion = i; });
    menu.addSubMenu (u8 ("자리바꿈: ") + u8 (inversions[options->inversion]).upToFirstOccurrenceOf (" (", false, false), inversion);
    juce::PopupMenu length;
    using L = HarmonyOptions::ChordLength;
    const std::pair<L, const char*> lengths[] { { L::grid, "격자 한 칸" }, { L::beat, "한 박" }, { L::twoBeats, "두 박" }, { L::bar, "한 마디" } };
    juce::String current;
    for (const auto& [value, text] : lengths)
    {
        length.addItem (u8 (text), true, options->chordLength == value, [options, value = value] { options->chordLength = value; });
        if (options->chordLength == value)
            current = u8 (text);
    }
    menu.addSubMenu (u8 ("길이: ") + current, length);
    return menu;
}

juce::PopupMenu scaleMenu (int rootPitch, std::shared_ptr<HarmonyOptions> options, std::function<void (const mr::ScaleType&)> insert)
{
    juce::PopupMenu menu;
    const auto root = juce::String (mr::pitchClassName (rootPitch));
    for (const auto& group : mr::scaleGroups())
    {
        juce::PopupMenu sub;
        for (const auto& s : mr::scaleTypes())
            if (s.group == group)
            {
                juce::StringArray notes;
                for (const int i : s.intervals)
                    notes.add (juce::String (mr::pitchClassName (rootPitch + i)));
                sub.addItem (root + " " + scaleLabel (s.id) + "    " + notes.joinIntoString (" "), [insert, s] { insert (s); });
            }
        menu.addSubMenu (scaleGroupLabel (group), sub);
    }
    menu.addSeparator();
    juce::PopupMenu direction;
    const std::pair<mr::ScaleDirection, const char*> directions[] { { mr::ScaleDirection::up, "오르기" }, { mr::ScaleDirection::down, "내리기" },
                                                                    { mr::ScaleDirection::upDown, "오르고 내리기" } };
    juce::String current;
    for (const auto& [value, text] : directions)
    {
        direction.addItem (u8 (text), true, options->direction == value, [options, value = value] { options->direction = value; });
        if (options->direction == value)
            current = u8 (text);
    }
    menu.addSubMenu (u8 ("방향: ") + current, direction);
    juce::PopupMenu octaves;
    for (int n = 1; n <= 3; ++n)
        octaves.addItem (juce::String (n) + u8 (" 옥타브"), true, options->octaves == n, [options, n] { options->octaves = n; });
    menu.addSubMenu (u8 ("범위: ") + juce::String (options->octaves) + u8 (" 옥타브"), octaves);
    menu.addItem (u8 ("음 하나의 길이는 격자 한 칸입니다"), false, false, std::function<void()>());
    return menu;
}

juce::PopupMenu scaleChoiceMenu (int rootPitch, std::function<void (const mr::ScaleType&)> choose)
{
    juce::PopupMenu menu;
    const auto root = juce::String (mr::pitchClassName (rootPitch));
    for (const auto& group : mr::scaleGroups())
    {
        juce::PopupMenu sub;
        for (const auto& s : mr::scaleTypes())
            if (s.group == group)
            {
                juce::StringArray notes;
                for (const int i : s.intervals)
                    notes.add (juce::String (mr::pitchClassName (rootPitch + i)));
                sub.addItem (root + " " + scaleLabel (s.id) + "    " + notes.joinIntoString (" "), [choose, s] { choose (s); });
            }
        menu.addSubMenu (scaleGroupLabel (group), sub);
    }
    return menu;
}

juce::PopupMenu rootMenu (std::function<juce::PopupMenu (int rootPitch)> make)
{
    juce::PopupMenu menu;
    for (int pc = 0; pc < 12; ++pc)
        menu.addSubMenu (juce::String (mr::pitchClassName (pc)) + "4", make (60 + pc));
    return menu;
}

} // namespace daw
