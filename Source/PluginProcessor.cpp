#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float minimumFrequency = 20.0f;
constexpr float maximumFrequency = 20000.0f;

float clampFrequency (float value)
{
    return juce::jlimit (minimumFrequency, maximumFrequency, value);
}
}

StagMotionBandsAudioProcessor::StagMotionBandsAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& filter : lowPassFilters)
        filter.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);

    for (auto& filter : highPassFilters)
        filter.setType (juce::dsp::LinkwitzRileyFilterType::highpass);

    for (auto& bandFilters : allPassFilters)
        for (auto& filter : bandFilters)
            filter.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

    bandCountParameter = parameters.getRawParameterValue ("bandCount");

    for (int index = 0; index < maxCrossovers; ++index)
        crossoverParameters[static_cast<size_t> (index)] = parameters.getRawParameterValue (crossoverParameterId (index));

    for (int band = 0; band < maxBands; ++band)
    {
        auto& pointers = bandParameters[static_cast<size_t> (band)];
        pointers.mute = parameters.getRawParameterValue (muteParameterId (band));
        pointers.solo = parameters.getRawParameterValue (soloParameterId (band));
        pointers.effectType = parameters.getRawParameterValue (effectTypeParameterId (band));
        pointers.rate = parameters.getRawParameterValue (rateParameterId (band));
        pointers.depth = parameters.getRawParameterValue (depthParameterId (band));
        pointers.feedback = parameters.getRawParameterValue (feedbackParameterId (band));
        pointers.mix = parameters.getRawParameterValue (mixParameterId (band));
        pointers.sync = parameters.getRawParameterValue (syncParameterId (band));
        pointers.division = parameters.getRawParameterValue (divisionParameterId (band));
        pointers.waveform = parameters.getRawParameterValue (waveformParameterId (band));
        pointers.stereoSpread = parameters.getRawParameterValue (stereoSpreadParameterId (band));
        pointers.modulationSource = parameters.getRawParameterValue (modulationSourceParameterId (band));
        pointers.attack = parameters.getRawParameterValue (attackParameterId (band));
        pointers.release = parameters.getRawParameterValue (releaseParameterId (band));
        pointers.retrigger = parameters.getRawParameterValue (retriggerParameterId (band));
        pointers.carrierFrequency = parameters.getRawParameterValue (carrierFrequencyParameterId (band));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout StagMotionBandsAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> result;

    result.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { "bandCount", 1 }, "Band Count", 1, maxBands, 1));

    const std::array<float, maxCrossovers> defaults { 180.0f, 1200.0f, 6000.0f };

    for (int index = 0; index < maxCrossovers; ++index)
    {
        juce::NormalisableRange<float> frequencyRange { minimumFrequency, maximumFrequency };
        frequencyRange.setSkewForCentre (1000.0f);

        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { crossoverParameterId (index), 1 },
            "Crossover " + juce::String (index + 1),
            frequencyRange,
            defaults[static_cast<size_t> (index)],
            juce::AudioParameterFloatAttributes().withLabel ("Hz")));
    }

    for (int band = 0; band < maxBands; ++band)
    {
        result.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { muteParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Mute",
            false));

        result.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { soloParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Solo",
            false));

        result.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { effectTypeParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Effect",
            juce::StringArray {
                "Off", "Chorus", "Flanger", "Phaser", "Tremolo",
                "Auto Pan", "Ring Mod", "Phase Warp", "Frequency Shifter"
            },
            0));

        juce::NormalisableRange<float> rateRange { 0.05f, 10.0f, 0.001f };
        rateRange.setSkewForCentre (1.0f);
        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { rateParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Rate",
            rateRange,
            0.5f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float value, int) {
                    return juce::String (value, 2) + " Hz";
                })));

        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { depthParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Depth",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            0.55f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            })));

        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { feedbackParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Feedback",
            juce::NormalisableRange<float> { -0.85f, 0.85f, 0.001f },
            0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            })));

        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { mixParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Mix",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            0.5f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) {
                return juce::String (juce::roundToInt (value * 100.0f)) + "%";
            })));

        result.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { syncParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Tempo Sync",
            false));

        result.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { divisionParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Division",
            juce::StringArray { "1/32", "1/16", "1/8", "1/4", "1/2", "1/1", "2/1" },
            3));

        result.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { waveformParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Waveform",
            juce::StringArray { "Sine", "Triangle", "Saw", "Square" },
            0));

        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { stereoSpreadParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Stereo Spread",
            juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f },
            0.5f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float value, int) {
                return juce::String (juce::roundToInt (value * 180.0f)) + " deg";
            })));

        result.push_back (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { modulationSourceParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Modulation Source",
            juce::StringArray { "LFO", "Envelope" },
            0));

        juce::NormalisableRange<float> attackRange { 1.0f, 500.0f, 0.1f };
        attackRange.setSkewForCentre (30.0f);
        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { attackParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Envelope Attack",
            attackRange,
            20.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([] (float value, int) {
                    return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
                })));

        juce::NormalisableRange<float> releaseRange { 10.0f, 2000.0f, 0.1f };
        releaseRange.setSkewForCentre (180.0f);
        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { releaseParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Envelope Release",
            releaseRange,
            180.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ms")
                .withStringFromValueFunction ([] (float value, int) {
                    return juce::String (value, 0) + " ms";
                })));

        result.push_back (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { retriggerParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " MIDI Retrigger",
            false));

        juce::NormalisableRange<float> carrierRange { 20.0f, 2000.0f, 0.1f };
        carrierRange.setSkewForCentre (180.0f);
        result.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { carrierFrequencyParameterId (band), 1 },
            "Band " + juce::String (band + 1) + " Carrier Frequency",
            carrierRange,
            80.0f,
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float value, int) {
                    return juce::String (value, value < 100.0f ? 1 : 0) + " Hz";
                })));
    }

    return { result.begin(), result.end() };
}

