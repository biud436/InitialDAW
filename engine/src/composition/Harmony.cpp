#include "composition/Harmony.h"

#include <algorithm>

namespace mr
{

const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> list {
        // 3화음
        { "maj", "", "triad", "Major", { 0, 4, 7 } },
        { "m", "m", "triad", "Minor", { 0, 3, 7 } },
        { "dim", "dim", "triad", "Diminished", { 0, 3, 6 } },
        { "aug", "aug", "triad", "Augmented", { 0, 4, 8 } },
        { "sus2", "sus2", "triad", "Suspended second", { 0, 2, 7 } },
        { "sus4", "sus4", "triad", "Suspended fourth", { 0, 5, 7 } },
        { "b5", "(b5)", "triad", "Major flat five", { 0, 4, 6 } },
        { "5", "5", "triad", "Power chord (root, fifth, octave)", { 0, 7, 12 } },
        // 6화음
        { "6", "6", "sixth", "Major sixth", { 0, 4, 7, 9 } },
        { "m6", "m6", "sixth", "Minor sixth", { 0, 3, 7, 9 } },
        { "6/9", "6/9", "sixth", "Six nine", { 0, 4, 7, 9, 14 } },
        { "m6/9", "m6/9", "sixth", "Minor six nine", { 0, 3, 7, 9, 14 } },
        // 7화음
        { "maj7", "maj7", "seventh", "Major seventh", { 0, 4, 7, 11 } },
        { "7", "7", "seventh", "Dominant seventh", { 0, 4, 7, 10 } },
        { "m7", "m7", "seventh", "Minor seventh", { 0, 3, 7, 10 } },
        { "m(maj7)", "m(maj7)", "seventh", "Minor major seventh", { 0, 3, 7, 11 } },
        { "m7b5", "m7b5", "seventh", "Half-diminished seventh", { 0, 3, 6, 10 } },
        { "dim7", "dim7", "seventh", "Diminished seventh", { 0, 3, 6, 9 } },
        { "7#5", "7#5", "seventh", "Augmented seventh", { 0, 4, 8, 10 } },
        { "maj7#5", "maj7#5", "seventh", "Augmented major seventh", { 0, 4, 8, 11 } },
        { "7b5", "7b5", "seventh", "Dominant seventh flat five", { 0, 4, 6, 10 } },
        { "maj7b5", "maj7b5", "seventh", "Major seventh flat five", { 0, 4, 6, 11 } },
        { "dim(maj7)", "dim(maj7)", "seventh", "Diminished major seventh", { 0, 3, 6, 11 } },
        { "7sus4", "7sus4", "seventh", "Dominant seventh suspended fourth", { 0, 5, 7, 10 } },
        { "7sus2", "7sus2", "seventh", "Dominant seventh suspended second", { 0, 2, 7, 10 } },
        { "maj7sus4", "maj7sus4", "seventh", "Major seventh suspended fourth", { 0, 5, 7, 11 } },
        // 부가음 화음
        { "add9", "add9", "added", "Added ninth", { 0, 4, 7, 14 } },
        { "m(add9)", "m(add9)", "added", "Minor added ninth", { 0, 3, 7, 14 } },
        { "add11", "add11", "added", "Added eleventh", { 0, 4, 7, 17 } },
        { "m(add11)", "m(add11)", "added", "Minor added eleventh", { 0, 3, 7, 17 } },
        { "add#11", "add#11", "added", "Added sharp eleventh", { 0, 4, 7, 18 } },
        // 9화음
        { "9", "9", "ninth", "Dominant ninth", { 0, 4, 7, 10, 14 } },
        { "maj9", "maj9", "ninth", "Major ninth", { 0, 4, 7, 11, 14 } },
        { "m9", "m9", "ninth", "Minor ninth", { 0, 3, 7, 10, 14 } },
        { "m(maj9)", "m(maj9)", "ninth", "Minor major ninth", { 0, 3, 7, 11, 14 } },
        { "9sus4", "9sus4", "ninth", "Dominant ninth suspended fourth", { 0, 5, 7, 10, 14 } },
        { "m9b5", "m9b5", "ninth", "Half-diminished ninth", { 0, 3, 6, 10, 14 } },
        // 11화음
        { "11", "11", "eleventh", "Dominant eleventh", { 0, 4, 7, 10, 14, 17 } },
        { "maj11", "maj11", "eleventh", "Major eleventh", { 0, 4, 7, 11, 14, 17 } },
        { "m11", "m11", "eleventh", "Minor eleventh", { 0, 3, 7, 10, 14, 17 } },
        { "m11b5", "m11b5", "eleventh", "Half-diminished eleventh", { 0, 3, 6, 10, 14, 17 } },
        { "7#11", "7#11", "eleventh", "Dominant seventh sharp eleventh", { 0, 4, 7, 10, 18 } },
        { "9#11", "9#11", "eleventh", "Dominant ninth sharp eleventh", { 0, 4, 7, 10, 14, 18 } },
        { "maj7#11", "maj7#11", "eleventh", "Major seventh sharp eleventh", { 0, 4, 7, 11, 18 } },
        { "maj9#11", "maj9#11", "eleventh", "Major ninth sharp eleventh", { 0, 4, 7, 11, 14, 18 } },
        // 13화음 (11 을 뺀 쌓기, 단화음은 11 을 둔다)
        { "13", "13", "thirteenth", "Dominant thirteenth", { 0, 4, 7, 10, 14, 21 } },
        { "maj13", "maj13", "thirteenth", "Major thirteenth", { 0, 4, 7, 11, 14, 21 } },
        { "m13", "m13", "thirteenth", "Minor thirteenth", { 0, 3, 7, 10, 14, 17, 21 } },
        { "13sus4", "13sus4", "thirteenth", "Dominant thirteenth suspended fourth", { 0, 5, 7, 10, 14, 21 } },
        { "13#11", "13#11", "thirteenth", "Dominant thirteenth sharp eleventh", { 0, 4, 7, 10, 14, 18, 21 } },
        { "maj13#11", "maj13#11", "thirteenth", "Major thirteenth sharp eleventh", { 0, 4, 7, 11, 14, 18, 21 } },
        // 변화 딸림화음
        { "7b9", "7b9", "altered", "Dominant seventh flat nine", { 0, 4, 7, 10, 13 } },
        { "7#9", "7#9", "altered", "Dominant seventh sharp nine", { 0, 4, 7, 10, 15 } },
        { "7b13", "7b13", "altered", "Dominant seventh flat thirteen", { 0, 4, 7, 10, 20 } },
        { "7b9b13", "7b9b13", "altered", "Dominant seventh flat nine flat thirteen", { 0, 4, 7, 10, 13, 20 } },
        { "7#9b13", "7#9b13", "altered", "Dominant seventh sharp nine flat thirteen", { 0, 4, 7, 10, 15, 20 } },
        { "7#5#9", "7#5#9", "altered", "Augmented seventh sharp nine", { 0, 4, 8, 10, 15 } },
        { "7b5b9", "7b5b9", "altered", "Dominant seventh flat five flat nine", { 0, 4, 6, 10, 13 } },
        { "7b5#9", "7b5#9", "altered", "Dominant seventh flat five sharp nine", { 0, 4, 6, 10, 15 } },
        { "13b9", "13b9", "altered", "Dominant thirteenth flat nine", { 0, 4, 7, 10, 13, 21 } },
        { "7alt", "7alt", "altered", "Altered dominant (third, seventh, flat and sharp nine, flat thirteen)", { 0, 4, 10, 13, 15, 20 } },
        // 증6화음 (근음은 베이스의 b6), 4도 쌓기
        { "It+6", "It+6", "other", "Italian augmented sixth", { 0, 4, 10 } },
        { "Fr+6", "Fr+6", "other", "French augmented sixth", { 0, 4, 6, 10 } },
        { "Ger+6", "Ger+6", "other", "German augmented sixth", { 0, 4, 7, 10 } },
        { "quartal", "quartal", "other", "Quartal (three notes in fourths)", { 0, 5, 10 } },
        { "quartal4", "quartal4", "other", "Quartal (four notes in fourths)", { 0, 5, 10, 15 } },
    };
    return list;
}

