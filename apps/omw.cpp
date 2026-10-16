// omw: runs the game. Usage: omw GAME_DIR [--keys FILE] [--shaders DIR] [--frames N] [--shot FILE]
//   --frames N  stop after N frames;  --shot FILE  with --frames: save the last frame as PNG (offscreen, no presentation)
#include "audio/audio.h"
#include "game/game_files.h"
#include "game/input.h"
#include "game/stage.h"
#include "render/gpu.h"
#include "render/renderer2d.h"
#include "script/host.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace omw;

int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0); // keep diagnostics visible if the process crashes
	if (argc < 2) {
		std::fputs("usage: omw GAME_DIR [--keys FILE] [--shaders DIR] [--frames N] [--shot FILE]\n", stderr);
		return 2;
	}
	std::filesystem::path game_dir = argv[1], keys = "keys.local.txt", shaders = "shaders";
	int frames = -1;
	bool trace = false;
	std::string shot;
	std::vector<std::pair<int, std::string>> evals; // --eval FRAME:CODE (run a script snippet at a frame)
	std::vector<std::pair<int, int>> taps; // --press FRAME:KEYCODE (scripted key taps for tests)
	for (int i = 2; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--keys" && i + 1 < argc) keys = argv[++i];
		else if (a == "--shaders" && i + 1 < argc) shaders = argv[++i];
		else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
		else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
		else if (a == "--trace") trace = true;
		else if (a == "--eval" && i + 1 < argc) {
			const std::string spec = argv[++i];
			evals.emplace_back(std::atoi(spec.c_str()), spec.substr(spec.find(':') + 1));
		}
		else if (a == "--press" && i + 1 < argc) {
			int f = 0, k = 0;
			if (std::sscanf(argv[++i], "%d:%d", &f, &k) == 2) taps.emplace_back(f, k);
		}
	}

	std::string err;
	GameFiles files;
	if (!files.open(game_dir, keys, &err)) {
		std::fprintf(stderr, "cannot open game files: %s\n", err.c_str());
		return 1;
	}
	GpuContext gpu;
	if (!gpu.init("OpenMware", Stage::kWidth, Stage::kHeight, shaders, &err)) {
		std::fprintf(stderr, "init failed: %s\n", err.c_str());
		return 1;
	}
	Renderer2D renderer;
	if (!renderer.init(gpu, files, &err)) {
		std::fprintf(stderr, "renderer init failed: %s\n", err.c_str());
		return 1;
	}

	renderer.fonts().set_files(&files);
	AudioEngine audio;
	if (!std::getenv("OMW_NOAUDIO") && !audio.init()) std::fprintf(stderr, "no audio device (%s); sound runs silently\n", SDL_GetError());
	Stage stage;
	ScriptHost host(*files.archive("script"));
	host.stage = &stage;
	host.renderer = &renderer;
	host.audio = &audio;
	host.files = &files;
	host.enable_discovery();
	host.verbose = trace;
	if (!host.import("media/script/main.nut", &err)) {
		std::fprintf(stderr, "boot failed: %s\n", err.c_str());
		return 1;
	}

	InputState input;
	host.input = &input;
	std::vector<DrawItem> items;
	bool running = true;
	for (int frame = 0; running && (frames < 0 || frame < frames); frame++) {
		for (const auto& [f, k] : taps)
			if (f == frame) input.tap_key(k);
		for (const auto& [f, code] : evals)
			if (f == frame) host.debug_eval(code.c_str());
		input.poll(gpu.window, Stage::kWidth, Stage::kHeight, 1000.f / 60.f);
		if (input.quit) running = false;
		if (!host.resume_frame(&err)) {
			std::fprintf(stderr, "script error at frame %d: %s\n", frame, err.c_str());
			break;
		}
		stage.update(1000.f / 60.f);
		audio.update(1000.f / 60.f);
		items.clear();
		stage.build_draw_list(items);
		const bool last = frames >= 0 && frame == frames - 1;
		if (last && !shot.empty()) {
			if (trace) stage.dump();
			renderer.save_frame_png(items, shot);
			std::printf("saved %s (%zu quads, %zu sprites)\n", shot.c_str(), items.size(), stage.size());
		} else if (shot.empty()) {
			renderer.render_frame(items);
		}
		if (host.app.closing) running = false;
	}

	if (const char* code = std::getenv("OMW_EVAL_END")) host.debug_eval(code);
	if (trace) std::printf("audio peak %.3f\n", audio.peak());
	std::printf("stubs called:\n");
	for (const auto& [name, n] : host.stub_calls) std::printf("  %5d  %s\n", n, name.c_str());
	host.renderer = nullptr; // script objects are released later; they must not touch the renderer any more
	renderer.shutdown();
	host.audio = nullptr;
	audio.shutdown();
	gpu.shutdown();
	return 0;
}
