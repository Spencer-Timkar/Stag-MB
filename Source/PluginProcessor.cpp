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

void StagMotionBandsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedBlockSize = juce::jmax (1, samplesPerBlock);
    preparedChannels = juce::jmax (1, getTotalNumOutputChannels());

    const juce::dsp::ProcessSpec spec {
        sampleRate,
        static_cast<juce::uint32> (preparedBlockSize),
        static_cast<juce::uint32> (preparedChannels)
    };

    for (int index = 0; index < maxCrossovers; ++index)
    {
        lowPassFilters[static_cast<size_t> (index)].prepare (spec);
        highPassFilters[static_cast<size_t> (index)].prepare (spec);
        lowPassFilters[static_cast<size_t> (index)].reset();
        highPassFilters[static_cast<size_t> (index)].reset();
    }

    for (auto& engine : modulationEngines)
        engine.prepare (spec);

    remainingBuffer.setSize (preparedChannels, preparedBlockSize);
    for (auto& bandBuffer : bandBuffers)
        bandBuffer.setSize (preparedChannels, preparedBlockSize);

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

    const auto bandCount = getBandCount();
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples > preparedBlockSize || numChannels > preparedChannels)
    {
        jassertfalse;
        return;
    }

    bool hasNoteOn = false;
    for (const auto metadata : midiMessages)
        hasNoteOn = hasNoteOn || metadata.getMessage().isNoteOn();

    if (hasNoteOn)
        for (int band = 0; band < bandCount; ++band)
            if (parameters.getRawParameterValue (retriggerParameterId (band))->load() >= 0.5f)
                modulationEngines[static_cast<size_t> (band)].trigger();

    if (bandCount == 1)
    {
        bandBuffers[0].makeCopyOf (buffer, true);
    }
    else
    {
        const auto crossovers = getSortedCrossovers();
        updateFilterCutoffs (crossovers, bandCount - 1);

        remainingBuffer.makeCopyOf (buffer, true);

        for (int split = 0; split < bandCount - 1; ++split)
        {
            auto& lowBand = bandBuffers[static_cast<size_t> (split)];
            lowBand.makeCopyOf (remainingBuffer, true);

            juce::dsp::AudioBlock<float> lowBlock (lowBand);
            juce::dsp::AudioBlock<float> highBlock (remainingBuffer);
            lowBlock = lowBlock.getSubBlock (0, static_cast<size_t> (numSamples));
            highBlock = highBlock.getSubBlock (0, static_cast<size_t> (numSamples));

            juce::dsp::ProcessContextReplacing<float> lowContext (lowBlock);
            juce::dsp::ProcessContextReplacing<float> highContext (highBlock);
            lowPassFilters[static_cast<size_t> (split)].process (lowContext);
            highPassFilters[static_cast<size_t> (split)].process (highContext);
        }

        bandBuffers[static_cast<size_t> (bandCount - 1)].makeCopyOf (remainingBuffer, true);
    }

    auto bpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            bpm = position->getBpm().orFallback (120.0);

    for (int band = 0; band < bandCount; ++band)
        applyBandEffect (band, numSamples, bpm);

    bool anySolo = false;
    for (int band = 0; band < bandCount; ++band)
        anySolo = anySolo || parameters.getRawParameterValue (soloParameterId (band))->load() >= 0.5f;

    buffer.clear();

    for (int band = 0; band < bandCount; ++band)
    {
        const auto muted = parameters.getRawParameterValue (muteParameterId (band))->load() >= 0.5f;
        const auto soloed = parameters.getRawParameterValue (soloParameterId (band))->load() >= 0.5f;
        const auto audible = ! muted && (! anySolo || soloed);

        if (! audible)
            continue;

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.addFrom (channel, 0, bandBuffers[static_cast<size_t> (band)], channel, 0, numSamples);
    }
}

