#include "PluginProcessor.h"

#include <cmath>
#include <functional>
#include <iostream>

namespace
{
bool setParameter (StagMotionBandsAudioProcessor& processor,
                   const juce::String& parameterId,
                   float actualValue)
{
    auto* parameter = processor.getParameters().getParameter (parameterId);
    if (parameter == nullptr)
        return false;

    parameter->setValueNotifyingHost (parameter->convertTo0to1 (actualValue));
    return true;
}

bool bufferIsFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite (buffer.getSample (channel, sample)))
                return false;

    return true;
}

double absoluteDifference (const juce::AudioBuffer<float>& a,
                           const juce::AudioBuffer<float>& b)
{
    double result = 0.0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int sample = 0; sample < a.getNumSamples(); ++sample)
            result += std::abs (static_cast<double> (a.getSample (channel, sample)
                                                   - b.getSample (channel, sample)));
    return result;
}

bool runLicenseGateTest()
{
    constexpr int blockLength = 1103; // Larger than both the announced block and the license fade.
    for (const auto channels : { 1, 2 })
    {
        std::atomic<bool> licensed { false };
        StagMotionBandsAudioProcessor processor (licensed);
        const auto channelSet = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        auto layout = processor.getBusesLayout();
        layout.inputBuses.set (0, channelSet);
        layout.outputBuses.set (0, channelSet);
        if (! processor.setBusesLayout (layout))
            return false;

        processor.prepareToPlay (44100.0, 17);

        // A DAW can restore a preset while dry. Its mute should only take effect
        // after activation, when the processor starts from the restored settings.
        const auto mute = StagMotionBandsAudioProcessor::muteParameterId (0);
        if (! setParameter (processor, mute, 1.0f))
            return false;
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        setParameter (processor, mute, 0.0f);
        processor.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

        juce::AudioBuffer<float> input (channels, blockLength);
        for (int channel = 0; channel < channels; ++channel)
            for (int sample = 0; sample < blockLength; ++sample)
                input.setSample (channel, sample, (channel == 0 ? 0.125f : -0.09375f)
                                                  * static_cast<float> (1 + sample % 5));

        juce::AudioBuffer<float> output (input);
        juce::MidiBuffer midi;
        processor.processBlock (output, midi);
        if (absoluteDifference (output, input) != 0.0)
            return false;

        std::array<float, blockLength> analyzer {};
        if (processor.popAnalyzerSamples (analyzer.data(), blockLength) != blockLength)
            return false;
        for (int sample = 0; sample < blockLength; ++sample)
        {
            const auto expected = channels == 1 ? input.getSample (0, sample)
                                                  : (input.getSample (0, sample) + input.getSample (1, sample)) * 0.5f;
            if (analyzer[static_cast<size_t> (sample)] != expected)
                return false;
        }

        const auto fadeIsSmooth = [&] (bool wet)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                auto previousGain = wet ? 1.0f : 0.0f;
                for (int sample = 0; sample < blockLength; ++sample)
                {
                    const auto dry = input.getSample (channel, sample);
                    const auto value = output.getSample (channel, sample);
                    const auto gain = value / dry; // The fully wet output is muted.
                    if (! std::isfinite (gain) || std::abs (gain - previousGain) > 0.003f)
                        return false;
                    if (sample >= 1000 && value != (wet ? 0.0f : dry))
                        return false;
                    previousGain = gain;
                }
            }
            return true;
        };

        const auto renderTransition = [&]
        {
            int offset = 0;
            for (const auto requested : { 17, 31, 511, blockLength })
            {
                const auto length = juce::jmin (requested, blockLength - offset);
                juce::AudioBuffer<float> block (output.getArrayOfWritePointers(), channels, offset, length);
                processor.processBlock (block, midi);
                offset += length;
            }
        };

        licensed.store (true);
        renderTransition();
        if (! fadeIsSmooth (true))
            return false;

        output.makeCopyOf (input, true);
        licensed.store (false);
        renderTransition();
        if (! fadeIsSmooth (false))
            return false;

        output.makeCopyOf (input, true);
        processor.processBlock (output, midi);
        if (absoluteDifference (output, input) != 0.0)
            return false;
    }
    return true;
}

