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

// Pixel shader family of a blend mode (ADD/SUB/DISABLE reuse "blend" with another fixed-function state).
const char* ps_family(int blend) {
	switch (blend) {
	case BLEND_MULTIPLE: return "mul";
	case BLEND_SCREEN: return "screen";
	case BLEND_OVERLAY: return "overlay";
	case BLEND_COLOR_DODGE: return "colordodge";
	case BLEND_LINEAR_DODGE: return "lineardodge";
	case BLEND_COLOR_BURN: return "colorburn";
	case BLEND_LINEAR_BURN: return "linearburn";
	case BLEND_SOFT_LIGHT: return "softlight";
	case BLEND_HARD_LIGHT: return "hardlight";
	case BLEND_VIVID_LIGHT: return "vividlight";
	case BLEND_LINEAR_LIGHT: return "linearlight";
	case BLEND_PIN_LIGHT: return "pinlight";
	case BLEND_HARD_MIX: return "hardmix";
	case BLEND_DARKEN: return "darken";
	case BLEND_LIGHTEN: return "lighten";
	case BLEND_DIFFERENCE: return "difference";
	case BLEND_EXCLUSION: return "exclusion";
	default: return "blend";
	}
}

std::string ps_for(int blend, bool mask, bool transition) {
	std::string v = mask ? (transition ? "masktransition" : "mask") : (transition ? "transition" : "normal");
	return std::string("pssprite_") + ps_family(blend) + "_" + v;
}

// Highest fragment texture register of a variant + 1 (the shaders bind t0, t2 mask, t3/t4 transition, t7 destination).
Uint32 sampler_count(int blend, bool mask, bool transition) {
	if (blend_reads_destination(blend)) return 8;
	return transition ? 5 : mask ? 3 : 1;
}

float clamp01(float v) { return v < 0.f ? 0.f : v > 1.f ? 1.f : v; }

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
	scratch(Stage::kWidth, Stage::kHeight);

	const uint8_t px[4] = {255, 255, 255, 255};
	white_.gpu = gpu.create_texture_rgba8(1, 1, px);
	white_.w = white_.h = 1;
	const std::vector<uint8_t> ramp(256 * 4, 255);
	rule_ = create_dynamic_texture(256, 1, ramp.data());
	return frame_ && scratch_.begin()->second.mask && sampler_linear_ && white_.gpu && rule_;
}

Renderer2D::Scratch& Renderer2D::scratch(int w, int h) {
	Scratch& s = scratch_[{w, h}];
	if (!s.mask) {
		SDL_GPUTextureCreateInfo ti{};
		ti.type = SDL_GPU_TEXTURETYPE_2D;
		ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		ti.width = static_cast<Uint32>(w);
		ti.height = static_cast<Uint32>(h);
		ti.layer_count_or_depth = 1;
		ti.num_levels = 1;
		s.mask = SDL_CreateGPUTexture(gpu_->device, &ti);
		ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		s.dest = SDL_CreateGPUTexture(gpu_->device, &ti);
	}
	return s;
}

