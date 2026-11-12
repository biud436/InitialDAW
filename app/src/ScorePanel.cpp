#include "ScorePanel.h"

#include "Theme.h"

#if INITIALDAW_HAS_BRAVURA
 #include "BinaryData.h"
#endif

#include <cmath>

namespace daw
{

namespace
{

// SMuFL 기호
constexpr juce::juce_wchar gClef = 0xE050, fClef = 0xE062, percussionClef = 0xE069;
constexpr juce::juce_wchar headWhole = 0xE0A2, headHalf = 0xE0A3, headBlack = 0xE0A4, headX = 0xE0A9, headCircleX = 0xE0B3, headDiamond = 0xE0DB;
constexpr juce::juce_wchar flagUp[] = { 0xE240, 0xE242, 0xE244 }, flagDown[] = { 0xE241, 0xE243, 0xE245 };
constexpr juce::juce_wchar restWhole = 0xE4E3, restHalf = 0xE4E4, restQuarter = 0xE4E5, restEighth = 0xE4E6, rest16 = 0xE4E7, rest32 = 0xE4E8;
constexpr juce::juce_wchar accidentalGlyph[] = { 0xE264, 0xE260, 0xE261, 0xE262, 0xE263 };   // -2..2
constexpr juce::juce_wchar timeDigit0 = 0xE080, dotGlyph = 0xE1E7, braceGlyph = 0xE000, metQuarter = 0xE1D5;

constexpr int sharpPositions[] = { 8, 5, 9, 6, 3, 7, 4 };   // 높은음자리표, E4 가 0
constexpr int flatPositions[] = { 4, 7, 3, 6, 2, 5, 1 };

constexpr float headWidth = 1.18f;   // 칸 단위
constexpr float staffGap = 7.0f;     // 큰보표 사이
constexpr float topMargin = 7.0f, bottomMargin = 8.0f;

int beamLevel (const std::string& type) { return type == "eighth" ? 1 : type == "16th" ? 2 : type == "32nd" ? 3 : 0; }

juce::Typeface::Ptr musicTypeface()
{
#if INITIALDAW_HAS_BRAVURA
    static juce::Typeface::Ptr face = juce::Typeface::createSystemTypefaceFor (InitialDAWFonts::Bravura_otf, (size_t) InitialDAWFonts::Bravura_otfSize);
    return face;
#else
    return nullptr;
#endif
}

float spacingFor (int gap, float sp) { return sp * (1.8f + 2.4f * std::sqrt ((float) gap / (float) mr::divisionsPerQuarter)); }

} // namespace

bool ScorePanel::hasMusicFont() { return musicTypeface() != nullptr; }

ScorePanel::ScorePanel (Project& project, TimelineState& timeline) : project_ (project), timeline_ (timeline)
{
    timeline_.addListener (this);
    project_.addListener (this);
    vertical_.addListener (this);
    vertical_.setAutoHide (false);
    addAndMakeVisible (vertical_);
    setStaffSpace (sp_);
}

ScorePanel::~ScorePanel()
{
    timeline_.removeListener (this);
    project_.removeListener (this);
}

void ScorePanel::setStaffSpace (float space)
{
    sp_ = juce::jlimit (5.0f, 16.0f, space);
    if (auto face = musicTypeface())
        music_ = juce::Font (juce::FontOptions (face).withPointHeight (4.0f * sp_));
    layout();
    repaint();
}

void ScorePanel::refresh()
{
    track_ = timeline_.selectedTrack();
    score_ = {};
    if (project_.loaded() && track_ >= 0 && (size_t) track_ < project_.trackCount()
        && project_.composition().tracks[(size_t) track_].instrument != "audio")
    {
        const auto& c = project_.composition();
        mr::ScoreOptions o;
        o.tracks = { (size_t) track_ };
        o.percussion = percussion;
        double end = 0.0;   // 곡 전체의 마디 (다른 트랙의 음이 더 늦게 끝나도)
        for (const auto& t : c.tracks)
            for (const auto& n : t.notes)
                end = std::max (end, n.startBeat + n.durationBeat);
        o.endBeat = std::max (end, 1.0e-3);
        try
        {
            score_ = mr::buildScore (c, o);
        }
        catch (const std::exception&)
        {
            score_ = {};
        }
    }
    layout();
    repaint();
}

void ScorePanel::timelineChanged()
{
    if (timeline_.selectedTrack() != track_)
    {
        scroll_ = 0.0f;
        refresh();
        return;
    }
    // 재생 위치가 다른 줄로 넘어가면 그 줄이 보이게
    if (! score_.parts.empty())
    {
        const auto& measures = score_.parts.front().measures;
        const double beat = timeline_.playheadBeat();
        int found = -1;
        for (size_t s = 0; s < systems_.size() && found < 0; ++s)
            for (const auto& m : systems_[s].measures)
            {
                const double start = measures[m.index].startBeat;
                if (beat >= start - 1.0e-9 && beat < start + measures[m.index].length / (double) mr::divisionsPerQuarter)
                    found = (int) s;
            }
        if (found >= 0 && found != playheadSystem_)
        {
            playheadSystem_ = found;
            const float top = systems_[(size_t) found].top - topMargin * sp_;
            if (top < scroll_ || top + systemHeight() > scroll_ + (float) getHeight())
            {
                scroll_ = std::max (0.0f, top);
                vertical_.setCurrentRangeStart (scroll_, juce::dontSendNotification);
            }
        }
    }
    repaint();
}

void ScorePanel::projectEdited (int) { refresh(); }

void ScorePanel::scrollBarMoved (juce::ScrollBar*, double start)
{
    scroll_ = (float) start;
    repaint();
}

void ScorePanel::resized()
{
    vertical_.setBounds (getWidth() - scrollBarSize, 0, scrollBarSize, getHeight());
    layout();
}

float ScorePanel::systemHeight() const
{
    const int staves = score_.parts.empty() ? 1 : (int) score_.parts.front().clefs.size();
    return sp_ * (topMargin + 4.0f * (float) staves + staffGap * (float) (staves - 1) + bottomMargin);
}

float ScorePanel::staffTop (const SystemLayout& s, int staff) const { return s.top + (float) staff * sp_ * (4.0f + staffGap); }

float ScorePanel::positionY (float top, int position) const { return top + 4.0f * sp_ - (float) position * sp_ * 0.5f; }

int ScorePanel::positionOf (const mr::NotatedPitch& p, mr::Clef clef) const
{
    if (clef == mr::Clef::percussion)
        return p.displayOctave * 7 + p.displayStep - 30;
    const int index = p.spelling.octave * 7 + p.spelling.step;
    return clef == mr::Clef::bass ? index - 18 : index - 30;
}

float ScorePanel::xAt (const MeasureLayout& m, int division) const
{
    if (m.xAt.empty())
        return m.contentX;
    auto hi = m.xAt.lower_bound (division);
    if (hi == m.xAt.end())
        return std::prev (hi)->second;
    if (hi->first == division || hi == m.xAt.begin())
        return hi->second;
    const auto lo = std::prev (hi);
    return lo->second + (hi->second - lo->second) * (float) (division - lo->first) / (float) (hi->first - lo->first);
}

void ScorePanel::layout()
{
    systems_.clear();
    playheadSystem_ = -1;
    if (score_.parts.empty() || getWidth() < 100)
    {
        vertical_.setRangeLimits (0.0, 1.0);
        return;
    }
    const auto& part = score_.parts.front();
    const float left = sp_ * 3.0f, right = (float) getWidth() - scrollBarSize - sp_ * 2.0f;
    const float usable = right - left;
    auto keyWidth = [this] (const mr::KeySignature& k) { return k.fifths == 0 ? 0.0f : sp_ * (1.0f * (float) std::abs (k.fifths) + 0.6f); };
    const float timeWidth = sp_ * 3.0f, clefWidth = sp_ * 3.6f;

    // 마디마다의 자연스러운 너비
    struct Natural
    {
        std::vector<int> positions;
        std::vector<float> gaps;      // positions[i] 에서 positions[i + 1] 까지
        std::vector<float> extra;     // positions[i] 앞의 임시표 자리
        float width = 0.0f;
    };
    std::vector<Natural> natural (part.measures.size());
    for (size_t i = 0; i < part.measures.size(); ++i)
    {
        const auto& m = part.measures[i];
        std::map<int, bool> accidentals;
        for (const auto& staff : m.staves)
            for (const auto& e : staff)
            {
                bool any = false;
                for (const auto& p : e.pitches)
                    any = any || p.accidental.has_value();
                accidentals[e.start] = accidentals[e.start] || any;
            }
        auto& n = natural[i];
        for (const auto& [pos, acc] : accidentals)
        {
            n.positions.push_back (pos);
            n.extra.push_back (acc ? sp_ * 1.3f : 0.0f);
        }
        n.positions.push_back (m.length);
        n.extra.push_back (0.0f);
        n.width = sp_ * 1.2f;
        for (size_t k = 0; k + 1 < n.positions.size(); ++k)
        {
            const float g = std::max (spacingFor (n.positions[k + 1] - n.positions[k], sp_), sp_ * 2.4f);
            n.gaps.push_back (g);
            n.width += g + n.extra[k];
        }
        n.width = std::max (n.width, sp_ * 7.0f);
    }

    float y = sp_ * topMargin;
    size_t i = 0;
    while (i < part.measures.size())
    {
        SystemLayout s;
        s.top = y;
        const auto& first = part.measures[i];
        s.headerWidth = clefWidth + keyWidth (first.key) + (first.timeChanged ? timeWidth : 0.0f);
        float used = s.headerWidth;
        std::vector<float> prefix;
        size_t j = i;
        while (j < part.measures.size())
        {
            const auto& m = part.measures[j];
            float pre = 0.0f;
            if (j > i)
                pre = (m.keyChanged ? keyWidth (m.key) : 0.0f) + (m.timeChanged ? timeWidth : 0.0f);
            if (j > i && used + pre + natural[j].width > usable)
                break;
            used += pre + natural[j].width;
            prefix.push_back (pre);
            ++j;
        }
        // 줄을 채운다 (마지막 줄은 많이 늘리지 않는다)
        float naturalSum = 0.0f, prefixSum = 0.0f;
        for (size_t k = i; k < j; ++k)
        {
            naturalSum += natural[k].width;
            prefixSum += prefix[k - i];
        }
        float stretch = (usable - s.headerWidth - prefixSum) / std::max (1.0f, naturalSum);
        if (j == part.measures.size())
            stretch = std::min (stretch, 1.25f);
        stretch = std::max (stretch, 0.5f);
        float x = left + s.headerWidth;
        for (size_t k = i; k < j; ++k)
        {
            const auto& m = part.measures[k];
            MeasureLayout ml;
            ml.index = k;
            ml.showKey = k > i && m.keyChanged;
            ml.showTime = k > i && m.timeChanged;
            ml.x = x;
            const float pre = prefix[k - i];
            const auto& n = natural[k];
            ml.contentX = x + pre + sp_ * 1.2f * stretch;
            float cx = ml.contentX;
            for (size_t p = 0; p < n.positions.size(); ++p)
            {
                cx += n.extra[p] * std::min (1.0f, stretch);
                ml.xAt[n.positions[p]] = cx;
                if (p < n.gaps.size())
                    cx += n.gaps[p] * stretch;
            }
            ml.width = pre + n.width * stretch;
            x += ml.width;
            s.measures.push_back (std::move (ml));
        }
        systems_.push_back (std::move (s));
        y += systemHeight();
        i = j;
    }
    const float total = y + sp_ * 2.0f;
    vertical_.setRangeLimits (0.0, std::max ((double) total, (double) getHeight()));
    vertical_.setCurrentRange (scroll_, getHeight(), juce::dontSendNotification);
    scroll_ = (float) vertical_.getCurrentRangeStart();
}

void ScorePanel::glyph (juce::Graphics& g, juce::juce_wchar code, float x, float y, float scale) const
{
    if (scale != 1.0f)
    {
        g.setFont (music_.withPointHeight (music_.getHeightInPoints() * scale));
        g.drawSingleLineText (juce::String::charToString (code), juce::roundToInt (x), juce::roundToInt (y));
        g.setFont (music_);
        return;
    }
    g.drawSingleLineText (juce::String::charToString (code), juce::roundToInt (x), juce::roundToInt (y));
}

void ScorePanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::background);
    g.setColour (colours::outline);
    g.fillRect (0, 0, getWidth(), 1);
    auto message = [&] (const juce::String& text)
    {
        g.setColour (colours::textDim);
        g.setFont (uiFont (14.0f));
        g.drawFittedText (text, getLocalBounds().reduced (40), juce::Justification::centred, 3);
    };
    if (! hasMusicFont())
    {
        message (juce::String::fromUTF8 ("악보 글꼴(Bravura)이 없습니다. ./build.sh 가 external/bravura 에 받은 뒤 다시 빌드하면 악보가 보입니다."));
        return;
    }
    if (track_ < 0 || ! project_.loaded())
    {
        message (juce::String::fromUTF8 ("트랙을 고르면 그 트랙의 악보가 여기에 나옵니다."));
        return;
    }
    if (score_.parts.empty())
    {
        message (juce::String::fromUTF8 ("오디오 트랙에는 악보가 없습니다."));
        return;
    }
    // 종이처럼 밝은 바탕
    g.setColour (juce::Colour (0xfff4f1ea));
    g.fillRect (getLocalBounds().withTrimmedRight (scrollBarSize));
    g.setFont (music_);
    for (size_t s = 0; s < systems_.size(); ++s)
    {
        const auto& system = systems_[s];
        if (system.top + systemHeight() < scroll_ || system.top - topMargin * sp_ > scroll_ + (float) getHeight())
            continue;
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::translation (0.0f, -scroll_));
        paintSystem (g, system, s);
    }
    // 조를 추정했으면 알린다
    g.setFont (uiFont (11.0f));
    g.setColour (juce::Colour (0xff6b6457));
    const auto& part = score_.parts.front();
    g.drawText (juce::String::fromUTF8 (part.name.c_str())
                    + (part.percussion ? juce::String::fromUTF8 ("    타악기 보표")
                                       : "    " + juce::String (mr::keyName (score_.key))
                                             + (score_.keyEstimated ? juce::String::fromUTF8 (" (음으로 추정, 곡에 key 를 적으면 그 조)") : juce::String())),
                juce::Rectangle<int> (12, 4, getWidth() - 40, 16), juce::Justification::centredLeft);
}

