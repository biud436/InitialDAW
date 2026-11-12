#include "ExportDialog.h"

#include "Theme.h"

#include <cmath>

namespace daw
{

namespace
{

juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

constexpr int labelWidth = 104;
constexpr int rowHeight = 28;
constexpr int rowGap = 10;
constexpr int margin = 18;

mr::AudioFileType typeForId (int id)
{
    switch (id)
    {
        case 2: return mr::AudioFileType::flac;
        case 3: return mr::AudioFileType::ogg;
        case 4: return mr::AudioFileType::mp3;
        default: return mr::AudioFileType::wav;
    }
}

int idForType (mr::AudioFileType type)
{
    switch (type)
    {
        case mr::AudioFileType::flac: return 2;
        case mr::AudioFileType::ogg: return 3;
        case mr::AudioFileType::mp3: return 4;
        default: return 1;
    }
}

juce::String durationText (double seconds)
{
    const int total = (int) std::lround (seconds);
    return total >= 60 ? juce::String (total / 60) + u8 ("분 ") + juce::String (total % 60) + u8 ("초") : juce::String (total) + u8 ("초");
}

juce::String barsText (double startBeat, double endBeat, const mr::MeterMap& meters)
{
    // 마디 줄이 아니면 그 마디 안의 비율로
    auto barNumber = [&meters] (double beat) { return meters.barAt (beat) + meters.beatInBar (beat) / meters.barBeatsAt (beat); };
    const double first = barNumber (startBeat) + 1.0, last = barNumber (endBeat);
    auto number = [] (double v) { return std::abs (v - std::round (v)) < 1.0e-6 ? juce::String ((int) std::lround (v)) : juce::String (v, 2); };
    return number (first) + "~" + number (last) + u8 ("마디");
}

} // namespace

int ExportDialog::songLastBar (const mr::Composition& c)
{
    double end = 0.0;
    for (const auto& t : c.tracks)
        for (const auto& n : t.notes)
            end = std::max (end, n.startBeat + n.durationBeat);
    if (c.render.endBeat)
        end = *c.render.endBeat;
    const auto meters = c.meters();
    return std::max (1, meters.isBarLine (end) ? meters.barAt (end) : meters.barAt (end) + 1);
}

ExportDialog::ExportDialog (Song song, ExportSettings defaults, juce::File directory)
    : song_ (std::move (song)), directory_ (std::move (directory))
{
    lastBar_ = songLastBar (song_.composition);
    baseName_ = song_.projectFile != juce::File() ? song_.projectFile.getFileNameWithoutExtension() : u8 ("곡");

    format_.addItem ("WAV", 1);
    format_.addItem ("FLAC", 2);
    format_.addItem ("Ogg Vorbis", 3);
    format_.addItem ("MP3", 4);
    format_.setItemEnabled (4, mr::mp3EncoderAvailable());
    video_ = exportableVideo (song_.composition);
    format_.addItem (u8 ("영상 (MP4)"), 5);
    format_.setItemEnabled (5, video_ != juce::File());
    sampleRate_.addItem ("44100 Hz", 44100);
    sampleRate_.addItem ("48000 Hz", 48000);
    range_.addItem (u8 ("곡 전체"), 1);
    const auto& c = song_.composition;
    range_.addItem (c.loop ? u8 ("반복 구간 (") + barsText (c.loop->startBeat, c.loop->endBeat, c.meters()) + ")" : u8 ("반복 구간 (곡에 없음)"), 2);
    range_.setItemEnabled (2, c.loop.has_value());
    range_.addItem (u8 ("마디 구간"), 3);
    stems_.setButtonText (u8 ("트랙별 스템 (WAV)"));
    midi_.setButtonText ("MIDI");
    videoSound_.setButtonText (u8 ("영상의 원래 소리 섞기"));
    videoSound_.onClick = [this] { updateState(); };
    for (auto* e : { &firstBar_, &lastBarField_ })
    {
        e->setInputRestrictions (4, "0123456789");
        e->setJustification (juce::Justification::centred);
        e->onTextChange = [this] { if (! updating_) { updateFile(); updateState(); } };
    }
    file_.setColour (juce::Label::textColourId, colours::textDim);
    file_.setMinimumHorizontalScale (0.6f);
    status_.setJustificationType (juce::Justification::topLeft);
    status_.setColour (juce::Label::textColourId, colours::textDim);
    change_.setButtonText (u8 ("바꾸기..."));
    start_.setButtonText (u8 ("내보내기"));
    start_.setColour (juce::TextButton::buttonColourId, colours::accent.withAlpha (0.85f));
    start_.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
   #if JUCE_MAC
    reveal_.setButtonText (u8 ("Finder 에서 보기"));
   #else
    reveal_.setButtonText (u8 ("폴더 열기"));
   #endif
    progress_.setColour (juce::ProgressBar::foregroundColourId, colours::accent);
    progress_.setColour (juce::ProgressBar::backgroundColourId, colours::panelRaised);

    format_.onChange = [this] { if (! updating_) { fillQualities (settings()); updateFile(); updateState(); } };
    for (auto* box : { &quality_, &sampleRate_, &range_ })
        box->onChange = [this] { if (! updating_) { updateFile(); updateState(); } };
    stems_.onClick = [this] { updateState(); };
    change_.onClick = [this] { chooseFile(); };
    start_.onClick = [this] { startExport(); };
    close_.onClick = [this]
    {
        if (busy())
            stopExport();
        else if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
            window->exitModalState (0);
    };
    reveal_.onClick = [this]
    {
        if (result_ && result_->ok)
            result_->output.revealToUser();
    };

    for (auto* comp : std::initializer_list<juce::Component*> { &format_, &quality_, &sampleRate_, &range_, &firstBar_, &lastBarField_, &stems_, &midi_,
                                                                &videoSound_, &file_, &change_, &progress_, &status_, &start_, &close_, &reveal_ })
        addAndMakeVisible (comp);
    reveal_.setVisible (false);

    setSettings (defaults);
    setSize (560, 356);
}

ExportDialog::~ExportDialog()
{
    job_.reset();   // 렌더링을 멈추고 기다린다 (악기는 메시지 스레드에서 지운다)
}

ExportSettings ExportDialog::settings() const
{
    ExportSettings s;
    s.video = format_.getSelectedId() == 5;
    s.type = typeForId (format_.getSelectedId());
    const int q = quality_.getSelectedId();
    if (s.video)
        s.quality = q;
    else if (s.type == mr::AudioFileType::wav || s.type == mr::AudioFileType::flac)
        s.bitDepth = q;
    else
        s.quality = s.type == mr::AudioFileType::ogg ? q - 100 : q;
    s.sampleRate = sampleRate_.getSelectedId() > 0 ? (double) sampleRate_.getSelectedId() : 48000.0;
    s.range = range_.getSelectedId() == 2 ? ExportRange::loop : range_.getSelectedId() == 3 ? ExportRange::bars : ExportRange::song;
    s.firstBar = firstBar_.getText().getIntValue();
    s.lastBar = lastBarField_.getText().getIntValue();
    if (s.video)
        s.range = ExportRange::song;
    s.stems = stems_.getToggleState() && s.range == ExportRange::song && ! s.video;
    s.midi = midi_.getToggleState();
    s.videoMixOriginal = s.video && videoSound_.getToggleState();
    const auto name = baseName_ + (customName_ || s.video ? juce::String() : rangeSuffix()) + extensionFor (s);
    s.output = directory_.getChildFile (name);
    return s;
}

void ExportDialog::setSettings (const ExportSettings& s)
{
    {
        const juce::ScopedValueSetter<bool> quiet (updating_, true);
        format_.setSelectedId (s.video && video_ != juce::File() ? 5
                               : s.type == mr::AudioFileType::mp3 && ! mr::mp3EncoderAvailable() ? 1 : idForType (s.type),
                               juce::dontSendNotification);
        videoSound_.setToggleState (s.videoMixOriginal, juce::dontSendNotification);
        fillQualities (s);
        sampleRate_.setSelectedId (std::abs (s.sampleRate - 44100.0) < 1.0 ? 44100 : 48000, juce::dontSendNotification);
        const bool loopOk = s.range != ExportRange::loop || song_.composition.loop.has_value();
        range_.setSelectedId (! loopOk ? 1 : s.range == ExportRange::loop ? 2 : s.range == ExportRange::bars ? 3 : 1, juce::dontSendNotification);
        const auto bars = s.range == ExportRange::bars && s.firstBar >= 1 ? std::make_pair (s.firstBar, s.lastBar)
                                                                           : song_.selectedBars.value_or (std::make_pair (1, lastBar_));
        firstBar_.setText (juce::String (bars.first), false);
        lastBarField_.setText (juce::String (bars.second), false);
        stems_.setToggleState (s.stems, juce::dontSendNotification);
        midi_.setToggleState (s.midi, juce::dontSendNotification);
        if (s.output != juce::File())
        {
            directory_ = s.output.getParentDirectory();
            baseName_ = s.output.getFileNameWithoutExtension();
            customName_ = true;
        }
    }
    updateFile();
    updateState();
}

void ExportDialog::fillQualities (const ExportSettings& wanted)
{
    const juce::ScopedValueSetter<bool> quiet (updating_, true);
    const auto type = typeForId (format_.getSelectedId());
    quality_.clear (juce::dontSendNotification);
    int want = 0, fallback = 0;
    if (format_.getSelectedId() == 5)
    {
        for (int k : { 128, 192, 256, 320 })
            quality_.addItem ("AAC " + juce::String (k) + " kbps", k);
        quality_.setSelectedId (quality_.indexOfItemId (wanted.quality) >= 0 ? wanted.quality : 256, juce::dontSendNotification);
        return;
    }
    switch (type)
    {
        case mr::AudioFileType::wav:
            quality_.addItem (u8 ("16비트"), 16);
            quality_.addItem (u8 ("24비트"), 24);
            quality_.addItem (u8 ("32비트 부동소수점"), 32);
            want = wanted.bitDepth;
            fallback = 24;
            break;
        case mr::AudioFileType::flac:
            quality_.addItem (u8 ("16비트"), 16);
            quality_.addItem (u8 ("24비트"), 24);
            want = wanted.bitDepth;
            fallback = 24;
            break;
        case mr::AudioFileType::ogg:
        {
            static const int kbps[] { 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 500 };
            for (int q = 0; q <= 10; ++q)
                quality_.addItem (u8 ("품질 ") + juce::String (q) + u8 (" (약 ") + juce::String (kbps[q]) + " kbps)", 100 + q);
            want = wanted.quality >= 0 ? 100 + wanted.quality : 0;
            fallback = 100 + mr::defaultQuality (type);
            break;
        }
        case mr::AudioFileType::mp3:
            for (int k : { 128, 160, 192, 224, 256, 320 })
                quality_.addItem (juce::String (k) + " kbps", k);
            want = wanted.quality;
            fallback = mr::defaultQuality (type);
            break;
    }
    quality_.setSelectedId (quality_.indexOfItemId (want) >= 0 ? want : fallback, juce::dontSendNotification);
}

juce::String ExportDialog::rangeSuffix() const
{
    switch (range_.getSelectedId())
    {
        case 2: return "_loop";
        case 3: return "_bars" + firstBar_.getText() + "-" + lastBarField_.getText();
        default: return {};
    }
}

void ExportDialog::updateFile()
{
    const auto s = settings();
    file_.setText (s.output.getFullPathName(), juce::dontSendNotification);
    file_.setTooltip (s.output.getFullPathName());
}

void ExportDialog::updateState()
{
    const bool running = busy();
    for (auto* comp : std::initializer_list<juce::Component*> { &format_, &quality_, &sampleRate_, &range_, &firstBar_, &lastBarField_, &midi_, &change_ })
        comp->setEnabled (! running);
    const auto s = settings();
    stems_.setEnabled (! running && s.range == ExportRange::song);
    stems_.setVisible (! s.video);
    videoSound_.setVisible (s.video);
    videoSound_.setEnabled (! running);
    range_.setEnabled (! running && ! s.video);
    if (s.video && range_.getSelectedId() != 1)
    {
        const juce::ScopedValueSetter<bool> quiet (updating_, true);
        range_.setSelectedId (1, juce::dontSendNotification);
    }
    firstBar_.setVisible (s.range == ExportRange::bars);
    lastBarField_.setVisible (s.range == ExportRange::bars);
    close_.setButtonText (running ? u8 ("멈추기") : u8 ("닫기"));
    progress_.setTextToDisplay (running || result_ ? juce::String() : juce::String (" "));   // 시작 전에는 0% 를 보이지 않는다
    reveal_.setVisible (! running && result_ && result_->ok);
    repaint();

    juce::String problem;
    if (s.range == ExportRange::loop && s.type == mr::AudioFileType::mp3)
        problem = u8 ("MP3 는 앞뒤에 빈 샘플이 붙어 반복 구간을 샘플 단위로 이을 수 없습니다. Ogg Vorbis, FLAC, WAV 가운데에서 고르십시오.");
    else if (s.range == ExportRange::bars && (s.firstBar < 1 || s.lastBar < s.firstBar))
        problem = u8 ("마디 구간은 1 마디부터 셉니다. 끝 마디가 첫 마디보다 앞일 수 없습니다.");
    else if (s.range == ExportRange::bars && s.firstBar > lastBar_)
        problem = u8 ("곡은 ") + juce::String (lastBar_) + u8 ("마디까지입니다.");
    start_.setEnabled (! running && problem.isEmpty());
    if (running || result_)
        return;

    const auto& c = song_.composition;
    juce::String text;
    if (problem.isNotEmpty())
        text = problem;
    else
    {
        if (s.video)
        {
            const double offset = c.video ? c.video->offsetSeconds : 0.0;
            text = u8 ("곡을 영상 ") + video_.getFileName() + u8 ("에 입힙니다. 영상의 화면은 다시 인코딩하지 않고, 소리는 AAC 로 영상 길이만큼 씁니다 (영상 0초에 곡의 ")
                   + juce::String (offset, 3) + u8 ("초).") + (s.videoMixOriginal ? u8 (" 영상의 원래 소리를 섞습니다.") : juce::String());
        }
        else switch (s.range)
        {
            case ExportRange::song:
                text = u8 ("곡 전체 (약 ") + durationText (mr::estimateDurationSeconds (c)) + u8 (")를 내보냅니다.");
                break;
            case ExportRange::loop:
                text = u8 ("처음부터 반복 끝까지 내보내고 반복 자리를 ")
                       + (s.type == mr::AudioFileType::wav ? u8 ("smpl 청크") : u8 ("LOOPSTART, LOOPLENGTH 주석")) + u8 ("로 적습니다.");
                break;
            case ExportRange::bars:
                text = juce::String (s.firstBar) + "~" + juce::String (s.lastBar) + u8 ("마디와 그 뒤 ") + juce::String (s.fadeSeconds, 0)
                       + u8 ("초를 내보냅니다. 뒤 ") + juce::String (s.fadeSeconds, 0) + u8 ("초 동안 줄어듭니다.");
                break;
        }
        if (c.master.targetLufs)
            text << u8 (" 곡의 목표 음량 ") << juce::String (*c.master.targetLufs, 1) << u8 (" LUFS 에 맞춥니다.");
    }
    status_.setText (text, juce::dontSendNotification);
}

void ExportDialog::startExport (bool askBeforeOverwrite)
{
    if (busy() || ! start_.isEnabled())
        return;
    const auto s = settings();
    if (askBeforeOverwrite && s.output.existsAsFile() && s.output != confirmed_)
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::NoIcon)
                                          .withTitle (u8 ("파일 덮어쓰기"))
                                          .withMessage (s.output.getFileName() + u8 (" 파일이 이미 있습니다. 덮어씁니까?"))
                                          .withButton (u8 ("덮어쓰기"))
                                          .withButton (u8 ("취소"))
                                          .withAssociatedComponent (this),
                                      [safe = juce::Component::SafePointer<ExportDialog> (this)] (int choice)
                                      {
                                          if (safe != nullptr && choice == 1)
                                              safe->startExport (false);
                                      });
        return;
    }
    result_.reset();
    if (onStart)
        onStart (s);
    job_ = std::make_unique<ExportJob> (song_.composition, s, [this] (const ExportResult& r) { finished (r); });
    progressValue_ = 0.0;
    updateState();
    const bool plugins = usesPluginInstruments (song_.composition);
    status_.setText (plugins ? u8 ("플러그인 악기를 준비하고 있습니다.") : u8 ("내보내는 중입니다."), juce::dontSendNotification);
    startTimerHz (15);
    if (plugins)   // 악기를 만드는 동안 화면이 멈추므로 문구를 먼저 그린다
        juce::Timer::callAfterDelay (40, [safe = juce::Component::SafePointer<ExportDialog> (this)]
        {
            if (safe != nullptr && safe->job_ != nullptr)
                safe->job_->start();
        });
    else
        job_->start();
}