bool runEffectSmoke (int effectType, int waveform,
                     int modulationSource = BandModulationEngine::lfo)
{
    StagMotionBandsAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    if (! setParameter (processor, StagMotionBandsAudioProcessor::effectTypeParameterId (0),
                        static_cast<float> (effectType))
        || ! setParameter (processor, StagMotionBandsAudioProcessor::waveformParameterId (0),
                           static_cast<float> (waveform))
        || ! setParameter (processor, StagMotionBandsAudioProcessor::modulationSourceParameterId (0),
                           static_cast<float> (modulationSource))
        || ! setParameter (processor, StagMotionBandsAudioProcessor::rateParameterId (0), 1.7f)
        || ! setParameter (processor, StagMotionBandsAudioProcessor::depthParameterId (0), 0.8f)
        || ! setParameter (processor, StagMotionBandsAudioProcessor::feedbackParameterId (0), 0.25f)
        || ! setParameter (processor, StagMotionBandsAudioProcessor::mixParameterId (0), 0.75f)
        || ! setParameter (processor, StagMotionBandsAudioProcessor::stereoSpreadParameterId (0), 1.0f))
        return false;

    if (! setParameter (processor,
                        StagMotionBandsAudioProcessor::carrierFrequencyParameterId (0), 83.0f))
        return false;

    juce::AudioBuffer<float> buffer (2, 512);
    juce::AudioBuffer<float> dry (2, 512);
    juce::MidiBuffer midi;
    double phase = 0.0;
    double accumulatedDifference = 0.0;

    for (int block = 0; block < 40; ++block)
    {
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto value = static_cast<float> (0.2 * std::sin (phase));
            phase += juce::MathConstants<double>::twoPi * 440.0 / 48000.0;
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }

        dry.makeCopyOf (buffer);
        processor.processBlock (buffer, midi);

        if (! bufferIsFinite (buffer))
            return false;

        accumulatedDifference += absoluteDifference (buffer, dry);
    }

    return accumulatedDifference > 1.0;
}

bool runStateAndMuteSmoke()
{
    StagMotionBandsAudioProcessor source;
    setParameter (source, StagMotionBandsAudioProcessor::effectTypeParameterId (0), 2.0f);
    setParameter (source, StagMotionBandsAudioProcessor::syncParameterId (0), 1.0f);
    setParameter (source, StagMotionBandsAudioProcessor::divisionParameterId (0), 1.0f);
    setParameter (source, StagMotionBandsAudioProcessor::waveformParameterId (0), 3.0f);
    setParameter (source, StagMotionBandsAudioProcessor::stereoSpreadParameterId (0), 0.75f);
    setParameter (source, StagMotionBandsAudioProcessor::modulationSourceParameterId (0), 1.0f);
    setParameter (source, StagMotionBandsAudioProcessor::attackParameterId (0), 12.0f);
    setParameter (source, StagMotionBandsAudioProcessor::releaseParameterId (0), 540.0f);
    setParameter (source, StagMotionBandsAudioProcessor::retriggerParameterId (0), 1.0f);
    setParameter (source, StagMotionBandsAudioProcessor::carrierFrequencyParameterId (0), 233.0f);

    juce::MemoryBlock state;
    source.getStateInformation (state);

    StagMotionBandsAudioProcessor restored;
    restored.setStateInformation (state.getData(), static_cast<int> (state.getSize()));

    const auto restoredEffect = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::effectTypeParameterId (0))->load();
    const auto restoredSync = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::syncParameterId (0))->load();
    const auto restoredWaveform = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::waveformParameterId (0))->load();
    const auto restoredSpread = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::stereoSpreadParameterId (0))->load();
    const auto restoredSource = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::modulationSourceParameterId (0))->load();
    const auto restoredAttack = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::attackParameterId (0))->load();
    const auto restoredRelease = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::releaseParameterId (0))->load();
    const auto restoredRetrigger = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::retriggerParameterId (0))->load();
    const auto restoredCarrier = restored.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::carrierFrequencyParameterId (0))->load();

    if (std::abs (restoredEffect - 2.0f) > 0.01f
        || restoredSync < 0.5f
        || std::abs (restoredWaveform - 3.0f) > 0.01f
        || std::abs (restoredSpread - 0.75f) > 0.01f
        || std::abs (restoredSource - 1.0f) > 0.01f
        || std::abs (restoredAttack - 12.0f) > 0.11f
        || std::abs (restoredRelease - 540.0f) > 0.11f
        || restoredRetrigger < 0.5f
        || std::abs (restoredCarrier - 233.0f) > 0.11f)
        return false;

    restored.prepareToPlay (48000.0, 128);
    setParameter (restored, StagMotionBandsAudioProcessor::muteParameterId (0), 1.0f);

    juce::AudioBuffer<float> buffer (2, 128);
    juce::MidiBuffer midi;

    // Mute ramps out over 10 ms; the block after the ramp must be silent.
    for (int block = 0; block < 8; ++block)
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                buffer.setSample (channel, sample, 0.25f);

        restored.processBlock (buffer, midi);
    }

    return buffer.getMagnitude (0, buffer.getNumSamples()) <= 1.0e-7f;
}

