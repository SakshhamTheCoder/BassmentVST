#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <cmath>
#include <vector>

// Mono three-band saturator. The low band is left clean so the sub stays solid;
// mid and high go through a tanh waveshaper at 4x oversampling.
class MultibandDrive
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        const juce::dsp::ProcessSpec spec { sampleRate, static_cast<juce::uint32> (maxBlockSize), 1 };
        for (auto* filter : { &lowSplit, &highSplit, &lowAlign })
            filter->prepare (spec);
        lowAlign.setType (juce::dsp::LinkwitzRileyFilterType::allpass);

        for (auto* band : { &mid, &high })
        {
            band->oversampler.initProcessing (static_cast<size_t> (maxBlockSize));
            band->oversampler.reset();
            band->samples.assign (static_cast<size_t> (maxBlockSize), 0.0f);
        }
        lowBand.assign (static_cast<size_t> (maxBlockSize), 0.0f);

        // The low band skips the oversamplers, so delay it to stay aligned with the others.
        lowDelay.setMaximumDelayInSamples (getLatencySamples() + 1);
        lowDelay.prepare (spec);
        lowDelay.setDelay (static_cast<float> (getLatencySamples()));

        lastCrossovers = { 0.0f, 0.0f };
        mid.gain = high.gain = 1.0f;
    }

    int getLatencySamples() const { return juce::roundToInt (mid.oversampler.getLatencyInSamples()); }

    void setParams (float lowCrossoverHz, float highCrossoverHz, float midDriveDb, float highDriveDb)
    {
        // Retuning recomputes filter coefficients, so only do it on change.
        if (lowCrossoverHz != lastCrossovers[0])
            lowSplit.setCutoffFrequency (lowCrossoverHz);
        if (highCrossoverHz != lastCrossovers[1])
        {
            highSplit.setCutoffFrequency (highCrossoverHz);
            lowAlign.setCutoffFrequency (highCrossoverHz);
        }
        lastCrossovers = { lowCrossoverHz, highCrossoverHz };

        mid.targetGain = juce::Decibels::decibelsToGain (midDriveDb);
        high.targetGain = juce::Decibels::decibelsToGain (highDriveDb);
    }

    // In place. numSamples must not exceed the maxBlockSize given to prepare().
    void process (float* samples, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float low, rest, midSample, highSample;
            lowSplit.processSample (0, samples[i], low, rest);
            highSplit.processSample (0, rest, midSample, highSample);

            const auto index = static_cast<size_t> (i);
            lowBand[index] = lowAlign.processSample (0, low);
            mid.samples[index] = midSample;
            high.samples[index] = highSample;
        }

        mid.saturate (numSamples);
        high.saturate (numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto index = static_cast<size_t> (i);
            lowDelay.pushSample (0, lowBand[index]);
            samples[i] = lowDelay.popSample (0) + mid.samples[index] + high.samples[index];
        }
    }

private:
    struct Band
    {
        void saturate (int numSamples)
        {
            // Ramp the drive across the block so automation doesn't click.
            const auto step = (targetGain - gain) / static_cast<float> (numSamples);
            for (int i = 0; i < numSamples; ++i)
            {
                gain += step;
                samples[static_cast<size_t> (i)] *= gain;
            }
            gain = targetGain;

            float* channel = samples.data();
            juce::dsp::AudioBlock<float> block (&channel, 1, static_cast<size_t> (numSamples));
            auto upsampled = oversampler.processSamplesUp (block);
            auto* data = upsampled.getChannelPointer (0);
            for (size_t i = 0; i < upsampled.getNumSamples(); ++i)
                data[i] = std::tanh (data[i]);
            oversampler.processSamplesDown (block);
        }

        juce::dsp::Oversampling<float> oversampler { 1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
        std::vector<float> samples;
        float gain = 1.0f, targetGain = 1.0f;
    };

    juce::dsp::LinkwitzRileyFilter<float> lowSplit, highSplit, lowAlign;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> lowDelay { 64 };
    Band mid, high;
    std::vector<float> lowBand;
    std::array<float, 2> lastCrossovers { 0.0f, 0.0f };
};