void ScorePanel::paintHeader (juce::Graphics& g, const SystemLayout& s, float x, size_t measure, bool clefs, bool key, bool time)
{
    const auto& part = score_.parts.front();
    const auto& m = part.measures[measure];
    const int staves = (int) part.clefs.size();
    for (int st = 0; st < staves; ++st)
    {
        const float top = staffTop (s, st);
        const auto clef = part.clefs[(size_t) st];
        float cx = x;
        if (clefs)
        {
            const int pos = clef == mr::Clef::treble ? 2 : clef == mr::Clef::bass ? 6 : 4;
            glyph (g, clef == mr::Clef::treble ? gClef : clef == mr::Clef::bass ? fClef : percussionClef, cx + sp_ * 0.4f, positionY (top, pos));
            cx += sp_ * 3.6f;
        }
        if (key && clef != mr::Clef::percussion && m.key.fifths != 0)
        {
            const int shift = clef == mr::Clef::bass ? -2 : 0;
            for (int k = 0; k < std::abs (m.key.fifths); ++k)
            {
                const int pos = (m.key.fifths > 0 ? sharpPositions[k] : flatPositions[k]) + shift;
                glyph (g, accidentalGlyph[m.key.fifths > 0 ? 3 : 1], cx + (float) k * sp_, positionY (top, pos));
            }
            cx += sp_ * (1.0f * (float) std::abs (m.key.fifths) + 0.6f);
        }
        else if (key && clef != mr::Clef::percussion && measure > 0 && m.keyChanged)
            cx += 0.0f;
        if (time)
        {
            auto digits = [&] (int value, int pos)
            {
                const auto text = juce::String (value);
                float dx = cx + (text.length() == 1 ? sp_ * 0.5f : 0.0f);
                for (const auto ch : text)
                {
                    glyph (g, timeDigit0 + (juce::juce_wchar) (ch - '0'), dx, positionY (top, pos));
                    dx += sp_ * 1.7f;
                }
            };
            digits (m.beatsPerBar, 6);
            digits (m.beatUnit, 2);
        }
    }
}

