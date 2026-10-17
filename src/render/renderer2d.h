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
	int supersample() const { return ss_; }
	// Offscreen surface the stage draws sprites into (SQSpriteRenderTarget); freed with destroy_dynamic_texture.
	Texture* create_target(int w, int h);

	// Renders the draw list and presents. Returns false when the window is minimised (no frame was produced).
	bool render_frame(const DrawList& list);
	// Renders only into the offscreen game frame and saves it as PNG (for tests/screenshots).
	bool save_frame_png(const DrawList& list, const std::string& path);
	// Reads a texture back, scales it to w x h and saves it as PNG (save-file thumbnails).
	bool save_texture_png(Texture* t, int w, int h, const std::string& path);

private:
	struct Vertex {
		float x, y, z, w;
		float r, g, b, a;
		float u, v;
	};
	// Helper textures of a pass target size: picture of the current sprite's mask, and a copy of the target so far
	// (blend modes that read the destination, screen/capture sprites).
	struct Scratch {
		SDL_GPUTexture* mask = nullptr;
		SDL_GPUTexture* dest = nullptr;
	};
	SDL_GPUGraphicsPipeline* pipeline(int blend, const std::string& ps_name, Uint32 samplers, bool to_target, bool premult);
	Texture* make_rule_texture(float now, float rate);
	Scratch& scratch(int w, int h);
	Texture* temp_texture(int slot, int w, int h);
	SDL_GPUGraphicsPipeline* fx_pipeline(int kind); // 0 colour, 1 blur, 2 warp
	void draw_passes(SDL_GPUCommandBuffer* cmd, const DrawList& list);
	void ensure_vertex_buffer(size_t bytes);

	GpuContext* gpu_ = nullptr;
	const GameFiles* files_ = nullptr;
	SDL_GPUShader* vs_ = nullptr;
	SDL_GPUSampler* sampler_linear_ = nullptr;
	SDL_GPUSampler* sampler_point_ = nullptr;
	std::vector<SDL_GPUTexture*> fx_params_;           // per effect pass of a frame: 4x1 float texture of its parameters
	SDL_GPUTransferBuffer* ptransfer_ = nullptr;
	size_t ptransfer_capacity_ = 0;
	int ss_ = 2; // the game frame is rendered at ss_ x the logical 1024x576 and scaled down to the window
	SDL_GPUTexture* frame_ = nullptr; // offscreen game frame
	std::map<std::pair<int, int>, Scratch> scratch_;
	std::vector<Texture*> temps_;                      // temporary textures of the effect chains, by slot
	SDL_GPUGraphicsPipeline* fx_pipes_[3] = {};        // [0] colour, [1] blur, [2] warp
	Texture* rule_ = nullptr;         // transition rule ramp
	SDL_GPUBuffer* vbuf_ = nullptr;
	SDL_GPUTransferBuffer* vtransfer_ = nullptr;
	size_t vcapacity_ = 0;
	Texture white_;
	std::vector<std::unique_ptr<Texture>> dynamic_;
	FontLibrary fonts_;
	std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
	std::unordered_map<std::string, long long> disk_stamp_; // file:/// textures: modification time they were loaded at
	std::map<std::pair<int, std::string>, SDL_GPUGraphicsPipeline*> pipelines_; // key: blend | flags << 8
	std::map<std::string, SDL_GPUShader*> shaders_;
	std::vector<Vertex> verts_;
};

} // namespace omw
