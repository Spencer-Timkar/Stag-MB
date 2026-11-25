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
        stageCoefficients.assign (static_cast<size_t> (channelCount), {});
        hilbertFilters.assign (static_cast<size_t> (channelCount), {});

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
        for (auto& filter : hilbertFilters)
            filter.reset();

        writePosition = 0;
        phase = 0.0;
        carrierPhase = 0.0;
        envelopeValue = 0.0f;
        activity = 0.0f;
        coefficientCountdown = 0;
        coefficientEffect = -1;
    }

    void trigger() noexcept
    {
        phase = 0.0;
        carrierPhase = 0.0;
    }

    // Puts the LFO and carrier at a position measured in cycles, e.g. host beats divided
    // by the beats per cycle of the synced division, so they stay locked to the bar.
    void setCyclePosition (double cycles) noexcept
    {
        phase = cycles - std::floor (cycles);
        carrierPhase = phase;
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
        const auto usesEnvelope = settings.modulationSource == envelope;

        if (settings.effectType != coefficientEffect)
        {
            coefficientEffect = settings.effectType;
            coefficientCountdown = 0;
        }

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto currentRate = rate.getNextValue();
            const auto currentDepth = depth.getNextValue();
            const auto currentFeedback = feedback.getNextValue();
            const auto currentMix = mix.getNextValue();
            const auto currentSpread = spread.getNextValue();
            const auto currentCarrierRate = carrierRate.getNextValue();

            // Allpass coefficients need tan/pow; the modulation is slow enough to
            // refresh them every few samples instead of every sample.
            const auto refreshCoefficients = coefficientCountdown == 0;
            coefficientCountdown = refreshCoefficients ? coefficientInterval - 1 : coefficientCountdown - 1;

            float detectorInput = 0.0f;
            for (int channel = 0; channel < channelsToProcess; ++channel)
                detectorInput = juce::jmax (detectorInput, std::abs (buffer.getSample (channel, sample)));

            const auto envelopeCoefficient = detectorInput > envelopeValue
                                                 ? attackCoefficient
                                                 : releaseCoefficient;
            envelopeValue = detectorInput + envelopeCoefficient * (envelopeValue - detectorInput);
            const auto envelopeModulation = usesEnvelope ? envelopeToModulation (envelopeValue) : 0.0f;

            for (int channel = 0; channel < channelsToProcess; ++channel)
            {
                if (settings.effectType == off)
                    break;

                const auto channelPosition = channelsToProcess <= 1
                                                 ? 0.0f
                                                 : static_cast<float> (channel)
                                                       / static_cast<float> (channelsToProcess - 1);
                const auto channelPhase = wrapPhase (static_cast<float> (phase)
                                                     + channelPosition * currentSpread * 0.5f);
                const auto modulation = usesEnvelope ? envelopeModulation
                                                     : waveformValue (channelPhase, settings.waveform);
                const auto input = buffer.getSample (channel, sample);
                const auto carrierChannelPhase = wrapPhase (
                    static_cast<float> (carrierPhase) + channelPosition * currentSpread * 0.5f);
                auto& coefficients = stageCoefficients[static_cast<size_t> (channel)];

                float wet = input;
                if (settings.effectType == chorus || settings.effectType == flanger)
                {
                    const auto baseDelayMs = settings.effectType == chorus ? chorusBaseDelayMs : flangerBaseDelayMs;
                    const auto excursionMs = settings.effectType == chorus ? chorusExcursionMs : flangerExcursionMs;
                    const auto delayMs = juce::jmax (
                        0.15f, baseDelayMs + modulation * excursionMs * currentDepth);
                    wet = readDelay (channel, delayMs * static_cast<float> (sampleRate) * 0.001f);
                    delayBuffers[static_cast<size_t> (channel)][static_cast<size_t> (writePosition)] =
                        input + wet * currentFeedback;
                }
                else if (settings.effectType == phaser)
                {
                    if (refreshCoefficients)
                        coefficients[0] = allpassCoefficient (juce::jlimit (
                            40.0f,
                            static_cast<float> (sampleRate * 0.45),
                            900.0f * std::pow (5.0f, modulation * currentDepth)));

                    auto driven = input + phaserFeedback[static_cast<size_t> (channel)] * currentFeedback;
                    auto& stages = phaserStates[static_cast<size_t> (channel)];
                    for (auto& state : stages)
                    {
                        const auto output = coefficients[0] * driven + state;
                        state = driven - coefficients[0] * output;
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
                    if (refreshCoefficients)
                    {
                        const auto magnitude = std::pow (std::abs (modulation),
                                                         0.35f + (1.0f - currentDepth) * 0.65f);
                        const auto warped = std::copysign (magnitude, modulation);

                        for (int stageIndex = 0; stageIndex < phaserStageCount; ++stageIndex)
                        {
                            const auto baseFrequency = 90.0f * std::pow (2.05f, static_cast<float> (stageIndex));
                            const auto stageSkew = 1.0f + static_cast<float> (stageIndex) * 0.12f;
                            coefficients[static_cast<size_t> (stageIndex)] = allpassCoefficient (juce::jlimit (
                                35.0f,
                                static_cast<float> (sampleRate * 0.45),
                                baseFrequency * std::pow (7.0f, warped * currentDepth * stageSkew)));
                        }
                    }

                    auto driven = input;
                    auto& stages = phaserStates[static_cast<size_t> (channel)];

                    for (int stageIndex = 0; stageIndex < phaserStageCount; ++stageIndex)
                    {
                        const auto coefficient = coefficients[static_cast<size_t> (stageIndex)];
                        auto& state = stages[static_cast<size_t> (stageIndex)];
                        const auto output = coefficient * driven + state;
                        state = driven - coefficient * output;
                        driven = output;
                    }

                    wet = driven;
                }
                else if (settings.effectType == frequencyShifter)
                {
                    const auto [inPhase, quadrature] = hilbertFilters[static_cast<size_t> (channel)].process (input);
                    const auto angle = juce::MathConstants<float>::twoPi * carrierChannelPhase;
                    wet = inPhase * std::cos (angle) - quadrature * std::sin (angle);
                }

                buffer.setSample (channel, sample,
                                  input * (1.0f - currentMix) + wet * currentMix);
            }

            writePosition = (writePosition + 1) % delayBufferSize;
            phase = wrapPhase (static_cast<float> (phase + currentRate / sampleRate));
            carrierPhase = wrapPhase (
                static_cast<float> (carrierPhase + currentCarrierRate / sampleRate));
        }

        const auto usesCarrier = settings.effectType == ringMod
                              || settings.effectType == frequencyShifter;
        const auto bipolarActivity = usesCarrier  ? waveformValue (static_cast<float> (carrierPhase), sine)
                                   : usesEnvelope ? envelopeToModulation (envelopeValue)
                                                  : waveformValue (static_cast<float> (phase), settings.waveform);
        activity = juce::jlimit (0.0f, 1.0f, bipolarActivity * 0.5f + 0.5f);
    }

    static float beatsPerCycleForDivision (int divisionIndex)
    {
        static constexpr std::array<float, 7> beatsPerCycle {
            0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f
        };

        const auto index = juce::jlimit (0, static_cast<int> (beatsPerCycle.size()) - 1, divisionIndex);
        return beatsPerCycle[static_cast<size_t> (index)];
    }

    static float rateForDivision (double bpm, int divisionIndex)
    {
        const auto beatsPerSecond = static_cast<float> (juce::jlimit (20.0, 400.0, bpm) / 60.0);
        return beatsPerSecond / beatsPerCycleForDivision (divisionIndex);
    }

    // Seconds for the delay effects' feedback loop to decay by 60 dB at this feedback,
    // assuming the longest loop (the chorus at full excursion). The phaser's feedback
    // runs through six first-order allpasses, whose group delay is shorter than that.
    static double feedbackTailSeconds (float feedback)
    {
        const auto magnitude = static_cast<double> (juce::jlimit (0.0f, 0.99f, std::abs (feedback)));
        const auto passes = magnitude > 0.0 ? std::ceil (std::log (0.001) / std::log (magnitude)) : 1.0;
        return passes * static_cast<double> (chorusBaseDelayMs + chorusExcursionMs) * 0.001;
    }

    // Maps the follower level in dB onto the bipolar modulation range: -48 dBFS and below
    // is -1, -24 dBFS is 0 and 0 dBFS is +1, so quiet bands still swing the modulation.
    static float envelopeToModulation (float level)
    {
        constexpr auto floorDb = -48.0f;
        return juce::jlimit (-1.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (level, floorDb) * (2.0f / -floorDb));
    }