void StagMotionBandsAudioProcessor::prepareToPlay (double sampleRate, int)
{
    preparedChannels = juce::jmax (1, getTotalNumOutputChannels());

    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (sectionSize),
        static_cast<juce::uint32> (preparedChannels)
    };

    for (auto& filter : lowPassFilters)
        filter.prepare (spec);

    for (auto& filter : highPassFilters)
        filter.prepare (spec);

    for (auto& bandFilters : allPassFilters)
        for (auto& filter : bandFilters)
            filter.prepare (spec);

    for (auto& engine : modulationEngines)
        engine.prepare (spec);

    for (auto& smoother : crossoverSmoothers)
        smoother.reset (sampleRate, 0.05);

    for (auto& gain : bandGains)
        gain.reset (sampleRate, 0.01);

    layoutFadeStep = static_cast<float> (1.0 / (0.005 * sampleRate));

    remainingBuffer.setSize (preparedChannels, sectionSize);
    for (auto& bandBuffer : bandBuffers)
        bandBuffer.setSize (preparedChannels, sectionSize);

    applyTargetLayout();
    layoutFade = LayoutFade::none;
    layoutGain = 1.0f;

    analyzerFifo.reset();
}

void StagMotionBandsAudioProcessor::releaseResources()
{
}

bool StagMotionBandsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input != output)
        return false;

    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void StagMotionBandsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();

    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    pushAnalyzerSamples (buffer);

    if (preparedChannels == 0)
        return;

    jassert (buffer.getNumChannels() <= preparedChannels);
    const auto numChannels = juce::jmin (buffer.getNumChannels(), preparedChannels);
    const auto numSamples = buffer.getNumSamples();

    auto bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            bpm = position->getBpm().orFallback (120.0);

    auto midiEvent = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();

    for (int start = 0; start < numSamples; start += sectionSize)
    {
        const auto length = juce::jmin (sectionSize, numSamples - start);

        bool noteOn = false;
        for (; midiEvent != midiEnd && (*midiEvent).samplePosition < start + length; ++midiEvent)
        {
            const auto metadata = *midiEvent;
            noteOn = noteOn
                  || (metadata.numBytes >= 3 && (metadata.data[0] & 0xf0) == 0x90 && metadata.data[2] != 0);
        }

        juce::AudioBuffer<float> section (buffer.getArrayOfWritePointers(), numChannels, start, length);
        processSection (section, noteOn, bpm);
    }
}

