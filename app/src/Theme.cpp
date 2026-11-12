#include "Theme.h"

namespace daw
{

namespace
{
// 한글과 라틴 글자를 함께 그리는 시스템 글꼴
juce::String sansName()
{
   #if JUCE_MAC
    return "Apple SD Gothic Neo";
   #elif JUCE_WINDOWS
    return "Malgun Gothic";
   #else
    return "Noto Sans CJK KR";
   #endif
}
} // namespace

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (sansName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font monoFont (float height)
{
   #if JUCE_MAC
    return juce::Font (juce::FontOptions ("Menlo", height, juce::Font::plain));
   #else
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
   #endif
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colours::background);
    setColour (juce::DocumentWindow::textColourId, colours::text);
    setColour (juce::TextButton::buttonColourId, colours::panelRaised);
    setColour (juce::TextButton::buttonOnColourId, colours::accent);
    setColour (juce::TextButton::textColourOffId, colours::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    setColour (juce::Label::textColourId, colours::text);
    setColour (juce::Slider::thumbColourId, colours::text);
    setColour (juce::Slider::trackColourId, colours::accent);
    setColour (juce::Slider::backgroundColourId, colours::outline);
    setColour (juce::ScrollBar::thumbColourId, colours::barLine);
    setColour (juce::PopupMenu::backgroundColourId, colours::panelRaised);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::selection);
    setColour (juce::ComboBox::backgroundColourId, colours::panelRaised);
    setColour (juce::ComboBox::textColourId, colours::text);
    setColour (juce::ComboBox::outlineColourId, colours::outline);
    setColour (juce::ListBox::backgroundColourId, colours::panel);
    setColour (juce::AlertWindow::backgroundColourId, colours::panelRaised);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelRaised);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::outline);
    setColour (juce::ToggleButton::textColourId, colours::text);
    setColour (juce::ToggleButton::tickColourId, colours::accent);
    setColour (juce::TextEditor::backgroundColourId, colours::panel);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::outline);
    setColour (juce::CaretComponent::caretColourId, colours::accent);
    setColour (juce::Slider::textBoxTextColourId, colours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, colours::background);
    setColour (juce::Slider::textBoxOutlineColourId, colours::outline);
    setColour (juce::Slider::textBoxHighlightColourId, colours::selection);
    setColour (juce::Slider::rotarySliderFillColourId, colours::accent);
    setDefaultSansSerifTypefaceName (uiFont (14.0f).getTypefaceName());
}

juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    return juce::LookAndFeel_V4::getTypefaceForFont (font);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    auto colour = backgroundColour;
    if (down)
        colour = colour.brighter (0.15f);
    else if (highlighted)
        colour = colour.brighter (0.07f);
    g.setColour (colour);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (button.getToggleState() ? colour.brighter (0.2f) : colours::outline);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return uiFont (std::min (14.0f, (float) buttonHeight * 0.62f), true);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return uiFont (13.0f); }
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }
juce::Font LookAndFeel::getAlertWindowMessageFont() { return uiFont (14.0f); }
juce::Font LookAndFeel::getAlertWindowFont() { return uiFont (13.0f); }

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float,
                                    float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style == juce::Slider::LinearVertical)
    {
        // 믹서 페이더: 가는 홈과 가로 손잡이
        const float cx = (float) x + (float) width * 0.5f;
        const juce::Rectangle<float> groove (cx - 2.0f, (float) y, 4.0f, (float) height);
        g.setColour (colours::background);
        g.fillRoundedRectangle (groove.expanded (1.0f), 2.5f);
        g.setColour (colours::outline);
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId).withAlpha (slider.isEnabled() ? 0.55f : 0.2f));
        g.fillRoundedRectangle (groove.withTop (sliderPos), 2.0f);
        const juce::Rectangle<float> cap (cx - 11.0f, sliderPos - 7.0f, 22.0f, 14.0f);
        g.setColour (colours::panelRaised.brighter (0.25f));
        g.fillRoundedRectangle (cap, 2.5f);
        g.setColour (colours::outline.brighter (0.3f));
        g.drawRoundedRectangle (cap.reduced (0.5f), 2.5f, 1.0f);
        g.setColour (colours::text);
        g.fillRect (cap.getX() + 3.0f, sliderPos - 0.5f, cap.getWidth() - 6.0f, 1.5f);
        return;
    }
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }
    const float cy = (float) y + (float) height * 0.5f;
    const juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) width, 4.0f);
    g.setColour (colours::outline);
    g.fillRoundedRectangle (track, 2.0f);
    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (track.withRight (sliderPos), 2.0f);
    g.setColour (colours::text);
    g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
}

int LookAndFeel::getSliderThumbRadius (juce::Slider& slider)
{
    if (slider.getSliderStyle() == juce::Slider::LinearVertical)
        return 8;
    return LookAndFeel_V4::getSliderThumbRadius (slider);
}

// 손잡이: 기준점에서 값까지 호를 그린다. 기준은 슬라이더 속성 arcFrom (0..1, 없으면 가운데: 밸런스)
void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto from = slider.getProperties()["arcFrom"];
    const float middle = start + (from.isVoid() ? 0.5f : (float) (double) from) * (end - start);
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, start, end, true);
    g.setColour (colours::outline);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (std::abs (angle - middle) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - 2.0f, radius - 2.0f, 0.0f, std::min (angle, middle), std::max (angle, middle), true);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float knob = radius - 6.0f;
    g.setColour (colours::panelRaised.brighter (0.15f));
    g.fillEllipse (centre.x - knob, centre.y - knob, knob * 2.0f, knob * 2.0f);
    g.setColour (colours::outline.brighter (0.2f));
    g.drawEllipse (centre.x - knob, centre.y - knob, knob * 2.0f, knob * 2.0f, 1.0f);
    const juce::Point<float> tip (centre.x + (knob - 2.0f) * std::sin (angle), centre.y - (knob - 2.0f) * std::cos (angle));
    g.setColour (colours::text);
    g.drawLine ({ centre, tip }, 2.0f);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool down)
{
    g.setColour (colours::panel);
    g.fillRect (x, y, width, height);
    if (thumbSize <= 0)
        return;
    auto thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, width - 4, thumbSize)
                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, height - 4);
    g.setColour (down ? colours::textFaint : (over ? colours::barLine.brighter (0.15f) : colours::barLine));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void LookAndFeel::drawStretchableLayoutResizerBar (juce::Graphics& g, int w, int h, bool, bool over, bool dragging)
{
    g.setColour (dragging ? colours::accent : (over ? colours::barLine : colours::outline));
    g.fillRect (0, 0, w, h);
}

} // namespace daw
