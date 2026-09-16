#include "PluginEditor.h"

namespace
{
const juce::Colour background { 0xff060807 };     // Stag Black
const juce::Colour deepPlum { 0xff0d0612 };
const juce::Colour panel { 0xff0d0612 };
const juce::Colour panelRaised { 0xff171419 };    // Carbon
const juce::Colour text { 0xffebe8d9 };           // Stag Ivory
const juce::Colour disabledText { 0xff66645f };   // Ash
const juce::Colour mutedText = text.withAlpha (0.65f);
const juce::Colour grid = text.withAlpha (0.14f);
const juce::Colour accent { 0xffad59f5 };         // Stag Violet
const juce::Colour accentHover { 0xffd2a4fa };    // Pale Violet
const juce::Colour accentPressed { 0xff7130b5 };  // Dark Violet
const juce::Colour accentSoft = accent.withAlpha (0.30f);
const juce::Colour destructive { 0xffd65c68 };    // Reserved for mute/error state

constexpr float minimumFrequency = 20.0f;
constexpr float maximumFrequency = 20000.0f;
}

SpectrumBandEditor::SpectrumBandEditor (StagMotionBandsAudioProcessor& owner)
    : processor (owner)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus (true);

    auto& state = processor.getParameters();

    for (int band = 0; band < StagMotionBandsAudioProcessor::maxBands; ++band)
    {
        auto& solo = soloButtons[static_cast<size_t> (band)];
        auto& mute = muteButtons[static_cast<size_t> (band)];
        auto& remove = removeButtons[static_cast<size_t> (band)];

        solo.setButtonText ("S");
        mute.setButtonText ("M");
        remove.setButtonText (juce::String::fromUTF8 ("\xc3\x97"));
        solo.setClickingTogglesState (true);
        mute.setClickingTogglesState (true);
        solo.setColour (juce::TextButton::buttonColourId, panelRaised);
        solo.setColour (juce::TextButton::buttonOnColourId, accentPressed);
        solo.setColour (juce::TextButton::textColourOffId, mutedText);
        solo.setColour (juce::TextButton::textColourOnId, background);
        mute.setColour (juce::TextButton::buttonColourId, panelRaised);
        mute.setColour (juce::TextButton::buttonOnColourId, destructive);
        mute.setColour (juce::TextButton::textColourOffId, mutedText);
        mute.setColour (juce::TextButton::textColourOnId, text);
        remove.setColour (juce::TextButton::buttonColourId, panelRaised);
        remove.setColour (juce::TextButton::buttonOnColourId, destructive);
        remove.setColour (juce::TextButton::textColourOffId, destructive);
        remove.setColour (juce::TextButton::textColourOnId, text);

        solo.setTooltip ("Solo band " + juce::String (band + 1));
        mute.setTooltip ("Mute band " + juce::String (band + 1));
        remove.setTooltip ("Remove band " + juce::String (band + 1));
        solo.onClick = [this, band] { selectBand (band); };
        mute.onClick = [this, band] { selectBand (band); };
        remove.onClick = [this, band] {
            processor.removeBand (band);
            selectBand (juce::jmin (band, processor.getBandCount() - 1));
            updateBandButtons();
            repaint();
        };

        addAndMakeVisible (solo);
        addAndMakeVisible (mute);
        addAndMakeVisible (remove);

        soloAttachments[static_cast<size_t> (band)] =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, StagMotionBandsAudioProcessor::soloParameterId (band), solo);

        muteAttachments[static_cast<size_t> (band)] =
            std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                state, StagMotionBandsAudioProcessor::muteParameterId (band), mute);
    }

    spectrumDb.fill (-100.0f);
    startTimerHz (30);
}

void SpectrumBandEditor::setBandSelectedCallback (std::function<void(int)> callback)
{
    bandSelectedCallback = std::move (callback);
    if (bandSelectedCallback)
        bandSelectedCallback (selectedBand);
}

void SpectrumBandEditor::selectBand (int bandIndex)
{
    const auto clamped = juce::jlimit (0, processor.getBandCount() - 1, bandIndex);
    if (selectedBand == clamped)
        return;

    selectedBand = clamped;
    if (bandSelectedCallback)
        bandSelectedCallback (selectedBand);
    repaint();
}

void SpectrumBandEditor::selectPreviousBand()
{
    selectBand (juce::jmax (0, selectedBand - 1));
}

void SpectrumBandEditor::selectNextBand()
{
    selectBand (juce::jmin (processor.getBandCount() - 1, selectedBand + 1));
}

juce::Rectangle<float> SpectrumBandEditor::plotBounds() const
{
    return getLocalBounds().toFloat().reduced (18.0f).withTrimmedBottom (20.0f);
}

