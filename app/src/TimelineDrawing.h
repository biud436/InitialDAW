// 트랙 보기와 피아노 롤이 함께 쓰는 눈금자와 격자 그리기. 가로 위치는 TimelineState 의 박과 확대율로 정한다.
#pragma once

#include "Project.h"
#include "Theme.h"

#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace daw
{

struct TimelineGeometry
{
    int left = 0;          // 0박 보기 시작이 놓이는 x
    int right = 0;         // 그리는 영역의 오른쪽 끝
    double startBeat = 0.0;
    double pixelsPerBeat = 28.0;

    TimelineGeometry (const TimelineState& t, int leftX, int rightX)
        : left (leftX), right (rightX), startBeat (t.viewStartBeat()), pixelsPerBeat (t.pixelsPerBeat()) {}

    float x (double beat) const { return (float) (left + (beat - startBeat) * pixelsPerBeat); }
    double beat (float xPos) const { return startBeat + (xPos - left) / pixelsPerBeat; }
    double endBeat() const { return beat ((float) right); }
};

// 마디 번호를 몇 마디마다 쓸지 (글자가 겹치지 않게)
inline int barLabelStep (double barPixels)
{
    int step = 1;
    while (barPixels * step < 56.0 && step < 1024)
        step *= 2;
    return step;
}

inline void drawGrid (juce::Graphics& g, juce::Rectangle<int> area, const TimelineGeometry& geo, const mr::MeterMap& meters)
{
    const double barPx = meters.barBeatsAt (geo.startBeat) * geo.pixelsPerBeat;
    const int step = barLabelStep (barPx);
    if (geo.pixelsPerBeat >= 9.0)
        for (double b = std::floor (geo.startBeat); b <= geo.endBeat(); b += 1.0)
        {
            const float xx = geo.x (b);
            if (xx < (float) area.getX() || xx > (float) area.getRight() || meters.isBarLine (b))
                continue;
            g.setColour (colours::beatLine);
            g.fillRect (xx, (float) area.getY(), 1.0f, (float) area.getHeight());
        }
    for (const double b : meters.barLines (std::max (0.0, geo.startBeat - 1.0), geo.endBeat() + 1.0))
    {
        const float xx = geo.x (b);
        if (xx < (float) area.getX() || xx > (float) area.getRight())
            continue;
        const bool strong = meters.barAt (b) % step == 0;
        g.setColour (strong ? colours::barLine : colours::beatLine.brighter (0.05f));
        g.fillRect (xx, (float) area.getY(), 1.0f, (float) area.getHeight());
    }
}

// 눈금자의 반복 구간 띠 (위쪽 절반). 양 끝의 손잡이는 끌어 고친다.
inline juce::Rectangle<float> loopBand (juce::Rectangle<int> area, const TimelineGeometry& geo, const std::pair<double, double>& loop)
{
    const float x0 = std::max ((float) area.getX(), geo.x (loop.first));
    const float x1 = std::min ((float) area.getRight(), geo.x (loop.second));
    return { x0, (float) area.getY(), std::max (0.0f, x1 - x0), area.getHeight() * 0.45f };
}

inline void drawRuler (juce::Graphics& g, juce::Rectangle<int> area, const TimelineGeometry& geo, const mr::MeterMap& meters,
                       double lengthBeats, double playheadBeat, const std::optional<std::pair<double, double>>& loop = std::nullopt,
                       bool loopOn = false)
{
    g.setColour (colours::panel);
    g.fillRect (area);
    // 곡의 길이 밖은 어둡게
    const float endX = geo.x (lengthBeats);
    if (endX < (float) area.getRight())
    {
        g.setColour (colours::background.withAlpha (0.6f));
        g.fillRect (juce::Rectangle<float> (std::max (endX, (float) area.getX()), (float) area.getY(),
                                            (float) area.getRight() - std::max (endX, (float) area.getX()), (float) area.getHeight()));
    }
    if (loop && loop->second > loop->first)
    {
        const auto band = loopBand (area, geo, *loop);
        if (band.getWidth() > 0.0f)
        {
            g.setColour (colours::accent.withAlpha (loopOn ? 0.45f : 0.2f));
            g.fillRect (band);
            g.setColour (colours::accent.withAlpha (loopOn ? 1.0f : 0.6f));
            g.fillRect (band.withWidth (2.0f));
            g.fillRect (band.withX (band.getRight() - 2.0f).withWidth (2.0f));
            g.fillRect (band.withHeight (1.0f).withY (band.getBottom() - 1.0f));
        }
    }
    const double barPx = meters.barBeatsAt (geo.startBeat) * geo.pixelsPerBeat;
    const int step = barLabelStep (barPx);
    g.setFont (monoFont (11.0f));
    for (const double b : meters.barLines (std::max (0.0, geo.startBeat - 64.0), geo.endBeat() + 1.0))
    {
        const float xx = geo.x (b);
        if (xx > (float) area.getRight())
            break;
        const int bar = meters.barAt (b);
        if (bar % step == 0)
        {
            g.setColour (colours::barLine.brighter (0.2f));
            g.fillRect (xx, (float) area.getY() + area.getHeight() * 0.45f, 1.0f, area.getHeight() * 0.55f);
            g.setColour (colours::textDim);
            g.drawText (juce::String (bar + 1), juce::Rectangle<float> (xx + 4.0f, (float) area.getY() + 2.0f, 48.0f, area.getHeight() * 0.5f),
                        juce::Justification::centredLeft, false);
        }
        else if (barPx >= 6.0)
        {
            g.setColour (colours::barLine);
            g.fillRect (xx, (float) area.getY() + area.getHeight() * 0.7f, 1.0f, area.getHeight() * 0.3f);
        }
    }
    // 박자가 바뀌는 자리 (곡 처음의 박자도)
    g.setFont (uiFont (10.0f, true));
    for (const auto& seg : meters.segments())
    {
        const float xx = geo.x (seg.startBeat);
        if (xx < (float) area.getX() - 40.0f || xx > (float) area.getRight() || (seg.startBeat == 0.0 && meters.constant()))
            continue;
        g.setColour (colours::accent.withAlpha (0.9f));
        g.drawText (juce::String (seg.beatsPerBar) + "/" + juce::String (seg.beatUnit),
                    juce::Rectangle<float> (xx + 4.0f, (float) area.getY() + area.getHeight() * 0.5f, 40.0f, area.getHeight() * 0.45f),
                    juce::Justification::centredLeft, false);
    }
    g.setColour (colours::outline);
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    const float px = geo.x (playheadBeat);
    if (px >= (float) area.getX() - 6.0f && px <= (float) area.getRight() + 6.0f)
    {
        juce::Path tri;
        const float bottom = (float) area.getBottom();
        tri.addTriangle (px - 6.0f, bottom - 10.0f, px + 6.0f, bottom - 10.0f, px, bottom);
        g.setColour (colours::playhead);
        g.fillPath (tri);
    }
}

// 눈금자에서 반복 구간을 끄는 동작 (트랙 보기와 피아노 롤이 같이 쓴다). ⌥ 끌기는 새 구간, 띠의 양 끝 가까이는 그 끝을 옮긴다.
// 박에 맞추고, 한 박이 9픽셀 아래면 마디에 맞춘다.
struct LoopRulerDrag
{
    enum class Mode { none, create, start, end };
    Mode mode = Mode::none;
    double anchor = 0.0;    // 움직이지 않는 끝
    bool bars = false;      // 마디에 맞춘다 (아니면 박)
    const mr::MeterMap* meters = nullptr;

    static bool snapsToBars (const TimelineGeometry& geo) { return geo.pixelsPerBeat < 9.0; }
    // 박이나 마디(박자 지도)에 맞춘다
    static double snapBeat (double beat, bool toBars, const mr::MeterMap& meters)
    {
        beat = std::max (0.0, beat);
        return toBars ? meters.roundToBar (beat) : std::round (beat);
    }
    double snapped (double beat) const { return meters != nullptr ? snapBeat (beat, bars, *meters) : std::max (0.0, std::round (beat)); }

    // 눈금자를 눌렀다. 끌 것이 있으면 true (그러면 재생 위치를 옮기지 않는다)
    bool begin (float x, juce::Rectangle<int> rulerArea, const TimelineGeometry& geo, const mr::MeterMap& meterMap,
                const std::optional<std::pair<double, double>>& loop, bool alt)
    {
        meters = &meterMap;
        bars = snapsToBars (geo);
        mode = Mode::none;
        if (loop && loop->second > loop->first && ! alt)
        {
            const auto band = loopBand (rulerArea, geo, *loop);
            if (std::abs (x - band.getX()) <= 6.0f)
            {
                mode = Mode::start;
                anchor = loop->second;
                return true;
            }
            if (std::abs (x - band.getRight()) <= 6.0f)
            {
                mode = Mode::end;
                anchor = loop->first;
                return true;
            }
        }
        if (alt)
        {
            mode = Mode::create;
            anchor = snapped (geo.beat (x));
            return true;
        }
        return false;
    }

    // 끄는 동안의 구간 (비면 아직 너비가 없다)
    std::optional<std::pair<double, double>> drag (float x, const TimelineGeometry& geo) const
    {
        if (mode == Mode::none)
            return std::nullopt;
        const double b = snapped (geo.beat (x));
        const double lo = std::min (anchor, b), hi = std::max (anchor, b);
        if (hi - lo < 0.5)
            return std::nullopt;
        return std::make_pair (lo, hi);
    }
};

// 마커 띠: 박마다 깃발과 이름 (다음 마커 앞에서 자른다). moving 이면 그 마커를 끄는 자리에 그린다
struct MarkerView
{
    double beat = 0.0;
    juce::String name;
};

inline juce::Rectangle<float> markerLabel (juce::Rectangle<int> area, const TimelineGeometry& geo, const std::vector<MarkerView>& markers, size_t i)
{
    const float x = geo.x (markers[i].beat);
    const float textW = (float) juce::GlyphArrangement::getStringWidthInt (uiFont (12.0f, true), markers[i].name) + 14.0f;
    float right = x + std::max (18.0f, textW);
    for (size_t k = 0; k < markers.size(); ++k)
        if (k != i && markers[k].beat > markers[i].beat)
            right = std::min (right, geo.x (markers[k].beat) - 2.0f);
    return { x, (float) area.getY() + 2.0f, std::max (6.0f, right - x), (float) area.getHeight() - 4.0f };
}

inline void drawMarkers (juce::Graphics& g, juce::Rectangle<int> area, const TimelineGeometry& geo, const std::vector<MarkerView>& markers,
                         int selected = -1)
{
    g.setColour (colours::panel.darker (0.12f));
    g.fillRect (area);
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto label = markerLabel (area, geo, markers, i);
        if (label.getRight() < (float) area.getX() || label.getX() > (float) area.getRight())
            continue;
        const bool on = (int) i == selected;
        g.setColour (on ? colours::marker : colours::marker.withAlpha (0.55f));
        g.fillRoundedRectangle (label, 3.0f);
        g.fillRect (label.getX(), (float) area.getY(), 2.0f, (float) area.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (uiFont (12.0f, true));
        g.drawText (markers[i].name, label.withTrimmedLeft (6.0f).withTrimmedRight (3.0f), juce::Justification::centredLeft, true);
    }
    g.setColour (colours::outline);
    g.fillRect (area.getX(), area.getBottom() - 1, area.getWidth(), 1);
}

// 피아노 롤 눈금자의 마커: 아래쪽 절반에 작은 깃발과 이름 (다음 마커 앞에서 자른다)
inline void drawRulerMarkers (juce::Graphics& g, juce::Rectangle<int> area, const TimelineGeometry& geo, const std::vector<mr::Marker>& markers)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area);
    const auto font = uiFont (10.0f, true);
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto name = juce::String::fromUTF8 (markers[i].name.c_str());
        const float x = geo.x (markers[i].beat);
        float right = x + juce::GlyphArrangement::getStringWidth (font, name) + 10.0f;
        if (i + 1 < markers.size())
            right = std::min (right, geo.x (markers[i + 1].beat) - 2.0f);
        if (right < (float) area.getX() || x > (float) area.getRight())
            continue;
        const juce::Rectangle<float> box (x, (float) area.getY() + area.getHeight() * 0.5f, std::max (4.0f, right - x), area.getHeight() * 0.46f);
        g.setColour (colours::marker.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (font);
        g.drawText (name, box.withTrimmedLeft (4.0f).withTrimmedRight (2.0f), juce::Justification::centredLeft, true);
    }
}