bool runEnvelopeActivitySmoke()
{
    StagMotionBandsAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    setParameter (processor, StagMotionBandsAudioProcessor::modulationSourceParameterId (0), 1.0f);
    setParameter (processor, StagMotionBandsAudioProcessor::attackParameterId (0), 1.0f);
    setParameter (processor, StagMotionBandsAudioProcessor::releaseParameterId (0), 200.0f);
    setParameter (processor, StagMotionBandsAudioProcessor::retriggerParameterId (0), 1.0f);

    juce::AudioBuffer<float> buffer (2, 512);
    buffer.clear();
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample (channel, sample, 0.06f); // -24 dBFS: inside the default -36..-18 dB window

    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 0);
    processor.processBlock (buffer, midi);
    const auto loudActivity = processor.getModulationActivity (0);

    buffer.clear();
    midi.clear();
    processor.processBlock (buffer, midi);
    const auto releaseActivity = processor.getModulationActivity (0);

    return processor.acceptsMidi()
        && loudActivity > 0.5f
        && releaseActivity > 0.0f
        && releaseActivity < loudActivity;
}

bool runBandLimitSmoke()
{
    StagMotionBandsAudioProcessor processor;
    for (const auto frequency : { 90.0f, 350.0f, 1200.0f, 4200.0f, 9000.0f })
        processor.addCrossover (frequency);

    if (processor.getBandCount() != 4)
        return false;

    processor.removeBand (2);
    return processor.getBandCount() == 3;
}

int effectOf (StagMotionBandsAudioProcessor& processor, int band)
{
    return static_cast<int> (std::lround (processor.getParameters().getRawParameterValue (
        StagMotionBandsAudioProcessor::effectTypeParameterId (band))->load()));
}

bool effectsAre (StagMotionBandsAudioProcessor& processor, std::initializer_list<int> expected)
{
    if (processor.getBandCount() != static_cast<int> (expected.size()))
        return false;

    int band = 0;
    for (const auto effect : expected)
        if (effectOf (processor, band++) != effect)
            return false;

    return true;
}

bool runBandSettingsFollowBandsTest()
{
    using Engine = BandModulationEngine;

    {
        StagMotionBandsAudioProcessor processor;
        setParameter (processor, "bandCount", 4.0f);
        const int effects[] = { Engine::chorus, Engine::flanger, Engine::phaser, Engine::tremolo };
        for (int band = 0; band < 4; ++band)
            setParameter (processor, StagMotionBandsAudioProcessor::effectTypeParameterId (band),
                          static_cast<float> (effects[band]));
        setParameter (processor, StagMotionBandsAudioProcessor::rateParameterId (3), 3.3f);

        processor.removeBand (1);
        const auto movedRate = processor.getParameters().getRawParameterValue (
            StagMotionBandsAudioProcessor::rateParameterId (2))->load();
        if (! effectsAre (processor, { Engine::chorus, Engine::phaser, Engine::tremolo })
            || std::abs (movedRate - 3.3f) > 0.01f)
            return false;

        const auto crossovers = processor.getSortedCrossovers();
        if (std::abs (crossovers[0] - 1200.0f) > 1.0f || std::abs (crossovers[1] - 6000.0f) > 1.0f)
            return false;

        processor.removeBand (0);
        if (! effectsAre (processor, { Engine::phaser, Engine::tremolo }))
            return false;

        processor.removeCrossover (0);
        if (! effectsAre (processor, { Engine::phaser }))
            return false;
    }

    {
        StagMotionBandsAudioProcessor processor;
        setParameter (processor, StagMotionBandsAudioProcessor::effectTypeParameterId (0), Engine::tremolo);
        setParameter (processor, StagMotionBandsAudioProcessor::effectTypeParameterId (1), Engine::ringMod);

        processor.addCrossover (5000.0f);
        if (! effectsAre (processor, { Engine::tremolo, Engine::tremolo }))
            return false;

        setParameter (processor, StagMotionBandsAudioProcessor::effectTypeParameterId (1), Engine::flanger);
        processor.addCrossover (100.0f);
        if (! effectsAre (processor, { Engine::tremolo, Engine::tremolo, Engine::flanger }))
            return false;

        processor.addCrossover (1000.0f);
        return effectsAre (processor, { Engine::tremolo, Engine::tremolo, Engine::tremolo, Engine::flanger });
    }
}

