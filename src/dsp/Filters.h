#pragma once

#include <cmath>

// Small JUCE-free filters shared by the vocoder, the dynamics and the disperser.

constexpr float filterPi = 3.14159265f;

// Topology-preserving state variable filter (Cytomic). 12 dB/oct.
// The band-pass output has unity gain at the cutoff.
class Svf
{
public:
    struct Output
    {
        float lowpass, bandpass, highpass;
    };

    void setUp (float cutoffHz, float q, float sampleRate)
    {
        const auto g = std::tan (filterPi * cutoffHz / sampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { s1 = s2 = 0.0f; }

    Output process (float input)
    {
        const auto v3 = input - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1;
        s2 = 2.0f * v2 - s2;
        return { v2, k * v1, input - k * v1 - v2 };
    }

private:
    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, s1 = 0.0f, s2 = 0.0f;
};

// Linkwitz-Riley 24 dB/oct crossover: low and high sum back to an all-pass of the input.
class Crossover
{
public:
    struct Output
    {
        float low, high;
    };

    void setUp (float cutoffHz, float sampleRate)
    {
        constexpr float butterworthQ = 0.70710678f;
        for (auto* stage : { &lowA, &lowB, &highA, &highB })
            stage->setUp (cutoffHz, butterworthQ, sampleRate);
    }

    void reset()
    {
        for (auto* stage : { &lowA, &lowB, &highA, &highB })
            stage->reset();
    }

    Output process (float input)
    {
        return { lowB.process (lowA.process (input).lowpass).lowpass,
                 highB.process (highA.process (input).highpass).highpass };
    }

private:
    Svf lowA, lowB, highA, highB;
};

// Second-order all-pass: flat magnitude, phase rotation centred on the cutoff.
class Allpass
{
public:
    void setUp (float cutoffHz, float q, float sampleRate)
    {
        const auto w0 = 2.0f * filterPi * cutoffHz / sampleRate;
        const auto alpha = std::sin (w0) / (2.0f * q);
        const auto a0 = 1.0f + alpha;
        b0 = (1.0f - alpha) / a0;
        b1 = -2.0f * std::cos (w0) / a0;
        // b2 = a0 / a0 = 1 and a1 = b1, a2 = b0 for an all-pass.
    }

    void reset() { z1 = z2 = 0.0f; }

    float process (float input)
    {
        const auto output = b0 * input + z1;
        z1 = b1 * input - b1 * output + z2;
        z2 = input - b0 * output;
        return output;
    }

private:
    float b0 = 1.0f, b1 = 0.0f, z1 = 0.0f, z2 = 0.0f;
};