void StagMotionBandsAudioProcessor::processSection (juce::AudioBuffer<float>& section, bool noteOn, double bpm)
{
    const auto numSamples = section.getNumSamples();
    const auto numChannels = section.getNumChannels();

    if (layoutFade != LayoutFade::fadingOut && targetLayoutDiffers())
        layoutFade = LayoutFade::fadingOut;

    // While fading out, the old crossovers stay put; the new ones are applied at silence.
    if (layoutFade != LayoutFade::fadingOut)
        updateCrossoverTargets();

    const auto anySolo = anyBandSoloed();

    for (int band = 0; band < appliedBandCount; ++band)
    {
        const auto& pointers = bandParameters[static_cast<size_t> (band)];
        auto& gain = bandGains[static_cast<size_t> (band)];
        auto& engine = modulationEngines[static_cast<size_t> (band)];
        auto& appliedEffect = appliedEffects[static_cast<size_t> (band)];

        // An effect change ramps this band to silence, swaps the effect with cleared
        // state, then ramps back, so stale delay lines and phase jumps never click.
        const auto targetEffect = readChoice (pointers.effectType);
        if (targetEffect != appliedEffect && ! gain.isSmoothing() && gain.getCurrentValue() == 0.0f)
        {
            appliedEffect = targetEffect;
            engine.reset();
        }

        gain.setTargetValue (bandIsAudible (band, anySolo) && targetEffect == appliedEffect ? 1.0f : 0.0f);

        if (noteOn && pointers.retrigger->load() >= 0.5f)
            engine.trigger();
    }

    splitIntoBands (section);

    for (int band = 0; band < appliedBandCount; ++band)
        applyBandEffect (band, numSamples, bpm);

    section.clear();

    for (int band = 0; band < appliedBandCount; ++band)
    {
        auto& gain = bandGains[static_cast<size_t> (band)];
        const auto startGain = gain.getCurrentValue();
        const auto endGain = gain.skip (numSamples);
        if (startGain == 0.0f && endGain == 0.0f)
            continue;

        const auto& bandBuffer = bandBuffers[static_cast<size_t> (band)];
        for (int channel = 0; channel < numChannels; ++channel)
            section.addFromWithRamp (channel, 0, bandBuffer.getReadPointer (channel), numSamples, startGain, endGain);
    }

    if (layoutFade == LayoutFade::none)
        return;

    const auto startGain = layoutGain;
    const auto change = layoutFadeStep * static_cast<float> (numSamples);
    layoutGain = layoutFade == LayoutFade::fadingOut ? juce::jmax (0.0f, layoutGain - change)
                                                     : juce::jmin (1.0f, layoutGain + change);
    section.applyGainRamp (0, numSamples, startGain, layoutGain);

    if (layoutFade == LayoutFade::fadingOut && layoutGain <= 0.0f)
    {
        applyTargetLayout();
        layoutFade = LayoutFade::fadingIn;
    }
    else if (layoutFade == LayoutFade::fadingIn && layoutGain >= 1.0f)
    {
        layoutFade = LayoutFade::none;
    }
}

bool StagMotionBandsAudioProcessor::targetLayoutDiffers() const noexcept
{
    return getBandCount() != appliedBandCount;
}