// With every effect Off the bands must sum back to the input, however close the crossovers.
bool runCrossoverSumIsFlatTest()
{
    constexpr double sampleRate = 48000.0;
    const std::array<std::array<float, 3>, 2> layouts { { { 180.0f, 1200.0f, 6000.0f }, { 400.0f, 440.0f, 480.0f } } };

    for (const auto& crossovers : layouts)
    {
        for (double frequency = 40.0; frequency < 18000.0; frequency *= 1.13)
        {
            StagMotionBandsAudioProcessor processor;
            for (int index = 0; index < 3; ++index)
                setParameter (processor, StagMotionBandsAudioProcessor::crossoverParameterId (index),
                              crossovers[static_cast<size_t> (index)]);
            setParameter (processor, "bandCount", 4.0f);
            processor.prepareToPlay (sampleRate, 512);

            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;
            double inputEnergy = 0.0;
            double outputEnergy = 0.0;
            long position = 0;

            // ~2 s of steady state, so partial periods at the window edges stay negligible.
            for (int block = 0; block < 230; ++block)
            {
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample, ++position)
                {
                    const auto value = static_cast<float> (
                        0.5 * std::sin (juce::MathConstants<double>::twoPi * frequency * static_cast<double> (position) / sampleRate));
                    buffer.setSample (0, sample, value);
                    buffer.setSample (1, sample, value);
                    if (block >= 30)
                        inputEnergy += value * value;
                }

                processor.processBlock (buffer, midi);

                if (block >= 30)
                    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                        outputEnergy += buffer.getSample (0, sample) * buffer.getSample (0, sample);
            }

            const auto gainDb = 10.0 * std::log10 (outputEnergy / inputEnergy);
            if (std::abs (gainDb) > 0.1)
            {
                std::cerr << "  sum is " << gainDb << " dB at " << frequency << " Hz with crossovers "
                          << crossovers[0] << "/" << crossovers[1] << "/" << crossovers[2] << '\n';
                return false;
            }
        }
    }

    return true;
}

// Hosts may send blocks larger than prepareToPlay announced (offline renders).
bool runOversizedBlockIsProcessedTest()
{
    StagMotionBandsAudioProcessor processor;
    setParameter (processor, StagMotionBandsAudioProcessor::effectTypeParameterId (0), BandModulationEngine::tremolo);
    setParameter (processor, StagMotionBandsAudioProcessor::depthParameterId (0), 1.0f);
    setParameter (processor, StagMotionBandsAudioProcessor::mixParameterId (0), 1.0f);
    processor.prepareToPlay (48000.0, 64);

    juce::AudioBuffer<float> buffer (2, 4096);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample (channel, sample, 0.5f);

    juce::AudioBuffer<float> dry;
    dry.makeCopyOf (buffer);
    juce::MidiBuffer midi;
    processor.processBlock (buffer, midi);

    return bufferIsFinite (buffer) && absoluteDifference (buffer, dry) > 1.0;
}

// Switching effects, muting, adding/removing bands and jumping a crossover must not
// produce a step much larger than the input's own sample-to-sample slope.
bool runEditsDoNotClickTest()
{
    using Processor = StagMotionBandsAudioProcessor;
    constexpr double sampleRate = 48000.0;
    constexpr double frequency = 110.0;
    constexpr double amplitude = 0.5;

    Processor processor;
    processor.addCrossover (600.0f);
    setParameter (processor, Processor::effectTypeParameterId (0), BandModulationEngine::chorus);
    setParameter (processor, Processor::mixParameterId (0), 1.0f);
    setParameter (processor, Processor::depthParameterId (0), 0.2f);
    setParameter (processor, Processor::rateParameterId (0), 0.2f);
    processor.prepareToPlay (sampleRate, 256);

    const std::vector<std::function<void()>> edits {
        [&] { setParameter (processor, Processor::effectTypeParameterId (0), BandModulationEngine::ringMod); },
        [&] { setParameter (processor, Processor::muteParameterId (0), 1.0f); },
        [&] { setParameter (processor, Processor::muteParameterId (0), 0.0f); },
        [&] { setParameter (processor, Processor::soloParameterId (1), 1.0f); },
        [&] { setParameter (processor, Processor::soloParameterId (1), 0.0f); },
        [&] { processor.setCrossover (0, 60.0f); },
        [&] { processor.addCrossover (80.0f); },
        [&] { processor.removeBand (0); },
        [&] { setParameter (processor, Processor::effectTypeParameterId (0), BandModulationEngine::off); },
    };

    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    long position = 0;
    float previous = 0.0f;
    float largestStep = 0.0f;
    const auto naturalStep = static_cast<float> (amplitude * juce::MathConstants<double>::twoPi * frequency / sampleRate);

    for (int block = 0; block < 40 * static_cast<int> (edits.size() + 1); ++block)
    {
        if (block % 40 == 0 && block > 0)
            edits[static_cast<size_t> (block / 40 - 1)]();

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample, ++position)
        {
            const auto value = static_cast<float> (
                amplitude * std::sin (juce::MathConstants<double>::twoPi * frequency * static_cast<double> (position) / sampleRate));
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }

        processor.processBlock (buffer, midi);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            const auto value = buffer.getSample (0, sample);
            if (block > 4)
                largestStep = juce::jmax (largestStep, std::abs (value - previous));
            previous = value;
        }
    }

    if (largestStep > naturalStep * 4.0f)
    {
        std::cerr << "  largest step " << largestStep << " vs natural " << naturalStep << '\n';
        return false;
    }

    return true;
}

