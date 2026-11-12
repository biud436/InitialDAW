// InitialDAW 앱의 시작점. 인자가 프로젝트 파일이면 연다.
//
//   InitialDAW [프로젝트.json]
//   InitialDAW --snapshot 화면.png [--size 1600x1000] [--track N] [--at 초] [--bars 41-49] [--lane cc1]
//              [--mixer] [--score] [--effect 트랙:번호 | bus번호:번호 | master:번호] 프로젝트.json
//     창을 띄우지 않고 화면을 PNG 로 그려 끝낸다 (문서와 화면 검증용). --mixer 는 아래 칸에 믹서를 그리고,
//     오디오 장치 없이 재생기를 만들어 --at 부터 잠깐 돌린 미터 값을 보인다. --effect 는 효과 매개변수 칸을 연다.
//     --instrument 트랙 은 그 트랙의 내장 신스 패널만 그린다. --export 는 내보내기 대화상자만, --new 는 새 곡 대화상자만 그린다.
//     --companions 1,+3 은 피아노 롤에 1번 트랙을 흐리게 함께 보이고 3번 트랙을 함께 고치는 트랙으로 둔다.
//     --video-window 는 곡의 영상에서 --at 의 곡 위치에 보이는 프레임(영상 창)만, --export-video 는 영상 형식을 고른 내보내기 대화상자만 그린다.
//     오디오 트랙은 녹음의 음 덩어리를 재서 그린다. --audio-note 0:2 는 0번 클립의 셋째 음을 고른다 (손잡이가 보인다).
//   InitialDAW --edit-test 프로젝트.json
//     창 없이 프로젝트의 편집 경로를 시험한다: 임시 사본에 음 옮기기, 곡선, 템포를 편집하고, 되돌리고, 다시 실행하고,
//     저장한 파일을 다시 읽어 맞는지 본다. 피아노 롤과 믹서에는 마우스를 흉내 내 보낸다. 녹음기의 결과 넣기와 눈금자의 반복 구간 끌기도 본다.
//     결과는 표준 출력의 JSON 한 줄, 종료 코드 0 이면 통과.
//   InitialDAW --plugin-test 플러그인.vst3
//     창 없이 악기 패널을 시험한다: 그 플러그인을 곡 안의 정의로 쓰는 곡을 만들고, 재생기를 장치 없이 만들고, 패널(일반 편집기)의
//     슬라이더를 움직여 플러그인 상태가 곡에 쓰이는지, 실행 취소와 다시 실행이 플러그인 값을 돌리는지, 저장한 곡을 다시 열면 같은지 본다.
//     Dexed 가 받아져 있으면 자체 화면이 만들어지는지도 본다. 결과는 표준 출력의 JSON 한 줄, 종료 코드 0 이면 통과.
//   InitialDAW --new-test [--png 화면.png]
//     창 없이 새 곡 대화상자를 시험한다: 템플릿 목록이 차는지, 대화상자의 만들기 길로 임시 폴더에 곡을 만들어 열고 템플릿과 같은 트랙인지,
//     있는 파일에는 만들지 않는지 본다. --png 는 대화상자를 그린다. 결과는 표준 출력의 JSON 한 줄, 종료 코드 0 이면 통과.
//   InitialDAW --export-test [플러그인.vst3] [--png 화면.png]
//     창 없이 내보내기 대화상자를 시험한다: 시험 곡을 만들어 대화상자의 내보내기를 누르고, 32비트 WAV 가 엔진의 renderComposition 과
//     바이트까지 같은지, 스템과 MIDI, Ogg 반복 구간(LOOPSTART), 마디 구간(MP3), 멈추기, 시험 영상에 입힌 MP4 (영상 샘플이 원본 그대로, 소리가 엔진으로 입힌 것과 같은지)를 본다. 플러그인을 주면 플러그인 악기 트랙이 있는
//     곡을 내보내 악기를 메시지 스레드에서 만들고 뒤에서 렌더링한 결과가 엔진과 같은지 본다. --png 는 끝난 대화상자를 그린다.
//     결과는 표준 출력의 JSON 한 줄, 종료 코드 0 이면 통과.
//   InitialDAW --scan-plugins 캐시.json [폴더...]
//     VST3(기본 위치와 준 폴더)와 AU 를 찾아 플러그인 캐시를 쓴다. 앱의 "플러그인 찾기"가 이것을 따로 실행한다.
//   InitialDAW --self-test [--at 초] [--loudness] 프로젝트.json
//     창 없이 오디오 장치를 열고, 재생기를 뒤에서 만들고, 아주 작은 소리(-60 dB)로 3초 재생해 위치가 나아가는지 본다.
//     재생하기 전에 멈춘 채로 피아노 롤의 음 누르기(가장 음이 많은 트랙의 가운데 음)를 0.5초 들려주고 그 트랙의 크기(페이더 뒤)가 오르는지 본다.
//     이어서 그 트랙을 고르고 자판 연주(Z)를 같은 길로 눌러 그 트랙의 크기가 오르는지 본다.
//     재생한 뒤에는 두 박 구간 재생(⇧스페이스와 같은 길)이 스스로 끝 샘플에서 멈추고 재생 위치가 구간의 처음으로 돌아오는지 본다.
//     끝으로 카운트인 없이 녹음을 켜고 MIDI 입력의 길로 음을 0.3초 눌러 그 트랙에 음표 하나가 그 자리에 들어가는지 본다.
//     --loudness 면 재생 전에 뒤에서 잰 음량(모니터 게인)을 기다려 함께 알린다.
//     결과는 표준 출력의 JSON 한 줄, 종료 코드 0 이면 통과.

#include "AutomationTargets.h"
#include "EffectEditor.h"
#include "ExportDialog.h"
#include "HarmonyMenu.h"
#include "InstrumentWindow.h"
#include "MainComponent.h"
#include "Grooves.h"
#include "MixerText.h"
#include "Theme.h"
#include "composition/Document.h"
#include "core/Log.h"
#include "instruments/AudioClips.h"
#include "io/AudioFile.h"
#include "midi/MidiExport.h"
#include "realtime/AudioTakeRecorder.h"
#include "plugins/PluginHost.h"

#include <algorithm>
#include <vector>
#include "realtime/RealtimePlayer.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <set>

namespace daw
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow (std::unique_ptr<MainComponent> content)
        : juce::DocumentWindow ("InitialDAW", colours::background, juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* raw = content.get();
        setContentOwned (content.release(), true);
        setResizable (true, true);
        setResizeLimits (900, 600, 10000, 10000);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (raw);
       #else
        setMenuBar (raw);
       #endif
        raw->grabKeyboardFocus();
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class Application final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "InitialDAW"; }
    const juce::String getApplicationVersion() override { return INITIALDAW_VERSION; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel_);
        mr::log::setLevel (mr::log::Level::quiet);   // 엔진의 진행 로그는 앱에서 쓰지 않는다
        hostSession_ = std::make_unique<mr::HostSession>();   // 플러그인 악기 (창과 재생기보다 늦게 닫는다)
        const auto args = getCommandLineParameterArray();
        if (args.contains ("--scan-plugins"))
        {
            setApplicationReturnValue (runScanPlugins (args));
            quit();
            return;
        }
        if (args.contains ("--plugin-test"))
        {
            pluginTest_ = std::make_unique<PluginTest> (args);
            return;
        }
        if (args.contains ("--plugin-effect-test"))
        {
            pluginEffectTest_ = std::make_unique<PluginEffectTest> (args);
            return;
        }
        if (args.contains ("--new-test"))
        {
            setApplicationReturnValue (runNewTest (args));
            quit();
            return;
        }
        if (args.contains ("--export-test"))
        {
            exportTest_ = std::make_unique<ExportTest> (args);
            return;
        }
        if (args.contains ("--snapshot"))
        {
            setApplicationReturnValue (runSnapshot (args));
            quit();
            return;
        }
        if (args.contains ("--edit-test"))
        {
            setApplicationReturnValue (runEditTest (args));
            quit();
            return;
        }
        if (args.contains ("--self-test"))
        {
            selfTest_ = std::make_unique<SelfTest> (args);
            return;
        }

        auto content = std::make_unique<MainComponent> (true);
        juce::File project;
        for (const auto& a : args)
            if (a.endsWithIgnoreCase (".json"))
                project = juce::File::getCurrentWorkingDirectory().getChildFile (a);
        auto* raw = content.get();
        window_ = std::make_unique<MainWindow> (std::move (content));
        if (project.existsAsFile())
            raw->openProject (project);
    }

    void shutdown() override
    {
        exportTest_.reset();
        selfTest_.reset();
        pluginTest_.reset();
        pluginEffectTest_.reset();
        window_.reset();
        hostSession_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        auto* content = window_ != nullptr ? dynamic_cast<MainComponent*> (window_->getContentComponent()) : nullptr;
        if (content == nullptr)
        {
            quit();
            return;
        }
        content->askToSave ([] { juce::JUCEApplication::getInstance()->quit(); });
    }

    // macOS 에서 `open -a InitialDAW 곡.json` 이나 아이콘에 파일을 끌어 놓으면 여기로 온다 (실행 중인 창에서 연다)
    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        if (window_ == nullptr)
            return;
        juce::StringArray tokens;
        tokens.addTokens (commandLine, true);
        for (auto token : tokens)
        {
            token = token.unquoted();
            if (! token.endsWithIgnoreCase (".json"))
                continue;
            const auto file = juce::File::isAbsolutePath (token) ? juce::File (token) : juce::File::getCurrentWorkingDirectory().getChildFile (token);
            if (auto* content = dynamic_cast<MainComponent*> (window_->getContentComponent()); content != nullptr && file.existsAsFile())
            {
                content->requestOpen (file);
                window_->toFront (true);
            }
        }
    }