void StagMotionBandsAudioProcessor::applyTargetLayout()
{
    appliedBandCount = getBandCount();
    const auto crossovers = getSortedCrossovers();

    for (int index = 0; index < maxCrossovers; ++index)
    {
        const auto frequency = crossovers[static_cast<size_t> (index)];
        crossoverSmoothers[static_cast<size_t> (index)].setCurrentAndTargetValue (frequency);
        appliedCutoffs[static_cast<size_t> (index)] = frequency;

        auto& lowPass = lowPassFilters[static_cast<size_t> (index)];
        auto& highPass = highPassFilters[static_cast<size_t> (index)];
        lowPass.setCutoffFrequency (frequency);
        highPass.setCutoffFrequency (frequency);
        lowPass.reset();
        highPass.reset();

        for (auto& bandFilters : allPassFilters)
        {
            bandFilters[static_cast<size_t> (index)].setCutoffFrequency (frequency);
            bandFilters[static_cast<size_t> (index)].reset();
        }
    }

    const auto anySolo = anyBandSoloed();

    for (int band = 0; band < maxBands; ++band)
    {
        appliedEffects[static_cast<size_t> (band)] = readChoice (bandParameters[static_cast<size_t> (band)].effectType);
        modulationEngines[static_cast<size_t> (band)].reset();
        bandGains[static_cast<size_t> (band)].setCurrentAndTargetValue (bandIsAudible (band, anySolo) ? 1.0f : 0.0f);
    }
}

void StagMotionBandsAudioProcessor::updateCrossoverTargets()
{
    const auto crossovers = getSortedCrossovers();

    for (int index = 0; index < appliedBandCount - 1; ++index)
        crossoverSmoothers[static_cast<size_t> (index)].setTargetValue (crossovers[static_cast<size_t> (index)]);
}

void StagMotionBandsAudioProcessor::splitIntoBands (const juce::AudioBuffer<float>& section)
{
    const auto numSamples = section.getNumSamples();
    const auto numSplits = appliedBandCount - 1;

    if (numSplits == 0)
    {
        bandBuffers[0].makeCopyOf (section, true);
        return;
    }

    for (int index = 0; index < numSplits; ++index)
    {
        const auto cutoff = crossoverSmoothers[static_cast<size_t> (index)].skip (numSamples);
        auto& applied = appliedCutoffs[static_cast<size_t> (index)];
        if (juce::exactlyEqual (cutoff, applied))
            continue;

        applied = cutoff;
        lowPassFilters[static_cast<size_t> (index)].setCutoffFrequency (cutoff);
        highPassFilters[static_cast<size_t> (index)].setCutoffFrequency (cutoff);
        for (auto& bandFilters : allPassFilters)
            bandFilters[static_cast<size_t> (index)].setCutoffFrequency (cutoff);
    }

    const auto process = [] (juce::dsp::LinkwitzRileyFilter<float>& filter, juce::AudioBuffer<float>& target)
    {
        juce::dsp::AudioBlock<float> block (target);
        juce::dsp::ProcessContextReplacing<float> context (block);
        filter.process (context);
    };

    remainingBuffer.makeCopyOf (section, true);

    for (int split = 0; split < numSplits; ++split)
    {
        auto& lowBand = bandBuffers[static_cast<size_t> (split)];
        lowBand.makeCopyOf (remainingBuffer, true);
        process (lowPassFilters[static_cast<size_t> (split)], lowBand);
        process (highPassFilters[static_cast<size_t> (split)], remainingBuffer);
    }

    bandBuffers[static_cast<size_t> (numSplits)].makeCopyOf (remainingBuffer, true);

    // Each band below a split must pass through that split's allpass, or the bands
    // only sum flat when the crossovers are far apart.
    for (int band = 0; band < numSplits - 1; ++band)
        for (int crossover = band + 1; crossover < numSplits; ++crossover)
            process (allPassFilters[static_cast<size_t> (band)][static_cast<size_t> (crossover)],
                     bandBuffers[static_cast<size_t> (band)]);
}

