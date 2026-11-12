// 실시간 입력(MIDI 장치, 자판)을 박 단위로 받아 음표와 컨트롤러 곡선으로 만든다. 스레드를 갖지 않는다 (앱이 메시지 스레드에서 부른다).
// 음 켜기와 끄기는 박으로 받고, 끝낼 때 격자 맞추기와 펀치를 적용한다.
#pragma once

#include "composition/Composition.h"

#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace mr
{

struct RecordOptions
{
    std::optional<double> quantizeGrid;               // 음의 시작을 이 격자에 맞춘다 (길이는 그대로)
    std::optional<std::pair<double, double>> punch;   // 이 박 구간 [시작, 끝) 에서 시작한 음만 남긴다
    std::optional<double> loopEndBeat;                // 반복 재생 중 되돌아간 뒤에 뗀 음은 여기서 끝난다
    bool controllers = true;                          // 컨트롤러와 피치 벤드를 곡선 점으로 남긴다
    int channel = 1;
};

struct RecordedTake
{
    std::vector<Note> notes;                  // 시작 순서
    std::map<int, AutomationLane> lanes;      // 컨트롤러 번호 -> 녹음한 점 (기존 곡선에 mergeLanePoints 로 더한다)
};

class MidiRecorder
{
public:
    void start (const RecordOptions& options, double startBeat);
    bool recording() const noexcept { return recording_; }
    double startBeat() const noexcept { return startBeat_; }

    // 같은 음을 누른 채 다시 누르면 앞 음을 그 박에서 끝낸다. 뗀 박이 누른 박보다 앞이면(반복이 되돌아갔다) 반복 끝에서 끝낸다.
    void noteOn (double beat, int pitch, int velocity);
    void noteOff (double beat, int pitch);
    // CC 는 0..127, 피치 벤드(kPitchBend)는 -1..1. 바로 앞 점과 같은 값은 두지 않고, 같은 박의 점은 새 값으로 바꾼다. 페달(64)은 step.
    void controller (double beat, int controller, double value);

    // 화면용: 끝난 음과 누르고 있는 음 (nowBeat 까지)
    std::vector<Note> notesSoFar (double nowBeat) const;
    size_t noteCount() const noexcept { return notes_.size() + held_.size(); }
    bool empty() const noexcept { return notes_.empty() && held_.empty() && lanes_.empty(); }

    // 누르고 있는 음은 endBeat 에서 끝낸다. 격자 맞추기와 펀치를 적용하고 녹음을 끝낸다.
    RecordedTake finish (double endBeat);
    void cancel();

private:
    struct Held
    {
        double start = 0.0;
        int velocity = 100;
    };

    Note closedNote (int pitch, const Held& held, double endBeat) const;

    RecordOptions options_;
    bool recording_ = false;
    double startBeat_ = 0.0;
    std::vector<Note> notes_;
    std::map<int, Held> held_;
    std::map<int, AutomationLane> lanes_;
};

// 곡선에 점을 더한다: 같은 박의 점은 새 점으로 바꾸고 박 순서로 둔다
void mergeLanePoints (AutomationLane& lane, const std::vector<AutomationPoint>& points);

} // namespace mr