void ExportDialog::stopExport()
{
    if (busy())
        job_->cancel();
}

void ExportDialog::timerCallback()
{
    if (job_ != nullptr)
    {
        progressValue_ = job_->progress();
        if (progressValue_ > 0.0 && ! status_.getText().startsWith (u8 ("내보내는")))
            status_.setText (u8 ("내보내는 중입니다."), juce::dontSendNotification);
    }
}

void ExportDialog::finished (const ExportResult& r)
{
    stopTimer();
    result_ = r;
    builtOnMessageThread_ = job_ != nullptr && job_->builtOnMessageThread();
    job_.reset();   // 악기를 메시지 스레드에서 지운다
    progressValue_ = r.ok ? 1.0 : 0.0;
    juce::String text;
    if (r.ok)
    {
        const auto& st = r.report.stats;
        if (r.video)
            text << u8 ("영상에 입혔습니다. 영상 길이 ") << durationText (r.video->durationSeconds) << u8 (", 곡이 들어간 길이 ")
                 << durationText (r.video->musicSeconds) << u8 (", 곡 길이 ") << durationText (st.durationSeconds);
        else
            text << u8 ("내보냈습니다. 길이 ") << durationText (st.durationSeconds);
        if (st.lufs)
            text << ", " << juce::String (*st.lufs, 1) << " LUFS";
        text << u8 (", 트루 피크 ") << juce::String (st.truePeakDb, 1) << " dBTP";
        text << (st.clipping ? u8 (", 클리핑 ") + juce::String (st.clippedSamples) + u8 (" 샘플") : u8 (", 클리핑 없음"));
        if (r.report.loop.applied && r.report.loop.seamDb)
            text << u8 (", 반복 이음매 차이 ")
                 << (*r.report.loop.seamDb <= -1.0e8 ? u8 ("없음") : juce::String (*r.report.loop.seamDb, 1) + " dB");
        text << ".";
        if (r.stems != juce::File())
            text << u8 (" 스템 ") << juce::String ((int) r.report.tracks.size() + (int) r.report.buses.size() + 1) << u8 ("개");
        if (r.midi != juce::File())
            text << (r.stems != juce::File() ? u8 (", MIDI") : u8 (" MIDI"));
        if (r.stems != juce::File() || r.midi != juce::File())
            text << u8 ("도 함께 썼습니다.");
    }
    else if (r.cancelled)
        text = u8 ("내보내기를 멈췄습니다.");
    else
        text = u8 ("내보내지 못했습니다. ") + r.error;
    status_.setText (text, juce::dontSendNotification);
    status_.setColour (juce::Label::textColourId, r.ok || r.cancelled ? colours::text : colours::danger);
    updateState();
    if (onFinished)
        onFinished (r);
}

