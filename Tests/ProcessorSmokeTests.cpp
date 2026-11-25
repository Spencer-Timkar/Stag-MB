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
            buffer.setSample (channel, sample, 0.8f);

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
        { "Band settings follow their bands", runBandSettingsFollowBandsTest },
        { "Crossover sum is flat", runCrossoverSumIsFlatTest },
        { "Oversized block is processed", runOversizedBlockIsProcessedTest },
        { "Edits do not click", runEditsDoNotClickTest },
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
