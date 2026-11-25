#include "PluginEditor.h"

using namespace StagColours;

namespace
{
constexpr float minimumFrequency = 20.0f;
constexpr float maximumFrequency = 20000.0f;
constexpr float floorDb = -100.0f;
constexpr std::array<float, 10> frequencyLines {
    20.0f, 50.0f, 100.0f, 200.0f, 500.0f,
    1000.0f, 2000.0f, 5000.0f, 10000.0f, 20000.0f
};

StagLookAndFeel& themeOf (juce::Component& component)
{
    if (auto* theme = dynamic_cast<StagLookAndFeel*> (&component.getLookAndFeel()))
        return *theme;

    static StagLookAndFeel fallback;
    return fallback;
}

int currentEffect (StagMotionBandsAudioProcessor& processor, int band)
{
    return static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::effectTypeParameterId (band))->load()));
}

bool parameterIsOn (StagMotionBandsAudioProcessor& processor, const juce::String& parameterId)
{
    return processor.getParameters().getRawParameterValue (parameterId)->load() >= 0.5f;
}

juce::String formatHz (float frequency)
{
    if (frequency >= 1000.0f)
        return juce::String (frequency / 1000.0f, frequency >= 10000.0f ? 1 : 2)
                   .trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";

    return juce::String (juce::roundToInt (frequency)) + " Hz";
}
}

//==============================================================================
SpectrumBandEditor::SpectrumBandEditor (StagMotionBandsAudioProcessor& owner)
    : processor (owner)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus (true);
    setTitle ("Band spectrum");
    setDescription ("Double-click to add a band. Drag a divider to move it. Double-click a divider to remove it.");

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

        for (auto* button : { &solo, &mute, &remove })
        {
            button->getProperties().set (StagProperties::onViolet, true);
            button->getProperties().set (StagProperties::chip, true);
            button->setColour (juce::TextButton::textColourOffId, cream);
        }

        solo.setColour (juce::TextButton::buttonOnColourId, apricot);
        solo.setColour (juce::TextButton::textColourOnId, ink);
        mute.setColour (juce::TextButton::buttonOnColourId, ink);
        mute.setColour (juce::TextButton::textColourOnId, cream);

        solo.setTooltip ("Solo band " + juce::String (band + 1));
        mute.setTooltip ("Mute band " + juce::String (band + 1));
        remove.setTooltip ("Remove band " + juce::String (band + 1) + " and merge it with its neighbour");
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

    displayDb.fill (floorDb);
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
    return getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (24.0f);
}

std::array<float, StagMotionBandsAudioProcessor::maxBands + 1> SpectrumBandEditor::bandEdges() const
{
    const auto plot = plotBounds();
    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();

    std::array<float, StagMotionBandsAudioProcessor::maxBands + 1> edges {};
    edges[0] = plot.getX();
    for (int index = 0; index < bandCount - 1; ++index)
        edges[static_cast<size_t> (index + 1)] = frequencyToX (crossovers[static_cast<size_t> (index)]);
    edges[static_cast<size_t> (bandCount)] = plot.getRight();
    return edges;
}

