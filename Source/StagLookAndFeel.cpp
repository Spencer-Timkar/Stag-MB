#include "StagLookAndFeel.h"

#include <BinaryData.h>

using namespace StagColours;

namespace
{
bool isOnViolet (const juce::Component& component)
{
    return static_cast<bool> (component.getProperties()[StagProperties::onViolet]);
}

bool isSliderTextBox (juce::Label& label)
{
    return dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr;
}

bool hasFocusRing (const juce::Component& component)
{
    return component.hasKeyboardFocus (true) && component.isShowing();
}
}

StagLookAndFeel::StagLookAndFeel()
{
    bodyTypeface = juce::Typeface::createSystemTypefaceFor (BinaryData::Inter400_ttf,
                                                            BinaryData::Inter400_ttfSize);
    strongTypeface = juce::Typeface::createSystemTypefaceFor (BinaryData::Inter600_ttf,
                                                              BinaryData::Inter600_ttfSize);
    setDefaultSansSerifTypeface (bodyTypeface);

    setColour (juce::Label::textColourId, ink);
    setColour (juce::Slider::textBoxTextColourId, ink);
    setColour (juce::Slider::textBoxBackgroundColourId, cream);
    setColour (juce::Slider::textBoxOutlineColourId, border);
    setColour (juce::Slider::textBoxHighlightColourId, lilac);
    setColour (juce::ComboBox::backgroundColourId, cream);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::outlineColourId, border);
    setColour (juce::ComboBox::arrowColourId, violet);
    setColour (juce::ComboBox::focusedOutlineColourId, violet);
    setColour (juce::PopupMenu::backgroundColourId, cream);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, violet);
    setColour (juce::PopupMenu::highlightedTextColourId, cream);
    setColour (juce::TextButton::buttonColourId, cream);
    setColour (juce::TextButton::buttonOnColourId, violet);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, cream);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::backgroundColourId, cream);
    setColour (juce::TextEditor::highlightColourId, lilac);
    setColour (juce::TextEditor::highlightedTextColourId, ink);
    setColour (juce::TextEditor::focusedOutlineColourId, violet);
    setColour (juce::TextEditor::outlineColourId, border);
    setColour (juce::CaretComponent::caretColourId, violet);
    setColour (juce::TooltipWindow::backgroundColourId, ink);
    setColour (juce::TooltipWindow::textColourId, cream);
    setColour (juce::TooltipWindow::outlineColourId, ink);
}

juce::Font StagLookAndFeel::regular (float size) const
{
    return juce::Font (juce::FontOptions (bodyTypeface).withHeight (size));
}

juce::Font StagLookAndFeel::semibold (float size) const
{
    return juce::Font (juce::FontOptions (strongTypeface).withHeight (size));
}

juce::Font StagLookAndFeel::numeric (float size) const
{
    return juce::Font (juce::FontOptions (strongTypeface)
                           .withHeight (size)
                           .withFeatureSetting (juce::FontFeatureSetting ("tnum", 1)));
}

juce::Font StagLookAndFeel::getLabelFont (juce::Label& label)
{
    if (isSliderTextBox (label))
        return numeric (15.0f);

    if (static_cast<bool> (label.getProperties()[StagProperties::fieldLabel]))
        return semibold (12.0f);

    if (dynamic_cast<juce::ComboBox*> (label.getParentComponent()) != nullptr)
        return regular (14.0f);

    return label.getFont();
}

juce::Font StagLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return regular (14.0f);
}

juce::Font StagLookAndFeel::getPopupMenuFont()
{
    return regular (14.0f);
}

juce::Font StagLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (static_cast<bool> (button.getProperties()[StagProperties::chip]))
        return semibold (juce::jmin (12.0f, static_cast<float> (buttonHeight) * 0.5f));

    return semibold (juce::jmin (14.0f, static_cast<float> (buttonHeight) * 0.52f));
}

juce::Label* StagLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox (slider);
    label->setFont (numeric (15.0f));
    label->setJustificationType (juce::Justification::centred);
    return label;
}

juce::Slider::SliderLayout StagLookAndFeel::getSliderLayout (juce::Slider& slider)
{
    if (! slider.isRotary())
        return LookAndFeel_V4::getSliderLayout (slider);

    // Keep the value box tucked directly under a square knob instead of
    // pinned to the bottom of a tall column.
    auto bounds = slider.getLocalBounds();
    const auto boxHeight = slider.getTextBoxHeight();
    const auto boxWidth = juce::jmin (slider.getTextBoxWidth(), bounds.getWidth());
    const auto knobSize = juce::jmax (40, juce::jmin (bounds.getWidth(), bounds.getHeight() - boxHeight - 6));
    const auto contentHeight = knobSize + 6 + boxHeight;
    auto content = bounds.withSizeKeepingCentre (bounds.getWidth(), contentHeight);

    juce::Slider::SliderLayout layout;
    layout.sliderBounds = content.removeFromTop (knobSize).withSizeKeepingCentre (knobSize, knobSize);
    content.removeFromTop (6);
    layout.textBoxBounds = content.withSizeKeepingCentre (boxWidth, boxHeight);
    return layout;
}

void StagLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (! isSliderTextBox (label))
    {
        LookAndFeel_V4::drawLabel (g, label);
        return;
    }

    const auto bounds = label.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (cream);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (label.isEnabled() && label.isMouseOver() ? violet : border);
    g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

    if (! label.isBeingEdited())
    {
        g.setFont (numeric (15.0f));
        g.setColour (label.isEnabled() ? ink : mutedInk);
        g.drawText (label.getText(), label.getLocalBounds().reduced (6, 0),
                    juce::Justification::centred, false);
    }
}

void StagLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto bounds = juce::Rectangle<float> (0.5f, 0.5f, static_cast<float> (width) - 1.0f,
                                                static_cast<float> (height) - 1.0f);
    g.setColour (isButtonDown ? lilac : cream);
    g.fillRoundedRectangle (bounds, 8.0f);

    const auto highlighted = box.isEnabled() && (box.isMouseOver (true) || hasFocusRing (box));
    g.setColour (highlighted ? violet : border);
    g.drawRoundedRectangle (bounds, 8.0f, highlighted && hasFocusRing (box) ? 2.0f : 1.0f);

    juce::Path arrow;
    const auto w = static_cast<float> (width);
    const auto h = static_cast<float> (height);
    arrow.startNewSubPath (w - 24.0f, h * 0.44f);
    arrow.lineTo (w - 19.0f, h * 0.59f);
    arrow.lineTo (w - 14.0f, h * 0.44f);
    g.setColour (box.isEnabled() ? violet : mutedInk);
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void StagLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 34, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void StagLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();
    const auto radius = juce::jmin (8.0f, bounds.getHeight() * 0.3f);

    if (isOnViolet (button))
    {
        const auto onColour = button.findColour (juce::TextButton::buttonOnColourId);
        auto fill = on ? onColour
                       : cream.withAlpha (isButtonDown ? 0.28f : isMouseOverButton ? 0.16f : 0.0f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (on ? onColour : cream.withAlpha (isMouseOverButton ? 0.9f : 0.5f));
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }
    else
    {
        g.setColour (on ? violet : isButtonDown ? lilac : cream);
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (on ? violet : isMouseOverButton ? violet : border);
        g.drawRoundedRectangle (bounds, radius, 1.0f);
    }

    if (hasFocusRing (button))
    {
        g.setColour (isOnViolet (button) ? apricot : violet);
        g.drawRoundedRectangle (bounds.reduced (1.5f), radius, 2.0f);
    }
}

void StagLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto on = button.getToggleState();
    auto colour = button.findColour (on ? juce::TextButton::textColourOnId
                                        : juce::TextButton::textColourOffId);
    if (! button.isEnabled())
        colour = colour.withMultipliedAlpha (0.5f);

    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (colour);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0),
                      juce::Justification::centred, 1, 0.85f);
}

void StagLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                        bool isMouseOverButton, bool isButtonDown)
{
    const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = button.getToggleState();

    g.setColour (on ? apricot : isButtonDown ? lilac : cream);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (isMouseOverButton || hasFocusRing (button) ? violet : on ? apricot.darker (0.25f) : border);
    g.drawRoundedRectangle (bounds, 8.0f, hasFocusRing (button) ? 2.0f : 1.0f);

    const auto dotY = bounds.getCentreY() - 3.5f;
    if (on)
    {
        g.setColour (ink);
        g.fillEllipse (12.0f, dotY, 7.0f, 7.0f);
    }
    else
    {
        g.setColour (mutedInk);
        g.drawEllipse (12.0f, dotY, 7.0f, 7.0f, 1.2f);
    }

    g.setFont (semibold (13.0f));
    g.setColour (button.isEnabled() ? ink : mutedInk);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().withTrimmedLeft (28).withTrimmedRight (8),
                      juce::Justification::centredLeft, 1, 0.85f);
}

void StagLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float position, float startAngle, float endAngle,
                                        juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                static_cast<float> (width), static_cast<float> (height))
                            .reduced (6.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centreX = bounds.getCentreX();
    const auto centreY = bounds.getCentreY();
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto arcRadius = radius - 3.0f;

    juce::Path track;
    juce::Path active;
    track.addCentredArc (centreX, centreY, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    active.addCentredArc (centreX, centreY, arcRadius, arcRadius, 0.0f, startAngle, angle, true);

    const juce::PathStrokeType stroke (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (lilac);
    g.strokePath (track, stroke);
    g.setColour (slider.isEnabled() ? violet : mutedInk);
    g.strokePath (active, stroke);

    const auto bodyRadius = juce::jmax (8.0f, arcRadius - 9.0f);
    g.setColour (cream);
    g.fillEllipse (centreX - bodyRadius, centreY - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f);
    g.setColour (slider.isMouseOverOrDragging() ? violet : border);
    g.drawEllipse (centreX - bodyRadius, centreY - bodyRadius, bodyRadius * 2.0f, bodyRadius * 2.0f, 1.0f);

    juce::Path pointer;
    const auto pointerLength = juce::jmin (15.0f, bodyRadius * 0.6f);
    pointer.addRoundedRectangle (-2.0f, -bodyRadius + 5.0f, 4.0f, pointerLength, 2.0f);
    g.setColour (slider.isEnabled() ? ink : mutedInk);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centreX, centreY));

    if (hasFocusRing (slider))
    {
        g.setColour (violet);
        g.drawEllipse (centreX - radius - 1.0f, centreY - radius - 1.0f,
                       radius * 2.0f + 2.0f, radius * 2.0f + 2.0f, 1.5f);
    }
}

void StagLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float> (static_cast<float> (width), static_cast<float> (height));
    g.setColour (ink);
    g.fillRoundedRectangle (bounds, 6.0f);
    g.setFont (regular (13.0f));
    g.setColour (cream);
    g.drawFittedText (text, juce::Rectangle<int> (width, height).reduced (10, 4),
                      juce::Justification::centredLeft, 3);
}

juce::Rectangle<int> StagLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (regular (13.0f), tipText, 0.0f, 0.0f);
    const auto width = juce::jmin (320, juce::roundToInt (glyphs.getBoundingBox (0, -1, true).getWidth()) + 22);
    const auto height = 30;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (width + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (height + 6) : screenPos.y + 6,
                                 width, height)
        .constrainedWithin (parentArea);
}