void SpectrumBandEditor::paint (juce::Graphics& g)
{
    auto plot = plotBounds();

    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 16.0f);

    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();

    std::array<float, StagMotionBandsAudioProcessor::maxBands + 1> edges {};
    edges[0] = plot.getX();
    for (int index = 0; index < bandCount - 1; ++index)
        edges[static_cast<size_t> (index + 1)] = frequencyToX (crossovers[static_cast<size_t> (index)]);
    edges[static_cast<size_t> (bandCount)] = plot.getRight();

    for (int band = 0; band < bandCount; ++band)
    {
        const auto bandArea = juce::Rectangle<float>::leftTopRightBottom (
            edges[static_cast<size_t> (band)], plot.getY(),
            edges[static_cast<size_t> (band + 1)], plot.getBottom());

        if ((band % 2) == 1)
        {
            g.setColour (text.withAlpha (0.025f));
            g.fillRect (bandArea);
        }

        const auto isMuted = processor.getParameters().getRawParameterValue (
                                 StagMotionBandsAudioProcessor::muteParameterId (band))->load() >= 0.5f;
        const auto isSoloed = processor.getParameters().getRawParameterValue (
                                  StagMotionBandsAudioProcessor::soloParameterId (band))->load() >= 0.5f;

        if (isMuted)
        {
            g.setColour (destructive.withAlpha (0.14f));
            g.fillRect (bandArea);
        }
        else if (isSoloed)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.fillRect (bandArea);
        }
    }

    const std::array<float, 10> frequencyLines {
        20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
        1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
    };

    g.setFont (12.0f);
    for (const auto frequency : frequencyLines)
    {
        const auto x = frequencyToX (frequency);
        g.setColour (grid);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        g.setColour (mutedText);
        g.drawText (formatFrequency (frequency),
                    juce::Rectangle<float> { x - 24.0f, plot.getBottom() + 3.0f, 48.0f, 16.0f },
                    juce::Justification::centred, false);
    }

    const std::array<float, 5> dbLines { -12.0f, -24.0f, -48.0f, -72.0f, -96.0f };
    for (const auto db : dbLines)
    {
        const auto normalized = juce::jmap (db, -100.0f, 0.0f, 1.0f, 0.0f);
        const auto y = plot.getY() + normalized * plot.getHeight();
        g.setColour (grid);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    juce::Path spectrumPath;
    bool started = false;
    const auto currentSampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto frequency = static_cast<float> (bin) * static_cast<float> (currentSampleRate)
                             / static_cast<float> (fftSize);
        if (frequency < minimumFrequency || frequency > maximumFrequency)
            continue;

        const auto x = frequencyToX (frequency);
        const auto normalized = juce::jmap (spectrumDb[static_cast<size_t> (bin)], -100.0f, 0.0f, 1.0f, 0.0f);
        const auto y = plot.getY() + juce::jlimit (0.0f, 1.0f, normalized) * plot.getHeight();

        if (! started)
        {
            spectrumPath.startNewSubPath (x, y);
            started = true;
        }
        else
        {
            spectrumPath.lineTo (x, y);
        }
    }

    if (! spectrumPath.isEmpty())
    {
        auto fillPath = spectrumPath;
        fillPath.lineTo (plot.getRight(), plot.getBottom());
        fillPath.lineTo (plot.getX(), plot.getBottom());
        fillPath.closeSubPath();

        g.setGradientFill (juce::ColourGradient {
            accent.withAlpha (0.28f), plot.getCentreX(), plot.getY(),
            accent.withAlpha (0.01f), plot.getCentreX(), plot.getBottom(), false
        });
        g.fillPath (fillPath);
        g.setColour (accent.withAlpha (0.92f));
        g.strokePath (spectrumPath, juce::PathStrokeType { 1.8f });
    }

    for (int index = 0; index < bandCount - 1; ++index)
    {
        const auto frequency = crossovers[static_cast<size_t> (index)];
        const auto x = frequencyToX (frequency);
        const auto highlighted = index == draggingSeparator || index == hoveredSeparator;

        g.setColour (highlighted ? accentHover : text.withAlpha (0.64f));
        g.drawLine (x, plot.getY(), x, plot.getBottom(), highlighted ? 2.5f : 1.4f);
        g.fillEllipse (x - 5.0f, plot.getY() + 8.0f, 10.0f, 10.0f);

        const auto label = formatFrequency (frequency);
        const auto labelArea = juce::Rectangle<float> { x - 34.0f, plot.getY() + 23.0f, 68.0f, 22.0f };
        g.setColour (background.withAlpha (0.84f));
        g.fillRoundedRectangle (labelArea, 5.0f);
        g.setColour (text);
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    for (int band = 0; band < bandCount; ++band)
    {
        const auto centreX = (edges[static_cast<size_t> (band)] + edges[static_cast<size_t> (band + 1)]) * 0.5f;
        g.setColour (band == selectedBand ? accent : mutedText);
        g.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
        g.drawText ("BAND " + juce::String (band + 1),
                    juce::Rectangle<float> { centreX - 38.0f, plot.getY() + 8.0f, 76.0f, 16.0f },
                    juce::Justification::centred, false);
    }

    if (mouseInside && draggingSeparator < 0)
    {
        const auto x = frequencyToX (hoveredFrequency);
        g.setColour (accentSoft);
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
    }
}