inline void drawPlayhead (juce::Graphics& g, juce::Rectangle<int> area, const TimelineGeometry& geo, double playheadBeat)
{
    const float px = geo.x (playheadBeat);
    if (px < (float) area.getX() || px > (float) area.getRight())
        return;
    g.setColour (colours::playhead);
    g.fillRect (px - 0.5f, (float) area.getY(), 1.5f, (float) area.getHeight());
}

// 곡선을 영역 안의 경로로. norm 은 값을 높이(0 아래, 1 위)로 바꾼다
template <typename Norm>
inline juce::Path automationPath (const mr::AutomationLane& lane, Norm&& norm, juce::Rectangle<float> area, const TimelineGeometry& geo, bool closed)
{
    juce::Path p;
    auto yFor = [&] (double v) { return area.getBottom() - (float) std::clamp ((double) norm (v), 0.0, 1.0) * area.getHeight(); };
    if (lane.points.empty())
        return p;
    const float x0 = std::max (area.getX(), geo.x (lane.points.front().beat));   // 첫 점 앞은 그리지 않는다
    const float x1 = area.getRight();
    bool started = false;
    for (float xx = x0; xx <= x1; xx += 2.0f)
    {
        const double v = lane.valueAt (geo.beat (xx));
        if (! started)
        {
            if (closed)
            {
                p.startNewSubPath (xx, area.getBottom());
                p.lineTo (xx, yFor (v));
            }
            else
                p.startNewSubPath (xx, yFor (v));
            started = true;
        }
        else
            p.lineTo (xx, yFor (v));
    }
    if (closed && started)
    {
        p.lineTo (x1, area.getBottom());
        p.closeSubPath();
    }
    return p;
}

// 컨트롤러 곡선 (값 0..127, 피치 벤드는 -1..1)
inline juce::Path automationPath (const mr::AutomationLane& lane, int id, juce::Rectangle<float> area, const TimelineGeometry& geo, bool closed)
{
    const bool bipolar = id == mr::kPitchBend;
    return automationPath (lane, [bipolar] (double v) { return bipolar ? (v + 1.0) * 0.5 : v / 127.0; }, area, geo, closed);
}

} // namespace daw