double goertzelPower (const std::vector<float>& samples, double frequency, double sampleRate)
{
    const auto coefficient = 2.0 * std::cos (juce::MathConstants<double>::twoPi * frequency / sampleRate);
    double previous = 0.0;
    double beforePrevious = 0.0;

    for (const auto sample : samples)
    {
        const auto current = static_cast<double> (sample) + coefficient * previous - beforePrevious;
        beforePrevious = previous;
        previous = current;
    }

    return previous * previous + beforePrevious * beforePrevious - coefficient * previous * beforePrevious;
}

// A single-sideband shifter must move a tone up by the carrier and suppress the mirror
// image, in low bands as well as high ones.
bool runFrequencyShifterRejectsImageTest()
{
    using Processor = StagMotionBandsAudioProcessor;
    constexpr double sampleRate = 48000.0;
    const std::array<std::pair<double, float>, 3> cases { { { 100.0, 40.0f }, { 1000.0, 200.0f }, { 5000.0, 300.0f } } };

    for (const auto& [frequency, carrier] : cases)
    {
        Processor processor;
        setParameter (processor, Processor::effectTypeParameterId (0), BandModulationEngine::frequencyShifter);
        setParameter (processor, Processor::mixParameterId (0), 1.0f);
        setParameter (processor, Processor::stereoSpreadParameterId (0), 0.0f);
        setParameter (processor, Processor::carrierFrequencyParameterId (0), carrier);
        processor.setRateAndBufferSizeDetails (sampleRate, 480);
        processor.prepareToPlay (sampleRate, 480);

        juce::AudioBuffer<float> buffer (2, 480);
        juce::MidiBuffer midi;
        std::vector<float> output;
        long position = 0;

        for (int block = 0; block < 150; ++block)
        {
            for (int sample = 0; sample < buffer.getNumSamples(); ++sample, ++position)
            {
                const auto value = static_cast<float> (
                    0.5 * std::sin (juce::MathConstants<double>::twoPi * frequency * static_cast<double> (position) / sampleRate));
                buffer.setSample (0, sample, value);
                buffer.setSample (1, sample, value);
            }

            processor.processBlock (buffer, midi);

            if (block >= 50)
                for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
                    output.push_back (buffer.getSample (0, sample));
        }

        const auto upper = goertzelPower (output, frequency + carrier, sampleRate);
        const auto image = goertzelPower (output, std::abs (frequency - carrier), sampleRate);
        const auto rejectionDb = 10.0 * std::log10 (upper / image);
        if (rejectionDb < 30.0)
        {
            std::cerr << "  " << frequency << " Hz shifted by " << carrier << " Hz: image only "
                      << rejectionDb << " dB below the shifted tone\n";
            return false;
        }
    }

    return true;
}

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

// A synced LFO must sit at the host's bar position, not wherever it happened to start.
bool runSyncedLfoFollowsHostTest()
{
    using Processor = StagMotionBandsAudioProcessor;
    constexpr double sampleRate = 48000.0;
    constexpr double bpm = 120.0;
    constexpr int blockSize = 480;
    constexpr double startPpq = 0.37;

    Processor processor;
    setParameter (processor, Processor::effectTypeParameterId (0), BandModulationEngine::tremolo);
    setParameter (processor, Processor::syncParameterId (0), 1.0f);
    setParameter (processor, Processor::divisionParameterId (0), 3.0f); // 1/4: one cycle per beat
    setParameter (processor, Processor::depthParameterId (0), 1.0f);
    setParameter (processor, Processor::mixParameterId (0), 1.0f);
    setParameter (processor, Processor::stereoSpreadParameterId (0), 0.0f);

    TestPlayHead playHead;
    playHead.info.setBpm (bpm);
    playHead.info.setIsPlaying (true);
    processor.setPlayHead (&playHead);
    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto beatsPerSample = bpm / 60.0 / sampleRate;
    float largestError = 0.0f;

    for (int block = 0; block < 100; ++block)
    {
        const auto blockPpq = startPpq + beatsPerSample * blockSize * block;
        playHead.info.setPpqPosition (blockPpq);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                buffer.setSample (channel, sample, 1.0f);

        processor.processBlock (buffer, midi);

        if (block < 90)
            continue;

        for (int sample = 0; sample < blockSize; ++sample)
        {
            const auto beats = blockPpq + beatsPerSample * sample;
            const auto lfo = std::sin (juce::MathConstants<double>::twoPi * (beats - std::floor (beats)));
            const auto expected = static_cast<float> (1.0 - (lfo * 0.5 + 0.5));
            largestError = juce::jmax (largestError, std::abs (buffer.getSample (0, sample) - expected));
        }
    }

    processor.setPlayHead (nullptr);

    if (largestError > 0.01f)
    {
        std::cerr << "  synced tremolo is off the host position by up to " << largestError << '\n';
        return false;
    }

    return true;
}

