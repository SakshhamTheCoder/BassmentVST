// Offline checks for Ott. No JUCE, no framework.
// Build and run: cmake -B build -DBASSMENT_BUILD_TESTS=ON, build, then ctest --test-dir build -C Release

#include "../src/dsp/Ott.h"

#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond, ...)                      \
    do {                                      \
        if (! (cond)) {                       \
            ++failures;                       \
            std::printf ("FAIL %s: ", #cond); \
            std::printf (__VA_ARGS__);        \
            std::printf ("%c", 10);           \
        }                                     \
    } while (0)

constexpr double fs = 48000.0;
constexpr double pi = 3.14159265358979323846;

static double rms (const std::vector<float>& x, size_t from)
{
    double sum = 0;
    for (size_t i = from; i < x.size(); ++i)
        sum += (double) x[i] * x[i];
    return std::sqrt (sum / (double) (x.size() - from));
}

static std::vector<float> sine (double hz, double amplitude, size_t n)
{
    std::vector<float> x (n);
    for (size_t i = 0; i < n; ++i)
        x[i] = (float) (amplitude * std::sin (2 * pi * hz * (double) i / fs));
    return x;
}

static double db (double ratio) { return 20.0 * std::log10 (ratio); }

template <typename Processor>
static std::vector<float> run (Processor& processor, const std::vector<float>& input)
{
    std::vector<float> out (input.size());
    for (size_t i = 0; i < input.size(); ++i)
        out[i] = processor.process (input[i]);
    return out;
}

int main()
{
    const size_t n = (size_t) fs;

    // Ott: loud is pulled down, quiet is pushed up, silence stays silent.
    {
        Ott ott;
        ott.prepare (fs);
        const auto loud = sine (200.0, 0.8, n);
        const auto out = run (ott, loud);
        const auto change = db (rms (out, n / 2) / rms (loud, n / 2));
        std::printf ("ott loud (-2 dB peak): %+.1f dB\n", change);
        CHECK (change < -3.0, "loud input only changed %.1f dB", change);
    }
    {
        Ott ott;
        ott.prepare (fs);
        const auto quiet = sine (200.0, 0.003, n); // about -50 dB peak
        const auto out = run (ott, quiet);
        const auto change = db (rms (out, n / 2) / rms (quiet, n / 2));
        std::printf ("ott quiet (-50 dB peak): %+.1f dB\n", change);
        CHECK (change > 6.0, "quiet input only changed %.1f dB", change);
    }
    {
        Ott ott;
        ott.prepare (fs);
        const auto out = run (ott, std::vector<float> (n, 0.0f));
        std::printf ("ott silence: rms %.8f\n", rms (out, 0));
        CHECK (rms (out, 0) < 1.0e-6, "silence leaked");
    }
    {
        // Every band must be reachable: a 5 kHz tone and a 40 Hz tone both come out at sensible levels.
        for (double hz : { 40.0, 1000.0, 5000.0 })
        {
            Ott ott;
            ott.prepare (fs);
            const auto in = sine (hz, 0.1, n);
            const auto out = run (ott, in);
            const auto level = rms (out, n / 2);
            std::printf ("ott %.0f Hz at -20 dB peak: output rms %.3f\n", hz, level);
            CHECK (level > 0.02 && level < 0.5, "ott %.0f Hz output rms %.3f", hz, level);
        }
    }

    std::printf (failures ? "%c%d FAILED%c" : "%cALL PASSED%c", 10, failures, 10);
    return failures ? 1 : 0;
}
