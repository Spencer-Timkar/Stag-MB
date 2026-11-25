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
    double getTailLengthSeconds() const override;

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

    // Band editing (message thread). Per-band settings move with their bands:
    // addCrossover splits a band and both halves keep its settings; removeCrossover
    // merges the two adjacent bands into the lower one; removeBand drops that band's
    // settings (band 0 merges right, any other band merges into its lower neighbour).
    void addCrossover (float frequencyHz);
    void removeCrossover (int crossoverIndex);
    void removeBand (int bandIndex);

    // Crossover parameters are stored in slots that host automation can leave unsorted;
    // crossoverSlot maps a sorted position (as drawn) to its parameter slot, or -1.
    int crossoverSlot (int sortedIndex) const;

    // Moves the crossover in this parameter slot without a gesture; the caller (a drag)
    // owns the gesture.
    void setCrossover (int slot, float frequencyHz);

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
    static juce::String thresholdParameterId (int bandIndex);
    static juce::String rangeParameterId (int bandIndex);

private:
    // Audio is processed in short sections so crossover glides, band gain ramps,
    // layout fades and MIDI retriggers land within this many samples, and so any
    // host block size works without reallocation.
    static constexpr int sectionSize = 32;

    struct BandParameters
    {
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* effectType = nullptr;
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* depth = nullptr;
        std::atomic<float>* feedback = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* waveform = nullptr;
        std::atomic<float>* stereoSpread = nullptr;
        std::atomic<float>* modulationSource = nullptr;
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* retrigger = nullptr;
        std::atomic<float>* carrierFrequency = nullptr;
        std::atomic<float>* threshold = nullptr;
        std::atomic<float>* range = nullptr;
    };

    enum class LayoutFade { none, fadingOut, fadingIn };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray bandParameterIds (int bandIndex);

    void setParameterWithGesture (const juce::String& parameterId, float normalisedValue);
    void setCrossoverWithGesture (int crossoverIndex, float frequencyHz);
    void setBandCountWithGesture (int bandCount);
    void copyBandSettings (int sourceBand, int destinationBand);
    void removeBandAndCrossover (int droppedBand, int droppedCrossover);

    void pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer);
    // Host tempo, plus the beat position at the start of a section while the transport plays.
    struct SectionTransport
    {
        double bpm = 120.0;
        std::optional<double> ppq;
    };

    void processSection (juce::AudioBuffer<float>& section, bool noteOn, const SectionTransport& transport);
    bool targetLayoutDiffers() const noexcept;
    void applyTargetLayout();
    void updateCrossoverTargets();
    void splitIntoBands (const juce::AudioBuffer<float>& section);
    void applyBandEffect (int bandIndex, int numSamples, const SectionTransport& transport);
    bool anyBandSoloed() const noexcept;
    bool bandIsAudible (int bandIndex, bool anySolo) const noexcept;
    int readChoice (const std::atomic<float>* value) const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* bandCountParameter = nullptr;
    std::array<std::atomic<float>*, maxCrossovers> crossoverParameters {};
    std::array<BandParameters, maxBands> bandParameters {};

    std::array<juce::dsp::LinkwitzRileyFilter<float>, maxCrossovers> lowPassFilters;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, maxCrossovers> highPassFilters;
    // allPassFilters[band][crossover], used for crossover > band: realigns the phase of
    // lower bands with the higher splits so the bands sum flat.
    std::array<std::array<juce::dsp::LinkwitzRileyFilter<float>, maxCrossovers>, maxCrossovers> allPassFilters;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, maxCrossovers> crossoverSmoothers;
    std::array<float, maxCrossovers> appliedCutoffs {};

    std::array<BandModulationEngine, maxBands> modulationEngines;
    std::array<std::atomic<float>, maxBands> modulationActivity {};
    std::array<juce::SmoothedValue<float>, maxBands> bandGains;
    std::array<juce::AudioBuffer<float>, maxBands> bandBuffers;
    juce::AudioBuffer<float> remainingBuffer;
    int preparedChannels = 0;
    double preparedSampleRate = 44100.0;

    // Layout = band count and per-band effect type. Changing either fades the output
    // out on the old layout, switches with cleared filter/effect state, then fades in.
    int appliedBandCount = 1;
    std::array<int, maxBands> appliedEffects {};
    LayoutFade layoutFade = LayoutFade::none;
    float layoutGain = 1.0f;
    float layoutFadeStep = 1.0f;

    std::array<float, analyzerCapacity> analyzerRing {};
    juce::AbstractFifo analyzerFifo { analyzerCapacity };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StagMotionBandsAudioProcessor)
};
