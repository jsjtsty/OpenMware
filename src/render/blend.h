// Engine blend modes (BLEND_MODE_* script constants) and their D3D blend states, recovered from Mware.dll (docs/render-notes.md).
#pragma once

#include <SDL3/SDL_gpu.h>

namespace omw {

enum BlendMode : int {
	BLEND_DISABLE = 0,
	BLEND_NORMAL,
	BLEND_ADD,
	BLEND_SUB,
	BLEND_MULTIPLE,
	BLEND_SCREEN,
	BLEND_OVERLAY,
	BLEND_COLOR_DODGE,
	BLEND_LINEAR_DODGE,
	BLEND_COLOR_BURN,
	BLEND_LINEAR_BURN,
	BLEND_SOFT_LIGHT,
	BLEND_HARD_LIGHT,
	BLEND_VIVID_LIGHT,
	BLEND_LINEAR_LIGHT,
	BLEND_PIN_LIGHT,
	BLEND_HARD_MIX,
	BLEND_DARKEN,
	BLEND_LIGHTEN,
	BLEND_DIFFERENCE,
	BLEND_EXCLUSION,
	BLEND_MODE_COUNT,
};

// True when the pixel shader of this mode samples the destination texture (t7) itself.
constexpr bool blend_reads_destination(int mode) { return mode >= BLEND_OVERLAY && mode < BLEND_MODE_COUNT; }

// Fixed-function state. Alpha channel is always ADD ONE/ONE and colour writes are RGB only (as in the original).
// `to_target`: drawing into an offscreen surface that has to keep its own alpha (it then holds premultiplied colour);
// `premult`: the source is such a surface (premultiplied), composited with ONE / INV_SRC_ALPHA (NORMAL blend only).
inline SDL_GPUColorTargetBlendState blend_state(int mode, bool to_target = false, bool premult = false) {
	SDL_GPUColorTargetBlendState s{};
	s.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B;
	s.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	s.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	s.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	s.color_blend_op = SDL_GPU_BLENDOP_ADD;
	s.enable_blend = true;
	switch (mode) {
	case BLEND_DISABLE: s.enable_blend = false; break;
	case BLEND_ADD:
		s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		break;
	case BLEND_SUB:
		s.color_blend_op = SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
		s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		break;
	case BLEND_MULTIPLE:
		s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_DST_COLOR;
		s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
		break;
	case BLEND_SCREEN:
		s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
		s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		break;
	default: // NORMAL and all in-shader modes
		s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		s.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		if (premult && mode == BLEND_NORMAL) s.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		break;
	}
	if (to_target) {
		s.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G | SDL_GPU_COLORCOMPONENT_B | SDL_GPU_COLORCOMPONENT_A;
		s.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		s.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	}
	return s;
}

} // namespace omw
