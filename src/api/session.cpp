#include "api/session.h"

#include "audio/audio.h"
#include "game/game_files.h"
#include "game/input.h"
#include "game/stage.h"
#include "game/texture.h"
#include "render/gpu.h"
#include "render/renderer2d.h"
#include "script/host.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace omw {

namespace {

// Restores the working directory when the session ends (the launcher keeps running in the same process on iOS).
struct CwdGuard {
	std::error_code ec;
	std::filesystem::path saved = std::filesystem::current_path(ec);
	~CwdGuard() {
		if (!saved.empty()) std::filesystem::current_path(saved, ec);
	}
};

int fail(std::string* error, int code, const std::string& message) {
	std::fprintf(stderr, "%s\n", message.c_str());
	if (error) *error = message;
	return code;
}

} // namespace

int run_session(const RunOptions& opt, std::string* error) {
	CwdGuard cwd;
	std::filesystem::path game_dir = opt.game_dir, keys = opt.keys, shaders = opt.shaders;
	if (!opt.data_dir.empty()) { // scripts use relative paths for saves; they land inside the data folder
		std::error_code ec;
#ifndef SDL_PLATFORM_ANDROID // on Android the shaders are an asset inside the APK, not a path
		shaders = std::filesystem::absolute(shaders, ec);
#endif
		if (!game_dir.empty()) game_dir = std::filesystem::absolute(game_dir, ec);
		keys = std::filesystem::absolute(keys, ec);
		std::filesystem::create_directories(opt.data_dir, ec);
		std::filesystem::current_path(opt.data_dir, ec);
		if (ec) std::fprintf(stderr, "cannot use data dir %s: %s\n", opt.data_dir.string().c_str(), ec.message().c_str());
	}

	std::string err;
	GameFiles files;
	if (!files.open(game_dir, keys, &err)) return fail(error, 1, "cannot open game files: " + err);
	GpuContext gpu;
	if (!gpu.init(opt.title.c_str(), Stage::kWidth, Stage::kHeight, shaders, &err)) return fail(error, 1, "init failed: " + err);
	Renderer2D renderer;
	if (!renderer.init(gpu, files, &err)) {
		gpu.shutdown();
		return fail(error, 1, "renderer init failed: " + err);
	}

	renderer.fonts().set_files(&files);
	AudioEngine audio;
	if (!std::getenv("OMW_NOAUDIO") && !audio.init()) std::fprintf(stderr, "no audio device (%s); sound runs silently\n", SDL_GetError());
	int result = 0;
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
		host.verbose = opt.trace;
		if (!host.import("media/script/main.nut", &err)) {
			result = fail(error, 1, "boot failed: " + err);
			host.renderer = nullptr;
			host.audio = nullptr;
			break;
		}

		InputState input;
		host.input = &input;
		DrawList items;
		bool running = true;
		// Fixed-timestep loop: the game logic always advances in 1/60 s steps; rendering runs at the display's rate. A time accumulator
		// decides how many logic steps are due per presented frame (none on a 144 Hz display half the time, several after a stall).
		const bool realtime = opt.frames < 0 && opt.shot.empty() && opt.shots_at.empty();
		const bool mem_stats = std::getenv("OMW_MEMSTATS") != nullptr; // prints cache/stage occupancy every 3000 frames
		const bool render_all = std::getenv("OMW_RENDER_ALL") != nullptr; // headless runs normally skip drawing; effects that read the screen need it
		const Uint64 step_ns = 1000000000ull / 60;
		Uint64 last_ns = SDL_GetTicksNS(), acc_ns = step_ns;
		int catch_up = 0;
		for (; running && (opt.frames < 0 || frame < opt.frames);) {
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
			for (const auto& [f, k] : opt.taps)
				if (f == frame) input.tap_key(k);
			for (const auto& [f, k] : opt.holds)
				if (f == frame) input.hold_key(k);
			for (const auto& [f, code] : opt.evals)
				if (f == frame) host.debug_eval(code.c_str());
			for (const auto& e : opt.eval_every)
				if (e.period > 0 && frame >= e.first && (frame - e.first) % e.period == 0) host.debug_eval(e.code.c_str());
			for (const auto& c : opt.mouse_cmds)
				if (c.frame == frame) {
					input.set_cursor(c.x, c.y);
					if (c.click) input.tap_mouse(c.button);
				}
			for (const auto& c : opt.touch_cmds) {
				if (frame != c.frame && frame != c.frame + c.frames) continue;
				int ww = 0, wh = 0;
				SDL_GetWindowSize(gpu.window, &ww, &wh);
				const float s = std::min(static_cast<float>(ww) / Stage::kWidth, static_cast<float>(wh) / Stage::kHeight);
				SDL_Event ev{};
				ev.type = frame == c.frame ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_UP;
				ev.tfinger.touchID = 1;
				ev.tfinger.fingerID = 1;
				ev.tfinger.x = ((ww - Stage::kWidth * s) * 0.5f + c.x * s) / ww;
				ev.tfinger.y = ((wh - Stage::kHeight * s) * 0.5f + c.y * s) / wh;
				SDL_PushEvent(&ev);
			}
			for (const auto& c : opt.auto_clicks)
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
			// A screen capture that has no picture yet is taken by the renderer, so this frame is drawn even when behind.
			for (const RenderPass& p : items)
				for (const DrawItem& d : p.items)
					if (d.snap == DrawItem::Snap::Capture && d.texture && d.texture->copies == 0) present = true;
			for (const auto& [f, path] : opt.shots_at)
				if (f == frame) renderer.save_frame_png(items, path);
			const bool last = opt.frames >= 0 && frame == opt.frames - 1;
			if (last && !opt.shot.empty()) {
				if (opt.trace) {
					stage.dump();
					for (const RenderPass& p : items)
						std::printf("  pass op=%d target=%d temp=%d in=%d %dx%d items=%zu\n", static_cast<int>(p.op), p.target ? 1 : 0, p.temp_target, p.in_temp,
									p.w, p.h, p.items.size());
				}
				renderer.save_frame_png(items, opt.shot);
				std::printf("saved %s (%zu quads, %zu sprites)\n", opt.shot.c_str(), items.empty() ? size_t(0) : items.back().items.size(), stage.size());
			} else if ((render_all || (opt.shot.empty() && opt.shots_at.empty())) && present) {
				renderer.render_frame(items);
			}
			if (host.app.closing) running = false;
			frame++;
		}

		if (host.app.reboot && !input.quit && (opt.frames < 0 || frame < opt.frames)) {
			std::printf("[app] rebooting (frame %d)\n", frame);
			reboot = true;
			continue; // the stage and the VM are destroyed here, while the renderer and audio still exist
		}
		if (const char* code = std::getenv("OMW_EVAL_END")) host.debug_eval(code);
		if (opt.trace) std::printf("audio peak %.3f\n", audio.peak());
		std::printf("stubs called:\n");
		for (const auto& [name, n] : host.stub_calls) std::printf("  %5d  %s\n", n, name.c_str());
		host.renderer = nullptr; // script objects are released later; they must not touch the renderer any more
		host.audio = nullptr;
	}
	renderer.shutdown();
	audio.shutdown();
	gpu.shutdown();
	return result;
}

} // namespace omw
