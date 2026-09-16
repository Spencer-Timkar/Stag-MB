#pragma once

#include <JuceHeader.h>

class BandModulationEngine
{
public:
    enum EffectType
    {
        off = 0,
        chorus,
        flanger,
        phaser,
        tremolo,
        autoPan,
        ringMod,
        phaseWarp,
        frequencyShifter
    };

    enum Waveform
    {
        sine = 0,
        triangle,
        saw,
        square
    };

    enum ModulationSource
    {
        lfo = 0,
        envelope
    };

    struct Settings
    {
        int effectType = off;
        int waveform = sine;
        int modulationSource = lfo;
        float rateHz = 0.5f;
        float depth = 0.55f;
        float feedback = 0.0f;
        float mix = 0.5f;
        float stereoSpread = 0.5f;
        float attackMs = 20.0f;
        float releaseMs = 180.0f;
        float carrierHz = 80.0f;
    };

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;
        channelCount = static_cast<int> (spec.numChannels);
        delayBufferSize = juce::jmax (32, static_cast<int> (std::ceil (sampleRate * 0.1)) + 2);

        delayBuffers.assign (static_cast<size_t> (channelCount),
                             std::vector<float> (static_cast<size_t> (delayBufferSize), 0.0f));
        phaserStates.assign (static_cast<size_t> (channelCount), {});
        phaserFeedback.assign (static_cast<size_t> (channelCount), 0.0f);
        hilbertBuffers.assign (static_cast<size_t> (channelCount), {});
        initialiseHilbertCoefficients();

        for (auto* value : { &rate, &depth, &feedback, &mix, &spread, &carrierRate })
            value->reset (sampleRate, 0.025);