void SpectrumBandEditor::resized()
{
    updateBandButtons();
}

void SpectrumBandEditor::updateBandButtons()
{
    const auto plot = plotBounds();
    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();

    if (selectedBand >= bandCount)
        selectBand (bandCount - 1);

    std::array<float, StagMotionBandsAudioProcessor::maxBands + 1> edges {};
    edges[0] = plot.getX();
    for (int index = 0; index < bandCount - 1; ++index)
        edges[static_cast<size_t> (index + 1)] = frequencyToX (crossovers[static_cast<size_t> (index)]);
    edges[static_cast<size_t> (bandCount)] = plot.getRight();

    for (int band = 0; band < StagMotionBandsAudioProcessor::maxBands; ++band)
    {
        const auto visible = band < bandCount;
        soloButtons[static_cast<size_t> (band)].setVisible (visible);
        muteButtons[static_cast<size_t> (band)].setVisible (visible);
        removeButtons[static_cast<size_t> (band)].setVisible (visible && bandCount > 1);

        if (! visible)
            continue;

        const auto centreX = (edges[static_cast<size_t> (band)] + edges[static_cast<size_t> (band + 1)]) * 0.5f;
        const auto y = juce::roundToInt (plot.getBottom() - 34.0f);
        soloButtons[static_cast<size_t> (band)].setBounds (juce::roundToInt (centreX) - 45, y, 26, 24);
        muteButtons[static_cast<size_t> (band)].setBounds (juce::roundToInt (centreX) - 13, y, 26, 24);
        removeButtons[static_cast<size_t> (band)].setBounds (juce::roundToInt (centreX) + 19, y, 26, 24);
    }
}

void SpectrumBandEditor::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto plot = plotBounds();
    if (! plot.contains (event.position))
        return;

    draggingSeparator = separatorAtX (event.position.x, 10.0f);
    if (draggingSeparator >= 0)
    {
        if (auto* parameter = processor.getParameters().getParameter (
                StagMotionBandsAudioProcessor::crossoverParameterId (draggingSeparator)))
            parameter->beginChangeGesture();

        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        return;
    }

    selectBand (bandAtX (event.position.x));
}

void SpectrumBandEditor::mouseDrag (const juce::MouseEvent& event)
{
    if (draggingSeparator < 0)
        return;

    const auto bandCount = processor.getBandCount();
    auto crossovers = processor.getSortedCrossovers();
    auto frequency = xToFrequency (event.position.x);

    const auto lower = draggingSeparator == 0
                           ? minimumFrequency
                           : crossovers[static_cast<size_t> (draggingSeparator - 1)] * 1.08f;
    const auto upper = draggingSeparator == bandCount - 2
                           ? maximumFrequency
                           : crossovers[static_cast<size_t> (draggingSeparator + 1)] / 1.08f;

    frequency = juce::jlimit (lower, upper, frequency);
    processor.setCrossover (draggingSeparator, frequency);
    updateBandButtons();
    repaint();
}

void SpectrumBandEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingSeparator >= 0)
        if (auto* parameter = processor.getParameters().getParameter (
                StagMotionBandsAudioProcessor::crossoverParameterId (draggingSeparator)))
            parameter->endChangeGesture();

    draggingSeparator = -1;
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void SpectrumBandEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (! plotBounds().contains (event.position))
        return;

    const auto separator = separatorAtX (event.position.x, 12.0f);
    if (separator >= 0)
    {
        processor.removeCrossover (separator);
        selectBand (juce::jmin (selectedBand, processor.getBandCount() - 1));
        updateBandButtons();
        repaint();
        return;
    }

    processor.addCrossover (xToFrequency (event.position.x));
    selectBand (bandAtX (event.position.x));
    updateBandButtons();
    repaint();
}