void ExportDialog::chooseFile()
{
    const auto s = settings();
    const juce::String pattern = juce::String ("*") + extensionFor (s);
    chooser_ = std::make_unique<juce::FileChooser> (u8 ("내보낼 파일"), s.output, pattern);
    chooser_->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                               | juce::FileBrowserComponent::warnAboutOverwriting,
                           [safe = juce::Component::SafePointer<ExportDialog> (this)] (const juce::FileChooser& fc)
                           {
                               if (safe == nullptr || fc.getResult() == juce::File())
                                   return;
                               auto chosen = fc.getResult();
                               const auto ext = extensionFor (safe->settings());
                               if (! chosen.hasFileExtension (ext))
                                   chosen = chosen.withFileExtension (ext);
                               safe->directory_ = chosen.getParentDirectory();
                               safe->baseName_ = chosen.getFileNameWithoutExtension();
                               safe->customName_ = true;
                               safe->confirmed_ = chosen;
                               safe->updateFile();
                           });
}

void ExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (colours::panel);
    g.setFont (uiFont (13.0f));
    g.setColour (colours::textDim);
    const juce::StringArray captions { u8 ("형식"), u8 ("샘플레이트"), u8 ("범위"), u8 ("함께 내보내기"), u8 ("저장할 곳") };
    for (int i = 0; i < captions.size(); ++i)
        g.drawText (captions[i], margin, margin + i * (rowHeight + rowGap), labelWidth - 8, rowHeight, juce::Justification::centredLeft);
    if (firstBar_.isVisible())
    {
        g.setColour (colours::text);
        g.drawText ("~", firstBar_.getRight(), firstBar_.getY(), 20, rowHeight, juce::Justification::centred);
        g.drawText (u8 ("마디"), lastBarField_.getRight() + 6, lastBarField_.getY(), 40, rowHeight, juce::Justification::centredLeft);
    }
    g.setColour (colours::outline);
    g.drawHorizontalLine (progress_.getY() - 10, (float) margin, (float) (getWidth() - margin));
}

