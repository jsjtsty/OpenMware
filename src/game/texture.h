#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

namespace omw {

struct Texture {
	SDL_GPUTexture* gpu = nullptr;
	int w = 0, h = 0;
	std::vector<uint8_t> alpha; // w*h alpha values of loaded images (click hit testing); empty for dynamic textures
	// Capture surfaces: how often the renderer has copied the frame into it. Until the first copy the GPU memory holds
	// nothing defined (garbage on Metal), so a capture must not freeze before that.
	uint32_t copies = 0;
};

} // namespace omw
