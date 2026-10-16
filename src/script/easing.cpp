#include "easing.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace omw {
namespace {

using Tempo = float (*)(float, float, float, float);
using Shape = float (*)(float);

// The original uses scalar SSE float instructions, then fld of its stored float
// for the x86 return ABI. Keep float intermediates; avoid contraction into FMA.
constexpr float kHalfPi = 1.5707963705062866f;
constexpr float kBack2 = 0.8633249402046204f;
constexpr float kBack3 = 1.701540231704712f;
constexpr float kBack4 = 2.554462194442749f;

static float square(float u) { return u * u; }
static float fourth(float u) { const float u2 = u * u; return u2 * u2; }
static float eighth(float u) { const float u4 = fourth(u); return u4 * u4; }
static float back2(float u) { return (u * (kBack2 + 1.0f) - kBack2) * u; }
static float back3(float u) { return (u * u) * (u * (kBack3 + 1.0f) - kBack3); }
static float back4(float u) { return ((u * u) * u) * (u * (kBack4 + 1.0f) - kBack4); }

static float elastic(float poweredTime)
{
    const float shifted = poweredTime - 1.0f;
    const float amplitude = std::pow(2.0f, shifted * 10.0f);
    const float phase = (shifted - 0.07500000298023224f) * 20.943950653076172f;
    return -(amplitude * std::sin(phase));
}
static float elastic2(float u) { return elastic(square(u)); }
static float elastic4(float u) { return elastic(fourth(u)); }
static float elastic8(float u) { return elastic(eighth(u)); }

static float bounce(float u)
{
    if (u < 0.3636363744735718f) return (u * 7.5625f) * u;
    float center, height;
    if (u < 0.7272727489471436f) { center = 0.5454545617103577f; height = 0.75f; }
    else if (u < 0.9090909361839294f) { center = 0.8181818127632141f; height = 0.9375f; }
    else { center = 0.9545454382896423f; height = 0.984375f; }
    const float local = u - center;
    return (local * 7.5625f) * local + height;
}
static float bounce2(float u) { return bounce(square(u)); }
static float bounce4(float u) { return bounce(fourth(u)); }

static float apply_in(Shape shape, float src, float dst, float duration, float timer)
{
    return shape(timer / duration) * (dst - src) + src;
}
static float apply_out(Shape shape, float src, float dst, float duration, float timer)
{
    return shape((duration - timer) / duration) * (src - dst) + dst;
}
static float split(Tempo first, Tempo second, float src, float dst, float duration, float timer)
{
    const float half = duration * 0.5f;
    const float midpoint = (dst + src) * 0.5f;
    if (timer < half) return first(src, midpoint, half, timer);
    return second(midpoint, dst, duration - half, timer - half);
}

static float linear(float src, float dst, float duration, float timer)
{
    return (timer / duration) * (dst - src) + src;
}

// Named entry points and dispatch table below preserve the original binary order.

static float power2_in(float src, float dst, float duration, float timer)
{
    return apply_in(square, src, dst, duration, timer);
}

static float power2_out(float src, float dst, float duration, float timer)
{
    return apply_out(square, src, dst, duration, timer);
}

static float power4_in(float src, float dst, float duration, float timer)
{
    return apply_in(fourth, src, dst, duration, timer);
}

static float power4_out(float src, float dst, float duration, float timer)
{
    return apply_out(fourth, src, dst, duration, timer);
}

static float power8_in(float src, float dst, float duration, float timer)
{
    return apply_in(eighth, src, dst, duration, timer);
}

static float power8_out(float src, float dst, float duration, float timer)
{
    return apply_out(eighth, src, dst, duration, timer);
}

static float power2_in_out(float src, float dst, float duration, float timer)
{
    return split(power2_in, power2_out, src, dst, duration, timer);
}

static float power2_out_in(float src, float dst, float duration, float timer)
{
    return split(power2_out, power2_in, src, dst, duration, timer);
}

static float power4_in_out(float src, float dst, float duration, float timer)
{
    return split(power4_in, power4_out, src, dst, duration, timer);
}

static float power4_out_in(float src, float dst, float duration, float timer)
{
    return split(power4_out, power4_in, src, dst, duration, timer);
}

static float power8_in_out(float src, float dst, float duration, float timer)
{
    return split(power8_in, power8_out, src, dst, duration, timer);
}

static float power8_out_in(float src, float dst, float duration, float timer)
{
    return split(power8_out, power8_in, src, dst, duration, timer);
}

static float back2_in(float src, float dst, float duration, float timer)
{
    return apply_in(back2, src, dst, duration, timer);
}

static float back2_out(float src, float dst, float duration, float timer)
{
    return apply_out(back2, src, dst, duration, timer);
}

static float back3_in(float src, float dst, float duration, float timer)
{
    return apply_in(back3, src, dst, duration, timer);
}

static float back3_out(float src, float dst, float duration, float timer)
{
    return apply_out(back3, src, dst, duration, timer);
}

static float back4_in(float src, float dst, float duration, float timer)
{
    return apply_in(back4, src, dst, duration, timer);
}

static float back4_out(float src, float dst, float duration, float timer)
{
    return apply_out(back4, src, dst, duration, timer);
}

static float back2_in_out(float src, float dst, float duration, float timer)
{
    return split(back2_in, back2_out, src, dst, duration, timer);
}

static float back2_out_in(float src, float dst, float duration, float timer)
{
    return split(back2_out, back2_in, src, dst, duration, timer);
}

static float back3_in_out(float src, float dst, float duration, float timer)
{
    return split(back3_in, back3_out, src, dst, duration, timer);
}

static float back3_out_in(float src, float dst, float duration, float timer)
{
    return split(back3_out, back3_in, src, dst, duration, timer);
}

static float back4_in_out(float src, float dst, float duration, float timer)
{
    return split(back4_in, back4_out, src, dst, duration, timer);
}

static float back4_out_in(float src, float dst, float duration, float timer)
{
    return split(back4_out, back4_in, src, dst, duration, timer);
}

static float elastic2_in(float src, float dst, float duration, float timer)
{
    return apply_in(elastic2, src, dst, duration, timer);
}

static float elastic2_out(float src, float dst, float duration, float timer)
{
    return apply_out(elastic2, src, dst, duration, timer);
}

static float elastic4_in(float src, float dst, float duration, float timer)
{
    return apply_in(elastic4, src, dst, duration, timer);
}

static float elastic4_out(float src, float dst, float duration, float timer)
{
    return apply_out(elastic4, src, dst, duration, timer);
}

static float elastic8_in(float src, float dst, float duration, float timer)
{
    return apply_in(elastic8, src, dst, duration, timer);
}

static float elastic8_out(float src, float dst, float duration, float timer)
{
    return apply_out(elastic8, src, dst, duration, timer);
}

static float elastic2_in_out(float src, float dst, float duration, float timer)
{
    return split(elastic2_in, elastic2_out, src, dst, duration, timer);
}

static float elastic2_out_in(float src, float dst, float duration, float timer)
{
    return split(elastic2_out, elastic2_in, src, dst, duration, timer);
}

static float elastic4_in_out(float src, float dst, float duration, float timer)
{
    return split(elastic4_in, elastic4_out, src, dst, duration, timer);
}

static float elastic4_out_in(float src, float dst, float duration, float timer)
{
    return split(elastic4_out, elastic4_in, src, dst, duration, timer);
}

static float elastic8_in_out(float src, float dst, float duration, float timer)
{
    return split(elastic8_in, elastic8_out, src, dst, duration, timer);
}

static float elastic8_out_in(float src, float dst, float duration, float timer)
{
    return split(elastic8_out, elastic8_in, src, dst, duration, timer);
}

static float bounce_in(float src, float dst, float duration, float timer)
{
    return apply_out(bounce, src, dst, duration, timer);
}

static float bounce_out(float src, float dst, float duration, float timer)
{
    return apply_in(bounce, src, dst, duration, timer);
}

static float bounce_power2_in(float src, float dst, float duration, float timer)
{
    return apply_out(bounce2, src, dst, duration, timer);
}

static float bounce_power2_out(float src, float dst, float duration, float timer)
{
    return apply_in(bounce2, src, dst, duration, timer);
}

static float bounce_power4_in(float src, float dst, float duration, float timer)
{
    return apply_out(bounce4, src, dst, duration, timer);
}

static float bounce_power4_out(float src, float dst, float duration, float timer)
{
    return apply_in(bounce4, src, dst, duration, timer);
}

static float bounce_in_out(float src, float dst, float duration, float timer)
{
    return split(bounce_in, bounce_out, src, dst, duration, timer);
}

static float bounce_out_in(float src, float dst, float duration, float timer)
{
    return split(bounce_out, bounce_in, src, dst, duration, timer);
}

static float bounce_power2_in_out(float src, float dst, float duration, float timer)
{
    return split(bounce_power2_in, bounce_power2_out, src, dst, duration, timer);
}

static float bounce_power2_out_in(float src, float dst, float duration, float timer)
{
    return split(bounce_power2_out, bounce_power2_in, src, dst, duration, timer);
}

static float bounce_power4_in_out(float src, float dst, float duration, float timer)
{
    return split(bounce_power4_in, bounce_power4_out, src, dst, duration, timer);
}

static float bounce_power4_out_in(float src, float dst, float duration, float timer)
{
    return split(bounce_power4_out, bounce_power4_in, src, dst, duration, timer);
}

static float sine_in(float src, float dst, float duration, float timer)
{
    const float angle = (timer * kHalfPi) / duration;
    const float wave = std::cos(angle);
    return (1.0f - (wave)) * (dst - src) + src;
}

static float sine_out(float src, float dst, float duration, float timer)
{
    const float angle = (timer * kHalfPi) / duration;
    const float wave = std::sin(angle);
    return (wave) * (dst - src) + src;
}

static float sine4_in(float src, float dst, float duration, float timer)
{
    const float angle = (timer * kHalfPi) / duration;
    const float wave = std::sin(angle);
    return (((wave * wave) * wave) * wave) * (dst - src) + src;
}

static float cosine4_out(float src, float dst, float duration, float timer)
{
    const float angle = (timer * kHalfPi) / duration;
    const float wave = std::cos(angle);
    return (1.0f - (((wave * wave) * wave) * wave)) * (dst - src) + src;
}

static float sine8_in(float src, float dst, float duration, float timer)
{
    const float angle = (timer * kHalfPi) / duration;
    const float wave = std::sin(angle);
    const float fourthWave = ((wave * wave) * wave) * wave;
    return (fourthWave * fourthWave) * (dst - src) + src;
}

static float cosine8_out(float src, float dst, float duration, float timer)
{
    const float angle = (timer * kHalfPi) / duration;
    const float wave = std::cos(angle);
    const float fourthWave = ((wave * wave) * wave) * wave;
    return (1.0f - (fourthWave * fourthWave)) * (dst - src) + src;
}

static float sine_in_out(float src, float dst, float duration, float timer)
{
    return split(sine_in, sine_out, src, dst, duration, timer);
}

static float sine_out_in(float src, float dst, float duration, float timer)
{
    return split(sine_out, sine_in, src, dst, duration, timer);
}

static float sine4_in_out(float src, float dst, float duration, float timer)
{
    return split(sine4_in, cosine4_out, src, dst, duration, timer);
}

static float sine4_out_in(float src, float dst, float duration, float timer)
{
    return split(cosine4_out, sine4_in, src, dst, duration, timer);
}

static float sine8_in_out(float src, float dst, float duration, float timer)
{
    return split(sine8_in, cosine8_out, src, dst, duration, timer);
}

static float sine8_out_in(float src, float dst, float duration, float timer)
{
    return split(cosine8_out, sine8_in, src, dst, duration, timer);
}

constexpr std::array<Tempo, kTempoCount> kTempos = {
    linear, // 0
    power2_in, // 1
    power2_out, // 2
    power4_in, // 3
    power4_out, // 4
    power8_in, // 5
    power8_out, // 6
    power2_in_out, // 7
    power2_out_in, // 8
    power4_in_out, // 9
    power4_out_in, // 10
    power8_in_out, // 11
    power8_out_in, // 12
    back2_in, // 13
    back2_out, // 14
    back3_in, // 15
    back3_out, // 16
    back4_in, // 17
    back4_out, // 18
    back2_in_out, // 19
    back2_out_in, // 20
    back3_in_out, // 21
    back3_out_in, // 22
    back4_in_out, // 23
    back4_out_in, // 24
    elastic2_in, // 25
    elastic2_out, // 26
    elastic4_in, // 27
    elastic4_out, // 28
    elastic8_in, // 29
    elastic8_out, // 30
    elastic2_in_out, // 31
    elastic2_out_in, // 32
    elastic4_in_out, // 33
    elastic4_out_in, // 34
    elastic8_in_out, // 35
    elastic8_out_in, // 36
    bounce_in, // 37
    bounce_out, // 38
    bounce_power2_in, // 39
    bounce_power2_out, // 40
    bounce_power4_in, // 41
    bounce_power4_out, // 42
    bounce_in_out, // 43
    bounce_out_in, // 44
    bounce_power2_in_out, // 45
    bounce_power2_out_in, // 46
    bounce_power4_in_out, // 47
    bounce_power4_out_in, // 48
    sine_in, // 49
    sine_out, // 50
    sine4_in, // 51
    cosine4_out, // 52
    sine8_in, // 53
    cosine8_out, // 54
    sine_in_out, // 55
    sine_out_in, // 56
    sine4_in_out, // 57
    sine4_out_in, // 58
    sine8_in_out, // 59
    sine8_out_in, // 60
};

} // namespace

float eval_tempo(int tempo, float src, float dst, float duration, float timer)
{
    if (duration <= 0.0f) return dst;
    return kTempos[std::clamp(tempo, 0, kTempoCount - 1)](src, dst, duration, timer);
}

} // namespace omw