void SpectrumBandEditor::mouseMove (const juce::MouseEvent& event)
{
    mouseInside = plotBounds().contains (event.position);
    hoveredFrequency = xToFrequency (event.position.x);
    hoveredSeparator = separatorAtX (event.position.x, 8.0f);
    setMouseCursor (hoveredSeparator >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                                          : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void SpectrumBandEditor::mouseExit (const juce::MouseEvent&)
{
    mouseInside = false;
    hoveredSeparator = -1;
    repaint();
}

void SpectrumBandEditor::timerCallback()
{
    const auto available = processor.getAnalyzerSamplesAvailable();
    auto remaining = available;

    while (remaining > 0)
    {
        const auto requested = juce::jmin (remaining, static_cast<int> (incomingSamples.size()));
        const auto received = processor.popAnalyzerSamples (incomingSamples.data(), requested);
        if (received <= 0)
            break;

        if (received >= fftSize)
        {
            std::copy_n (incomingSamples.data() + received - fftSize, fftSize, sampleHistory.data());
        }
        else
        {
            std::move (sampleHistory.begin() + received, sampleHistory.end(), sampleHistory.begin());
            std::copy_n (incomingSamples.begin(), received, sampleHistory.end() - received);
        }

        remaining -= received;
    }

    updateSpectrum();
    updateBandButtons();
    repaint();
}

void SpectrumBandEditor::updateSpectrum()
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (sampleHistory.begin(), sampleHistory.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const auto magnitude = fftData[static_cast<size_t> (bin)] / static_cast<float> (fftSize);
        const auto value = juce::Decibels::gainToDecibels (magnitude, -100.0f);
        auto& smoothed = spectrumDb[static_cast<size_t> (bin)];
        smoothed = value > smoothed ? value : juce::jmax (-100.0f, smoothed - 1.8f);
    }
}

float SpectrumBandEditor::xToFrequency (float x) const
{
    const auto plot = plotBounds();
    const auto proportion = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
    return minimumFrequency * std::pow (maximumFrequency / minimumFrequency, proportion);
}

float SpectrumBandEditor::frequencyToX (float frequency) const
{
    const auto plot = plotBounds();
    const auto proportion = std::log (juce::jlimit (minimumFrequency, maximumFrequency, frequency) / minimumFrequency)
                          / std::log (maximumFrequency / minimumFrequency);
    return plot.getX() + static_cast<float> (proportion) * plot.getWidth();
}

int SpectrumBandEditor::separatorAtX (float x, float tolerance) const
{
    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();

    for (int index = 0; index < bandCount - 1; ++index)
        if (std::abs (frequencyToX (crossovers[static_cast<size_t> (index)]) - x) <= tolerance)
            return index;

    return -1;
}

int SpectrumBandEditor::bandAtX (float x) const
{
    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();

    for (int index = 0; index < bandCount - 1; ++index)
        if (x < frequencyToX (crossovers[static_cast<size_t> (index)]))
            return index;

    return bandCount - 1;
}

juce::String SpectrumBandEditor::formatFrequency (float frequency) const
{
    if (frequency >= 1000.0f)
        return juce::String (frequency / 1000.0f, frequency >= 10000.0f ? 0 : 1) + "k";

    return juce::String (juce::roundToInt (frequency));
}

EffectMenuPanel::EffectMenuPanel (StagMotionBandsAudioProcessor& owner)
    : processor (owner)
{
    title.setText ("EFFECT", juce::dontSendNotification);
    title.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    title.setColour (juce::Label::textColourId, mutedText);
    title.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (title);

    const std::array<juce::String, 9> names {
        "OFF", "CHORUS", "FLANGER", "PHASER", "TREMOLO",
        "AUTO PAN", "RING MOD", "PHASE WARP", "FREQ SHIFT"
    };
    const std::array<juce::String, 9> tooltips {
        "Disable modulation for the selected band",
        "Add animated width and pitch movement",
        "Create short comb-filter sweeps",
        "Create moving all-pass phase sweeps",
        "Create rhythmic level modulation",
        "Move the selected band across the stereo field",
        "Multiply the selected band with an audio-rate carrier",
        "Create asymmetric nonlinear phase movement",
        "Shift every frequency by a fixed amount"
    };

    for (int index = 0; index < static_cast<int> (effectButtons.size()); ++index)
    {
        auto& button = effectButtons[static_cast<size_t> (index)];
        button.setButtonText (names[static_cast<size_t> (index)]);
        button.setTooltip (tooltips[static_cast<size_t> (index)]);
        button.setColour (juce::TextButton::buttonColourId, panel);
        button.setColour (juce::TextButton::buttonOnColourId, accentPressed);
        button.setColour (juce::TextButton::textColourOffId, mutedText);
        button.setColour (juce::TextButton::textColourOnId, text);
        button.onClick = [this, index] { chooseEffect (index); };
        addAndMakeVisible (button);
    }

    updateButtons();
    startTimerHz (20);
}

void EffectMenuPanel::paint (juce::Graphics& g)
{
    g.setColour (panelRaised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 16.0f);
    g.setColour (grid);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 16.0f, 1.0f);
}

void EffectMenuPanel::resized()
{
    auto bounds = getLocalBounds().reduced (14);
    title.setBounds (bounds.removeFromTop (24));
    bounds.removeFromTop (8);

    constexpr int buttonGap = 4;
    const auto buttonHeight = juce::jmax (
        24, (bounds.getHeight() - buttonGap * (static_cast<int> (effectButtons.size()) - 1))
                / static_cast<int> (effectButtons.size()));
    for (auto& button : effectButtons)
    {
        button.setBounds (bounds.removeFromTop (buttonHeight));
        bounds.removeFromTop (buttonGap);
    }
}

void EffectMenuPanel::selectBand (int bandIndex)
{
    selectedBand = juce::jlimit (0, processor.getBandCount() - 1, bandIndex);
    updateButtons();
}

void EffectMenuPanel::chooseEffect (int effectIndex)
{
    if (auto* parameter = processor.getParameters().getParameter (
            StagMotionBandsAudioProcessor::effectTypeParameterId (selectedBand)))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (effectIndex)));
        parameter->endChangeGesture();
    }

    updateButtons();
}

