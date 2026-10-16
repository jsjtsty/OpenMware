// omw: runs the game. Usage: omw GAME_DIR [--keys FILE] [--shaders DIR] [--frames N] [--shot FILE]
//   --frames N  stop after N frames;  --shot FILE  with --frames: save the last frame as PNG (offscreen, no presentation)
#include "game/game_files.h"
#include "game/stage.h"
#include "render/gpu.h"
#include "render/renderer2d.h"
#include "script/host.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace omw;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fputs("usage: omw GAME_DIR [--keys FILE] [--shaders DIR] [--frames N] [--shot FILE]\n", stderr);
		return 2;
	}
	std::filesystem::path game_dir = argv[1], keys = "keys.local.txt", shaders = "shaders";
	int frames = -1;
	bool trace = false;
	std::string shot;
	for (int i = 2; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--keys" && i + 1 < argc) keys = argv[++i];
		else if (a == "--shaders" && i + 1 < argc) shaders = argv[++i];
		else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
		else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
		else if (a == "--trace") trace = true;
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

	Stage stage;
	ScriptHost host(*files.archive("script"));
	host.stage = &stage;
	host.renderer = &renderer;
	host.enable_discovery();
	host.verbose = trace;
	if (!host.import("media/script/main.nut", &err)) {
		std::fprintf(stderr, "boot failed: %s\n", err.c_str());
		return 1;
	}

	std::vector<DrawItem> items;
	bool running = true;
	for (int frame = 0; running && (frames < 0 || frame < frames); frame++) {
		SDL_Event e;
		while (SDL_PollEvent(&e))
			if (e.type == SDL_EVENT_QUIT) running = false;
		if (!host.resume_frame(&err)) {
			std::fprintf(stderr, "script error at frame %d: %s\n", frame, err.c_str());
			break;
		}
		stage.update(1000.f / 60.f);
		items.clear();
		stage.build_draw_list(items);
		const bool last = frames >= 0 && frame == frames - 1;
		if (last && !shot.empty()) {
			renderer.save_frame_png(items, shot);
			std::printf("saved %s (%zu quads, %zu sprites)\n", shot.c_str(), items.size(), stage.size());
		} else if (shot.empty()) {
			renderer.render_frame(items);
		}
		if (host.app.closing) running = false;
	}

	std::printf("stubs called:\n");
	for (const auto& [name, n] : host.stub_calls) std::printf("  %5d  %s\n", n, name.c_str());
	renderer.shutdown();
	gpu.shutdown();
	return 0;
}
