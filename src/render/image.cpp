#include "render/image.h"

#include <webp/decode.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#include <cstring>

namespace omw {

bool decode_image(const std::vector<uint8_t>& file, Image& out) {
	if (file.size() >= 12 && std::memcmp(file.data(), "RIFF", 4) == 0 && std::memcmp(file.data() + 8, "WEBP", 4) == 0) {
		int w = 0, h = 0;
		uint8_t* px = WebPDecodeRGBA(file.data(), file.size(), &w, &h);
		if (!px) return false;
		out.w = w;
		out.h = h;
		out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
		WebPFree(px);
		return true;
	}
	int w = 0, h = 0, comp = 0;
	uint8_t* px = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &comp, 4);
	if (!px) return false;
	out.w = w;
	out.h = h;
	out.rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
	stbi_image_free(px);
	return true;
}

} // namespace omw