void EffectMenuPanel::timerCallback()
{
    updateButtons();
}

void EffectMenuPanel::updateButtons()
{
    const auto effect = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::effectTypeParameterId (selectedBand))->load()));

    for (int index = 0; index < static_cast<int> (effectButtons.size()); ++index)
        effectButtons[static_cast<size_t> (index)].setToggleState (
            index == effect, juce::dontSendNotification);
}

BandControlPanel::BandControlPanel (StagMotionBandsAudioProcessor& owner)
    : processor (owner)
{
    bandTitle.setText ("BAND 1", juce::dontSendNotification);
    bandTitle.setFont (juce::FontOptions { 15.0f, juce::Font::bold });
    bandTitle.setColour (juce::Label::textColourId, accent);

    waveformLabel.setText ("WAVE", juce::dontSendNotification);
    waveformLabel.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
    waveformLabel.setColour (juce::Label::textColourId, mutedText);

    divisionLabel.setText ("DIVISION", juce::dontSendNotification);
    divisionLabel.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
    divisionLabel.setColour (juce::Label::textColourId, mutedText);

    sourceLabel.setText ("SOURCE", juce::dontSendNotification);
    sourceLabel.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
    sourceLabel.setColour (juce::Label::textColourId, mutedText);

    triggerLabel.setText ("MIDI", juce::dontSendNotification);
    triggerLabel.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
    triggerLabel.setColour (juce::Label::textColourId, mutedText);

    waveformSelector.addItemList ({ "SINE", "TRIANGLE", "SAW", "SQUARE" }, 1);
    divisionSelector.addItemList ({ "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1" }, 1);
    sourceSelector.addItemList ({ "LFO", "ENVELOPE" }, 1);

    for (auto* selector : { &waveformSelector, &divisionSelector, &sourceSelector })
    {
        selector->setColour (juce::ComboBox::backgroundColourId, panel);
        selector->setColour (juce::ComboBox::textColourId, text);
        selector->setColour (juce::ComboBox::outlineColourId, grid);
        selector->setColour (juce::ComboBox::arrowColourId, accent);
    }

    waveformSelector.setTooltip ("LFO waveform for the selected band");
    divisionSelector.setTooltip ("Musical cycle length when tempo sync is enabled");
    sourceSelector.setTooltip ("Choose the modulation signal for the selected band");

    syncButton.setButtonText ("PROJECT SYNC");
    syncButton.setClickingTogglesState (true);
    syncButton.setColour (juce::ToggleButton::textColourId, text);
    syncButton.setColour (juce::ToggleButton::tickColourId, accent);
    syncButton.setColour (juce::ToggleButton::tickDisabledColourId, disabledText);
    syncButton.setTooltip ("Replace free-rate Hz with divisions locked to the project tempo");

    retriggerButton.setButtonText ("RETRIGGER");
    retriggerButton.setClickingTogglesState (true);
    retriggerButton.setColour (juce::ToggleButton::textColourId, text);
    retriggerButton.setColour (juce::ToggleButton::tickColourId, accent);
    retriggerButton.setColour (juce::ToggleButton::tickDisabledColourId, disabledText);
    retriggerButton.setTooltip ("Reset this band's LFO phase when a MIDI note starts");

    const std::array<juce::String, 8> labels {
        "RATE", "DEPTH", "FEEDBACK", "MIX", "STEREO", "ATTACK", "RELEASE", "FREQUENCY"
    };
    for (int index = 0; index < 8; ++index)
        configureSlider (parameterSliders[static_cast<size_t> (index)],
                         parameterLabels[static_cast<size_t> (index)],
                         labels[static_cast<size_t> (index)]);

    parameterSliders[0].textFromValueFunction = [] (double value) {
        return juce::String (value, 2) + " Hz";
    };

    for (int index = 1; index < 4; ++index)
        parameterSliders[static_cast<size_t> (index)].textFromValueFunction = [] (double value) {
            return juce::String (juce::roundToInt (value * 100.0)) + "%";
        };

    parameterSliders[4].textFromValueFunction = [] (double value) {
        return juce::String (juce::roundToInt (value * 180.0)) + " deg";
    };

    for (int index = 5; index < 7; ++index)
        parameterSliders[static_cast<size_t> (index)].textFromValueFunction = [] (double value) {
            return juce::String (value, value < 10.0 ? 1 : 0) + " ms";
        };

    parameterSliders[7].textFromValueFunction = [] (double value) {
        return juce::String (value, value < 100.0 ? 1 : 0) + " Hz";
    };

    addAndMakeVisible (bandTitle);
    addAndMakeVisible (waveformLabel);
    addAndMakeVisible (divisionLabel);
    addAndMakeVisible (sourceLabel);
    addAndMakeVisible (triggerLabel);
    addAndMakeVisible (waveformSelector);
    addAndMakeVisible (divisionSelector);
    addAndMakeVisible (sourceSelector);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (retriggerButton);

    rebuildAttachments();
    updateControlVisibility();
    startTimerHz (30);
}