void ScorePanel::paintSystem (juce::Graphics& g, const SystemLayout& s, size_t systemIndex)
{
    const auto& part = score_.parts.front();
    const int staves = (int) part.clefs.size();
    const float left = sp_ * 3.0f;
    const float right = s.measures.empty() ? left : s.measures.back().x + s.measures.back().width;
    const juce::Colour ink (0xff1d1b18);
    g.setColour (ink);
    // 오선
    for (int st = 0; st < staves; ++st)
        for (int line = 0; line < 5; ++line)
            g.fillRect (juce::Rectangle<float> (left, staffTop (s, st) + (float) line * sp_ - 0.5f, right - left, 1.1f));
    const float systemTop = staffTop (s, 0), systemBottom = staffTop (s, staves - 1) + 4.0f * sp_;
    g.fillRect (juce::Rectangle<float> (left - 0.6f, systemTop, 1.3f, systemBottom - systemTop));
    if (staves > 1)
        glyph (g, braceGlyph, left - sp_ * 1.6f, systemBottom, (systemBottom - systemTop) / (4.0f * sp_));
    // 마디 번호
    if (! s.measures.empty())
    {
        g.setFont (uiFont (sp_ * 1.3f));
        g.drawText (juce::String ((int) s.measures.front().index + 1), juce::Rectangle<float> (left, systemTop - sp_ * 3.4f, sp_ * 6.0f, sp_ * 1.6f),
                    juce::Justification::centredLeft);
        g.setFont (music_);
    }
    if (! s.measures.empty())
        paintHeader (g, s, left, s.measures.front().index, true, true, part.measures[s.measures.front().index].timeChanged);

    // 템포 기호: 줄 안에서 가까이 붙으면(8 칸 안) 뒤의 것만
    std::vector<std::pair<float, double>> tempoMarks;
    for (const auto& m : s.measures)
        for (const auto& t : part.measures[m.index].tempo)
            if (t.shown)
                tempoMarks.push_back ({ xAt (m, t.offset), t.bpm });
    for (size_t k = 0; k < tempoMarks.size(); ++k)
    {
        if (k + 1 < tempoMarks.size() && tempoMarks[k + 1].first - tempoMarks[k].first < sp_ * 8.0f)
            continue;
        const float tx = tempoMarks[k].first, above = systemTop - sp_ * 2.2f;
        glyph (g, metQuarter, tx, above, 0.75f);
        g.setFont (uiFont (sp_ * 1.35f, true));
        g.drawText ("= " + juce::String (juce::roundToInt (tempoMarks[k].second)),
                    juce::Rectangle<float> (tx + sp_ * 1.4f, above - sp_ * 1.4f, sp_ * 8.0f, sp_ * 1.8f), juce::Justification::centredLeft);
        g.setFont (music_);
    }

    const double playhead = timeline_.playheadBeat();
    for (const auto& m : s.measures)
    {
        const auto& measure = part.measures[m.index];
        if (m.showKey || m.showTime)
            paintHeader (g, s, m.x + sp_ * 0.6f, m.index, false, m.showKey, m.showTime);
        // 마디 줄 (큰보표는 이어서)
        g.setColour (ink);
        g.fillRect (juce::Rectangle<float> (m.x + m.width - 0.6f, systemTop, 1.2f, systemBottom - systemTop));
        // 마커
        for (const auto& name : measure.markers)
        {
            g.setFont (uiFont (sp_ * 1.3f, true));
            const auto text = juce::String::fromUTF8 (name.c_str());
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + sp_;
            const auto box = juce::Rectangle<float> (m.x + sp_ * 0.4f, systemTop - sp_ * 5.6f, w, sp_ * 1.8f);
            g.drawRect (box, 1.0f);
            g.drawText (text, box, juce::Justification::centred);
            g.setFont (music_);
        }
        for (int st = 0; st < staves; ++st)
            paintStaffEvents (g, s, m, st);
        // 재생 위치
        const double start = measure.startBeat, end = start + measure.length / (double) mr::divisionsPerQuarter;
        if (playhead >= start - 1.0e-9 && playhead < end)
        {
            const float px = xAt (m, (int) std::llround ((playhead - start) * mr::divisionsPerQuarter));
            g.setColour (colours::playhead.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> (m.x, systemTop - sp_, m.width, systemBottom - systemTop + 2.0f * sp_));
            g.setColour (colours::playhead.darker (0.3f));
            g.fillRect (juce::Rectangle<float> (px - 0.75f, systemTop - sp_, 1.5f, systemBottom - systemTop + 2.0f * sp_));
            g.setColour (ink);
        }
    }
    juce::ignoreUnused (systemIndex);
}

