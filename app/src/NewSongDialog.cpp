#include "NewSongDialog.h"

#include "Theme.h"

#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
juce::String u8 (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }

constexpr int margin = 18;
constexpr int listWidth = 250;
constexpr int rowHeight = 46;

juce::String bpmText (double bpm)
{
    return std::abs (bpm - std::round (bpm)) < 1.0e-6 ? juce::String ((int) std::lround (bpm)) : juce::String (bpm, 1);
}

juce::String factsLine (const mr::SongTemplate& t)
{
    juce::String s = bpmText (t.bpm) + " BPM, " + juce::String (t.beatsPerBar) + "/" + juce::String (t.beatUnit) + ", "
                     + juce::String ((int) t.trackNames.size()) + u8 ("트랙");
    if (t.loop)
        s << u8 (", 되풀이");
    return s;
}

} // namespace

NewSongDialog::NewSongDialog (std::vector<mr::SongTemplate> templates, juce::File directory, juce::String missingFolder)
    : templates_ (std::move (templates)), directory_ (std::move (directory)), missingFolder_ (std::move (missingFolder))
{
    list_.setModel (this);
    list_.setRowHeight (rowHeight);
    list_.setColour (juce::ListBox::backgroundColourId, colours::background);
    list_.setColour (juce::ListBox::outlineColourId, colours::outline);
    list_.setOutlineThickness (1);
    title_.setText (u8 ("새 곡"), false);
    title_.setTextToShowWhenEmpty (u8 ("곡 제목"), colours::textFaint);
    title_.setJustification (juce::Justification::centredLeft);
    title_.setIndents (8, 0);
    title_.onReturnKey = [this] { chooseFile(); };
    error_.setColour (juce::Label::textColourId, colours::danger);
    error_.setJustificationType (juce::Justification::centredLeft);
    create_.setButtonText (u8 ("만들기..."));
    create_.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.85f));
    create_.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    cancel_.setButtonText (u8 ("취소"));
    create_.onClick = [this] { chooseFile(); };
    cancel_.onClick = [this]
    {
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &list_, &title_, &error_, &create_, &cancel_ })
        addAndMakeVisible (c);
    create_.setEnabled (! templates_.empty());
    if (! templates_.empty())
        list_.selectRow (0);
    setSize (760, 470);
}

void NewSongDialog::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) templates_.size())
        return;
    const auto& t = templates_[(size_t) row];
    if (selected)
        g.fillAll (colours::selection.withAlpha (0.35f));
    g.setColour (colours::text);
    g.setFont (uiFont (14.0f, true));
    g.drawText (u8 (t.name), 12, 5, width - 20, 20, juce::Justification::centredLeft, true);
    g.setColour (colours::textDim);
    g.setFont (uiFont (12.0f));
    g.drawText (factsLine (t), 12, 24, width - 20, 17, juce::Justification::centredLeft, true);
    g.setColour (colours::outline);
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

juce::Rectangle<int> NewSongDialog::detailArea() const
{
    return { margin + listWidth + 20, margin, getWidth() - (margin + listWidth + 20) - margin, getHeight() - 2 * margin - 52 };
}