using Signal = std::function<float (long)>;

// Activity of band 0 after every 32-sample block, so follower timing can be read at 0.7 ms resolution.
std::vector<float> activityTrace (StagMotionBandsAudioProcessor& processor, const Signal& signal, double seconds)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 32;
    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    std::vector<float> trace;
    long position = 0;

    for (int block = 0; block < static_cast<int> (seconds * sampleRate / blockSize); ++block)
    {
        for (int sample = 0; sample < blockSize; ++sample, ++position)
        {
            const auto value = signal (position);
            buffer.setSample (0, sample, value);
            buffer.setSample (1, sample, value);
        }

        processor.processBlock (buffer, midi);
        trace.push_back (processor.getModulationActivity (0));
    }

    return trace;
}

double traceSeconds (size_t index) { return static_cast<double> (index + 1) * 32.0 / 48000.0; }

void useFollower (StagMotionBandsAudioProcessor& processor, int source, float thresholdDb, float rangeDb,
                  float attackMs, float releaseMs)
{
    using Processor = StagMotionBandsAudioProcessor;
    setParameter (processor, Processor::modulationSourceParameterId (0), static_cast<float> (source));
    setParameter (processor, Processor::thresholdParameterId (0), thresholdDb);
    setParameter (processor, Processor::rangeParameterId (0), rangeDb);
    setParameter (processor, Processor::attackParameterId (0), attackMs);
    setParameter (processor, Processor::releaseParameterId (0), releaseMs);
}

float sineAt (long position, float gainDb, double frequency)
{
    return juce::Decibels::decibelsToGain (gainDb)
         * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * frequency * static_cast<double> (position) / 48000.0));
}

// Kick-like hits: a 55 Hz tone with a 3 ms rise and 120 ms decay, every 500 ms.
float kickAt (long position, float peakDb)
{
    const auto time = std::fmod (static_cast<double> (position) / 48000.0, 0.5);
    const auto envelope = std::min (1.0, time / 0.003) * std::exp (-time / 0.12);
    return static_cast<float> (envelope) * sineAt (position, peakDb, 55.0);
}

// Threshold maps to the bottom of the modulation and Threshold + Range to the top.
bool runEnvelopeWindowTest()
{
    struct Case { float thresholdDb, rangeDb, expectedActivity; };
    const Case cases[] { { -40.0f, 20.0f, 0.5f }, { -30.0f, 20.0f, 0.0f }, { -45.0f, 10.0f, 1.0f }, { -36.0f, 24.0f, 0.25f } };

    for (const auto& [thresholdDb, rangeDb, expected] : cases)
    {
        StagMotionBandsAudioProcessor processor;
        useFollower (processor, BandModulationEngine::envelope, thresholdDb, rangeDb, 1.0f, 100.0f);
        const auto trace = activityTrace (processor, [] (long) { return juce::Decibels::decibelsToGain (-30.0f); }, 0.5);
        if (std::abs (trace.back() - expected) > 0.03f)
        {
            std::cerr << "  -30 dBFS with threshold " << thresholdDb << " / range " << rangeDb
                      << ": activity " << trace.back() << ", expected " << expected << '\n';
            return false;
        }
    }

    return true;
}

