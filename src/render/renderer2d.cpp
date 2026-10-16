#include "render/renderer2d.h"

#include "render/blend.h"
#include "render/image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace omw {
namespace {

struct SpriteUniforms { // cbuffer ConstantBuffer of vssprite (0x90 bytes)
	uint32_t use_color_override; float pad0[3];
	float color_override[4];
	float alpha_exponent; float pad1[3];
	float color_balance[4];
	float offset[4];
	float mT[16];
};
static_assert(sizeof(SpriteUniforms) == 0x90);

// Pixel shader of a blend mode (variant "normal": no mask/transition/displacement).
std::string ps_for_blend(int blend) {
	switch (blend) {
	case BLEND_MULTIPLE: return "pssprite_mul_normal";
	case BLEND_SCREEN: return "pssprite_screen_normal";
	default: return "pssprite_blend_normal"; // TODO: in-shader modes need the destination copy
	}
}

} // namespace

bool Renderer2D::init(GpuContext& gpu, const GameFiles& files, std::string* error) {
	gpu_ = &gpu;
	files_ = &files;
	vs_ = gpu.load_shader("vssprite.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	if (!vs_) {
		if (error) *error = std::string("cannot load vssprite: ") + SDL_GetError();
		return false;
	}
	SDL_GPUSamplerCreateInfo si{};
	si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
	si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler_linear_ = SDL_CreateGPUSampler(gpu.device, &si);

	SDL_GPUTextureCreateInfo ti{};
	ti.type = SDL_GPU_TEXTURETYPE_2D;
	ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ti.width = Stage::kWidth;
	ti.height = Stage::kHeight;
	ti.layer_count_or_depth = 1;
	ti.num_levels = 1;
	frame_ = SDL_CreateGPUTexture(gpu.device, &ti);

	const uint8_t px[4] = {255, 255, 255, 255};
	white_.gpu = gpu.create_texture_rgba8(1, 1, px);
	white_.w = white_.h = 1;
	return frame_ && sampler_linear_ && white_.gpu;
}

void Renderer2D::shutdown() {
	if (!gpu_ || !gpu_->device) return;
	for (auto& [k, p] : pipelines_) if (p) SDL_ReleaseGPUGraphicsPipeline(gpu_->device, p);
	for (auto& [k, s] : shaders_) if (s) SDL_ReleaseGPUShader(gpu_->device, s);
	for (auto& [k, t] : textures_) if (t->gpu) SDL_ReleaseGPUTexture(gpu_->device, t->gpu);
	for (auto& t : dynamic_) if (t->gpu) SDL_ReleaseGPUTexture(gpu_->device, t->gpu);
	dynamic_.clear();
	if (white_.gpu) SDL_ReleaseGPUTexture(gpu_->device, white_.gpu);
	if (vbuf_) SDL_ReleaseGPUBuffer(gpu_->device, vbuf_);
	if (vtransfer_) SDL_ReleaseGPUTransferBuffer(gpu_->device, vtransfer_);
	if (frame_) SDL_ReleaseGPUTexture(gpu_->device, frame_);
	if (sampler_linear_) SDL_ReleaseGPUSampler(gpu_->device, sampler_linear_);
	if (vs_) SDL_ReleaseGPUShader(gpu_->device, vs_);
	pipelines_.clear();
	shaders_.clear();
	textures_.clear();
}

Texture* Renderer2D::create_dynamic_texture(int w, int h, const uint8_t* rgba) {
	auto t = std::make_unique<Texture>();
	t->gpu = gpu_->create_texture_rgba8(w, h, rgba);
	t->w = w;
	t->h = h;
	dynamic_.push_back(std::move(t));
	return dynamic_.back().get();
}

void Renderer2D::replace_dynamic_texture(Texture* t, int w, int h, const uint8_t* rgba) {
	if (t->gpu) SDL_ReleaseGPUTexture(gpu_->device, t->gpu);
	t->gpu = gpu_->create_texture_rgba8(w, h, rgba);
	t->w = w;
	t->h = h;
}

void Renderer2D::destroy_dynamic_texture(Texture* t) {
	for (auto it = dynamic_.begin(); it != dynamic_.end(); ++it) {
		if (it->get() != t) continue;
		if (t->gpu) SDL_ReleaseGPUTexture(gpu_->device, t->gpu);
		dynamic_.erase(it);
		return;
	}
}