void StagMotionBandsAudioProcessor::applyBandEffect (int bandIndex, int numSamples, double bpm)
{
    const auto& pointers = bandParameters[static_cast<size_t> (bandIndex)];
    const auto effectType = appliedEffects[static_cast<size_t> (bandIndex)];
    const auto tempoSync = pointers.sync->load() >= 0.5f;
    const auto division = readChoice (pointers.division);

    BandModulationEngine::Settings settings;
    settings.effectType = effectType;
    settings.waveform = readChoice (pointers.waveform);
    settings.modulationSource = readChoice (pointers.modulationSource);
    settings.rateHz = tempoSync ? BandModulationEngine::rateForDivision (bpm, division)
                                : pointers.rate->load();
    settings.depth = pointers.depth->load();
    settings.feedback = pointers.feedback->load();
    settings.mix = pointers.mix->load();
    settings.stereoSpread = pointers.stereoSpread->load();
    settings.attackMs = pointers.attack->load();
    settings.releaseMs = pointers.release->load();
    const auto usesCarrier = effectType == BandModulationEngine::ringMod
                          || effectType == BandModulationEngine::frequencyShifter;
    settings.carrierHz = tempoSync && usesCarrier ? BandModulationEngine::rateForDivision (bpm, division)
                                                  : pointers.carrierFrequency->load();

    auto& engine = modulationEngines[static_cast<size_t> (bandIndex)];
    engine.process (bandBuffers[static_cast<size_t> (bandIndex)], numSamples, settings);
    modulationActivity[static_cast<size_t> (bandIndex)].store (engine.getActivity(), std::memory_order_relaxed);
}

bool StagMotionBandsAudioProcessor::anyBandSoloed() const noexcept
{
    for (int band = 0; band < appliedBandCount; ++band)
        if (bandParameters[static_cast<size_t> (band)].solo->load() >= 0.5f)
            return true;

    return false;
}

bool StagMotionBandsAudioProcessor::bandIsAudible (int bandIndex, bool anySolo) const noexcept
{
    const auto& pointers = bandParameters[static_cast<size_t> (bandIndex)];
    return pointers.mute->load() < 0.5f && (! anySolo || pointers.solo->load() >= 0.5f);
}

int StagMotionBandsAudioProcessor::readChoice (const std::atomic<float>* value) const noexcept
{
    return static_cast<int> (std::lround (value->load()));
}

void StagMotionBandsAudioProcessor::pushAnalyzerSamples (const juce::AudioBuffer<float>& buffer)
{
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0)
        return;

    auto scope = analyzerFifo.write (numSamples);
    int sourceSample = 0;

    const std::array<std::pair<int, int>, 2> ranges {
        std::pair { scope.startIndex1, scope.blockSize1 },
        std::pair { scope.startIndex2, scope.blockSize2 }
    };

    for (const auto& [start, size] : ranges)
    {
        for (int index = 0; index < size; ++index, ++sourceSample)
        {
            float mono = 0.0f;
            for (int channel = 0; channel < numChannels; ++channel)
                mono += buffer.getSample (channel, sourceSample);

            analyzerRing[static_cast<size_t> (start + index)] = mono / static_cast<float> (numChannels);
        }
    }
}

int StagMotionBandsAudioProcessor::getAnalyzerSamplesAvailable() const noexcept
{
    return analyzerFifo.getNumReady();
}

int StagMotionBandsAudioProcessor::popAnalyzerSamples (float* destination, int maxSamples)
{
    const auto amount = juce::jmin (maxSamples, analyzerFifo.getNumReady());
    if (amount <= 0)
        return 0;

    auto scope = analyzerFifo.read (amount);
    int destinationSample = 0;

    const std::array<std::pair<int, int>, 2> ranges {
        std::pair { scope.startIndex1, scope.blockSize1 },
        std::pair { scope.startIndex2, scope.blockSize2 }
    };

    for (const auto& [start, size] : ranges)
    {
        std::copy_n (analyzerRing.data() + start, size, destination + destinationSample);
        destinationSample += size;
    }

    return amount;
}

