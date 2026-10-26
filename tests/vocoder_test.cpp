// Offline checks for Vocoder. No JUCE, no framework.
// Build and run: cmake -B build -DBASSMENT_BUILD_TESTS=ON, build, then ctest --test-dir build -C Release

#include "../src/dsp/Vocoder.h"
#include "../src/dsp/SubOscillator.h"

#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond, ...)                      \
    do {                                      \
        if (! (cond)) {                       \
            ++failures;                       \
            std::printf ("FAIL %s: ", #cond); \
            std::printf (__VA_ARGS__);        \
            std::printf ("\n");               \
        }                                     \
    } while (0)

constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

static double rms (const std::vector<float>& x, size_t from)
{
    double sum = 0;
    for (size_t i = from; i < x.size(); ++i)
        sum += x[i] * x[i];
    return std::sqrt (sum / (double) (x.size() - from));
}

// Energy at one frequency (Goertzel, normalised to a sine's amplitude).
static double amplitudeAt (const std::vector<float>& x, size_t from, double hz)
{
    const double w = 2 * pi * hz / fs;
    double s1 = 0, s2 = 0;
    for (size_t i = from; i < x.size(); ++i)
    {
        const double s = x[i] + 2 * std::cos (w) * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - 2 * std::cos (w) * s1 * s2;
    return 2 * std::sqrt (power) / (double) (x.size() - from);
}

static std::vector<float> run (const std::vector<float>& modulator, double carrierHz, float motionDepth = 0.0f)
{
    Vocoder vocoder;
    vocoder.prepare (fs);
    vocoder.setMotion (motionDepth, 4.67f);
    SubOscillator saw;
    saw.setShape (1.0f);
    std::vector<float> out (modulator.size());
    for (size_t i = 0; i < modulator.size(); ++i)
        out[i] = vocoder.process (modulator[i], saw.next (carrierHz, fs));
    return out;
}

int main()
{
    const size_t n = (size_t) fs;
    const size_t settled = n / 2;

    // Two-tone "vowel" at 500 Hz and 1500 Hz, carrier saw at 55 Hz.
    std::vector<float> vowel (n);
    for (size_t i = 0; i < n; ++i)
        vowel[i] = (float) (0.3 * std::sin (2 * pi * 500 * i / fs) + 0.2 * std::sin (2 * pi * 1500 * i / fs));

    const auto voiced = run (vowel, 55.0);
    const auto inLevel = rms (vowel, settled);
    const auto outLevel = rms (voiced, settled);
    std::printf ("vowel: input rms %.3f, output rms %.3f\n", inLevel, outLevel);
    CHECK (outLevel > 0.3 * inLevel && outLevel < 3.0 * inLevel, "output level %.3f vs input %.3f", outLevel, inLevel);

    // Energy lands on carrier harmonics next to the formants, not far below them or in the valley between.
    const auto nearFormant = amplitudeAt (voiced, settled, 440.0);
    const auto belowFormant = amplitudeAt (voiced, settled, 110.0);
    const auto valley = amplitudeAt (voiced, settled, 880.0);
    std::printf ("harmonics: 440 Hz %.4f, 110 Hz %.4f, 880 Hz %.4f\n", nearFormant, belowFormant, valley);
    CHECK (nearFormant > 3.0 * belowFormant, "no shaping below the formant");
    CHECK (nearFormant > 3.0 * valley, "no shaping between formants");

    // Sibilance: a 6 kHz tone has nothing to ride on at a 55 Hz saw's harmonics, so the noise carrier must carry it.
    std::vector<float> hiss (n);
    for (size_t i = 0; i < n; ++i)
        hiss[i] = (float) (0.2 * std::sin (2 * pi * 6000 * i / fs));
    const auto hissOut = run (hiss, 55.0);
    std::printf ("sibilance: input rms %.3f, output rms %.3f%c", rms (hiss, settled), rms (hissOut, settled), 10);
    CHECK (rms (hissOut, settled) > 0.3 * rms (hiss, settled), "high band lost");

    // Formant motion keeps the level sane and finite while the bands swing.
    const auto moving = run (vowel, 55.0, 0.4f);
    bool finite = true;
    for (auto v : moving)
        finite = finite && std::isfinite (v);
    std::printf ("motion: output rms %.3f%c", rms (moving, settled), 10);
    CHECK (finite, "motion produced non-finite samples");
    CHECK (rms (moving, settled) > 0.3 * inLevel && rms (moving, settled) < 3.0 * inLevel, "motion level %.3f", rms (moving, settled));

    // No input, no output.
    const auto silent = run (std::vector<float> (n, 0.0f), 55.0);
    CHECK (rms (silent, settled) < 1.0e-4, "silent input leaked %.6f", rms (silent, settled));

    // A drum-like click train should produce bursts, not silence and not blow-ups.
    std::vector<float> clicks (n, 0.0f);
    for (size_t i = 0; i < n; i += n / 4)
        for (size_t j = 0; j < 200 && i + j < n; ++j)
            clicks[i + j] = (float) (0.8 * std::exp (-(double) j / 40.0) * std::sin (2 * pi * 200 * j / fs));
    const auto drums = run (clicks, 55.0);
    float peak = 0;
    for (auto v : drums)
        peak = std::max (peak, std::abs (v));
    std::printf ("clicks: output peak %.3f\n", peak);
    CHECK (peak > 0.05f && peak < 8.0f, "click peak %.3f", peak);

    std::printf (failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