void BandControlPanel::configureSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 20);
    slider.setNumDecimalPlacesToDisplay (2);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, grid);
    slider.setColour (juce::Slider::thumbColourId, text);
    slider.setColour (juce::Slider::textBoxTextColourId, text);
    slider.setColour (juce::Slider::textBoxBackgroundColourId, panel);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    label.setText (labelText, juce::dontSendNotification);
    label.setFont (juce::FontOptions { 10.0f, juce::Font::bold });
    label.setColour (juce::Label::textColourId, mutedText);
    label.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void BandControlPanel::selectBand (int bandIndex)
{
    const auto clamped = juce::jlimit (0, StagMotionBandsAudioProcessor::maxBands - 1, bandIndex);
    if (selectedBand == clamped && sourceAttachment != nullptr)
        return;

    selectedBand = clamped;
    bandTitle.setText ("BAND " + juce::String (selectedBand + 1), juce::dontSendNotification);
    rebuildAttachments();
    updateControlVisibility();
    repaint();
}

void BandControlPanel::rebuildAttachments()
{
    waveformAttachment.reset();
    divisionAttachment.reset();
    sourceAttachment.reset();
    syncAttachment.reset();
    retriggerAttachment.reset();
    for (auto& attachment : sliderAttachments)
        attachment.reset();

    auto& state = processor.getParameters();
    waveformAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, StagMotionBandsAudioProcessor::waveformParameterId (selectedBand), waveformSelector);

    divisionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, StagMotionBandsAudioProcessor::divisionParameterId (selectedBand), divisionSelector);

    sourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        state, StagMotionBandsAudioProcessor::modulationSourceParameterId (selectedBand), sourceSelector);

    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, StagMotionBandsAudioProcessor::syncParameterId (selectedBand), syncButton);

    retriggerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        state, StagMotionBandsAudioProcessor::retriggerParameterId (selectedBand), retriggerButton);

    const std::array<juce::String, 8> parameterIds {
        StagMotionBandsAudioProcessor::rateParameterId (selectedBand),
        StagMotionBandsAudioProcessor::depthParameterId (selectedBand),
        StagMotionBandsAudioProcessor::feedbackParameterId (selectedBand),
        StagMotionBandsAudioProcessor::mixParameterId (selectedBand),
        StagMotionBandsAudioProcessor::stereoSpreadParameterId (selectedBand),
        StagMotionBandsAudioProcessor::attackParameterId (selectedBand),
        StagMotionBandsAudioProcessor::releaseParameterId (selectedBand),
        StagMotionBandsAudioProcessor::carrierFrequencyParameterId (selectedBand)
    };

    for (int index = 0; index < 8; ++index)
        sliderAttachments[static_cast<size_t> (index)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                state, parameterIds[static_cast<size_t> (index)],
                parameterSliders[static_cast<size_t> (index)]);
}

void BandControlPanel::paint (juce::Graphics& g)
{
    g.setColour (panelRaised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
    g.setColour (grid);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 14.0f, 1.0f);

    const auto dividerX = 340.0f;
    g.drawVerticalLine (juce::roundToInt (dividerX), 20.0f, static_cast<float> (getHeight() - 20));

    const juce::Rectangle<float> meterArea { 244.0f, 25.0f, 68.0f, 5.0f };
    g.setColour (grid);
    g.fillRoundedRectangle (meterArea, 2.5f);
    g.setColour (accent.withAlpha (0.9f));
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * displayedActivity), 2.5f);

    if (displayedEffect == BandModulationEngine::off)
    {
        g.setColour (mutedText);
        g.setFont (juce::FontOptions { 12.0f, juce::Font::bold });
        g.drawText ("SELECT AN EFFECT TO SHOW ITS ESSENTIAL CONTROLS",
                    getLocalBounds().toFloat().withTrimmedLeft (370.0f),
                    juce::Justification::centred, false);
    }
}