        rate.setCurrentAndTargetValue (0.5f);
        depth.setCurrentAndTargetValue (0.55f);
        feedback.setCurrentAndTargetValue (0.0f);
        mix.setCurrentAndTargetValue (0.5f);
        spread.setCurrentAndTargetValue (0.5f);
        carrierRate.setCurrentAndTargetValue (80.0f);
        reset();
    }

    void reset()
    {
        for (auto& channel : delayBuffers)
            std::fill (channel.begin(), channel.end(), 0.0f);

        for (auto& channel : phaserStates)
            channel.fill (0.0f);

        std::fill (phaserFeedback.begin(), phaserFeedback.end(), 0.0f);
        for (auto& channel : hilbertBuffers)
            channel.fill (0.0f);
        writePosition = 0;
        hilbertPosition = 0;
        phase = 0.0;
        carrierPhase = 0.0;
        envelopeValue = 0.0f;
        activity = 0.0f;
    }

    void trigger() noexcept
    {
        phase = 0.0;
        carrierPhase = 0.0;
    }

    float getActivity() const noexcept { return activity; }

    void process (juce::AudioBuffer<float>& buffer, int numSamples, const Settings& settings)
    {
        if (sampleRate <= 0.0 || channelCount <= 0)
            return;

        rate.setTargetValue (juce::jlimit (0.01f, 20.0f, settings.rateHz));
        depth.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.depth));
        feedback.setTargetValue (juce::jlimit (-0.95f, 0.95f, settings.feedback));
        mix.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.mix));
        spread.setTargetValue (juce::jlimit (0.0f, 1.0f, settings.stereoSpread));
        carrierRate.setTargetValue (juce::jlimit (
            0.01f, static_cast<float> (sampleRate * 0.45), settings.carrierHz));

        const auto channelsToProcess = juce::jmin (channelCount, buffer.getNumChannels());
        const auto attackCoefficient = timeCoefficient (settings.attackMs);
        const auto releaseCoefficient = timeCoefficient (settings.releaseMs);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto currentRate = rate.getNextValue();
            const auto currentDepth = depth.getNextValue();
            const auto currentFeedback = feedback.getNextValue();
            const auto currentMix = mix.getNextValue();
            const auto currentSpread = spread.getNextValue();
            const auto currentCarrierRate = carrierRate.getNextValue();

            float detectorInput = 0.0f;
            for (int channel = 0; channel < channelsToProcess; ++channel)
                detectorInput = juce::jmax (detectorInput, std::abs (buffer.getSample (channel, sample)));

            const auto envelopeCoefficient = detectorInput > envelopeValue
                                                 ? attackCoefficient
                                                 : releaseCoefficient;
            envelopeValue = detectorInput + envelopeCoefficient * (envelopeValue - detectorInput);
            const auto envelopeModulation = juce::jlimit (-1.0f, 1.0f, envelopeValue * 2.0f - 1.0f);

            for (int channel = 0; channel < channelsToProcess; ++channel)
            {
                const auto channelPosition = channelsToProcess <= 1
                                                 ? 0.0f
                                                 : static_cast<float> (channel)
                                                       / static_cast<float> (channelsToProcess - 1);
                const auto channelPhase = wrapPhase (static_cast<float> (phase)
                                                     + channelPosition * currentSpread * 0.5f);
                const auto lfoValue = waveformValue (channelPhase, settings.waveform);
                const auto modulation = settings.modulationSource == envelope
                                            ? envelopeModulation
                                            : lfoValue;
                const auto input = buffer.getSample (channel, sample);
                const auto carrierChannelPhase = wrapPhase (
                    static_cast<float> (carrierPhase) + channelPosition * currentSpread * 0.5f);

                if (settings.effectType == off)
                    continue;

                float wet = input;
                if (settings.effectType == chorus || settings.effectType == flanger)
                {
                    const auto baseDelayMs = settings.effectType == chorus ? 8.0f : 2.2f;
                    const auto excursionMs = settings.effectType == chorus ? 5.5f : 1.9f;
                    const auto delayMs = juce::jmax (
                        0.15f, baseDelayMs + modulation * excursionMs * currentDepth);
                    wet = readDelay (channel, delayMs * static_cast<float> (sampleRate) * 0.001f);
                    delayBuffers[static_cast<size_t> (channel)][static_cast<size_t> (writePosition)] =
                        input + wet * currentFeedback;
                }
                else if (settings.effectType == phaser)
                {
                    auto driven = input + phaserFeedback[static_cast<size_t> (channel)] * currentFeedback;
                    const auto centreFrequency = juce::jlimit (
                        40.0f,
                        static_cast<float> (sampleRate * 0.45),
                        900.0f * std::pow (5.0f, modulation * currentDepth));
                    const auto tangent = std::tan (juce::MathConstants<float>::pi * centreFrequency
                                                   / static_cast<float> (sampleRate));
                    const auto coefficient = (1.0f - tangent) / (1.0f + tangent);

                    auto& stages = phaserStates[static_cast<size_t> (channel)];
                    for (auto& state : stages)
                    {
                        const auto output = coefficient * driven + state;
                        state = driven - coefficient * output;
                        driven = output;
                    }

                    wet = driven;
                    phaserFeedback[static_cast<size_t> (channel)] = wet;
                }
                else if (settings.effectType == tremolo)
                {
                    const auto unipolarModulation = modulation * 0.5f + 0.5f;
                    const auto gain = 1.0f - currentDepth * unipolarModulation;
                    wet = input * gain;
                }
                else if (settings.effectType == autoPan)
                {
                    if (channelsToProcess > 1)
                    {
                        const auto pan = juce::jlimit (
                            -1.0f, 1.0f, modulation * currentDepth * currentSpread);
                        const auto angle = (pan + 1.0f)
                                         * juce::MathConstants<float>::pi * 0.25f;
                        const auto gain = channel == 0
                                              ? std::cos (angle) * juce::MathConstants<float>::sqrt2
                                              : std::sin (angle) * juce::MathConstants<float>::sqrt2;
                        wet = input * gain;
                    }
                }
                else if (settings.effectType == ringMod)
                {
                    const auto carrier = waveformValue (carrierChannelPhase, settings.waveform);
                    wet = input * ((1.0f - currentDepth) + carrier * currentDepth);
                }
                else if (settings.effectType == phaseWarp)
                {
                    const auto magnitude = std::pow (std::abs (modulation),
                                                     0.35f + (1.0f - currentDepth) * 0.65f);
                    const auto warped = std::copysign (magnitude, modulation);
                    auto driven = input;
                    auto& stages = phaserStates[static_cast<size_t> (channel)];

                    for (int stageIndex = 0; stageIndex < phaserStageCount; ++stageIndex)
                    {
                        const auto baseFrequency = 90.0f * std::pow (2.05f, static_cast<float> (stageIndex));
                        const auto stageSkew = 1.0f + static_cast<float> (stageIndex) * 0.12f;
                        const auto frequency = juce::jlimit (
                            35.0f,
                            static_cast<float> (sampleRate * 0.45),
                            baseFrequency * std::pow (7.0f, warped * currentDepth * stageSkew));
                        const auto tangent = std::tan (juce::MathConstants<float>::pi * frequency
                                                       / static_cast<float> (sampleRate));
                        const auto coefficient = (1.0f - tangent) / (1.0f + tangent);
                        auto& state = stages[static_cast<size_t> (stageIndex)];
                        const auto output = coefficient * driven + state;
                        state = driven - coefficient * output;
                        driven = output;
                    }

                    wet = driven;
                }
                else if (settings.effectType == frequencyShifter)
                {
                    auto& history = hilbertBuffers[static_cast<size_t> (channel)];
                    history[static_cast<size_t> (hilbertPosition)] = input;

                    const auto delayedIndex = (hilbertPosition - hilbertCentre + hilbertTapCount)
                                            % hilbertTapCount;
                    const auto inPhase = history[static_cast<size_t> (delayedIndex)];
                    float quadrature = 0.0f;
                    for (int tap = 0; tap < hilbertTapCount; ++tap)
                    {
                        const auto historyIndex = (hilbertPosition - tap + hilbertTapCount)
                                                % hilbertTapCount;
                        quadrature += hilbertCoefficients[static_cast<size_t> (tap)]
                                    * history[static_cast<size_t> (historyIndex)];
                    }

                    const auto angle = juce::MathConstants<float>::twoPi * carrierChannelPhase;
                    wet = inPhase * std::cos (angle) - quadrature * std::sin (angle);
                }

                buffer.setSample (channel, sample,
                                  input * (1.0f - currentMix) + wet * currentMix);
            }

            writePosition = (writePosition + 1) % delayBufferSize;
            if (settings.effectType == frequencyShifter)
                hilbertPosition = (hilbertPosition + 1) % hilbertTapCount;
            phase = wrapPhase (static_cast<float> (phase + currentRate / sampleRate));
            carrierPhase = wrapPhase (
                static_cast<float> (carrierPhase + currentCarrierRate / sampleRate));
            const auto usesCarrier = settings.effectType == ringMod
                                  || settings.effectType == frequencyShifter;
            activity = usesCarrier
                           ? waveformValue (static_cast<float> (carrierPhase), sine) * 0.5f + 0.5f
                           : settings.modulationSource == envelope
                           ? juce::jlimit (0.0f, 1.0f, envelopeValue)
                           : juce::jlimit (0.0f, 1.0f,
                                          waveformValue (static_cast<float> (phase), settings.waveform)
                                              * 0.5f + 0.5f);
        }
    }

    static float rateForDivision (double bpm, int divisionIndex)
    {
        static constexpr std::array<float, 7> beatsPerCycle {
            0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f
        };

        const auto index = juce::jlimit (0, static_cast<int> (beatsPerCycle.size()) - 1, divisionIndex);
        const auto beatsPerSecond = static_cast<float> (juce::jlimit (20.0, 400.0, bpm) / 60.0);
        return beatsPerSecond / beatsPerCycle[static_cast<size_t> (index)];
    }