private:
    // Olli Niemitalo's pair of allpass chains whose outputs stay 90 degrees apart across
    // nearly the whole audio band, unlike a short FIR Hilbert filter, which loses the
    // quadrature at low frequencies and lets the unwanted sideband through.
    class HilbertPair
    {
    public:
        void reset() noexcept
        {
            for (auto* path : { &inPhasePath, &quadraturePath })
                for (auto& section : *path)
                    section = {};

            delayedInPhase = 0.0f;
        }

        std::pair<float, float> process (float input) noexcept
        {
            auto inPhase = input;
            for (size_t index = 0; index < inPhasePath.size(); ++index)
                inPhase = inPhasePath[index].process (inPhase, inPhaseCoefficients[index]);

            auto quadrature = input;
            for (size_t index = 0; index < quadraturePath.size(); ++index)
                quadrature = quadraturePath[index].process (quadrature, quadratureCoefficients[index]);

            // The in-phase path takes one extra sample of delay to line up with the other,
            // which then leads it by 90 degrees; negating it gives the lagging quadrature
            // of a true Hilbert transform, so the shifter moves frequencies up.
            const auto alignedInPhase = delayedInPhase;
            delayedInPhase = inPhase;
            return { alignedInPhase, -quadrature };
        }

    private:
        // y[n] = a^2 (x[n] + y[n-2]) - x[n-2]
        struct Section
        {
            float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

            float process (float x, float coefficient) noexcept
            {
                const auto y = coefficient * (x + y2) - x2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                return y;
            }
        };

        // Section coefficients are the published values squared.
        static constexpr std::array<float, 4> inPhaseCoefficients {
            0.6923878f * 0.6923878f,
            0.9360654322959f * 0.9360654322959f,
            0.9882295226860f * 0.9882295226860f,
            0.9987488452737f * 0.9987488452737f
        };
        static constexpr std::array<float, 4> quadratureCoefficients {
            0.4021921162426f * 0.4021921162426f,
            0.8561710882420f * 0.8561710882420f,
            0.9722909545651f * 0.9722909545651f,
            0.9952884791278f * 0.9952884791278f
        };

        std::array<Section, 4> inPhasePath {};
        std::array<Section, 4> quadraturePath {};
        float delayedInPhase = 0.0f;
    };

    float allpassCoefficient (float frequency) const
    {
        const auto tangent = std::tan (juce::MathConstants<float>::pi * frequency
                                       / static_cast<float> (sampleRate));
        return (1.0f - tangent) / (1.0f + tangent);
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

    static constexpr float chorusBaseDelayMs = 8.0f;
    static constexpr float chorusExcursionMs = 5.5f;
    static constexpr float flangerBaseDelayMs = 2.2f;
    static constexpr float flangerExcursionMs = 1.9f;
    static constexpr int phaserStageCount = 6;
    static constexpr int coefficientInterval = 8;

    double sampleRate = 0.0;
    int channelCount = 0;
    int delayBufferSize = 0;
    int writePosition = 0;
    double phase = 0.0;
    double carrierPhase = 0.0;
    float envelopeValue = 0.0f;
    float activity = 0.0f;
    int coefficientCountdown = 0;
    int coefficientEffect = -1;

    std::vector<std::vector<float>> delayBuffers;
    std::vector<std::array<float, phaserStageCount>> phaserStates;
    std::vector<float> phaserFeedback;
    std::vector<std::array<float, phaserStageCount>> stageCoefficients;
    std::vector<HilbertPair> hilbertFilters;

    juce::SmoothedValue<float> rate;
    juce::SmoothedValue<float> depth;
    juce::SmoothedValue<float> feedback;
    juce::SmoothedValue<float> mix;
    juce::SmoothedValue<float> spread;
    juce::SmoothedValue<float> carrierRate;
};