void StagMotionBandsAudioProcessor::applyBandEffect (int bandIndex, int numSamples, double bpm)
{
    const auto effectType = static_cast<int> (std::lround (
        parameters.getRawParameterValue (effectTypeParameterId (bandIndex))->load()));

    const auto tempoSync = parameters.getRawParameterValue (syncParameterId (bandIndex))->load() >= 0.5f;
    const auto division = static_cast<int> (std::lround (
        parameters.getRawParameterValue (divisionParameterId (bandIndex))->load()));

    BandModulationEngine::Settings settings;
    settings.effectType = effectType;
    settings.waveform = static_cast<int> (std::lround (
        parameters.getRawParameterValue (waveformParameterId (bandIndex))->load()));
    settings.modulationSource = static_cast<int> (std::lround (
        parameters.getRawParameterValue (modulationSourceParameterId (bandIndex))->load()));
    settings.rateHz = tempoSync
                          ? BandModulationEngine::rateForDivision (bpm, division)
                          : parameters.getRawParameterValue (rateParameterId (bandIndex))->load();
    settings.depth = parameters.getRawParameterValue (depthParameterId (bandIndex))->load();
    settings.feedback = parameters.getRawParameterValue (feedbackParameterId (bandIndex))->load();
    settings.mix = parameters.getRawParameterValue (mixParameterId (bandIndex))->load();
    settings.stereoSpread = parameters.getRawParameterValue (stereoSpreadParameterId (bandIndex))->load();
    settings.attackMs = parameters.getRawParameterValue (attackParameterId (bandIndex))->load();
    settings.releaseMs = parameters.getRawParameterValue (releaseParameterId (bandIndex))->load();
    const auto usesCarrier = effectType == BandModulationEngine::ringMod
                          || effectType == BandModulationEngine::frequencyShifter;
    settings.carrierHz = tempoSync && usesCarrier
                             ? BandModulationEngine::rateForDivision (bpm, division)
                             : parameters.getRawParameterValue (
                                   carrierFrequencyParameterId (bandIndex))->load();

    auto& engine = modulationEngines[static_cast<size_t> (bandIndex)];
    engine.process (bandBuffers[static_cast<size_t> (bandIndex)], numSamples, settings);
    modulationActivity[static_cast<size_t> (bandIndex)].store (
        engine.getActivity(), std::memory_order_relaxed);
}

void StagMotionBandsAudioProcessor::updateFilterCutoffs (
    const std::array<float, maxCrossovers>& frequencies,
    int numCrossovers)
{
    for (int index = 0; index < numCrossovers; ++index)
    {
        const auto frequency = frequencies[static_cast<size_t> (index)];
        lowPassFilters[static_cast<size_t> (index)].setCutoffFrequency (frequency);
        highPassFilters[static_cast<size_t> (index)].setCutoffFrequency (frequency);
    }
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
    return juce::jlimit (1, maxBands,
                         static_cast<int> (std::lround (parameters.getRawParameterValue ("bandCount")->load())));
}

std::array<float, StagMotionBandsAudioProcessor::maxCrossovers>
StagMotionBandsAudioProcessor::getSortedCrossovers() const
{
    std::array<float, maxCrossovers> frequencies {};

    for (int index = 0; index < maxCrossovers; ++index)
        frequencies[static_cast<size_t> (index)] =
            clampFrequency (parameters.getRawParameterValue (crossoverParameterId (index))->load());

    std::sort (frequencies.begin(), frequencies.begin() + juce::jmax (0, getBandCount() - 1));
    return frequencies;
}

void StagMotionBandsAudioProcessor::addCrossover (float frequencyHz)
{
    const auto oldBandCount = getBandCount();
    if (oldBandCount >= maxBands)
        return;

    std::vector<float> active;
    const auto sorted = getSortedCrossovers();
    active.assign (sorted.begin(), sorted.begin() + oldBandCount - 1);
    active.push_back (clampFrequency (frequencyHz));
    std::sort (active.begin(), active.end());

    for (int index = 0; index < static_cast<int> (active.size()); ++index)
        setCrossover (index, active[static_cast<size_t> (index)]);

    if (auto* parameter = parameters.getParameter ("bandCount"))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (oldBandCount + 1)));
        parameter->endChangeGesture();
    }
}

void StagMotionBandsAudioProcessor::removeCrossover (int crossoverIndex)
{
    const auto oldBandCount = getBandCount();
    if (oldBandCount <= 1 || crossoverIndex < 0 || crossoverIndex >= oldBandCount - 1)
        return;

    auto active = getSortedCrossovers();
    for (int index = crossoverIndex; index < oldBandCount - 2; ++index)
        setCrossover (index, active[static_cast<size_t> (index + 1)]);

    if (auto* parameter = parameters.getParameter ("bandCount"))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (static_cast<float> (oldBandCount - 1)));
        parameter->endChangeGesture();
    }
}

void StagMotionBandsAudioProcessor::removeBand (int bandIndex)
{
    const auto bandCount = getBandCount();
    if (bandCount <= 1 || bandIndex < 0 || bandIndex >= bandCount)
        return;

    // The first band merges right; every other band merges into its left neighbour.
    removeCrossover (bandIndex == 0 ? 0 : bandIndex - 1);
}

void StagMotionBandsAudioProcessor::setCrossover (int crossoverIndex, float frequencyHz)
{
    if (crossoverIndex < 0 || crossoverIndex >= maxCrossovers)
        return;

    if (auto* parameter = parameters.getParameter (crossoverParameterId (crossoverIndex)))
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (clampFrequency (frequencyHz)));
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