Texture* Renderer2D::texture(const std::string& path) {
	std::string key = path;
	for (char& c : key) {
		if (c == '\\') c = '/';
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	}
	if (const auto it = textures_.find(key); it != textures_.end()) return it->second->gpu ? it->second.get() : nullptr;
	auto tex = std::make_unique<Texture>();
	std::vector<uint8_t> file;
	Image img;
	if (files_->read(path, file) && decode_image(file, img)) {
		tex->gpu = gpu_->create_texture_rgba8(img.w, img.h, img.rgba.data());
		tex->w = img.w;
		tex->h = img.h;
		tex->alpha.resize(static_cast<size_t>(img.w) * img.h);
		for (size_t i = 0; i < tex->alpha.size(); i++) tex->alpha[i] = img.rgba[i * 4 + 3];
	} else {
		std::fprintf(stderr, "[texture] cannot load %s\n", path.c_str());
	}
	Texture* raw = tex.get();
	textures_[key] = std::move(tex);
	return raw->gpu ? raw : nullptr;
}

SDL_GPUGraphicsPipeline* Renderer2D::pipeline(int blend, const std::string& ps_name) {
	const auto key = std::make_pair(blend, ps_name);
	if (const auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
	SDL_GPUShader*& ps = shaders_[ps_name];
	if (!ps) ps = gpu_->load_shader(ps_name + ".frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
	if (!ps) {
		std::fprintf(stderr, "[render] missing pixel shader %s\n", ps_name.c_str());
		return pipelines_[key] = nullptr;
	}
	SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
	SDL_GPUVertexAttribute attrs[3] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 32},
	};
	SDL_GPUColorTargetDescription ct{};
	ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ct.blend_state = blend_state(blend);
	SDL_GPUGraphicsPipelineCreateInfo pi{};
	pi.vertex_shader = vs_;
	pi.fragment_shader = ps;
	pi.vertex_input_state = {&vb, 1, attrs, 3};
	pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pi.target_info = {&ct, 1, SDL_GPU_TEXTUREFORMAT_INVALID, false};
	return pipelines_[key] = SDL_CreateGPUGraphicsPipeline(gpu_->device, &pi);
}

void Renderer2D::ensure_vertex_buffer(size_t bytes) {
	if (bytes <= vcapacity_) return;
	if (vbuf_) SDL_ReleaseGPUBuffer(gpu_->device, vbuf_);
	if (vtransfer_) SDL_ReleaseGPUTransferBuffer(gpu_->device, vtransfer_);
	vcapacity_ = std::max<size_t>(bytes * 2, 1 << 16);
	SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<Uint32>(vcapacity_), 0};
	vbuf_ = SDL_CreateGPUBuffer(gpu_->device, &bi);
	SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, static_cast<Uint32>(vcapacity_), 0};
	vtransfer_ = SDL_CreateGPUTransferBuffer(gpu_->device, &ti);
}

