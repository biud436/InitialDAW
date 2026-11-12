// 화음과 음계 표: 근음에서의 반음 간격. 앱의 코드, 스케일 넣기 메뉴와 명령행(chords, scales)이 같은 표를 쓴다.
#pragma once

#include "composition/Composition.h"

#include <string>
#include <vector>

namespace mr
{

struct ChordType
{
    std::string id;            // "m7"
    std::string suffix;        // 코드 기호에서 근음 뒤에 붙는 글자 ("m7")
    std::string group;         // triad, sixth, seventh, added, ninth, eleventh, thirteenth, altered, other
    std::string name;          // 영어 이름
    std::vector<int> intervals;   // 근음에서 오름차순 (0 으로 시작)
};

struct ScaleType
{
    std::string id;
    std::string group;         // diatonic, minor, melodic-minor-modes, harmonic-minor-modes, pentatonic, hexatonic, symmetric, bebop, exotic
    std::string name;
    std::vector<int> intervals;   // 한 옥타브 안, 0 으로 시작하는 오름차순
};

const std::vector<ChordType>& chordTypes();
const std::vector<ScaleType>& scaleTypes();
const std::vector<std::string>& chordGroups();   // 메뉴 순서
const std::vector<std::string>& scaleGroups();
std::string scaleGroupName (const std::string& group);   // 영어 이름 ("Melodic minor modes"), 모르면 비어 있다
const ChordType* findChordType (const std::string& id);
const ScaleType* findScaleType (const std::string& id);

// C Db D Eb E F F# G Ab A Bb B
std::string pitchClassName (int pitch);
// "Em7", "Bbmaj7", "Ab It+6"
std::string chordSymbol (int rootPitch, const ChordType& chord);

// 근음(MIDI)에서 위로 쌓는다. inversion 만큼 아래 음을 한 옥타브 올린다 (자리바꿈). 0~127 밖의 음은 뺀다.
std::vector<int> chordPitches (int rootPitch, const ChordType& chord, int inversion = 0);

enum class ScaleDirection { up, down, upDown };

// 근음에서 octaves 옥타브. 오르기는 근음에서 시작해 맨 위 근음에서, 내리기는 맨 위 근음에서 시작해 근음에서 끝난다.
// 오르고 내리기는 맨 위 음을 한 번만 친다. 0~127 밖의 음은 뺀다.
std::vector<int> scalePitches (int rootPitch, const ScaleType& scale, int octaves, ScaleDirection direction);

// 메뉴가 넣는 음표: 화음은 모두 startBeat 에서 lengthBeats 동안, 음계는 stepBeats 마다 하나씩
std::vector<Note> chordNotes (int rootPitch, const ChordType& chord, int inversion, double startBeat, double lengthBeats, int velocity);
std::vector<Note> scaleNotes (int rootPitch, const ScaleType& scale, int octaves, ScaleDirection direction, double startBeat,
                              double stepBeats, int velocity);

} // namespace mr
