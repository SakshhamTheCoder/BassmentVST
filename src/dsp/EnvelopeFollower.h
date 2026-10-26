#pragma once

#include <cmath>

// Peak follower with separate attack and release, per sample.
class EnvelopeFollower
{
public:
    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        level = 0.0f;
    }

    void setTimes (float attackMs, float releaseMs)
    {
        attackCoef = coefficient (attackMs);
        releaseCoef = coefficient (releaseMs);
    }

    float process (float input)
    {
        const auto rectified = std::abs (input);
        const auto coef = rectified > level ? attackCoef : releaseCoef;
        level = rectified + coef * (level - rectified);
        return level;
    }

    float current() const { return level; }

private:
    float coefficient (float ms) const { return std::exp (-1.0f / (0.001f * ms * fs)); }

    float fs = 44100.0f;
    float level = 0.0f;
    float attackCoef = 0.0f;
    float releaseCoef = 0.0f;
};