void Renderer2D::draw_items(SDL_GPUCommandBuffer* cmd, const std::vector<DrawItem>& items) {
	verts_.clear();
	for (const DrawItem& d : items) {
		Vertex q[4];
		for (int i = 0; i < 4; i++)
			q[i] = {d.x[i] / (Stage::kWidth / 2.f) - 1.f, 1.f - d.y[i] / (Stage::kHeight / 2.f), 0.f, 1.f, d.r[i], d.g[i], d.b[i], d.a, d.u[i], d.v[i]};
		for (int i : {0, 1, 2, 0, 2, 3}) verts_.push_back(q[i]);
	}
	if (!verts_.empty()) {
		const size_t bytes = verts_.size() * sizeof(Vertex);
		ensure_vertex_buffer(bytes);
		void* map = SDL_MapGPUTransferBuffer(gpu_->device, vtransfer_, true);
		std::memcpy(map, verts_.data(), bytes);
		SDL_UnmapGPUTransferBuffer(gpu_->device, vtransfer_);
		SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTransferBufferLocation src{vtransfer_, 0};
		SDL_GPUBufferRegion dst{vbuf_, 0, static_cast<Uint32>(bytes)};
		SDL_UploadToGPUBuffer(cp, &src, &dst, true);
		SDL_EndGPUCopyPass(cp);
	}

	SDL_GPUColorTargetInfo ci{};
	ci.texture = frame_;
	ci.clear_color = {0.f, 0.f, 0.f, 1.f};
	ci.load_op = SDL_GPU_LOADOP_CLEAR;
	ci.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);

	SpriteUniforms u{};
	u.color_balance[0] = u.color_balance[1] = u.color_balance[2] = u.color_balance[3] = 1.f;
	const float mT[16] = {0.5f, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0, 1, 0, 0.5f, 0.5f, 0, 1}; // clip space -> texture space
	std::memcpy(u.mT, mT, sizeof(mT));
	SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));

	if (!verts_.empty()) {
		SDL_GPUBufferBinding bind{vbuf_, 0};
		SDL_BindGPUVertexBuffers(rp, 0, &bind, 1);
		size_t i = 0;
		while (i < items.size()) {
			size_t j = i + 1;
			const Texture* tex = items[i].texture ? items[i].texture : &white_;
			while (j < items.size() && items[j].blend == items[i].blend && (items[j].texture ? items[j].texture : &white_) == tex) j++;
			if (SDL_GPUGraphicsPipeline* p = pipeline(items[i].blend, ps_for_blend(items[i].blend))) {
				SDL_BindGPUGraphicsPipeline(rp, p);
				SDL_GPUTextureSamplerBinding ts{tex->gpu, sampler_linear_};
				SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
				SDL_DrawGPUPrimitives(rp, static_cast<Uint32>((j - i) * 6), 1, static_cast<Uint32>(i * 6), 0);
			}
			i = j;
		}
	}
	SDL_EndGPURenderPass(rp);
}

bool Renderer2D::render_frame(const std::vector<DrawItem>& items) {
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_->device);
	SDL_GPUTexture* swap = nullptr;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, gpu_->window, &swap, &sw, &sh) || !swap) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return false;
	}
	draw_items(cmd, items);
	// Letterbox the fixed frame into the window.
	const float scale = std::min(sw / static_cast<float>(Stage::kWidth), sh / static_cast<float>(Stage::kHeight));
	const float dw = Stage::kWidth * scale, dh = Stage::kHeight * scale;
	SDL_GPUBlitInfo bi{};
	bi.source.texture = frame_;
	bi.source.w = Stage::kWidth;
	bi.source.h = Stage::kHeight;
	bi.destination.texture = swap;
	bi.destination.x = static_cast<Uint32>((sw - dw) / 2);
	bi.destination.y = static_cast<Uint32>((sh - dh) / 2);
	bi.destination.w = static_cast<Uint32>(dw);
	bi.destination.h = static_cast<Uint32>(dh);
	bi.load_op = SDL_GPU_LOADOP_CLEAR;
	bi.clear_color = {0.f, 0.f, 0.f, 1.f};
	bi.filter = SDL_GPU_FILTER_LINEAR;
	SDL_BlitGPUTexture(cmd, &bi);
	SDL_SubmitGPUCommandBuffer(cmd);
	return true;
}

bool Renderer2D::save_frame_png(const std::vector<DrawItem>& items, const std::string& path) {
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_->device);
	draw_items(cmd, items);
	const Uint32 bytes = Stage::kWidth * Stage::kHeight * 4;
	SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, bytes, 0};
	SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu_->device, &tbi);
	SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureRegion reg{};
	reg.texture = frame_;
	reg.w = Stage::kWidth;
	reg.h = Stage::kHeight;
	reg.d = 1;
	SDL_GPUTextureTransferInfo dst{tb, 0, 0, 0};
	SDL_DownloadFromGPUTexture(cp, &reg, &dst);
	SDL_EndGPUCopyPass(cp);
	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
	SDL_WaitForGPUFences(gpu_->device, true, &fence, 1);
	SDL_ReleaseGPUFence(gpu_->device, fence);
	void* data = SDL_MapGPUTransferBuffer(gpu_->device, tb, false);
	SDL_Surface* surf = SDL_CreateSurfaceFrom(Stage::kWidth, Stage::kHeight, SDL_PIXELFORMAT_RGBA32, data, Stage::kWidth * 4);
	const bool ok = SDL_SavePNG(surf, path.c_str());
	SDL_DestroySurface(surf);
	SDL_UnmapGPUTransferBuffer(gpu_->device, tb);
	SDL_ReleaseGPUTransferBuffer(gpu_->device, tb);
	return ok;
}

} // namespace omw