void BandControlPanel::updateControlVisibility()
{
    const auto effect = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::effectTypeParameterId (selectedBand))->load()));
    const auto source = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::modulationSourceParameterId (selectedBand))->load()));
    const auto tempoSync = processor.getParameters().getRawParameterValue (
                               StagMotionBandsAudioProcessor::syncParameterId (selectedBand))->load()
                           >= 0.5f;
    const auto effectEnabled = effect != BandModulationEngine::off;
    const auto usesCarrier = effect == BandModulationEngine::ringMod
                          || effect == BandModulationEngine::frequencyShifter;
    const auto usesLfo = source == BandModulationEngine::lfo && ! usesCarrier;
    const auto usesEnvelope = source == BandModulationEngine::envelope && ! usesCarrier;
    const auto isFrequencyShifter = effect == BandModulationEngine::frequencyShifter;
    const auto showsWaveform = effectEnabled && ! isFrequencyShifter && (usesCarrier || usesLfo);
    const auto usesOscillator = effectEnabled && (usesCarrier || usesLfo);

    sourceLabel.setVisible (effectEnabled && ! usesCarrier);
    sourceSelector.setVisible (effectEnabled && ! usesCarrier);
    waveformLabel.setVisible (showsWaveform);
    waveformSelector.setVisible (showsWaveform);
    triggerLabel.setVisible (usesOscillator);
    retriggerButton.setVisible (usesOscillator);
    divisionLabel.setVisible (usesOscillator && tempoSync);
    divisionSelector.setVisible (usesOscillator && tempoSync);
    syncButton.setVisible (usesOscillator);

    std::array<bool, 8> visibleSliders {};
    if (effectEnabled)
    {
        visibleSliders[3] = true; // Mix
        visibleSliders[4] = effect == BandModulationEngine::chorus
                         || effect == BandModulationEngine::tremolo
                         || effect == BandModulationEngine::autoPan
                         || effect == BandModulationEngine::ringMod
                         || effect == BandModulationEngine::phaseWarp
                         || effect == BandModulationEngine::frequencyShifter;
        visibleSliders[2] = effect == BandModulationEngine::flanger
                         || effect == BandModulationEngine::phaser;
        visibleSliders[1] = effect != BandModulationEngine::frequencyShifter;

        if (usesLfo)
            visibleSliders[0] = ! tempoSync; // Free-rate Hz is replaced by Division when synced.
        else if (usesEnvelope)
        {
            visibleSliders[5] = true; // Attack
            visibleSliders[6] = true; // Release
        }

        if (usesCarrier)
            visibleSliders[7] = ! tempoSync;
    }

    parameterLabels[1].setText (
        effect == BandModulationEngine::phaseWarp ? "WARP"
          : effect == BandModulationEngine::autoPan ? "PAN"
                                                     : "DEPTH",
        juce::dontSendNotification);
    parameterLabels[7].setText (
        effect == BandModulationEngine::frequencyShifter ? "SHIFT" : "CARRIER",
        juce::dontSendNotification);

    for (int index = 0; index < static_cast<int> (parameterSliders.size()); ++index)
    {
        parameterSliders[static_cast<size_t> (index)].setVisible (
            visibleSliders[static_cast<size_t> (index)]);
        parameterLabels[static_cast<size_t> (index)].setVisible (
            visibleSliders[static_cast<size_t> (index)]);
    }

    displayedEffect = effect;
    displayedSource = source;
    displayedSync = tempoSync;
    resized();
    repaint();
}

void BandControlPanel::timerCallback()
{
    const auto target = processor.getModulationActivity (selectedBand);
    displayedActivity += (target - displayedActivity) * 0.28f;
    repaint (240, 20, 78, 16);

    const auto effect = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::effectTypeParameterId (selectedBand))->load()));
    const auto source = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::modulationSourceParameterId (selectedBand))->load()));
    const auto tempoSync = processor.getParameters().getRawParameterValue (
                               StagMotionBandsAudioProcessor::syncParameterId (selectedBand))->load()
                           >= 0.5f;
    if (effect != displayedEffect || source != displayedSource || tempoSync != displayedSync)
        updateControlVisibility();
}

void BandControlPanel::resized()
{
    auto bounds = getLocalBounds().reduced (20, 14);
    auto identity = bounds.removeFromLeft (300);
    bandTitle.setBounds (identity.removeFromTop (26));
    identity.removeFromTop (4);

    auto labelsRow = identity.removeFromTop (16);
    sourceLabel.setBounds (labelsRow.removeFromLeft (134));
    labelsRow.removeFromLeft (10);
    waveformLabel.setBounds (labelsRow);

    auto selectorsRow = identity.removeFromTop (32);
    sourceSelector.setBounds (selectorsRow.removeFromLeft (134));
    selectorsRow.removeFromLeft (10);
    waveformSelector.setBounds (selectorsRow);

    identity.removeFromTop (5);
    auto sourceLabels = identity.removeFromTop (16);
    triggerLabel.setBounds (sourceLabels.removeFromLeft (134));
    sourceLabels.removeFromLeft (10);
    divisionLabel.setBounds (sourceLabels);

    auto sourceRow = identity.removeFromTop (32);
    retriggerButton.setBounds (sourceRow.removeFromLeft (134));
    sourceRow.removeFromLeft (10);
    divisionSelector.setBounds (sourceRow);

    identity.removeFromTop (5);
    auto syncRow = identity.removeFromTop (32);
    syncButton.setBounds (syncRow.removeFromLeft (134));

    bounds.removeFromLeft (42);
    std::vector<int> visibleSliderIndices;
    for (int index = 0; index < static_cast<int> (parameterSliders.size()); ++index)
        if (parameterSliders[static_cast<size_t> (index)].isVisible())
            visibleSliderIndices.push_back (index);

    if (visibleSliderIndices.empty())
        return;

    const auto sliderWidth = juce::jmax (
        82, bounds.getWidth() / static_cast<int> (visibleSliderIndices.size()));
    for (int position = 0; position < static_cast<int> (visibleSliderIndices.size()); ++position)
    {
        const auto index = visibleSliderIndices[static_cast<size_t> (position)];
        auto column = bounds.removeFromLeft (
            position == static_cast<int> (visibleSliderIndices.size()) - 1
                ? bounds.getWidth()
                : sliderWidth);
        parameterLabels[static_cast<size_t> (index)].setBounds (column.removeFromTop (18));
        parameterSliders[static_cast<size_t> (index)].setBounds (column.reduced (6, 0));
    }
}