float StagMotionBandsAudioProcessor::getModulationActivity (int bandIndex) const noexcept
{
    if (bandIndex < 0 || bandIndex >= maxBands)
        return 0.0f;

    return modulationActivity[static_cast<size_t> (bandIndex)].load (std::memory_order_relaxed);
}

int StagMotionBandsAudioProcessor::getBandCount() const noexcept
{
    return juce::jlimit (1, maxBands, static_cast<int> (std::lround (bandCountParameter->load())));
}

std::array<float, StagMotionBandsAudioProcessor::maxCrossovers>
StagMotionBandsAudioProcessor::getSortedCrossovers() const
{
    std::array<float, maxCrossovers> frequencies {};

    for (int index = 0; index < maxCrossovers; ++index)
        frequencies[static_cast<size_t> (index)] = clampFrequency (crossoverParameters[static_cast<size_t> (index)]->load());

    std::sort (frequencies.begin(), frequencies.begin() + juce::jmax (0, getBandCount() - 1));
    return frequencies;
}

void StagMotionBandsAudioProcessor::addCrossover (float frequencyHz)
{
    const auto oldBandCount = getBandCount();
    if (oldBandCount >= maxBands)
        return;

    const auto frequency = clampFrequency (frequencyHz);
    const auto sorted = getSortedCrossovers();
    const auto activeEnd = sorted.begin() + (oldBandCount - 1);
    const auto splitBand = static_cast<int> (std::lower_bound (sorted.begin(), activeEnd, frequency) - sorted.begin());

    // Bands above the split move up one slot; the new upper half copies the split band.
    for (int band = oldBandCount; band > splitBand; --band)
        copyBandSettings (band - 1, band);

    std::vector<float> active (sorted.begin(), activeEnd);
    active.insert (active.begin() + splitBand, frequency);
    for (int index = 0; index < static_cast<int> (active.size()); ++index)
        setCrossoverWithGesture (index, active[static_cast<size_t> (index)]);

    setBandCountWithGesture (oldBandCount + 1);
}

void StagMotionBandsAudioProcessor::removeCrossover (int crossoverIndex)
{
    removeBandAndCrossover (crossoverIndex + 1, crossoverIndex);
}

void StagMotionBandsAudioProcessor::removeBand (int bandIndex)
{
    removeBandAndCrossover (bandIndex, bandIndex == 0 ? 0 : bandIndex - 1);
}

void StagMotionBandsAudioProcessor::removeBandAndCrossover (int droppedBand, int droppedCrossover)
{
    const auto oldBandCount = getBandCount();
    if (oldBandCount <= 1
        || droppedBand < 0 || droppedBand >= oldBandCount
        || droppedCrossover < 0 || droppedCrossover >= oldBandCount - 1)
        return;

    const auto sorted = getSortedCrossovers();

    for (int band = droppedBand; band < oldBandCount - 1; ++band)
        copyBandSettings (band + 1, band);

    // Rewrite every remaining crossover in sorted order: the stored parameters may be
    // unsorted (host automation), and slots are matched to bands by sorted position.
    for (int index = 0; index < oldBandCount - 2; ++index)
        setCrossoverWithGesture (index, sorted[static_cast<size_t> (index < droppedCrossover ? index : index + 1)]);

    setBandCountWithGesture (oldBandCount - 1);
}

