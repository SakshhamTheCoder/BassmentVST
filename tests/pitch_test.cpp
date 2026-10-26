// Offline checks for PitchDetector and PitchTracker. No JUCE, no framework.
// Build and run: cmake -B build -DBASSMENT_BUILD_TESTS=ON, build, then ctest --test-dir build -C Release

#include "../src/dsp/PitchDetector.h"
#include "../src/dsp/PitchTracker.h"

#include <cstdio>
#include <functional>

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

constexpr double pi = 3.14159265358979323846;

static float sine (double fs, double hz, int n) { return (float) (0.5 * std::sin (2 * pi * hz * n / fs)); }

// Band-limited saw (harmonics below Nyquist only) so the test has no aliasing.
static float saw (double fs, double hz, int n)
{
    double sum = 0;
    for (int k = 1; k * hz < fs / 2; ++k)
        sum += std::sin (2 * pi * k * hz * n / fs) / k;
    return (float) (0.3 * sum);
}

// Worst cents error over every estimate after a 0.2 s warm-up, and the lowest confidence.
static void run (double fs, const std::function<float (int)>& source, double expectedHz,
                 double& worstCents, float& lowestConfidence)
{
    PitchDetector detector;
    detector.prepare (fs);
    worstCents = 0;
    lowestConfidence = 1;
    for (int n = 0; n < static_cast<int> (fs); ++n)
    {
        if (! detector.push (source (n)) || n < 0.2 * fs)
            continue;
        const auto r = detector.result();
        lowestConfidence = std::min (lowestConfidence, r.confidence);
        worstCents = std::max (worstCents, std::abs (1200.0 * std::log2 (r.frequency / expectedHz)));
    }
}

int main()
{
    const double rates[] = { 12000.0, 11025.0 };
    const double freqs[] = { 60, 82.4, 110, 220, 330, 440, 660, 880, 1000 };

    for (auto fs : rates)
        for (auto hz : freqs)
            for (int shape = 0; shape < 2; ++shape)
            {
                double cents;
                float confidence;
                run (fs, [&] (int n) { return shape == 0 ? sine (fs, hz, n) : saw (fs, hz, n); }, hz, cents, confidence);
                const char* name = shape == 0 ? "sine" : "saw ";
                std::printf ("%s fs=%.0f %7.1f Hz  worst %.2f cents  min confidence %.2f\n", name, fs, hz, cents, confidence);
                CHECK (cents < 10.0, "%s %.1f Hz at %.0f: %.2f cents", name, hz, fs, cents);
                CHECK (confidence > 0.8f, "%s %.1f Hz at %.0f: confidence %.2f", name, hz, fs, confidence);
            }

    // White noise must never look pitched.
    {
        unsigned state = 12345;
        PitchDetector detector;
        detector.prepare (12000.0);
        float highest = 0;
        for (int n = 0; n < 12000; ++n)
        {
            state = state * 1664525u + 1013904223u;
            const float noise = (float) (state >> 8) / (float) (1 << 24) - 0.5f;
            if (detector.push (noise) && n > 2400)
                highest = std::max (highest, detector.result().confidence);
        }
        std::printf ("noise highest confidence %.2f\n", highest);
        CHECK (highest < 0.5f, "noise confidence %.2f", highest);
    }

    // Pitch curve for a glide 100 -> 400 Hz over 1 s, about one line per 50 ms. Eyeball it.
    {
        PitchDetector detector;
        detector.prepare (12000.0);
        double phase = 0;
        std::printf ("glide curve (expected -> detected):\n");
        for (int n = 0; n < 12000; ++n)
        {
            const double hz = 100.0 * std::pow (4.0, n / 12000.0);
            phase += 2 * pi * hz / 12000.0;
            if (detector.push ((float) (0.5 * std::sin (phase))) && n % 600 < 60)
                std::printf ("  %6.1f -> %6.1f\n", hz, detector.result().frequency);
        }
    }

    // Tracker: a note sitting on the octave edge must not flip octaves.
    {
        PitchTracker tracker;
        tracker.prepare (48000.0);
        tracker.setParams (40.0f, 30.0f);
        tracker.update (440.0f, true);
        CHECK (std::abs (tracker.targetHz() - 55.0f) < 0.01f, "fold 440 -> %.2f", tracker.targetHz());

        for (int i = 0; i < 20; ++i)
        {
            tracker.update (i % 2 ? 8 * 81.0f : 8 * 79.0f, true);
            CHECK (tracker.targetHz() > 75.0f && tracker.targetHz() < 85.0f, "edge flip: %.2f", tracker.targetHz());
        }

        tracker.update (4 * 100.0f, true);
        CHECK (std::abs (tracker.targetHz() - 50.0f) < 0.01f, "fold 400 -> %.2f", tracker.targetHz());

        tracker.update (123.0f, false);
        CHECK (! tracker.isOpen(), "unconfident estimate must close the gate");
        CHECK (std::abs (tracker.targetHz() - 50.0f) < 0.01f, "pitch must hold while unconfident");
    }

    std::printf (failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
