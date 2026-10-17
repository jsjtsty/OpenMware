// OpenMware: runs the game. Usage: OpenMware GAME_DIR [--keys FILE] [--shaders DIR] [--data-dir DIR] [--frames N] [--shot FILE]
//   --frames N  stop after N frames;  --shot FILE  with --frames: save the last frame as PNG (offscreen, no presentation)
// The game loop itself lives in src/api/session.cpp (shared with the launchers' C API, include/mware/mware.h).
#include "api/session.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h> // the entry point on Android and iOS (and wide-character arguments on Windows)

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace omw;

int main(int argc, char** argv) {
	std::setvbuf(stdout, nullptr, _IONBF, 0); // keep diagnostics visible if the process crashes
	// Started without arguments (a phone app): the game files and the keys file are looked up in the app's user-visible
	// data folder (Android: the app's external files dir, iOS: Documents), see docs/mobile.md.
	const std::string platform = SDL_GetPlatform();
	const bool mobile = platform == "Android" || platform == "iOS";
	const bool has_game_arg = argc >= 2 && argv[1][0] != '-';
	if (!has_game_arg && !mobile) {
		std::fputs("usage: OpenMware GAME_DIR [--keys FILE] [--shaders DIR] [--data-dir DIR] [--frames N] [--shot FILE]\n", stderr);
		return 2;
	}
	// Arguments are UTF-8 (SDL_main converts them on Windows); a plain path(const char*) would read them in the ANSI code page.
	auto utf8_path = [](const char* s) { return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s))); };
	RunOptions opt;
	opt.keys = "keys.local.txt";
	if (has_game_arg) opt.game_dir = utf8_path(argv[1]);
	bool keys_given = false;
	for (int i = has_game_arg ? 2 : 1; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--keys" && i + 1 < argc) { opt.keys = utf8_path(argv[++i]); keys_given = true; }
		else if (a == "--shaders" && i + 1 < argc) opt.shaders = utf8_path(argv[++i]);
		else if (a == "--data-dir" && i + 1 < argc) opt.data_dir = utf8_path(argv[++i]);
		else if (a == "--frames" && i + 1 < argc) opt.frames = std::atoi(argv[++i]);
		else if (a == "--shot" && i + 1 < argc) opt.shot = argv[++i];
		else if (a == "--trace") opt.trace = true;
		else if (a == "--shot-at" && i + 1 < argc) {
			const std::string spec = argv[++i];
			opt.shots_at.emplace_back(std::atoi(spec.c_str()), spec.substr(spec.find(':') + 1));
		}
		else if (a == "--auto" && i + 1 < argc) {
			RunOptions::AutoClick c{0, 60, 0, 0};
			if (std::sscanf(argv[++i], "%d:%d:%f:%f", &c.first, &c.period, &c.x, &c.y) == 4 && c.period > 0) opt.auto_clicks.push_back(c);
		}
		else if (a == "--eval" && i + 1 < argc) {
			const std::string spec = argv[++i];
			opt.evals.emplace_back(std::atoi(spec.c_str()), spec.substr(spec.find(':') + 1));
		}
		else if (a == "--mouse" && i + 1 < argc) {
			RunOptions::MouseCmd c{0, 0, 0, false};
			const std::string spec = argv[++i];
			c.click = spec.find("click") != std::string::npos;
			if (spec.find("rclick") != std::string::npos) c.button = 1;
			if (std::sscanf(spec.c_str(), "%d:%f:%f", &c.frame, &c.x, &c.y) == 3) opt.mouse_cmds.push_back(c);
		}
		else if (a == "--eval-file" && i + 1 < argc) { // FRAME:PATH (script snippet read from a UTF-8 file)
			const std::string spec = argv[++i];
			std::ifstream in(spec.substr(spec.find(':') + 1), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			opt.evals.emplace_back(std::atoi(spec.c_str()), ss.str());
		}
		else if (a == "--eval-every" && i + 1 < argc) { // FIRST:PERIOD:SNIPPET, run again every PERIOD frames
			const std::string spec = argv[++i];
			const size_t c1 = spec.find(':'), c2 = c1 == std::string::npos ? c1 : spec.find(':', c1 + 1);
			if (c2 != std::string::npos) opt.eval_every.push_back({std::atoi(spec.c_str()), std::atoi(spec.c_str() + c1 + 1), spec.substr(c2 + 1)});
		}
		else if (a == "--hold" && i + 1 < argc) { // FRAME:KEYCODE, the key stays down from that frame on
			int f = 0, k = 0;
			if (std::sscanf(argv[++i], "%d:%d", &f, &k) == 2) opt.holds.emplace_back(f, k);
		}
		else if (a == "--press" && i + 1 < argc) {
			int f = 0, k = 0;
			if (std::sscanf(argv[++i], "%d:%d", &f, &k) == 2) opt.taps.emplace_back(f, k);
		}
	}

	// Where things live. Desktop: as given (the working directory holds "save/" and "temp/"). Mobile: the app's data folder.
	if (mobile) {
		const char* base = nullptr;
#ifdef SDL_PLATFORM_ANDROID
		base = SDL_GetAndroidExternalStoragePath();
#else
		base = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
#endif
		if (base) {
			if (opt.data_dir.empty()) opt.data_dir = utf8_path(base);
			if (!has_game_arg) opt.game_dir = opt.data_dir / "game";
			if (!keys_given) opt.keys = opt.data_dir / "keys.txt";
		}
	}
	if (opt.shaders.empty()) { // not given: "shaders" in the working directory, else next to the executable / inside the bundle
		opt.shaders = "shaders";
		const char* base = SDL_GetBasePath();
		if (base && !std::filesystem::exists(opt.shaders)) opt.shaders = utf8_path(base) / "shaders";
#ifdef SDL_PLATFORM_ANDROID
		opt.shaders = "shaders"; // an asset inside the APK, read through SDL_LoadFile
#endif
	}

	std::string err;
	return run_session(opt, &err);
}
