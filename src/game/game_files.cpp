#include "game/game_files.h"

namespace omw {

bool GameFiles::open(const std::filesystem::path& game_dir, const std::filesystem::path& keys_file, std::string* error) {
	const auto keys = load_npk_keys(keys_file, error);
	if (keys.empty()) return false;
	for (const char* name : {"script", "system", "cg", "font", "sound", "voice", "dx"}) {
		const auto path = game_dir / (std::string(name) + ".npk");
		if (!std::filesystem::exists(path)) continue;
		std::string err;
		auto a = NpkArchive::open(path, keys, &err);
		if (!a) {
			if (error) *error = err;
			return false;
		}
		archives_.emplace_back(name, std::move(a));
	}
	return !archives_.empty();
}

NpkArchive* GameFiles::archive(const std::string& name) const {
	for (const auto& [n, a] : archives_)
		if (n == name) return a.get();
	return nullptr;
}

bool GameFiles::read(const std::string& path, std::vector<uint8_t>& out) const {
	std::string norm = path;
	for (char& c : norm)
		if (c == '\\') c = '/';
	for (const auto& [n, a] : archives_) {
		if (const NpkEntry* e = a->find_ci(norm)) return a->read(*e, out, nullptr);
	}
	return false;
}

} // namespace omw
