#pragma once

#include <cmath>

// Sine blended with a PolyBLEP saw. Shape 0 is pure sine, 1 is pure saw.
class SubOscillator
{
public:
    void reset() { phase = 0.0; }
    void setShape (float newShape) { shape = newShape; }

    float next (double frequencyHz, double sampleRate)
    {
        const auto step = frequencyHz / sampleRate;
        phase += step;
        phase -= std::floor (phase);

        const auto sine = static_cast<float> (std::sin (twoPi * phase));
        const auto saw = static_cast<float> (2.0 * phase - 1.0 - polyBlep (phase, step));
        return (1.0f - shape) * sine + shape * saw;
    }

private:
    static constexpr double twoPi = 6.283185307179586;

    // Smooths the saw's jump so it doesn't alias.
    static double polyBlep (double t, double step)
    {
        if (t < step)
        {
            t /= step;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - step)
        {
            t = (t - 1.0) / step;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    }

    double phase = 0.0;
    float shape = 0.3f;
};
