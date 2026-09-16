#pragma once

#include <JuceHeader.h>
#include "ModulationDSP.h"

class StagMotionBandsAudioProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int maxBands = 4;
    static constexpr int maxCrossovers = maxBands - 1;
    static constexpr int analyzerCapacity = 32768;

    StagMotionBandsAudioProcessor();
    ~StagMotionBandsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    const juce::AudioProcessorValueTreeState& getParameters() const noexcept { return parameters; }

    int getBandCount() const noexcept;
    std::array<float, maxCrossovers> getSortedCrossovers() const;
    void addCrossover (float frequencyHz);
    void removeCrossover (int crossoverIndex);
    void removeBand (int bandIndex);
    void setCrossover (int crossoverIndex, float frequencyHz);

    int getAnalyzerSamplesAvailable() const noexcept;
    int popAnalyzerSamples (float* destination, int maxSamples);
    float getModulationActivity (int bandIndex) const noexcept;

    static juce::String crossoverParameterId (int index);
    static juce::String muteParameterId (int bandIndex);
    static juce::String soloParameterId (int bandIndex);
    static juce::String effectTypeParameterId (int bandIndex);
    static juce::String rateParameterId (int bandIndex);
    static juce::String depthParameterId (int bandIndex);
    static juce::String feedbackParameterId (int bandIndex);
    static juce::String mixParameterId (int bandIndex);
    static juce::String syncParameterId (int bandIndex);
    static juce::String divisionParameterId (int bandIndex);
    static juce::String waveformParameterId (int bandIndex);
    static juce::String stereoSpreadParameterId (int bandIndex);
    static juce::String modulationSourceParameterId (int bandIndex);
    static juce::String attackParameterId (int bandIndex);
    static juce::String releaseParameterId (int bandIndex);
    static juce::String retriggerParameterId (int bandIndex);
    static juce::String carrierFrequencyParameterId (int bandIndex);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer);
    void updateFilterCutoffs (const std::array<float, maxCrossovers>& frequencies, int numCrossovers);
    void applyBandEffect (int bandIndex, int numSamples, double bpm);

    juce::AudioProcessorValueTreeState parameters;

    std::array<juce::dsp::LinkwitzRileyFilter<float>, maxCrossovers> lowPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, maxCrossovers> highPassFilters;
    std::array<BandModulationEngine, maxBands> modulationEngines;
    std::array<std::atomic<float>, maxBands> modulationActivity {};
    std::array<juce::AudioBuffer<float>, maxBands> bandBuffers;
    juce::AudioBuffer<float> remainingBuffer;
    int preparedBlockSize = 0;
    int preparedChannels = 0;

    std::array<float, analyzerCapacity> analyzerRing {};
    juce::AbstractFifo analyzerFifo { analyzerCapacity };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagMotionBandsAudioProcessor)
};