// Release is a time constant of the modulation itself: 90 % -> 10 % takes ln 9 = 2.2 x Release.
bool runEnvelopeReleaseMatchesSettingTest()
{
    for (const auto releaseMs : { 50.0f, 200.0f })
    {
        StagMotionBandsAudioProcessor processor;
        useFollower (processor, BandModulationEngine::envelope, -36.0f, 18.0f, 5.0f, releaseMs);
        const auto trace = activityTrace (processor, [] (long position) {
            return position < 24000 ? sineAt (position, -6.0f, 1000.0) : 0.0f;
        }, 2.0);

        double fallsBelow90 = -1.0, fallsBelow10 = -1.0;
        for (size_t index = 0; index < trace.size(); ++index)
        {
            if (traceSeconds (index) < 0.5)
                continue;
            if (fallsBelow90 < 0 && trace[index] < 0.9f)
                fallsBelow90 = traceSeconds (index);
            if (fallsBelow10 < 0 && trace[index] < 0.1f)
                fallsBelow10 = traceSeconds (index);
        }

        const auto measuredMs = (fallsBelow10 - fallsBelow90) * 1000.0;
        const auto expectedMs = std::log (9.0) * releaseMs;
        if (fallsBelow90 < 0 || fallsBelow10 < 0 || std::abs (measuredMs - expectedMs) > expectedMs * 0.15)
        {
            std::cerr << "  release " << releaseMs << " ms: 90->10 % took " << measuredMs
                      << " ms, expected about " << expectedMs << " ms\n";
            return false;
        }
    }

    return true;
}

// A steady low tone inside the window must give a steady envelope, not one that wobbles
// with the waveform.
bool runSteadyLowToneHoldsEnvelopeTest()
{
    StagMotionBandsAudioProcessor processor;
    useFollower (processor, BandModulationEngine::envelope, -36.0f, 18.0f, 5.0f, 180.0f);
    const auto trace = activityTrace (processor, [] (long position) { return sineAt (position, -27.0f, 60.0); }, 2.0);

    const auto [lowest, highest] = std::minmax_element (trace.begin() + static_cast<long> (trace.size() / 2), trace.end());
    if (*highest - *lowest > 0.1f || *lowest < 0.2f)
    {
        std::cerr << "  steady 60 Hz envelope moves between " << *lowest << " and " << *highest << '\n';
        return false;
    }

    return true;
}

// Transient mode reacts to hits at any level, falls back between them, and ignores a
// steady tone once it has settled.
bool runTransientRespondsToHitsTest()
{
    for (const auto peakDb : { -6.0f, -30.0f })
    {
        StagMotionBandsAudioProcessor processor;
        useFollower (processor, BandModulationEngine::transient, -60.0f, 18.0f, 5.0f, 100.0f);
        const auto trace = activityTrace (processor, [peakDb] (long position) { return kickAt (position, peakDb); }, 4.0);

        // Look at the hit at 3.0 s: the peak within 40 ms and the level just before 3.5 s.
        float peakAfterHit = 0.0f, beforeNextHit = 1.0f;
        for (size_t index = 0; index < trace.size(); ++index)
        {
            const auto seconds = traceSeconds (index);
            if (seconds > 3.0 && seconds < 3.04)
                peakAfterHit = juce::jmax (peakAfterHit, trace[index]);
            if (seconds > 3.47 && seconds < 3.49)
                beforeNextHit = trace[index];
        }

        if (peakAfterHit < 0.9f || beforeNextHit > 0.15f)
        {
            std::cerr << "  hits at " << peakDb << " dBFS: peak " << peakAfterHit
                      << " within 40 ms, " << beforeNextHit << " before the next hit\n";
            return false;
        }
    }

    StagMotionBandsAudioProcessor processor;
    useFollower (processor, BandModulationEngine::transient, -60.0f, 18.0f, 5.0f, 100.0f);
    const auto trace = activityTrace (processor, [] (long position) { return sineAt (position, -12.0f, 1000.0); }, 2.0);
    const auto settled = *std::max_element (trace.begin() + static_cast<long> (trace.size() * 3 / 4), trace.end());
    if (settled > 0.1f)
    {
        std::cerr << "  steady tone still drives transient activity to " << settled << '\n';
        return false;
    }

    return true;
}

// Hosts keep processing for getTailLengthSeconds() after the input stops; the reported tail
// must cover the longest feedback ring the parameters allow.
bool runTailCoversFeedbackRingTest()
{
    using Processor = StagMotionBandsAudioProcessor;
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 480;

    for (const auto feedback : { 0.85f, -0.85f })
    {
        Processor processor;
        setParameter (processor, Processor::effectTypeParameterId (0), BandModulationEngine::chorus);
        setParameter (processor, Processor::feedbackParameterId (0), feedback);
        setParameter (processor, Processor::depthParameterId (0), 1.0f);
        setParameter (processor, Processor::mixParameterId (0), 1.0f);
        setParameter (processor, Processor::rateParameterId (0), 0.3f);
        processor.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        long position = 0;
        long impulseAt = -1;
        long lastAudible = -1;

        for (int block = 0; block < 250; ++block)
        {
            buffer.clear();
            if (block == 20)
            {
                buffer.setSample (0, 0, 1.0f);
                buffer.setSample (1, 0, 1.0f);
                impulseAt = position;
            }

            processor.processBlock (buffer, midi);

            for (int sample = 0; sample < blockSize; ++sample, ++position)
                if (impulseAt >= 0 && std::abs (buffer.getSample (0, sample)) > 0.001f)
                    lastAudible = position;
        }

        const auto ringSeconds = static_cast<double> (lastAudible - impulseAt) / sampleRate;
        const auto tailSeconds = processor.getTailLengthSeconds();
        if (lastAudible < 0 || ringSeconds > tailSeconds)
        {
            std::cerr << "  feedback " << feedback << " rings for " << ringSeconds
                      << " s but the reported tail is " << tailSeconds << " s\n";
            return false;
        }
    }

    return true;
}