StagMotionBandsAudioProcessorEditor::StagMotionBandsAudioProcessorEditor (
    StagMotionBandsAudioProcessor& owner)
    : AudioProcessorEditor (&owner), ownerProcessor (owner), spectrumEditor (owner), effectMenu (owner),
      bandControls (owner)
{
    stagLookAndFeel.setColour (juce::PopupMenu::backgroundColourId, panelRaised);
    stagLookAndFeel.setColour (juce::PopupMenu::textColourId, text);
    stagLookAndFeel.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
    stagLookAndFeel.setColour (juce::PopupMenu::highlightedTextColourId, background);
    stagLookAndFeel.setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    stagLookAndFeel.setColour (juce::TooltipWindow::textColourId, text);
    stagLookAndFeel.setColour (juce::TooltipWindow::outlineColourId, grid);
    stagLookAndFeel.setColour (juce::ComboBox::focusedOutlineColourId, accentHover);
    setLookAndFeel (&stagLookAndFeel);

    setOpaque (true);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (760, 600, 1600, 1050);
    setSize (1100, 900);

    productName.setText ("MOTION / BANDS", juce::dontSendNotification);
    productName.setFont (juce::FontOptions { 24.0f, juce::Font::bold });
    productName.setColour (juce::Label::textColourId, text);

    productDescriptor.setText ("MULTIBAND MODULATION WORKSPACE", juce::dontSendNotification);
    productDescriptor.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    productDescriptor.setColour (juce::Label::textColourId, mutedText);

    interactionHint.setText (
        "DOUBLE-CLICK TO SPLIT  /  DRAG DIVIDERS  /  ARROW KEYS SWITCH BANDS",
        juce::dontSendNotification);
    interactionHint.setFont (juce::FontOptions { 11.0f });
    interactionHint.setColour (juce::Label::textColourId, mutedText);
    interactionHint.setJustificationType (juce::Justification::centredRight);

    buildBadge.setText ("ALPHA 0.7", juce::dontSendNotification);
    buildBadge.setFont (juce::FontOptions { 11.0f, juce::Font::bold });
    buildBadge.setColour (juce::Label::textColourId, background);
    buildBadge.setColour (juce::Label::backgroundColourId, accent);
    buildBadge.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (productName);
    addAndMakeVisible (productDescriptor);
    addAndMakeVisible (interactionHint);
    addAndMakeVisible (buildBadge);
    addAndMakeVisible (spectrumEditor);
    addAndMakeVisible (effectMenu);
    addAndMakeVisible (bandControls);

    spectrumEditor.setBandSelectedCallback ([this] (int band) {
        effectMenu.selectBand (band);
        bandControls.selectBand (band);
    });
}

StagMotionBandsAudioProcessorEditor::~StagMotionBandsAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void StagMotionBandsAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto fullBounds = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient {
        background, fullBounds.getTopLeft(), deepPlum, fullBounds.getBottomRight(), false
    });
    g.fillRect (fullBounds);

    auto bounds = fullBounds;
    g.setColour (grid);
    g.drawHorizontalLine (86, bounds.getX() + 28.0f, bounds.getRight() - 28.0f);

    const auto footer = bounds.removeFromBottom (64.0f).reduced (28.0f, 0.0f);
    g.setColour (mutedText);
    g.setFont (11.0f);
    g.drawText ("STAG AUDIO  /  EXPANDED EFFECTS MILESTONE",
                footer, juce::Justification::centredLeft, false);
    g.drawText ("VST3  /  AU  /  STANDALONE",
                footer, juce::Justification::centredRight, false);
}

void StagMotionBandsAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (28);
    auto header = bounds.removeFromTop (58);

    auto brand = header.removeFromLeft (280);
    productName.setBounds (brand.removeFromTop (34));
    productDescriptor.setBounds (brand);

    buildBadge.setBounds (header.removeFromRight (86).reduced (0, 15));
    interactionHint.setBounds (header.reduced (12, 0));

    bounds.removeFromTop (28);
    bounds.removeFromBottom (48);
    auto controls = bounds.removeFromBottom (232);
    auto analysisArea = bounds.withTrimmedBottom (18);
    effectMenu.setBounds (analysisArea.removeFromRight (154));
    analysisArea.removeFromRight (12);
    spectrumEditor.setBounds (analysisArea);
    bandControls.setBounds (controls);
}

bool StagMotionBandsAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::leftKey)
    {
        spectrumEditor.selectPreviousBand();
        return true;
    }

    if (key == juce::KeyPress::rightKey)
    {
        spectrumEditor.selectNextBand();
        return true;
    }

    return AudioProcessorEditor::keyPressed (key);
}
