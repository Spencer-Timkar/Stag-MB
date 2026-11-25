#pragma once

#include <JuceHeader.h>

// Shared Stag Audio palette, matching Stag Limiter and stagaudio.com.
namespace StagColours
{
inline const juce::Colour violet { 0xff6337b5 };
inline const juce::Colour cream { 0xfff7f0e5 };
inline const juce::Colour ink { 0xff2c193e };
inline const juce::Colour lilac { 0xffeae2fa };
inline const juce::Colour apricot { 0xfff2ad83 };
inline const juce::Colour mutedInk { 0xff675971 };
inline const juce::Colour mutedViolet { 0xffd8cdea };
inline const juce::Colour border { 0xff82718f };
inline const juce::Colour deepViolet { 0xff4f2a96 };
}

// Components drawn on the violet workspace set this property so buttons and
// toggles use the inverted (cream-on-violet) treatment.
namespace StagProperties
{
inline const juce::Identifier onViolet { "stagOnViolet" };
inline const juce::Identifier segment { "stagSegment" };
inline const juce::Identifier chip { "stagChip" };
inline const juce::Identifier fieldLabel { "stagFieldLabel" };
}

class StagLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    StagLookAndFeel();

    juce::Font regular (float size) const;
    juce::Font semibold (float size) const;
    juce::Font numeric (float size) const;

    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;

    void drawLabel (juce::Graphics&, juce::Label&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool isMouseOverButton, bool isButtonDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool isMouseOverButton, bool isButtonDown) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

private:
    juce::Typeface::Ptr bodyTypeface;
    juce::Typeface::Ptr strongTypeface;
};
