#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// YIN pitch detector. Feed it samples at a low rate (about 12 kHz); every `hop`
// samples it re-analyses the last window and updates result(). Allocates only in prepare().
class PitchDetector
{
public:
    struct Result
    {
        float frequency = 0.0f; // Hz, 0 when nothing was found
        float confidence = 0.0f; // 0..1, equals 1 - YIN aperiodicity
    };

    void prepare (double sampleRate, float minHz = 50.0f, float maxHz = 1000.0f,
                  float windowMs = 40.0f, float hopMs = 5.0f)
    {
        fs = sampleRate;
        window = static_cast<int> (std::ceil (windowMs * 0.001 * fs));
        tauMax = static_cast<int> (std::ceil (fs / minHz));
        tauMin = std::max (2, static_cast<int> (fs / maxHz));
        hop = std::max (1, static_cast<int> (hopMs * 0.001 * fs));

        const auto frameSize = static_cast<size_t> (window + tauMax);
        ring.assign (frameSize, 0.0f);
        frame.assign (frameSize, 0.0f);
        cmnd.assign (static_cast<size_t> (tauMax) + 1, 1.0f);
        writePos = 0;
        sinceHop = 0;
        last = {};
    }

    // Returns true when result() holds a fresh estimate.
    bool push (float sample)
    {
        ring[writePos] = sample;
        writePos = (writePos + 1) % ring.size();

        if (++sinceHop < hop)
            return false;

        sinceHop = 0;
        analyse();
        return true;
    }

    Result result() const { return last; }

    // How far behind the input an estimate is, in samples at the detector's rate:
    // the analysed frame is centred half a frame back.
    int latencySamples() const { return (window + tauMax) / 2; }

private:
    static constexpr float threshold = 0.15f;
    static constexpr float silenceMeanSquare = 1.0e-7f;

    float at (int tau) const { return cmnd[static_cast<size_t> (tau)]; }

    void analyse()
    {
        for (size_t i = 0; i < frame.size(); ++i)
            frame[i] = ring[(writePos + i) % ring.size()];

        float energy = 0.0f;
        for (int j = 0; j < window; ++j)
            energy += frame[static_cast<size_t> (j)] * frame[static_cast<size_t> (j)];

        if (energy < silenceMeanSquare * static_cast<float> (window))
        {
            last = {};
            return;
        }

        // Difference function, folded straight into the cumulative mean normalised form.
        cmnd[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float diff = 0.0f;
            for (int j = 0; j < window; ++j)
            {
                const auto delta = frame[static_cast<size_t> (j)] - frame[static_cast<size_t> (j + tau)];
                diff += delta * delta;
            }
            runningSum += diff;
            cmnd[static_cast<size_t> (tau)] = runningSum > 0.0f ? diff * static_cast<float> (tau) / runningSum : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (at (tau) < threshold)
            {
                while (tau + 1 < tauMax && at (tau + 1) < at (tau))
                    ++tau;
                best = tau;
                break;
            }
        }

        // Nothing under the threshold: take the deepest dip, it will report low confidence.
        if (best < 0)
            best = static_cast<int> (std::min_element (cmnd.begin() + tauMin, cmnd.begin() + tauMax) - cmnd.begin());

        const auto curvature = at (best - 1) - 2.0f * at (best) + at (best + 1);
        const auto offset = curvature != 0.0f ? std::clamp (0.5f * (at (best - 1) - at (best + 1)) / curvature, -1.0f, 1.0f) : 0.0f;

        last.frequency = static_cast<float> (fs / (static_cast<double> (best) + offset));
        last.confidence = std::clamp (1.0f - at (best), 0.0f, 1.0f);
    }

    double fs = 12000.0;
    int window = 0, tauMin = 0, tauMax = 0, hop = 1, sinceHop = 0;
    size_t writePos = 0;
    std::vector<float> ring, frame, cmnd;
    Result last;
};
