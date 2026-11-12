// 새 곡 대화상자: 템플릿 목록과 그 편성, 제목. 만들기를 누르면 저장할 곳을 고르고 템플릿에서 곡을 만들어 연다.
#pragma once

#include "composition/Templates.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw
{

class NewSongDialog final : public juce::Component, private juce::ListBoxModel
{
public:
    NewSongDialog (std::vector<mr::SongTemplate> templates, juce::File directory, juce::String missingFolder = {});

    // 고른 템플릿과 파일로 곡을 만든다. 성공하면 true (대화상자를 닫는다), 실패하면 문구를 보인다.
    std::function<bool (const mr::SongTemplate&, const juce::String& title, const juce::File& file, juce::String& error)> onCreate;

    int selectedTemplate() const { return list_.getSelectedRow(); }
    void selectTemplate (int row) { list_.selectRow (row); }
    juce::String title() const { return title_.getText().trim(); }
    void create (const juce::File& file);   // 파일 고르기를 건너뛴다 (시험)

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    int getNumRows() override { return (int) templates_.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int) override { repaint(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { chooseFile(); }
    void chooseFile();
    juce::Rectangle<int> detailArea() const;

    std::vector<mr::SongTemplate> templates_;
    juce::File directory_;
    juce::String missingFolder_;
    juce::ListBox list_;
    juce::TextEditor title_;
    juce::Label error_;
    juce::TextButton create_, cancel_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace daw