// Automation can leave crossover slots unsorted; dragging the separator drawn at a sorted
// position must move that crossover, not whatever happens to be stored in the same slot.
bool runCrossoverSlotFollowsSortedOrderTest()
{
    using Processor = StagMotionBandsAudioProcessor;

    Processor processor;
    setParameter (processor, "bandCount", 4.0f);
    setParameter (processor, Processor::crossoverParameterId (0), 6000.0f);
    setParameter (processor, Processor::crossoverParameterId (1), 180.0f);
    setParameter (processor, Processor::crossoverParameterId (2), 1200.0f);

    if (processor.crossoverSlot (0) != 1 || processor.crossoverSlot (1) != 2
        || processor.crossoverSlot (2) != 0 || processor.crossoverSlot (3) != -1)
        return false;

    processor.setCrossover (processor.crossoverSlot (0), 250.0f);
    const auto sorted = processor.getSortedCrossovers();
    return std::abs (sorted[0] - 250.0f) < 1.0f
        && std::abs (sorted[1] - 1200.0f) < 1.0f
        && std::abs (sorted[2] - 6000.0f) < 1.0f;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceScope;

    if (std::abs (BandModulationEngine::rateForDivision (120.0, 3) - 2.0f) > 0.0001f
        || std::abs (BandModulationEngine::rateForDivision (120.0, 2) - 4.0f) > 0.0001f)
    {
        std::cerr << "Tempo division conversion failed\n";
        return 1;
    }

    for (int effect = BandModulationEngine::chorus;
         effect <= BandModulationEngine::frequencyShifter;
         ++effect)
    {
        for (int waveform = BandModulationEngine::sine; waveform <= BandModulationEngine::square; ++waveform)
            if (! runEffectSmoke (effect, waveform))
            {
                std::cerr << "DSP smoke failed for effect " << effect
                          << ", waveform " << waveform << '\n';
                return 1;
            }

        if (! runEffectSmoke (effect, BandModulationEngine::sine, BandModulationEngine::envelope))
        {
            std::cerr << "Envelope DSP smoke failed for effect " << effect << '\n';
            return 1;
        }
    }

    if (! runStateAndMuteSmoke())
    {
        std::cerr << "State or mute smoke failed\n";
        return 1;
    }

    if (! runEnvelopeActivitySmoke())
    {
        std::cerr << "Envelope activity or MIDI input smoke failed\n";
        return 1;
    }

    if (! runBandLimitSmoke())
    {
        std::cerr << "Four-band limit smoke failed\n";
        return 1;
    }

    const std::pair<const char*, bool (*)()> regressionTests[] {
        { "License gate preserves dry audio and fades in both directions", runLicenseGateTest },
        { "Band settings follow their bands", runBandSettingsFollowBandsTest },
        { "Crossover sum is flat", runCrossoverSumIsFlatTest },
        { "Oversized block is processed", runOversizedBlockIsProcessedTest },
        { "Edits do not click", runEditsDoNotClickTest },
        { "Frequency shifter rejects the image", runFrequencyShifterRejectsImageTest },
        { "Synced LFO follows the host", runSyncedLfoFollowsHostTest },
        { "Envelope window follows Threshold and Range", runEnvelopeWindowTest },
        { "Envelope release matches its setting", runEnvelopeReleaseMatchesSettingTest },
        { "Steady low tone holds the envelope", runSteadyLowToneHoldsEnvelopeTest },
        { "Transient responds to hits", runTransientRespondsToHitsTest },
        { "Tail covers the feedback ring", runTailCoversFeedbackRingTest },
        { "Crossover slot follows sorted order", runCrossoverSlotFollowsSortedOrderTest },
    };

    for (const auto& [name, test] : regressionTests)
    {
        if (! test())
        {
            std::cerr << name << " failed\n";
            return 1;
        }
    }

    std::cout << "All Stag Motion Bands smoke tests passed\n";
    return 0;
}
