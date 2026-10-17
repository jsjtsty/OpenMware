#include "render/gpu.h"

#include <fstream>
#include <vector>

namespace omw {

bool GpuContext::init(const char* title, int width, int height, const std::filesystem::path& dir, std::string* error) {
	shader_dir = dir;
	if (!SDL_Init(SDL_INIT_VIDEO)) {
		if (error) *error = SDL_GetError();
		return false;
	}
	const SDL_GPUShaderFormat wanted = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
	device = SDL_CreateGPUDevice(wanted, false, nullptr);
	if (!device) {
		if (error) *error = SDL_GetError();
		return false;
	}
	const SDL_GPUShaderFormat have = SDL_GetGPUShaderFormats(device);
	if (have & SDL_GPU_SHADERFORMAT_SPIRV) { format = SDL_GPU_SHADERFORMAT_SPIRV; format_ext = ".spv"; }
	else if (have & SDL_GPU_SHADERFORMAT_DXIL) { format = SDL_GPU_SHADERFORMAT_DXIL; format_ext = ".dxil"; }
	// Metal: precompiled libraries are preferred (built by CMake on Apple hosts); the MSL source is compiled by the driver at load.
	else if (have & SDL_GPU_SHADERFORMAT_METALLIB) { format = SDL_GPU_SHADERFORMAT_METALLIB; format_ext = ".metallib"; }
	else if (have & SDL_GPU_SHADERFORMAT_MSL) { format = SDL_GPU_SHADERFORMAT_MSL; format_ext = ".msl"; }
	window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
	if (!window || !SDL_ClaimWindowForGPUDevice(device, window)) {
		if (error) *error = SDL_GetError();
		return false;
	}
	// Premultiplication-free SDR output; vsync keeps the 60 Hz pacing of the original.
	SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC);
	// Started by a launcher, the new window would otherwise open behind it without keyboard focus.
	SDL_RaiseWindow(window);
	return true;
}

void GpuContext::shutdown() {
	if (device && window) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (window) SDL_DestroyWindow(window);
	if (device) SDL_DestroyGPUDevice(device);
	window = nullptr;
	device = nullptr;
	SDL_Quit();
}

SDL_GPUShader* GpuContext::load_shader(const std::string& name, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniform_buffers) {
	// SDL_LoadFile also reads Android assets (the shaders ship inside the APK) and iOS/macOS bundle paths.
	size_t code_size = 0;
	const auto u8 = (shader_dir / (name + format_ext)).u8string(); // SDL takes UTF-8 paths
	void* code = SDL_LoadFile(std::string(u8.begin(), u8.end()).c_str(), &code_size);
	if (!code) return nullptr;
	SDL_GPUShaderCreateInfo info{};
	info.code = static_cast<const Uint8*>(code);
	info.code_size = code_size;
	info.entrypoint = (format == SDL_GPU_SHADERFORMAT_MSL || format == SDL_GPU_SHADERFORMAT_METALLIB) ? "main0" : "main";
	info.format = format;
	info.stage = stage;
	info.num_samplers = samplers;
	info.num_uniform_buffers = uniform_buffers;
	SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
	SDL_free(code);
	return shader;
}

SDL_GPUTexture* GpuContext::create_texture_rgba8(int w, int h, const void* pixels, bool render_target) {
	SDL_GPUTextureCreateInfo ti{};
	ti.type = SDL_GPU_TEXTURETYPE_2D;
	ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | (render_target ? SDL_GPU_TEXTUREUSAGE_COLOR_TARGET : 0);
	ti.width = static_cast<Uint32>(w);
	ti.height = static_cast<Uint32>(h);
	ti.layer_count_or_depth = 1;
	ti.num_levels = 1;
	SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &ti);
	if (!tex || !pixels) return tex;

	const Uint32 bytes = static_cast<Uint32>(w) * h * 4;
	SDL_GPUTransferBufferCreateInfo tb{SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, bytes, 0};
	SDL_GPUTransferBuffer* tbuf = SDL_CreateGPUTransferBuffer(device, &tb);
	void* map = SDL_MapGPUTransferBuffer(device, tbuf, false);
	SDL_memcpy(map, pixels, bytes);
	SDL_UnmapGPUTransferBuffer(device, tbuf);
	SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
	SDL_GPUCopyPass* cp = SDL_BeginGPUCopyPass(cmd);
	SDL_GPUTextureTransferInfo src{tbuf, 0, 0, 0};
	SDL_GPUTextureRegion dst{};
	dst.texture = tex;
	dst.w = static_cast<Uint32>(w);
	dst.h = static_cast<Uint32>(h);
	dst.d = 1;
	SDL_UploadToGPUTexture(cp, &src, &dst, false);
	SDL_EndGPUCopyPass(cp);
	SDL_SubmitGPUCommandBuffer(cmd);
	SDL_ReleaseGPUTransferBuffer(device, tbuf);
	return tex;
}

} // namespace omw
