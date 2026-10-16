// Timed value animation shared by the sprite properties (move, scale, rotate, fade, colours, ...).
#pragma once

#include <algorithm>
#include <array>

namespace omw {

// Tempo curve evaluator: value between src and dst at `timer` of `duration` (milliseconds). Installed by the script layer
// (the exact curve table of the original, see docs/tempo-table.md); linear until then.
using TempoFn = float (*)(int tempo, float src, float dst, float duration, float timer);
extern TempoFn g_tempo_eval;

template <int N>
struct Anim {
	std::array<float, N> value{}, src{}, dst{};
	float duration = 0, timer = 0;
	int tempo = 0;
	bool active = false;

	Anim() = default;
	explicit Anim(float v) { value.fill(v); src = dst = value; }

	// Starts moving from the current value to `to` in `time_ms`; a zero time jumps immediately.
	void start(const std::array<float, N>& to, float time_ms, int tempo_id) {
		src = value;
		dst = to;
		duration = time_ms;
		timer = 0;
		tempo = tempo_id;
		active = time_ms > 0;
		if (!active) value = dst;
	}
	void set(const std::array<float, N>& v) { value = src = dst = v; active = false; }
	void update(float dt_ms) {
		if (!active) return;
		timer = std::min(timer + dt_ms, duration);
		for (int i = 0; i < N; i++) value[i] = g_tempo_eval(tempo, src[i], dst[i], duration, timer);
		if (timer >= duration) { value = dst; active = false; }
	}
};

} // namespace omw