const std::vector<ScaleType>& scaleTypes()
{
    static const std::vector<ScaleType> list {
        // 장음계와 교회 선법
        { "major", "diatonic", "Major (Ionian)", { 0, 2, 4, 5, 7, 9, 11 } },
        { "dorian", "diatonic", "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "phrygian", "diatonic", "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "lydian", "diatonic", "Lydian", { 0, 2, 4, 6, 7, 9, 11 } },
        { "mixolydian", "diatonic", "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
        { "minor", "diatonic", "Natural minor (Aeolian)", { 0, 2, 3, 5, 7, 8, 10 } },
        { "locrian", "diatonic", "Locrian", { 0, 1, 3, 5, 6, 8, 10 } },
        // 단음계와 화성 장음계
        { "harmonic-minor", "minor", "Harmonic minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "melodic-minor", "minor", "Melodic minor (ascending)", { 0, 2, 3, 5, 7, 9, 11 } },
        { "harmonic-major", "minor", "Harmonic major", { 0, 2, 4, 5, 7, 8, 11 } },
        // 가락 단음계의 선법
        { "dorian-b2", "melodic-minor-modes", "Dorian flat two", { 0, 1, 3, 5, 7, 9, 10 } },
        { "lydian-augmented", "melodic-minor-modes", "Lydian augmented", { 0, 2, 4, 6, 8, 9, 11 } },
        { "lydian-dominant", "melodic-minor-modes", "Lydian dominant", { 0, 2, 4, 6, 7, 9, 10 } },
        { "mixolydian-b6", "melodic-minor-modes", "Mixolydian flat six", { 0, 2, 4, 5, 7, 8, 10 } },
        { "locrian-n2", "melodic-minor-modes", "Locrian natural two (half-diminished)", { 0, 2, 3, 5, 6, 8, 10 } },
        { "altered", "melodic-minor-modes", "Altered (super Locrian)", { 0, 1, 3, 4, 6, 8, 10 } },
        // 화성 단음계의 선법
        { "locrian-n6", "harmonic-minor-modes", "Locrian natural six", { 0, 1, 3, 5, 6, 9, 10 } },
        { "ionian-#5", "harmonic-minor-modes", "Ionian sharp five", { 0, 2, 4, 5, 8, 9, 11 } },
        { "dorian-#4", "harmonic-minor-modes", "Dorian sharp four", { 0, 2, 3, 6, 7, 9, 10 } },
        { "phrygian-dominant", "harmonic-minor-modes", "Phrygian dominant", { 0, 1, 4, 5, 7, 8, 10 } },
        { "lydian-#2", "harmonic-minor-modes", "Lydian sharp two", { 0, 3, 4, 6, 7, 9, 11 } },
        { "ultralocrian", "harmonic-minor-modes", "Ultralocrian", { 0, 1, 3, 4, 6, 8, 9 } },
        // 5음 음계
        { "major-pentatonic", "pentatonic", "Major pentatonic", { 0, 2, 4, 7, 9 } },
        { "minor-pentatonic", "pentatonic", "Minor pentatonic", { 0, 3, 5, 7, 10 } },
        { "suspended-pentatonic", "pentatonic", "Suspended pentatonic", { 0, 2, 5, 7, 10 } },
        { "man-gong", "pentatonic", "Man gong (blues minor pentatonic)", { 0, 3, 5, 8, 10 } },
        { "ritusen", "pentatonic", "Ritusen (yo, blues major pentatonic)", { 0, 2, 5, 7, 9 } },
        { "pyeongjo", "pentatonic", "Pyeongjo (Korean)", { 0, 2, 5, 7, 9 } },
        { "gyemyeonjo", "pentatonic", "Gyemyeonjo (Korean)", { 0, 3, 5, 7, 10 } },
        { "hirajoshi", "pentatonic", "Hirajoshi", { 0, 2, 3, 7, 8 } },
        { "in-sen", "pentatonic", "In-sen", { 0, 1, 5, 7, 10 } },
        { "iwato", "pentatonic", "Iwato", { 0, 1, 5, 6, 10 } },
        { "kumoi", "pentatonic", "Kumoi", { 0, 2, 3, 7, 9 } },
        // 블루스
        { "blues", "blues", "Blues", { 0, 3, 5, 6, 7, 10 } },
        { "major-blues", "blues", "Major blues", { 0, 2, 3, 4, 7, 9 } },
        // 대칭 음계
        { "whole-tone", "symmetric", "Whole tone", { 0, 2, 4, 6, 8, 10 } },
        { "diminished-half-whole", "symmetric", "Diminished (half, whole)", { 0, 1, 3, 4, 6, 7, 9, 10 } },
        { "diminished-whole-half", "symmetric", "Diminished (whole, half)", { 0, 2, 3, 5, 6, 8, 9, 11 } },
        { "augmented", "symmetric", "Augmented (minor third, half)", { 0, 3, 4, 7, 8, 11 } },
        { "tritone", "symmetric", "Tritone", { 0, 1, 4, 6, 7, 10 } },
        { "chromatic", "symmetric", "Chromatic", { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        // 비밥
        { "bebop-dominant", "bebop", "Bebop dominant", { 0, 2, 4, 5, 7, 9, 10, 11 } },
        { "bebop-major", "bebop", "Bebop major", { 0, 2, 4, 5, 7, 8, 9, 11 } },
        { "bebop-dorian", "bebop", "Bebop Dorian", { 0, 2, 3, 4, 5, 7, 9, 10 } },
        { "bebop-melodic-minor", "bebop", "Bebop melodic minor", { 0, 2, 3, 5, 7, 8, 9, 11 } },
        // 그 밖의 음계
        { "double-harmonic", "exotic", "Double harmonic (Byzantine)", { 0, 1, 4, 5, 7, 8, 11 } },
        { "hungarian-minor", "exotic", "Hungarian minor", { 0, 2, 3, 6, 7, 8, 11 } },
        { "hungarian-major", "exotic", "Hungarian major", { 0, 3, 4, 6, 7, 9, 10 } },
        { "neapolitan-major", "exotic", "Neapolitan major", { 0, 1, 3, 5, 7, 9, 11 } },
        { "neapolitan-minor", "exotic", "Neapolitan minor", { 0, 1, 3, 5, 7, 8, 11 } },
        { "persian", "exotic", "Persian", { 0, 1, 4, 5, 6, 8, 11 } },
        { "arabian", "exotic", "Arabian (major Locrian)", { 0, 2, 4, 5, 6, 8, 10 } },
        { "enigmatic", "exotic", "Enigmatic", { 0, 1, 4, 6, 8, 10, 11 } },
        { "spanish-eight", "exotic", "Spanish eight-tone", { 0, 1, 3, 4, 5, 6, 8, 10 } },
    };
    return list;
}

const std::vector<std::string>& chordGroups()
{
    static const std::vector<std::string> list { "triad", "sixth", "seventh", "added", "ninth", "eleventh", "thirteenth", "altered", "other" };
    return list;
}

const std::vector<std::string>& scaleGroups()
{
    static const std::vector<std::string> list { "diatonic", "minor", "melodic-minor-modes", "harmonic-minor-modes", "pentatonic",
                                                 "blues", "symmetric", "bebop", "exotic" };
    return list;
}

std::string scaleGroupName (const std::string& group)
{
    static const std::vector<std::pair<std::string, std::string>> names {
        { "diatonic", "Major and church modes" }, { "minor", "Minor and harmonic major" }, { "melodic-minor-modes", "Melodic minor modes" },
        { "harmonic-minor-modes", "Harmonic minor modes" }, { "pentatonic", "Pentatonic" }, { "blues", "Blues" },
        { "symmetric", "Symmetric" }, { "bebop", "Bebop" }, { "exotic", "Other scales" },
    };
    for (const auto& [id, name] : names)
        if (id == group)
            return name;
    return {};
}

const ChordType* findChordType (const std::string& id)
{
    for (const auto& c : chordTypes())
        if (c.id == id)
            return &c;
    return nullptr;
}

const ScaleType* findScaleType (const std::string& id)
{
    for (const auto& s : scaleTypes())
        if (s.id == id)
            return &s;
    return nullptr;
}

std::string pitchClassName (int pitch)
{
    static const char* names[] { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pitch % 12) + 12) % 12];
}

std::string chordSymbol (int rootPitch, const ChordType& chord)
{
    const bool spaced = chord.group == "other";   // "Ab It+6"
    return pitchClassName (rootPitch) + (spaced ? " " : "") + chord.suffix;
}

std::vector<int> chordPitches (int rootPitch, const ChordType& chord, int inversion)
{
    std::vector<int> pitches;
    for (const int i : chord.intervals)
        pitches.push_back (rootPitch + i);
    const int turns = std::clamp (inversion, 0, (int) pitches.size() - 1);
    for (int k = 0; k < turns; ++k)
    {
        pitches.front() += 12;
        std::sort (pitches.begin(), pitches.end());
    }
    pitches.erase (std::remove_if (pitches.begin(), pitches.end(), [] (int p) { return p < 0 || p > 127; }), pitches.end());
    pitches.erase (std::unique (pitches.begin(), pitches.end()), pitches.end());
    return pitches;
}

std::vector<int> scalePitches (int rootPitch, const ScaleType& scale, int octaves, ScaleDirection direction)
{
    std::vector<int> up;
    const int n = std::max (1, octaves);
    for (int o = 0; o < n; ++o)
        for (const int i : scale.intervals)
            up.push_back (rootPitch + 12 * o + i);
    up.push_back (rootPitch + 12 * n);
    std::vector<int> pitches;
    if (direction == ScaleDirection::up)
        pitches = up;
    else if (direction == ScaleDirection::down)
        pitches.assign (up.rbegin(), up.rend());
    else
    {
        pitches = up;
        pitches.insert (pitches.end(), up.rbegin() + 1, up.rend());
    }
    pitches.erase (std::remove_if (pitches.begin(), pitches.end(), [] (int p) { return p < 0 || p > 127; }), pitches.end());
    return pitches;
}

std::vector<Note> chordNotes (int rootPitch, const ChordType& chord, int inversion, double startBeat, double lengthBeats, int velocity)
{
    std::vector<Note> notes;
    for (const int p : chordPitches (rootPitch, chord, inversion))
    {
        Note n;
        n.pitch = p;
        n.startBeat = startBeat;
        n.durationBeat = lengthBeats;
        n.velocity = velocity;
        notes.push_back (n);
    }
    return notes;
}

std::vector<Note> scaleNotes (int rootPitch, const ScaleType& scale, int octaves, ScaleDirection direction, double startBeat,
                              double stepBeats, int velocity)
{
    std::vector<Note> notes;
    double beat = startBeat;
    for (const int p : scalePitches (rootPitch, scale, octaves, direction))
    {
        Note n;
        n.pitch = p;
        n.startBeat = beat;
        n.durationBeat = stepBeats;
        n.velocity = velocity;
        notes.push_back (n);
        beat += stepBeats;
    }
    return notes;
}

} // namespace mr