private:
    void initialiseHilbertCoefficients()
    {
        for (int tap = 0; tap < hilbertTapCount; ++tap)
        {
            const auto offset = tap - hilbertCentre;
            auto coefficient = 0.0f;
            if (offset != 0 && std::abs (offset) % 2 == 1)
                coefficient = 2.0f / (juce::MathConstants<float>::pi * static_cast<float> (offset));

            const auto window = 0.5f - 0.5f * std::cos (
                juce::MathConstants<float>::twoPi * static_cast<float> (tap)
                / static_cast<float> (hilbertTapCount - 1));
            hilbertCoefficients[static_cast<size_t> (tap)] = coefficient * window;
        }
    }

    float timeCoefficient (float milliseconds) const
    {
        const auto seconds = juce::jlimit (0.001f, 5.0f, milliseconds * 0.001f);
        return std::exp (-1.0f / (seconds * static_cast<float> (sampleRate)));
    }

    static float wrapPhase (float value)
    {
        return value - std::floor (value);
    }

    static float waveformValue (float normalizedPhase, int waveform)
    {
        const auto wrapped = wrapPhase (normalizedPhase);

        switch (waveform)
        {
            case triangle: return 1.0f - 4.0f * std::abs (wrapped - 0.5f);
            case saw:      return 2.0f * wrapped - 1.0f;
            case square:   return wrapped < 0.5f ? 1.0f : -1.0f;
            case sine:
            default:       return std::sin (juce::MathConstants<float>::twoPi * wrapped);
        }
    }

    float readDelay (int channel, float delayInSamples) const
    {
        auto readPosition = static_cast<float> (writePosition) - delayInSamples;
        while (readPosition < 0.0f)
            readPosition += static_cast<float> (delayBufferSize);

        const auto indexA = static_cast<int> (std::floor (readPosition)) % delayBufferSize;
        const auto indexB = (indexA + 1) % delayBufferSize;
        const auto fraction = readPosition - std::floor (readPosition);
        const auto& channelBuffer = delayBuffers[static_cast<size_t> (channel)];
        return juce::jmap (fraction,
                          channelBuffer[static_cast<size_t> (indexA)],
                          channelBuffer[static_cast<size_t> (indexB)]);
    }

    static constexpr int phaserStageCount = 6;
    static constexpr int hilbertTapCount = 63;
    static constexpr int hilbertCentre = hilbertTapCount / 2;

    double sampleRate = 0.0;
    int channelCount = 0;
    int delayBufferSize = 0;
    int writePosition = 0;
    int hilbertPosition = 0;
    double phase = 0.0;
    double carrierPhase = 0.0;
    float envelopeValue = 0.0f;
    float activity = 0.0f;

    std::vector<std::vector<float>> delayBuffers;
    std::vector<std::array<float, phaserStageCount>> phaserStates;
    std::vector<float> phaserFeedback;
    std::vector<std::array<float, hilbertTapCount>> hilbertBuffers;
    std::array<float, hilbertTapCount> hilbertCoefficients {};

    juce::SmoothedValue<float> rate;
    juce::SmoothedValue<float> depth;
    juce::SmoothedValue<float> feedback;
    juce::SmoothedValue<float> mix;
    juce::SmoothedValue<float> spread;
    juce::SmoothedValue<float> carrierRate;
};
