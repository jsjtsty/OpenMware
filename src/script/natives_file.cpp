// SQFile: sequential file access for scripts. Reads go to the real disk first (save data) and then to the game archives
// (media/...); writes always go to the disk, creating directories as needed.
#include "game/game_files.h"
#include "script/bind.h"
#include "script/host.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace omw {

struct FileObj {
	std::vector<uint8_t> data; // whole file for reading
	size_t pos = 0;
	bool open = false;
	bool writing = false;
	std::filesystem::path path;
	std::FILE* out = nullptr;

	~FileObj() { close(); }
	void close() {
		if (out) std::fclose(out);
		out = nullptr;
		open = false;
	}
};

// Raw access for the VM snapshot code (it shares the stream with the script callbacks).
bool file_obj_write(FileObj* f, const void* p, size_t n) { return f && f->out && std::fwrite(p, 1, n, f->out) == n; }
bool file_obj_read(FileObj* f, void* p, size_t n) {
	if (!f || f->pos + n > f->data.size()) return false;
	std::memcpy(p, f->data.data() + f->pos, n);
	f->pos += n;
	return true;
}
long file_obj_tell(FileObj* f) { return !f ? -1 : f->out ? std::ftell(f->out) : static_cast<long>(f->pos); }
bool file_obj_seek(FileObj* f, long pos) {
	if (!f) return false;
	if (f->out) return std::fseek(f->out, pos, SEEK_SET) == 0;
	if (pos < 0 || static_cast<size_t>(pos) > f->data.size()) return false;
	f->pos = static_cast<size_t>(pos);
	return true;
}
bool file_obj_seek_end(FileObj* f) { return f && f->out && std::fseek(f->out, 0, SEEK_END) == 0; }

