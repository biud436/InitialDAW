// 앱의 색과 글꼴. 어두운 배경에 트랙마다 다른 색을 쓴다.
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

namespace daw
{

namespace colours
{
const juce::Colour background { 0xff15171c };
const juce::Colour panel { 0xff1c1f26 };
const juce::Colour panelRaised { 0xff232731 };
const juce::Colour laneA { 0xff191c22 };
const juce::Colour laneB { 0xff1d2027 };
const juce::Colour beatLine { 0xff242832 };
const juce::Colour barLine { 0xff323845 };
const juce::Colour outline { 0xff2b303b };
const juce::Colour text { 0xffdfe3ea };
const juce::Colour textDim { 0xff8b94a5 };
const juce::Colour textFaint { 0xff5c6474 };
const juce::Colour accent { 0xff4cb4ff };
const juce::Colour playhead { 0xffffc24b };
const juce::Colour selection { 0xff3b82f6 };
const juce::Colour mute { 0xffe0a03a };
const juce::Colour solo { 0xff47c97a };
const juce::Colour danger { 0xffef5b5b };
const juce::Colour marker { 0xffc9a2f2 };      // 마커 깃발
const juce::Colour clipGain { 0xffff9f43 };    // 클립 안의 게인 곡선
const juce::Colour whiteKey { 0xffd9dde4 };
const juce::Colour blackKey { 0xff2a2e37 };
} // namespace colours

juce::Font uiFont (float height, bool bold = false);
juce::Font monoFont (float height);

class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    int getSliderThumbRadius (juce::Slider&) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    void drawStretchableLayoutResizerBar (juce::Graphics&, int w, int h, bool isVerticalBar, bool isMouseOver, bool isMouseDragging) override;
};

} // namespace daw
