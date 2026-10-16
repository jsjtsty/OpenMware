// scriptrun: boots the game's main.nut from script.npk against stub natives and reports which natives get called.
#include "script/host.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace omw;

int main(int argc, char** argv) {
	if (argc < 2) {
		std::fputs("usage: scriptrun SCRIPT.npk [--keys FILE] [--verbose] [entry.nut]\n", stderr);
		return 2;
	}
	std::filesystem::path keys_file = "keys.local.txt";
	std::string entry = "media/script/main.nut";
	bool verbose = false, discover = false;
	int frames = 0;
	for (int i = 2; i < argc; i++) {
		const std::string a = argv[i];
		if (a == "--keys" && i + 1 < argc) keys_file = argv[++i];
		else if (a == "--verbose") verbose = true;
		else if (a == "--discover") discover = true;
		else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
		else entry = a;
	}
	std::string err;
	const auto keys = load_npk_keys(keys_file, &err);
	auto archive = NpkArchive::open(argv[1], keys, &err);
	if (!archive) {
		std::fprintf(stderr, "error: %s\n", err.c_str());
		return 1;
	}
	ScriptHost host(*archive);
	host.verbose = verbose;
	if (discover) host.enable_discovery();
	bool ok = host.import(entry, &err);
	for (int f = 0; ok && f < frames; f++) ok = host.resume_frame(&err);
	std::printf("\n== %s: %s%s\n", entry.c_str(), ok ? "ok" : "FAILED ", ok ? "" : err.c_str());
	std::printf("== stub natives called (%zu):\n", host.stub_calls.size());
	for (const auto& [name, n] : host.stub_calls) std::printf("  %5d  %s\n", n, name.c_str());
	std::printf("== missing root names (%zu):\n", host.missing.size());
	for (const auto& [name, n] : host.missing) std::printf("  %5d  %s\n", n, name.c_str());
	return ok ? 0 : 1;
}
