#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "StagLookAndFeel.h"

class SpectrumBandEditor final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit SpectrumBandEditor (StagMotionBandsAudioProcessor&);
    ~SpectrumBandEditor() override = default;

    void setBandSelectedCallback (std::function<void(int)> callback);
    int getSelectedBand() const noexcept { return selectedBand; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void selectPreviousBand();
    void selectNextBand();

private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int displayPoints = 220;

    void timerCallback() override;
    void updateSpectrum();
    void updateBandButtons();
    void selectBand (int bandIndex);
    float xToFrequency (float x) const;
    float frequencyToX (float frequency) const;
    int separatorAtX (float x, float tolerance) const;
    int bandAtX (float x) const;
    juce::Rectangle<float> plotBounds() const;
    juce::String formatFrequency (float frequency) const;
    std::array<float, StagMotionBandsAudioProcessor::maxBands + 1> bandEdges() const;

    StagMotionBandsAudioProcessor& processor;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window {
        fftSize, juce::dsp::WindowingFunction<float>::hann, true
    };

    std::array<float, fftSize> sampleHistory {};
    std::array<float, fftSize * 2> fftData {};
    std::array<float, displayPoints> displayDb {};
    std::array<float, 1024> incomingSamples {};

    std::array<juce::TextButton, StagMotionBandsAudioProcessor::maxBands> soloButtons;
    std::array<juce::TextButton, StagMotionBandsAudioProcessor::maxBands> muteButtons;
    std::array<juce::TextButton, StagMotionBandsAudioProcessor::maxBands> removeButtons;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
               StagMotionBandsAudioProcessor::maxBands> soloAttachments;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>,
               StagMotionBandsAudioProcessor::maxBands> muteAttachments;

    int draggingSeparator = -1; // sorted position, as drawn
    int draggingSlot = -1;      // crossover parameter slot being dragged
    int hoveredSeparator = -1;
    int selectedBand = 0;
    float hoveredFrequency = 0.0f;
    bool mouseInside = false;
    std::function<void(int)> bandSelectedCallback;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumBandEditor)
};

class EffectMenuPanel final : public juce::Component,
                              private juce::Timer
{
public:
    explicit EffectMenuPanel (StagMotionBandsAudioProcessor&);
    ~EffectMenuPanel() override = default;

    void resized() override;
    void selectBand (int bandIndex);

    static constexpr int numEffects = 9;
    static const std::array<juce::String, numEffects>& effectNames();

private:
    void chooseEffect (int effectIndex);
    void timerCallback() override;
    void updateButtons();

    StagMotionBandsAudioProcessor& processor;
    int selectedBand = 0;
    std::array<juce::TextButton, numEffects> effectButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectMenuPanel)
};

class BandControlPanel final : public juce::Component,
                               private juce::Timer
{
public:
    BandControlPanel (StagMotionBandsAudioProcessor&, EffectMenuPanel&);
    ~BandControlPanel() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;
    void selectBand (int bandIndex);

private:
    void configureSlider (juce::Slider&, juce::Label&, const juce::String& labelText);
    void configureFieldLabel (juce::Label&, const juce::String& labelText);
    void rebuildAttachments();
    void updateControlVisibility();
    void updateBandRange();
    void timerCallback() override;
    juce::Rectangle<int> activityMeterBounds() const;

    StagMotionBandsAudioProcessor& processor;
    EffectMenuPanel& effectMenu;
    int selectedBand = 0;
    juce::Label bandTitle;
    juce::Label bandRange;
    juce::Label effectLabel;
    juce::Label waveformLabel;
    juce::Label divisionLabel;
    juce::Label sourceLabel;
    juce::ComboBox waveformSelector;
    juce::ComboBox divisionSelector;
    juce::ComboBox sourceSelector;
    juce::ToggleButton syncButton;
    juce::ToggleButton retriggerButton;
    std::array<juce::Slider, 8> parameterSliders;
    std::array<juce::Label, 8> parameterLabels;
    float displayedActivity = 0.0f;
    int displayedEffect = -1;
    int displayedSource = -1;
    bool displayedSync = false;
    juce::Rectangle<int> knobArea;
    int dividerX = -1;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divisionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> retriggerAttachment;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 8> sliderAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandControlPanel)
};

class StagMotionBandsAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit StagMotionBandsAudioProcessorEditor (StagMotionBandsAudioProcessor&);
    ~StagMotionBandsAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int deckTop() const;

    StagLookAndFeel stagLookAndFeel;
    std::unique_ptr<juce::Drawable> mark;
    SpectrumBandEditor spectrumEditor;
    EffectMenuPanel effectMenu;
    BandControlPanel bandControls;
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagMotionBandsAudioProcessorEditor)
};