void ExportDialog::resized()
{
    auto row = [this] (int i) { return juce::Rectangle<int> (margin + labelWidth, margin + i * (rowHeight + rowGap), getWidth() - 2 * margin - labelWidth, rowHeight); };
    auto r0 = row (0);
    format_.setBounds (r0.removeFromLeft (150));
    r0.removeFromLeft (10);
    quality_.setBounds (r0.removeFromLeft (200));
    sampleRate_.setBounds (row (1).removeFromLeft (150));
    auto r2 = row (2);
    range_.setBounds (r2.removeFromLeft (230));
    r2.removeFromLeft (14);
    firstBar_.setBounds (r2.removeFromLeft (52));
    r2.removeFromLeft (20);
    lastBarField_.setBounds (r2.removeFromLeft (52));
    auto r3 = row (3);
    stems_.setBounds (r3.withWidth (180));
    videoSound_.setBounds (r3.removeFromLeft (180));
    midi_.setBounds (r3.removeFromLeft (100));
    auto r4 = row (4);
    change_.setBounds (r4.removeFromRight (90));
    r4.removeFromRight (8);
    file_.setBounds (r4);

    const int top = margin + 5 * (rowHeight + rowGap) + 12;
    progress_.setBounds (margin, top, getWidth() - 2 * margin, 20);
    status_.setBounds (margin - 4, top + 26, getWidth() - 2 * margin + 8, 44);
    auto buttons = juce::Rectangle<int> (margin, getHeight() - margin - 30, getWidth() - 2 * margin, 30);
    start_.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    close_.setBounds (buttons.removeFromRight (90));
    reveal_.setBounds (buttons.removeFromLeft (150));
}

} // namespace daw