Texture* Renderer2D::create_target(int w, int h) {
	auto t = std::make_unique<Texture>();
	SDL_GPUTextureCreateInfo ti{};
	ti.type = SDL_GPU_TEXTURETYPE_2D;
	ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	ti.width = static_cast<Uint32>(w);
	ti.height = static_cast<Uint32>(h);
	ti.layer_count_or_depth = 1;
	ti.num_levels = 1;
	t->gpu = SDL_CreateGPUTexture(gpu_->device, &ti);
	t->w = w;
	t->h = h;
	if (!t->gpu) return nullptr;
	dynamic_.push_back(std::move(t));
	return dynamic_.back().get();
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
	for (auto& [k, s] : scratch_) {
		if (s.mask) SDL_ReleaseGPUTexture(gpu_->device, s.mask);
		if (s.dest) SDL_ReleaseGPUTexture(gpu_->device, s.dest);
	}
	scratch_.clear();
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

SDL_GPUGraphicsPipeline* Renderer2D::pipeline(int blend, const std::string& ps_name, Uint32 samplers, bool to_target, bool premult) {
	const auto key = std::make_pair(blend | (to_target ? 1 << 8 : 0) | (premult ? 1 << 9 : 0), ps_name);
	if (const auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
	SDL_GPUShader*& ps = shaders_[ps_name];
	if (!ps) ps = gpu_->load_shader(ps_name + ".frag", SDL_GPU_SHADERSTAGE_FRAGMENT, samplers, 0);
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
	ct.blend_state = blend_state(blend, to_target, premult);
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

// The transition rule ramp (what psUpdateTransitionRule renders): alpha as a function of the transition image value.
Texture* Renderer2D::make_rule_texture(float now, float rate) {
	uint8_t px[256 * 4];
	for (int i = 0; i < 256; i++) {
		const float x = static_cast<float>(i) / 255.f;
		float value;
		if (rate <= 0.f) {
			value = now >= x ? 1.f : 0.f; // psUpdateTransitionRule_Rate0
		} else {
			const float slope = 2.f / rate;
			const float intercept = slope * now;
			const float edge = 1.f - now * (1.f / slope + 1.f);
			value = clamp01(x * (-slope * edge - intercept) + intercept);
		}
		const uint8_t b = static_cast<uint8_t>(value * 255.f + 0.5f);
		px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = b;
		px[i * 4 + 3] = 255;
	}
	replace_dynamic_texture(rule_, 256, 1, px);
	return rule_;
}

void Renderer2D::draw_passes(SDL_GPUCommandBuffer* cmd, const DrawList& list) {
	// Vertices: per pass, every item's quad, then the mask quads of the masked items.
	verts_.clear();
	std::vector<size_t> pass_base(list.size());
	std::vector<std::vector<size_t>> mask_first(list.size());
	for (size_t pi = 0; pi < list.size(); pi++) {
		const RenderPass& ps = list[pi];
		const float w = static_cast<float>(ps.w), h = static_cast<float>(ps.h);
		auto push_quad = [&](const DrawQuad& d, const DrawItem* item) {
			Vertex q[4];
			for (int i = 0; i < 4; i++) {
				float r = d.r[i], g = d.g[i], b = d.b[i], u = d.u[i], v = d.v[i];
				if (item && item->premult) { // premultiplied source: the fade scales the colour as well
					r *= d.a;
					g *= d.a;
					b *= d.a;
				}
				if (item && item->snap == DrawItem::Snap::Screen) { // UV: the captured region of the target
					u = (item->rx + u * item->rw) / w;
					v = (item->ry + v * item->rh) / h;
				}
				q[i] = {d.x[i] / (w / 2.f) - 1.f, 1.f - d.y[i] / (h / 2.f), 0.f, 1.f, r, g, b, d.a, u, v};
			}
			for (int i : {0, 1, 2, 0, 2, 3}) verts_.push_back(q[i]);
		};
		pass_base[pi] = verts_.size();
		for (const DrawItem& d : ps.items) push_quad(d, &d);
		mask_first[pi].assign(ps.items.size(), 0);
		for (size_t i = 0; i < ps.items.size(); i++) {
			if (!ps.items[i].has_mask) continue;
			mask_first[pi][i] = verts_.size();
			DrawQuad m = ps.items[i].mask;
			m.a = 1.f; // the mask picture is stored as is; the sprite's own fade does not dim it
			push_quad(m, nullptr);
		}
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

	SpriteUniforms u{};
	u.color_balance[0] = u.color_balance[1] = u.color_balance[2] = u.color_balance[3] = 1.f;
	// clip space -> texture space, stored for HLSL's default column-major cbuffer matrices (mul(v, M) with a row vector)
	const float mT[16] = {0.5f, 0, 0, 0.5f, 0, -0.5f, 0, 0.5f, 0, 0, 1, 0, 0, 0, 0, 1};
	std::memcpy(u.mT, mT, sizeof(mT));

	SDL_GPURenderPass* rp = nullptr;
	auto begin_pass = [&](SDL_GPUTexture* target, bool clear, float clear_a) {
		SDL_GPUColorTargetInfo ci{};
		ci.texture = target;
		ci.clear_color = {0.f, 0.f, 0.f, clear_a};
		ci.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		ci.store_op = SDL_GPU_STOREOP_STORE;
		rp = SDL_BeginGPURenderPass(cmd, &ci, 1, nullptr);
		SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
		if (!verts_.empty()) {
			SDL_GPUBufferBinding bind{vbuf_, 0};
			SDL_BindGPUVertexBuffers(rp, 0, &bind, 1);
		}
	};
	auto end_pass = [&] {
		SDL_EndGPURenderPass(rp);
		rp = nullptr;
	};
	auto tex_of = [&](const Texture* t) { return t ? t : &white_; };
	auto copy_texture = [&](SDL_GPUTexture* from, SDL_GPUTexture* to, int w, int h) {
		SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureLocation a{from, 0, 0, 0, 0, 0}, b{to, 0, 0, 0, 0, 0};
		SDL_CopyGPUTextureToTexture(cp, &a, &b, static_cast<Uint32>(w), static_cast<Uint32>(h), 1, false);
		SDL_EndGPUCopyPass(cp);
	};

	for (size_t pi = 0; pi < list.size(); pi++) {
		const RenderPass& ps = list[pi];
		const std::vector<DrawItem>& items = ps.items;
		const bool to_target = ps.target != nullptr;
		SDL_GPUTexture* target = to_target ? ps.target->gpu : frame_;
		Scratch& sc = scratch(ps.w, ps.h);
		const size_t base = pass_base[pi];
		auto is_plain = [&](const DrawItem& d) {
			return !d.has_mask && !d.trans_tex && !blend_reads_destination(d.blend) && d.snap == DrawItem::Snap::None;
		};

		begin_pass(target, true, to_target ? 0.f : 1.f);
		size_t i = 0;
		while (i < items.size()) {
			const DrawItem& it = items[i];
			const bool reads_dest = blend_reads_destination(it.blend);
			if (is_plain(it)) { // batch consecutive plain quads with the same state and texture
				size_t j = i + 1;
				const Texture* tex = tex_of(it.texture);
				while (j < items.size() && is_plain(items[j]) && items[j].blend == it.blend && items[j].premult == it.premult &&
					   tex_of(items[j].texture) == tex)
					j++;
				if (SDL_GPUGraphicsPipeline* p = pipeline(it.blend, ps_for(it.blend, false, false), 1, to_target, it.premult)) {
					SDL_BindGPUGraphicsPipeline(rp, p);
					SDL_GPUTextureSamplerBinding ts{tex->gpu, sampler_linear_};
					SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
					SDL_DrawGPUPrimitives(rp, static_cast<Uint32>((j - i) * 6), 1, static_cast<Uint32>(base + i * 6), 0);
				}
				i = j;
				continue;
			}

			// A sprite with a mask picture, a destination read or a snapshot of the target: helper work between passes.
			const bool snap_screen = it.snap == DrawItem::Snap::Screen, snap_capture = it.snap == DrawItem::Snap::Capture;
			if (it.has_mask || reads_dest || snap_screen || snap_capture) {
				end_pass();
				if (it.has_mask) {
					begin_pass(sc.mask, true, 0.f);
					if (SDL_GPUGraphicsPipeline* p = pipeline(BLEND_DISABLE, ps_for(BLEND_NORMAL, false, false), 1, false, false)) {
						SDL_BindGPUGraphicsPipeline(rp, p);
						SDL_GPUTextureSamplerBinding ts{tex_of(it.mask.texture)->gpu, sampler_linear_};
						SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
						SDL_DrawGPUPrimitives(rp, 6, 1, static_cast<Uint32>(mask_first[pi][i]), 0);
					}
					end_pass();
				}
				if (reads_dest || snap_screen) copy_texture(target, sc.dest, ps.w, ps.h);
				if (snap_capture && it.texture && it.texture->gpu)
					copy_texture(target, it.texture->gpu, std::min(ps.w, it.texture->w), std::min(ps.h, it.texture->h));
				begin_pass(target, false, 1.f);
			}

			const bool trans = it.trans_tex != nullptr;
			const Uint32 n = sampler_count(it.blend, it.has_mask, trans);
			std::string name = ps_for(it.blend, it.has_mask, trans);
			if (SDL_GPUGraphicsPipeline* p = pipeline(it.blend, name, n, to_target, it.premult)) {
				SDL_GPUTextureSamplerBinding slots[8];
				for (auto& s : slots) s = {white_.gpu, sampler_linear_};
				slots[0].texture = snap_screen ? sc.dest : tex_of(it.texture)->gpu;
				slots[2].texture = sc.mask;
				if (trans) {
					slots[3].texture = it.trans_tex->gpu;
					slots[4].texture = make_rule_texture(it.trans_now, it.trans_rate)->gpu;
				}
				slots[7].texture = sc.dest;
				SDL_BindGPUGraphicsPipeline(rp, p);
				SDL_BindGPUFragmentSamplers(rp, 0, slots, n);
				SDL_DrawGPUPrimitives(rp, 6, 1, static_cast<Uint32>(base + i * 6), 0);
			}
			i++;
		}
		end_pass();
	}
}

bool Renderer2D::render_frame(const DrawList& list) {
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_->device);
	SDL_GPUTexture* swap = nullptr;
	Uint32 sw = 0, sh = 0;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, gpu_->window, &swap, &sw, &sh) || !swap) {
		SDL_SubmitGPUCommandBuffer(cmd);
		return false;
	}
	draw_passes(cmd, list);
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

bool Renderer2D::save_frame_png(const DrawList& list, const std::string& path) {
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu_->device);
	draw_passes(cmd, list);
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
