// 내보내기 대화상자: 형식, 품질, 샘플레이트, 범위, 스템과 MIDI, 저장할 곳. 내보내는 동안 진행 막대, 끝나면 길이와 음량.
#pragma once

#include "Exporter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>

namespace daw
{

class ExportDialog final : public juce::Component, private juce::Timer
{
public:
    struct Song
    {
        mr::Composition composition;     // 지금 문서 (저장하지 않은 편집 포함)
        juce::File projectFile;          // 기본 파일 이름
        std::optional<std::pair<int, int>> selectedBars;   // 고른 음표가 걸친 마디 (마디 구간의 처음 값)
    };

    ExportDialog (Song song, ExportSettings defaults, juce::File directory);
    ~ExportDialog() override;

    std::function<void (const ExportSettings&)> onStart;       // 내보내기를 시작할 때 (앱 설정에 고른 값을 남긴다)
    std::function<void (const ExportResult&)> onFinished;

    ExportSettings settings() const;
    void setSettings (const ExportSettings& settings);
    void startExport (bool askBeforeOverwrite = true);
    void stopExport();
    bool busy() const noexcept { return job_ != nullptr && ! result_.has_value(); }
    const std::optional<ExportResult>& result() const noexcept { return result_; }
    bool lastBuiltOnMessageThread() const noexcept { return builtOnMessageThread_; }   // 지난 내보내기가 악기를 메시지 스레드에서 만들었는지
    static int songLastBar (const mr::Composition& composition);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void fillQualities (const ExportSettings& wanted);
    void updateFile();
    void updateState();
    void finished (const ExportResult& result);
    void chooseFile();
    juce::String rangeSuffix() const;

    Song song_;
    juce::File directory_;
    juce::String baseName_;
    bool customName_ = false;
    juce::File confirmed_;   // 파일 고르기에서 덮어쓰기를 이미 물은 파일
    int lastBar_ = 1;

    juce::ComboBox format_, quality_, sampleRate_, range_;
    juce::TextEditor firstBar_, lastBarField_;
    juce::ToggleButton stems_, midi_, videoSound_;
    juce::File video_;   // 곡의 영상 (없으면 영상 형식을 고를 수 없다)
    juce::Label file_, status_;
    juce::TextButton change_, start_, close_, reveal_;
    double progressValue_ = 0.0;
    juce::ProgressBar progress_ { progressValue_ };
    std::unique_ptr<ExportJob> job_;
    std::optional<ExportResult> result_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool updating_ = false;
    bool builtOnMessageThread_ = false;
};

} // namespace daw
