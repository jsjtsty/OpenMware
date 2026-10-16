// Read access to the game's NPK archives (script, cg, system, sound, voice, dx, font) by archive-relative path.
#pragma once

#include "archive/npk.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace omw {

class GameFiles {
public:
	// Opens every known archive found in `game_dir`; `keys_file` holds the NPK keys. Missing archives are skipped.
	bool open(const std::filesystem::path& game_dir, const std::filesystem::path& keys_file, std::string* error);
	NpkArchive* archive(const std::string& name) const; // "script", "cg", ...
	// Reads a file from the first archive that contains it (ASCII case-insensitive path).
	bool read(const std::string& path, std::vector<uint8_t>& out) const;

private:
	std::vector<std::pair<std::string, std::unique_ptr<NpkArchive>>> archives_;
};

} // namespace omw