void NewSongDialog::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setColour (colours::textDim);
    g.setFont (uiFont (13.0f));
    g.drawText (u8 ("제목"), margin, title_.getY(), 44, title_.getHeight(), juce::Justification::centredLeft);
    auto area = detailArea();
    if (templates_.empty())
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawFittedText (u8 ("템플릿 폴더를 찾지 못했습니다.\n") + missingFolder_, area, juce::Justification::topLeft, 4);
        return;
    }
    const int row = juce::jlimit (0, (int) templates_.size() - 1, list_.getSelectedRow());
    const auto& t = templates_[(size_t) row];

    g.setColour (colours::text);
    g.setFont (uiFont (19.0f, true));
    g.drawText (u8 (t.name), area.removeFromTop (28), juce::Justification::centredLeft, true);
    area.removeFromTop (6);

    juce::AttributedString description;
    description.append (u8 (t.description), uiFont (13.5f), colours::text);
    description.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (description, (float) area.getWidth());
    layout.draw (g, area.removeFromTop ((int) std::ceil (layout.getHeight())).toFloat());
    area.removeFromTop (12);

    const double bar = t.beatsPerBar * 4.0 / t.beatUnit;
    const auto loopText = juce::String ((int) std::lround (t.loopStartBeat / bar) + 1) + "~" + juce::String ((int) std::lround (t.loopEndBeat / bar))
                          + u8 ("마디");
    juce::StringArray keys { u8 ("템포"), u8 ("박자"), u8 ("목표 음량"), u8 ("반복 구간"), u8 ("바탕") };
    juce::StringArray values { bpmText (t.bpm) + " BPM", juce::String (t.beatsPerBar) + "/" + juce::String (t.beatUnit),
                               t.targetLufs ? juce::String (*t.targetLufs, 1) + " LUFS" : u8 ("맞추지 않음"),
                               t.loop ? loopText : u8 ("없음"), t.sourceTitle.empty() ? u8 ("없음") : u8 ("「") + u8 (t.sourceTitle) + u8 ("」의 편성과 믹스") };
    g.setFont (uiFont (13.0f));
    for (int i = 0; i < keys.size(); ++i)
    {
        auto line = area.removeFromTop (20);
        g.setColour (colours::textDim);
        g.drawText (keys[i], line.removeFromLeft (80), juce::Justification::centredLeft);
        g.setColour (colours::text);
        g.drawText (values[i], line, juce::Justification::centredLeft, true);
    }
    area.removeFromTop (10);
    g.setColour (colours::textDim);
    g.drawText (u8 ("트랙 ") + juce::String ((int) t.trackNames.size()), area.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (colours::outline);
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());
    area.removeFromTop (6);

    // 트랙 이름과 악기, 두 단
    const int columns = t.trackNames.size() > 8 ? 2 : 1;
    const int perColumn = (int) std::ceil ((double) t.trackNames.size() / columns);
    const int columnWidth = area.getWidth() / columns;
    const int lineHeight = juce::jmin (18, perColumn > 0 ? area.getHeight() / perColumn : 18);
    g.setFont (uiFont (12.5f));
    for (size_t i = 0; i < t.trackNames.size(); ++i)
    {
        const int col = (int) i / perColumn, line = (int) i % perColumn;
        juce::Rectangle<int> r (area.getX() + col * columnWidth, area.getY() + line * lineHeight, columnWidth - 10, lineHeight);
        g.setColour (colours::text);
        g.drawText (u8 (t.trackNames[i]), r.removeFromLeft (r.getWidth() / 2), juce::Justification::centredLeft, true);
        g.setColour (colours::textDim);
        g.drawText (u8 (t.instruments[i]), r, juce::Justification::centredLeft, true);
    }
}

void NewSongDialog::resized()
{
    list_.setBounds (margin, margin, listWidth, getHeight() - 2 * margin - 52);
    auto bottom = juce::Rectangle<int> (margin, getHeight() - margin - 30, getWidth() - 2 * margin, 30);
    create_.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    cancel_.setBounds (bottom.removeFromRight (90));
    title_.setBounds (bottom.removeFromLeft (listWidth).withTrimmedLeft (48));
    bottom.removeFromLeft (12);
    error_.setBounds (bottom);
}

void NewSongDialog::chooseFile()
{
    if (templates_.empty() || list_.getSelectedRow() < 0)
        return;
    const auto name = juce::File::createLegalFileName (title().isEmpty() ? u8 ("새 곡") : title());
    chooser_ = std::make_unique<juce::FileChooser> (u8 ("새 곡을 저장할 곳"), directory_.getChildFile (name + ".json"), "*.json");
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                           [safe = juce::Component::SafePointer<NewSongDialog> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe != nullptr && fc.getResult() != juce::File())
                                   safe->create (fc.getResult().withFileExtension (".json"));
                           });
}

void NewSongDialog::create (const juce::File& file)
{
    const int row = list_.getSelectedRow();
    if (row < 0 || row >= (int) templates_.size() || ! onCreate)
        return;
    if (file.exists())
    {
        error_.setText (file.getFileName() + u8 (" 파일이 이미 있습니다. 다른 이름을 고르십시오."), juce::dontSendNotification);
        return;
    }
    juce::String error;
    const auto songTitle = title().isEmpty() ? file.getFileNameWithoutExtension() : title();
    if (! onCreate (templates_[(size_t) row], songTitle, file, error))
    {
        error_.setText (u8 ("만들지 못했습니다. ") + error, juce::dontSendNotification);
        return;
    }
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState (1);
}

} // namespace daw
