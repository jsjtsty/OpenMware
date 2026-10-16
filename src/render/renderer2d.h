// Draws the stage's quads into the fixed 1024x576 game frame and presents it letterboxed into the window.
#pragma once

#include "game/game_files.h"
#include "game/stage.h"
#include "game/texture.h"
#include "render/gpu.h"
#include "render/text_raster.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace omw {

class Renderer2D {
public:
	bool init(GpuContext& gpu, const GameFiles& files, std::string* error);
	void shutdown();

	// Cached texture from an archive path (webp/png/jpg); nullptr when it cannot be loaded.
	Texture* texture(const std::string& path);
	Texture* white() { return &white_; }

	// Textures generated at run time (text bitmaps). The renderer owns them; `replace` swaps the pixels and size in place.
	Texture* create_dynamic_texture(int w, int h, const uint8_t* rgba);
	void replace_dynamic_texture(Texture* t, int w, int h, const uint8_t* rgba);
	void destroy_dynamic_texture(Texture* t);
	FontLibrary& fonts() { return fonts_; }

	// Renders the draw list and presents. Returns false when the window is minimised (no frame was produced).
	bool render_frame(const std::vector<DrawItem>& items);
	// Renders only into the offscreen game frame and saves it as PNG (for tests/screenshots).
	bool save_frame_png(const std::vector<DrawItem>& items, const std::string& path);

private:
	struct Vertex {
		float x, y, z, w;
		float r, g, b, a;
		float u, v;
	};
	SDL_GPUGraphicsPipeline* pipeline(int blend, const std::string& ps_name, Uint32 samplers);
	Texture* make_rule_texture(float now, float rate);
	void draw_items(SDL_GPUCommandBuffer* cmd, const std::vector<DrawItem>& items);
	void ensure_vertex_buffer(size_t bytes);

	GpuContext* gpu_ = nullptr;
	const GameFiles* files_ = nullptr;
	SDL_GPUShader* vs_ = nullptr;
	SDL_GPUSampler* sampler_linear_ = nullptr;
	SDL_GPUTexture* frame_ = nullptr; // offscreen game frame
	SDL_GPUTexture* mask_ = nullptr;  // screen-space picture of the current sprite's mask
	SDL_GPUTexture* dest_ = nullptr;  // copy of the frame so far, for blend modes that read the destination
	Texture* rule_ = nullptr;         // transition rule ramp
	SDL_GPUBuffer* vbuf_ = nullptr;
	SDL_GPUTransferBuffer* vtransfer_ = nullptr;
	size_t vcapacity_ = 0;
	Texture white_;
	std::vector<std::unique_ptr<Texture>> dynamic_;
	FontLibrary fonts_;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
	std::map<std::pair<int, std::string>, SDL_GPUGraphicsPipeline*> pipelines_;
	std::map<std::string, SDL_GPUShader*> shaders_;
	std::vector<Vertex> verts_;
};

} // namespace omw