void StagMotionBandsAudioProcessor::setCrossover (int crossoverIndex, float frequencyHz)
{
    if (crossoverIndex < 0 || crossoverIndex >= maxCrossovers)
        return;

    if (auto* parameter = parameters.getParameter (crossoverParameterId (crossoverIndex)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (clampFrequency (frequencyHz)));
}

void StagMotionBandsAudioProcessor::setParameterWithGesture (const juce::String& parameterId, float normalisedValue)
{
    auto* parameter = parameters.getParameter (parameterId);
    if (parameter == nullptr || juce::exactlyEqual (parameter->getValue(), normalisedValue))
        return;

    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (normalisedValue);
    parameter->endChangeGesture();
}

void StagMotionBandsAudioProcessor::setCrossoverWithGesture (int crossoverIndex, float frequencyHz)
{
    const auto parameterId = crossoverParameterId (crossoverIndex);
    if (auto* parameter = parameters.getParameter (parameterId))
        setParameterWithGesture (parameterId, parameter->convertTo0to1 (clampFrequency (frequencyHz)));
}

void StagMotionBandsAudioProcessor::setBandCountWithGesture (int bandCount)
{
    if (auto* parameter = parameters.getParameter ("bandCount"))
        setParameterWithGesture ("bandCount", parameter->convertTo0to1 (static_cast<float> (bandCount)));
}

void StagMotionBandsAudioProcessor::copyBandSettings (int sourceBand, int destinationBand)
{
    const auto sourceIds = bandParameterIds (sourceBand);
    const auto destinationIds = bandParameterIds (destinationBand);

    for (int index = 0; index < sourceIds.size(); ++index)
        if (auto* source = parameters.getParameter (sourceIds[index]))
            setParameterWithGesture (destinationIds[index], source->getValue());
}

juce::StringArray StagMotionBandsAudioProcessor::bandParameterIds (int bandIndex)
{
    return {
        muteParameterId (bandIndex),
        soloParameterId (bandIndex),
        effectTypeParameterId (bandIndex),
        rateParameterId (bandIndex),
        depthParameterId (bandIndex),
        feedbackParameterId (bandIndex),
        mixParameterId (bandIndex),
        syncParameterId (bandIndex),
        divisionParameterId (bandIndex),
        waveformParameterId (bandIndex),
        stereoSpreadParameterId (bandIndex),
        modulationSourceParameterId (bandIndex),
        attackParameterId (bandIndex),
        releaseParameterId (bandIndex),
        retriggerParameterId (bandIndex),
        carrierFrequencyParameterId (bandIndex)
    };
}

juce::String StagMotionBandsAudioProcessor::crossoverParameterId (int index)
{
    return "cross" + juce::String (index + 1);
}

juce::String StagMotionBandsAudioProcessor::muteParameterId (int bandIndex)
{
    return "mute" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::soloParameterId (int bandIndex)
{
    return "solo" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::effectTypeParameterId (int bandIndex)
{
    return "effect" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::rateParameterId (int bandIndex)
{
    return "rate" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::depthParameterId (int bandIndex)
{
    return "depth" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::feedbackParameterId (int bandIndex)
{
    return "feedback" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::mixParameterId (int bandIndex)
{
    return "mix" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::syncParameterId (int bandIndex)
{
    return "sync" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::divisionParameterId (int bandIndex)
{
    return "division" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::waveformParameterId (int bandIndex)
{
    return "waveform" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::stereoSpreadParameterId (int bandIndex)
{
    return "spread" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::modulationSourceParameterId (int bandIndex)
{
    return "source" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::attackParameterId (int bandIndex)
{
    return "attack" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::releaseParameterId (int bandIndex)
{
    return "release" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::retriggerParameterId (int bandIndex)
{
    return "retrigger" + juce::String (bandIndex + 1);
}

juce::String StagMotionBandsAudioProcessor::carrierFrequencyParameterId (int bandIndex)
{
    return "carrier" + juce::String (bandIndex + 1);
}

void StagMotionBandsAudioProcessor::getStateInformation (juce::MemoryBlock& destinationData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destinationData);
}

void StagMotionBandsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* StagMotionBandsAudioProcessor::createEditor()
{
    return new StagMotionBandsAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StagMotionBandsAudioProcessor();
}
