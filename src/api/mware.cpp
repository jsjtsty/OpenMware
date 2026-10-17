#include "mware/mware.h"

#include "api/session.h"
#include "game/game_files.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace {

std::filesystem::path utf8_path(const char* s) {
	return s ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s))) : std::filesystem::path();
}

void copy_message(char* dst, size_t size, const std::string& text) {
	if (!dst || size == 0) return;
	const size_t n = std::min(text.size(), size - 1);
	std::memcpy(dst, text.data(), n);
	dst[n] = 0;
}

} // namespace

extern "C" {

MW_API int mw_run(const mw_options* options, char* error, size_t error_size) {
	if (!options || !options->game_dir || !options->keys_file) {
		copy_message(error, error_size, "game_dir and keys_file are required");
		return 2;
	}
	SDL_SetMainReady(); // the host application owns the process entry point
	omw::RunOptions opt;
	opt.game_dir = utf8_path(options->game_dir);
	opt.keys = utf8_path(options->keys_file);
	opt.data_dir = utf8_path(options->data_dir);
	opt.shaders = utf8_path(options->shaders_dir);
	if (options->window_title) opt.title = options->window_title;
	std::string err;
	const int rc = omw::run_session(opt, &err);
	copy_message(error, error_size, err);
	return rc;
}

MW_API int mw_probe(const char* game_dir, const char* keys_file, mw_probe_result* result) {
	mw_probe_result local{};
	mw_probe_result& r = result ? *result : local;
	r = mw_probe_result{};
	omw::GameFiles files;
	std::string err;
	if (!files.open(utf8_path(game_dir), utf8_path(keys_file), &err)) {
		copy_message(r.message, sizeof r.message, err.empty() ? "no game archives found" : err);
		return 0;
	}
	r.archives = static_cast<int>(files.archive_count());
	std::vector<uint8_t> boot;
	if (!files.archive("script") || !files.read("media/script/main.nut", boot)) {
		copy_message(r.message, sizeof r.message, "the boot script (script.npk) is missing or unreadable");
		return 0;
	}
	r.ok = 1;
	return 1;
}

MW_API const char* mw_version(void) { return "0.1.0"; }

} // extern "C"
