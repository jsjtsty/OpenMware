#include "game/anim.h"

namespace omw {

TempoFn g_tempo_eval = [](int, float src, float dst, float duration, float timer) -> float {
	return duration > 0.f ? src + (dst - src) * (timer / duration) : dst;
};

} // namespace omw
