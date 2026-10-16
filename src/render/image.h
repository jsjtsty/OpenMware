// Image decoding (WebP and PNG/JPEG) to RGBA8.
#pragma once

#include <cstdint>
#include <vector>

namespace omw {

struct Image {
	int w = 0, h = 0;
	std::vector<uint8_t> rgba; // w*h*4, straight alpha
};

bool decode_image(const std::vector<uint8_t>& file, Image& out);

} // namespace omw
