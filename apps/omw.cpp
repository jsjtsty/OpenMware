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
#include <fstream>
#include <sstream>

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
	struct MouseCmd { int frame; float x, y; bool click; };
	std::vector<MouseCmd> mouse_cmds; // --mouse FRAME:X:Y[:click]
	struct AutoClick { int first, period; float x, y; };
	std::vector<AutoClick> auto_clicks; // --auto FIRST:PERIOD:X:Y (a click every PERIOD frames from FIRST on)
	std::vector<std::pair<int, std::string>> shots_at; // --shot-at FRAME:PATH (extra screenshots on the way, implies offscreen)
	struct EvalEvery {
		int first, period;
		std::string code;
	};
	std::vector<EvalEvery> eval_every; // --eval-every FIRST:PERIOD:SNIPPET
	std::vector<std::pair<int, int>> holds; // --hold FRAME:KEYCODE
	std::vector<std::pair<int, int>> taps; // --press FRAME:KEYCODE (scripted key taps for tests)
	for (int i = 2; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--keys" && i + 1 < argc) keys = argv[++i];
		else if (a == "--shaders" && i + 1 < argc) shaders = argv[++i];
		else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
		else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
		else if (a == "--trace") trace = true;
		else if (a == "--shot-at" && i + 1 < argc) {
			const std::string spec = argv[++i];
			shots_at.emplace_back(std::atoi(spec.c_str()), spec.substr(spec.find(':') + 1));
		}
		else if (a == "--auto" && i + 1 < argc) {
			AutoClick c{0, 60, 0, 0};
			if (std::sscanf(argv[++i], "%d:%d:%f:%f", &c.first, &c.period, &c.x, &c.y) == 4 && c.period > 0) auto_clicks.push_back(c);
		}
		else if (a == "--eval" && i + 1 < argc) {
			const std::string spec = argv[++i];
			evals.emplace_back(std::atoi(spec.c_str()), spec.substr(spec.find(':') + 1));
		}
		else if (a == "--mouse" && i + 1 < argc) {
			MouseCmd c{0, 0, 0, false};
			const std::string spec = argv[++i];
			c.click = spec.find("click") != std::string::npos;
			if (std::sscanf(spec.c_str(), "%d:%f:%f", &c.frame, &c.x, &c.y) == 3) mouse_cmds.push_back(c);
		}
		else if (a == "--eval-file" && i + 1 < argc) { // FRAME:PATH (script snippet read from a UTF-8 file)
			const std::string spec = argv[++i];
			std::ifstream in(spec.substr(spec.find(':') + 1), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			evals.emplace_back(std::atoi(spec.c_str()), ss.str());
		}
		else if (a == "--eval-every" && i + 1 < argc) { // FIRST:PERIOD:SNIPPET, run again every PERIOD frames
			const std::string spec = argv[++i];
			const size_t c1 = spec.find(':'), c2 = c1 == std::string::npos ? c1 : spec.find(':', c1 + 1);
			if (c2 != std::string::npos) eval_every.push_back({std::atoi(spec.c_str()), std::atoi(spec.c_str() + c1 + 1), spec.substr(c2 + 1)});
		}
		else if (a == "--hold" && i + 1 < argc) { // FRAME:KEYCODE, the key stays down from that frame on
			int f = 0, k = 0;
			if (std::sscanf(argv[++i], "%d:%d", &f, &k) == 2) holds.emplace_back(f, k);
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
	// One pass of the loop is one run of the game; a script's ::RebootRequest() + exit starts the next one from the boot script
	// (the window, renderer and audio device stay; the stage and the script VM are rebuilt, the save files carry the state over).
	int frame = 0;
	bool reboot = true;
	while (reboot) {
		reboot = false;
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
		DrawList items;
		bool running = true;
		// Fixed-timestep loop: the game logic always advances in 1/60 s steps; rendering runs at the display's rate. A time accumulator
		// decides how many logic steps are due per presented frame (none on a 144 Hz display half the time, several after a stall).
		const bool realtime = frames < 0 && shot.empty() && shots_at.empty();
		const bool mem_stats = std::getenv("OMW_MEMSTATS") != nullptr; // prints cache/stage occupancy every 3000 frames
		const bool render_all =std::getenv("OMW_RENDER_ALL") != nullptr; // headless runs normally skip drawing; effects that read the screen need it
		const Uint64 step_ns = 1000000000ull / 60;
		Uint64 last_ns = SDL_GetTicksNS(), acc_ns = step_ns;
		int catch_up = 0;
		for (; running && (frames < 0 || frame < frames);) {
			bool present = true;
			if (realtime) {
				const Uint64 now = SDL_GetTicksNS();
				acc_ns += std::min<Uint64>(now - last_ns, 250000000ull); // after a long stall, do not fast-forward
				last_ns = now;
				if (acc_ns < step_ns) { // no step due: show the last frame again
					if (items.empty()) SDL_DelayNS(1000000);
					else renderer.render_frame(items);
					continue;
				}
				acc_ns -= step_ns;
				present = acc_ns < step_ns || ++catch_up >= 4; // behind: step again before presenting (at most 4 in a row)
				if (present) catch_up = 0;
			}
			for (const auto& [f, k] : taps)
				if (f == frame) input.tap_key(k);
			for (const auto& [f, k] : holds)
				if (f == frame) input.hold_key(k);
			for (const auto& [f, code] : evals)
				if (f == frame) host.debug_eval(code.c_str());
			for (const auto& e : eval_every)
				if (e.period > 0 && frame >= e.first && (frame - e.first) % e.period == 0) host.debug_eval(e.code.c_str());
			for (const auto& c : mouse_cmds)
				if (c.frame == frame) {
					input.set_cursor(c.x, c.y);
					if (c.click) input.tap_mouse();
				}
			for (const auto& c : auto_clicks)
				if (frame >= c.first && (frame - c.first) % c.period == 0) {
					input.set_cursor(c.x, c.y);
					input.tap_mouse();
				}
			input.poll(gpu.window, Stage::kWidth, Stage::kHeight, 1000.f / 60.f);
			if (input.quit) running = false;
			stage.update_pointer(input.mouse_x, input.mouse_y);
			if (!host.resume_frame(&err)) {
				std::fprintf(stderr, "script error at frame %d: %s\n", frame, err.c_str());
				break;
			}
			stage.update(1000.f / 60.f);
			audio.update(1000.f / 60.f);
			if (mem_stats && frame % 3000 == 0) {
				const auto cs = renderer.cache_stats();
				std::printf("[mem] frame %d: sprites %zu, textures kept %zu (%zu MB) of %zu keys, dynamic %zu\n", frame, stage.size(), cs.kept,
							cs.kept_bytes >> 20, cs.keys, cs.dynamic);
			}
			items.clear();
			stage.build_draw_list(items);
			for (const auto& [f, path] : shots_at)
				if (f == frame) renderer.save_frame_png(items, path);
			const bool last = frames >= 0 && frame == frames - 1;
			if (last && !shot.empty()) {
				if (trace) {
					stage.dump();
					for (const RenderPass& p : items)
						std::printf("  pass op=%d target=%d temp=%d in=%d %dx%d items=%zu\n", static_cast<int>(p.op), p.target ? 1 : 0, p.temp_target, p.in_temp,
									p.w, p.h, p.items.size());
				}
				renderer.save_frame_png(items, shot);
				std::printf("saved %s (%zu quads, %zu sprites)\n", shot.c_str(), items.empty() ? size_t(0) : items.back().items.size(), stage.size());
			} else if ((render_all || (shot.empty() && shots_at.empty())) && present) {
				renderer.render_frame(items);
			}
			if (host.app.closing) running = false;
			frame++;
		}

		if (host.app.reboot && !input.quit && (frames < 0 || frame < frames)) {
			std::printf("[app] rebooting (frame %d)\n", frame);
			reboot = true;
			continue; // the stage and the VM are destroyed here, while the renderer and audio still exist
		}
		if (const char* code = std::getenv("OMW_EVAL_END")) host.debug_eval(code);
		if (trace) std::printf("audio peak %.3f\n", audio.peak());
		std::printf("stubs called:\n");
		for (const auto& [name, n] : host.stub_calls) std::printf("  %5d  %s\n", n, name.c_str());
		host.renderer = nullptr; // script objects are released later; they must not touch the renderer any more
		host.audio = nullptr;
	}
	renderer.shutdown();
	audio.shutdown();
	gpu.shutdown();
	return 0;
}