void SpectrumBandEditor::paint (juce::Graphics& g)
{
    auto& theme = themeOf (*this);
    const auto plot = plotBounds();
    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();
    const auto edges = bandEdges();
    const auto& names = EffectMenuPanel::effectNames();

    g.setColour (deepViolet);
    g.fillRoundedRectangle (plot, 12.0f);

    {
        juce::Graphics::ScopedSaveState clipped (g);
        {
            juce::Path clip;
            clip.addRoundedRectangle (plot, 12.0f);
            g.reduceClipRegion (clip);
        }

        // Band states sit under everything else.
        for (int band = 0; band < bandCount; ++band)
        {
            const auto area = juce::Rectangle<float>::leftTopRightBottom (
                edges[static_cast<size_t> (band)], plot.getY(),
                edges[static_cast<size_t> (band + 1)], plot.getBottom());
            const auto muted = parameterIsOn (processor, StagMotionBandsAudioProcessor::muteParameterId (band));
            const auto soloed = parameterIsOn (processor, StagMotionBandsAudioProcessor::soloParameterId (band));

            if (band == selectedBand)
            {
                g.setColour (cream.withAlpha (0.09f));
                g.fillRect (area);
            }

            if (soloed)
            {
                g.setColour (apricot.withAlpha (0.12f));
                g.fillRect (area);
            }

            if (muted)
            {
                g.setColour (ink.withAlpha (0.45f));
                g.fillRect (area);
                g.setColour (cream.withAlpha (0.06f));
                for (auto x = area.getX() - area.getHeight(); x < area.getRight(); x += 12.0f)
                    g.drawLine (x, area.getBottom(), x + area.getHeight(), area.getY(), 1.0f);
            }

            if (band == selectedBand || soloed)
            {
                g.setColour (band == selectedBand ? cream : apricot);
                g.fillRect (area.withHeight (3.0f).reduced (1.0f, 0.0f));
            }
        }

        // Grid.
        for (const auto frequency : frequencyLines)
        {
            g.setColour (cream.withAlpha (0.08f));
            g.drawVerticalLine (juce::roundToInt (frequencyToX (frequency)), plot.getY(), plot.getBottom());
        }

        const std::array<float, 4> dbLines { -24.0f, -48.0f, -72.0f, -96.0f };
        for (const auto db : dbLines)
        {
            const auto y = plot.getY() + juce::jmap (db, floorDb, 0.0f, 1.0f, 0.0f) * plot.getHeight();
            g.setColour (cream.withAlpha (0.08f));
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        // Spectrum.
        juce::Path spectrumPath;
        for (int point = 0; point < displayPoints; ++point)
        {
            const auto proportion = static_cast<float> (point) / static_cast<float> (displayPoints - 1);
            const auto x = plot.getX() + proportion * plot.getWidth();
            const auto normalized = juce::jmap (displayDb[static_cast<size_t> (point)], floorDb, 0.0f, 1.0f, 0.0f);
            const auto y = plot.getY() + juce::jlimit (0.0f, 1.0f, normalized) * plot.getHeight();

            if (point == 0)
                spectrumPath.startNewSubPath (x, y);
            else
                spectrumPath.lineTo (x, y);
        }

        auto fillPath = spectrumPath;
        fillPath.lineTo (plot.getRight(), plot.getBottom());
        fillPath.lineTo (plot.getX(), plot.getBottom());
        fillPath.closeSubPath();
        g.setGradientFill (juce::ColourGradient {
            lilac.withAlpha (0.42f), plot.getCentreX(), plot.getY() + plot.getHeight() * 0.25f,
            lilac.withAlpha (0.04f), plot.getCentreX(), plot.getBottom(), false
        });
        g.fillPath (fillPath);
        g.setColour (cream.withAlpha (0.95f));
        g.strokePath (spectrumPath, juce::PathStrokeType { 1.6f, juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded });

        if (mouseInside && draggingSeparator < 0 && hoveredSeparator < 0)
        {
            g.setColour (cream.withAlpha (0.35f));
            g.drawVerticalLine (juce::roundToInt (frequencyToX (hoveredFrequency)), plot.getY(), plot.getBottom());
        }

        // Band names and their effects.
        for (int band = 0; band < bandCount; ++band)
        {
            const auto left = edges[static_cast<size_t> (band)];
            const auto right = edges[static_cast<size_t> (band + 1)];
            const auto width = right - left;
            const auto selected = band == selectedBand;
            const auto muted = parameterIsOn (processor, StagMotionBandsAudioProcessor::muteParameterId (band));
            const auto effect = juce::jlimit (0, EffectMenuPanel::numEffects - 1, currentEffect (processor, band));
            const auto labelArea = juce::Rectangle<float> (left + 6.0f, plot.getY() + 14.0f, width - 12.0f, 18.0f);

            g.setFont (theme.semibold (14.0f));
            g.setColour (selected ? cream : mutedViolet);
            g.drawFittedText (width >= 70.0f ? "Band " + juce::String (band + 1) : juce::String (band + 1),
                              labelArea.toNearestInt(), juce::Justification::centred, 1, 0.8f);

            if (width >= 70.0f)
            {
                const auto detail = muted ? juce::String ("Muted")
                                          : effect == 0 ? juce::String ("No effect") : names[static_cast<size_t> (effect)];
                g.setFont (theme.regular (12.0f));
                g.setColour (selected ? cream.withAlpha (0.85f) : mutedViolet.withAlpha (0.8f));
                g.drawFittedText (detail, labelArea.translated (0.0f, 18.0f).toNearestInt(),
                                  juce::Justification::centred, 1, 0.8f);
            }
        }

        // Crossover dividers.
        for (int index = 0; index < bandCount - 1; ++index)
        {
            const auto frequency = crossovers[static_cast<size_t> (index)];
            const auto x = frequencyToX (frequency);
            const auto highlighted = index == draggingSeparator || index == hoveredSeparator;

            g.setColour (highlighted ? apricot : cream.withAlpha (0.75f));
            g.drawLine (x, plot.getY(), x, plot.getBottom(), highlighted ? 2.4f : 1.4f);

            const auto label = formatFrequency (frequency);
            const auto pill = juce::Rectangle<float> (x - 26.0f, plot.getCentreY() - 12.0f, 52.0f, 24.0f);
            g.setColour (highlighted ? apricot : cream);
            g.fillRoundedRectangle (pill, 12.0f);
            g.setColour (ink);
            g.setFont (theme.numeric (12.0f));
            g.drawText (label, pill, juce::Justification::centred, false);
        }
    }

    // Frequency axis.
    g.setFont (theme.numeric (11.0f));
    for (size_t line = 0; line < frequencyLines.size(); ++line)
    {
        const auto frequency = frequencyLines[line];
        const auto x = frequencyToX (frequency);
        auto labelArea = juce::Rectangle<float> { x - 24.0f, plot.getBottom() + 6.0f, 48.0f, 14.0f };
        auto justification = juce::Justification::centred;
        if (line == 0)
        {
            labelArea.setX (plot.getX() + 4.0f);
            justification = juce::Justification::centredLeft;
        }
        else if (line == frequencyLines.size() - 1)
        {
            labelArea.setRight (plot.getRight() - 4.0f);
            labelArea.setX (labelArea.getRight() - 48.0f);
            justification = juce::Justification::centredRight;
        }

        g.setColour (mutedViolet);
        g.drawText (formatFrequency (frequency), labelArea, justification, false);
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

    if (selectedBand >= bandCount)
        selectBand (bandCount - 1);

    const auto edges = bandEdges();

    for (int band = 0; band < StagMotionBandsAudioProcessor::maxBands; ++band)
    {
        const auto visible = band < bandCount;
        const auto canRemove = visible && bandCount > 1;
        soloButtons[static_cast<size_t> (band)].setVisible (visible);
        muteButtons[static_cast<size_t> (band)].setVisible (visible);
        removeButtons[static_cast<size_t> (band)].setVisible (canRemove);

        if (! visible)
            continue;

        constexpr int buttonWidth = 28;
        constexpr int buttonHeight = 24;
        constexpr int gap = 4;
        const auto count = canRemove ? 3 : 2;
        const auto groupWidth = count * buttonWidth + (count - 1) * gap;
        const auto centreX = (edges[static_cast<size_t> (band)] + edges[static_cast<size_t> (band + 1)]) * 0.5f;
        auto x = juce::roundToInt (centreX) - groupWidth / 2;
        const auto y = juce::roundToInt (plot.getBottom() - 12.0f) - buttonHeight;

        soloButtons[static_cast<size_t> (band)].setBounds (x, y, buttonWidth, buttonHeight);
        x += buttonWidth + gap;
        muteButtons[static_cast<size_t> (band)].setBounds (x, y, buttonWidth, buttonHeight);
        x += buttonWidth + gap;
        removeButtons[static_cast<size_t> (band)].setBounds (x, y, buttonWidth, buttonHeight);
    }
}

void SpectrumBandEditor::mouseDown (const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto plot = plotBounds();
    if (! plot.contains (event.position))
        return;

    draggingSeparator = separatorAtX (event.position.x, 10.0f);
    draggingSlot = processor.crossoverSlot (draggingSeparator);
    if (draggingSlot < 0)
        draggingSeparator = -1;

    if (draggingSeparator >= 0)
    {
        if (auto* parameter = processor.getParameters().getParameter (
                StagMotionBandsAudioProcessor::crossoverParameterId (draggingSlot)))
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
    processor.setCrossover (draggingSlot, frequency);
    updateBandButtons();
    repaint();
}

void SpectrumBandEditor::mouseUp (const juce::MouseEvent&)
{
    if (draggingSlot >= 0)
        if (auto* parameter = processor.getParameters().getParameter (
                StagMotionBandsAudioProcessor::crossoverParameterId (draggingSlot)))
            parameter->endChangeGesture();

    draggingSeparator = -1;
    draggingSlot = -1;
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

    // Display on a log-spaced grid with 1/6-octave smoothing so the high end
    // reads as a calm curve rather than a wall of FFT bins.
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    const auto binWidth = static_cast<float> (sampleRate) / static_cast<float> (fftSize);
    const auto halfWidth = std::pow (2.0f, 1.0f / 12.0f);
    constexpr int lastBin = fftSize / 2 - 1;

    for (int point = 0; point < displayPoints; ++point)
    {
        const auto proportion = static_cast<float> (point) / static_cast<float> (displayPoints - 1);
        const auto frequency = minimumFrequency * std::pow (maximumFrequency / minimumFrequency, proportion);
        const auto lowBin = juce::jlimit (1, lastBin, static_cast<int> (std::floor (frequency / halfWidth / binWidth)));
        const auto highBin = juce::jlimit (lowBin, lastBin, static_cast<int> (std::ceil (frequency * halfWidth / binWidth)));

        auto power = 0.0f;
        if (highBin - lowBin < 3)
        {
            // Below a few bins per point, interpolate between neighbouring
            // bins so the low end is a curve rather than a staircase.
            const auto exactBin = juce::jlimit (1.0f, static_cast<float> (lastBin - 1), frequency / binWidth);
            const auto lower = static_cast<int> (exactBin);
            const auto fraction = exactBin - static_cast<float> (lower);
            const auto a = fftData[static_cast<size_t> (lower)] / static_cast<float> (fftSize);
            const auto b = fftData[static_cast<size_t> (lower + 1)] / static_cast<float> (fftSize);
            const auto magnitude = a + (b - a) * fraction;
            power = magnitude * magnitude;
        }
        else
        {
            for (int bin = lowBin; bin <= highBin; ++bin)
            {
                const auto magnitude = fftData[static_cast<size_t> (bin)] / static_cast<float> (fftSize);
                power += magnitude * magnitude;
            }

            power /= static_cast<float> (highBin - lowBin + 1);
        }

        const auto value = juce::Decibels::gainToDecibels (std::sqrt (power), floorDb);
        auto& smoothed = displayDb[static_cast<size_t> (point)];
        smoothed = value > smoothed ? value : juce::jmax (floorDb, smoothed - 1.8f);
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

//==============================================================================
const std::array<juce::String, EffectMenuPanel::numEffects>& EffectMenuPanel::effectNames()
{
    static const std::array<juce::String, numEffects> names {
        "Off", "Chorus", "Flanger", "Phaser", "Tremolo",
        "Auto pan", "Ring mod", "Phase warp", "Freq shift"
    };
    return names;
}

EffectMenuPanel::EffectMenuPanel (StagMotionBandsAudioProcessor& owner)
    : processor (owner)
{
    setTitle ("Effect");

    const std::array<juce::String, numEffects> tooltips {
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

    for (int index = 0; index < numEffects; ++index)
    {
        auto& button = effectButtons[static_cast<size_t> (index)];
        button.setButtonText (effectNames()[static_cast<size_t> (index)]);
        button.setTooltip (tooltips[static_cast<size_t> (index)]);
        button.setRadioGroupId (0x5354);
        button.getProperties().set (StagProperties::segment, true);
        button.onClick = [this, index] { chooseEffect (index); };
        addAndMakeVisible (button);
    }

    updateButtons();
    startTimerHz (20);
}

void EffectMenuPanel::resized()
{
    auto bounds = getLocalBounds();
    constexpr int gap = 6;
    const auto count = static_cast<int> (effectButtons.size());

    // One row when there is room, otherwise two rows so every effect stays reachable.
    const auto rows = bounds.getWidth() >= 9 * 74 + 8 * gap ? 1 : 2;
    const auto perRow = (count + rows - 1) / rows;
    const auto rowHeight = (bounds.getHeight() - (rows - 1) * gap) / rows;

    for (int row = 0; row < rows; ++row)
    {
        auto rowArea = bounds.removeFromTop (rowHeight);
        bounds.removeFromTop (gap);
        const auto first = row * perRow;
        const auto last = juce::jmin (count, first + perRow);
        const auto inRow = last - first;
        const auto buttonWidth = (rowArea.getWidth() - (perRow - 1) * gap) / perRow;

        for (int index = first; index < last; ++index)
        {
            const auto isLastInRow = index == last - 1 && inRow == perRow;
            effectButtons[static_cast<size_t> (index)].setBounds (
                isLastInRow ? rowArea : rowArea.removeFromLeft (buttonWidth));
            rowArea.removeFromLeft (gap);
        }
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
    const auto effect = currentEffect (processor, selectedBand);

    for (int index = 0; index < static_cast<int> (effectButtons.size()); ++index)
        effectButtons[static_cast<size_t> (index)].setToggleState (
            index == effect, juce::dontSendNotification);
}

//==============================================================================
BandControlPanel::BandControlPanel (StagMotionBandsAudioProcessor& owner, EffectMenuPanel& menu)
    : processor (owner), effectMenu (menu)
{
    bandTitle.setText ("Band 1", juce::dontSendNotification);
    bandTitle.setColour (juce::Label::textColourId, ink);
    bandTitle.setBorderSize ({});

    bandRange.setColour (juce::Label::textColourId, mutedInk);
    bandRange.setBorderSize ({});

    configureFieldLabel (effectLabel, "Effect");
    configureFieldLabel (sourceLabel, "Source");
    configureFieldLabel (waveformLabel, "Wave");
    configureFieldLabel (divisionLabel, "Division");

    waveformSelector.addItemList ({ "Sine", "Triangle", "Saw", "Square" }, 1);
    divisionSelector.addItemList ({ "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1" }, 1);
    sourceSelector.addItemList ({ "LFO", "Envelope" }, 1);

    waveformSelector.setTooltip ("LFO waveform for the selected band");
    divisionSelector.setTooltip ("Musical cycle length when Project sync is on");
    sourceSelector.setTooltip ("Choose the modulation signal for the selected band");

    syncButton.setButtonText ("Project sync");
    syncButton.setClickingTogglesState (true);
    syncButton.setTooltip ("Replace free-rate Hz with divisions locked to the project tempo");

    retriggerButton.setButtonText ("MIDI retrigger");
    retriggerButton.setClickingTogglesState (true);
    retriggerButton.setTooltip ("Reset this band's LFO phase when a MIDI note starts");

    const std::array<juce::String, 8> labels {
        "Rate", "Depth", "Feedback", "Mix", "Stereo", "Attack", "Release", "Frequency"
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
        return juce::String (juce::roundToInt (value * 180.0)) + juce::String::fromUTF8 ("\xc2\xb0");
    };

    for (int index = 5; index < 7; ++index)
        parameterSliders[static_cast<size_t> (index)].textFromValueFunction = [] (double value) {
            return juce::String (value, value < 10.0 ? 1 : 0) + " ms";
        };

    parameterSliders[7].textFromValueFunction = [] (double value) {
        return juce::String (value, value < 100.0 ? 1 : 0) + " Hz";
    };

    for (auto& slider : parameterSliders)
        slider.updateText();

    addAndMakeVisible (bandTitle);
    addAndMakeVisible (bandRange);
    addAndMakeVisible (effectLabel);
    addAndMakeVisible (effectMenu);
    addAndMakeVisible (waveformLabel);
    addAndMakeVisible (divisionLabel);
    addAndMakeVisible (sourceLabel);
    addAndMakeVisible (waveformSelector);
    addAndMakeVisible (divisionSelector);
    addAndMakeVisible (sourceSelector);
    addAndMakeVisible (syncButton);
    addAndMakeVisible (retriggerButton);

    rebuildAttachments();
    updateBandRange();
    updateControlVisibility();
    startTimerHz (30);
}

void BandControlPanel::configureFieldLabel (juce::Label& label, const juce::String& labelText)
{
    label.setText (labelText, juce::dontSendNotification);
    label.getProperties().set (StagProperties::fieldLabel, true);
    label.setColour (juce::Label::textColourId, mutedInk);
    label.setBorderSize ({});
}

void BandControlPanel::configureSlider (juce::Slider& slider, juce::Label& label, const juce::String& labelText)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 28);
    slider.setNumDecimalPlacesToDisplay (2);
    slider.setTitle (labelText);

    configureFieldLabel (label, labelText);
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
    bandTitle.setText ("Band " + juce::String (selectedBand + 1), juce::dontSendNotification);
    rebuildAttachments();
    updateBandRange();
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

void BandControlPanel::updateBandRange()
{
    const auto bandCount = processor.getBandCount();
    const auto crossovers = processor.getSortedCrossovers();
    const auto band = juce::jmin (selectedBand, bandCount - 1);
    const auto low = band == 0 ? minimumFrequency : crossovers[static_cast<size_t> (band - 1)];
    const auto high = band == bandCount - 1 ? maximumFrequency : crossovers[static_cast<size_t> (band)];
    const auto text = formatHz (low) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + formatHz (high);

    if (bandRange.getText() != text)
        bandRange.setText (text, juce::dontSendNotification);
}

juce::Rectangle<int> BandControlPanel::activityMeterBounds() const
{
    return { getWidth() - 24 - 140, 31, 140, 6 };
}

void BandControlPanel::paint (juce::Graphics& g)
{
    auto& theme = themeOf (*this);
    g.fillAll (cream);

    // Live modulation activity for the selected band.
    const auto meter = activityMeterBounds().toFloat();
    g.setFont (theme.semibold (12.0f));
    g.setColour (mutedInk);
    g.drawText ("Motion", meter.withX (meter.getX() - 60.0f).withWidth (52.0f).withY (meter.getCentreY() - 8.0f).withHeight (16.0f),
                juce::Justification::centredRight, false);
    g.setColour (lilac);
    g.fillRoundedRectangle (meter, 3.0f);
    g.setColour (apricot.darker (0.1f));
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, displayedActivity)), 3.0f);

    if (dividerX > 0)
    {
        g.setColour (border.withAlpha (0.35f));
        g.drawVerticalLine (dividerX, static_cast<float> (knobArea.getY()), static_cast<float> (knobArea.getBottom()));
    }

    if (displayedEffect == BandModulationEngine::off)
    {
        g.setFont (theme.semibold (16.0f));
        g.setColour (ink);
        g.drawText ("Band " + juce::String (selectedBand + 1) + " is unprocessed.",
                    knobArea.withTrimmedBottom (knobArea.getHeight() / 2).withTrimmedBottom (4),
                    juce::Justification::centredBottom, false);
        g.setFont (theme.regular (14.0f));
        g.setColour (mutedInk);
        g.drawText ("Choose an effect above to start shaping it.",
                    knobArea.withTrimmedTop (knobArea.getHeight() / 2).withTrimmedTop (4),
                    juce::Justification::centredTop, false);
    }
}

void BandControlPanel::updateControlVisibility()
{
    const auto effect = currentEffect (processor, selectedBand);
    const auto source = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::modulationSourceParameterId (selectedBand))->load()));
    const auto tempoSync = parameterIsOn (processor, StagMotionBandsAudioProcessor::syncParameterId (selectedBand));
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
        effect == BandModulationEngine::phaseWarp ? "Warp"
          : effect == BandModulationEngine::autoPan ? "Pan"
                                                     : "Depth",
        juce::dontSendNotification);
    parameterLabels[7].setText (
        effect == BandModulationEngine::frequencyShifter ? "Shift" : "Carrier",
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
    repaint (activityMeterBounds().expanded (2));
    updateBandRange();

    const auto effect = currentEffect (processor, selectedBand);
    const auto source = static_cast<int> (std::lround (
        processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::modulationSourceParameterId (selectedBand))->load()));
    const auto tempoSync = parameterIsOn (processor, StagMotionBandsAudioProcessor::syncParameterId (selectedBand));
    if (effect != displayedEffect || source != displayedSource || tempoSync != displayedSync)
        updateControlVisibility();
}

