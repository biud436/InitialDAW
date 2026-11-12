// 악보 보기: 고른 트랙을 엔진의 악보 모델(buildScore)로 옮겨 오선에 그린다 (Bravura 글꼴의 SMuFL 기호).
// 마디를 줄 바꿈하며 늘어놓고, 큰보표는 두 보표의 같은 자리를 맞춘다. 마디를 누르면 그 마디로 옮긴다. 읽기만 한다.
#pragma once

#include "Project.h"
#include "TimelineDrawing.h"
#include "notation/Notation.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <map>
#include <optional>

namespace daw
{

class ScorePanel final : public juce::Component,
                         private TimelineState::Listener,
                         private Project::Listener,
                         private juce::ScrollBar::Listener
{
public:
    static constexpr int scrollBarSize = 10;

    ScorePanel (Project& project, TimelineState& timeline);
    ~ScorePanel() override;

    std::function<bool (const mr::TrackSpec&)> percussion;   // 건반 이름이 있는 악기 (타악기 보표)
    std::function<void (double beat)> onSeek;

    static bool hasMusicFont();
    void refresh();                                // 곡이나 고른 트랙이 바뀌었다
    const mr::Score& score() const noexcept { return score_; }
    int systemCount() const noexcept { return (int) systems_.size(); }
    float staffSpace() const noexcept { return sp_; }
    void setStaffSpace (float space);
    std::optional<int> measureAt (juce::Point<float> p) const;   // 그 자리의 마디 번호 (0부터)
    juce::Point<float> measurePoint (int measure) const;          // 마디의 가운데 (시험)

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    struct MeasureLayout
    {
        size_t index = 0;
        float x = 0.0f, width = 0.0f;
        float contentX = 0.0f;            // 첫 음표의 자리
        bool showKey = false, showTime = false;
        std::map<int, float> xAt;         // 마디 안의 자리(divisions) -> x
    };
    struct SystemLayout
    {
        float top = 0.0f;                 // 첫 보표의 맨 윗줄
        float headerWidth = 0.0f;
        std::vector<MeasureLayout> measures;
    };

    void timelineChanged() override;
    void projectEdited (int track) override;
    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override;

    void layout();
    float staffTop (const SystemLayout& s, int staff) const;   // 보표의 맨 윗줄
    float systemHeight() const;
    float positionY (float staffTop, int position) const;      // 아래 줄에서 반 칸 단위의 자리
    int positionOf (const mr::NotatedPitch& p, mr::Clef clef) const;
    float xAt (const MeasureLayout& m, int division) const;

    void paintSystem (juce::Graphics& g, const SystemLayout& s, size_t systemIndex);
    void paintHeader (juce::Graphics& g, const SystemLayout& s, float x, size_t measure, bool clefs, bool key, bool time);
    void paintStaffEvents (juce::Graphics& g, const SystemLayout& s, const MeasureLayout& m, int staff);
    void glyph (juce::Graphics& g, juce::juce_wchar code, float x, float y, float scale = 1.0f) const;

    Project& project_;
    TimelineState& timeline_;
    mr::Score score_;
    int track_ = -1;
    float sp_ = 8.0f;                     // 오선 칸 (픽셀)
    std::vector<SystemLayout> systems_;
    float scroll_ = 0.0f;
    juce::ScrollBar vertical_ { true };
    juce::Font music_;
    int playheadSystem_ = -1;
};

} // namespace daw
