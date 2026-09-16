#include "PluginProcessor.h"

#include <cmath>
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
    buffer.clear();
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            buffer.setSample (channel, sample, 0.25f);

    juce::MidiBuffer midi;
    restored.processBlock (buffer, midi);
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

    std::cout << "All Stag Motion Bands smoke tests passed\n";
    return 0;
}