void BandControlPanel::resized()
{
    auto& theme = themeOf (*this);
    bandTitle.setFont (theme.semibold (22.0f));
    bandRange.setFont (theme.regular (14.0f));

    auto bounds = getLocalBounds().reduced (24, 0);
    bounds.removeFromTop (18);

    auto titleRow = bounds.removeFromTop (30);
    bandTitle.setBounds (titleRow.removeFromLeft (96));
    bandRange.setBounds (titleRow.removeFromLeft (220));

    bounds.removeFromTop (14);
    effectLabel.setBounds (bounds.removeFromTop (16));
    bounds.removeFromTop (6);
    const auto twoRows = bounds.getWidth() < 9 * 74 + 8 * 6;
    effectMenu.setBounds (bounds.removeFromTop (twoRows ? 78 : 40));

    bounds.removeFromTop (18);
    bounds.removeFromBottom (18);

    // Modulation settings on the left, the effect's knobs on the right.
    const auto showsSettings = sourceSelector.isVisible() || waveformSelector.isVisible()
                            || syncButton.isVisible() || retriggerButton.isVisible();
    knobArea = bounds;
    dividerX = -1;

    if (showsSettings)
    {
        auto settings = bounds.removeFromLeft (juce::jlimit (230, 320, bounds.getWidth() * 34 / 100));
        dividerX = settings.getRight() + 20;
        bounds.removeFromLeft (40);
        knobArea = bounds;

        constexpr int gap = 10;
        constexpr int labelHeight = 16;
        constexpr int controlHeight = 34;
        const auto half = (settings.getWidth() - gap) / 2;

        auto placePair = [&] (juce::Component* firstLabel, juce::Component* first,
                              juce::Component* secondLabel, juce::Component* second)
        {
            const auto firstVisible = first != nullptr && first->isVisible();
            const auto secondVisible = second != nullptr && second->isVisible();
            if (! firstVisible && ! secondVisible)
                return;

            const auto hasLabels = (firstLabel != nullptr && firstVisible) || (secondLabel != nullptr && secondVisible);
            auto labels = hasLabels ? settings.removeFromTop (labelHeight + 4) : juce::Rectangle<int>();
            auto controls = settings.removeFromTop (controlHeight);
            settings.removeFromTop (12);

            auto place = [&] (juce::Component* label, juce::Component* control, bool fullWidth)
            {
                auto labelCell = fullWidth ? labels : labels.removeFromLeft (half);
                auto controlCell = fullWidth ? controls : controls.removeFromLeft (half);
                if (label != nullptr)
                    label->setBounds (labelCell.withHeight (labelHeight));
                control->setBounds (controlCell);
            };

            if (firstVisible && secondVisible)
            {
                place (firstLabel, first, false);
                labels.removeFromLeft (gap);
                controls.removeFromLeft (gap);
                place (secondLabel, second, true);
            }
            else if (firstVisible)
                place (firstLabel, first, false);
            else
                place (secondLabel, second, false);
        };

        placePair (&sourceLabel, &sourceSelector, &waveformLabel, &waveformSelector);
        placePair (nullptr, &syncButton, nullptr, &retriggerButton);
    }

    std::vector<int> visibleSliderIndices;
    for (int index = 0; index < static_cast<int> (parameterSliders.size()); ++index)
        if (parameterSliders[static_cast<size_t> (index)].isVisible())
            visibleSliderIndices.push_back (index);

    // When Project sync is on, Division takes the place of the Rate (or
    // Carrier) knob, so it sits in the same row as the other timing controls.
    const auto showsDivision = divisionSelector.isVisible();
    const auto count = static_cast<int> (visibleSliderIndices.size()) + (showsDivision ? 1 : 0);
    if (count == 0)
        return;

    const auto columnWidth = juce::jmin (130, knobArea.getWidth() / count);
    auto row = knobArea.withSizeKeepingCentre (columnWidth * count, knobArea.getHeight());
    if (showsSettings)
        row.setX (knobArea.getX());

    if (showsDivision)
    {
        auto column = row.removeFromLeft (columnWidth);
        divisionLabel.setJustificationType (juce::Justification::centred);
        divisionLabel.setBounds (column.removeFromTop (18));
        column.removeFromTop (4);
        divisionSelector.setBounds (column.reduced (8, 0).withSizeKeepingCentre (column.getWidth() - 16, 34));
    }

    for (const auto index : visibleSliderIndices)
    {
        auto column = row.removeFromLeft (columnWidth);
        parameterLabels[static_cast<size_t> (index)].setBounds (column.removeFromTop (18));
        column.removeFromTop (4);
        parameterSliders[static_cast<size_t> (index)].setBounds (column.reduced (8, 0));
    }
}

