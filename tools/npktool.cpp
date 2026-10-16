// npktool: list / extract / verify NPK3 archives.
//   npktool [--keys FILE] list    ARCHIVE
//   npktool [--keys FILE] extract ARCHIVE OUTDIR [PREFIX]
//   npktool [--keys FILE] verify  ARCHIVE
// Keys default to $OPENMWARE_KEYS or ./keys.local.txt.

#include "archive/npk.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

static int usage() {
	std::fputs("usage: npktool [--keys FILE] list ARCHIVE\n"
			   "       npktool [--keys FILE] extract ARCHIVE OUTDIR [PREFIX]\n"
			   "       npktool [--keys FILE] verify ARCHIVE\n", stderr);
	return 2;
}

// Command-line arguments as UTF-8 (on Windows main() would hand us the ANSI code page).
static std::vector<std::string> utf8_args(int argc, char** argv) {
#ifdef _WIN32
	(void)argc; (void)argv;
	int n = 0;
	wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n);
	std::vector<std::string> out;
	for (int i = 1; i < n; i++) {
		const int len = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, nullptr, 0, nullptr, nullptr);
		std::string s(static_cast<size_t>(len > 0 ? len - 1 : 0), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w[i], -1, s.data(), len, nullptr, nullptr);
		out.push_back(std::move(s));
	}
	LocalFree(w);
	return out;
#else
	return std::vector<std::string>(argv + 1, argv + argc);
#endif
}

int main(int argc, char** argv) {
	std::vector<std::string> args = utf8_args(argc, argv);
	fs::path key_file = std::getenv("OPENMWARE_KEYS") ? std::getenv("OPENMWARE_KEYS") : "keys.local.txt";
	if (args.size() >= 2 && args[0] == "--keys") {
		key_file = args[1];
		args.erase(args.begin(), args.begin() + 2);
	}
	if (args.size() < 2) return usage();

	std::string err;
	const auto keys = omw::load_npk_keys(key_file, &err);
	if (keys.empty()) {
		std::fprintf(stderr, "error: %s\n", err.empty() ? "no keys loaded" : err.c_str());
		return 1;
	}
	auto archive = omw::NpkArchive::open(fs::u8path(args[1]), keys, &err);
	if (!archive) {
		std::fprintf(stderr, "error: %s\n", err.c_str());
		return 1;
	}

	const std::string& cmd = args[0];
	if (cmd == "list") {
		for (const auto& e : archive->entries()) std::printf("%10u  %s\n", e.size, e.path.c_str());
		return 0;
	}
	if (cmd == "verify") {
		size_t bad = 0;
		uint64_t bytes = 0;
		const auto t0 = std::chrono::steady_clock::now();
		std::vector<uint8_t> data;
		for (const auto& e : archive->entries()) {
			if (!archive->read(e, data, &err)) {
				std::printf("FAIL %s: %s\n", e.path.c_str(), err.c_str());
				bad++;
			} else {
				bytes += data.size();
			}
		}
		const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		std::printf("%zu entries, %zu failed, %.1f MiB in %.1fs\n", archive->entries().size(), bad,
					static_cast<double>(bytes) / (1 << 20), s);
		return bad ? 1 : 0;
	}
	if (cmd == "extract" && args.size() >= 3) {
		const fs::path out_dir = fs::u8path(args[2]);
		const std::string prefix = args.size() >= 4 ? args[3] : "";
		size_t n = 0;
		std::vector<uint8_t> data;
		for (const auto& e : archive->entries()) {
			if (e.path.compare(0, prefix.size(), prefix) != 0) continue;
			if (!archive->read(e, data, &err)) {
				std::fprintf(stderr, "error: %s\n", err.c_str());
				return 1;
			}
			const fs::path dest = out_dir / fs::u8path(e.path);
			fs::create_directories(dest.parent_path());
			std::ofstream(dest, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
			n++;
		}
		std::printf("extracted %zu files\n", n);
		return 0;
	}
	return usage();
}
