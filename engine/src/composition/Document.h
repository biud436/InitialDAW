// 편집하는 Composition JSON. 읽은 JSON 트리를 그대로 들고 있어 저장할 때 모르는 필드, 주석, 키 순서가 남는다.
// 편집은 트리의 일부(트랙, 템포, 박자)를 새 값으로 바꾸고 다시 파싱해 확인한다.
#pragma once

#include "composition/Composition.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>
#include <vector>

namespace mr
{

class CompositionDocument
{
public:
    // JSON 이 아니거나 최상위가 객체가 아니면 nullopt 와 오류 문장
    static std::optional<CompositionDocument> fromText (const juce::String& text, const juce::File& file, std::string& error);
    static std::optional<CompositionDocument> load (const juce::File& file, std::string& error);

    ParseResult parse() const;
    std::string toJson() const;
    // 같은 폴더의 임시 파일에 쓰고 바꿔 끼운다. 성공하면 file() 이 바뀐다.
    bool save (const juce::File& file, std::string& error);

    const juce::var& root() const noexcept { return root_; }
    const juce::File& file() const noexcept { return file_; }

    size_t trackCount() const;
    juce::var track (size_t index) const;                 // 깊은 사본
    void setTrack (size_t index, const juce::var& track);
    juce::var property (const juce::Identifier& name) const;   // 깊은 사본, 없으면 void
    void setProperty (const juce::Identifier& name, const juce::var& value);

private:
    CompositionDocument (juce::var root, juce::File file) : root_ (std::move (root)), file_ (std::move (file)) {}

    juce::var root_;
    juce::File file_;
};

ParseResult parseCompositionValue (const juce::var& root, const juce::File& sourceFile);

// 저장 형식: 들여쓰기 한 칸, 값이 스칼라뿐인 객체와 배열은 한 줄. 숫자는 되읽어 같은 값이 되는 가장 짧은 표기.
std::string writeJson (const juce::var& value);

// 편집 중인 음: 파싱한 값과 원래 JSON 객체 (새 음은 void). 쓸 때 원래 객체의 다른 필드를 지킨다.
struct EditNote
{
    Note note;
    juce::var source;
};

// trackJson 의 notes[i] 와 parsed.notes[i] 를 짝짓는다 (같은 순서로 파싱한다)
std::vector<EditNote> editNotes (const juce::var& trackJson, const TrackSpec& parsed);
// 음을 바꾼 트랙 JSON (깊은 사본). 높이는 원래 형식(이름 또는 번호)을 따른다.
juce::var withNotes (const juce::var& trackJson, const std::vector<EditNote>& notes);
// 컨트롤러 곡선을 바꾼 트랙 JSON. 곡선이 있던 자리("automation" 안 또는 트랙 바로 아래)에 쓰고, 빈 곡선은 지운다.
juce::var withLane (const juce::var& trackJson, int controller, const AutomationLane& lane);
// 곡선의 JSON (점의 배열, 빈 곡선은 void)
juce::var laneJson (const AutomationLane& lane);
// 믹서 설정을 바꾼 객체 (트랙, 버스, 마스터의 깊은 사본, owner 가 비면 새 객체). 원래 있던 필드는 새 값으로 바꾸고,
// 없던 필드는 기본값(0, false)과 다를 때만 더한다. mixer 객체의 다른 필드는 남는다.
juce::var withMixer (const juce::var& owner, const MixerSettings& mixer);
// 센드를 바꾼 트랙. i 번째 센드는 원래 i 번째 객체에 쓴다 (주석 같은 다른 필드가 남는다). preFader 는 참이거나 원래 있을 때만 쓴다.
// 크기 곡선(automation.levelDb)은 SendSpec 의 levelLane 을 쓴다. 센드가 없어지면 sends 키를 지운다.
juce::var withSends (const juce::var& track, const std::vector<SendSpec>& sends);
// 효과 목록을 바꾼 객체. 목록이 비었고 원래 키가 없었으면 더하지 않는다.
juce::var withEffects (const juce::var& owner, const juce::Array<juce::var>& effects);
// 매개변수 하나를 바꾼 효과 객체 (깊은 사본). value 가 void 면 그 키를 지운다.
juce::var withEffectParameter (const juce::var& effect, const juce::Identifier& key, const juce::var& value);
juce::var effectsJson (const juce::var& owner);   // owner 의 effects 배열의 깊은 사본 (없으면 빈 배열)
// 플러그인 상태를 쓴 트랙. 상태가 모든 값을 담으므로 그 트랙의 plugin.program 과 plugin.parameters 는 지운다.
juce::var withPluginState (const juce::var& track, const std::string& stateBase64);
// 악기를 바꾼 트랙. 앞 악기의 플러그인 설정(plugin)과 신스 값(synth)은 지운다.
juce::var withInstrument (const juce::var& track, const std::string& instrument);
// 내장 신스 값 하나를 바꾼 트랙 (void 면 그 키를 지워 기본값으로, 비면 synth 를 지운다)
juce::var withSynthValue (const juce::var& track, const std::string& key, const juce::var& value);
// 곡의 instruments 값에 곡 안의 정의를 더한다 (첫 객체에 넣고, 없으면 객체를 더한다). 파일 경로 항목은 그대로 둔다.
juce::var withInlineInstrument (const juce::var& instruments, const std::string& name, const juce::var& definition);

// 오디오 클립의 JSON. original 이 있으면 그 객체의 다른 필드(주석)를 지키고, 0 인 선택 필드(offsetSeconds, gainDb, 페이드)는 원래 없었으면 쓰지 않는다
juce::var clipJson (const AudioClip& clip, const juce::var& original = {});
// 트랙의 clips 를 바꾼 사본: index 자리를 바꾸기, 더하기(맨 뒤), 지우기
juce::var withClip (const juce::var& track, size_t index, const AudioClip& clip);
juce::var withClipAdded (const juce::var& track, const AudioClip& clip);
juce::var withClipRemoved (const juce::var& track, size_t index);

juce::var tempoJson (const std::vector<TempoPoint>& tempo);   // 점 하나에 램프가 없으면 숫자 하나
juce::var timeSignatureJson (int beatsPerBar, int beatUnit);

// C4 = 60. flats 면 Bb4, 아니면 A#4.
std::string noteName (int pitch, bool flats = false);

} // namespace mr
