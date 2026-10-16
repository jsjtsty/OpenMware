#pragma once

#include <SDL3/SDL_gpu.h>

namespace omw {

struct Texture {
	SDL_GPUTexture* gpu = nullptr;
	int w = 0, h = 0;
};

} // namespace omw
