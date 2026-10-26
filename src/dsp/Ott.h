#pragma once

#include "EnvelopeFollower.h"
#include "Filters.h"

#include <algorithm>
#include <array>
#include <cmath>

// Three-band "over the top" dynamics: loud parts are pushed down and quiet parts are pushed up,
// so the bass sits at one dense level whatever the input does. Crossovers follow the Xfer OTT
// (about 88 Hz and 2.5 kHz). The thresholds, ratios and times are my own tuning, not OTT's.
class Ott
{
public:
    void prepare (double sampleRate)
    {
        const auto fs = static_cast<float> (sampleRate);
        lowSplit.setUp (lowCrossoverHz, fs);
        highSplit.setUp (highCrossoverHz, fs);
        lowAlign.setUp (highCrossoverHz, fs);
        lowSplit.reset();
        highSplit.reset();
        lowAlign.reset();

        const BandTimes times[] = { { 10.0f, 120.0f }, { 3.0f, 70.0f }, { 1.0f, 40.0f } };
        for (size_t i = 0; i < bands.size(); ++i)
        {
            bands[i].follower.prepare (sampleRate);
            bands[i].follower.setTimes (times[i].attackMs, times[i].releaseMs);
        }
    }

    float process (float input)
    {
        const auto lowSplitOutput = lowSplit.process (input);
        const auto highSplitOutput = highSplit.process (lowSplitOutput.high);

        // The low band skips the second crossover, so run it through an all-pass at that corner.
        const auto alignedLow = lowAlign.process (lowSplitOutput.low);

        return bands[0].process (alignedLow.low + alignedLow.high)
             + bands[1].process (highSplitOutput.low)
             + bands[2].process (highSplitOutput.high);
    }

private:
    struct BandTimes
    {
        float attackMs, releaseMs;
    };

    struct Band
    {
        float process (float input)
        {
            const auto levelDb = 20.0f * std::log10 (std::max (follower.process (input), 1.0e-6f));

            float gainDb = 0.0f;
            if (levelDb > downThresholdDb)
                gainDb = -(levelDb - downThresholdDb) * (1.0f - 1.0f / downRatio);
            else if (levelDb < upThresholdDb)
            {
                // Fade the boost out near silence so a noise floor isn't lifted into the mix.
                const auto fade = std::clamp ((levelDb - silenceDb) / fadeRangeDb, 0.0f, 1.0f);
                gainDb = std::min (maxBoostDb, (upThresholdDb - levelDb) * (1.0f - 1.0f / upRatio)) * fade;
            }
            return input * std::pow (10.0f, gainDb / 20.0f);
        }

        EnvelopeFollower follower;
    };

    static constexpr float lowCrossoverHz = 88.3f, highCrossoverHz = 2500.0f;
    static constexpr float downThresholdDb = -20.0f, upThresholdDb = -30.0f;
    static constexpr float downRatio = 4.0f, upRatio = 3.0f;
    static constexpr float maxBoostDb = 24.0f;
    static constexpr float silenceDb = -80.0f, fadeRangeDb = 20.0f;

    Crossover lowSplit, highSplit, lowAlign;
    std::array<Band, 3> bands;
};
