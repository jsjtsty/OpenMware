#pragma once

#include "archive/aes256.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace omw {

using NpkKey = std::array<uint8_t, 32>;

// Parses a text file with one 64-digit hex key per line ('#' starts a comment).
std::vector<NpkKey> load_npk_keys(const std::filesystem::path& file, std::string* error = nullptr);

struct NpkSegment {
	uint64_t offset = 0;
	uint32_t aligned_size = 0;      // bytes stored in the file (AES blocks)
	uint32_t real_size = 0;         // payload bytes after decryption; < decompressed_size => zstd frame
	uint32_t decompressed_size = 0;
};

struct NpkEntry {
	std::string path; // UTF-8, '/' separated
	uint32_t size = 0;
	std::array<uint8_t, 32> sha256{};
	uint8_t segmentation_mode = 0;
	std::vector<NpkSegment> segments;
};

// Read-only view of an NPK3 archive (AES-256-CBC table and segments, zstd compression).
// Not thread-safe: reads share one file handle.
class NpkArchive {
public:
	// Tries every key against the file table and keeps the first that parses.
	static std::unique_ptr<NpkArchive> open(const std::filesystem::path& file,
											const std::vector<NpkKey>& keys, std::string* error = nullptr);

	const std::vector<NpkEntry>& entries() const { return entries_; }
	const NpkEntry* find(std::string_view path) const;
	// ASCII case-insensitive lookup (the game's scripts use paths whose case differs from the archive's).
	const NpkEntry* find_ci(std::string_view path) const;

	// Reads and decodes the whole file. Returns false (and sets `error`) on failure.
	bool read(const NpkEntry& entry, std::vector<uint8_t>& out, std::string* error = nullptr);

	const std::filesystem::path& path() const { return path_; }
	uint32_t version() const { return version_; }

private:
	NpkArchive() = default;

	std::filesystem::path path_;
	std::ifstream file_;
	std::array<uint8_t, 16> iv_{};
	std::optional<Aes256Cbc> cipher_;
	uint32_t version_ = 0;
	std::vector<NpkEntry> entries_;
	std::unordered_map<std::string, size_t> index_;
	std::unordered_map<std::string, size_t> index_ci_;
};

} // namespace omw