private:
    // 창 없이 앱의 재생 경로를 시험한다: 불러오기(뒤 스레드), 재생기 바꿔 끼우기, 오디오 콜백, 위치
    class SelfTest final : private juce::Timer
    {
    public:
        explicit SelfTest (const juce::StringArray& args) : view_ (true, false)
        {
            juce::File project;
            for (int i = 0; i < args.size(); ++i)
            {
                if (args[i] == "--at" && i + 1 < args.size())
                    at_ = args[++i].getDoubleValue();
                else if (args[i] == "--loudness")
                    waitLoudness_ = true;
                else if (args[i].endsWithIgnoreCase (".json"))
                    project = juce::File::getCurrentWorkingDirectory().getChildFile (args[i]);
            }
            start_ = juce::Time::getMillisecondCounterHiRes();
            if (! view_.openProject (project))
            {
                finish (false, "cannot open project");
                return;
            }
            if (! waitLoudness_)
                view_.setMonitorGainDb (-24.0);   // 사람이 정한 값으로 친다: 음량을 재도 바꾸지 않는다
            startTimer (5);
        }

    private:
        void timerCallback() override
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            auto player = view_.player();
            if (stage_ == 0)
            {
                if (player == nullptr || (waitLoudness_ && ! view_.loudnessMeasured()))
                {
                    if (now - start_ > (waitLoudness_ ? 240000.0 : 60000.0))
                        finish (false, player == nullptr ? "player was not ready in time" : "loudness was not measured in time");
                    return;
                }
                loadSeconds_ = (now - start_) / 1000.0;
                measuredGain_ = view_.monitorGainDb();
                player->setMonitorGainDb (-60.0);
                view_.setPlayheadSeconds (at_);
                chooseAuditionNote();
                player->takeTrackPeaks (auditionTrack_);
                stageStart_ = now;
                stage_ = 1;
                return;
            }
            if (stage_ == 1)
            {
                silentPeak_ = std::max (silentPeak_, peakOf (*player));
                if (now - stageStart_ < 300.0)
                    return;
                if (view_.pianoRoll().onAudition)
                    view_.pianoRoll().onAudition (auditionTrack_, auditionPitch_, 100, {}, 1);
                stageStart_ = now;
                stage_ = 2;
                return;
            }
            if (stage_ == 2)
            {
                auditionPeak_ = std::max (auditionPeak_, peakOf (*player));
                if (now - stageStart_ < 500.0)
                    return;
                view_.pianoRoll().onAudition (auditionTrack_, auditionPitch_, 0, {}, 1);
                stageStart_ = now;
                stage_ = 20;
                return;
            }
            if (stage_ == 20)
            {
                if (now - stageStart_ < 700.0)   // 앞 음의 울림이 그칠 때까지
                    return;
                peakOf (*player);
                view_.selectTrack ((int) auditionTrack_);
                auto& keys = view_.typingKeyboard();
                keys.setEnabled (true);
                keys.setBase (auditionPitch_);
                keys.update ([] (int code) { return code == 'Z'; });   // 앱의 타이머가 실제 자판을 읽어 곧 뗀다
                stageStart_ = now;
                stage_ = 21;
                return;
            }
            if (stage_ == 21)
            {
                // 앱의 타이머(30 Hz)가 실제 자판을 읽어 흉내 낸 Z 를 떼므로 5 ms 마다 다시 누른다
                view_.typingKeyboard().update ([] (int code) { return code == 'Z'; });
                typingPeak_ = std::max (typingPeak_, peakOf (*player));
                if (now - stageStart_ < 300.0)
                    return;
                view_.typingKeyboard().releaseAll();
                view_.play();
                playStart_ = now;
                stage_ = 3;
                return;
            }
            if (stage_ == 3)
            {
                if (now - playStart_ < 3000.0)
                    return;
                advanced_ = player->positionSeconds() - at_;
                view_.stop();
                rangeFrom_ = view_.project().beatAtSeconds (at_);
                rangeTo_ = rangeFrom_ + 2.0;
                rangeOk_ = view_.playBeats (rangeFrom_, rangeTo_);
                stageStart_ = now;
                stage_ = 4;
                return;
            }
            const double rangeSeconds = view_.project().secondsAtBeat (rangeTo_) - view_.project().secondsAtBeat (rangeFrom_);
            if (stage_ == 4)
            {
                // 가장 멀리 간 자리 (멈춘 뒤 앱의 타이머가 곧 처음으로 돌리므로 멈춘 순간의 위치 대신)
                rangeEnd_ = std::max (rangeEnd_, player->position());
                if (player->isPlaying() && now - stageStart_ < rangeSeconds * 1000.0 + 1500.0)
                    return;
                rangeElapsed_ = (now - stageStart_) / 1000.0;
                stageStart_ = now;
                stage_ = 5;
                return;
            }
            if (stage_ == 5)
            {
                if (now - stageStart_ < 200.0)
                    return;
                const auto expectedEnd = (int64_t) std::llround (view_.project().secondsAtBeat (rangeTo_) * player->sampleRate());
                const bool returned = std::abs (player->positionSeconds() - view_.project().secondsAtBeat (rangeFrom_)) < 0.01;
                const bool reachedEnd = rangeEnd_ <= expectedEnd && rangeEnd_ >= expectedEnd - 2 * player->blockSize();   // 끝 샘플은 엔진 시험이 본다
                rangeResult_ = rangeOk_ && ! player->isPlaying() && reachedEnd && rangeElapsed_ < rangeSeconds + 0.5 && returned;

                // 녹음: 카운트인 없이 at_ 에서 시작해 자판 Z 를 0.3초 누른다
                view_.selectTrack ((int) auditionTrack_);
                view_.setPlayheadSeconds (at_);
                view_.setCountInBars (0);
                notesBefore_ = view_.project().composition().tracks[auditionTrack_].notes.size();
                recordStarted_ = view_.startRecording();
                // MIDI 입력의 길로 음을 넣는다 (자판 연주는 앱의 타이머가 실제 자판을 읽어 바로 떼므로 녹음 길이를 잴 수 없다)
                typedPitch_ = auditionPitch_;
                if (auto* midi = view_.midiInputs())
                    midi->inject (juce::MidiMessage::noteOn (1, typedPitch_, (juce::uint8) 100));
                stageStart_ = now;
                stage_ = 6;
                return;
            }
            if (stage_ == 6)
            {
                if (now - stageStart_ < 300.0)
                    return;
                if (auto* midi = view_.midiInputs())
                    midi->inject (juce::MidiMessage::noteOff (1, typedPitch_));
                stageStart_ = now;
                stage_ = 7;
                return;
            }
            if (now - stageStart_ < 200.0)
                return;
            recorded_ = view_.stopRecording();
            view_.stop();
            const auto& notes = view_.project().composition().tracks[auditionTrack_].notes;
            const double startBeat = view_.project().beatAtSeconds (at_);
            bool noteOk = false;
            for (const auto& n : notes)
                if (! n.humanize && n.pitch == typedPitch_ && std::abs (n.startBeat - startBeat) < 0.3)
                {
                    const double seconds = view_.project().secondsAtBeat (n.startBeat + n.durationBeat) - view_.project().secondsAtBeat (n.startBeat);
                    recordedStart_ = n.startBeat - startBeat;
                    recordedSeconds_ = seconds;
                    noteOk = seconds > 0.15 && seconds < 0.5;
                }
            const bool record = recordStarted_ && recorded_ && notes.size() == notesBefore_ + 1 && noteOk;
            const bool heard = auditionPeak_ > std::max (1.0e-7f, silentPeak_ * 10.0f);
            const bool typed = typingPeak_ > std::max (1.0e-7f, silentPeak_ * 10.0f);
            finish (advanced_ > 2.5 && advanced_ < 3.6 && heard && typed && rangeResult_ && record,
                    "advanced " + juce::String (advanced_, 3) + " s" + (heard ? "" : ", the pressed note was not heard")
                        + (typed ? "" : ", the typed key was not heard")
                        + (rangeResult_ ? "" : ", the range did not stop on its end or return")
                        + (record ? "" : ", the typed key was not recorded as one note at the start position"), advanced_);
        }

        // 5 ms 마다 읽어 가장 큰 값을 둔다 (화면의 미터도 30 Hz 로 읽어 0 으로 돌린다. 마스터는 -60 dB 모니터 게인 뒤라 0 이다)
        float peakOf (mr::RealtimePlayer& player) const
        {
            const auto p = player.takeTrackPeaks (auditionTrack_);
            return std::max (p.left, p.right);
        }

        void chooseAuditionNote()
        {
            const auto& tracks = view_.project().composition().tracks;
            size_t most = 0;
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].notes.size() > tracks[most].notes.size())
                    most = i;
            auditionTrack_ = most;
            if (! tracks.empty() && ! tracks[most].notes.empty())
            {
                std::vector<int> pitches;
                for (const auto& n : tracks[most].notes)
                    pitches.push_back (n.pitch);
                std::nth_element (pitches.begin(), pitches.begin() + (long) pitches.size() / 2, pitches.end());
                auditionPitch_ = pitches[pitches.size() / 2];
            }
        }

        void finish (bool ok, const juce::String& message, double advanced = 0.0)
        {
            stopTimer();
            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", ok);
            o->setProperty ("message", message);
            o->setProperty ("loadSeconds", loadSeconds_);
            o->setProperty ("advancedSeconds", advanced);
            o->setProperty ("device", view_.deviceDescription());
            o->setProperty ("rangeSeconds", rangeElapsed_);
            o->setProperty ("rangeFarthestSample", (juce::int64) rangeEnd_);
            o->setProperty ("auditionTrack", (int) auditionTrack_);
            o->setProperty ("auditionPitch", auditionPitch_);
            o->setProperty ("auditionPeakDb", juce::Decibels::gainToDecibels (auditionPeak_, -200.0f));
            o->setProperty ("typingPeakDb", juce::Decibels::gainToDecibels (typingPeak_, -200.0f));
            o->setProperty ("silentPeakDb", juce::Decibels::gainToDecibels (silentPeak_, -200.0f));
            o->setProperty ("recordedNotes", recorded_ ? 1 : 0);
            o->setProperty ("recordedStartOffsetBeats", recordedStart_);
            o->setProperty ("recordedSeconds", recordedSeconds_);
            if (waitLoudness_)
                o->setProperty ("monitorGainDb", measuredGain_);
            std::printf ("%s\n", juce::JSON::toString (juce::var (o), true).toRawUTF8());
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (ok ? 0 : 1);
            juce::JUCEApplication::getInstance()->quit();
        }

        MainComponent view_;
        double at_ = 0.0, start_ = 0.0, playStart_ = 0.0, stageStart_ = 0.0, loadSeconds_ = 0.0, measuredGain_ = 0.0;
        int stage_ = 0;
        size_t auditionTrack_ = 0;
        int auditionPitch_ = 60;
        float silentPeak_ = 0.0f, auditionPeak_ = 0.0f, typingPeak_ = 0.0f;
        double advanced_ = 0.0, rangeFrom_ = 0.0, rangeTo_ = 0.0, rangeElapsed_ = 0.0;
        int64_t rangeEnd_ = -1;
        bool rangeOk_ = false, rangeResult_ = false;
        bool waitLoudness_ = false;
        bool recordStarted_ = false, recorded_ = false;
        size_t notesBefore_ = 0;
        int typedPitch_ = 60;
        double recordedStart_ = 0.0, recordedSeconds_ = 0.0;
    };

    int runEditTest (const juce::StringArray& args)
    {
        juce::File source;
        for (const auto& a : args)
            if (a.endsWithIgnoreCase (".json"))
                source = juce::File::getCurrentWorkingDirectory().getChildFile (a);
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("initialdaw-edit-test");
        dir.deleteRecursively();   // 멈춘 실행이 남긴 파일 (테이크와 굳힌 파일의 번호가 어긋난다)
        dir.createDirectory();
        const auto copy = dir.getChildFile (source.getFileName());
        const auto saved = dir.getChildFile ("saved-" + source.getFileName());
        juce::StringArray failures;
        int checks = 0;
        auto expect = [&failures, &checks] (bool ok, const char* what)
        {
            ++checks;
            if (! ok)
                failures.add (what);
        };

        Project project;
        juce::String error;
        if (! source.copyFileTo (copy) || ! project.load (copy, error))
        {
            std::printf ("{\"ok\": false, \"message\": \"cannot open project\"}\n");
            return 1;
        }
        int notified = 0;
        struct Counter final : Project::Listener
        {
            int& count;
            explicit Counter (int& c) : count (c) {}
            void projectEdited (int) override { ++count; }
        } counter (notified);
        project.addListener (&counter);

        const auto original = project.toJson();
        const auto firstPitch = project.composition().tracks[0].notes.at (0).pitch;
        const auto notes = project.editNotes (0);
        const auto moved = mr::moveNotes (notes, { 0 }, 1.0, 2);
        expect (project.editTrack (0, mr::withNotes (project.trackJson (0), moved.notes), "move", error), "move edit accepted");
        expect (project.composition().tracks[0].notes[0].pitch == firstPitch + 2, "moved note pitch");
        expect (project.hasUnsavedEdits(), "unsaved after edit");

        mr::AutomationLane lane;
        lane.points = { { 0.0, 20.0, mr::CurveShape::linear }, { 4.0, 110.0, mr::CurveShape::smooth } };
        expect (project.editTrack (0, mr::withLane (project.trackJson (0), 11, lane), "lane", error), "lane edit accepted");
        expect (project.composition().tracks[0].automation.count (11) == 1, "lane present");
        expect (project.editTempo (mr::tempoJson ({ mr::TempoPoint { 0.0, 100.0, false } }), mr::timeSignatureJson (3, 4), "tempo", error),
                "tempo edit accepted");
        expect (project.barBeats() == 3.0, "time signature applied");
        const auto edited = project.toJson();

        // 형식에 맞지 않는 편집은 받지 않고 아무것도 바꾸지 않는다
        auto bad = project.trackJson (0);
        bad["notes"][0].getDynamicObject()->setProperty ("velocity", 0);
        expect (! project.editTrack (0, bad, "bad", error) && error.isNotEmpty(), "invalid edit rejected");
        expect (project.toJson() == edited, "invalid edit left the project unchanged");

        for (int i = 0; i < 3; ++i)
            project.undoManager().undo();
        expect (project.toJson() == original, "undo restores the original text");
        expect (! project.hasUnsavedEdits(), "no unsaved edits after undoing everything");
        for (int i = 0; i < 3; ++i)
            project.undoManager().redo();
        expect (project.toJson() == edited, "redo restores the edits");
        expect (notified == 9, "listener called for every change");

        expect (project.save (saved, error), "save");
        expect (! project.hasUnsavedEdits(), "saved");
        Project reread;
        expect (reread.load (saved, error) && reread.toJson() == edited, "saved file reads back the same");
        project.removeListener (&counter);

        runPianoRollTest (dir, expect);
        runTypingTest (expect);
        runMixerTest (dir, expect);
        runInstrumentTest (dir, expect);
        runSynthPanelTest (dir, expect);
        runHarmonyTest (dir, expect);
        runRecordTest (dir, expect);
        runTrackTest (dir, expect);
        runArrangeTest (dir, expect);
        runAudioTest (dir, expect);
        runAutomationTest (dir, expect);
        runNoteToolsTest (dir, expect);
        runBounceTest (dir, expect);
        runHistoryTest (dir, expect);
        runDrumTest (dir, expect);
        runStepInputTest (dir, expect);
        runScoreTest (dir, expect);
        runAnalyzerTest (expect);
        runInputMonitorTest (dir, expect);
        runClipStretchTest (dir, expect);
        runAudioNoteTest (dir, expect);
        runFolderTest (dir, expect);
        runCompTest (dir, expect);
        runSamplerTest (dir, expect);
        runTranscribeTest (dir, expect);
        runSliceTest (dir, expect);
        runWarpTest (dir, expect);
        runGrooveTest (dir, expect);
        runFollowTempoTest (dir, expect);
        runTrackTemplateTest (dir, expect);
        runVideoTest (dir, expect);
        runCompanionTest (dir, expect);
        runSectionTest (dir, expect);

        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", failures.isEmpty());
        o->setProperty ("failures", failures.joinIntoString (", "));
        o->setProperty ("checks", checks);
        o->setProperty ("notes", (int) reread.composition().tracks[0].notes.size());
        std::printf ("%s\n", juce::JSON::toString (juce::var (o), true).toRawUTF8());
        dir.deleteRecursively();
        return failures.isEmpty() ? 0 : 1;
    }

    // 구간 띠: 마커에서 다음 마커까지의 덩어리를 끌어 순서 바꾸기 (편집 하나, 실행 취소), 누르면 그 처음으로, 두 번 누르면 모든 트랙의 구간 고르기,
    // 복제와 지우기, 마디 줄이 아닌 경계의 구간은 옮기지 않기
    template <typename Expect>
    static void runSectionTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("sections.json");
        file.replaceWithText (R"({ "tempo": [ { "beat": 0, "bpm": 120 }, { "beat": 8, "bpm": 90 } ],
            "meterChanges": [ { "beat": 8, "timeSignature": [3, 4] }, { "beat": 14, "timeSignature": [4, 4] } ],
            "markers": [ { "beat": 0, "name": "A" }, { "beat": 8, "name": "B" }, { "beat": 14, "name": "C" } ], "render": { "endBeat": 22 },
            "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 },
                { "pitch": 64, "startBeat": 8, "durationBeat": 1, "velocity": 80 }, { "pitch": 67, "startBeat": 14, "durationBeat": 1, "velocity": 80 } ] },
              { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 8, "velocity": 80 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "section project opened");
            return;
        }
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        view.showBars (1, 6);
        auto send = [&arrangement] (const char* kind, juce::Point<float> at, juce::Point<float> down)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &arrangement, &arrangement, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") arrangement.mouseDown (e);
            else if (k == "drag") arrangement.mouseDrag (e);
            else if (k == "up") arrangement.mouseUp (e);
            else if (k == "double") arrangement.mouseDoubleClick (e);
        };
        auto names = [&project]
        {
            juce::StringArray out;
            for (const auto& m : project.markers())
                out.add (juce::String::fromUTF8 (m.name.c_str()) + "@" + juce::String (m.beat));
            return out.joinIntoString (" ");
        };
        const float y = (float) arrangement.sectionStrip().getCentreY();
        expect (arrangement.sections().size() == 3, "the section strip shows three sections");

        // B 를 끌어 A 앞에 놓는다
        const juce::Point<float> from (arrangement.sectionX (1, 0.5f), y), to (arrangement.sectionX (0, 0.05f), y);
        send ("down", from, from);
        send ("drag", from.translated (-20.0f, 0.0f), from);
        send ("drag", to, from);
        expect (names() == "A@0 B@8 C@14", "dragging a section previews without editing");
        send ("up", to, from);
        expect (names() == "B@0 A@6 C@14" && project.composition().beatsPerBar == 3 && project.composition().tracks[0].notes[0].pitch == 64,
                "dropping a section moves it with its meter and notes");
        project.undoManager().undo();
        expect (project.toJson() == original, "one undo puts the section back");

        // 누르면 그 처음으로, 두 번 누르면 모든 트랙의 구간
        const juce::Point<float> c (arrangement.sectionX (2, 0.5f), y);
        send ("down", c, c);
        send ("up", c, c);
        expect (std::abs (view.playheadBeat() - 14.0) < 1.0e-6, "clicking a section moves the playhead to its start");
        send ("double", c, c);
        const auto range = arrangement.range();
        expect (range && range->firstTrack == 0 && range->lastTrack == 1 && range->start == 14.0 && range->end == 22.0,
                "double-clicking a section selects it on every track");

        expect (view.duplicateSection (1) && names() == "A@0 B@8 B@14 C@20", "a section is duplicated right after itself");
        expect (view.deleteSection (2) && names() == "A@0 B@8 C@14", "a section is deleted and the rest pulled forward");
        expect (view.moveSection (0, 3) && names() == "B@0 C@6 A@14", "a section moves to the end");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing every section edit restores the song");

        // 마디 줄이 아닌 경계
        expect (view.addMarker (17.0, "D", false) && ! view.moveSection (3, 0) && project.markers().size() == 4, "a section off the bar lines is not moved");
    }

    // 피아노 롤의 여러 트랙: 함께 고치는 트랙을 사각형으로 함께 고르고, 끌어 옮기고, 조옮김, 지우기, 다듬기가 실행 취소 한 번에 돌아오기,
    // 한 음 누르기, 흐리게 보는 트랙의 음을 두 번 눌러 트랙 바꾸기, 조 바꾸기 앞뒤의 조의 줄
    template <typename Expect>
    static void runCompanionTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("companions.json");
        file.replaceWithText (R"({ "tempo": 120, "key": "C major", "keyChanges": [ { "beat": 8, "key": "E major" } ],
            "markers": [ { "beat": 4, "name": "B" } ], "tracks": [
            { "name": "Lead", "instrument": "synth.piano", "notes": [ { "pitch": 64, "startBeat": 0, "durationBeat": 1, "velocity": 90 },
                                                                      { "pitch": 65, "startBeat": 1, "durationBeat": 1, "velocity": 90 } ] },
            { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 2, "velocity": 90 },
                                                                     { "pitch": 62, "startBeat": 2, "durationBeat": 1, "velocity": 90 } ] },
            { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": 67, "startBeat": 5, "durationBeat": 2, "velocity": 70 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "companion project opened");
            return;
        }
        view.selectTrack (0);
        view.showBars (1, 3);
        auto& roll = view.pianoRoll();
        auto& project = view.project();
        const auto original = project.toJson();
        auto send = [&roll] (const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {}, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") roll.mouseDown (e);
            else if (k == "drag") roll.mouseDrag (e);
            else if (k == "up") roll.mouseUp (e);
            else if (k == "double") roll.mouseDoubleClick (e);
        };
        auto dragFromTo = [&send] (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
        {
            send ("down", from, from, mods);
            send ("drag", from + (to - from) * 0.5f, from, mods);
            send ("drag", to, from, mods);
            send ("up", to, from, mods);
        };
        auto starts = [&project] (size_t t)
        {
            juce::StringArray out;
            for (const auto& n : project.composition().tracks[t].notes)
                out.add (juce::String (n.pitch) + "@" + juce::String (n.startBeat));
            return out.joinIntoString (" ");
        };
        using Companion = PianoRollPanel::Companion;
        roll.setCompanion (1, Companion::edited);
        roll.setCompanion (2, Companion::shown);
        expect (roll.companionTracks (Companion::edited) == std::vector<size_t> { 1 } && roll.companionTracks (Companion::shown) == std::vector<size_t> { 2 },
                "tracks are shown and edited together");

        // 사각형: 함께 고치는 Bass 의 음도 고르고, 흐리게 보는 Pad 는 고르지 않는다
        dragFromTo (roll.notePoint (-0.0, 70).translated (2.0f, 0.0f), roll.notePoint (7.5, 58));
        expect (roll.selectionSize() == 4 && roll.otherSelection().count (1) && roll.otherSelection().at (1).size() == 2,
                "a band selects notes on the track edited together, not on the one only shown");
        // 끌기: 고른 음 넷이 한 박 오른쪽으로, 편집 하나
        dragFromTo (roll.notePoint (0.5, 64), roll.notePoint (1.5, 64));
        expect (starts (0) == "64@1 65@2" && starts (1) == "60@1 62@3" && starts (2) == "67@5", "dragging moves the selected notes on both tracks");
        project.undoManager().undo();
        expect (project.toJson() == original, "one undo puts both tracks back");
        project.undoManager().redo();
        expect (roll.selectAll() && roll.selectionSize() == 4, "select all takes the track edited together");   // 다시 실행은 선택을 푼다
        expect (roll.transposeSelection (2) && starts (0) == "66@1 67@2" && starts (1) == "62@1 64@3", "transposing reaches the track edited together");
        expect (roll.setLengths (0.5) && project.composition().tracks[1].notes[0].durationBeat == 0.5 && project.composition().tracks[0].notes[1].durationBeat == 0.5,
                "note tools reach the track edited together");
        expect (roll.deleteSelection() && project.composition().tracks[0].notes.empty() && project.composition().tracks[1].notes.empty()
                    && project.composition().tracks[2].notes.size() == 1,
                "deleting removes the selected notes on both tracks");
        for (int i = 0; i < 4; ++i)
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing every edit restores the song");

        // 한 음 누르기: 함께 고치는 트랙의 음 하나만 고른다
        const auto bassNote = roll.notePoint (1.0, 60);
        send ("down", bassNote, bassNote);
        send ("up", bassNote, bassNote);
        expect (roll.selectionSize() == 1 && roll.otherSelection().count (1) && roll.otherSelection().at (1) == mr::NoteSelection { 0 },
                "clicking a note of the track edited together selects it alone");
        expect (roll.nudgeSelection (4) && starts (1) == "60@1 62@2" && starts (0) == "64@0 65@1", "nudging moves only that note");
        project.undoManager().undo();

        // 흐린 음을 두 번 누르면 그 트랙으로: 보던 트랙은 흐리게 보는 트랙이 된다
        const auto padNote = roll.notePoint (5.5, 67);
        send ("double", padNote, padNote, {}, 2);
        expect (view.selectedTrack() == 2 && roll.selectionSize() == 1 && roll.companion (0) == Companion::shown && roll.companion (2) == Companion::none,
                "double-clicking a shown note switches to its track");
        expect (project.composition().tracks[2].notes.size() == 1, "switching does not add or delete a note");

        // 조의 줄: C 장조 (C, E 는 들고 C#, F# 은 빠진다), 8박부터 E 장조
        expect (roll.inKey (0.0, 60) && roll.inKey (0.0, 64) && ! roll.inKey (0.0, 61) && ! roll.inKey (4.0, 66), "C major rows");
        expect (roll.inKey (8.0, 61) && roll.inKey (8.0, 66) && ! roll.inKey (8.0, 60) && roll.inKey (7.9, 60), "the key change moves the rows");
        expect (project.toJson() == original, "selecting and switching tracks does not edit the song");
    }

    // 영상 창의 그림에서 시험 영상의 프레임 번호를 읽는다
    static int64_t patternFrameOf (const juce::Image& image)
    {
        if (! image.isValid())
            return -1;
        mr::media::VideoImage v;
        v.width = image.getWidth();
        v.height = image.getHeight();
        v.rgba.resize ((size_t) v.width * (size_t) v.height * 4);
        const juce::Image::BitmapData data (image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < v.height; ++y)
            for (int x = 0; x < v.width; ++x)
            {
                const auto c = data.getPixelColour (x, y);
                auto* px = &v.rgba[((size_t) y * (size_t) v.width + (size_t) x) * 4];
                px[0] = c.getRed();
                px[1] = c.getGreen();
                px[2] = c.getBlue();
                px[3] = 255;
            }
        return mr::media::readPatternFrame (v);
    }

    // 영상: 붙이기 (곡 폴더 안은 상대 경로), 영상 레인을 끌어 자리 옮기기 (격자, ⌘ 는 프레임), 장면 바뀜 후보와 마커, 영상의 소리를 오디오 트랙으로,
    // 영상 창의 프레임 (멈춘 자리, 차례로 풀기, 뒤로 뛰기, 영상 밖), 끌어 놓기, 떼기, 실행 취소
    template <typename Expect>
    static void runVideoTest (const juce::File& dir, Expect& expect)
    {
        if (! mr::media::videoSupported())
            return;
        const auto folder = dir.getChildFile ("video-song");
        const auto movie = folder.getChildFile ("clips").getChildFile ("promo.mp4");
        movie.getParentDirectory().createDirectory();
        mr::media::TestPattern pattern;
        pattern.rate = { 30000, 1001 };
        pattern.frames = 240;   // 8.008 초
        pattern.sceneCuts = { 60, 150 };
        pattern.clickSeconds = { 0.5, 3.0 };
        std::string reason;
        expect (mr::media::writeTestPattern (movie.getFullPathName().toStdString(), pattern, reason), "the test video is written");
        const auto file = folder.getChildFile ("video.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [
            { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 32, "velocity": 90 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "video project opened");
            return;
        }
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        const int lanesBefore = arrangement.lanesTop();
        const auto rate = pattern.rate;
        auto near = [] (double a, double b, double tolerance = 1.0e-6) { return std::abs (a - b) < tolerance; };
        auto offset = [&project] { return project.composition().video ? project.composition().video->offsetSeconds : -999.0; };
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {})
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
            else if (k == "double") target.mouseDoubleClick (e);
        };

        juce::String error;
        expect (view.attachVideo (movie, error), "attaching a video is one edit");
        expect (project.composition().video && project.composition().video->file == "clips/promo.mp4" && offset() == 0.0,
                "a video inside the song folder is stored relative, at the song's start");
        expect (arrangement.video().has_value() && arrangement.lanesTop() == lanesBefore + ArrangementPanel::videoHeight,
                "the video lane appears above the tracks");
        expect (view.videoInfo() && view.videoInfo()->frames == 240 && view.videoInfo()->rate.num == 30000, "the app reads the video");

        // 끌기: 한 박이 20 픽셀 넘게 보이면 박에 맞춘다 (120 BPM 의 두 박은 1초)
        view.showBars (1.0, 8.0);
        const float laneY = (float) arrangement.videoLaneBounds().getCentreY();
        const float beatPx = arrangement.lanePoint (0, 1.0).x - arrangement.lanePoint (0, 0.0).x;
        const juce::Point<float> from (arrangement.videoX (1.0), laneY), to = from.translated (2.0f * beatPx + 4.0f, 0.0f);
        send (arrangement, "down", from, from);
        send (arrangement, "drag", to, from);
        expect (offset() == 0.0, "dragging the video lane previews without editing");
        send (arrangement, "up", to, from);
        expect (near (offset(), 1.0), "dropping the video lane moves the video, snapped to the beat");
        // ⌘ 끌기는 프레임에 맞춘다
        const juce::Point<float> from2 (arrangement.videoX (2.0), laneY), to2 = from2.translated (0.37f * beatPx, 0.0f);
        send (arrangement, "down", from2, from2, juce::ModifierKeys::commandModifier);
        send (arrangement, "drag", to2, from2, juce::ModifierKeys::commandModifier);
        send (arrangement, "up", to2, from2, juce::ModifierKeys::commandModifier);
        const double frames = offset() * rate.fps();
        expect (offset() > 1.1 && offset() < 1.3 && near (frames, std::round (frames), 1.0e-3), "a command-drag snaps the video to frames");
        project.undoManager().undo();
        expect (near (offset(), 1.0), "undo puts the video back");

        // 장면 바뀜: 후보 둘, 후보를 두 번 누르면 "장면 1" 마커, 모두 넣으면 나머지 하나만 더한다
        expect (view.findSceneChanges (true) && arrangement.sceneChanges().size() == 2, "scene changes are found");
        if (arrangement.sceneChanges().size() == 2)
        {
            const auto scenes = arrangement.sceneChanges();
            expect (near (scenes[0], mr::media::secondsAtFrame (60, rate), 1.0e-4) && near (scenes[1], mr::media::secondsAtFrame (150, rate), 1.0e-4),
                    "the scene changes are at the cuts");
            const juce::Point<float> at (arrangement.videoX (scenes[0]), laneY);
            send (arrangement, "double", at, at);
            expect (project.markers().size() == 1 && project.markers()[0].name == juce::String::fromUTF8 ("장면 1").toStdString()
                        && near (project.markers()[0].beat, (scenes[0] + 1.0) * 2.0, 1.0e-5),
                    "double-clicking a scene change puts a marker at its beat");
            expect (view.addSceneMarkers (scenes) && project.markers().size() == 2
                        && project.markers()[1].name == juce::String::fromUTF8 ("장면 2").toStdString(),
                    "putting every scene change as a marker skips the one already there");
        }

        // 영상의 소리: 영상 자리(1초, 두 박)에 놓인 오디오 트랙
        const size_t tracksBefore = project.trackCount();
        expect (view.importVideoSound (error) && project.trackCount() == tracksBefore + 1, "the video's sound becomes an audio track");
        if (project.trackCount() == tracksBefore + 1)
        {
            const auto& t = project.composition().tracks.back();
            expect (t.instrument == "audio" && t.clips.size() == 1 && near (t.clips[0].startBeat, 2.0) && t.clips[0].offsetSeconds == 0.0
                        && folder.getChildFile (juce::String::fromUTF8 (t.clips[0].file.c_str())).existsAsFile(),
                    "the sound clip starts where the video starts");
        }
        expect (view.setVideoOffset (-0.5) && view.importVideoSound (error), "a video before the song start imports its sound too");
        if (project.trackCount() == tracksBefore + 2)
        {
            const auto& c = project.composition().tracks.back().clips.at (0);
            expect (c.startBeat == 0.0 && near (c.offsetSeconds, 0.5) && c.lengthSeconds && near (*c.lengthSeconds, 8.008 - 0.5, 0.03),
                    "the part before the song is cut off the sound clip");
        }

        // 영상 창: 멈춘 자리, 재생 (차례로 풀기), 뒤로 뛰기, 영상 밖
        {
            VideoView video;
            video.setSize (320, 180);
            video.setVideo (movie, rate, 8.008);
            video.showFrameNow (2.5);
            expect (video.shownSeconds() && near (*video.shownSeconds(), mr::media::secondsAtFrame (74, rate), 1.0e-4)
                        && patternFrameOf (video.shownImage()) == 74,
                    "the video view shows the frame at the time");
            auto waitFor = [&video] (double seconds, bool playing)
            {
                video.setTime (seconds, playing);
                for (int i = 0; i < 400; ++i)
                {
                    video.pollFrame();
                    const auto shown = video.shownSeconds();
                    const bool outside = seconds < 0.0 || seconds >= 8.008;
                    if (outside ? ! shown.has_value() && ! video.shownImage().isValid()
                                : shown && std::abs (*shown - mr::media::secondsAtFrame (mr::media::frameAtSeconds (seconds, { 30000, 1001 }), { 30000, 1001 })) < 1.0e-4)
                        return true;
                    juce::Thread::sleep (5);
                }
                return false;
            };
            expect (waitFor (1.0, true) && patternFrameOf (video.shownImage()) == mr::media::frameAtSeconds (1.0, rate), "playing decodes the frame at the time");
            bool smooth = true;
            for (double t = 1.04; t < 2.0; t += 0.04)
                smooth = smooth && waitFor (t, true) && patternFrameOf (video.shownImage()) == mr::media::frameAtSeconds (t, rate);
            expect (smooth, "playing follows the time frame by frame");
            expect (waitFor (0.3, true) && patternFrameOf (video.shownImage()) == mr::media::frameAtSeconds (0.3, rate), "jumping back while playing decodes again");
            expect (waitFor (6.2, false) && patternFrameOf (video.shownImage()) == mr::media::frameAtSeconds (6.2, rate), "a stopped seek reads that frame");
            expect (waitFor (9.0, false), "outside the video nothing is shown");
        }

        // 끌어 놓은 영상은 붙이고, 떼면 레인이 사라진다
        const auto other = folder.getChildFile ("other.mov");
        expect (mr::media::writeTestPattern (other.getFullPathName().toStdString(), pattern, reason), "a second video is written");
        expect (view.isInterestedInFileDrag ({ other.getFullPathName() }), "the window takes video files");
        view.filesDropped ({ other.getFullPathName() }, 10, 10);
        expect (project.composition().video && project.composition().video->file == "other.mov" && arrangement.sceneChanges().empty(),
                "a dropped video replaces the song's video and its scene candidates");
        expect (view.detachVideo() && ! project.composition().video && ! arrangement.video() && arrangement.lanesTop() == lanesBefore,
                "detaching the video removes the lane");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original && ! arrangement.video(), "undoing every video edit restores the song");
    }

    // 트랙 목록: 더하기, 이름 (머리의 이름 칸), 복제, 끌어서 옮기기, 지우기, 색. 사이드체인 키가 이름을 따라가고 실행 취소로 돌아온다
    template <typename Expect>
    static void runTrackTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("tracks.json");
        file.replaceWithText (juce::String::fromUTF8 (R"({ "tempo": 120, "tracks": [
            { "name": "Kick", "instrument": "synth.drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 100 } ] },
            { "name": "Lead", "instrument": "synth.piano", "comment": "선율", "effects": [ { "type": "compressor", "sidechain": "Kick" } ],
              "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90, "lyric": "가" } ] },
            { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })"));
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "track project opened");
            return;
        }
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        // 키보드 포커스가 어디에도 없어도 파일 메뉴의 저장이 켜져 있다 (명령은 이 화면이 받는다)
        juce::Component::unfocusAllComponents();
        {
            bool saveEnabled = false;
            const auto fileMenu = view.getMenuForIndex (0, "File");
            for (juce::PopupMenu::MenuItemIterator it (fileMenu); it.next();)
                if (it.getItem().text == juce::String::fromUTF8 ("저장"))
                    saveEnabled = it.getItem().isEnabled;
            expect (saveEnabled, "the save command stays enabled without keyboard focus");
        }
        auto names = [&project]
        {
            juce::StringArray out;
            for (const auto& t : project.composition().tracks)
                out.add (juce::String::fromUTF8 (t.name.c_str()));
            return out.joinIntoString (",");
        };
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
            else if (k == "double") target.mouseDoubleClick (e);
        };
        auto rowY = [] (int index) { return (float) (ArrangementPanel::rulerHeight + index * ArrangementPanel::laneHeight + 40); };

        // 더하기: 악기 이름에서 이름을 짓고 그 자리에 넣어 고른다
        expect (view.addTrack ("synth.pad", {}, 1) && names() == "Kick,Pad,Lead,Bass", "a new track goes where asked and is named after its instrument");
        expect (view.selectedTrack() == 1 && project.composition().tracks[1].notes.empty(), "the new track is selected and empty");
        expect (arrangement.addTrackBox().getY() > ArrangementPanel::rulerHeight + 4 * ArrangementPanel::laneHeight - 1,
                "the add-track button sits below the last track");
        expect (view.addTrack ("synth.pad") && names() == "Kick,Pad,Lead,Bass,Pad 2", "a second track of the same instrument gets a number");
        project.undoManager().undo();

        // 이름 칸을 두 번 눌러 고친다. 사이드체인 키도 새 이름을 따라간다
        const juce::Point<float> nameAt (40.0f, (float) (ArrangementPanel::rulerHeight + 16));
        send (arrangement, "double", nameAt, nameAt);
        auto* editor = arrangement.renameEditor();
        expect (editor != nullptr && editor->getText() == "Kick", "double-clicking the name opens the name editor");
        if (editor != nullptr)
        {
            editor->setText ("Kick In");
            arrangement.finishRename (true);   // 리턴 (TextEditor 는 리턴을 메시지로 늦게 알린다)
        }
        expect (arrangement.renameEditor() == nullptr && names() == "Kick In,Pad,Lead,Bass", "return commits the new name");
        expect (project.toJson().find ("\"sidechain\": \"Kick In\"") != std::string::npos, "the sidechain key follows the rename");
        expect (! view.renameTrack (1, "Bass") && names() == "Kick In,Pad,Lead,Bass", "a name already in use is refused");
        arrangement.beginRename (3);
        if (auto* ed = arrangement.renameEditor())
        {
            ed->setText ("Nope");
            arrangement.finishRename (false);
        }
        expect (names() == "Kick In,Pad,Lead,Bass", "escape leaves the name");

        // 복제: 바로 아래에, 음과 다른 필드까지
        expect (view.duplicateTrack (2) && names() == "Kick In,Pad,Lead,Lead 2,Bass", "duplicate goes right below with a numbered name");
        expect (project.composition().tracks[3].notes.size() == 1 && project.toJson().find ("\"lyric\": \"가\"") != project.toJson().rfind ("\"lyric\": \"가\""),
                "the duplicate keeps the notes and their lyrics");
        expect (view.selectedTrack() == 3, "the duplicate is selected");

        // 머리를 끌어 옮긴다: Bass(4)를 맨 위로
        const juce::Point<float> from (120.0f, rowY (4)), mid (120.0f, rowY (2)), to (120.0f, (float) ArrangementPanel::rulerHeight + 4.0f);
        send (arrangement, "down", from, from);
        send (arrangement, "drag", mid, from);
        expect (names() == "Kick In,Pad,Lead,Lead 2,Bass", "dragging previews without editing");
        send (arrangement, "drag", to, from);
        send (arrangement, "up", to, from);
        expect (names() == "Bass,Kick In,Pad,Lead,Lead 2", "dropping the header moves the track");
        expect (view.selectedTrack() == 0, "the moved track stays selected");
        const juce::Point<float> click (120.0f, rowY (1));
        send (arrangement, "down", click, click);
        send (arrangement, "up", click, click);
        expect (names() == "Bass,Kick In,Pad,Lead,Lead 2" && view.selectedTrack() == 1, "a click only selects");

        // 색
        expect (view.setTrackColour (3, "#4a90e2") && project.summary (3).colour == juce::Colour (0xff4a90e2), "a chosen colour is used");
        expect (project.toJson().find ("\"color\": \"#4a90e2\"") != std::string::npos, "the colour is saved with the track");

        // 지우기: 키로 쓰던 압축기는 사이드체인을 풀고, 실행 취소로 돌아온다
        const auto beforeDelete = project.toJson();
        expect (view.deleteTrack (1) && names() == "Bass,Pad,Lead,Lead 2", "delete removes the track");
        expect (project.toJson().find ("sidechain") == std::string::npos, "compressors keyed by the deleted track lose the key");
        project.undoManager().undo();
        expect (project.toJson() == beforeDelete, "undo brings the track and its key back");
        while (project.trackCount() > 1)
            view.deleteTrack (0);
        expect (! view.deleteTrack (0) && project.trackCount() == 1, "the last track is not deleted");

        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing every track edit restores the file text");
    }

    // 곡선 칸의 대상: 페이더, 밸런스, 센드, 효과 매개변수. 더하기, 두 번 눌러 점 넣기 (대상의 눈금으로), 끌어 옮기기, 실행 취소
    template <typename Expect>
    static void runAutomationTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("automation.json");
        file.replaceWithText (R"({ "tempo": 120, "buses": [ { "name": "Verb", "effects": [ { "type": "delay", "timeMs": 200, "mix": 1 } ] } ],
          "tracks": [ { "name": "Lead", "instrument": "synth.piano", "mixer": { "gainDb": -3 }, "sends": [ { "bus": "Verb", "levelDb": -10 } ],
                        "effects": [ { "type": "filter", "mode": "lowpass", "cutoffHz": 2000 } ],
                        "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 8, "velocity": 90 } ] },
                      { "name": "Take", "instrument": "audio", "clips": [] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "automation project opened");
            return;
        }
        auto& project = view.project();
        auto& roll = view.pianoRoll();
        const auto original = project.toJson();
        view.showBars (1.0, 4.0);
        auto send = [&roll] (const char* kind, juce::Point<float> at, juce::Point<float> down, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") roll.mouseDown (e);
            else if (k == "drag") roll.mouseDrag (e);
            else if (k == "up") roll.mouseUp (e);
            else if (k == "double") roll.mouseDoubleClick (e);
        };
        const auto& lead = [&project]() -> const mr::TrackSpec& { return project.composition().tracks[0]; };

        // 대상 목록: 컨트롤러, 볼륨, 밸런스, 센드, 효과의 곡선을 둘 수 있는 매개변수
        const auto targets = automationTargets (lead());
        auto has = [&targets] (const std::string& key) { return std::any_of (targets.begin(), targets.end(), [&key] (const LaneTarget& t) { return t.key == key; }); };
        expect (has ("cc1") && has ("mixer/gainDb") && has ("mixer/pan") && has ("sends/Verb") && has ("effects/0:filter/cutoffHz")
                    && ! has ("effects/0:filter/mode"),
                "the lane targets cover controllers, the fader, the balance, sends and automatable effect parameters");
        const auto cutoff = automationTarget (lead(), "effects/0:filter/cutoffHz");
        expect (cutoff && cutoff->scale == LaneTarget::Scale::logarithmic && std::abs (cutoff->fromNorm (cutoff->toNorm (440.0)) - 440.0) < 1.0e-6,
                "a frequency lane uses a logarithmic scale");

        // 볼륨 곡선: 지금 값(-3 dB)의 한 점으로 시작하고, 두 번 눌러 맨 위(+12 dB)에 점을 넣는다
        expect (roll.addLane ("mixer/gainDb") && roll.laneShown() == "mixer/gainDb", "add a fader curve");
        expect (lead().mixer.gainLane.points.size() == 1 && lead().mixer.gainLane.points[0].value == -3.0, "the curve starts at the fader's value");
        const auto top = roll.lanePoint (4.0, 1.0f);
        send ("double", top, top, 2);
        expect (lead().mixer.gainLane.points.size() == 2 && std::abs (lead().mixer.gainLane.points[1].value - 12.0) < 0.01
                    && std::abs (lead().mixer.gainLane.points[1].beat - 4.0) < 1.0e-6,
                "double-clicking the lane adds a point on the fader scale");
        // 점을 아래 끝으로 끈다: -120 dB (소리 없음)
        const auto bottom = roll.lanePoint (4.0, 0.0f);
        send ("down", top, top);
        send ("drag", bottom, top);
        send ("up", bottom, top);
        expect (std::abs (lead().mixer.gainLane.points[1].value - (-120.0)) < 0.01, "dragging the point to the bottom silences the track");
        // 효과의 차단 주파수: 가운데 높이는 범위의 기하 평균
        expect (roll.addLane ("effects/0:filter/cutoffHz"), "add an effect parameter curve");
        const auto middle = roll.lanePoint (2.0, 0.5f);
        send ("double", middle, middle, 2);
        const auto& points = project.composition().tracks[0].effects[0].params["automation"]["cutoffHz"];
        const double expected = std::sqrt (cutoff->minimum * cutoff->maximum);
        expect (points.isArray() && points.getArray()->size() == 2 && std::abs ((double) points[1]["value"] / expected - 1.0) < 0.05,
                "the effect curve point lands on the logarithmic scale");
        expect (roll.addLane ("sends/Verb") && lead().sends[0].levelLane.points.size() == 1 && lead().sends[0].levelLane.points[0].value == -10.0,
                "a send curve starts at the send level");
        expect (! roll.addLane ("sends/Verb"), "a curve that exists is not added twice");
        // 템포: 곡마다 있는 곡선. 두 번 눌러 점을 넣고 (앞 점의 계단을 따른다), 첫 점은 지우지 않는다
        roll.showLane ("tempo");
        expect (roll.laneShown() == "tempo", "the tempo lane is always offered");
        const auto scale = tempoTarget (project.composition().tempo);
        expect (scale.minimum == 80.0 && scale.maximum == 160.0, "the tempo scale fits the song's tempo");
        const auto mid = roll.lanePoint (4.0, 0.75f);
        send ("double", mid, mid, 2);
        const auto& tempo = project.composition().tempo;
        expect (tempo.size() == 2 && tempo[1].beat == 4.0 && std::abs (tempo[1].bpm - 140.0) < 0.5 && ! tempo[1].ramp,
                "double-clicking the tempo lane adds a tempo change on the beat");
        const auto slowScale = tempoTarget (project.composition().tempo);
        const auto slow = roll.lanePoint (4.0, (float) slowScale.toNorm (100.0));
        const auto point = roll.lanePoint (4.0, (float) slowScale.toNorm (140.0));   // 눈금이 새 템포에 맞춰졌다
        send ("down", point, point);
        send ("drag", slow, point);
        send ("up", slow, point);
        expect (project.composition().tempo.size() == 2 && std::abs (project.composition().tempo[1].bpm - 100.0) < 0.5, "dragging a tempo point changes the tempo");
        const auto first = roll.lanePoint (0.0, (float) tempoTarget (project.composition().tempo).toNorm (120.0));
        send ("double", first, first, 2);
        expect (project.composition().tempo.size() == 2 && project.composition().tempo[0].beat == 0.0, "the first tempo point stays");

        // 오디오 트랙: 벨로시티 칸이 없고 볼륨 곡선을 그린다
        view.selectTrack (1);
        expect (roll.laneShown().empty() && roll.addLane ("mixer/gainDb") && ! project.composition().tracks[1].mixer.gainLane.empty(),
                "an audio track gets a volume curve");

        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing every curve edit restores the file text");
    }

    // 오디오 트랙: 가져오기(곡 폴더의 audio/ 에 복사), 레인에 끌어 놓기, 클립 끌기(옮기기, 자르기, 페이드, ⌥ 사본), 나누기, 게인, 정규화,
    // 복제, 지우기, 구간 복제가 클립을 나누는지. 모두 실행 취소로 돌아온다
    template <typename Expect>
    static void runAudioTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("audio-song");
        songDir.createDirectory();
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "render": { "endBeat": 32 }, "tracks": [
            { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ] } ] })");
        // 곡 폴더 밖의 2초짜리 사인파 (44.1 kHz)
        const auto outside = dir.getChildFile ("tone.wav");
        {
            juce::AudioBuffer<float> b (1, 88200);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));
            mr::writeWav (outside, b, b.getNumSamples(), 44100.0, 24);
        }
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "audio project opened");
            return;
        }
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        view.showBars (1.0, 12.0);   // 한 박이 20 픽셀 넘게: 클립을 박에 맞춘다
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {})
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
        };
        auto clips = [&project] (size_t t) { return project.composition().tracks[t].clips; };
        auto near = [] (double a, double b) { return std::abs (a - b) < 1.0e-6; };
        juce::String error;

        // 가져오기: 새 오디오 트랙, 파일은 곡 폴더의 audio/ 로
        expect (arrangement.rangeSnap() == 1.0, "the grid is one beat at this zoom");
        expect (view.importAudio ({ outside.getFullPathName() }, std::nullopt, 4.0, error) && project.trackCount() == 2, "importing audio adds an audio track");
        expect (project.composition().tracks[1].instrument == "audio" && project.composition().tracks[1].name == "tone", "the track is named after the file");
        expect (clips (1).size() == 1 && clips (1)[0].file == "audio/tone.wav" && near (clips (1)[0].startBeat, 4.0)
                    && clips (1)[0].lengthSeconds && near (*clips (1)[0].lengthSeconds, 2.0),
                "the clip points at the copy in the song folder, at the beat, with the file's length");
        expect (songDir.getChildFile ("audio/tone.wav").existsAsFile(), "the file is copied next to the song");
        expect (arrangement.isAudioTrack (1) && ! arrangement.isAudioTrack (0), "the arrangement knows the audio track");
        // 같은 파일을 레인에 끌어 놓으면 그 트랙에 클립을 더한다 (복사본은 다시 만들지 않는다)
        const auto dropAt = arrangement.lanePoint (1, 12.3);
        arrangement.filesDropped ({ outside.getFullPathName() }, (int) dropAt.x, (int) dropAt.y);
        expect (clips (1).size() == 2 && near (clips (1)[1].startBeat, 12.0) && ! songDir.getChildFile ("audio/tone 2.wav").exists(),
                "dropping on the audio lane adds a clip at the grid");

        // 몸통을 끌어 옮긴다 (마디에 맞춘다)
        auto box = arrangement.clipBounds (1, 0);
        const auto c0 = box.getCentre(), c1 = c0.translated (arrangement.lanePoint (1, 8.0).x - arrangement.lanePoint (1, 4.0).x, 0.0f);
        send (arrangement, "down", c0, c0);
        send (arrangement, "drag", c1, c0);
        expect (near (clips (1)[0].startBeat, 4.0), "dragging a clip previews without editing");
        send (arrangement, "up", c1, c0);
        expect (near (clips (1)[0].startBeat, 8.0) && arrangement.selectedClip() && arrangement.selectedClip()->clip == 0, "dropping the clip moves it");
        // 오른쪽 끝을 끌어 자른다: 2초(4박)를 3박으로
        box = arrangement.clipBounds (1, 0);
        const juce::Point<float> e0 (box.getRight() - 1.0f, box.getCentreY()), e1 (arrangement.lanePoint (1, 11.0).x, box.getCentreY());
        send (arrangement, "down", e0, e0);
        send (arrangement, "drag", e1, e0);
        send (arrangement, "up", e1, e0);
        expect (clips (1)[0].lengthSeconds && near (*clips (1)[0].lengthSeconds, 1.5), "dragging the right edge trims the clip end");
        // 왼쪽 끝: 한 박 안쪽으로 (파일 안의 시작이 0.5초)
        box = arrangement.clipBounds (1, 0);
        const juce::Point<float> s0 (box.getX() + 1.0f, box.getCentreY()), s1 (arrangement.lanePoint (1, 9.0).x, box.getCentreY());
        send (arrangement, "down", s0, s0);
        send (arrangement, "drag", s1, s0);
        send (arrangement, "up", s1, s0);
        expect (near (clips (1)[0].startBeat, 9.0) && near (clips (1)[0].offsetSeconds, 0.5) && near (*clips (1)[0].lengthSeconds, 1.0),
                "dragging the left edge trims the start and moves the file offset");
        // 페이드 손잡이
        box = arrangement.clipBounds (1, 0);
        const juce::Point<float> f0 (box.getX() + 1.0f, box.getY() + 2.0f), f1 (arrangement.lanePoint (1, 9.5).x, box.getY() + 2.0f);
        send (arrangement, "down", f0, f0);
        send (arrangement, "drag", f1, f0);
        send (arrangement, "up", f1, f0);
        expect (near (clips (1)[0].fadeInSeconds, 0.25), "dragging the top-left handle sets the fade-in");
        // ⌥ 끌기는 사본
        const auto beforeCopy = clips (1).size();
        box = arrangement.clipBounds (1, 0);
        const auto a0 = box.getCentre(), a1 = a0.translated (arrangement.lanePoint (1, 20.0).x - arrangement.lanePoint (1, 9.0).x, 0.0f);
        send (arrangement, "down", a0, a0, juce::ModifierKeys::altModifier);
        send (arrangement, "drag", a1, a0, juce::ModifierKeys::altModifier);
        send (arrangement, "up", a1, a0, juce::ModifierKeys::altModifier);
        expect (clips (1).size() == beforeCopy + 1 && near (clips (1).back().startBeat, 20.0) && near (clips (1)[0].startBeat, 9.0),
                "alt-dragging a clip copies it");

        // 나누기, 게인, 정규화, 복제, 지우기
        const ArrangementPanel::ClipRef second { 1, 1 };   // 12박의 클립 (2초)
        expect (view.splitClip (second, 13.0) && clips (1).size() == 4, "split at a beat inside the clip");
        const auto tail = clips (1).back();
        expect (near (tail.startBeat, 13.0) && near (tail.offsetSeconds, 0.5) && near (*tail.lengthSeconds, 1.5) && near (*clips (1)[1].lengthSeconds, 0.5),
                "the two halves continue the file");
        expect (! view.splitClip (second, 2.0), "splitting outside the clip does nothing");
        expect (view.setClipGain (second, -6.0) && near (clips (1)[1].gainDb, -6.0), "clip gain");
        expect (view.normalizeClip (second) && std::abs (clips (1)[1].gainDb - (-1.0 - juce::Decibels::gainToDecibels (0.25))) < 0.1,
                "normalising sets the peak to -1 dBFS");
        expect (view.duplicateClip (second) && clips (1).size() == 5 && near (clips (1).back().startBeat, 13.0), "duplicate goes right after the clip");
        expect (view.deleteClip ({ 1, 4 }) && clips (1).size() == 4, "delete a clip");

        // 구간 복제는 클립을 구간 경계에서 나눈다 (8~12박을 12~16박에)
        arrangement.setRange (ArrangementPanel::Range { 1, 1, 8.0, 12.0 });
        expect (view.duplicateRange(), "duplicate a range on the audio track");
        bool shifted = false;
        for (const auto& c : clips (1))
            shifted = shifted || (near (c.startBeat, 13.0) && near (c.offsetSeconds, 0.5) && c.lengthSeconds && near (*c.lengthSeconds, 1.0));
        expect (shifted, "the copied clip lands four beats later");

        // 오디오 녹음: 재생기가 없으면 시작하지 않는다. 테이크는 지연만큼 앞으로 옮겨 클립이 된다
        view.selectTrack (1);
        expect (! view.startRecording() && ! view.isRecording() && ! view.isRecordingAudio(), "audio recording needs a player");
        const auto takeFile = MainComponent::nextTakeFile (project, 1);
        expect (takeFile.getFileName() == "tone take 1.wav" && takeFile.getParentDirectory() == songDir.getChildFile ("audio"), "takes are numbered in audio/");
        {
            juce::AudioBuffer<float> b (2, 48000);
            b.clear();
            mr::writeWav (takeFile, b, b.getNumSamples(), 48000.0, 24);
        }
        expect (MainComponent::nextTakeFile (project, 1).getFileName() == "tone take 2.wav", "the next take gets the next number");
        mr::AudioTake take { takeFile, 96000, 48000, 48000.0, 2 };   // 곡의 2초(4박)부터 1초, 지연 100 ms
        const auto clipsBefore = clips (1).size();
        expect (MainComponent::applyAudioTake (project, 1, take, 4800, error) && clips (1).size() == clipsBefore + 1, "a take becomes a clip");
        const auto recorded = clips (1).back();
        expect (recorded.file == "audio/tone take 1.wav" && near (recorded.startBeat, 3.8) && near (recorded.offsetSeconds, 0.0)
                    && recorded.lengthSeconds && near (*recorded.lengthSeconds, 1.0),
                "the clip starts the latency earlier than the first recorded block");
        mr::AudioTake early { takeFile, 2400, 48000, 48000.0, 2 };   // 지연이 첫 위치보다 크다: 파일의 앞을 건너뛴다
        expect (MainComponent::applyAudioTake (project, 1, early, 4800, error) && near (clips (1).back().startBeat, 0.0)
                    && near (clips (1).back().offsetSeconds, 0.05) && near (*clips (1).back().lengthSeconds, 0.95),
                "a take that would start before the song skips the start of the file");

        // 클립 편집 (D37): 거꾸로 재생, 페이드 모양, 게인 곡선을 마우스로, 영점에서 나누기, 크로스페이드
        {
            const auto tracksBefore = project.trackCount();
            expect (view.importAudio ({ outside.getFullPathName() }, std::nullopt, 40.0, error) && project.trackCount() == tracksBefore + 1,
                    "a second audio track for the clip edits");
            size_t t = 0;
            for (size_t k = 0; k < project.trackCount(); ++k)
                if (project.composition().tracks[k].instrument == "audio" && clips (k).size() == 1 && near (clips (k)[0].startBeat, 40.0))
                    t = k;
            const ArrangementPanel::ClipRef ref { t, 0 };
            expect (t > 1 && view.reverseClip (ref) && clips (t)[0].reverse && near (clips (t)[0].offsetSeconds, 0.0)
                        && near (*clips (t)[0].lengthSeconds, 2.0) && near (clips (t)[0].startBeat, 40.0),
                    "reverse plays the same part backwards in the same place");
            expect (project.toJson().find ("\"reverse\": true") != std::string::npos, "the song saves the reversed clip");
            expect (view.reverseClip (ref) && ! clips (t)[0].reverse, "reversing again plays forwards");
            expect (view.setClipFadeShape (ref, true, mr::FadeShape::exponential) && clips (t)[0].fadeInShape == mr::FadeShape::exponential
                        && project.toJson().find ("\"fadeInShape\": \"exponential\"") != std::string::npos,
                    "fade-in shape");
            expect (! view.setClipFadeShape (ref, true, mr::FadeShape::exponential), "the same shape is no edit");

            // 게인 곡선: 몸통을 누르면 점을 더하고, 끌어 옮기고, ⌥ 누르기로 지운다 (클립은 옮기지 않는다)
            view.setClipGainEditing (true);
            expect (arrangement.gainCurveEditing(), "gain curve editing is on");
            const auto p0 = arrangement.gainCurvePoint (t, 0, 0.5, -6.0);
            send (arrangement, "down", p0, p0);
            send (arrangement, "up", p0, p0);
            auto lane = clips (t)[0].gainCurve.points;
            expect (lane.size() == 1 && std::abs (lane[0].beat - 0.5) < 0.05 && std::abs (lane[0].value + 6.0) < 1.5 && near (clips (t)[0].startBeat, 40.0),
                    "clicking the clip adds a gain point at the file's second and the height's dB");
            const auto q0 = arrangement.gainCurvePoint (t, 0, lane[0].beat, lane[0].value), q1 = arrangement.gainCurvePoint (t, 0, 1.0, 0.0);
            send (arrangement, "down", q0, q0);
            send (arrangement, "drag", q1, q0);
            expect (clips (t)[0].gainCurve.points.size() == 1 && std::abs (clips (t)[0].gainCurve.points[0].beat - lane[0].beat) < 1.0e-9,
                    "dragging a gain point previews without editing");
            send (arrangement, "up", q1, q0);
            lane = clips (t)[0].gainCurve.points;
            expect (lane.size() == 1 && std::abs (lane[0].beat - 1.0) < 0.05 && std::abs (lane[0].value) < 1.5, "dragging moves the gain point");
            const auto p1 = arrangement.gainCurvePoint (t, 0, 1.5, -12.0);
            send (arrangement, "down", p1, p1);
            send (arrangement, "up", p1, p1);
            lane = clips (t)[0].gainCurve.points;
            expect (lane.size() == 2 && lane[0].beat < lane[1].beat && std::abs (lane[1].value + 12.0) < 1.5, "a second point goes after the first");
            const auto r0 = arrangement.gainCurvePoint (t, 0, lane[0].beat, lane[0].value);
            send (arrangement, "down", r0, r0, juce::ModifierKeys::altModifier);
            send (arrangement, "up", r0, r0, juce::ModifierKeys::altModifier);
            expect (clips (t)[0].gainCurve.points.size() == 1 && std::abs (clips (t)[0].gainCurve.points[0].value + 12.0) < 1.5,
                    "alt-clicking a gain point removes it");
            expect (project.toJson().find ("\"gainCurve\"") != std::string::npos, "the song saves the gain curve");
            {
                // 거꾸로 재생, 페이드 모양, 게인 곡선을 그린다
                juce::Image image (juce::Image::ARGB, arrangement.getWidth(), arrangement.getHeight(), true);
                juce::Graphics g (image);
                arrangement.paintEntireComponent (g, false);
            }
            view.setClipGainEditing (false);
            expect (view.clearClipGainCurve (ref) && clips (t)[0].gainCurve.empty(), "clear the gain curve");

            // 영점에서 나누기: 1.165초는 440 Hz 의 영점이 아니다. 나눈 자리는 가까운 영점 (한 샘플 안)
            view.setZeroCrossingCuts (true);
            expect (view.zeroCrossingCuts() && view.splitClip (ref, 40.0 + 2.0 * 1.165) && clips (t).size() == 2, "split with zero crossings");
            const auto head = clips (t)[0], tail = clips (t)[1];
            const double phase = std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * tail.offsetSeconds);
            expect (std::abs (tail.offsetSeconds - 1.165) > 1.0e-4 && std::abs (tail.offsetSeconds - 1.165) < 0.0012 && std::abs (phase) < 0.035
                        && std::abs (tail.startBeat - (40.0 + 2.0 * tail.offsetSeconds)) < 1.0e-5 && near (*head.lengthSeconds, tail.offsetSeconds),
                    "the cut moves to the nearest zero crossing and both halves meet there");
            view.setZeroCrossingCuts (false);
            // 맞붙은 두 조각의 크로스페이드: 앞뒤 10 ms 씩 늘이고 같은 소리라 S자
            expect (view.nextClip (ref) && view.nextClip (ref)->clip == 1 && view.crossfadeClip (ref), "crossfade the two halves");
            const auto xa = clips (t)[0], xb = clips (t)[1];
            expect (std::abs (*xa.lengthSeconds - (*head.lengthSeconds + 0.01)) < 1.0e-5 && std::abs (xb.offsetSeconds - (tail.offsetSeconds - 0.01)) < 1.0e-5
                        && std::abs (xa.fadeOutSeconds - 0.02) < 1.0e-5 && std::abs (xb.fadeInSeconds - 0.02) < 1.0e-5
                        && xa.fadeOutShape == mr::FadeShape::sine && xb.fadeInShape == mr::FadeShape::sine,
                    "the crossfade lengthens both halves into each other with sine fades");
            expect (! view.crossfadeClip ({ t, 1 }), "the last clip has nothing after it");
        }

        expect (project.toJson().find ("\"instrument\": \"audio\"") != std::string::npos, "the song saves the audio track");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing every audio edit restores the file text");
    }

    // 마커 띠와 레인의 구간: 마커 더하기(두 번 누르기와 이름 칸), 끌어 옮기기, 누르면 그 자리로, 구간 고르기(끌기), 복사와 붙이기,
    // 복제, 내용 지우기, 시간 넣기와 지우기. 모두 실행 취소로 돌아온다
    template <typename Expect>
    static void runArrangeTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("arrange.json");
        file.replaceWithText (R"({ "tempo": 120, "render": { "endBeat": 40 }, "tracks": [
            { "name": "Keys", "instrument": "synth.piano", "cc1": [ { "beat": 0, "value": 40 }, { "beat": 8, "value": 100 } ],
              "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 }, { "pitch": 62, "startBeat": 1, "durationBeat": 1, "velocity": 90 },
                         { "pitch": 64, "startBeat": 4, "durationBeat": 2, "velocity": 90 }, { "pitch": 67, "startBeat": 9, "durationBeat": 1, "velocity": 90 } ] },
            { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 8, "velocity": 90 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "arrange project opened");
            return;
        }
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        view.showBars (1.0, 16.0);
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
            else if (k == "double") target.mouseDoubleClick (e);
        };
        const float stripY = (float) ArrangementPanel::timeRulerHeight + ArrangementPanel::markerHeight * 0.5f;
        auto stripAt = [&] (double beat) { return juce::Point<float> (arrangement.lanePoint (0, beat).x, stripY); };
        auto notesOf = [&project] (size_t t)
        {
            juce::StringArray out;
            for (const auto& n : project.composition().tracks[t].notes)
                out.add (juce::String (n.pitch) + "@" + juce::String (n.startBeat) + "+" + juce::String (n.durationBeat));
            return out.joinIntoString (" ");
        };

        // 마커: 마커 띠를 두 번 누르면 가까운 마디에 더하고 이름 칸이 열린다
        send (arrangement, "double", stripAt (16.6), stripAt (16.6));
        expect (project.markers().size() == 1 && project.markers()[0].beat == 16.0, "double-clicking the marker strip adds a marker at the bar");
        if (auto* ed = arrangement.renameEditor())
        {
            ed->setText (juce::String::fromUTF8 ("후렴"));
            arrangement.finishRename (true);
        }
        expect (project.markers().size() == 1 && project.markers()[0].name == "후렴", "the new marker's name field renames it");
        expect (view.addMarker (0.0, juce::String::fromUTF8 ("도입"), false) && project.markers().size() == 2 && project.markers()[0].name == "도입",
                "markers stay in beat order");
        const auto m0 = stripAt (16.2), m1 = stripAt (24.0);
        send (arrangement, "down", m0, m0);
        send (arrangement, "drag", m1, m0);
        expect (project.markers()[1].beat == 16.0, "dragging a marker previews without editing");
        send (arrangement, "up", m1, m0);
        expect (project.markers()[1].beat == 24.0, "dropping a marker moves it, snapped to the beat");
        view.setPlayheadSeconds (0.0);
        send (arrangement, "down", stripAt (24.3), stripAt (24.3));
        send (arrangement, "up", stripAt (24.3), stripAt (24.3));
        expect (view.selectedTrack() == 0 && view.goToMarker (-1) && ! view.goToMarker (-1), "clicking a marker goes there, the previous marker is the first");
        view.setPlayheadSeconds (1.0);   // 2박
        expect (view.goToMarker (1) && ! view.goToMarker (1), "next marker goes to the following marker and stops at the last");
        expect (project.toJson().find ("\"markers\"") != std::string::npos, "markers are saved in the song");

        // 구간 고르기: 레인을 끌면 트랙 범위와 박 범위 (마디에 맞춘다)
        const auto a = arrangement.lanePoint (0, 0.5), b = arrangement.lanePoint (1, 7.5);
        send (arrangement, "down", a, a);
        send (arrangement, "drag", b, a);
        send (arrangement, "up", b, a);
        const auto range = arrangement.range();
        expect (range && range->firstTrack == 0 && range->lastTrack == 1 && range->start == 0.0 && range->end == 8.0,
                "dragging across lanes selects both tracks and whole bars");
        send (arrangement, "down", a, a);
        send (arrangement, "up", a, a);
        expect (! arrangement.range(), "a click in a lane drops the range");
        arrangement.setRange (ArrangementPanel::Range { 0, 1, 0.0, 8.0 });

        // 복제: 구간 바로 뒤에 (그 자리의 9박 음은 지우고), 붙인 구간을 고른다
        expect (view.duplicateRange(), "duplicate the range");
        expect (notesOf (0) == "60@0+1 62@1+1 64@4+2 60@8+1 62@9+1 64@12+2" && notesOf (1) == "36@0+8 36@8+8",
                "the duplicate overwrites the bars after the range");
        expect (arrangement.range() && arrangement.range()->start == 8.0 && arrangement.range()->end == 16.0, "the pasted range is selected");
        const auto& cc1 = project.composition().tracks[0].automation.at (1);
        expect (std::abs (cc1.valueAt (10.0) - cc1.valueAt (2.0)) < 1.0e-6 && std::abs (cc1.valueAt (7.9) - 99.25) < 0.01,
                "the curve is copied into the duplicate and kept before it");

        // 복사와 붙이기 (고른 구간이 없으면 재생 위치에, 고른 트랙부터)
        arrangement.setRange (ArrangementPanel::Range { 0, 0, 4.0, 8.0 });
        expect (view.copyRange (false), "copy the range");
        arrangement.setRange (std::nullopt);
        view.selectTrack (0);
        view.setPlayheadSeconds (16.2 * 0.5);   // 16.2박: 격자에 맞춰 16박
        expect (view.pasteRange() && notesOf (0).endsWith ("64@16+2"), "paste goes to the playhead snapped to the grid");
        expect (view.clearRange() && notesOf (0).endsWith ("64@12+2"), "clearing the pasted range removes its notes");

        // 박자 바꾸기: 3마디(8박)부터 3/4 이면 4마디는 11박, 마디 넣기는 넣는 자리 앞의 박자, 지우면 돌아온다
        const auto meterBefore = project.toJson();
        expect (view.setMeterAt (9.5, std::make_pair (3, 4)) && project.composition().meterChanges.size() == 1
                    && project.composition().meterChanges[0].beat == 8.0 && project.composition().meterChanges[0].beatsPerBar == 3,
                "a meter change starts at the playhead's bar");
        expect (MainComponent::describeBeats (11.0, project.meters()) == juce::String::fromUTF8 ("4마디")
                    && MainComponent::describeBeats (12.0, project.meters()) == "4.2",
                "bars and beats follow the new meter");
        expect (view.setMeterAt (8.0, std::make_pair (6, 8)) && project.composition().meterChanges.size() == 1
                    && project.composition().meterChanges[0].beatsPerBar == 6, "choosing another meter at the same bar replaces it");
        expect (view.setMeterAt (8.0, std::nullopt) && project.composition().meterChanges.empty(), "the change can be removed");
        expect (view.setMeterAt (0.0, std::make_pair (3, 4)) && project.composition().beatsPerBar == 3 && project.composition().meterChanges.empty(),
                "at the start it changes the song's first meter");
        while (project.toJson() != meterBefore && project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == meterBefore, "meter edits undo");

        // 시간 넣기와 지우기: 곡 전체 (마커도)
        const double markerBefore = project.markers()[1].beat;
        expect (view.insertTime (4.0, 4.0) && notesOf (0).startsWith ("60@0+1 62@1+1 64@8+2") && project.markers()[1].beat == markerBefore + 4.0,
                "inserting time pushes notes and markers");
        expect (view.deleteTime (4.0, 8.0) && notesOf (0).startsWith ("60@0+1 62@1+1 64@4+2") && project.markers()[1].beat == markerBefore,
                "deleting the same time brings them back");

        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing every arrangement edit restores the file text");

        // MIDI 가져오기: 내보낸 MIDI 를 끌어 놓으면 트랙으로 더하고 (편집 하나), 새 곡으로도 가져온다
        const auto midi = dir.getChildFile ("arrange.mid");
        mr::exportMidi (project.composition(), midi, false, std::nullopt);
        juce::String error;
        view.filesDropped ({ midi.getFullPathName() }, 400, 200);
        expect (project.trackCount() == 4 && project.composition().tracks[2].name == "Keys 2" && project.composition().tracks[3].notes.size() == 1,
                "dropping a MIDI file adds its tracks with fresh names");
        expect (view.selectedTrack() == 2, "the first imported track is selected");
        project.undoManager().undo();
        expect (project.toJson() == original, "one undo removes the imported tracks");
        const auto song = dir.getChildFile ("from-midi.json");
        expect (view.importMidiAsSong (midi, song, error) && project.file() == song && project.trackCount() == 2
                    && project.composition().tracks[0].notes.size() == 4,
                "a MIDI file becomes a new song that opens");
        expect (! view.importMidiAsSong (midi, song, error) && error.isNotEmpty(), "importing never replaces an existing song");

        // 자동 저장: 저장하지 않은 편집의 사본을 쓰고, 다시 열면 되살리고 (편집 하나), 저장하면 지운다
        const auto autosaves = dir.getChildFile ("autosave");
        view.setAutosaveDirectory (autosaves);
        expect (! view.autosaveNow() && ! view.autosaveFile().existsAsFile(), "nothing is written without unsaved edits");
        expect (view.addMarker (0.0, juce::String::fromUTF8 ("되살릴 마커"), false) && view.autosaveNow() && view.autosaveFile().existsAsFile(),
                "an unsaved edit is written to the autosave folder");
        expect (! song.getParentDirectory().getChildFile (song.getFileNameWithoutExtension() + ".autosave.json").exists()
                    && view.autosaveFile().getParentDirectory() == autosaves,
                "the copy is not written next to the song");
        const auto copy = view.autosaveFile();
        MainComponent reopened (false, false);
        reopened.setSize (1500, 940);
        reopened.setAutosaveDirectory (autosaves);
        song.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::minutes (5));
        expect (reopened.openProject (song) && reopened.hasNewerAutosave(), "reopening the song finds the newer copy");
        expect (reopened.restoreAutosave() && reopened.project().markers().size() == 1 && reopened.project().hasUnsavedEdits(),
                "restoring brings the edit back as an unsaved edit");
        reopened.project().undoManager().undo();
        expect (reopened.project().markers().empty(), "undo goes back to the file's content");
        reopened.project().undoManager().redo();
        expect (reopened.saveProject() && ! copy.existsAsFile(), "saving removes the copy");
    }

    // 피아노 롤에 마우스를 흉내 내 보내고 프로젝트가 어떻게 바뀌는지 본다 (창 없이)
    // 자판 연주: 자판의 상태를 흉내 내 누르고 떼어 본다
    template <typename Expect>
    static void runTypingTest (Expect& expect)
    {
        TypingKeyboard keys;
        std::vector<std::pair<int, bool>> events;
        keys.onNote = [&events] (int pitch, bool on) { events.push_back ({ pitch, on }); };
        std::set<int> down;
        auto state = [&down] (int code) { return down.count (code) > 0; };
        auto press = [&] (std::initializer_list<int> codes) { for (int c : codes) down.insert (c); keys.update (state); };
        auto release = [&] (std::initializer_list<int> codes) { for (int c : codes) down.erase (c); keys.update (state); };
        using Events = std::vector<std::pair<int, bool>>;

        expect (TypingKeyboard::offsetOf ('z') == 0 && TypingKeyboard::offsetOf ('S') == 1 && TypingKeyboard::offsetOf ('M') == 11
                    && TypingKeyboard::offsetOf ('/') == 16 && TypingKeyboard::offsetOf ('Q') == 12 && TypingKeyboard::offsetOf ('2') == 13
                    && TypingKeyboard::offsetOf ('U') == 23 && TypingKeyboard::offsetOf (']') == 31 && TypingKeyboard::offsetOf ('A') == -1
                    && TypingKeyboard::offsetOf ('1') == -1 && TypingKeyboard::offsetOf ('4') == -1,
                "typing keys: Z row white, S row black, Q row an octave up, number row black");
        press ({ 'Z', 'C', 'B' });
        expect (events == Events { { 60, true }, { 64, true }, { 67, true } }, "Z C B play a C major chord from C4");
        events.clear();
        release ({ 'C' });
        expect (events == Events { { 64, false } }, "releasing one key stops only its note");
        events.clear();
        press ({ ',', 'Q' });
        release ({ ',' });
        expect (events == Events { { 72, true } }, "two keys on the same pitch sound it once and keep it while one is held");
        events.clear();
        release ({ 'Q', 'Z', 'B' });
        expect (events.size() == 3 && events[0].second == false && events[1].second == false && events[2].second == false, "releasing the rest stops them");
        events.clear();
        keys.setBase (72);
        press ({ 'Z' });
        keys.setBase (84);
        release ({ 'Z' });
        expect (events == Events { { 72, true }, { 72, false } }, "a key held across an octave change stops the note it started");
        keys.setBase (65);
        expect (keys.base() == 60, "the octave base is a C");
        const auto labels = keys.labels();
        expect (labels.at (60) == "Z" && labels.at (61) == "S" && labels.at (72) == ", Q" && labels.at (91) == "]", "key labels for the piano roll");
        expect (keys.keyPressed (juce::KeyPress ('q'), nullptr) && keys.keyPressed (juce::KeyPress ('x'), nullptr),
                "Q and X play notes instead of their shortcuts while typing is on");
        expect (! keys.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0), nullptr), "command keys stay shortcuts");
        expect (! keys.keyPressed (juce::KeyPress ('a'), nullptr), "keys outside the layout stay shortcuts");
        keys.setEnabled (false);
        expect (! keys.keyPressed (juce::KeyPress ('q'), nullptr), "typing off leaves Q to its shortcut");

        // macOS: 운영체제가 먼저 받은 물리 자판 (한글 입력기와 상관없이, 받은 이벤트는 앱에 넘기지 않아 경고음이 없다)
        expect (TypingKeyboard::asciiForVirtualKey (0x06) == 'Z' && TypingKeyboard::asciiForVirtualKey (0x01) == 'S'
                    && TypingKeyboard::asciiForVirtualKey (0x0C) == 'Q' && TypingKeyboard::asciiForVirtualKey (0x13) == '2'
                    && TypingKeyboard::asciiForVirtualKey (0x2B) == ',' && TypingKeyboard::asciiForVirtualKey (0x1E) == ']'
                    && TypingKeyboard::asciiForVirtualKey (0x00) == 0 && TypingKeyboard::asciiForVirtualKey (0x31) == 0,
                "physical key positions map to the layout (A and space are not used)");
        TypingKeyboard native;
        native.setNativeKeys (true);
        events.clear();
        native.onNote = [&events] (int pitch, bool on) { events.push_back ({ pitch, on }); };
        expect (native.nativeKey ('Z', true, false) && events == Events { { 60, true } }, "a physical key plays and is taken from the app");
        expect (native.nativeKey ('Z', true, true) && events.size() == 1, "key repeat is taken without playing again");
        native.update();
        expect (events.size() == 1, "the timer keeps a physical key that is held");
        expect (! native.keyPressed (juce::KeyPress ('z'), nullptr), "with physical keys the JUCE listener stays out");
        expect (native.nativeKey ('Z', false, false) && events == Events { { 60, true }, { 60, false } }, "releasing the key stops it and is taken");
        expect (! native.nativeKey ('A', true, false), "keys outside the layout go to the app");
        expect (! native.nativeKey ('Q', false, false), "a release whose press the app saw goes to the app");
        native.setEnabled (false);
        expect (! native.nativeKey ('Z', true, false), "typing off gives the keys to the app");
    }

    // 코드와 스케일 넣기: 메뉴가 표의 모든 항목을 담는지, 넣은 음, 자리바꿈, 방향과 옥타브, 실행 취소
    // 녹음기의 결과를 트랙에 넣기, 반복 구간 편집(눈금자 끌기와 메뉴), MIDI 입력 넣기
    template <typename Expect>
    static void runRecordTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("record.json");
        file.replaceWithText (R"({ "tempo": 120, "metadata": { "author": "test" }, "tracks": [ { "name": "Keys", "instrument": "synth.piano",
            "cc64": [ { "beat": 0, "value": 0, "curve": "step" } ],
            "notes": [ { "pitch": 48, "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "record project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();

        // 녹음기 -> 편집 하나
        mr::MidiRecorder recorder;
        recorder.start ({}, 4.0);
        recorder.noteOn (4.0, 60, 100);
        recorder.noteOn (4.5, 64, 80);
        recorder.noteOff (5.0, 60);
        recorder.controller (4.0, 64, 127);
        recorder.controller (6.0, 64, 0);
        const auto take = recorder.finish (6.0);
        mr::NoteSelection selected;
        juce::String error;
        expect (MainComponent::applyTake (project, 0, take, selected, error), "take applied as one edit");
        const auto& track = project.composition().tracks[0];
        expect (track.notes.size() == 3 && track.notes[1].pitch == 60 && track.notes[2].pitch == 64 && ! track.notes[2].humanize
                    && std::abs (track.notes[2].durationBeat - 1.5) < 1.0e-9,
                "recorded notes are in the track in start order, the held note ends at the finish");
        expect (selected == mr::NoteSelection { 1, 2 }, "recorded notes are the selection");
        expect (track.automation.count (64) == 1 && track.automation.at (64).points.size() == 3
                    && track.automation.at (64).points[1].value == 127.0 && track.automation.at (64).points[1].curve == mr::CurveShape::step,
                "pedal points are merged into the existing lane");
        expect (project.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("녹음")), "recording is one undo step");
        project.undoManager().undo();
        expect (project.toJson() == original, "undo removes the recording");

        // 반복 구간 편집 (metadata 의 다른 필드는 남는다)
        expect (! project.loopRange().has_value(), "no loop at first");
        expect (view.editLoop (std::make_pair (4.0, 12.0)) && project.loopRange() == std::make_pair (4.0, 12.0), "loop edit sets metadata.loop");
        expect (project.toJson().find ("\"author\": \"test\"") != std::string::npos, "other metadata survives");
        expect (! view.looping() && view.setLooping (true) && view.looping(), "looping can be turned on once a loop exists");
        expect (view.editLoop (std::nullopt) && ! project.loopRange().has_value(), "loop edit can remove the loop");
        expect (! view.looping(), "removing the loop turns looping off");
        expect (! view.setLooping (true), "looping refuses without a loop");
        project.undoManager().undo();
        expect (project.loopRange() == std::make_pair (4.0, 12.0), "undo restores the loop");
        project.undoManager().undo();
        expect (project.toJson() == original, "undo removes the loop and leaves the text as it was");

        // 눈금자 끌기: 트랙 보기에서 ⌥ 로 새 구간, 띠의 끝을 끌어 고치기, 피아노 롤에서도
        view.showBars (1.0, 8.0);
        auto& roll = view.pianoRoll();
        auto& arrangement = view.arrangement();
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {})
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
        };
        auto rulerX = [&roll] (double beat) { return roll.notePoint (beat, 60).x; };   // 가로축은 두 보기가 같다
        const juce::ModifierKeys alt (juce::ModifierKeys::altModifier);
        const juce::Point<float> a0 (rulerX (4.0), 10.0f), a1 (rulerX (12.1), 10.0f);
        send (arrangement, "down", a0, a0, alt);
        send (arrangement, "drag", a1, a0, alt);
        expect (! project.loopRange().has_value(), "dragging previews without editing");
        send (arrangement, "up", a1, a0, alt);
        expect (project.loopRange() == std::make_pair (4.0, 12.0), "alt-dragging the ruler sets the loop snapped to beats");
        const juce::Point<float> e0 (rulerX (12.0) + 2.0f, 10.0f), e1 (rulerX (16.0), 10.0f);
        send (arrangement, "down", e0, e0);
        send (arrangement, "drag", e1, e0);
        send (arrangement, "up", e1, e0);
        expect (project.loopRange() == std::make_pair (4.0, 16.0), "dragging the band's end moves the loop end");
        const juce::Point<float> s0 (rulerX (20.0), 10.0f);
        send (arrangement, "down", s0, s0);
        send (arrangement, "up", s0, s0);
        expect (project.loopRange() == std::make_pair (4.0, 16.0), "clicking the ruler elsewhere only seeks");
        const juce::Point<float> r0 (rulerX (8.0), 10.0f), r1 (rulerX (20.0), 10.0f);
        send (roll, "down", r0, r0, alt);
        send (roll, "drag", r1, r0, alt);
        send (roll, "up", r1, r0, alt);
        expect (project.loopRange() == std::make_pair (8.0, 20.0), "alt-dragging the piano roll ruler sets the loop");

        // MIDI 입력: 장치 없이 넣은 메시지가 받은 위치와 함께 온다. 재생기가 없으면 녹음은 시작하지 않는다
        MidiInputs inputs;
        std::vector<MidiInputs::Event> events;
        inputs.positionSource = [] { return (int64_t) 4800; };
        inputs.onEvent = [&events] (const MidiInputs::Event& e) { events.push_back (e); };
        inputs.inject (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
        expect (events.size() == 1 && events[0].message.isNoteOn() && events[0].position == 4800, "injected MIDI events carry the position");
        view.liveMidi (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        view.liveMidi (juce::MidiMessage::noteOff (1, 60), 0);
        expect (! view.startRecording() && ! view.isRecording(), "recording needs a player");
        expect (! view.stopRecording(), "stopping without a recording does nothing");

        // 드럼과 타악기 트랙은 피아노 롤이 건반 이름을 쓴다 (악기 정의의 keyNames)
        const auto drums = dir.getChildFile ("drums.json");
        drums.replaceWithText (R"({ "tempo": 100, "tracks": [
            { "name": "Kit", "instrument": "drum-kit", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 100 } ] },
            { "name": "Synth kit", "instrument": "synth.drums", "notes": [ { "pitch": 38, "startBeat": 0, "durationBeat": 0.5, "velocity": 100 } ] },
            { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ] } ] })");
        MainComponent drumView (false);
        drumView.setSize (1500, 940);
        if (! drumView.openProject (drums))
        {
            expect (false, "drum project opened");
            return;
        }
        drumView.selectTrack (0);
        const auto kitNames = drumView.pianoRoll().keyNames();
        expect (kitNames.count (36) == 1 && kitNames.at (36) == juce::String::fromUTF8 ("킥") && kitNames.count (46) == 1
                    && kitNames.at (46) == juce::String::fromUTF8 ("열린 하이햇"),
                "the drum kit's keys are named in the piano roll");
        drumView.selectTrack (1);
        expect (drumView.pianoRoll().keyNames().count (38) == 1 && drumView.pianoRoll().keyNames().at (38) == juce::String::fromUTF8 ("스네어"),
                "the built-in drum synth's keys are named too");
        drumView.selectTrack (2);
        expect (drumView.pianoRoll().keyNames().empty(), "a piano track has no key names");
    }

    template <typename Expect>
    static void runHarmonyTest (const juce::File& dir, Expect& expect)
    {
        expect (missingHarmonyLabels().isEmpty(), "every chord has a Korean name and every scale an English one");
        expect (scaleLabel ("dorian") == "Dorian" && scaleGroupLabel ("pentatonic") == "Pentatonic", "scales are named in English");
        const auto file = dir.getChildFile ("harmony.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [
            { "pitch": 48, "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "harmony project opened");
            return;
        }
        view.selectTrack (0);
        auto& roll = view.pianoRoll();
        auto& project = view.project();
        auto leaves = [] (const juce::PopupMenu& menu)
        {
            int actions = 0, subMenus = 0;
            for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
            {
                const auto& item = it.getItem();
                if (item.subMenu != nullptr)
                    ++subMenus;
                else if (item.action != nullptr)
                    ++actions;
            }
            return std::make_pair (actions, subMenus);
        };
        // 화음 메뉴: 화음마다 하나와 설정 8개 (자리바꿈 4, 길이 4), 음계 메뉴: 음계마다 하나와 설정 6개 (방향 3, 옥타브 3)
        expect (leaves (roll.insertMenu (true, 64, 8.0)).first == (int) mr::chordTypes().size() + 8, "the chord menu holds every chord");
        expect (leaves (roll.insertMenu (false, 64, 8.0)).first == (int) mr::scaleTypes().size() + 6, "the scale menu holds every scale");
        expect (roll.insertMenu (true, std::nullopt, 0.0).getNumItems() == 12, "the menu bar asks for one of twelve roots");

        auto options = roll.harmonyOptions();
        const size_t before = project.composition().tracks[0].notes.size();
        expect (roll.insertChord (64, "m7", 8.0), "Em7 inserted");
        const auto& notes = project.composition().tracks[0].notes;
        expect (notes.size() == before + 4 && roll.selectionSize() == 4, "four notes inserted and selected");
        std::vector<int> pitches;
        for (const auto& n : notes)
            if (std::abs (n.startBeat - 8.0) < 1.0e-9 && std::abs (n.durationBeat - 4.0) < 1.0e-9)
                pitches.push_back (n.pitch);
        std::sort (pitches.begin(), pitches.end());
        expect (pitches == std::vector<int> ({ 64, 67, 71, 74 }), "Em7 is E G B D for one bar");
        project.undoManager().undo();
        expect (project.composition().tracks[0].notes.size() == before, "undo removes the chord");

        options->inversion = 1;
        options->chordLength = HarmonyOptions::ChordLength::beat;
        expect (roll.insertChord (64, "m7", 8.0), "first inversion inserted");
        pitches.clear();
        for (const auto& n : project.composition().tracks[0].notes)
            if (std::abs (n.startBeat - 8.0) < 1.0e-9 && std::abs (n.durationBeat - 1.0) < 1.0e-9)
                pitches.push_back (n.pitch);
        std::sort (pitches.begin(), pitches.end());
        expect (pitches == std::vector<int> ({ 67, 71, 74, 76 }), "first inversion puts E on top");
        project.undoManager().undo();

        expect (roll.insertScale (62, "dorian", 16.0), "D Dorian inserted");
        const double step = roll.grid();
        int found = 0;
        for (const auto& n : project.composition().tracks[0].notes)
            if (n.startBeat >= 16.0 - 1.0e-9)
                ++found;
        expect (found == 8 && roll.selectionSize() == 8, "a one-octave scale is eight notes");
        bool second = false;
        for (const auto& n : project.composition().tracks[0].notes)
            second = second || (n.pitch == 64 && std::abs (n.startBeat - (16.0 + step)) < 1.0e-9 && std::abs (n.durationBeat - step) < 1.0e-9);
        expect (second, "scale notes follow the grid");
        project.undoManager().undo();
        options->direction = mr::ScaleDirection::upDown;
        options->octaves = 2;
        expect (roll.insertScale (60, "major", 0.0), "two-octave scale up and down inserted");
        expect (roll.selectionSize() == 29, "two octaves up and down are 29 notes");
        project.undoManager().undo();
        expect (! roll.insertChord (64, "no-such-chord", 0.0), "an unknown chord inserts nothing");
    }

    // 퀵 샘플러: 오디오 파일이 곡 폴더의 samples/ 로 복사되고, 높이를 잰 SFZ 와 곡 안의 악기 정의와 트랙이 편집 하나로 더해진다
    template <typename Expect>
    static void runSamplerTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("sampler-song");
        songDir.createDirectory();
        const auto outside = dir.getChildFile ("Bell Tone.wav");
        {
            juce::AudioBuffer<float> b (1, 44100);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.63 * i / 44100.0));
            mr::writeWav (outside, b, b.getNumSamples(), 44100.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "sampler project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        view.selectTrack (0);
        juce::String error;
        expect (view.makeSamplerTrack (outside, 0.0, std::nullopt, false, error) && project.trackCount() == 2, "a sampler track is added");
        const auto& tracks = project.composition().tracks;
        expect (tracks.size() == 2 && tracks[1].instrument == "sampler.bell-tone" && tracks[1].name == "Bell Tone", "the track plays the new instrument");
        const auto sfz = songDir.getChildFile ("samples/bell-tone.sfz");
        expect (songDir.getChildFile ("samples/Bell Tone.wav").existsAsFile() && sfz.existsAsFile(), "the sound and its SFZ are in the song folder");
        expect (sfz.loadFileAsString().contains ("pitch_keycenter=60") && sfz.loadFileAsString().contains ("sample=Bell Tone.wav"),
                "the SFZ is tuned to the measured pitch (C4) and points next to it");
        expect (juce::String (project.toJson()).contains ("\"sampler.bell-tone\"") && juce::String (project.toJson()).contains ("samples/bell-tone.sfz"),
                "the instrument is defined in the song");
        expect (view.makeSamplerTrack (outside, 0.25, 0.5, true, error) && tracks.size() == 3 && tracks[2].instrument == "sampler.bell-tone-2",
                "a second instrument from a part of the file");
        const auto part = songDir.getChildFile ("samples/bell-tone-2.sfz").loadFileAsString();
        expect (part.contains ("offset=11025") && part.contains ("end=33074") && part.contains ("loop_mode=one_shot"), "the part and one-shot are in the SFZ");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "sampler tracks undo");
    }

    // 멜로디 받아 적기: 클립이 재생하는 파일 구간의 음이 곡의 박으로 새 트랙에 들어간다 (편집 하나)
    template <typename Expect>
    static void runTranscribeTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("transcribe-song");
        songDir.createDirectory();
        // 파일: 0.1~0.6초 A4, 0.7~1.2초 C5, 1.3~1.8초 E5 (배음 셋)
        {
            juce::AudioBuffer<float> b (1, 96000);
            b.clear();
            const double tones[3][3] = { { 0.1, 0.6, 69 }, { 0.7, 1.2, 72 }, { 1.3, 1.8, 76 } };
            for (const auto& t : tones)
            {
                const double f = 440.0 * std::pow (2.0, (t[2] - 69.0) / 12.0);
                for (int i = (int) (t[0] * 48000.0); i < (int) (t[1] * 48000.0); ++i)
                {
                    const double x = 2.0 * juce::MathConstants<double>::pi * f * i / 48000.0;
                    b.setSample (0, i, (float) (0.2 * (std::sin (x) + 0.5 * std::sin (2.0 * x) + 0.3 * std::sin (3.0 * x))));
                }
            }
            mr::writeWav (songDir.getChildFile ("hum.wav"), b, b.getNumSamples(), 48000.0, 24);
        }
        // 2 박(1초)에 놓은 클립이 파일의 0.65초부터 1.3초를 재생한다
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Voice", "instrument": "audio",
            "clips": [ { "file": "hum.wav", "startBeat": 2, "offsetSeconds": 0.65, "lengthSeconds": 1.3, "pitchSemitones": 0 } ] },
            { "name": "Keys", "instrument": "synth.piano" } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "transcribe project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        juce::String error;
        expect (view.transcribeClip ({ 0, 0 }, error) && project.trackCount() == 3, "a melody track is added under the clip");
        const auto& tracks = project.composition().tracks;
        if (tracks.size() == 3)
        {
            const auto& notes = tracks[1].notes;
            expect (tracks[1].name == "hum melody" && notes.size() == 2, "only the notes the clip plays are written");
            if (notes.size() == 2)
            {
                // C5 는 파일의 0.7초 = 곡의 1.05초 = 2.1 박, E5 는 1.3초 = 1.65초 = 3.3 박
                expect (notes[0].pitch == 72 && notes[1].pitch == 76, "the pitches are written");
                expect (std::abs (notes[0].startBeat - 2.1) < 0.05 && std::abs (notes[1].startBeat - 3.3) < 0.05
                            && std::abs (notes[1].durationBeat - 1.0) < 0.06, "the notes sit where the clip plays them");
                expect (! notes[0].humanize, "transcribed notes are not humanized again");
            }
        }
        // 클립이 반음 둘 올리면 들리는 높이로
        project.undoManager().undo();
        expect (project.toJson() == original, "transcribing undoes");
        auto clip = project.composition().tracks[0].clips[0];
        clip.pitchSemitones = 2.0;
        if (view.editClip ({ 0, 0 }, clip) && view.transcribeClip ({ 0, 0 }, error))
            expect (project.composition().tracks[1].notes.size() == 2 && project.composition().tracks[1].notes[0].pitch == 74, "a transposed clip is written as heard");
        else
            expect (false, "transcribe a transposed clip");
        // 높이가 없는 소리
        {
            juce::AudioBuffer<float> b (1, 48000);
            juce::Random r (5);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, r.nextFloat() * 0.4f - 0.2f);
            mr::writeWav (songDir.getChildFile ("noise.wav"), b, b.getNumSamples(), 48000.0, 24);
        }
        clip.file = "noise.wav";
        clip.offsetSeconds = 0.0;
        clip.lengthSeconds = std::nullopt;
        const auto before = project.trackCount();
        expect (view.editClip ({ 0, 0 }, clip) && ! view.transcribeClip ({ 0, 0 }, error) && error.isNotEmpty() && project.trackCount() == before,
                "noise gives no notes and no track");
    }

    // 어택으로 조각내기와 템포 재기: 96 BPM 한 마디 루프 클립
    template <typename Expect>
    static void runSliceTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("slice-song");
        songDir.getChildFile ("audio").createDirectory();
        // 0.625초(96 BPM 의 한 박)마다 잡음 두드림 넷과 그 사이 8분음표의 여린 두드림: 한 마디 2.5초
        std::vector<double> hits;
        {
            juce::AudioBuffer<float> b (1, 120000);
            b.clear();
            juce::Random r (11);
            for (int k = 0; k < 8; ++k)
            {
                const double at = k * 0.3125;
                hits.push_back (at);
                const int start = (int) std::lround (at * 48000.0);
                for (int i = 0; i < 4000; ++i)
                    b.setSample (0, start + i, (float) ((k % 2 == 0 ? 0.5 : 0.2) * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 48000.0 * 70.0)
                                                          * std::min (1.0, (4000 - i) / 480.0)));
            }
            mr::writeWav (songDir.getChildFile ("audio/Break.wav"), b, b.getNumSamples(), 48000.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Loop", "instrument": "audio", "clips": [ { "file": "audio/Break.wav", "startBeat": 4 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "slice project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        juce::String error;
        expect (view.sliceClip ({ 0, 0 }, error) && project.trackCount() == 2, "slicing adds an instrument track");
        const auto& tracks = project.composition().tracks;
        if (tracks.size() == 2)
        {
            const auto& notes = tracks[1].notes;
            expect (tracks[1].instrument == "sampler.break-slices" && notes.size() == hits.size(), "one note per hit");
            bool placed = notes.size() == hits.size();
            for (size_t k = 0; placed && k < notes.size(); ++k)
                placed = notes[k].pitch == 36 + (int) k && std::abs (notes[k].startBeat - (4.0 + hits[k] * 2.0)) < 0.01 && notes[k].velocity == 127 && ! notes[k].humanize;
            expect (placed, "each slice plays where the clip played its hit (120 BPM)");
            const auto sfz = songDir.getChildFile ("samples/break-slices.sfz").loadFileAsString();
            expect (sfz.contains ("sample=../audio/Break.wav") && sfz.contains ("<region> key=43 "), "the slices point at the clip's file in the song folder");
        }
        project.undoManager().undo();
        expect (project.toJson() == original, "slicing undoes");
        // 템포 재기: 한 마디 2.5초는 96 BPM, 클립이 곡 템포를 따른다
        if (mr::clipStretchAvailable())
        {
            expect (view.detectClipTempo ({ 0, 0 }, error), "the clip's tempo is measured");
            const auto& clip = project.composition().tracks[0].clips[0];
            expect (clip.sourceBpm && std::abs (*clip.sourceBpm - 96.0) < 0.01, "the clip follows the song from 96 BPM");
            // 120 BPM 의 곡에서 한 마디(4 박)에 놓인다
            expect (std::abs (mr::clipEndSeconds (clip, project.tempo(), songDir) - project.tempo().secondsAt (8.0)) < 0.01, "the loop now lasts one bar of the song");
        }
    }

    // 트랙 템플릿: 저장, 목록 (기본과 내 것), 템플릿으로 트랙 더하기, 실행 취소
    template <typename Expect>
    static void runTrackTemplateTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("templates.json");
        file.replaceWithText (R"({ "tempo": 120, "buses": [ { "name": "Hall", "effects": [ { "type": "reverb" } ] } ],
            "tracks": [ { "name": "Lead", "instrument": "synth.lead", "notes": [ { "pitch": 72, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ],
                          "mixer": { "gainDb": -4 }, "sends": [ { "bus": "Hall", "levelDb": -12 } ],
                          "effects": [ { "type": "chorus" }, { "type": "delay", "timeBeats": 0.75 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "template project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        view.trackTemplateFolder = dir.getChildFile ("TrackTemplates");
        juce::String error;
        expect (view.saveTrackTemplate (0, "My lead", error) && view.trackTemplateFolder.getChildFile ("My lead.json").existsAsFile(), "a track saves as a template");
        const auto templates = view.trackTemplates();
        const auto mine = std::find_if (templates.begin(), templates.end(), [] (const auto& t) { return t.first == "My lead"; });
        expect (templates.size() == mr::factoryTrackTemplates().size() + 1 && mine != templates.end(), "the template is listed after the factory ones");
        if (mine != templates.end())
        {
            expect (view.addTrackFromTemplate (mine->second) && project.trackCount() == 2, "a track comes from the template");
            const auto& made = project.composition().tracks[1];
            expect (made.name == "Lead 2" && made.instrument == "synth.lead" && made.notes.empty() && made.effects.size() == 2 && made.sends.size() == 1
                        && std::abs (made.mixer.gainDb + 4.0) < 1.0e-9,
                    "it has the sound, effects, mixer and sends but no notes");
        }
        expect (view.addTrackFromTemplate (templates.front().second, 0) && project.composition().tracks[0].instrument == "audio"
                    && project.composition().tracks[0].effects.size() == 3,
                "a factory template (vocal) adds an audio track with its chain");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "template tracks undo");
    }

    // 곡 템포를 녹음에 맞추기: 빨라지는 연주의 박마다 템포 점, 클립은 늘이지 않고 첫 박이 마디 머리, 다른 트랙의 음은 박에 남는다
    template <typename Expect>
    static void runFollowTempoTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("follow-song");
        songDir.getChildFile ("audio").createDirectory();
        std::vector<double> hits;
        {
            juce::AudioBuffer<float> b (1, 48000 * 12);
            b.clear();
            juce::Random r (4);
            double t = 0.25;
            for (int k = 0; k < 16; ++k)
            {
                hits.push_back (t);
                const int start = (int) std::lround (t * 48000.0);
                for (int i = 0; i < 4000; ++i)
                    b.setSample (0, start + i, (float) ((k % 4 == 0 ? 0.5 : 0.3) * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 48000.0 * 70.0)));
                t += 60.0 / (100.0 + 20.0 * k / 15.0);
            }
            mr::writeWav (songDir.getChildFile ("audio/Free.wav"), b, b.getNumSamples(), 48000.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "timeSignature": [4, 4], "tracks": [ { "name": "Free", "instrument": "audio", "clips": [ { "file": "audio/Free.wav", "startBeat": 0 } ] },
            { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 8, "durationBeat": 1, "velocity": 80 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "follow project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        juce::String error;
        expect (view.followClipTempo ({ 0, 0 }, error) && project.composition().tempo.size() > 8, "the song's tempo follows the recording");
        const auto& clip = project.composition().tracks[0].clips[0];
        bool onBeats = ! clip.stretched();
        for (const double h : hits)
        {
            const double beat = project.tempo().beatAt (mr::clipSongSecondsAt (clip, project.tempo(), h));
            onBeats = onBeats && std::abs (beat - std::round (beat)) < 0.01;
        }
        expect (onBeats, "every hit of the recording falls on a beat of the song");
        expect (project.composition().tracks[1].notes[0].startBeat == 8.0, "other tracks keep their beats");
        project.undoManager().undo();
        expect (project.toJson() == original, "following the tempo undoes");
    }

    // 그루브: 기본 그루브 입히기, 음표로 저장하고 다시 읽기, 오디오 클립의 어택으로 저장, 클립을 그루브로 맞추기
    template <typename Expect>
    static void runGrooveTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("groove-song");
        songDir.getChildFile ("audio").createDirectory();
        const auto grooveDir = songDir.getChildFile ("Grooves");
        // 120 BPM 의 8분음표 여덟 타격: 엇박이 30 ms (0.06 박) 늦고 6 dB 작다
        {
            juce::AudioBuffer<float> b (1, 120000);
            b.clear();
            juce::Random r (8);
            for (int k = 0; k < 8; ++k)
            {
                const int start = (int) std::lround ((k * 0.25 + (k % 2 == 1 ? 0.03 : 0.0)) * 48000.0);
                const double amp = k % 2 == 1 ? 0.25 : 0.5;
                for (int i = 0; i < 4000; ++i)
                    b.setSample (0, start + i, (float) (amp * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 48000.0 * 70.0) * std::min (1.0, (4000 - i) / 480.0)));
            }
            mr::writeWav (songDir.getChildFile ("audio/Swung.wav"), b, b.getNumSamples(), 48000.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        juce::String notes;
        for (int k = 0; k < 8; ++k)
            notes << (k > 0 ? ", " : "") << R"({ "pitch": 42, "startBeat": )" << juce::String (k * 0.25) << R"(, "durationBeat": 0.2, "velocity": )"
                  << (k % 2 == 1 ? "70" : "100") << " }";
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Hat", "instrument": "synth.edm-drums", "notes": [ )" + notes
                              + R"( ] }, { "name": "Loop", "instrument": "audio", "clips": [ { "file": "audio/Swung.wav", "startBeat": 0 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "groove project opened");
            return;
        }
        auto& project = view.project();
        auto& roll = view.pianoRoll();
        roll.grooveFolder = grooveDir;
        const auto original = project.toJson();
        view.selectTrack (0);
        roll.deselect();
        roll.setGrid (0.25);
        auto starts = [&project] { std::vector<double> v; for (const auto& n : project.composition().tracks[0].notes) v.push_back (n.startBeat); return v; };
        expect (roll.applyGroove (*mr::findFactoryGroove ("swing-16-66"), 1.0, 1.0) && std::abs (starts()[1] - 0.33) < 1.0e-6 && std::abs (starts()[2] - 0.5) < 1.0e-6,
                "a factory swing moves the offbeat sixteenths");
        project.undoManager().undo();
        // 지금 음표(세기 100, 70)로 그루브를 저장하면 목록에 있고 강세를 낸다
        juce::String error;
        expect (roll.saveGrooveFromNotes ("Hat accents", error) && grooveDir.getChildFile ("Hat accents.json").existsAsFile(), "notes save as a groove");
        const auto grooves = availableGrooves (grooveDir);
        const auto mine = std::find_if (grooves.begin(), grooves.end(), [] (const mr::Groove& g) { return g.name == "Hat accents"; });
        expect (mine != grooves.end() && mine->steps.size() == 16 && mine->steps[0].velocity > mine->steps[1].velocity && grooveName (*mine) == "Hat accents",
                "the saved groove is listed with its accents");
        expect (roll.toolsMenu().containsAnyActiveItems(), "the tools menu with grooves");
        // 오디오 클립의 어택으로: 엇박이 늦고 작다
        const ArrangementPanel::ClipRef clip { 1, 0 };
        expect (view.saveGrooveFromClip (clip, "Swung loop", error), "a clip's attacks save as a groove");
        std::string parseError;
        const auto fromClip = mr::grooveFromJson (juce::JSON::parse (grooveDir.getChildFile ("Swung loop.json").loadFileAsString()), parseError);
        expect (fromClip && fromClip->steps.size() == 16 && std::abs (fromClip->steps[2].offset - 0.06) < 0.01 && std::abs (fromClip->steps[0].offset) < 0.01
                    && fromClip->steps[0].velocity > fromClip->steps[2].velocity * 1.2,
                "the clip groove has the late, quiet offbeats");
        // 그 그루브를 곧은 음표에 입히면 엇박 8분음표가 0.06 박 늦는다
        expect (roll.applyGroove (*fromClip, 1.0, 0.0) && std::abs (starts()[2] - 0.56) < 0.01 && std::abs (starts()[4] - 1.0) < 0.01, "the clip's feel goes onto notes");
        // 클립을 기본 그루브(곧은 8분음표 대신 60% 스윙)로 맞춘다
        if (mr::clipStretchAvailable())
        {
            const auto* swing = mr::findFactoryGroove ("swing-8-60");
            expect (view.quantizeClipAudio (clip, swing->grid, 1.0, error, *swing), "a clip quantizes to a groove");
            const auto& warped = project.composition().tracks[1].clips[0];
            bool placed = warped.warp.size() == 9;
            for (size_t k = 1; placed && k + 1 < warped.warp.size(); ++k)
                placed = std::abs (mr::warpMarkerSongBeat (warped, k) - mr::grooveTarget (*swing, 0.5 * (double) k)) < 1.0e-6;
            expect (placed, "each attack lands on the groove's place");
        }
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "groove edits undo");
    }

    // 워프 마커: 어택을 격자에 맞추기, 마커 끌기, ⌥ 누르기로 지우기, 두 번 눌러 더하기, 실행 취소
    template <typename Expect>
    static void runWarpTest (const juce::File& dir, Expect& expect)
    {
        if (! mr::clipStretchAvailable())
            return;
        const auto songDir = dir.getChildFile ("warp-song");
        songDir.getChildFile ("audio").createDirectory();
        // 120 BPM 의 8분음표 여덟 타격 (처음 말고는 ±25 ms 어긋난다)
        const double jitter[] = { 0.0, 0.021, -0.018, 0.012, -0.025, 0.017, -0.009, 0.024 };
        {
            juce::AudioBuffer<float> b (1, 120000);
            b.clear();
            juce::Random r (5);
            for (int k = 0; k < 8; ++k)
            {
                const int start = (int) std::lround ((k * 0.25 + jitter[k]) * 48000.0);
                for (int i = 0; i < 4000; ++i)
                    b.setSample (0, start + i, (float) (0.5 * (r.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 48000.0 * 70.0) * std::min (1.0, (4000 - i) / 480.0)));
            }
            mr::writeWav (songDir.getChildFile ("audio/Hits.wav"), b, b.getNumSamples(), 48000.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Hits", "instrument": "audio", "clips": [ { "file": "audio/Hits.wav", "startBeat": 4 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "warp project opened");
            return;
        }
        view.showBars (1.0, 6.0);
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        const ArrangementPanel::ClipRef ref { 0, 0 };
        auto clip = [&project] { return project.composition().tracks[0].clips[0]; };
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {}, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
            else if (k == "double") target.mouseDoubleClick (e);
        };
        juce::String error;
        expect (view.quantizeClipAudio (ref, 0.5, 1.0, error) && clip().warp.size() == 9, "quantizing writes a warp marker per attack and both ends");
        bool onGrid = clip().warp.size() == 9;
        for (size_t k = 1; onGrid && k + 1 < clip().warp.size(); ++k)
            onGrid = std::abs (mr::warpMarkerSongBeat (clip(), k) - (4.0 + 0.5 * (double) k)) < 1.0e-6;
        expect (onGrid, "each attack lands on the eighth-note grid");
        expect (std::abs (clip().startBeat - 4.0) < 1.0e-6 && juce::String (project.toJson()).contains ("\"warp\""), "the clip stays put and the markers are saved");
        project.undoManager().undo();
        expect (project.toJson() == original, "quantizing undoes");
        expect (view.quantizeClipAudio (ref, 0.5, 0.5, error), "half-strength quantize");
        expect (clip().warp.size() == 9 && std::abs (mr::warpMarkerSongBeat (clip(), 1) - (4.5 + jitter[1])) < 0.01, "half strength moves an attack half way");
        project.undoManager().undo();
        expect (view.quantizeClipAudio (ref, 0.5, 1.0, error), "quantize again");
        // 셋째 마커(5 박)를 ⌘ 끌기로 0.2 박 늦춘다: 이웃 마커는 그대로
        const auto before = clip();
        const auto m0 = arrangement.warpMarkerPoint (0, 0, 2);
        const juce::Point<float> m1 (arrangement.lanePoint (0, 5.2).x, m0.y);
        send (arrangement, "down", m0, m0, juce::ModifierKeys::commandModifier);
        send (arrangement, "drag", m1, m0, juce::ModifierKeys::commandModifier);
        send (arrangement, "up", m1, m0, juce::ModifierKeys::commandModifier);
        expect (std::abs (mr::warpMarkerSongBeat (clip(), 2) - 5.2) < 0.02 && std::abs (mr::warpMarkerSongBeat (clip(), 1) - 4.5) < 1.0e-6
                    && std::abs (mr::warpMarkerSongBeat (clip(), 3) - 5.5) < 1.0e-6 && clip().warp.size() == before.warp.size(),
                "dragging a warp marker moves that attack only");
        // 자르기 방식은 곡에 warpMode 로 남고, 지도(마커의 자리)는 그대로
        auto sliced = clip();
        sliced.warpSlices = true;
        expect (view.editClip (ref, sliced) && clip().warpSlices && juce::String (project.toJson()).contains ("\"warpMode\": \"slice\"")
                    && std::abs (mr::warpMarkerSongBeat (clip(), 2) - mr::warpMarkerSongBeat (sliced, 2)) < 1.0e-9,
                "the slice warp mode is saved");
        // ⌥ 누르기: 그 마커를 지운다
        const auto r0 = arrangement.warpMarkerPoint (0, 0, 2);
        send (arrangement, "down", r0, r0, juce::ModifierKeys::altModifier);
        send (arrangement, "up", r0, r0, juce::ModifierKeys::altModifier);
        expect (clip().warp.size() == before.warp.size() - 1 && std::abs (mr::warpMarkerSongBeat (clip(), 2) - 5.5) < 1.0e-6, "alt-click removes a warp marker");
        // 두 번 누르기: 그 자리에 마커 (소리는 그대로)
        const auto box = arrangement.clipBounds (0, 0);
        const juce::Point<float> d (arrangement.lanePoint (0, 6.25).x, box.getCentreY());
        send (arrangement, "down", d, d);
        send (arrangement, "up", d, d);
        send (arrangement, "double", d, d, {}, 2);
        expect (clip().warp.size() == before.warp.size(), "double-clicking a clip adds a warp marker");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "warp edits undo");
    }

    // 반복 녹음의 테이크와 컴핑: 반복마다 클립 (마지막만 들린다), 테이크 고르기, 고른 구간만 고르기
    template <typename Expect>
    static void runCompTest (const juce::File& dir, Expect& expect)
    {
        const auto songDir = dir.getChildFile ("comp-song");
        songDir.getChildFile ("audio").createDirectory();
        const auto takeFile = songDir.getChildFile ("audio/Vocal take 1.wav");
        {
            juce::AudioBuffer<float> b (1, 96000);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.2f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (i < 48000 ? 440.0 : 660.0) * i / 48000.0));
            mr::writeWav (takeFile, b, b.getNumSamples(), 48000.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Vocal", "instrument": "audio" } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "comp project opened");
            return;
        }
        view.showBars (1.0, 4.0);
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        auto clips = [&project] { return project.composition().tracks[0].clips; };
        // 1초(2 박)부터 1초 반복을 두 번 녹음했다: 파일의 0초와 1초
        mr::AudioTake take;
        take.file = takeFile;
        take.startPosition = 48000;
        take.samples = 96000;
        take.sampleRate = 48000.0;
        take.channels = 1;
        take.passes = { { 0, 48000, 48000 }, { 48000, 48000, 48000 } };
        juce::String error;
        expect (MainComponent::applyAudioTake (project, 0, take, 0, error) && clips().size() == 2, "a loop recording adds a clip per pass");
        expect (clips().size() == 2 && clips()[0].muted && ! clips()[1].muted && std::abs (clips()[0].startBeat - 2.0) < 1.0e-6
                    && std::abs (clips()[1].startBeat - 2.0) < 1.0e-6 && std::abs (clips()[1].offsetSeconds - 1.0) < 1.0e-6,
                "the passes stack on the loop, the last one plays");
        expect (arrangement.clipBounds (0, 0).getY() > arrangement.clipBounds (0, 1).getBottom() - 1.0f, "muted takes sit in the band under the playing clip");
        expect (view.useTake ({ 0, 0 }) && ! clips()[0].muted && clips()[1].muted, "choosing a take plays it and mutes the other");
        // 앞 반 박(2~2.5 박... 고른 구간 2~3 박 = 0.5초)만 둘째 테이크로
        arrangement.setRange (ArrangementPanel::Range { 0, 0, 2.0, 3.0 });
        expect (view.useTake ({ 0, 1 }) && clips().size() == 4, "a take in the selected range splits the clips at its edges");
        int playing = 0;
        bool secondInside = false, firstAfter = false;
        for (const auto& c : clips())
        {
            playing += c.muted ? 0 : 1;
            if (! c.muted && std::abs (c.startBeat - 2.0) < 1.0e-6 && std::abs (c.offsetSeconds - 1.0) < 1.0e-6 && c.lengthSeconds && std::abs (*c.lengthSeconds - 0.5) < 1.0e-6)
                secondInside = true;
            if (! c.muted && std::abs (c.startBeat - 3.0) < 1.0e-6 && std::abs (c.offsetSeconds - 0.5) < 1.0e-6)
                firstAfter = true;
        }
        expect (playing == 2 && secondInside && firstAfter, "the range plays the second take, the rest the first");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "comping undoes");
    }

    // 트랙의 출력(서브그룹)과 폴더: 폴더 머리 줄, 접기, 폴더를 버스로 묶기, 버스 이름 바꾸기와 지우기가 출력을 따른다, 실행 취소
    template <typename Expect>
    static void runFolderTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("folders.json");
        file.replaceWithText (R"({ "tempo": 120, "buses": [ { "name": "Verb" } ], "tracks": [
            { "name": "Kick", "instrument": "synth.edm-drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 1, "velocity": 100 } ] },
            { "name": "Snare", "instrument": "synth.edm-drums", "notes": [ { "pitch": 38, "startBeat": 1, "durationBeat": 1, "velocity": 100 } ] },
            { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "folder project opened");
            return;
        }
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        const auto& tracks = project.composition().tracks;
        expect (view.setTrackOutput ({ 2 }, "Verb") && tracks[2].output == "Verb", "a track goes to a bus");
        expect (view.setTrackFolder ({ 0, 1 }, "Drums") && tracks[0].folder == "Drums" && tracks[1].folder == "Drums", "two tracks in a folder");
        const auto header = arrangement.folderHeaderBounds ("Drums");
        expect (! header.isEmpty() && header.getBottom() <= arrangement.lanePoint (0, 1.0).y, "the folder header sits above its tracks");
        const auto keysBefore = arrangement.lanePoint (2, 1.0).y;
        arrangement.setFolderCollapsed ("Drums", true);
        expect (arrangement.trackHidden (0) && arrangement.trackHidden (1) && ! arrangement.trackHidden (2)
                    && arrangement.lanePoint (2, 1.0).y < keysBefore - 100.0f, "collapsing hides the folder's tracks and moves the next up");
        arrangement.setFolderCollapsed ("Drums", false);
        expect (! arrangement.trackHidden (0), "and expanding shows them");
        expect (view.routeFolderToBus ("Drums") && project.busCount() == 2 && project.composition().buses[1].name == "Drums"
                    && tracks[0].output == "Drums" && tracks[1].output == "Drums", "a folder becomes a bus group");
        expect (view.mixer().renameBus (1, "Kit") && tracks[0].output == "Kit" && tracks[1].output == "Kit", "renaming the bus renames the outputs");
        expect (view.mixer().removeBus (1) && tracks[0].output.empty() && project.busCount() == 1, "removing the bus sends the tracks to the master");
        expect (view.renameFolder ("Drums", "Beat") && tracks[0].folder == "Beat" && view.folderTracks ("Beat").size() == 2, "renaming a folder");
        expect (view.setTrackFolder ({ 0, 1 }, {}) && tracks[0].folder.empty() && arrangement.folderHeaderBounds ("Beat").isEmpty(), "ungrouping");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "folder and output edits undo");
    }

    // 클립의 늘이기와 음높이: 경계가 늘인 길이를 따르고, 곡의 템포를 따르는 클립은 박으로 남으며, 나누기가 파일의 자리를 늘이기로 센다
    template <typename Expect>
    static void runClipStretchTest (const juce::File& dir, Expect& expect)
    {
        if (! mr::clipStretchAvailable())
            return;
        const auto songDir = dir.getChildFile ("stretch-song");
        songDir.getChildFile ("audio").createDirectory();
        {
            juce::AudioBuffer<float> b (1, 88200);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0));
            mr::writeWav (songDir.getChildFile ("audio/tone.wav"), b, b.getNumSamples(), 44100.0, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Tone", "instrument": "audio", "clips": [ { "file": "audio/tone.wav", "startBeat": 0, "lengthSeconds": 2 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "stretch project opened");
            return;
        }
        view.showBars (1.0, 8.0);
        auto& project = view.project();
        auto& arrangement = view.arrangement();
        const auto original = project.toJson();
        const ArrangementPanel::ClipRef ref { 0, 0 };
        auto clip = [&project] { return project.composition().tracks[0].clips[0]; };
        const float plain = arrangement.clipBounds (0, 0).getWidth();
        auto edited = clip();
        edited.pitchSemitones = 12.0;
        expect (view.editClip (ref, edited) && clip().pitchSemitones == 12.0 && std::abs (arrangement.clipBounds (0, 0).getWidth() - plain) < 0.5f,
                "a pitch change keeps the clip's length");
        expect (juce::String (project.toJson()).contains ("\"pitchSemitones\": 12"), "the pitch is written to the song");
        edited = clip();
        edited.stretch = 2.0;
        expect (view.editClip (ref, edited) && std::abs (arrangement.clipBounds (0, 0).getWidth() - 2.0f * plain) < 1.0f, "stretching doubles the clip");
        // 곡의 템포를 따른다: 파일의 템포 120 은 2초가 4박. 곡을 60 BPM 으로 바꾸면 4박이 4초
        edited = clip();
        edited.stretch = 1.0;
        edited.sourceBpm = 120.0;
        expect (view.editClip (ref, edited) && clip().sourceBpm == std::optional<double> (120.0), "the clip follows the tempo");
        juce::String error;
        expect (project.editTempo (mr::tempoJson ({ mr::TempoPoint { 0.0, 60.0, false } }), project.property ("timeSignature"), "tempo", error),
                "the song's tempo changes");
        const auto folder = songDir;
        expect (std::abs (mr::clipEndSeconds (clip(), project.tempo(), folder) - 4.0) < 1.0e-6
                    && std::abs (project.beatAtSeconds (mr::clipEndSeconds (clip(), project.tempo(), folder)) - 4.0) < 1.0e-6,
                "a tempo-following clip keeps its beats when the tempo changes");
        // 2 박(2초)에서 나누면 파일의 1초에서 나뉜다
        expect (view.splitClip (ref, 2.0) && project.composition().tracks[0].clips.size() == 2, "a stretched clip splits");
        const auto& clips = project.composition().tracks[0].clips;
        expect (clips.size() == 2 && clips[0].lengthSeconds && std::abs (*clips[0].lengthSeconds - 1.0) < 1.0e-6 && std::abs (clips[1].offsetSeconds - 1.0) < 1.0e-6
                    && clips[1].lengthSeconds && std::abs (*clips[1].lengthSeconds - 1.0) < 1.0e-6 && clips[1].sourceBpm == std::optional<double> (120.0),
                "the split counts the file's time through the stretch");
        // 음높이 교정: 곡에 적히고 다시 읽으면 조의 음이 허락된다. 자르기 방식과는 함께 쓰지 않는다
        edited = project.composition().tracks[0].clips[0];
        mr::PitchCorrection tune;
        tune.key = "A minor";
        tune.speedMs = 0.0;
        edited.pitchCorrection = tune;
        const ArrangementPanel::ClipRef first { 0, 0 };
        expect (view.editClip (first, edited) && project.composition().tracks[0].clips[0].pitchCorrection
                    && project.composition().tracks[0].clips[0].pitchCorrection->allowed[9] && ! project.composition().tracks[0].clips[0].pitchCorrection->allowed[1]
                    && juce::String (project.toJson()).contains ("\"key\": \"A minor\""),
                "pitch correction is saved with its key");
        auto sliced = project.composition().tracks[0].clips[0];
        sliced.warpSlices = true;
        expect (! view.editClip (first, sliced) && ! project.composition().tracks[0].clips[0].warpSlices, "a sliced clip cannot be pitch corrected");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "stretch edits undo");
    }

    // 오디오 트랙의 음: 피아노 롤에 음 덩어리, 끌어서 반음과 센트, 손잡이로 펴기와 비브라토, ↑, 시작 끌기(워프 마커, 앞 음은 제자리), 되돌리기
    template <typename Expect>
    static void runAudioNoteTest (const juce::File& dir, Expect& expect)
    {
        if (! mr::clipStretchAvailable())
            return;
        const auto songDir = dir.getChildFile ("sung-song");
        songDir.createDirectory();
        {
            // A3 +20 센트 (0.3~1.0초), C4 의 비브라토 ±30 센트 (1.1~1.8초), E4 (1.9~2.6초): 배음 다섯
            const double rate = 48000.0, twoPi = juce::MathConstants<double>::twoPi;
            juce::AudioBuffer<float> b (1, (int) (2.9 * rate));
            b.clear();
            const double notes[3][4] = { { 57.2, 0.3, 1.0, 0.0 }, { 60.0, 1.1, 1.8, 0.3 }, { 64.0, 1.9, 2.6, 0.0 } };
            double phase = 0.0;
            for (const auto& n : notes)
                for (int i = (int) (n[1] * rate); i < (int) (n[2] * rate); ++i)
                {
                    const double t = i / rate - n[1];
                    const double midi = n[0] + n[3] * std::sin (twoPi * 5.5 * t);
                    phase += twoPi * 440.0 * std::pow (2.0, (midi - 69.0) / 12.0) / rate;
                    double x = 0.0;
                    for (int h = 1; h <= 5; ++h)
                        x += std::sin (h * phase) / h;
                    b.setSample (0, i, (float) (0.2 * x * std::min (1.0, std::min (t / 0.02, (n[2] - n[1] - t) / 0.03))));
                }
            mr::writeWav (songDir.getChildFile ("take.wav"), b, b.getNumSamples(), rate, 24);
        }
        const auto file = songDir.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Vocal", "instrument": "audio", "clips": [ { "file": "take.wav", "startBeat": 0 } ] } ] })");
        MainComponent view (false, false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "sung project opened");
            return;
        }
        view.selectTrack (0);
        view.showBars (1.0, 3.0);
        auto& roll = view.pianoRoll();
        auto& project = view.project();
        const auto original = project.toJson();
        roll.measureAudioNotes();
        expect (roll.audioNoteCount (0) == 3, "the piano roll shows the take's three notes");
        using Ref = PianoRollPanel::AudioNoteRef;
        const Ref first { 0, 0 }, second { 0, 1 }, third { 0, 2 };
        const auto* s1 = roll.audioNoteSegment (first);
        expect (s1 != nullptr && std::abs (s1->midi - 57.2) < 0.05 && std::abs (s1->startSeconds - 0.3) < 0.03, "the first note is A3 +20 cents from 0.3 s");
        auto send = [&roll] (const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {})
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down, now, 1, at != down);
            const juce::String k (kind);
            if (k == "down") roll.mouseDown (e);
            else if (k == "drag") roll.mouseDrag (e);
            else if (k == "up") roll.mouseUp (e);
        };
        auto dragFromTo = [&send] (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
        {
            send ("down", from, from, mods);
            send ("drag", from + (to - from) * 0.5f, from, mods);
            send ("drag", to, from, mods);
            send ("up", to, from, mods);
        };
        const float row = roll.keyPoint (60, 0.5f).y - roll.keyPoint (61, 0.5f).y;   // 한 반음의 높이 (픽셀)
        auto edit = [&roll] (Ref r) { return roll.audioNoteEdit (r).value_or (mr::PitchEdit {}); };

        // 끌어 올리기: 두 줄 위로 놓으면 가운데 높이가 B3 에 맞는다 (57.2 에서 59)
        const auto p1 = roll.audioNotePoint (first, 0.5f);
        dragFromTo (p1, p1 + juce::Point<float> (0.0f, -2.0f * row));
        expect (std::abs (edit (first).semitones - (59.0 - s1->midi)) < 1.0e-6 && roll.selectedAudioNote() == std::optional<Ref> (first),
                "dragging up two rows puts the note's centre on B3");
        expect (juce::String (project.toJson()).contains ("\"pitchEdits\""), "the edit is written to the clip");
        // ⌥ 끌기는 센트 단위: 0.3 줄 위로 30 센트
        const auto p2 = roll.audioNotePoint (second, 0.5f);
        dragFromTo (p2, p2 + juce::Point<float> (0.0f, -0.3f * row), juce::ModifierKeys::altModifier);
        expect (std::abs (edit (second).semitones - 0.3) < 0.011, "an option-drag moves by cents");
        // 손잡이: 고른 음(둘째) 위의 비브라토 손잡이를 80 픽셀 내리면 0, 첫째 음의 펴기 손잡이를 40 픽셀 올리면 50%
        const auto vibrato = roll.audioNoteHandle (second, true);
        dragFromTo (vibrato, vibrato + juce::Point<float> (0.0f, 80.0f));
        expect (std::abs (edit (second).vibrato) < 1.0e-6 && std::abs (edit (second).semitones - 0.3) < 0.011, "the vibrato handle removes the vibrato");
        send ("down", roll.audioNotePoint (first, 0.5f), roll.audioNotePoint (first, 0.5f));
        send ("up", roll.audioNotePoint (first, 0.5f), roll.audioNotePoint (first, 0.5f));
        const auto flatten = roll.audioNoteHandle (first, false);
        dragFromTo (flatten, flatten + juce::Point<float> (0.0f, -40.0f));
        expect (std::abs (edit (first).flatten - 0.5) < 1.0e-6 && std::abs (edit (first).semitones - (59.0 - s1->midi)) < 1.0e-6, "the flatten handle sets 50%");
        // ↑ 는 고른 음을 반음 올린다
        expect (roll.selectedAudioNote() == std::optional<Ref> (first) && roll.transposeSelection (1) && std::abs (edit (first).semitones - (60.0 - s1->midi)) < 1.0e-6,
                "the up arrow raises the chosen note a semitone");
        expect (roll.snapAudioNote (second) && std::abs (edit (second).semitones - (60.0 - roll.audioNoteSegment (second)->midi)) < 1.0e-6 && edit (second).vibrato == 0.0,
                "snapping puts the centre on C4");
        auto clip = [&project] { return project.composition().tracks[0].clips[0]; };   // 편집마다 곡을 다시 읽는다
        expect (clip().pitchEdits.size() == 2, "two notes carry edits");
        // 셋째 음의 시작을 4.25 박으로 (격자에 맞춘다): 워프 마커가 들어가고 둘째 음은 제자리
        const auto* s2 = roll.audioNoteSegment (second);
        const auto* s3 = roll.audioNoteSegment (third);
        const double secondBefore = mr::clipSongSecondsAt (clip(), project.tempo(), s2->startSeconds);
        const auto edge = roll.audioNotePoint (third, 0.0f);
        dragFromTo (edge, { roll.notePoint (4.27, 64).x, edge.y });
        const auto warped = clip();
        int marker = -1;
        for (size_t i = 0; i < warped.warp.size(); ++i)
            if (std::abs (warped.warp[i].seconds - s3->startSeconds) < 1.0e-3)
                marker = (int) i;
        expect (marker >= 0 && std::abs (mr::warpMarkerSongBeat (warped, (size_t) marker) - 4.25) < 1.0e-6, "dragging the start puts a warp marker on beat 4.25");
        expect (std::abs (mr::clipSongSecondsAt (warped, project.tempo(), s2->startSeconds) - secondBefore) < 1.0e-6, "the note before stays where it was");
        expect (roll.setAudioNoteEdit (first, std::nullopt, "reset") && ! roll.audioNoteEdit (first), "resetting removes the note's edit");
        // 자르기 방식의 클립은 높이를 고치지 않는다
        auto sliced = clip();
        sliced.pitchEdits.clear();
        sliced.warpSlices = true;
        expect (view.editClip ({ 0, 0 }, sliced) && ! roll.shiftAudioNote (third, 1) && project.composition().tracks[0].clips[0].pitchEdits.empty(),
                "a sliced clip keeps the file's pitch");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "audio note edits undo");
    }

    // 입력 모니터링: 오디오 트랙만, 한 번에 한 트랙, 트랙을 옮겨도 따라가고 지우면 꺼진다
    template <typename Expect>
    static void runInputMonitorTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("monitor.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] },
            { "name": "Vocal", "instrument": "audio" }, { "name": "Guitar", "instrument": "audio" } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "monitor project opened");
            return;
        }
        expect (! view.setInputMonitoring (0, true) && view.inputMonitorTrack() == -1, "an instrument track does not monitor input");
        expect (view.setInputMonitoring (1, true) && view.inputMonitorTrack() == 1, "an audio track monitors input");
        expect (view.setInputMonitoring (2, true) && view.inputMonitorTrack() == 2, "one track at a time");
        expect (view.moveTrack (2, 0) && view.inputMonitorTrack() == 0, "monitoring follows the track when it moves");
        expect (view.deleteTrack (0) && view.inputMonitorTrack() == -1, "deleting the track turns monitoring off");
        expect (view.setInputMonitoring (1, true) && view.setInputMonitoring (1, false) && view.inputMonitorTrack() == -1, "and it turns off");
    }

    // 마스터 분석: 사인파(B5, 987.8 Hz)를 재생기로 돌려 스펙트럼의 봉우리 자리와 라우드니스 값을 본다
    template <typename Expect>
    static void runAnalyzerTest (Expect& expect)
    {
        auto parsed = mr::parseComposition (R"({ "tempo": 120, "tracks": [ { "name": "Tone", "instrument": "synth.sine", "notes": [
            { "pitch": 83, "startBeat": 0, "durationBeat": 16, "velocity": 100 } ] } ] })", juce::File());
        if (! parsed.composition)
        {
            expect (false, "analyzer song parses");
            return;
        }
        mr::PlayerOptions o;
        o.threads = 0;
        mr::RealtimePlayer player (*parsed.composition, o);
        player.play();
        juce::AudioBuffer<float> out (2, 512);
        float* ptrs[2] = { out.getWritePointer (0), out.getWritePointer (1) };
        for (int i = 0; i < (int) (4.0 * player.sampleRate() / 512.0); ++i)
            player.process (ptrs, 2, 512);
        AnalyzerPanel panel;
        panel.setSize (AnalyzerPanel::width, 400);
        panel.update (&player, -14.0);
        const auto& spectrum = panel.spectrum();
        const auto peak = std::max_element (spectrum.begin(), spectrum.end());
        const double column = (double) (peak - spectrum.begin()) + 0.5;
        const double hz = 20.0 * std::pow (1000.0, column / (double) spectrum.size());
        expect (! spectrum.empty() && std::abs (std::log2 (hz / 987.8)) < 0.08, "the spectrum peaks at the sine's frequency");
        expect (*peak > -30.0f && *peak < 0.0f, "the peak level is in range");
        expect (std::abs (panel.reading().integrated - player.loudness().integrated()) < 1.0e-6 && panel.reading().integrated > -40.0f
                    && panel.reading().seconds > 3.9, "the panel shows the player's loudness");
    }

    // 악보 보기와 MusicXML 내보내기: 고른 트랙의 악보, 줄 나누기, 마디 누르기, 타악기 보표, 트랙을 바꾸면 다시 만들기
    template <typename Expect>
    static void runScoreTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("score.json");
        juce::String notes;
        for (int i = 0; i < 64; ++i)
            notes << (i > 0 ? ", " : "") << "{ \"pitch\": " << 60 + (i * 5) % 12 << ", \"startBeat\": " << i * 0.5 << ", \"durationBeat\": 0.5, \"velocity\": 80 }";
        file.replaceWithText (juce::String (R"({ "tempo": 120, "key": "G major", "tracks": [
            { "name": "Lead", "instrument": "synth.pluck", "notes": [ )") + notes + R"( ] },
            { "name": "Drums", "instrument": "synth.edm-drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 0.5, "velocity": 100 },
                { "pitch": 42, "startBeat": 0.5, "durationBeat": 0.5, "velocity": 80 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "score project opened");
            return;
        }
        view.selectTrack (0);
        view.showScore (true);
        auto& panel = view.scorePanel();
        expect (view.scoreShown() && panel.isVisible() && ! view.pianoRoll().isVisible(), "the score replaces the piano roll");
        expect (panel.score().parts.size() == 1 && panel.score().parts[0].name == "Lead" && panel.score().parts[0].measures.size() == 8,
                "the selected track's score has the song's bars");
        expect (! panel.score().keyEstimated && panel.score().key == (mr::KeySignature { 1, false }), "the song's key");
        expect (panel.systemCount() >= 2, "bars wrap onto several lines");
        // 셋째 마디를 누르면 그 마디의 처음으로
        const auto at = panel.measurePoint (2);
        const auto now = juce::Time::getCurrentTime();
        panel.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                           0.0f, 0.0f, 0.0f, 0.0f, &panel, &panel, now, at, now, 1, false));
        expect (std::abs (view.playheadBeat() - 8.0) < 1.0e-9, "clicking a bar moves the playhead there");
        view.selectTrack (1);
        expect (panel.score().parts.size() == 1 && panel.score().parts[0].percussion && panel.score().parts[0].clefs[0] == mr::Clef::percussion,
                "a drum track shows a percussion staff");
        view.showMixer (true);
        expect (! view.scoreShown() && ! panel.isVisible(), "the mixer takes the place of the score");
        view.showScore (true);
        expect (view.scoreShown() && ! view.mixerShown(), "and back");
        // MusicXML 내보내기
        const auto out = dir.getChildFile ("score.musicxml");
        juce::String error;
        expect (view.exportMusicXml (out, false, error) && out.existsAsFile(), "MusicXML export writes a file");
        const auto xml = juce::parseXML (out);
        int parts = 0;
        if (xml != nullptr)
            for (auto* p : xml->getChildWithTagNameIterator ("part"))
                parts += p != nullptr ? 1 : 0;
        expect (xml != nullptr && xml->getTagName() == "score-partwise" && parts == 2, "both tracks are parts");
        expect (view.exportMusicXml (out, true, error) && juce::parseXML (out) != nullptr, "the selected track alone");
    }

    // 스텝 입력: 화음은 모두 뗄 때 한 칸에, 쉼표, 마지막 칸 지우기, MIDI 입력, 끄면 넣지 않는다
    template <typename Expect>
    static void runStepInputTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("steps.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [
            { "pitch": 48, "startBeat": 0, "durationBeat": 8, "velocity": 70 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "step project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        auto notes = [&project] { return project.composition().tracks[0].notes; };
        auto at = [&notes] (double start, int pitch) -> const mr::Note*
        {
            static mr::Note found;
            for (const auto& n : notes())
                if (std::abs (n.startBeat - start) < 1.0e-9 && n.pitch == pitch)
                    return &(found = n);
            return nullptr;
        };
        auto& keys = view.typingKeyboard();
        view.selectTrack (0);
        view.pianoRoll().setGrid (0.5);
        view.setPlayheadSeconds (1.05);   // 120 BPM 의 2.1 박
        view.setStepInput (true);
        expect (view.stepInput() && std::abs (view.stepCursor() - 2.0) < 1.0e-9, "step input starts at the playhead on the grid");
        keys.onNote (60, true);
        keys.onNote (64, true);
        keys.onNote (60, false);
        expect (notes().size() == 1, "nothing is written while a key is held");
        keys.onNote (64, false);
        const auto* c = at (2.0, 60);
        expect (c != nullptr && at (2.0, 64) != nullptr && std::abs (c->durationBeat - 0.5) < 1.0e-9 && notes().size() == 3,
                "a chord lands on one step, one grid step long");
        expect (std::abs (view.stepCursor() - 2.5) < 1.0e-9, "the cursor moves one step");
        expect (view.stepRest() && std::abs (view.stepCursor() - 3.0) < 1.0e-9, "a rest moves without notes");
        keys.onNote (67, true);
        keys.onNote (67, false);
        expect (at (3.0, 67) != nullptr && std::abs (view.stepCursor() - 3.5) < 1.0e-9, "the next note goes after the rest");
        expect (view.stepDelete() && at (3.0, 67) == nullptr && std::abs (view.stepCursor() - 3.0) < 1.0e-9, "delete removes the last step");
        expect (view.stepDelete() && notes().size() == 3 && std::abs (view.stepCursor() - 2.5) < 1.0e-9, "deleting a rest only steps back");
        expect (view.stepDelete() && notes().size() == 1 && std::abs (view.stepCursor() - 2.0) < 1.0e-9, "deleting the chord");
        view.liveMidi (juce::MidiMessage::noteOn (1, 62, (juce::uint8) 90), 0);
        view.liveMidi (juce::MidiMessage::noteOff (1, 62), 0);
        const auto* m = at (2.0, 62);
        expect (m != nullptr && m->velocity == 90, "MIDI input writes with its velocity");
        view.setPlayheadSeconds (2.5);    // 5 박
        keys.onNote (65, true);
        keys.onNote (65, false);
        expect (at (5.0, 65) != nullptr, "moving the playhead moves the cursor");
        expect (view.stepBack() && std::abs (view.stepCursor() - 5.0) < 1.0e-9, "stepping back");
        view.setStepInput (false);
        keys.onNote (69, true);
        keys.onNote (69, false);
        expect (at (5.0, 69) == nullptr && ! view.stepInput(), "nothing is written when step input is off");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "step input undoes");
    }

    // 드럼 보기: 이름 있는 건반과 쓰인 건반의 줄, 클릭으로 치기, 두 번 클릭, 줄 단위로 옮기기, 트랙마다 기억하는 보기
    template <typename Expect>
    static void runDrumTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("drums.json");
        file.replaceWithText (R"({ "tempo": 120, "tracks": [
            { "name": "Drums", "instrument": "synth.edm-drums", "notes": [
                { "pitch": 52, "startBeat": 0.5, "durationBeat": 0.25, "velocity": 90 },
                { "pitch": 53, "startBeat": 1, "durationBeat": 0.25, "velocity": 90 },
                { "pitch": 55, "startBeat": 2, "durationBeat": 0.25, "velocity": 90 },
                { "pitch": 70, "startBeat": 3, "durationBeat": 0.25, "velocity": 90 } ] },
            { "name": "Keys", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "drum project opened");
            return;
        }
        view.selectTrack (0);
        view.showBars (1, 2);
        auto& roll = view.pianoRoll();
        auto& project = view.project();
        const auto original = project.toJson();
        auto notes = [&project] { return project.composition().tracks[0].notes; };
        auto find = [&notes] (double start, int pitch)
        {
            const auto list = notes();
            return std::any_of (list.begin(), list.end(), [&] (const mr::Note& n) { return std::abs (n.startBeat - start) < 1.0e-9 && n.pitch == pitch; });
        };
        auto send = [&roll] (const char* kind, juce::Point<float> at, juce::Point<float> down, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") roll.mouseDown (e);
            else if (k == "drag") roll.mouseDrag (e);
            else if (k == "up") roll.mouseUp (e);
            else if (k == "double") roll.mouseDoubleClick (e);
        };
        const auto& rows = roll.drumRows();
        expect (roll.canUseDrumView() && roll.drumView(), "a drum kit opens in the drum view");
        expect (! rows.empty() && rows.front() == 70 && std::is_sorted (rows.rbegin(), rows.rend())
                    && std::find (rows.begin(), rows.end(), 54) == rows.end() && std::find (rows.begin(), rows.end(), 36) != rows.end(),
                "rows are the named keys and the used ones, high to low");

        // 빈 칸 클릭: 그 칸에 격자 한 칸의 치기
        roll.setGrid (0.25);
        const auto empty = roll.notePoint (2.1, 53);
        send ("down", empty, empty);
        send ("up", empty, empty);
        expect (find (2.0, 53) && notes().size() == 5, "a click on an empty cell adds a hit on the grid");
        const auto added = notes();
        expect (std::any_of (added.begin(), added.end(), [] (const mr::Note& n) { return n.startBeat == 2.0 && n.pitch == 53 && n.durationBeat == 0.25; }),
                "the hit is one grid step long");
        // 빈 칸의 두 번 클릭은 치기 하나를 남긴다
        const auto other = roll.notePoint (3.6, 53);
        send ("down", other, other, 1);
        send ("up", other, other, 1);
        send ("down", other, other, 2);
        send ("double", other, other, 2);
        send ("up", other, other, 2);
        expect (find (3.5, 53) && notes().size() == 6, "a double-click on an empty cell leaves one hit");
        // 있는 치기를 두 번 누르면 지운다
        const auto hit = roll.notePoint (2.0, 53);
        send ("down", hit, hit, 1);
        send ("up", hit, hit, 1);
        send ("down", hit, hit, 2);
        send ("double", hit, hit, 2);
        send ("up", hit, hit, 2);
        expect (! find (2.0, 53) && notes().size() == 5, "a double-click on a hit deletes it");
        // 한 줄 위로 끌면 위 줄의 건반 (53 위는 55, 이름 없는 54 는 줄이 아니다)
        const auto from = roll.notePoint (1.0, 53), to = roll.notePoint (1.0, 55);
        send ("down", from, from);
        send ("drag", to, from);
        send ("up", to, from);
        expect (find (1.0, 55) && ! find (1.0, 53) && ! find (1.0, 54), "dragging a hit one row up moves it to the next row's key");

        // 보기는 트랙마다 기억한다
        roll.setDrumView (false);
        expect (! roll.drumView() && roll.drumRows().empty(), "the piano view can be chosen");
        view.selectTrack (1);
        expect (! roll.canUseDrumView() && ! roll.drumView(), "a piano track has no drum view");
        view.selectTrack (0);
        expect (! roll.drumView(), "the choice is kept for the track");
        roll.setDrumView (true);
        expect (roll.drumView(), "and can be turned back on");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "drum edits undo");
    }

    // 실행 취소 기록: 여러 걸음을 한 번에, 알림은 한 번, 메뉴의 항목과 지금 자리
    template <typename Expect>
    static void runHistoryTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("history.json");
        file.replaceWithText (R"({ "tempo": 100, "tracks": [
            { "name": "A", "instrument": "synth.piano", "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 80 } ] },
            { "name": "B", "instrument": "synth.bass", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "history project opened");
            return;
        }
        auto& project = view.project();
        struct Counter final : Project::Listener
        {
            int calls = 0;
            void projectEdited (int) override { ++calls; }
        } counter;
        const auto start = project.toJson();
        juce::String error;
        auto quieter = project.trackJson (0);
        quieter.getDynamicObject()->setProperty ("mixer", juce::JSON::parse (R"({ "gainDb": -6 })"));
        expect (project.editTrack (0, quieter, "one", error), "edit one");
        const auto afterOne = project.toJson();
        expect (view.duplicateTrack (1), "edit two (a track)");
        expect (view.setMeterAt (4.0, std::make_pair (3, 4)), "edit three (a meter)");
        const auto afterThree = project.toJson();
        expect (view.historyMenu().getNumItems() == 4, "the history lists the start and three edits");
        project.addListener (&counter);
        expect (view.jumpHistory (-2) && project.toJson() == afterOne && project.trackCount() == 2, "two steps back at once");
        expect (counter.calls == 1, "one notification for a jump");
        expect (view.historyMenu().getNumItems() == 5, "the start, one past edit, a header and two redoable edits");
        expect (view.jumpHistory (2) && project.toJson() == afterThree && project.composition().meterChanges.size() == 1, "two steps forward");
        expect (view.jumpHistory (-3) && project.toJson() == start && ! project.hasUnsavedEdits(), "back to the start, as saved");
        expect (! view.jumpHistory (-1), "nothing before the start");
        expect (view.jumpHistory (3) && project.toJson() == afterThree && project.hasUnsavedEdits(), "all the way forward");
        project.removeListener (&counter);
    }

    // 트랙을 오디오로 굳히기: 굳힌 파일이 원래 트랙 뒤의 오디오 트랙에 들어가고 원래 트랙은 뮤트, 믹서와 센드를 옮긴다, 실행 취소
    template <typename Expect>
    static void runBounceTest (const juce::File& dir, Expect& expect)
    {
        const auto folder = dir.getChildFile ("bounce");
        folder.createDirectory();
        const auto file = folder.getChildFile ("song.json");
        file.replaceWithText (R"({ "tempo": 120, "buses": [ { "name": "room" } ], "tracks": [
            { "name": "Keys", "instrument": "synth.piano", "color": "#3366cc", "mixer": { "gainDb": -3, "pan": -0.2, "solo": true },
              "effects": [ { "type": "eq", "bands": [ { "type": "peak", "freqHz": 1000, "gainDb": 3, "q": 1 } ] } ],
              "sends": [ { "bus": "room", "levelDb": -12 } ],
              "notes": [ { "pitch": 60, "startBeat": 9, "durationBeat": 1, "velocity": 90 }, { "pitch": 64, "startBeat": 10, "durationBeat": 2, "velocity": 80 } ] },
            { "name": "Bass", "instrument": "synth.bass", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 4, "velocity": 90 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "bounce project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        const auto output = nextBounceFile (file, "Keys");
        expect (output == folder.getChildFile ("audio/Keys bounce 1.wav"), "the bounce goes to the song's audio folder");
        BounceOutcome outcome;
        try
        {
            mr::BounceRender render (project.composition(), 0, {});
            outcome.result = render.run (output);
            outcome.ok = true;
        }
        catch (const std::exception& e)
        {
            outcome.error = e.what();
        }
        expect (outcome.ok && output.existsAsFile(), "the track renders to a file");
        expect (std::abs (outcome.result.startBeat - 8.0) < 1.0e-9, "the clip starts at the bar of the first sound (bar 3)");
        expect (view.finishBounce ("Keys", outcome), "the bounce goes into the song");
        const auto& tracks = project.composition().tracks;
        expect (tracks.size() == 3 && tracks[1].instrument == "audio" && tracks[1].name == juce::String::fromUTF8 ("Keys 오디오").toStdString(),
                "an audio track right after the original");
        expect (tracks.size() == 3 && tracks[1].clips.size() == 1 && tracks[1].clips[0].file == "audio/Keys bounce 1.wav"
                    && std::abs (tracks[1].clips[0].startBeat - 8.0) < 1.0e-9 && tracks[1].notes.empty() && tracks[1].effects.empty(),
                "the clip, without notes and effects");
        expect (tracks.size() == 3 && tracks[1].mixer.gainDb == -3.0 && tracks[1].mixer.pan == -0.2 && ! tracks[1].mixer.solo
                    && tracks[1].sends.size() == 1 && tracks[1].colour == "#3366cc",
                "the mixer, sends and colour move over (not the solo)");
        expect (tracks[0].mixer.mute && ! tracks[0].mixer.solo && tracks[0].notes.size() == 2, "the original is muted and keeps its notes");
        expect (view.selectedTrack() == 1, "the audio track is selected");
        project.undoManager().undo();
        expect (project.toJson() == original, "the bounce undoes");
        BounceOutcome silent;
        silent.ok = true;
        silent.result.silent = true;
        expect (! view.finishBounce ("Keys", silent) && project.toJson() == original, "a silent bounce changes nothing");
        expect (! view.finishBounce ("Gone", outcome) && project.toJson() == original, "a renamed track is reported");
    }

    // 음표 다듬기: 고른 음표만, 없으면 모두. 편집 하나씩, 실행 취소, 모르는 필드와 가사를 지킨다
    template <typename Expect>
    static void runNoteToolsTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("tools.json");
        file.replaceWithText (juce::String::fromUTF8 (R"({ "tempo": 120, "tracks": [ { "name": "Lead", "instrument": "synth.piano", "notes": [
            { "pitch": 60, "startBeat": 0.1, "durationBeat": 0.3, "velocity": 60, "lyric": "가" },
            { "pitch": 61, "startBeat": 0.6, "durationBeat": 0.3, "velocity": 80 },
            { "pitch": 66, "startBeat": 1.1, "durationBeat": 0.3, "velocity": 100, "comment": "keep me" } ] } ] })"));
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "tools project opened");
            return;
        }
        view.selectTrack (0);
        auto& roll = view.pianoRoll();
        auto& project = view.project();
        auto notes = [&project] { return project.composition().tracks[0].notes; };
        const auto original = project.toJson();
        roll.setGrid (0.5);
        roll.deselect();

        expect (roll.quantizeWith ({ 0.5, 0.5, 0.0, false }, "q") && std::abs (notes()[0].startBeat - 0.05) < 1.0e-9
                    && std::abs (notes()[1].startBeat - 0.55) < 1.0e-9 && std::abs (notes()[2].startBeat + notes()[2].durationBeat - 1.4) < 1.0e-9,
                "half-strength quantize moves every start halfway and keeps the ends");
        expect (project.undoManager().getUndoDescription() == "q", "one edit per tool");
        project.undoManager().undo();
        expect (roll.quantizeWith ({ 0.5, 1.0, 1.0, false }, "swing") && std::abs (notes()[1].startBeat - (0.5 + 0.5 / 3.0)) < 1.0e-6,
                "full swing moves the off-beat eighth to the triplet");
        project.undoManager().undo();

        roll.setSelection ({ 0, 2 });
        expect (roll.scaleVelocities (0.5) && notes()[0].velocity == 70 && notes()[2].velocity == 90 && notes()[1].velocity == 80,
                "narrowing velocities touches only the selected notes");
        project.undoManager().undo();
        roll.deselect();
        expect (roll.rampVelocities (false) && notes()[0].velocity == 100 && notes()[1].velocity == 80 && notes()[2].velocity == 60,
                "a diminuendo runs from the loudest to the softest value in time order");
        project.undoManager().undo();
        expect (roll.legato() && std::abs (notes()[0].durationBeat - 0.5) < 1.0e-9 && std::abs (notes()[2].durationBeat - 0.3) < 1.0e-9,
                "legato reaches the next start");
        project.undoManager().undo();
        expect (roll.snapToScale (0, "major") && notes()[1].pitch == 60 && notes()[2].pitch == 65 && notes()[0].pitch == 60,
                "snapping to C major moves C# and F# down");
        project.undoManager().undo();
        expect (roll.invert() && notes()[1].pitch == 59 && notes()[2].pitch == 54, "inversion mirrors around the first note");
        project.undoManager().undo();
        expect (roll.retrograde() && std::abs (notes()[2].startBeat - 0.1) < 1.0e-9 && std::abs (notes()[0].startBeat - 1.1) < 1.0e-9,
                "retrograde reverses the time span");
        const auto saved = juce::String::fromUTF8 (project.toJson().c_str());
        const auto lyricKept = saved.contains (juce::String::fromUTF8 ("\"lyric\": \"가\"")) && saved.contains ("keep me");
        expect (lyricKept, "tools keep lyrics and unknown fields");
        project.undoManager().undo();
        expect (roll.humanize (8, 12.0), "humanize changes something");
        bool within = true;
        const auto before = mr::parseComposition (juce::String::fromUTF8 (original.c_str()), file).composition->tracks[0].notes;
        for (size_t i = 0; i < notes().size(); ++i)
            within = within && std::abs (notes()[i].velocity - before[i].velocity) <= 8
                     && std::abs (notes()[i].startBeat - before[i].startBeat) <= 0.012 * 2.0 + 1.0e-9;   // 120 BPM 에서 12 ms 는 0.024 박
        expect (within, "humanize stays inside its range");
        project.undoManager().undo();
        // 화음 펼치기: 0 박의 C E 화음 (한 박)을 8분음표 아르페지오로, 스트럼 30 ms 는 120 BPM 에서 0.06 박
        roll.deselect();
        auto chordTrack = project.trackJson (0);
        chordTrack.getDynamicObject()->setProperty ("notes", juce::JSON::parse (R"([ { "pitch": 64, "startBeat": 0, "durationBeat": 1, "velocity": 70 },
            { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ])"));
        juce::String chordError;
        expect (project.editTrack (0, chordTrack, "chord", chordError), "a chord to spread");
        expect (roll.arpeggiate (mr::ArpeggioOrder::up) && notes().size() == 2 && notes()[0].pitch == 60 && notes()[1].pitch == 64
                    && std::abs (notes()[1].startBeat - 0.5) < 1.0e-9 && notes()[1].velocity == 70,
                "an arpeggio at the grid (an eighth note) keeps each note's velocity");
        project.undoManager().undo();
        expect (roll.strum (true, 30.0) && std::abs (notes()[0].startBeat - 0.06) < 1.0e-9 && notes()[1].startBeat == 0.0
                    && std::abs (notes()[0].startBeat + notes()[0].durationBeat - 1.0) < 1.0e-9,
                "a strum from the low note delays the upper note by 30 ms and keeps its end");
        project.undoManager().undo();
        project.undoManager().undo();
        expect (! roll.setLengths (0.3), "a tool that changes nothing makes no edit");
        expect (project.toJson() == original, "every tool undoes");
        expect (roll.toolsMenu().getNumItems() >= 8, "the tools menu");
    }

    template <typename Expect>
    static void runPianoRollTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("roll.json");
        file.replaceWithText (juce::String::fromUTF8 (R"({ "tempo": 120, "tracks": [ { "name": "Keys", "instrument": "synth.piano", "notes": [
            { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 },
            { "pitch": 64, "startBeat": 2, "durationBeat": 1, "velocity": 80, "lyric": "늦", "comment": "keep me" } ] } ] })"));
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "roll project opened");
            return;
        }
        view.selectTrack (0);
        view.showBars (1, 2);
        auto& roll = view.pianoRoll();
        auto& project = view.project();
        auto notes = [&project] { return project.composition().tracks[0].notes; };
        auto find = [&notes] (double start, int pitch)
        {
            for (const auto& n : notes())
                if (std::abs (n.startBeat - start) < 1.0e-9 && n.pitch == pitch)
                    return true;
            return false;
        };

        auto send = [&roll] (const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {}, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") roll.mouseDown (e);
            else if (k == "drag") roll.mouseDrag (e);
            else if (k == "up") roll.mouseUp (e);
            else if (k == "double") roll.mouseDoubleClick (e);
        };
        auto dragFromTo = [&send] (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys mods = {})
        {
            send ("down", from, from, mods);
            send ("drag", from + (to - from) * 0.5f, from, mods);
            send ("drag", to, from, mods);
            send ("up", to, from, mods);
        };

        // 빈 자리 두 번 클릭: 격자(16분음표) 아래로 맞춘 자리에 음 추가
        const auto empty = roll.notePoint (4.1, 67);
        send ("down", empty, empty);
        send ("up", empty, empty);
        send ("double", empty, empty, {}, 2);
        expect (notes().size() == 3 && find (4.0, 67), "double click adds a note on the grid");

        // 음 끌기: 1박 오른쪽, 2반음 위
        dragFromTo (roll.notePoint (0.5, 60), roll.notePoint (1.5, 62));
        expect (find (1.0, 62) && ! find (0.0, 60), "drag moves a note");

        // 오른쪽 끝 끌기: 길이 1 -> 1.5
        dragFromTo (roll.notePoint (2.97, 64), roll.notePoint (3.5, 64));
        bool longer = false;
        for (const auto& n : notes())
            longer |= n.pitch == 64 && std::abs (n.durationBeat - 1.5) < 1.0e-9;
        expect (longer, "dragging the right edge changes the length");

        // ⌥ 끌기: 복사
        dragFromTo (roll.notePoint (2.5, 64), roll.notePoint (6.5, 64), juce::ModifierKeys::altModifier);
        expect (notes().size() == 4 && find (2.0, 64) && find (6.0, 64), "alt drag copies a note");
        auto lyricAt = [&notes] (double start, int pitch)
        {
            for (const auto& n : notes())
                if (std::abs (n.startBeat - start) < 1.0e-9 && n.pitch == pitch)
                    return n.lyric;
            return std::string ("?");
        };
        expect (lyricAt (2.0, 64) == "늦" && lyricAt (6.0, 64) == "늦" && lyricAt (4.0, 67).empty() && lyricAt (1.0, 62).empty(),
                "the lyric stays on the resized note and goes with its copy, drawn and moved notes without one have none");

        // 사각형 고르기로 모두 고르고 지우기, 되돌리기
        dragFromTo (roll.notePoint (0.1, 69), roll.notePoint (7.9, 59));
        expect (roll.selectionSize() == 4, "band selects every note");
        roll.deleteSelection();
        expect (notes().empty(), "delete removes the selection");
        project.undoManager().undo();
        expect (notes().size() == 4, "undo brings the notes back");

        // 벨로시티 칸: 막대를 맨 위로
        roll.deselect();
        expect (roll.laneShown().empty(), "velocity lane shown");
        dragFromTo (roll.lanePoint (1.0, 0.5), roll.lanePoint (1.0, 1.0));
        bool loud = false;
        for (const auto& n : notes())
            loud |= n.pitch == 62 && n.velocity == 127;
        expect (loud, "dragging a velocity bar sets the velocity");

        // 음 두 번 클릭: 지우기. 원래 음의 다른 필드는 남는다.
        const auto onNote = roll.notePoint (6.5, 64);
        send ("down", onNote, onNote);
        send ("up", onNote, onNote);
        send ("double", onNote, onNote, {}, 2);
        expect (notes().size() == 3 && ! find (6.0, 64), "double click on a note deletes it");
        expect (juce::String (project.toJson()).contains ("keep me"), "other fields of edited notes stay");

        // 건반과 음표를 누르면 그 음을 들려준다 (트랙, 곡의 음높이, 벨로시티, 0 은 끄기)
        struct Heard { size_t track; int pitch; int velocity; };
        std::vector<Heard> heard;
        roll.onAudition = [&heard] (size_t t, int pitch, int velocity, const std::string&, int) { heard.push_back ({ t, pitch, velocity }); };
        const auto key = roll.keyPoint (65, 0.9f);
        send ("down", key, key);
        expect (heard.size() == 1 && heard[0].track == 0 && heard[0].pitch == 65 && heard[0].velocity > 100, "pressing a key plays it");
        send ("drag", roll.keyPoint (67, 0.9f), key);
        expect (heard.size() == 3 && heard[1].pitch == 65 && heard[1].velocity == 0 && heard[2].pitch == 67 && heard[2].velocity > 0,
                "dragging over the keys moves to the next key");
        send ("up", roll.keyPoint (67, 0.9f), key);
        expect (heard.size() == 4 && heard[3].pitch == 67 && heard[3].velocity == 0, "releasing the key stops it");
        heard.clear();
        const auto soft = roll.keyPoint (60, 0.05f);
        send ("down", soft, soft);
        send ("up", soft, soft);
        expect (heard.size() == 2 && heard[0].velocity < 40 && heard[1].velocity == 0, "the left side of a key plays softly");
        heard.clear();
        dragFromTo (roll.notePoint (1.5, 62), roll.notePoint (1.5, 64));
        expect (! heard.empty() && heard.front().pitch == 62 && heard.front().velocity == 127, "pressing a note plays its pitch and velocity");
        bool followed = false;
        for (const auto& h : heard)
            followed |= h.pitch == 64 && h.velocity == 127;
        expect (followed && heard.back().pitch == 64 && heard.back().velocity == 0, "dragging a note plays the new pitch, releasing stops it");
        expect (find (1.0, 64), "the dragged note moved");
        project.undoManager().undo();
        heard.clear();
        const auto fresh = roll.notePoint (5.1, 66);
        send ("double", fresh, fresh, {}, 2);
        expect (heard.size() == 1 && heard[0].pitch == 66 && heard[0].velocity > 0, "a new note is played");
        roll.onAudition = nullptr;

        // 고른 음표의 구간 (⇧스페이스가 재생하는 박): 첫 시작부터 마지막 끝까지
        roll.deselect();
        expect (! roll.selectionRange(), "no range without a selection");
        roll.selectAll();
        double first = 1.0e9, last = 0.0;
        for (const auto& n : notes())
        {
            first = std::min (first, n.startBeat);
            last = std::max (last, n.startBeat + n.durationBeat);
        }
        const auto range = roll.selectionRange();
        expect (range && std::abs (range->first - first) < 1.0e-9 && std::abs (range->second - last) < 1.0e-9,
                "the selection range runs from the first start to the last end");
    }

    // 믹서에 마우스를 흉내 내 보내고 프로젝트가 어떻게 바뀌는지 본다 (창 없이)
    template <typename Expect>
    static void runMixerTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("mix.json");
        file.replaceWithText (R"({ "tempo": 120,
  "buses": [ { "name": "hall", "effects": [ { "type": "reverb", "mix": 1, "comment": "keep this" } ] } ],
  "master": { "effects": [ { "type": "limiter", "ceilingDb": -1 } ] },
  "tracks": [
    { "name": "Keys", "instrument": "synth.piano", "effects": [ { "type": "eq", "bands": [ { "type": "peak", "freqHz": 800, "gainDb": 2, "q": 1 } ] } ],
      "sends": [ { "bus": "hall", "levelDb": -12 } ],
      "notes": [ { "pitch": 60, "startBeat": 0, "durationBeat": 1, "velocity": 90 } ] },
    { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": 48, "startBeat": 0, "durationBeat": 4, "velocity": 70 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "mixer project opened");
            return;
        }
        view.showMixer (true);
        auto& mixer = view.mixer();
        auto& project = view.project();
        const auto original = project.toJson();
        int previews = 0;
        mixer.onPreview = [&previews] (const mr::Composition&) { ++previews; };
        auto* keys = mixer.strip (ChannelRef::track (0));
        expect (keys != nullptr && mixer.strip (ChannelRef::bus (0)) != nullptr && mixer.strip (ChannelRef::master()) != nullptr,
                "strips for tracks, buses and the master");
        if (keys == nullptr)
            return;
        const auto keysMixer = [&project] { return project.composition().tracks[0].mixer; };

        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down, juce::ModifierKeys mods = {}, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
            else if (k == "double") target.mouseDoubleClick (e);
        };

        // 페이더 끌기: 끄는 동안은 미리 듣기만, 놓으면 편집 하나
        auto& fader = keys->fader();
        const juce::Point<float> cap ((float) fader.getWidth() * 0.5f, (float) fader.getPositionOfValue (fader.getValue()));
        send (fader, "down", cap, cap);
        send (fader, "drag", cap.translated (0.0f, -20.0f), cap);
        send (fader, "drag", cap.translated (0.0f, -40.0f), cap);
        expect (previews > 0 && keysMixer().gainDb == 0.0, "dragging a fader previews without editing");
        send (fader, "up", cap.translated (0.0f, -40.0f), cap);
        expect (keysMixer().gainDb > 0.5, "releasing a fader edits the gain");
        expect (project.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("게인")), "fader edit is one undo step");
        send (fader, "double", cap, cap, {}, 2);
        expect (keysMixer().gainDb == 0.0, "double click sets the fader to 0 dB");

        // 곡선 쓰기: 재생 중(여기서는 흉내 낸 박)에 페이더를 끌면 그 동안의 값이 곡선이 되고, 앞뒤는 원래 값으로
        {
            double fakeBeat = 4.0;
            auto savedBeat = mixer.writeBeat;
            mixer.writeBeat = [&fakeBeat]() -> std::optional<double> { return fakeBeat; };
            mixer.setWriteMode (true);
            const double before = keysMixer().gainDb;
            send (fader, "down", cap, cap);
            for (int step = 1; step <= 8; ++step)
            {
                fakeBeat = 4.0 + step * 0.25;
                send (fader, "drag", cap.translated (0.0f, -5.0f * (float) step), cap);
            }
            send (fader, "up", cap.translated (0.0f, -40.0f), cap);
            const auto& lane = keysMixer().gainLane;
            expect (! lane.empty() && lane.points.front().beat == 0.0 && lane.points.front().value == before
                        && std::abs (lane.valueAt (2.0) - before) < 1.0e-9, "before the write the fader keeps its value");
            expect (! lane.empty() && lane.valueAt (5.5) > before + 0.5 && lane.valueAt (5.9) > lane.valueAt (4.6),
                    "the written moves become the curve");
            expect (! lane.empty() && std::abs (lane.valueAt (8.0) - before) < 1.0e-9, "after the release it returns to the value before");
            expect (project.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("곡선 쓰기")), "writing is one edit");
            expect (fader.isEnabled(), "a fader with a curve can be dragged while writing");
            project.undoManager().undo();
            expect (keysMixer().gainLane.empty(), "the write undoes");
            mixer.setWriteMode (false);
            mixer.writeBeat = savedBeat;
        }

        // 밸런스 손잡이: 오른쪽으로 끌면 오른쪽
        auto& pan = keys->panKnob();
        const juce::Point<float> knob ((float) pan.getWidth() * 0.5f, (float) pan.getHeight() * 0.5f);
        send (pan, "down", knob, knob);
        send (pan, "drag", knob.translated (30.0f, 0.0f), knob);
        send (pan, "up", knob.translated (30.0f, 0.0f), knob);
        expect (keysMixer().pan > 0.2, "dragging the balance knob moves it right");

        // 뮤트와 솔로
        keys->muteButton().onClick();
        keys->soloButton().onClick();
        expect (keysMixer().mute && keysMixer().solo, "mute and solo buttons edit the track");
        keys->muteButton().onClick();
        expect (! keysMixer().mute, "mute toggles back");

        // 센드 끌기: 0.25 dB/픽셀
        const auto slot = keys->sendSlot (0).getCentre().toFloat();
        send (*keys, "down", slot, slot);
        send (*keys, "drag", slot.translated (20.0f, 0.0f), slot);
        send (*keys, "drag", slot.translated (40.0f, 0.0f), slot);
        expect (project.composition().tracks[0].sends[0].levelDb == -12.0, "dragging a send previews without editing");
        send (*keys, "up", slot.translated (40.0f, 0.0f), slot);
        expect (std::abs (project.composition().tracks[0].sends[0].levelDb + 2.0) < 1.0e-9, "dragging a send changes its level");

        // 크기 곡선이 있는 센드: 곡선이 크기를 정하므로 끌어도, 두 번 눌러도 바뀌지 않는다
        {
            auto sends = project.composition().tracks[0].sends;
            sends[0].levelLane.points = { { 0.0, -20.0, mr::CurveShape::linear }, { 4.0, -6.0, mr::CurveShape::linear } };
            expect (mixer.commitSends (0, sends, "lane"), "a send lane can be added");
        }
        expect (keys->sendAutomated (0), "the send with a lane is marked as automated");
        const int previewsBefore = previews;
        send (*keys, "down", slot, slot);
        send (*keys, "drag", slot.translated (40.0f, 0.0f), slot);
        send (*keys, "up", slot.translated (40.0f, 0.0f), slot);
        send (*keys, "double", slot, slot, {}, 2);
        expect (previews == previewsBefore && std::abs (project.composition().tracks[0].sends[0].levelDb + 2.0) < 1.0e-9
                    && project.composition().tracks[0].sends[0].levelLane.points.size() == 2,
                "dragging or double clicking an automated send changes nothing");
        {
            auto sends = project.composition().tracks[0].sends;
            sends[0].levelLane = {};
            expect (mixer.commitSends (0, sends, "no lane") && ! keys->sendAutomated (0), "removing the lane makes the send editable again");
        }

        // 효과: 더하면 매개변수 칸이 열리고, 슬라이더를 끌면 편집 하나
        expect (mixer.addEffect (ChannelRef::track (0), "compressor"), "add an effect");
        expect (project.composition().tracks[0].effects.size() == 2 && mixer.isEffectOpen (ChannelRef::track (0), 1), "added effect is open");
        auto& editor = mixer.effectEditor();
        if (auto* threshold = editor.slider ("thresholdDb"))
        {
            const auto from = juce::Point<float> ((float) threshold->getPositionOfValue (-18.0), (float) threshold->getHeight() * 0.5f);
            send (*threshold, "down", from, from);
            send (*threshold, "drag", from.translated (-30.0f, 0.0f), from);
            expect (! project.composition().tracks[0].effects[1].params.hasProperty ("thresholdDb"), "dragging a parameter previews");
            send (*threshold, "up", from.translated (-30.0f, 0.0f), from);
            const auto value = project.composition().tracks[0].effects[1].params.getProperty ("thresholdDb", juce::var());
            expect (value.isDouble() || value.isInt(), "releasing a parameter writes it");
            expect (value.isDouble() && (double) value < -18.0, "dragging left lowers the threshold");
        }
        else
            expect (false, "threshold slider");
        mixer.openEffect (ChannelRef::track (0), 0);
        if (auto* gain = editor.slider ("bands.0.gainDb"))
        {
            gain->setValue (4.5, juce::sendNotificationSync);
            expect ((double) project.composition().tracks[0].effects[0].params["bands"][0]["gainDb"] == 4.5, "eq band gain");
        }
        else
            expect (false, "eq band slider");
        expect (mixer.toggleBypass (ChannelRef::track (0), 0) && project.composition().tracks[0].effects[0].bypass, "bypass an effect");
        expect (mixer.moveEffect (ChannelRef::track (0), 1, -1) && project.composition().tracks[0].effects[0].type == "compressor", "move an effect");
        expect (mixer.removeEffect (ChannelRef::track (0), 0) && project.composition().tracks[0].effects.size() == 1, "remove an effect");

        // 필터와 덕킹: 매개변수 칸, 필터 방식 목록, 곡선이 있는 매개변수는 손잡이가 꺼지고 다른 편집이 곡선을 지킨다
        expect (mixer.addEffect (ChannelRef::master(), "filter") && mixer.isEffectOpen (ChannelRef::master(), 1), "add a filter to the master");
        expect (editor.slider ("cutoffHz") != nullptr && editor.slider ("resonance") != nullptr && editor.combo ("mode") != nullptr,
                "the filter editor has cutoff, resonance and mode");
        expect (editor.rowLabel ("cutoffHz") == juce::String::fromUTF8 ("차단 주파수"), "the cutoff row has its Korean name");
        if (auto* mode = editor.combo ("mode"))
        {
            mode->setSelectedItemIndex (1, juce::sendNotificationSync);
            expect (project.composition().master.effects[1].params.getProperty ("mode", "").toString() == "highpass", "the mode list writes highpass");
        }
        {
            auto effects = mr::effectsJson (project.channelJson (ChannelRef::master()));
            auto filter = effects[1].clone();
            filter.getDynamicObject()->setProperty ("automation", juce::JSON::parse (
                R"({ "cutoffHz": [ { "beat": 0, "value": 20 }, { "beat": 16, "value": 1200 } ] })"));
            effects.getArray()->set (1, filter);
            expect (mixer.commitEffects (ChannelRef::master(), *effects.getArray(), "lane"), "a cutoff lane can be added");
        }
        mixer.openEffect (ChannelRef::master(), 1);
        if (auto* cutoff = editor.slider ("cutoffHz"))
        {
            expect (! cutoff->isEnabled(), "an automated parameter's slider is disabled");
            expect (editor.rowLabel ("cutoffHz").contains (juce::String::fromUTF8 ("(곡선)")), "an automated parameter is marked as a curve");
            cutoff->setValue (5000.0, juce::sendNotificationSync);   // 꺼진 손잡이도 값을 쓰면 곡선 곁의 고정값만 바뀐다
        }
        else
            expect (false, "automated cutoff slider");
        if (auto* res = editor.slider ("resonance"))
        {
            expect (res->isEnabled(), "a parameter without a lane stays editable");
            res->setValue (0.4, juce::sendNotificationSync);
        }
        {
            const auto& params = project.composition().master.effects[1].params;
            expect (std::abs ((double) params.getProperty ("resonance", 0.0) - 0.4) < 1.0e-9, "the resonance edit is written");
            const auto lane = params.getProperty ("automation", juce::var()).getProperty ("cutoffHz", juce::var());
            expect (lane.isArray() && lane.size() == 2 && (double) lane[1].getProperty ("value", 0.0) == 1200.0, "editing keeps the automation lane");
            expect (juce::String (project.toJson()).contains ("\"automation\""), "the saved text keeps the automation field");
        }
        expect (mixer.addEffect (ChannelRef::bus (0), "ducker") && mixer.isEffectOpen (ChannelRef::bus (0), 1), "add a ducker to a bus");
        expect (editor.slider ("rateBeats") != nullptr && editor.slider ("releaseBeats") != nullptr && editor.slider ("curve") != nullptr,
                "the ducker editor has rate, release and curve");
        if (auto* depth = editor.slider ("depthDb"))
        {
            depth->setValue (-18.0, juce::sendNotificationSync);
            expect ((double) project.composition().buses[0].effects[1].params.getProperty ("depthDb", 0.0) == -18.0, "the ducker depth is written");
        }
        else
            expect (false, "ducker depth slider");
        expect (editor.rowLabel ("depthDb") == juce::String::fromUTF8 ("깊이"), "the ducker depth row has its Korean name");
        expect (mixer.removeEffect (ChannelRef::bus (0), 1) && mixer.removeEffect (ChannelRef::master(), 1), "remove the filter and the ducker");

        // 딜레이의 박 단위 시간: 켜면 timeMs 를 지워 검사 경고가 남지 않고, 끄면 첫 템포(120)에서 같은 길이의 timeMs 를 쓴다
        expect (mixer.addEffect (ChannelRef::track (1), "delay") && mixer.isEffectOpen (ChannelRef::track (1), 0), "add a delay");
        if (auto* ms = editor.slider ("timeMs"))
            ms->setValue (300.0, juce::sendNotificationSync);
        const auto delayParams = [&project] { return project.composition().tracks[1].effects[0].params; };
        expect ((double) delayParams().getProperty ("timeMs", 0.0) == 300.0, "the delay time is written");
        if (auto* beats = editor.toggle ("timeBeats"))
        {
            beats->setToggleState (true, juce::dontSendNotification);
            beats->onClick();
            expect (delayParams().hasProperty ("timeBeats") && ! delayParams().hasProperty ("timeMs"), "turning on beat time removes timeMs");
            mr::Diagnostics check;
            mr::validateEffectSpec (project.composition().tracks[1].effects[0], check);
            expect (check.ok() && check.warnings.empty(), "a delay in beats has no warning");
            beats->setToggleState (false, juce::dontSendNotification);
            beats->onClick();
            expect (! delayParams().hasProperty ("timeBeats") && (double) delayParams().getProperty ("timeMs", 0.0) == 375.0,
                    "turning off beat time writes the same length in milliseconds");
        }
        else
            expect (false, "the delay has a beat time toggle");
        expect (mixer.removeEffect (ChannelRef::track (1), 0), "remove the delay");

        // 프리셋: 기본 프리셋을 입히고, 지금 값을 내 프리셋으로 저장해 다시 입히고, 효과 사슬을 저장해 불러온다
        {
            mixer.presetFolder = dir.getChildFile ("presets");
            const auto params = [&project] { return project.composition().tracks[1].effects[0].params; };
            expect (mixer.addEffect (ChannelRef::track (1), "compressor") && editor.applyFactoryPreset ("vocal-level")
                        && (double) params().getProperty ("thresholdDb", 0.0) == -20.0 && (double) params().getProperty ("ratio", 0.0) == 3.0,
                    "a factory preset sets the effect's values");
            expect (project.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("보컬 고르게")), "applying a preset is one named edit");
            if (auto* ratio = editor.slider ("ratio"))
                ratio->setValue (7.0, juce::sendNotificationSync);
            expect (editor.saveUserPreset ("Squash") && editor.userPresets().contains ("Squash")
                        && mixer.presetFolder.getChildFile ("Effects/compressor/Squash.json").existsAsFile(), "the current values are saved as a preset");
            expect (editor.applyFactoryPreset ("gentle-bus") && (double) params().getProperty ("ratio", 0.0) == 2.0, "another preset");
            expect (editor.applyUserPreset ("Squash") && (double) params().getProperty ("ratio", 0.0) == 7.0, "the saved preset brings the values back");
            expect (mixer.addEffect (ChannelRef::track (1), "delay") && mixer.saveChain (ChannelRef::track (1), "Vocal chain")
                        && mixer.chainPresets().contains ("Vocal chain"), "the effect chain is saved");
            expect (mixer.removeEffect (ChannelRef::track (1), 1) && mixer.removeEffect (ChannelRef::track (1), 0)
                        && project.composition().tracks[1].effects.empty(), "the chain is cleared");
            expect (mixer.loadChain (ChannelRef::track (1), "Vocal chain") && project.composition().tracks[1].effects.size() == 2
                        && project.composition().tracks[1].effects[0].type == "compressor" && (double) params().getProperty ("ratio", 0.0) == 7.0
                        && project.composition().tracks[1].effects[1].type == "delay", "loading the chain brings both effects back in order");
            expect (editor.deleteUserPreset ("Squash") && ! editor.userPresets().contains ("Squash"), "a user preset is deleted");
            while (! project.composition().tracks[1].effects.empty())
                mixer.removeEffect (ChannelRef::track (1), 0);
        }

        // MIDI 배우기: 덮개에서 페이더를 누르고 컨트롤러를 움직이면 묶이고, 묶인 컨트롤러는 손으로 끄는 것처럼 미리 듣다가 놓을 때 편집 하나
        {
            auto& learn = view.midiLearn();
            learn.clear();
            view.setMidiLearn (true);
            const auto controls = mixer.learnableControls();
            const auto faderTarget = LearnTarget::parse ("track:0/fader");
            const auto found = std::find_if (controls.begin(), controls.end(), [&] (const auto& c) { return c.first == *faderTarget; });
            expect (mixer.learnMode() && mixer.learnOverlay() != nullptr && found != controls.end(), "learn mode lists the track fader");
            if (found != controls.end() && mixer.learnOverlay() != nullptr)
            {
                const auto centre = found->second.getCentre().toFloat();
                send (*mixer.learnOverlay(), "down", centre, centre);
                send (*mixer.learnOverlay(), "up", centre, centre);
            }
            expect (learn.armed() && *learn.armed() == *faderTarget, "clicking the overlay picks the fader");
            expect (learn.handle (juce::MidiMessage::controllerEvent (1, 21, 10)) && learn.bindings().size() == 1 && learn.bindings()[0].controller == 21,
                    "the next controller is bound to it");
            view.setMidiLearn (false);
            const double before = project.composition().tracks[0].mixer.gainDb;
            const int previewsBefore = previews;
            const auto undoBefore = project.undoManager().getUndoDescription();
            learn.handle (juce::MidiMessage::controllerEvent (1, 21, 64));
            learn.handle (juce::MidiMessage::controllerEvent (1, 21, 100));
            expect (previews > previewsBefore && project.composition().tracks[0].mixer.gainDb == before, "moving the controller previews like dragging");
            learn.finishGestures();
            expect (std::abs (project.composition().tracks[0].mixer.gainDb - faderlaw::toDb (100.0 / 127.0)) < 0.051
                        && project.undoManager().getUndoDescription() != undoBefore, "letting go writes the fader as one edit");
            // 뮤트 단추: 64 를 넘어 오를 때 한 번 누른다
            learn.setLearning (true);
            learn.arm (*LearnTarget::parse ("track:0/mute"));
            learn.handle (juce::MidiMessage::controllerEvent (1, 22, 0));
            learn.setLearning (false);
            const bool muted = project.composition().tracks[0].mixer.mute;
            learn.handle (juce::MidiMessage::controllerEvent (1, 22, 127));
            learn.handle (juce::MidiMessage::controllerEvent (1, 22, 0));
            expect (project.composition().tracks[0].mixer.mute != muted, "a button press toggles the mute once");
            learn.handle (juce::MidiMessage::controllerEvent (1, 22, 127));
            expect (project.composition().tracks[0].mixer.mute == muted, "the next press toggles it back");
            // 효과 매개변수: 열린 효과의 줄을 덮개에서 고를 수 있고, 주파수는 로그 눈금
            expect (mixer.addEffect (ChannelRef::track (0), "filter"), "a filter to learn");
            const int filterIndex = (int) project.composition().tracks[0].effects.size() - 1;
            const auto cutoff = LearnTarget::parse ("track:0/effect:" + std::to_string (filterIndex) + "/cutoffHz");
            view.setMidiLearn (true);
            const auto withEditor = mixer.learnableControls();
            expect (std::any_of (withEditor.begin(), withEditor.end(), [&] (const auto& c) { return c.first == *cutoff; }), "the open effect's parameters can be learned");
            learn.arm (*cutoff);
            learn.handle (juce::MidiMessage::controllerEvent (2, 74, 0));
            view.setMidiLearn (false);
            const auto cutoffOf = [&] { return (double) project.composition().tracks[0].effects[(size_t) filterIndex].params.getProperty ("cutoffHz", 0.0); };
            learn.handle (juce::MidiMessage::controllerEvent (2, 74, 127));
            learn.finishGestures();
            const double top = cutoffOf();
            learn.handle (juce::MidiMessage::controllerEvent (2, 74, 64));
            learn.finishGestures();
            expect (top == 20000.0 && std::abs (cutoffOf() - 20.0 * std::pow (1000.0, 64.0 / 127.0)) / cutoffOf() < 0.01, "the cutoff follows the controller in log frequency");
            expect (! learn.handle (juce::MidiMessage::controllerEvent (1, 74, 5)), "another MIDI channel is not bound");
            // 앱 설정 글의 왕복
            MidiLearn copy (mixer, project);
            copy.fromSettings (learn.toSettings());
            expect (copy.bindings().size() == 3 && copy.bindings()[2].midiChannel == 2 && copy.bindings()[2].target == *cutoff, "bindings survive the settings");
            mixer.removeEffect (ChannelRef::track (0), filterIndex);
            learn.clear();
        }

        // 모듈레이션 효과: 트레몰로의 모양은 이름으로, 페이저의 단 수는 고른 값으로 쓴다
        expect (mixer.addEffect (ChannelRef::track (1), "tremolo") && mixer.isEffectOpen (ChannelRef::track (1), 0), "add a tremolo");
        expect (editor.rowLabel ("phaseDeg") == juce::String::fromUTF8 ("좌우 위상차"), "the tremolo rows have Korean names");
        if (auto* beats = editor.toggle ("rateBeats"))
        {
            const auto tremolo = [&project] { return project.composition().tracks[1].effects[0].params; };
            beats->setToggleState (true, juce::dontSendNotification);
            beats->onClick();
            expect (tremolo().hasProperty ("rateBeats") && ! tremolo().hasProperty ("rateHz") && ! editor.slider ("rateHz")->isEnabled(),
                    "a tremolo in beats has no rate in Hz");
            beats->setToggleState (false, juce::dontSendNotification);
            beats->onClick();
            // 0.5 박은 120 BPM 에서 0.25초, 4 Hz
            expect (! tremolo().hasProperty ("rateBeats") && (double) tremolo().getProperty ("rateHz", 0.0) == 4.0, "turning off the beat rate writes the same rate in Hz");
        }
        else
            expect (false, "the tremolo has a beat rate toggle");
        if (auto* shape = editor.combo ("shape"))
        {
            shape->setSelectedItemIndex (2, juce::sendNotificationSync);
            expect (project.composition().tracks[1].effects[0].params.getProperty ("shape", "").toString() == "square", "the tremolo shape is written by name");
        }
        else
            expect (false, "the tremolo has a shape list");
        expect (mixer.removeEffect (ChannelRef::track (1), 0) && mixer.addEffect (ChannelRef::track (1), "phaser"), "add a phaser");
        if (auto* stages = editor.combo ("stages"))
        {
            expect (stages->getNumItems() == 6 && stages->getText() == juce::String::fromUTF8 ("6단"), "the phaser stages show the default");
            stages->setSelectedItemIndex (0, juce::sendNotificationSync);
            expect ((double) project.composition().tracks[1].effects[0].params.getProperty ("stages", 0) == 2.0, "the phaser stages are written");
        }
        else
            expect (false, "the phaser has a stage list");
        expect (mixer.removeEffect (ChannelRef::track (1), 0), "remove the phaser");

        // 버스: 더하고, 센드를 보내고, 이름을 바꾸면 센드도 따라가고, 지우면 센드도 지워진다
        expect (mixer.addBus() && project.busCount() == 2, "add a bus");
        expect (mixer.addSend (1, 1) && project.composition().tracks[1].sends.size() == 1, "add a send to the new bus");
        expect (mixer.renameBus (1, "plate") && project.composition().tracks[1].sends[0].bus == "plate", "renaming a bus renames its sends");
        expect (mixer.removeBus (1) && project.busCount() == 1 && project.composition().tracks[1].sends.empty(), "removing a bus removes its sends");
        mr::MixerSettings master;
        master.gainDb = -2.0;
        expect (mixer.commitMixer (ChannelRef::master(), master, "master") && project.composition().master.mixer.gainDb == -2.0, "master fader");
        mr::MixerSettings bus;
        bus.mute = true;
        expect (mixer.commitMixer (ChannelRef::bus (0), bus, "bus mute") && project.composition().buses[0].mixer.mute, "bus mute");
        expect (juce::String (project.toJson()).contains ("keep this"), "unknown fields of edited buses stay");

        // VCA: 채널의 게인에 더하고 뮤트를 건다. 이름을 바꾸거나 지우면 속한 채널도 함께
        {
            const auto beforeVca = project.toJson();
            const auto& c = project.composition();
            expect (mixer.addVca() && c.vcas.size() == 1 && c.vcas[0].name == "VCA 1" && mixer.vcaStrip (0) != nullptr, "add a VCA strip");
            expect (mixer.assignVca (ChannelRef::track (1), "VCA 1") && c.tracks[1].vca == "VCA 1", "a track joins the VCA");
            const double ownGain = c.tracks[1].mixer.gainDb;
            if (auto* strip = mixer.vcaStrip (0))
            {
                strip->fader().setValue (-6.0, juce::sendNotificationSync);
                expect (c.vcas[0].gainDb == -6.0 && c.tracks[1].mixer.vcaDb == -6.0 && c.tracks[1].mixer.gainDb == ownGain,
                        "the VCA fader adds to the member without changing its own fader");
                strip->muteButton().onClick();
                expect (c.vcas[0].mute && c.tracks[1].mixer.muted() && ! c.tracks[1].mixer.mute, "the VCA mutes its members");
            }
            else
                expect (false, "VCA strip");
            expect (mixer.renameVca (0, "Strings") && c.vcas[0].name == "Strings" && c.tracks[1].vca == "Strings", "renaming a VCA renames its members");
            // 속한 트랙의 곡선 칸에 VCA 게인이 있고, 그린 곡선이 VCA 에 들어간다
            {
                auto& roll = view.pianoRoll();
                view.selectTrack (1);
                roll.showLane ("vca");
                const auto target = vcaTarget (c.vcas[0]);
                const auto at = roll.lanePoint (4.0, (float) target.toNorm (-18.0));
                send (roll, "double", at, at, {}, 2);
                expect (roll.laneShown() == "vca" && c.vcas[0].gainLane.points.size() == 2 && std::abs (c.vcas[0].gainLane.points[1].value + 18.0) < 1.0
                            && c.tracks[1].mixer.vcaLane.points.size() == 2, "the VCA lane is drawn from a member track");
                project.undoManager().undo();
            }
            expect (mixer.addVca (ChannelRef::bus (0)) && c.vcas.size() == 2 && c.buses[0].vca == c.vcas[1].name, "a new VCA with a bus in it");
            expect (mixer.removeVca (0) && c.vcas.size() == 1 && c.tracks[1].vca.empty() && c.tracks[1].mixer.vcaDb == 0.0 && ! c.tracks[1].mixer.muted(),
                    "removing a VCA frees its members");
            for (int i = 0; i < 7; ++i)
                project.undoManager().undo();
            expect (project.toJson() == beforeVca, "the VCA edits undo");
        }

        const auto edited = project.toJson();
        int steps = 0;
        while (project.undoManager().canUndo())
        {
            project.undoManager().undo();
            ++steps;
        }
        expect (project.toJson() == original, "undoing every mixer edit restores the original text");
        for (int i = 0; i < steps; ++i)
            project.undoManager().redo();
        expect (project.toJson() == edited, "redo restores the mixer edits");
        juce::String error;
        const auto saved = dir.getChildFile ("mix-saved.json");
        Project reread;
        expect (project.save (saved, error) && reread.load (saved, error) && reread.toJson() == edited, "mixer edits save and read back");
    }

    // 악기 바꾸기와 아티큘레이션 (창 없이, 재생기 없이)
    template <typename Expect>
    static void runInstrumentTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("instruments.json");
        file.replaceWithText (R"({ "tempo": 90, "tracks": [
    { "name": "Strings", "instrument": "violins", "notes": [
        { "pitch": "E4", "startBeat": 0, "durationBeat": 1, "velocity": 80 }, { "pitch": "G4", "startBeat": 1, "durationBeat": 1, "velocity": 80 } ] },
    { "name": "Keys", "instrument": "synth.piano", "plugin": { "program": 2 },
      "notes": [ { "pitch": "C4", "startBeat": 0, "durationBeat": 2, "velocity": 90 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "instrument project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();

        // 아티큘레이션: 악기 프로필에서 나오고, 고른 음표에 쓴다
        const auto names = view.articulationsOf (0);
        expect (std::find (names.begin(), names.end(), "pizzicato") != names.end(), "violins offer pizzicato");
        view.selectTrack (0);
        auto& roll = view.pianoRoll();
        roll.selectAll();
        expect (roll.setArticulation ("pizzicato"), "set the articulation of the selection");
        expect (project.composition().tracks[0].notes[0].articulation == "pizzicato" && project.composition().tracks[0].notes[1].articulation == "pizzicato",
                "both notes play pizzicato");
        expect (roll.setArticulation ("") && project.composition().tracks[0].notes[1].articulation.empty(), "back to the track default");

        // 악기 바꾸기: 앞 악기의 플러그인 설정은 지운다
        expect (view.changeInstrument (1, "synth.ep"), "change an instrument");
        expect (project.composition().tracks[1].instrument == "synth.ep" && project.composition().tracks[1].plugin.empty(),
                "the new instrument drops the old plugin settings");
        // 찾은 플러그인: 곡 안의 정의를 함께 더한다
        auto* def = new juce::DynamicObject();
        def->setProperty ("type", "au");
        def->setProperty ("plugin", "AudioUnit:Synths/aumu,dls ,appl");
        def->setProperty ("pluginName", "DLSMusicDevice");
        expect (view.changeInstrument (1, "dlsmusicdevice-au", juce::var (def)), "choose a found plugin");
        expect (project.composition().tracks[1].instrument == "dlsmusicdevice-au" && project.composition().inlineInstruments.size() == 1,
                "the song carries the plugin definition");
        expect (! view.openInstrumentWindow (1), "no window without a player");
        expect (project.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("악기 바꾸기")), "instrument change is one undo step");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing restores the instruments and articulations");
    }

    // 내장 신스 패널의 손잡이에 마우스를 흉내 내 보낸다 (창 없이)
    template <typename Expect>
    static void runSynthPanelTest (const juce::File& dir, Expect& expect)
    {
        const auto file = dir.getChildFile ("synth.json");
        file.replaceWithText (R"({ "tempo": 100, "tracks": [
    { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 4, "velocity": 80 } ] },
    { "name": "Strings", "instrument": "violins", "notes": [ { "pitch": "E4", "startBeat": 0, "durationBeat": 4, "velocity": 80 } ] },
    { "name": "Kit", "instrument": "synth.club-drums", "notes": [ { "pitch": 36, "startBeat": 0, "durationBeat": 1, "velocity": 100 } ] } ] })");
        MainComponent view (false);
        view.setSize (1500, 940);
        if (! view.openProject (file))
        {
            expect (false, "synth project opened");
            return;
        }
        auto& project = view.project();
        const auto original = project.toJson();
        expect (view.makeSynthPanel (1) == nullptr, "a sample instrument has no synth panel");
        auto panel = view.makeSynthPanel (0);
        expect (panel != nullptr && panel->preset() == "pad", "the pad track gets a synth panel");
        if (panel == nullptr)
            return;
        int previews = 0;
        panel->onPreview = [&previews] (const mr::Composition&) { ++previews; };
        auto* cutoff = panel->knob ("cutoffHz");
        expect (cutoff != nullptr && std::abs (cutoff->getValue() - 900.0) < 1.0e-6, "the cutoff knob shows the preset value");
        if (cutoff == nullptr)
            return;
        auto send = [] (juce::Component& target, const char* kind, juce::Point<float> at, juce::Point<float> down, int clicks = 1)
        {
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                      0.0f, 0.0f, 0.0f, 0.0f, &target, &target, now, down, now, clicks, at != down);
            const juce::String k (kind);
            if (k == "down") target.mouseDown (e);
            else if (k == "drag") target.mouseDrag (e);
            else if (k == "up") target.mouseUp (e);
            else if (k == "double") target.mouseDoubleClick (e);
        };
        // 위로 끌면 컷오프가 오른다: 끄는 동안은 재생기에만, 놓으면 편집 하나
        const juce::Point<float> c ((float) cutoff->getWidth() * 0.5f, (float) cutoff->getHeight() * 0.5f);
        send (*cutoff, "down", c, c);
        send (*cutoff, "drag", c.translated (0.0f, -30.0f), c);
        send (*cutoff, "drag", c.translated (0.0f, -60.0f), c);
        expect (previews > 0 && project.composition().tracks[0].synth.empty(), "dragging a knob previews without editing");
        send (*cutoff, "up", c.translated (0.0f, -60.0f), c);
        const auto& synth = project.composition().tracks[0].synth;
        expect (synth.count ("cutoffHz") == 1 && synth.at ("cutoffHz") > 900.0, "releasing a knob writes the value into the track");
        expect (project.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("컷오프")), "one undo step named after the knob");
        send (*cutoff, "double", c, c, 2);
        expect (project.composition().tracks[0].synth.count ("cutoffHz") == 0, "double click goes back to the preset value and removes the key");
        if (auto* release = panel->knob ("release"))
        {
            release->setValue (3.0, juce::sendNotificationSync);
            expect (std::abs (project.composition().tracks[0].synth.at ("release") - 3.0) < 1.0e-9, "typing a value writes it");
        }
        if (auto* filter = panel->filterSwitch())
        {
            filter->setToggleState (false, juce::dontSendNotification);
            filter->onClick();
            expect (project.composition().tracks[0].synth.count ("filter") == 1 && project.composition().tracks[0].synth.at ("filter") == 0.0,
                    "the filter switch turns the filter off");
        }
        if (auto* mono = panel->toggle ("mono"))
        {
            expect (! mono->getToggleState(), "the pad plays chords (mono off)");
            mono->setToggleState (true, juce::dontSendNotification);
            mono->onClick();
            expect (project.composition().tracks[0].synth.count ("mono") == 1 && project.composition().tracks[0].synth.at ("mono") == 1.0,
                    "the mono switch writes mono");
            expect (panel->filterSwitch() != nullptr && ! panel->filterSwitch()->getToggleState(), "the mono switch leaves the filter switch alone");
        }
        else
            expect (false, "mono switch");
        expect (panel->knob ("glide") != nullptr && panel->knob ("pitchEnv") != nullptr && panel->knob ("pitchEnvDecay") != nullptr,
                "the pitch section has glide and the pitch envelope");
        if (auto* bend = panel->knob ("bendRange"))
        {
            expect (std::abs (bend->getValue() - 2.0) < 1.0e-9, "the bend range knob shows 2 semitones");
            bend->setValue (12.0, juce::sendNotificationSync);
            expect (std::abs (project.composition().tracks[0].synth.at ("bendRange") - 12.0) < 1.0e-9, "the bend range knob writes semitones");
        }
        else
            expect (false, "bend range knob");
        if (auto* mode = panel->choice ("filterMode"))
        {
            expect (mode->getSelectedItemIndex() == 0 && mode->getText() == juce::String::fromUTF8 ("저역 통과"), "the filter mode shows lowpass");
            mode->setSelectedItemIndex (1, juce::sendNotificationSync);
            expect (project.composition().tracks[0].synth.count ("filterMode") == 1 && project.composition().tracks[0].synth.at ("filterMode") == 1.0,
                    "choosing highpass sets the filter mode");
            expect (juce::String (project.toJson()).contains ("\"filterMode\": \"highpass\""), "the filter mode is saved by name");
            mode->setSelectedItemIndex (0, juce::sendNotificationSync);
            expect (project.composition().tracks[0].synth.count ("filterMode") == 0, "back to lowpass removes the key");
        }
        else
            expect (false, "filter mode list");
        expect (panel->reset() && project.composition().tracks[0].synth.empty(), "reset removes every synth value");
        if (auto kit = view.makeSynthPanel (2))
        {
            expect (kit->knob ("tune") != nullptr && kit->knob ("bendRange") != nullptr && kit->knob ("levelDb") != nullptr,
                    "a drum kit panel has tune, bend range and level");
            if (auto* tune = kit->knob ("tune"))
            {
                tune->setValue (-3.0, juce::sendNotificationSync);
                expect (std::abs (project.composition().tracks[2].synth.at ("tune") + 3.0) < 1.0e-9, "the tune knob writes semitones");
            }
            expect (kit->choice ("filterMode") == nullptr, "a drum kit has no filter mode");
        }
        else
            expect (false, "the club kit gets a synth panel");
        while (project.undoManager().canUndo())
            project.undoManager().undo();
        expect (project.toJson() == original, "undoing restores the song");
    }

    static int writePng (juce::Component& component, const juce::File& out)
    {
        const auto image = component.createComponentSnapshot (component.getLocalBounds(), true, 2.0f);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        {
            std::fprintf (stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf ("%s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    // 새 곡 대화상자를 창 없이: 만들기 길로 곡을 만들어 열고, 템플릿과 맞는지 본다
    int runNewTest (const juce::StringArray& args)
    {
        juce::StringArray failures;
        int checks = 0;
        auto expect = [&] (bool ok, const juce::String& what) { ++checks; if (! ok) failures.add (what); };
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("initialdaw-new-test");
        dir.deleteRecursively();
        dir.createDirectory();
        mr::Diagnostics d;
        const auto templates = mr::listTemplates (mr::defaultTemplateDirectory(), d);
        expect (templates.size() >= 13, "the template folder has the templates");
        MainComponent view (false, false);
        view.setSize (1280, 800);
        auto dialog = view.makeNewSongDialog();
        expect (dialog->selectedTemplate() == 0, "the first template is selected");
        const int row = templates.size() > 2 ? 2 : 0;
        dialog->selectTemplate (row);
        const auto file = dir.getChildFile ("battle").getChildFile ("battle.json");
        dialog->create (file);
        expect (file.existsAsFile(), "the dialog writes the song");
        expect (view.project().loaded() && view.project().file() == file, "the new song is open");
        if (view.project().loaded() && ! templates.empty())
        {
            const auto& t = templates[(size_t) row];
            const auto& c = view.project().composition();
            expect (c.title == juce::String::fromUTF8 ("새 곡").toStdString(), "the title comes from the dialog");
            expect (c.tracks.size() == t.trackNames.size(), "the song has the template's tracks");
            expect (c.loop.has_value() == t.loop, "the loop follows the template");
            expect (! view.project().hasUnsavedEdits(), "a new song starts saved");
        }
        const auto before = file.getLastModificationTime();
        dialog->create (file);
        expect (file.getLastModificationTime() == before, "an existing file is not replaced");
        if (const int png = args.indexOf ("--png"); png >= 0 && png + 1 < args.size())
            expect (writePng (*dialog, juce::File::getCurrentWorkingDirectory().getChildFile (args[png + 1])) == 0, "the dialog is drawn");
        dir.deleteRecursively();
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", failures.isEmpty());
        o->setProperty ("failures", failures.joinIntoString (", "));
        o->setProperty ("checks", checks);
        o->setProperty ("templates", (int) templates.size());
        std::printf ("%s\n", juce::JSON::toString (juce::var (o), true).toRawUTF8());
        return failures.isEmpty() ? 0 : 1;
    }

    int runSnapshot (const juce::StringArray& args)
    {
        juce::File out, project;
        int width = 1600, height = 1000, track = 0;
        std::string lane;   // 곡선의 열쇠 (비면 벨로시티)
        double at = 0.0, firstBar = 0.0, lastBar = 0.0;
        bool mixer = false, score = false, exportDialog = false, exportVideo = false, newDialog = false, videoWindow = false;
        juce::String effect, companions, audioNote;
        int instrument = -1;
        for (int i = 0; i < args.size(); ++i)
        {
            const auto& a = args[i];
            auto next = [&] { return i + 1 < args.size() ? args[++i] : juce::String(); };
            if (a == "--audio-note")
                audioNote = next();
            else if (a == "--snapshot")
                out = juce::File::getCurrentWorkingDirectory().getChildFile (next());
            else if (a == "--size")
            {
                const auto v = next();
                width = v.upToFirstOccurrenceOf ("x", false, false).getIntValue();
                height = v.fromFirstOccurrenceOf ("x", false, false).getIntValue();
            }
            else if (a == "--track")
                track = next().getIntValue();
            else if (a == "--lane")
                lane = next().toStdString();
            else if (a == "--at")
                at = next().getDoubleValue();
            else if (a == "--mixer")
                mixer = true;
            else if (a == "--score")
                score = true;
            else if (a == "--instrument")
                instrument = next().getIntValue();
            else if (a == "--export")
                exportDialog = true;
            else if (a == "--export-video")
                exportDialog = exportVideo = true;
            else if (a == "--new")
                newDialog = true;
            else if (a == "--video-window")
                videoWindow = true;
            else if (a == "--companions")
                companions = next();
            else if (a == "--effect")
            {
                effect = next();
                mixer = true;
            }
            else if (a == "--bars")
            {
                const auto v = next();
                firstBar = v.upToFirstOccurrenceOf ("-", false, false).getDoubleValue();
                lastBar = v.fromFirstOccurrenceOf ("-", false, false).getDoubleValue();
            }
            else if (a.endsWithIgnoreCase (".json"))
                project = juce::File::getCurrentWorkingDirectory().getChildFile (a);
        }
        if (out == juce::File() || width < 400 || height < 300)
        {
            std::fprintf (stderr, "usage: InitialDAW --snapshot out.png [--size WxH] [--track N] [--at seconds] [--bars A-B] [--lane cc1] [--mixer] [--score] [--effect T:E] [--video-window] [--audio-note C:N] project.json\n");
            return 2;
        }
        MainComponent view (false);
        view.setSize (width, height);
        if (project != juce::File() && ! view.openProject (project))
        {
            std::fprintf (stderr, "cannot open %s\n", project.getFullPathName().toRawUTF8());
            return 1;
        }
        if (newDialog)
        {
            auto dialog = view.makeNewSongDialog();
            dialog->selectTemplate (track);
            return writePng (*dialog, out);
        }
        if (exportDialog)
        {
            auto dialog = view.makeExportDialog();
            if (dialog == nullptr)
            {
                std::fprintf (stderr, "--export needs a project\n");
                return 1;
            }
            if (exportVideo)
            {
                auto s = dialog->settings();
                s.video = true;
                s.quality = 256;
                s.output = juce::File();
                dialog->setSettings (s);
            }
            const auto image = dialog->createComponentSnapshot (dialog->getLocalBounds(), true, 2.0f);
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat png;
            if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
                return 1;
            std::printf ("%s\n", out.getFullPathName().toRawUTF8());
            return 0;
        }
        if (videoWindow)
        {
            // 영상 창의 화면만 그린다: --at 의 곡 위치에 보이는 프레임
            const auto& info = view.videoInfo();
            if (! info || ! view.project().composition().video)
            {
                std::fprintf (stderr, "--video-window needs a song with a video\n");
                return 1;
            }
            VideoView video;
            video.setSize (std::min (width, 960), std::min (width, 960) * std::max (1, info->height) / std::max (1, info->width));
            video.setVideo (mr::videoFileOf (view.project().composition()), info->rate, info->durationSeconds);
            video.showFrameNow (at - view.project().composition().video->offsetSeconds);
            return writePng (video, out);
        }
        if (instrument >= 0)
        {
            // 신스 패널만 그린다 (창에 넣지 않고)
            auto panel = view.makeSynthPanel ((size_t) instrument);
            if (panel == nullptr)
            {
                std::fprintf (stderr, "track %d does not play a built-in synth\n", instrument);
                return 1;
            }
            const auto image = panel->createComponentSnapshot (panel->getLocalBounds(), true, 2.0f);
            out.deleteFile();
            juce::FileOutputStream stream (out);
            juce::PNGImageFormat png;
            if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
                return 1;
            std::printf ("%s\n", out.getFullPathName().toRawUTF8());
            return 0;
        }
        view.selectTrack (track);
        // 피아노 롤에 함께 보는 트랙 (+ 를 붙이면 함께 고치는 트랙): --companions 1,+3
        for (const auto& item : juce::StringArray::fromTokens (companions, ",", ""))
            if (item.trim().isNotEmpty())
                view.pianoRoll().setCompanion ((size_t) item.trim().removeCharacters ("+").getIntValue(),
                                               item.trim().startsWith ("+") ? PianoRollPanel::Companion::edited : PianoRollPanel::Companion::shown);
        view.showLane (lane);
        view.showMixer (mixer);
        view.showScore (score);
        view.zoomToFit();
        if (firstBar >= 1.0 && lastBar >= firstBar)
            view.showBars (firstBar, lastBar);
        view.setPlayheadSeconds (at);
        // 오디오 트랙의 음은 그리기 전에 잰다. --audio-note 0:2 는 0번 클립의 셋째 음을 고른다
        view.pianoRoll().measureAudioNotes();
        if (audioNote.isNotEmpty())
            view.pianoRoll().selectAudioNote (PianoRollPanel::AudioNoteRef { (size_t) audioNote.upToFirstOccurrenceOf (":", false, false).getIntValue(),
                                                                             (size_t) audioNote.fromFirstOccurrenceOf (":", false, false).getIntValue() });
        if (effect.isNotEmpty() && view.project().loaded())
        {
            const auto owner = effect.upToFirstOccurrenceOf (":", false, false).trim();
            const int index = effect.fromFirstOccurrenceOf (":", false, false).getIntValue();
            ChannelRef channel = ChannelRef::track ((size_t) owner.getIntValue());
            if (owner == "master")
                channel = ChannelRef::master();
            else if (owner.startsWith ("bus"))
                channel = ChannelRef::bus ((size_t) owner.substring (3).getIntValue());
            view.mixer().openEffect (channel, index);
        }
        if (mixer && view.project().loaded())
            view.primeMeters (at);
        const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
        out.deleteFile();
        juce::FileOutputStream stream (out);
        juce::PNGImageFormat png;
        if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
        {
            std::fprintf (stderr, "cannot write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }
        std::printf ("%s\n", out.getFullPathName().toRawUTF8());
        return 0;
    }

    static int runScanPlugins (const juce::StringArray& args)
    {
        const int at = args.indexOf ("--scan-plugins");
        const juce::File cache (args[at + 1]);
        if (args[at + 1].isEmpty())
        {
            std::printf ("{\"ok\": false, \"message\": \"usage: --scan-plugins cache.json [folders...]\"}\n");
            return 2;
        }
        std::vector<juce::PluginDescription> all;
        std::vector<mr::Diagnostic> problems;
        std::set<juce::String> seen;
        auto add = [&] (const std::vector<juce::PluginDescription>& found)
        {
            for (const auto& d : found)
                if (seen.insert (d.createIdentifierString()).second)
                    all.push_back (d);
        };
        auto& host = mr::PluginHost::get();
        add (host.scan ("vst3", {}, problems));
        for (int i = at + 2; i < args.size(); ++i)
            add (host.scan ("vst3", { args[i].toStdString() }, problems));
        if (host.supports ("au"))
            add (host.scan ("au", {}, problems));
        try
        {
            mr::writePluginCache (cache, all);
        }
        catch (const std::exception& e)
        {
            std::printf ("\n{\"ok\": false, \"message\": \"%s\"}\n", e.what());
            return 1;
        }
        const auto instruments = std::count_if (all.begin(), all.end(), [] (const juce::PluginDescription& d) { return d.isInstrument; });
        std::printf ("\n{\"ok\": true, \"plugins\": %d, \"instruments\": %d, \"problems\": %d}\n", (int) all.size(), (int) instruments,
                     (int) problems.size());
        return 0;
    }

    // 효과 칸의 플러그인 창을 창 없이 시험한다: 매개변수를 옮기면 0.5초 뒤 효과의 state 로 편집 하나, 반영은 같은 플러그인에 (사슬을 바꾸지 않는다),
    // 실행 취소로 플러그인이 돌아오고, 저장한 곡을 다시 열면 같은 소리
    class PluginEffectTest final : private juce::Timer, private Project::Listener
    {
    public:
        explicit PluginEffectTest (const juce::StringArray& args)
        {
            const auto plugin = juce::File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--plugin-effect-test") + 1]);
            mr::PluginHost::get().allowLocation (plugin.getFullPathName().toStdString());   // 명령줄에 직접 준 플러그인
            dir_ = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("initialdaw-plugin-effect-test");
            dir_.deleteRecursively();
            dir_.createDirectory();
            file_ = dir_.getChildFile ("effect.json");
            file_.replaceWithText (juce::String (R"({ "tempo": 120, "buses": [ { "name": "Bus", "effects": [ { "type": "plugin", "plugin": ")") + plugin.getFullPathName()
                                   + R"(" } ] } ],
  "tracks": [ { "name": "Keys", "instrument": "synth.piano", "effects": [ { "type": "plugin", "format": "vst3", "plugin": ")" + plugin.getFullPathName() + R"(" },
                                                                          { "type": "gain", "gainDb": -1 } ],
                "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] } ] })");
            juce::String error;
            expect (plugin.exists(), "test effect exists");
            expect (project_.load (file_, error), "a song with plugin effects opens");
            if (! project_.loaded())
            {
                finish();
                return;
            }
            expect (effectSlotName (project_.composition().tracks[0].effects[0]) == "MusicRender Test Effect", "the slot is named after the plugin");
            project_.addListener (this);
            try
            {
                mr::PlayerOptions o;
                o.threads = 0;
                player_ = std::make_unique<mr::RealtimePlayer> (project_.composition(), o);
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
                finish();
                return;
            }
            instance_ = player_->effectPlugin (MainComponent::effectOwner (ChannelRef::track (0)), 0);
            expect (instance_ != nullptr && player_->effectPlugin (MainComponent::effectOwner (ChannelRef::bus (0)), 0) != nullptr,
                    "the player has the track's and the bus's plugin effects");
            expect (player_->effectPlugin ("track:0", 1) == nullptr, "a built-in effect has no plugin");
            if (instance_ == nullptr)
            {
                finish();
                return;
            }
            gain_ = instance_->getParameters()[0];
            original_ = gain_->getValue();
            panel_ = std::make_unique<InstrumentPanel> (project_, ChannelRef::track (0), 0, instance_);
            expect (panel_->isEffect() && panel_->editor() != nullptr, "the effect panel shows the parameter list");
            if (auto* slider = findSlider (panel_->editor()))
                slider->setValue (slider->getMinimum() + 0.25 * (slider->getMaximum() - slider->getMinimum()), juce::sendNotificationSync);
            else
                expect (false, "a slider in the effect's parameter list");
            started_ = juce::Time::getMillisecondCounterHiRes();
            startTimer (100);
        }

        ~PluginEffectTest() override
        {
            stopTimer();
            panel_.reset();
            instance_.reset();
            project_.removeListener (this);
            player_.reset();
        }

    private:
        static juce::Slider* findSlider (juce::Component* c)
        {
            if (c == nullptr)
                return nullptr;
            if (auto* s = dynamic_cast<juce::Slider*> (c))
                return s;
            for (auto* child : c->getChildren())
                if (auto* s = findSlider (child))
                    return s;
            return nullptr;
        }

        void projectEdited (int) override
        {
            try
            {
                expect (player_->apply (project_.composition()), "effect edits apply without rebuilding the player");
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
            }
            panel_->resync();
            expect (player_->effectPlugin ("track:0", 0) == instance_, "a changed plugin sound keeps the same plugin playing");
        }

        void timerCallback() override
        {
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - started_;
            if (stage_ != 0 || elapsed < 900.0)
                return;
            stage_ = 1;
            const auto& effect = project_.composition().tracks[0].effects[0];
            expect (effect.params.getProperty ("state", "").toString().isNotEmpty(), "the panel change is written to the effect's state");
            expect (project_.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("효과 소리")), "one undo step named after the effect sound");
            edited_ = gain_->getValue();
            expect (std::abs (edited_ - original_) > 0.05f, "the slider moved the plugin parameter");
            project_.undoManager().undo();
            expect (! project_.composition().tracks[0].effects[0].params.hasProperty ("state"), "undo removes the state");
            project_.undoManager().redo();
            expect (std::abs (gain_->getValue() - edited_) < 1.0e-4f, "redo puts the edited value back into the same plugin");
            juce::String error;
            const auto saved = dir_.getChildFile ("saved.json");
            expect (project_.save (saved, error), "save");
            Project reread;
            expect (reread.load (saved, error), "read the saved song");
            if (reread.loaded())
            {
                mr::PlayerOptions o;
                o.threads = 0;
                mr::RealtimePlayer again (reread.composition(), o);
                auto p = again.effectPlugin ("track:0", 0);
                expect (p != nullptr && std::abs (p->getParameters()[0]->getValue() - edited_) < 1.0e-4f, "the saved state comes back");
            }
            finish();
        }

        void expect (bool ok, const char* what)
        {
            ++checks_;
            if (! ok)
                failures_.add (what);
        }

        void finish()
        {
            stopTimer();
            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", failures_.isEmpty());
            o->setProperty ("failures", failures_.joinIntoString (", "));
            o->setProperty ("checks", checks_);
            std::printf ("%s\n", juce::JSON::toString (juce::var (o), true).toRawUTF8());
            dir_.deleteRecursively();
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (failures_.isEmpty() ? 0 : 1);
            juce::MessageManager::callAsync ([] { juce::JUCEApplication::getInstance()->quit(); });
        }

        Project project_;
        std::unique_ptr<mr::RealtimePlayer> player_;
        std::shared_ptr<juce::AudioPluginInstance> instance_;
        std::unique_ptr<InstrumentPanel> panel_;
        juce::AudioProcessorParameter* gain_ = nullptr;
        float original_ = 0.0f, edited_ = 0.0f;
        juce::File dir_, file_;
        double started_ = 0.0;
        int stage_ = 0;
        int checks_ = 0;
        juce::StringArray failures_;
    };

    // 악기 패널을 창 없이 시험한다 (MainComponent 가 하는 반영과 같은 순서로: 편집, apply, 창의 기준 맞추기)
    class PluginTest final : private juce::Timer, private Project::Listener
    {
    public:
        explicit PluginTest (const juce::StringArray& args)
        {
            const auto plugin = juce::File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--plugin-test") + 1]);
            mr::PluginHost::get().allowLocation (plugin.getFullPathName().toStdString());   // 명령줄에 직접 준 플러그인
            dir_ = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("initialdaw-plugin-test");
            dir_.deleteRecursively();
            dir_.createDirectory();
            file_ = dir_.getChildFile ("plugin.json");
            file_.replaceWithText (juce::String (R"({ "tempo": 120,
  "instruments": [ { "synth": { "type": "vst3", "plugin": ")") + plugin.getFullPathName() + R"(", "parameters": { "Level": 0.5 } } } ],
  "tracks": [
    { "name": "Tone", "instrument": "synth", "notes": [ { "pitch": "A4", "startBeat": 0, "durationBeat": 2, "velocity": 100 } ] },
    { "name": "Pad", "instrument": "synth.pad", "notes": [ { "pitch": "A3", "startBeat": 0, "durationBeat": 2, "velocity": 80 } ] } ] })");
            juce::String error;
            expect (plugin.exists(), "test plugin exists");
            expect (project_.load (file_, error), "song with an inline plugin definition opens");
            if (! project_.loaded())
            {
                finish();
                return;
            }
            project_.addListener (this);
            try
            {
                mr::PlayerOptions o;
                o.threads = 0;
                player_ = std::make_unique<mr::RealtimePlayer> (project_.composition(), o);
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
                finish();
                return;
            }
            auto* instance = player_->pluginInstance (0);
            expect (instance != nullptr, "the plugin track has a plugin instance");
            expect (player_->pluginInstance (1) == nullptr, "a synth track has none");
            if (instance == nullptr)
            {
                finish();
                return;
            }
            level_ = instance->getParameters()[0];
            expect (std::abs (level_->getValue() - 0.5f) < 1.0e-4f, "the song's definition set the level");
            panel_ = std::make_unique<InstrumentPanel> (project_, 0, *instance);
            expect (panel_->editor() != nullptr && ! panel_->hasOwnEditor(), "a plugin without its own panel gets the parameter list");
            // 사람처럼 패널의 첫 슬라이더(Level)를 옮긴다
            if (auto* slider = findSlider (panel_->editor()))
                slider->setValue (0.3, juce::sendNotificationSync);
            else
                expect (false, "a slider in the parameter list");
            expect (project_.composition().tracks[0].plugin.stateBase64.empty(), "the state is not written while the panel is still changing");
            started_ = juce::Time::getMillisecondCounterHiRes();
            startTimer (100);
        }

        ~PluginTest() override
        {
            stopTimer();
            panel_.reset();
            project_.removeListener (this);
            player_.reset();
        }

    private:
        static juce::Slider* findSlider (juce::Component* c)
        {
            if (c == nullptr)
                return nullptr;
            if (auto* s = dynamic_cast<juce::Slider*> (c))
                return s;
            for (auto* child : c->getChildren())
                if (auto* s = findSlider (child))
                    return s;
            return nullptr;
        }

        void projectEdited (int) override
        {
            const auto changed = tracksWithChangedPluginSettings (player_->composition(), project_.composition());
            try
            {
                expect (player_->apply (project_.composition()), "plugin edits apply without rebuilding the player");
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
            }
            for (const size_t t : changed)
                if (t == 0)
                    panel_->resync();
        }

        void timerCallback() override
        {
            const double elapsed = juce::Time::getMillisecondCounterHiRes() - started_;
            if (stage_ == 0 && elapsed > 900.0)
            {
                // 0.5초 조용해지면 상태가 곡에 들어간다
                const auto& spec = project_.composition().tracks[0];
                expect (! spec.plugin.stateBase64.empty(), "the panel change is written to the song");
                expect (project_.undoManager().getUndoDescription().contains (juce::String::fromUTF8 ("악기 소리")), "one undo step named after the sound");
                edited_ = level_->getValue();
                expect (std::abs (edited_ - 0.3f) < 1.0e-3f, "the slider moved the plugin parameter");
                project_.undoManager().undo();
                expect (std::abs (level_->getValue() - 0.5f) < 1.0e-4f, "undo puts the song's level back into the plugin");
                expect (project_.composition().tracks[0].plugin.empty(), "undo removes the track plugin settings");
                stage_ = 1;
                started_ = juce::Time::getMillisecondCounterHiRes();
                return;
            }
            if (stage_ == 1 && elapsed > 800.0)
            {
                expect (project_.undoManager().canRedo(), "restoring the plugin does not make a new edit");
                project_.undoManager().redo();
                expect (std::abs (level_->getValue() - edited_) < 1.0e-4f, "redo puts the edited level back");
                // 저장한 곡을 다시 열면 같은 소리
                juce::String error;
                const auto saved = dir_.getChildFile ("saved.json");
                expect (project_.save (saved, error), "save");
                Project reread;
                expect (reread.load (saved, error), "read the saved song");
                if (reread.loaded())
                {
                    mr::PlayerOptions o;
                    o.threads = 0;
                    mr::RealtimePlayer again (reread.composition(), o);
                    auto* p = again.pluginInstance (0);
                    expect (p != nullptr && std::abs (p->getParameters()[0]->getValue() - edited_) < 1.0e-4f, "the saved state comes back");
                }
                checkDexed();
                finish();
            }
        }

        // 받아 둔 Dexed 가 있으면 자체 화면이 만들어지는지 본다 (창에 붙이지는 않는다)
        void checkDexed()
        {
            const auto dexed = mr::resolveRegistryPath ("${MUSIC_RENDER_PLUGINS:-../../external/plugins}/dexed/Dexed.vst3",
                                                        mr::defaultRegistryFile().getParentDirectory());
            if (! dexed.exists())
            {
                dexedEditor_ = "absent";
                return;
            }
            juce::PluginDescription chosen;
            std::string error;
            auto instance = mr::PluginHost::get().load ("vst3", dexed.getFullPathName().toStdString(), "", 48000.0, 512, chosen, error);
            expect (instance != nullptr, "Dexed loads");
            if (instance == nullptr)
                return;
            expect (instance->hasEditor(), "Dexed has its own panel");
            std::unique_ptr<juce::AudioProcessorEditor> editor (instance->createEditorIfNeeded());
            expect (editor != nullptr && editor->getWidth() > 200 && editor->getHeight() > 100, "Dexed's panel is created with a size");
            if (editor != nullptr)
                dexedEditor_ = juce::String (editor->getWidth()) + "x" + juce::String (editor->getHeight());
            editor.reset();
        }

        void expect (bool ok, const char* what)
        {
            ++checks_;
            if (! ok)
                failures_.add (what);
        }

        void finish()
        {
            stopTimer();
            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", failures_.isEmpty());
            o->setProperty ("failures", failures_.joinIntoString (", "));
            o->setProperty ("checks", checks_);
            o->setProperty ("dexedPanel", dexedEditor_);
            std::printf ("%s\n", juce::JSON::toString (juce::var (o), true).toRawUTF8());
            dir_.deleteRecursively();
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (failures_.isEmpty() ? 0 : 1);
            juce::JUCEApplication::getInstance()->quit();
        }

        Project project_;
        std::unique_ptr<mr::RealtimePlayer> player_;
        std::unique_ptr<InstrumentPanel> panel_;
        juce::AudioProcessorParameter* level_ = nullptr;
        juce::File dir_, file_;
        juce::StringArray failures_;
        juce::String dexedEditor_ = "not checked";
        int checks_ = 0, stage_ = 0;
        double started_ = 0.0;
        float edited_ = 0.0f;
    };

    // 내보내기 대화상자를 창 없이 시험한다 (단추를 누르는 길로)
    class ExportTest final : private juce::Timer
    {
    public:
        explicit ExportTest (const juce::StringArray& args)
        {
            const int at = args.indexOf ("--export-test");
            if (at + 1 < args.size() && ! args[at + 1].startsWith ("--"))
            {
                plugin_ = juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1]);
                mr::PluginHost::get().allowLocation (plugin_.getFullPathName().toStdString());   // 명령줄에 직접 준 플러그인
            }
            if (const int png = args.indexOf ("--png"); png >= 0 && png + 1 < args.size())
                png_ = juce::File::getCurrentWorkingDirectory().getChildFile (args[png + 1]);
            dir_ = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("initialdaw-export-test");
            dir_.deleteRecursively();
            dir_.createDirectory();
            song_ = load (writeSong ("song.json", {}));
            expect (song_.has_value(), "the test song parses");
            if (! song_)
            {
                finish();
                return;
            }
            started_ = juce::Time::getMillisecondCounterHiRes();
            ExportSettings s;
            s.type = mr::AudioFileType::wav;
            s.bitDepth = 32;
            s.stems = true;
            s.midi = true;
            s.output = dir_.getChildFile ("whole.wav");
            begin (*song_, s);
            startTimer (50);
        }

    private:
        static std::optional<mr::Composition> load (const juce::File& file)
        {
            auto r = mr::loadComposition (file);
            return r.composition;
        }

        // 6마디(24박), 120 BPM, 반복 4박에서 20박, 버스의 잔향, 목표 음량과 리미터. pluginLine 이 있으면 그 악기 정의와 트랙을 더한다.
        juce::File writeSong (const juce::String& name, const juce::String& pluginPath)
        {
            juce::String keys, bass;
            const int chord[4][3] = { { 52, 55, 59 }, { 48, 52, 55 }, { 55, 59, 62 }, { 50, 54, 57 } };
            for (int bar = 0; bar < 6; ++bar)
            {
                const auto& c = chord[bar % 4];
                for (int beat = 0; beat < 4; ++beat)
                    keys << (keys.isEmpty() ? "" : ", ") << "{ \"pitch\": " << c[beat % 3] + 12 << ", \"startBeat\": " << bar * 4 + beat
                         << ", \"durationBeat\": 0.9, \"velocity\": " << 70 + 8 * (beat == 0) << " }";
                bass << (bass.isEmpty() ? "" : ", ") << "{ \"pitch\": " << c[0] - 12 << ", \"startBeat\": " << bar * 4 << ", \"durationBeat\": 3.8, \"velocity\": 90 }";
            }
            juce::String text;
            text << "{ \"title\": \"" << juce::String::fromUTF8 ("내보내기 시험") << "\", \"tempo\": 120, \"render\": { \"tailSeconds\": 1 },\n"
                 << "  \"metadata\": { \"loop\": { \"startBeat\": 4, \"endBeat\": 20 } },\n";
            if (pluginPath.isNotEmpty())
                text << "  \"instruments\": [ { \"tone\": { \"type\": \"vst3\", \"plugin\": \"" << pluginPath << "\" } } ],\n";
            text << "  \"buses\": [ { \"name\": \"Hall\", \"effects\": [ { \"type\": \"reverb\", \"mix\": 1, \"decaySeconds\": 1.5 } ] } ],\n"
                 << "  \"master\": { \"targetLufs\": -16, \"effects\": [ { \"type\": \"limiter\", \"ceilingDb\": -1, \"truePeak\": 1 } ] },\n"
                 << "  \"tracks\": [\n"
                 << "    { \"name\": \"Keys\", \"instrument\": \"synth.piano\", \"sends\": [ { \"bus\": \"Hall\", \"levelDb\": -10 } ], \"notes\": [ " << keys << " ] },\n"
                 << "    { \"name\": \"Bass\", \"instrument\": \"synth.bass\", \"notes\": [ " << bass << " ] }";
            if (pluginPath.isNotEmpty())
                text << ",\n    { \"name\": \"Tone\", \"instrument\": \"tone\", \"notes\": [ { \"pitch\": \"A4\", \"startBeat\": 0, \"durationBeat\": 8, \"velocity\": 90 } ] }";
            text << " ] }\n";
            const auto file = dir_.getChildFile (name);
            file.replaceWithText (text);
            return file;
        }

        void begin (const mr::Composition& c, const ExportSettings& s, bool cancelAtOnce = false)
        {
            ExportDialog::Song song;
            song.composition = c;
            dialog_ = std::make_unique<ExportDialog> (std::move (song), ExportSettings {}, dir_);
            dialog_->setSettings (s);
            expect (dialog_->settings().output == s.output, "the dialog keeps the chosen file");
            settings_ = dialog_->settings();
            dialog_->startExport (false);
            expect (dialog_->busy(), "the export starts");
            if (cancelAtOnce)
                dialog_->stopExport();
        }

        // 엔진으로 같은 곡을 렌더링해 바이트를 견준다
        bool sameAsEngine (const mr::Composition& c, const juce::File& exported)
        {
            auto o = renderOptionsFor (c, settings_);
            o.stemsDir.reset();
            o.output = dir_.getChildFile ("engine_" + exported.getFileName());
            try
            {
                mr::renderComposition (c, o);
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
                return false;
            }
            juce::MemoryBlock a, b;
            exported.loadFileAsData (a);
            o.output.loadFileAsData (b);
            return a.getSize() > 1000 && a == b;
        }

        void timerCallback() override
        {
            if (juce::Time::getMillisecondCounterHiRes() - started_ > 180000.0)
            {
                expect (false, "export test timed out at stage " + juce::String (stage_));
                finish();
                return;
            }
            if (bounceJob_ != nullptr)
            {
                if (bounceOutcome_)
                    checkBounce();
                return;
            }
            if (dialog_ == nullptr || ! dialog_->result())
                return;
            const auto r = *dialog_->result();
            switch (stage_++)
            {
                case 0:
                {
                    expect (r.ok, "whole song exported: " + r.error);
                    expect (sameAsEngine (*song_, r.output), "the app's WAV is byte for byte the engine's render");
                    expect (r.stems.getChildFile ("master.wav").existsAsFile() && r.stems.getChildFile ("Keys.wav").existsAsFile(),
                            "stems are written beside the file");
                    juce::MemoryBlock mid;
                    r.midi.loadFileAsData (mid);
                    expect (mid.getSize() > 14 && std::memcmp (mid.getData(), "MThd", 4) == 0, "MIDI is written beside the file");
                    expect (std::abs ((r.report.stats.lufs ? *r.report.stats.lufs : 0.0) + 16.0) < 0.3, "the song's loudness target is met");
                    ExportSettings s;
                    s.type = mr::AudioFileType::ogg;
                    s.quality = 5;
                    s.range = ExportRange::loop;
                    s.output = dir_.getChildFile ("loop.ogg");
                    begin (*song_, s);
                    break;
                }
                case 1:
                {
                    expect (r.ok, "loop exported: " + r.error);
                    expect (r.report.loop.applied && r.report.loop.startSample == 96000 && r.report.loop.endSample == 480000,
                            "the loop is 2 s to 10 s at 48 kHz");
                    const auto comments = mr::readVorbisComments (r.output);
                    auto has = [&] (const char* c) { return std::find (comments.begin(), comments.end(), juce::String::fromUTF8 (c).toStdString()) != comments.end(); };
                    expect (has ("LOOPSTART=96000") && has ("LOOPLENGTH=384000"), "Ogg carries LOOPSTART and LOOPLENGTH");
                    expect (has ("TITLE=내보내기 시험"), "Ogg carries the title");
                    expect (mr::readAudioFile (r.output).samples == 480000, "the loop file ends at the loop end");
                    ExportSettings s;
                    s.type = mr::mp3EncoderAvailable() ? mr::AudioFileType::mp3 : mr::AudioFileType::flac;
                    s.quality = 192;
                    s.range = ExportRange::bars;
                    s.firstBar = 2;
                    s.lastBar = 3;
                    s.output = dir_.getChildFile (s.type == mr::AudioFileType::mp3 ? "bars.mp3" : "bars.flac");
                    begin (*song_, s);
                    break;
                }
                case 2:
                {
                    expect (r.ok, "bars exported: " + r.error);
                    expect (r.report.range.applied && r.report.range.startBeat == 4.0 && r.report.range.endBeat == 12.0, "bars 2-3 are beats 4 to 12");
                    expect (std::abs (r.report.stats.durationSeconds - 6.0) < 1.0e-3, "4 s of bars and a 2 s fade");
                    const auto decoded = mr::readAudioFile (r.output);
                    expect (std::abs ((double) decoded.samples / decoded.sampleRate - 6.0) < 0.1, "the encoded file is about 6 s");
                    ExportSettings s;
                    s.output = dir_.getChildFile ("stopped.wav");
                    s.stems = true;
                    begin (*song_, s, true);
                    break;
                }
                case 3:
                {
                    expect (r.cancelled && ! r.ok, "stopping cancels the export");
                    expect (! r.output.exists(), "a stopped export writes no file");
                    expect (! dir_.getChildFile ("stopped_stems").exists(), "a stopped export removes its unfinished stems");
                    if (mr::media::videoSupported())
                    {
                        // 곡을 영상에 입힌다 (영상 0초에 곡의 0.5초, 영상의 원래 소리를 섞는다)
                        mr::media::TestPattern pattern;
                        pattern.rate = { 25, 1 };
                        pattern.frames = 125;
                        pattern.clickSeconds = { 1.0, 3.5 };
                        std::string reason;
                        const auto movie = dir_.getChildFile ("pattern.mov");
                        expect (mr::media::writeTestPattern (movie.getFullPathName().toStdString(), pattern, reason), "the test video is written");
                        videoSong_ = *song_;
                        videoSong_->video = mr::VideoSpec { movie.getFullPathName().toStdString(), 0.5 };
                        ExportSettings s;
                        s.video = true;
                        s.videoMixOriginal = true;
                        s.quality = 192;
                        s.output = dir_.getChildFile ("scored.mp4");
                        begin (*videoSong_, s);
                        break;
                    }
                    stage_ = 5;   // 다음 결과는 플러그인 곡
                    afterVideo();
                    return;
                }
                case 4:
                {
                    if (videoSong_)
                        checkVideo (r);
                    afterVideo();
                    return;
                }
                default:
                {
                    expect (r.ok, "plugin song exported: " + r.error);
                    expect (dialog_->lastBuiltOnMessageThread(), "plugin instruments are built on the message thread");
                    expect (sameAsEngine (*pluginSong_, r.output), "the plugin song's WAV is byte for byte the engine's render");
                    beginBounce (*pluginSong_, 2);
                    return;
                }
            }
        }

        // 영상 다음: 플러그인 곡을 내보내거나 (플러그인이 없으면) 굳히기
        void afterVideo()
        {
            if (plugin_ == juce::File())
            {
                beginBounce (*song_, 0);
                return;
            }
            pluginSong_ = load (writeSong ("plugin.json", plugin_.getFullPathName()));
            expect (pluginSong_.has_value(), "the plugin song parses");
            if (! pluginSong_)
            {
                finish();
                return;
            }
            ExportSettings s;
            s.bitDepth = 32;
            s.output = dir_.getChildFile ("plugin.wav");
            begin (*pluginSong_, s);
        }

        // 영상에 입힌 내보내기가 엔진으로 입힌 것과 같은지: 영상 샘플은 원본 그대로, 디코딩한 소리는 샘플까지 같다
        void checkVideo (const ExportResult& r)
        {
            expect (r.ok, "the scored video is exported: " + r.error);
            expect (settings_.video && settings_.output.hasFileExtension ("mp4") && settings_.range == ExportRange::song,
                    "the video format writes an .mp4 over the whole video");
            expect (r.video && r.video->videoSamples == 125 && r.video->mixedOriginal && std::abs (r.video->durationSeconds - 5.0) < 1.0e-3,
                    "the scored video keeps every frame and mixes the video's sound");
            const auto source = mr::videoFileOf (*videoSong_).getFullPathName().toStdString();
            std::string error;
            const auto a = mr::media::videoSampleDigests (source, error), b = mr::media::videoSampleDigests (r.output.getFullPathName().toStdString(), error);
            expect (a && b && a->size() == 125 && *a == *b, "the picture is copied without re-encoding");
            // 엔진으로 같은 곡을 같은 영상에 입힌다
            const auto engineFile = dir_.getChildFile ("engine_scored.mp4");
            auto o = renderOptionsFor (*videoSong_, settings_);
            o.onAudio = [&] (const juce::AudioBuffer<float>& audio, double sampleRate)
            {
                const mr::media::ScoreAudio music { audio.getArrayOfReadPointers(), audio.getNumChannels(), audio.getNumSamples(), sampleRate, 0.5 };
                mr::media::ScoreVideoOptions so;
                so.mixOriginal = true;
                so.aacBitrate = 192000;
                std::string reason;
                expect (mr::media::writeScoredVideo (source, music, engineFile.getFullPathName().toStdString(), so, reason).has_value(),
                        "the engine scores the same video: " + juce::String (reason));
            };
            try
            {
                mr::renderComposition (*videoSong_, o);
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
            }
            const auto appSound = mr::media::readVideoAudio (r.output.getFullPathName().toStdString(), 0.0, error);
            const auto engineSound = mr::media::readVideoAudio (engineFile.getFullPathName().toStdString(), 0.0, error);
            expect (appSound && engineSound && appSound->frames > 200000 && appSound->channels == engineSound->channels,
                    "the scored video's sound is sample for sample the engine's");
        }

        // 트랙을 오디오로 굳히기의 뒤 스레드 작업 (플러그인 곡이면 플러그인 악기 트랙)
        void beginBounce (const mr::Composition& c, size_t track)
        {
            bounced_ = c;
            bounceTrack_ = track;
            bounceJob_ = std::make_unique<BounceJob> (c, track, dir_.getChildFile ("bounce.wav"), [this] (const BounceOutcome& o) { bounceOutcome_ = o; });
            bounceJob_->start();
        }

        void checkBounce()
        {
            const auto o = *bounceOutcome_;
            expect (o.ok && ! o.result.silent && o.result.file.existsAsFile(), "the bounce job writes a file: " + o.error);
            expect (o.result.startBeat == 0.0 && o.result.lengthSeconds > 4.0, "the bounce starts at the first bar");
            // 엔진에서 같은 굳히기를 하면 같은 바이트
            const auto again = dir_.getChildFile ("bounce-engine.wav");
            try
            {
                mr::BounceRender render (*bounced_, bounceTrack_, {});
                render.run (again);
            }
            catch (const std::exception& e)
            {
                expect (false, e.what());
            }
            juce::MemoryBlock a, b;
            o.result.file.loadFileAsData (a);
            again.loadFileAsData (b);
            expect (a.getSize() > 1000 && a == b, "the bounce job's file is byte for byte the engine's bounce");
            finish();
        }

        void expect (bool ok, const juce::String& what)
        {
            ++checks_;
            if (! ok)
                failures_.add (what);
        }

        void finish()
        {
            stopTimer();
            bounceJob_.reset();
            if (png_ != juce::File() && dialog_ != nullptr)
            {
                const auto image = dialog_->createComponentSnapshot (dialog_->getLocalBounds(), true, 2.0f);
                png_.deleteFile();
                juce::FileOutputStream stream (png_);
                juce::PNGImageFormat png;
                expect (stream.openedOk() && png.writeImageToStream (image, stream), "the finished dialog is drawn");
            }
            dialog_.reset();
            auto* o = new juce::DynamicObject();
            o->setProperty ("ok", failures_.isEmpty());
            o->setProperty ("failures", failures_.joinIntoString (", "));
            o->setProperty ("checks", checks_);
            o->setProperty ("mp3", mr::mp3EncoderAvailable());
            o->setProperty ("plugin", plugin_ != juce::File());
            std::printf ("%s\n", juce::JSON::toString (juce::var (o), true).toRawUTF8());
            dir_.deleteRecursively();
            juce::JUCEApplication::getInstance()->setApplicationReturnValue (failures_.isEmpty() ? 0 : 1);
            juce::JUCEApplication::getInstance()->quit();
        }

        juce::File plugin_, png_, dir_;
        std::optional<mr::Composition> song_, pluginSong_, videoSong_;
        std::unique_ptr<ExportDialog> dialog_;
        std::unique_ptr<BounceJob> bounceJob_;
        std::optional<BounceOutcome> bounceOutcome_;
        std::optional<mr::Composition> bounced_;
        size_t bounceTrack_ = 0;
        ExportSettings settings_;
        juce::StringArray failures_;
        int checks_ = 0, stage_ = 0;
        double started_ = 0.0;
    };

    LookAndFeel lookAndFeel_;
    std::unique_ptr<mr::HostSession> hostSession_;
    std::unique_ptr<PluginTest> pluginTest_;
    std::unique_ptr<class PluginEffectTest> pluginEffectTest_;
    std::unique_ptr<ExportTest> exportTest_;
    std::unique_ptr<MainWindow> window_;
    std::unique_ptr<SelfTest> selfTest_;
};

} // namespace daw

START_JUCE_APPLICATION (daw::Application)