void ScorePanel::paintStaffEvents (juce::Graphics& g, const SystemLayout& s, const MeasureLayout& m, int staff)
{
    const auto& part = score_.parts.front();
    const auto& measure = part.measures[m.index];
    const auto& events = measure.staves[(size_t) staff];
    const auto clef = part.clefs[(size_t) staff];
    const float top = staffTop (s, staff);
    const juce::Colour ink (0xff1d1b18);
    const float head = headWidth * sp_;

    // 빔 묶음: 8분음표보다 짧은 음이 한 박(셋잇단은 그 칸) 안에서 이어지면
    const int beatLen = measure.beatUnit == 8 && measure.beatsPerBar % 3 == 0 ? 36 : measure.beatUnit >= 8 ? 24 : 96 / measure.beatUnit;
    std::vector<int> group (events.size(), -1);
    std::vector<std::vector<size_t>> groups;
    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        if (e.rest || beamLevel (e.type) == 0)
            continue;
        const int key = e.triplet ? 1000 + e.start / mr::divisionsPerQuarter : e.start / beatLen;
        if (i > 0 && group[i - 1] >= 0)
        {
            const auto& prev = events[i - 1];
            const int prevKey = prev.triplet ? 1000 + prev.start / mr::divisionsPerQuarter : prev.start / beatLen;
            if (prevKey == key)
            {
                group[i] = group[i - 1];
                groups[(size_t) group[i]].push_back (i);
                continue;
            }
        }
        group[i] = (int) groups.size();
        groups.push_back ({ i });
    }
    for (size_t i = 0; i < events.size(); ++i)
        if (group[i] >= 0 && groups[(size_t) group[i]].size() < 2)
            group[i] = -1;
    // 묶음마다의 기둥 방향과 빔 높이
    std::vector<bool> groupDown (groups.size(), false);
    std::vector<float> groupBeam (groups.size(), 0.0f);
    for (size_t k = 0; k < groups.size(); ++k)
    {
        if (groups[k].size() < 2)
            continue;
        float sum = 0.0f;
        int count = 0;
        float highest = 1.0e9f, lowest = -1.0e9f;
        for (const size_t i : groups[k])
            for (const auto& p : events[i].pitches)
            {
                const int pos = positionOf (p, clef);
                sum += (float) pos;
                ++count;
                highest = std::min (highest, positionY (top, pos));
                lowest = std::max (lowest, positionY (top, pos));
            }
        groupDown[k] = count > 0 && (clef == mr::Clef::percussion ? sum / (float) count < 3.0f : sum / (float) count >= 4.0f);
        groupBeam[k] = groupDown[k] ? lowest + 3.2f * sp_ : highest - 3.2f * sp_;
    }

    auto stemX = [&] (float x, bool down) { return down ? x + 0.06f * sp_ : x + head - 0.06f * sp_; };
    for (size_t i = 0; i < events.size(); ++i)
    {
        const auto& e = events[i];
        const float x = xAt (m, e.start);
        g.setColour (ink);
        if (e.measureRest)
        {
            glyph (g, restWhole, m.x + m.width * 0.5f - head * 0.5f, positionY (top, 6));
            continue;
        }
        if (e.rest)
        {
            const juce::juce_wchar code = e.type == "whole" ? restWhole : e.type == "half" ? restHalf : e.type == "quarter" ? restQuarter
                                        : e.type == "eighth" ? restEighth : e.type == "16th" ? rest16 : rest32;
            glyph (g, code, x, positionY (top, e.type == "whole" ? 6 : 4));
            for (int d = 0; d < e.dots; ++d)
                glyph (g, dotGlyph, x + head + sp_ * (0.5f + 0.6f * (float) d), positionY (top, 5));
            continue;
        }
        // 음표 머리 (2도로 붙은 음은 기둥 반대쪽으로)
        std::vector<int> positions;
        for (const auto& p : e.pitches)
            positions.push_back (positionOf (p, clef));
        const bool whole = e.type == "whole";
        bool down = false;
        if (group[i] >= 0)
            down = groupDown[(size_t) group[i]];
        else
        {
            float sum = 0.0f;
            for (const int pos : positions)
                sum += (float) pos;
            const float average = positions.empty() ? 4.0f : sum / (float) positions.size();
            down = ! positions.empty() && (clef == mr::Clef::percussion ? average < 3.0f : average >= 4.0f);   // 타악기는 킥과 페달만 아래로
        }
        std::vector<float> offsets (positions.size(), 0.0f);
        if (down)
        {
            for (size_t k = positions.size(); k-- > 1;)
                if (positions[k] - positions[k - 1] == 1 && offsets[k] == 0.0f)
                    offsets[k - 1] = -head;
        }
        else
            for (size_t k = 1; k < positions.size(); ++k)
                if (positions[k] - positions[k - 1] == 1 && offsets[k - 1] == 0.0f)
                    offsets[k] = head;
        int accidentalIndex = 0;
        for (size_t k = 0; k < positions.size(); ++k)
        {
            const auto& p = e.pitches[k];
            const int pos = positions[k];
            const float y = positionY (top, pos);
            const float hx = x + offsets[k];
            // 덧줄
            for (int q = -2; q >= pos; q -= 2)
                g.fillRect (juce::Rectangle<float> (hx - 0.4f * sp_, positionY (top, q) - 0.5f, head + 0.8f * sp_, 1.1f));
            for (int q = 10; q <= pos; q += 2)
                g.fillRect (juce::Rectangle<float> (hx - 0.4f * sp_, positionY (top, q) - 0.5f, head + 0.8f * sp_, 1.1f));
            juce::juce_wchar code = whole ? headWhole : e.type == "half" ? headHalf : headBlack;
            if (p.notehead == "x")
                code = headX;
            else if (p.notehead == "circle-x")
                code = headCircleX;
            else if (p.notehead == "diamond")
                code = headDiamond;
            glyph (g, code, hx, y);
            if (p.accidental)
                glyph (g, accidentalGlyph[*p.accidental + 2], x - sp_ * (1.3f + 1.1f * (float) (accidentalIndex++ % 2)), y);
            for (int d = 0; d < e.dots; ++d)
                glyph (g, dotGlyph, x + head + sp_ * (0.5f + 0.6f * (float) d) + std::max (0.0f, offsets[k]), positionY (top, pos % 2 == 0 ? pos + 1 : pos));
            // 붙임줄: 같은 높이의 다음 음까지 (다음 줄이면 이 줄 끝까지)
            if (p.tieStart)
            {
                float endX = s.measures.back().x + s.measures.back().width;
                if (i + 1 < events.size())
                    endX = xAt (m, events[i + 1].start);
                else if (&m != &s.measures.back())
                {
                    const auto& next = *(&m + 1);
                    endX = next.contentX;
                }
                juce::Path tie;
                const bool below = ! down;
                const float y0 = y + (below ? 0.7f : -0.7f) * sp_;
                const float x0 = hx + head + 0.15f * sp_, x1 = endX - 0.15f * sp_;
                const float bulge = (below ? 1.0f : -1.0f) * std::min (sp_ * 1.2f, (x1 - x0) * 0.15f + sp_ * 0.3f);
                tie.startNewSubPath (x0, y0);
                tie.quadraticTo ((x0 + x1) * 0.5f, y0 + bulge, x1, y0);
                tie.quadraticTo ((x0 + x1) * 0.5f, y0 + bulge * 0.7f, x0, y0);
                g.fillPath (tie);
                g.strokePath (tie, juce::PathStrokeType (0.6f));
            }
        }
        // 기둥과 꼬리
        if (! whole && ! positions.empty())
        {
            const float highY = positionY (top, positions.back()), lowY = positionY (top, positions.front());
            const float sx = stemX (x, down);
            float endY = down ? lowY + 3.5f * sp_ : highY - 3.5f * sp_;
            if (group[i] >= 0)
                endY = groupBeam[(size_t) group[i]];
            const float from = down ? highY : lowY;
            g.fillRect (juce::Rectangle<float> (sx - 0.06f * sp_, std::min (from, endY), 0.13f * sp_, std::abs (endY - from)));
            const int level = beamLevel (e.type);
            if (level > 0 && group[i] < 0)
                glyph (g, down ? flagDown[level - 1] : flagUp[level - 1], sx - 0.06f * sp_, endY);
        }
        // 가사
        if (! e.lyric.empty() && e.lyric != "-")
        {
            g.setFont (uiFont (sp_ * 1.4f));
            g.drawText (juce::String::fromUTF8 (e.lyric.c_str()), juce::Rectangle<float> (x - sp_ * 2.0f, top + 4.0f * sp_ + 2.4f * sp_, head + sp_ * 4.0f, sp_ * 1.8f),
                        juce::Justification::centred);
            g.setFont (music_);
        }
    }
    // 빔
    for (size_t k = 0; k < groups.size(); ++k)
    {
        const auto& members = groups[k];
        if (members.size() < 2)
            continue;
        const bool down = groupDown[k];
        const float beamY = groupBeam[k];
        const float thickness = 0.5f * sp_;
        auto beam = [&] (float x0, float x1, int level)
        {
            const float y = beamY + (down ? -1.0f : 1.0f) * (float) (level - 1) * 0.75f * sp_;
            g.fillRect (juce::Rectangle<float> (x0, down ? y - thickness : y, x1 - x0, thickness));
        };
        std::vector<float> xs;
        for (const size_t i : members)
            xs.push_back (stemX (xAt (m, events[i].start), down));
        beam (xs.front() - 0.06f * sp_, xs.back() + 0.07f * sp_, 1);
        for (int level = 2; level <= 3; ++level)
            for (size_t a = 0; a < members.size(); ++a)
            {
                if (beamLevel (events[members[a]].type) < level)
                    continue;
                const bool nextToo = a + 1 < members.size() && beamLevel (events[members[a + 1]].type) >= level;
                const bool prevToo = a > 0 && beamLevel (events[members[a - 1]].type) >= level;
                if (nextToo)
                    beam (xs[a] - 0.06f * sp_, xs[a + 1] + 0.07f * sp_, level);
                else if (! prevToo)
                {
                    const float stub = sp_ * 1.1f;
                    if (a > 0)
                        beam (xs[a] - stub, xs[a] + 0.07f * sp_, level);
                    else
                        beam (xs[a] - 0.06f * sp_, xs[a] + stub, level);
                }
            }
        // 셋잇단 숫자
        if (events[members.front()].triplet)
        {
            g.setFont (uiFont (sp_ * 1.3f, true).italicised());
            const float cx = (xs.front() + xs.back()) * 0.5f;
            const float ty = down ? beamY + sp_ * 0.6f : beamY - sp_ * 2.2f;
            g.drawText ("3", juce::Rectangle<float> (cx - sp_, ty, sp_ * 2.0f, sp_ * 1.6f), juce::Justification::centred);
            g.setFont (music_);
        }
    }
    // 빔 없는 셋잇단 (4분음표 셋잇단 등): 묶음 위에 숫자
    for (size_t i = 0; i < events.size(); ++i)
        if (events[i].tupletStart && group[i] < 0)
        {
            size_t j = i;
            while (j + 1 < events.size() && ! events[j].tupletStop)
                ++j;
            if (j > i && group[j] >= 0)
                continue;
            g.setFont (uiFont (sp_ * 1.3f, true).italicised());
            const float cx = (xAt (m, events[i].start) + xAt (m, events[j].start) + head) * 0.5f;
            g.drawText ("3", juce::Rectangle<float> (cx - sp_, top - sp_ * 3.0f, sp_ * 2.0f, sp_ * 1.6f), juce::Justification::centred);
            g.setFont (music_);
        }
}

