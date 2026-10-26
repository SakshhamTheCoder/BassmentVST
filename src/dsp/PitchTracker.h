#pragma once

#include <cmath>

// Turns raw detector output into a stable bass frequency: folds it into one octave
// above the Range setting (with hysteresis at the edges), holds the last pitch while
// the detector is unsure, and glides in log-frequency.
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        hasPitch = false;
        confidentNow = false;
        octaveShift = 0;
    }

    void setParams (float lowestHz, float glideMs)
    {
        rangeLog = std::log2 (lowestHz);
        glideCoef = glideMs <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (0.001f * glideMs * fs));
    }

    // Call once per detector estimate.
    void update (float hz, bool confident)
    {
        confidentNow = confident && hz > 0.0f;
        if (! confidentNow)
            return;

        // Position inside the target octave: 0 = Range, 1 = Range * 2.
        const auto hzLog = std::log2 (hz);
        auto position = hzLog - rangeLog + static_cast<float> (octaveShift);
        if (position < -edgeMargin || position >= 1.0f + edgeMargin)
        {
            octaveShift -= static_cast<int> (std::floor (position));
            position = hzLog - rangeLog + static_cast<float> (octaveShift);
        }

        targetLog = rangeLog + position;
        if (! hasPitch)
        {
            smoothedLog = targetLog;
            hasPitch = true;
        }
    }

    // Per-sample smoothed frequency in Hz.
    float nextFrequency()
    {
        smoothedLog += glideCoef * (targetLog - smoothedLog);
        return std::exp2 (smoothedLog);
    }

    float targetHz() const { return std::exp2 (targetLog); }
    bool isOpen() const { return hasPitch && confidentNow; }

private:
    // About 84 cents of slack past either edge before the octave is re-chosen.
    static constexpr float edgeMargin = 0.07f;

    float fs = 44100.0f;
    float rangeLog = 5.3219f; // log2 (40 Hz)
    float glideCoef = 1.0f;
    float targetLog = 5.3219f;
    float smoothedLog = 5.3219f;
    int octaveShift = 0;
    bool hasPitch = false;
    bool confidentNow = false;
};
