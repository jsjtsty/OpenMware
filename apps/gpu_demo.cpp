// gpu_demo: opens a 1024x576 window and draws two procedural sprites through the recovered sprite vertex shader.
#include "render/blend.h"
#include "render/gpu.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace omw;

struct Vertex {
	float x, y, z, w; // clip space (the sprite VS only adds g_Offset and projects with g_mT)
	float r, g, b, a;
	float u, v;
};

struct SpriteUniforms { // matches cbuffer ConstantBuffer of vssprite (0x90 bytes)
	uint32_t use_color_override; float pad0[3];
	float color_override[4];
	float alpha_exponent; float pad1[3];
	float color_balance[4];
	float offset[4];
	float mT[16];
};
static_assert(sizeof(SpriteUniforms) == 0x90);

int main(int argc, char** argv) {
	const std::filesystem::path shader_dir = argc > 1 ? argv[1] : "shaders";
	GpuContext gpu;
	std::string err;
	if (!gpu.init("OpenMware gpu_demo", 1024, 576, shader_dir, &err)) {
		std::fprintf(stderr, "init failed: %s\n", err.c_str());
		return 1;
	}
	std::printf("GPU driver: %s, shader format %s\n", SDL_GetGPUDeviceDriver(gpu.device), gpu.format_ext);

	SDL_GPUShader* vs = gpu.load_shader("vssprite.vert", SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
	SDL_GPUShader* ps = gpu.load_shader("pssprite_blend_normal.frag", SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);
	if (!vs || !ps) {
		std::fprintf(stderr, "shader load failed: %s\n", SDL_GetError());
		return 1;
	}

	SDL_GPUVertexBufferDescription vb{0, sizeof(Vertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0};
	SDL_GPUVertexAttribute attrs[3] = {
		{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 0},
		{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, 16},
		{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, 32},
	};
	SDL_GPUColorTargetDescription ct{};
	ct.format = SDL_GetGPUSwapchainTextureFormat(gpu.device, gpu.window);
	ct.blend_state = blend_state(BLEND_NORMAL);
	SDL_GPUGraphicsPipelineCreateInfo pi{};
	pi.vertex_shader = vs;
	pi.fragment_shader = ps;
	pi.vertex_input_state = {&vb, 1, attrs, 3};
	pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pi.target_info = {&ct, 1, SDL_GPU_TEXTUREFORMAT_INVALID, false};
	SDL_GPUGraphicsPipeline* pipe = SDL_CreateGPUGraphicsPipeline(gpu.device, &pi);
	ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	SDL_GPUGraphicsPipeline* pipe_rgba = SDL_CreateGPUGraphicsPipeline(gpu.device, &pi);
	if (!pipe) {
		std::fprintf(stderr, "pipeline failed: %s\n", SDL_GetError());
		return 1;
	}

	std::vector<uint8_t> px(256 * 256 * 4);
	for (int y = 0; y < 256; y++)
		for (int x = 0; x < 256; x++) {
			uint8_t* p = &px[(y * 256 + x) * 4];
			const bool checker = ((x / 32) + (y / 32)) & 1;
			const float dx = x - 128.f, dy = y - 128.f;
			p[0] = static_cast<uint8_t>(x);
			p[1] = static_cast<uint8_t>(y);
			p[2] = checker ? 255 : 64;
			p[3] = dx * dx + dy * dy < 120.f * 120.f ? 255 : 0; // round sprite
		}
	SDL_GPUTexture* tex = gpu.create_texture_rgba8(256, 256, px.data());
	SDL_GPUSamplerCreateInfo si{};
	si.min_filter = si.mag_filter = SDL_GPU_FILTER_LINEAR;
	si.address_mode_u = si.address_mode_v = si.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpu.device, &si);

	// Two 256x256 sprites in 1024x576 game pixels, converted to clip space on the CPU.
	auto quad = [](std::vector<Vertex>& out, float x, float y, float w, float h, float alpha) {
		const float x0 = x / 512.f - 1.f, x1 = (x + w) / 512.f - 1.f, y0 = 1.f - y / 288.f, y1 = 1.f - (y + h) / 288.f;
		const Vertex v[4] = {{x0, y0, 0, 1, 1, 1, 1, alpha, 0, 0}, {x1, y0, 0, 1, 1, 1, 1, alpha, 1, 0},
							 {x1, y1, 0, 1, 1, 1, 1, alpha, 1, 1}, {x0, y1, 0, 1, 1, 1, 1, alpha, 0, 1}};
		for (int i : {0, 1, 2, 0, 2, 3}) out.push_back(v[i]);
	};
	std::vector<Vertex> verts;
	quad(verts, 200, 100, 256, 256, 1.0f);
	quad(verts, 340, 180, 256, 256, 0.6f);
	SDL_GPUBufferCreateInfo bi{SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<Uint32>(verts.size() * sizeof(Vertex)), 0};
	SDL_GPUBuffer* vbuf = SDL_CreateGPUBuffer(gpu.device, &bi);
	{
		SDL_GPUTransferBufferCreateInfo ti{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bi.size, 0};
		SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu.device, &ti);
		std::memcpy(SDL_MapGPUTransferBuffer(gpu.device, tb, false), verts.data(), bi.size);
		SDL_UnmapGPUTransferBuffer(gpu.device, tb);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
		SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTransferBufferLocation src{tb, 0};
		SDL_GPUBufferRegion dst{vbuf, 0, bi.size};
		SDL_UploadToGPUBuffer(cp, &src, &dst, false);
		SDL_EndGPUCopyPass(cp);
		SDL_SubmitGPUCommandBuffer(cmd);
		SDL_ReleaseGPUTransferBuffer(gpu.device, tb);
	}

	// g_mT maps clip space to texture space for the destination/mask lookups (row-vector convention).
	SpriteUniforms u{};
	u.color_balance[0] = u.color_balance[1] = u.color_balance[2] = u.color_balance[3] = 1.f;
	const float mT[16] = {0.5f, 0, 0, 0, 0, -0.5f, 0, 0, 0, 0, 1, 0, 0.5f, 0.5f, 0, 1};
	std::memcpy(u.mT, mT, sizeof(mT));

	if (argc > 3 && std::string(argv[2]) == "--shot") { // render one frame offscreen and save it as PNG
		SDL_GPUTextureCreateInfo ti{};
		ti.type = SDL_GPU_TEXTURETYPE_2D;
		ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
		ti.width = 1024; ti.height = 576; ti.layer_count_or_depth = 1; ti.num_levels = 1;
		SDL_GPUTexture* target = SDL_CreateGPUTexture(gpu.device, &ti);
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
		SDL_GPUColorTargetInfo c{};
		c.texture = target;
		c.clear_color = {0.1f, 0.12f, 0.2f, 1.f};
		c.load_op = SDL_GPU_LOADOP_CLEAR;
		c.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &c, 1, nullptr);
		SDL_BindGPUGraphicsPipeline(rp, pipe_rgba);
		SDL_GPUBufferBinding bind{vbuf, 0};
		SDL_BindGPUVertexBuffers(rp, 0, &bind, 1);
		SDL_GPUTextureSamplerBinding ts{tex, sampler};
		SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
		SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
		SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(verts.size()), 1, 0, 0);
		SDL_EndGPURenderPass(rp);
		SDL_GPUTransferBufferCreateInfo tbi{SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, 1024 * 576 * 4, 0};
		SDL_GPUTransferBuffer* tb = SDL_CreateGPUTransferBuffer(gpu.device, &tbi);
		SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
		SDL_GPUTextureRegion reg{};
		reg.texture = target; reg.w = 1024; reg.h = 576; reg.d = 1;
		SDL_GPUTextureTransferInfo dst{tb, 0, 0, 0};
		SDL_DownloadFromGPUTexture(cp, &reg, &dst);
		SDL_EndGPUCopyPass(cp);
		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
		SDL_WaitForGPUFences(gpu.device, true, &fence, 1);
		SDL_ReleaseGPUFence(gpu.device, fence);
		void* data = SDL_MapGPUTransferBuffer(gpu.device, tb, false);
		SDL_Surface* surf = SDL_CreateSurfaceFrom(1024, 576, SDL_PIXELFORMAT_RGBA32, data, 1024 * 4);
		const bool ok = SDL_SavePNG(surf, argv[3]);
		std::printf("saved %s: %s\n", argv[3], ok ? "ok" : SDL_GetError());
		SDL_DestroySurface(surf);
		SDL_UnmapGPUTransferBuffer(gpu.device, tb);
		gpu.shutdown();
		return ok ? 0 : 1;
	}

	const int max_frames = argc > 2 ? std::atoi(argv[2]) : -1;
	for (int frame = 0; max_frames < 0 || frame < max_frames;) {
		SDL_Event e;
		bool quit = false;
		while (SDL_PollEvent(&e)) quit |= e.type == SDL_EVENT_QUIT;
		if (quit) break;
		SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(gpu.device);
		SDL_GPUTexture* swap = nullptr;
		Uint32 sw, sh;
		if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, gpu.window, &swap, &sw, &sh) || !swap) {
			SDL_SubmitGPUCommandBuffer(cmd);
			continue;
		}
		SDL_GPUColorTargetInfo c{};
		c.texture = swap;
		c.clear_color = {0.1f, 0.12f, 0.2f, 1.f};
		c.load_op = SDL_GPU_LOADOP_CLEAR;
		c.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &c, 1, nullptr);
		SDL_BindGPUGraphicsPipeline(rp, pipe);
		// Letterbox the fixed 1024x576 image into the window.
		const float scale = std::fmin(sw / 1024.f, sh / 576.f);
		SDL_GPUViewport vp{(sw - 1024 * scale) / 2, (sh - 576 * scale) / 2, 1024 * scale, 576 * scale, 0, 1};
		SDL_SetGPUViewport(rp, &vp);
		SDL_GPUBufferBinding bind{vbuf, 0};
		SDL_BindGPUVertexBuffers(rp, 0, &bind, 1);
		SDL_GPUTextureSamplerBinding ts{tex, sampler};
		SDL_BindGPUFragmentSamplers(rp, 0, &ts, 1);
		SDL_PushGPUVertexUniformData(cmd, 0, &u, sizeof(u));
		SDL_DrawGPUPrimitives(rp, static_cast<Uint32>(verts.size()), 1, 0, 0);
		SDL_EndGPURenderPass(rp);
		SDL_SubmitGPUCommandBuffer(cmd);
		frame++;
	}
	SDL_ReleaseGPUBuffer(gpu.device, vbuf);
	SDL_ReleaseGPUSampler(gpu.device, sampler);
	SDL_ReleaseGPUTexture(gpu.device, tex);
	SDL_ReleaseGPUGraphicsPipeline(gpu.device, pipe);
	SDL_ReleaseGPUShader(gpu.device, vs);
	SDL_ReleaseGPUShader(gpu.device, ps);
	gpu.shutdown();
	return 0;
}
