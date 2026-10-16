#include "archive/npk.h"

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace omw {
namespace {

constexpr uint32_t kMagicNpk3 = 0x334B504Eu; // "NPK3"
constexpr size_t kHeaderSize = 0x20;
constexpr size_t kSegmentRecordSize = 20;

std::string ascii_lower(std::string_view s) {
	std::string out(s);
	for (char& c : out)
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return out;
}

void set_error(std::string* error, std::string message) {
	if (error) *error = std::move(message);
}

template <class T>
bool take(const std::vector<uint8_t>& buf, size_t& pos, T& value) {
	if (pos + sizeof(T) > buf.size()) return false;
	std::memcpy(&value, buf.data() + pos, sizeof(T)); // little-endian host assumed (x86/ARM)
	pos += sizeof(T);
	return true;
}

// Parses the decrypted, unpadded file table. Returns false if it is not a well-formed table.
bool parse_table(const std::vector<uint8_t>& table, uint32_t expected_count, std::vector<NpkEntry>& out) {
	size_t pos = 0;
	out.clear();
	while (pos < table.size()) {
		NpkEntry e;
		uint16_t name_len = 0;
		uint32_t seg_count = 0;
		if (!take(table, pos, e.segmentation_mode) || e.segmentation_mode > 1) return false;
		if (!take(table, pos, name_len) || name_len == 0 || pos + name_len > table.size()) return false;
		e.path.assign(reinterpret_cast<const char*>(table.data() + pos), name_len);
		pos += name_len;
		if (!take(table, pos, e.size)) return false;
		if (pos + 32 > table.size()) return false;
		std::memcpy(e.sha256.data(), table.data() + pos, 32);
		pos += 32;
		if (!take(table, pos, seg_count) || pos + uint64_t{seg_count} * kSegmentRecordSize > table.size()) return false;
		e.segments.resize(seg_count);
		for (auto& s : e.segments) {
			take(table, pos, s.offset);
			take(table, pos, s.aligned_size);
			take(table, pos, s.real_size);
			take(table, pos, s.decompressed_size);
		}
		out.push_back(std::move(e));
	}
	return out.size() == expected_count;
}

} // namespace

std::vector<NpkKey> load_npk_keys(const std::filesystem::path& file, std::string* error) {
	std::vector<NpkKey> keys;
	std::ifstream in(file);
	if (!in) {
		set_error(error, "cannot open key file: " + file.string());
		return keys;
	}
	std::string line;
	while (std::getline(in, line)) {
		if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
		std::string hex;
		for (char c : line)
			if (!std::isspace(static_cast<unsigned char>(c))) hex += c;
		if (hex.empty()) continue;
		if (hex.size() != 64) {
			set_error(error, "bad key length in " + file.string());
			return {};
		}
		NpkKey key{};
		for (size_t i = 0; i < 32; i++) key[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
		keys.push_back(key);
	}
	return keys;
}

std::unique_ptr<NpkArchive> NpkArchive::open(const std::filesystem::path& file,
											 const std::vector<NpkKey>& keys, std::string* error) {
	std::unique_ptr<NpkArchive> a(new NpkArchive());
	a->path_ = file;
	a->file_.open(file, std::ios::binary);
	if (!a->file_) {
		set_error(error, "cannot open " + file.string());
		return nullptr;
	}
	uint8_t header[kHeaderSize];
	if (!a->file_.read(reinterpret_cast<char*>(header), kHeaderSize)) {
		set_error(error, "truncated header: " + file.string());
		return nullptr;
	}
	uint32_t magic, count, table_size;
	std::memcpy(&magic, header, 4);
	std::memcpy(&a->version_, header + 4, 4);
	std::memcpy(a->iv_.data(), header + 8, 16);
	std::memcpy(&count, header + 0x18, 4);
	std::memcpy(&table_size, header + 0x1C, 4);
	if (magic != kMagicNpk3) {
		set_error(error, "not an NPK3 archive: " + file.string());
		return nullptr;
	}
	if (table_size == 0 || table_size % 16 != 0) {
		set_error(error, "invalid file table size");
		return nullptr;
	}
	std::vector<uint8_t> table(table_size);
	if (!a->file_.read(reinterpret_cast<char*>(table.data()), table_size)) {
		set_error(error, "truncated file table");
		return nullptr;
	}
	for (const NpkKey& key : keys) {
		Aes256Cbc cipher(key.data());
		std::vector<uint8_t> plain(table_size);
		cipher.decrypt(a->iv_.data(), table.data(), plain.data(), table_size);
		const uint8_t pad = plain.back();
		if (pad == 0 || pad > 16 || pad > plain.size()) continue;
		plain.resize(plain.size() - pad);
		if (!parse_table(plain, count, a->entries_)) continue;
		a->cipher_.emplace(key.data());
		for (size_t i = 0; i < a->entries_.size(); i++) {
				a->index_.emplace(a->entries_[i].path, i);
				a->index_ci_.emplace(ascii_lower(a->entries_[i].path), i);
			}
		return a;
	}
	set_error(error, "no key opens the file table of " + file.string());
	return nullptr;
}

const NpkEntry* NpkArchive::find(std::string_view path) const {
	const auto it = index_.find(std::string(path));
	return it == index_.end() ? nullptr : &entries_[it->second];
}

const NpkEntry* NpkArchive::find_ci(std::string_view path) const {
	const auto it = index_ci_.find(ascii_lower(path));
	return it == index_ci_.end() ? nullptr : &entries_[it->second];
}

bool NpkArchive::read(const NpkEntry& entry, std::vector<uint8_t>& out, std::string* error) {
	out.clear();
	out.reserve(entry.size);
	std::vector<uint8_t> raw, plain, inflated;
	for (const NpkSegment& s : entry.segments) {
		if (s.aligned_size == 0 || s.aligned_size % 16 != 0) {
			set_error(error, "invalid segment size in " + entry.path);
			return false;
		}
		raw.resize(s.aligned_size);
		file_.clear();
		file_.seekg(static_cast<std::streamoff>(s.offset));
		if (!file_.read(reinterpret_cast<char*>(raw.data()), s.aligned_size)) {
			set_error(error, "truncated segment in " + entry.path);
			return false;
		}
		plain.resize(s.aligned_size);
		cipher_->decrypt(iv_.data(), raw.data(), plain.data(), s.aligned_size);
		if (s.real_size < s.decompressed_size) {
			// Stored as a zstd frame.
			inflated.resize(s.decompressed_size);
			const size_t n = ZSTD_decompress(inflated.data(), inflated.size(), plain.data(), s.real_size);
			if (ZSTD_isError(n) || n != s.decompressed_size) {
				set_error(error, "zstd failure in " + entry.path);
				return false;
			}
			out.insert(out.end(), inflated.begin(), inflated.end());
		} else {
			// Stored raw: the first decompressed_size bytes are the data (the rest is padding).
			if (s.decompressed_size > plain.size()) {
				set_error(error, "invalid raw segment in " + entry.path);
				return false;
			}
			out.insert(out.end(), plain.begin(), plain.begin() + s.decompressed_size);
		}
	}
	if (out.size() != entry.size) {
		set_error(error, "size mismatch in " + entry.path);
		return false;
	}
	return true;
}

} // namespace omw
