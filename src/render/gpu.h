// Thin helpers over SDL_gpu: device/window setup, precompiled shader loading, texture upload.
#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace omw {

struct GpuContext {
	SDL_Window* window = nullptr;
	SDL_GPUDevice* device = nullptr;
	SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID; // the format we load shaders in
	const char* format_ext = ""; // file extension of that format (".spv", ".dxil", ".msl")
	std::filesystem::path shader_dir;
	void* metallib = nullptr; // Metal: shaders.metallib, read once and shared by all shaders
	size_t metallib_size = 0;

	bool init(const char* title, int width, int height, const std::filesystem::path& shader_dir, std::string* error);
	void shutdown();

	// Loads <shader_dir>/<name><ext> (compiled by the build). Returns nullptr on failure.
	SDL_GPUShader* load_shader(const std::string& name, SDL_GPUShaderStage stage, Uint32 samplers, Uint32 uniform_buffers);
	// Creates an RGBA8 texture (sampler usage) and uploads tightly packed pixels.
	SDL_GPUTexture* create_texture_rgba8(int w, int h, const void* pixels, bool render_target = false);
};

} // namespace omw
