#include "script/easing.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
void near(float actual, float expected, const char* description, int tempo = -1)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > 4e-6f * (1.0f + std::abs(expected))) {
        std::fprintf(stderr, "%s (tempo %d): %.9g, expected %.9g\n", description, tempo, actual, expected);
        ++failures;
    }
}
}

int main()
{
    static_assert(omw::kTempoCount == 61);
    // Elastic is deliberately not endpoint-normalized in the executable.
    for (int tempo = 0; tempo < omw::kTempoCount; ++tempo) {
        float start = 0.0f, finish = 1.0f;
        if (tempo >= 25 && tempo <= 30) {
            if (tempo % 2) start = -0.00048828125f;
            else finish = 1.00048828125f;
        } else if (tempo == 31 || tempo == 33 || tempo == 35) {
            start = -0.000244140625f; finish = 1.000244140625f;
        }
        near(omw::eval_tempo(tempo, 0, 1, 2, 0), start, "source endpoint", tempo);
        near(omw::eval_tempo(tempo, 0, 1, 2, 2), finish, "destination endpoint", tempo);
        near(omw::eval_tempo(tempo, -3, 7, 2, 0), -3 + 10 * start, "translated source endpoint", tempo);
        near(omw::eval_tempo(tempo, -3, 7, 2, 2), -3 + 10 * finish, "translated destination endpoint", tempo);
        if (tempo <= 12 || tempo >= 49) {
            float previous = omw::eval_tempo(tempo, 0, 1, 2, 0);
            for (int sample = 1; sample <= 1024; ++sample) {
                const float value = omw::eval_tempo(tempo, 0, 1, 2, 2.0f * sample / 1024);
                if (!std::isfinite(value) || value + 1e-6f < previous) {
                    std::fprintf(stderr, "monotonicity failed: tempo %d sample %d\n", tempo, sample);
                    ++failures; break;
                }
                previous = value;
            }
        }
        near(omw::eval_tempo(tempo, 1, 4, 0, 0), 4, "zero duration", tempo);
    }
    // Independent closed-form values, rather than a second implementation of the table.
    near(omw::eval_tempo(0, 0, 1, 1, .25f), .25f, "linear quarter");
    near(omw::eval_tempo(1, 0, 1, 1, .25f), .0625f, "quadratic quarter");
    near(omw::eval_tempo(2, 0, 1, 1, .25f), .4375f, "quadratic out quarter");
    near(omw::eval_tempo(3, 0, 1, 1, .25f), .00390625f, "quartic quarter");
    near(omw::eval_tempo(4, 0, 1, 1, .25f), .68359375f, "quartic out quarter");
    near(omw::eval_tempo(5, 0, 1, 1, .25f), 1.0f / 65536, "eighth-power quarter");
    near(omw::eval_tempo(7, 0, 1, 1, .25f), .125f, "quadratic in-out quarter");
    near(omw::eval_tempo(8, 0, 1, 1, .25f), .375f, "quadratic out-in quarter");
    near(omw::eval_tempo(15, 0, 1, 1, .5f), -.087692529f, "back overshoot");
    near(omw::eval_tempo(38, 0, 1, 1, .5f), .765625f, "bounce midpoint");
    near(omw::eval_tempo(49, 0, 1, 1, .5f), .292893219f, "cosine midpoint");
    near(omw::eval_tempo(50, 0, 1, 1, .5f), .707106781f, "sine midpoint");
    near(omw::eval_tempo(51, 0, 1, 1, .5f), .25f, "sine fourth midpoint");
    near(omw::eval_tempo(52, 0, 1, 1, .5f), .75f, "cosine fourth midpoint");
    near(omw::eval_tempo(53, 0, 1, 1, .5f), .0625f, "sine eighth midpoint");
    near(omw::eval_tempo(54, 0, 1, 1, .5f), .9375f, "cosine eighth midpoint");
    near(omw::eval_tempo(-1, 2, 6, 2, 1), 4, "low tempo clamps to linear");
    near(omw::eval_tempo(999, 0, 1, 1, .25f), omw::eval_tempo(60, 0, 1, 1, .25f), "high tempo clamp");
    near(omw::eval_tempo(0, 0, 1, 1, 2), 2, "raw time extrapolation");
    std::printf("61 tempo curves: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
