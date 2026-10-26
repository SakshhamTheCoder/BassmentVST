#pragma once

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

// Tempo-synced resonant low-pass sweep, the "wub" in a dubstep bass.
// Depth 0 leaves the signal untouched; depth 1 sweeps hard with a strong resonant peak.
class Growl
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        openLog = std::log (std::min (18000.0f, 0.45f * static_cast<float> (sampleRate)));
        depthCoef = 1.0f - std::exp (-1.0f / (0.02f * static_cast<float> (sampleRate)));

        filter.prepare ({ sampleRate, 512, 1 });
        filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        depth = 0.0f;
        lfoPhase = 0.0f;
    }

    void setTarget (float newDepth, float lfoRateHz)
    {
        targetDepth = newDepth;
        lfoStep = lfoRateHz / static_cast<float> (sampleRate);
    }

    void process (float* samples, int numSamples)
    {
        if (targetDepth <= 0.0f && depth < 0.001f)
        {
            depth = 0.0f;
            filter.reset();
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            depth += depthCoef * (targetDepth - depth);
            lfoPhase += lfoStep;
            lfoPhase -= std::floor (lfoPhase);

            const auto lfo = 0.5f - 0.5f * std::cos (twoPi * lfoPhase);
            const auto sweepLog = sweepLowLog + (sweepHighLog - sweepLowLog) * lfo;
            filter.setCutoffFrequency (std::exp (openLog + depth * (sweepLog - openLog)));
            filter.setResonance (0.7f + 4.0f * depth);
            samples[i] = filter.processSample (0, samples[i]);
        }
    }

private:
    static constexpr float twoPi = 6.2831853f;
    static inline const float sweepLowLog = std::log (150.0f);
    static inline const float sweepHighLog = std::log (2500.0f);

    juce::dsp::StateVariableTPTFilter<float> filter;
    double sampleRate = 44100.0;
    float openLog = 9.8f;
    float depthCoef = 0.0f;
    float depth = 0.0f, targetDepth = 0.0f;
    float lfoPhase = 0.0f, lfoStep = 0.0f;
};