namespace {

using sqb::arg_int;
using sqb::arg_string;

#define FILE_OBJ sqb::self<FileObj>(v)

SQInteger ctor(HSQUIRRELVM v) {
	sqb::attach<FileObj>(v);
	return 0;
}

SQInteger open_file(HSQUIRRELVM v) { // (filename, mode, compress) -> 0 on success, -1 on failure; the compress flag is ignored (plain files)
	FileObj* f = FILE_OBJ;
	f->close();
	f->data.clear();
	f->pos = 0;
	const std::string name = arg_string(v, 2);
	const SQInteger mode = arg_int(v, 3);
	f->path = std::filesystem::u8path(name);
	f->writing = mode != 0;
	if (!f->writing) {
		std::ifstream in(f->path, std::ios::binary);
		if (in) {
			f->data.assign(std::istreambuf_iterator<char>(in), {});
			f->open = true;
		} else if (const GameFiles* gf = ScriptHost::current()->files; gf && gf->read(name, f->data)) {
			f->open = true;
		}
	} else {
		std::error_code ec;
		if (f->path.has_parent_path()) std::filesystem::create_directories(f->path.parent_path(), ec);
		if (mode == 2) { // read-write: load the current content so seeks/reads work, then rewrite on close
			std::ifstream in(f->path, std::ios::binary);
			if (in) f->data.assign(std::istreambuf_iterator<char>(in), {});
		}
		f->out = std::fopen(f->path.string().c_str(), mode == 2 ? "r+b" : "wb");
		if (!f->out && mode == 2) f->out = std::fopen(f->path.string().c_str(), "w+b");
		f->open = f->out != nullptr;
	}
	sq_pushinteger(v, f->open ? 0 : -1); // 0 = success
	return 1;
}

SQInteger close_file(HSQUIRRELVM v) { FILE_OBJ->close(); return 0; }

SQInteger read_string(HSQUIRRELVM v) { // (line): non-zero reads up to the end of the line, zero a NUL-terminated string
	FileObj* f = FILE_OBJ;
	const bool line = arg_int(v, 2) != 0;
	std::string s;
	while (f->pos < f->data.size()) {
		const char c = static_cast<char>(f->data[f->pos++]);
		if (line ? c == '\n' : c == '\0') break;
		s += c;
	}
	if (line && !s.empty() && s.back() == '\r') s.pop_back();
	sq_pushstring(v, s.c_str(), static_cast<SQInteger>(s.size()));
	return 1;
}

template <class T>
bool read_pod(FileObj* f, T& out) {
	if (f->pos + sizeof(T) > f->data.size()) return false;
	std::memcpy(&out, f->data.data() + f->pos, sizeof(T));
	f->pos += sizeof(T);
	return true;
}
SQInteger read_integer(HSQUIRRELVM v) {
	int32_t n = 0;
	read_pod(FILE_OBJ, n);
	sq_pushinteger(v, n);
	return 1;
}
SQInteger read_float(HSQUIRRELVM v) {
	float x = 0;
	read_pod(FILE_OBJ, x);
	sq_pushfloat(v, x);
	return 1;
}

SQInteger write_string(HSQUIRRELVM v) { // (text, write_null)
	FileObj* f = FILE_OBJ;
	const std::string s = arg_string(v, 2);
	if (f->out) {
		std::fwrite(s.data(), 1, s.size(), f->out);
		if (arg_int(v, 3)) std::fputc(0, f->out);
	}
	return 0;
}
SQInteger write_integer(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	const int32_t n = static_cast<int32_t>(arg_int(v, 2));
	if (f->out) std::fwrite(&n, sizeof n, 1, f->out);
	return 0;
}
SQInteger write_float(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	const float x = sqb::arg_float(v, 2);
	if (f->out) std::fwrite(&x, sizeof x, 1, f->out);
	return 0;
}

SQInteger get_file_size(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	if (f->out) {
		const long cur = std::ftell(f->out);
		std::fseek(f->out, 0, SEEK_END);
		const long end = std::ftell(f->out);
		std::fseek(f->out, cur, SEEK_SET);
		sq_pushinteger(v, end);
	} else {
		sq_pushinteger(v, static_cast<SQInteger>(f->data.size()));
	}
	return 1;
}
SQInteger seek_start(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	if (f->out) std::fseek(f->out, static_cast<long>(arg_int(v, 2)), SEEK_SET);
	else f->pos = std::min<size_t>(static_cast<size_t>(arg_int(v, 2)), f->data.size());
	return 0;
}
SQInteger seek_end(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	if (f->out) std::fseek(f->out, static_cast<long>(arg_int(v, 2)), SEEK_END);
	else {
		const long long p = static_cast<long long>(f->data.size()) + arg_int(v, 2);
		f->pos = static_cast<size_t>(std::max<long long>(0, std::min<long long>(p, static_cast<long long>(f->data.size()))));
	}
	return 0;
}
SQInteger seek_cur(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	if (f->out) std::fseek(f->out, static_cast<long>(arg_int(v, 2)), SEEK_CUR);
	else {
		const long long p = static_cast<long long>(f->pos) + arg_int(v, 2);
		f->pos = static_cast<size_t>(std::max<long long>(0, std::min<long long>(p, static_cast<long long>(f->data.size()))));
	}
	return 0;
}
SQInteger get_position(HSQUIRRELVM v) {
	FileObj* f = FILE_OBJ;
	sq_pushinteger(v, f->out ? std::ftell(f->out) : static_cast<SQInteger>(f->pos));
	return 1;
}
SQInteger ret_null(HSQUIRRELVM) { return 0; }
SQInteger ret_zero(HSQUIRRELVM v) { sq_pushinteger(v, 0); return 1; }

} // namespace

void register_file_class(ScriptHost& h) {
	h.begin_class("SQFile");
	h.add_method("constructor", ctor);
	h.add_method("open", open_file);
	h.add_method("close", close_file);
	h.add_method("readString", read_string);
	h.add_method("readInteger", read_integer);
	h.add_method("readFloat", read_float);
	h.add_method("writeString", write_string);
	h.add_method("writeInteger", write_integer);
	h.add_method("writeFloat", write_float);
	h.add_method("getFileSize", get_file_size);
	h.add_method("seekStart", seek_start);
	h.add_method("seekEnd", seek_end);
	h.add_method("seek", seek_cur);
	h.add_method("getFilePosition", get_position);
	h.add_method("setStringCodePage", ret_null);
	h.add_method("setLock", ret_null);
	h.add_method("setDisused", ret_null);
	h.add_method("getLock", ret_zero);
	h.add_method("getDisused", ret_zero);
	h.end_class();
}

} // namespace omw
