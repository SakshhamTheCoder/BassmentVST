#pragma once

#include "EnvelopeFollower.h"
#include "Filters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Channel vocoder: the spectral shape of the modulator (the input) is painted onto the carrier
// (a bass-pitched saw). This is what makes a voice "talk" through the bass, and what lets drums
// and other unpitched sources come out as bass movement.
//
// Two additions over a plain vocoder:
// - Bands above about 2.5 kHz take a noise carrier instead of the saw, so consonants and breath
//   come out as hiss rather than a buzz at the bass pitch.
// - The synthesis bands can swing up and down in frequency against the analysis bands. That moves
//   the vowel (formants) in time with the music, the "yoi yoi" of a dubstep growl.
class Vocoder
{
public:
    static constexpr int numBands = 16;

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        const auto lowest = 100.0f;
        const auto highest = std::min (8000.0f, 0.45f * fs);
        const auto ratio = std::pow (highest / lowest, 1.0f / (numBands - 1));

        for (int i = 0; i < numBands; ++i)
        {
            auto& band = bands[static_cast<size_t> (i)];
            band.centreHz = lowest * std::pow (ratio, static_cast<float> (i));
            band.noiseMix = std::clamp ((band.centreHz - noiseStartHz) / (noiseFullHz - noiseStartHz), 0.0f, 1.0f);
            band.analysis.setUp (band.centreHz, bandQ, fs);
            band.synthesis.setUp (band.centreHz, bandQ, fs);
            band.modulatorLevel.prepare (sampleRate);
            band.modulatorLevel.setTimes (attackMs, releaseMs);
            band.carrierLevel.prepare (sampleRate);
            band.carrierLevel.setTimes (carrierAttackMs, carrierReleaseMs);
            band.analysis.reset();
            band.synthesis.reset();
        }

        motionPhase = 0.0f;
        controlCounter = 0;
    }

    // Swing the synthesis bands by +-depthOctaves, once per cycle at rateHz. Depth 0 turns it off.
    void setMotion (float depthOctaves, float rateHz)
    {
        motionDepth = depthOctaves;
        motionStep = rateHz / fs;
    }

    // Current level of one analysis band, for metering.
    float bandLevel (int band) const { return bands[static_cast<size_t> (band)].modulatorLevel.current(); }

    float process (float modulator, float carrier)
    {
        const auto noise = nextNoise();

        if (motionDepth > 0.0f)
        {
            motionPhase += motionStep;
            motionPhase -= std::floor (motionPhase);
            if (++controlCounter >= controlInterval)
            {
                controlCounter = 0;
                retuneSynthesis();
            }
        }

        float sum = 0.0f;
        for (auto& band : bands)
        {
            const auto modulatorLevel = band.modulatorLevel.process (band.analysis.process (modulator));
            const auto carrierBand = band.synthesis.process (carrier + (noise - carrier) * band.noiseMix);
            // Dividing by the carrier's own level flattens its spectrum, so every band comes out
            // as loud as the input's band no matter how the saw's harmonics fall off.
            sum += carrierBand * (modulatorLevel / std::max (band.carrierLevel.process (carrierBand), carrierFloor));
        }
        return sum;
    }

private:
    // Two stages: a single one leaks too much of a loud band into its neighbours.
    struct SteepBandPass
    {
        void setUp (float centreHz, float q, float sampleRate)
        {
            first.setUp (centreHz, q, sampleRate);
            second.setUp (centreHz, q, sampleRate);
        }

        void reset()
        {
            first.reset();
            second.reset();
        }

        float process (float input) { return second.process (first.process (input).bandpass).bandpass; }

        Svf first, second;
    };

    struct Band
    {
        SteepBandPass analysis, synthesis;
        EnvelopeFollower modulatorLevel, carrierLevel;
        float centreHz = 0.0f;
        float noiseMix = 0.0f;
    };

    void retuneSynthesis()
    {
        const auto shift = std::exp2 (motionDepth * std::sin (2.0f * filterPi * motionPhase));
        for (auto& band : bands)
            band.synthesis.setUp (std::min (band.centreHz * shift, 0.45f * fs), bandQ, fs);
    }

    float nextNoise()
    {
        noiseState = noiseState * 1664525u + 1013904223u;
        return static_cast<float> (static_cast<int32_t> (noiseState)) * (1.0f / 2147483648.0f);
    }

    static constexpr float bandQ = 2.5f;
    static constexpr float attackMs = 2.0f, releaseMs = 25.0f;
    static constexpr float carrierAttackMs = 5.0f, carrierReleaseMs = 50.0f;
    static constexpr float carrierFloor = 1.0e-3f;
    static constexpr float noiseStartHz = 2500.0f, noiseFullHz = 5000.0f;
    static constexpr int controlInterval = 16; // samples between synthesis retunes

    std::array<Band, numBands> bands;
    float fs = 44100.0f;
    float motionDepth = 0.0f, motionStep = 0.0f, motionPhase = 0.0f;
    int controlCounter = 0;
    uint32_t noiseState = 22222u;
};