std::optional<int> ScorePanel::measureAt (juce::Point<float> p) const
{
    const float y = p.y + scroll_;
    for (const auto& s : systems_)
    {
        const float top = s.top - topMargin * sp_ * 0.5f, bottom = s.top + systemHeight() - (topMargin + bottomMargin * 0.5f) * sp_;
        if (y < top || y > bottom)
            continue;
        for (const auto& m : s.measures)
            if (p.x >= m.x && p.x < m.x + m.width)
                return (int) m.index;
    }
    return std::nullopt;
}

juce::Point<float> ScorePanel::measurePoint (int measure) const
{
    for (const auto& s : systems_)
        for (const auto& m : s.measures)
            if ((int) m.index == measure)
                return { m.x + m.width * 0.5f, s.top + 2.0f * sp_ - scroll_ };
    return {};
}

void ScorePanel::mouseDown (const juce::MouseEvent& e)
{
    if (const auto m = measureAt (e.position); m && onSeek && ! score_.parts.empty())
        onSeek (score_.parts.front().measures[(size_t) *m].startBeat);
}

void ScorePanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        setStaffSpace (sp_ * (float) std::pow (1.0015, wheel.deltaY * 400.0));
        return;
    }
    vertical_.setCurrentRangeStart (vertical_.getCurrentRangeStart() - wheel.deltaY * 300.0);
}

} // namespace daw