//==============================================================================
StagMotionBandsAudioProcessorEditor::StagMotionBandsAudioProcessorEditor (
    StagMotionBandsAudioProcessor& owner)
    : AudioProcessorEditor (&owner), spectrumEditor (owner), effectMenu (owner),
      bandControls (owner, effectMenu)
{
    setLookAndFeel (&stagLookAndFeel);
    mark = juce::Drawable::createFromImageData (BinaryData::stagmark_svg, BinaryData::stagmark_svgSize);

    setOpaque (true);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (760, 600, 1600, 1050);
    setSize (1100, 820);

    addAndMakeVisible (spectrumEditor);
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

int StagMotionBandsAudioProcessorEditor::deckTop() const
{
    const auto deckHeight = getWidth() < 9 * 74 + 8 * 6 + 48 ? 352 : 314;
    return getHeight() - deckHeight;
}

void StagMotionBandsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (violet);

    if (mark != nullptr)
        mark->drawWithin (g, { 24.0f, 12.0f, 36.0f, 44.0f }, juce::RectanglePlacement::centred, 1.0f);

    g.setFont (stagLookAndFeel.semibold (24.0f));
    g.setColour (cream);
    g.drawText ("Stag Motion Bands", juce::Rectangle<int> (72, 10, 320, 30), juce::Justification::centredLeft, false);
    g.setFont (stagLookAndFeel.regular (13.0f));
    g.setColour (cream.withAlpha (0.85f));
    g.drawText ("Stag Audio  " + juce::String::fromUTF8 ("\xc2\xb7") + "  Multiband modulation",
                juce::Rectangle<int> (72, 38, 320, 18), juce::Justification::centredLeft, false);

    const auto badge = juce::Rectangle<float> (static_cast<float> (getWidth()) - 24.0f - 86.0f, 21.0f, 86.0f, 26.0f);
    g.setColour (apricot);
    g.fillRoundedRectangle (badge, 13.0f);
    g.setColour (ink);
    g.setFont (stagLookAndFeel.semibold (12.0f));
    g.drawText ("Alpha " + juce::String (JucePlugin_VersionString).upToLastOccurrenceOf (".", false, false),
                badge, juce::Justification::centred, false);

    if (getWidth() >= 960)
    {
        g.setFont (stagLookAndFeel.regular (13.0f));
        g.setColour (mutedViolet);
        g.drawText ("Double-click to add a band  " + juce::String::fromUTF8 ("\xc2\xb7")
                        + "  Drag dividers  " + juce::String::fromUTF8 ("\xc2\xb7")
                        + "  " + juce::String::fromUTF8 ("\xe2\x86\x90 \xe2\x86\x92") + " switch bands",
                    juce::Rectangle<int> (400, 21, getWidth() - 400 - 24 - 86 - 20, 26),
                    juce::Justification::centredRight, false);
    }
}

void StagMotionBandsAudioProcessorEditor::resized()
{
    const auto top = deckTop();
    spectrumEditor.setBounds (16, 68, getWidth() - 32, top - 68 - 14);
    bandControls.setBounds (0, top, getWidth(), getHeight() - top);
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
