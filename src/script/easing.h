#pragma once

namespace omw {

constexpr int kTempoCount = 61;

// Original script tempo order. Invalid indices clamp to the nearest entry.
// Positive durations preserve the curve's extrapolation and elastic overshoot.
// A nonpositive duration completes immediately at dst.
float eval_tempo(int tempo, float src, float dst, float duration, float timer);

} // namespace omw
