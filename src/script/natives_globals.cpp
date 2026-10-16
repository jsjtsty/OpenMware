// Global native functions of the script runtime (the ones InitializeSquirrelRuntime registers in the original exe).
#include "script/bind.h"
#include "script/host.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <random>

namespace omw {
namespace {

std::mt19937& rng() {
	static std::mt19937 r{std::random_device{}()};
	return r;
}

SQInteger screen_width(HSQUIRRELVM v) { sq_pushinteger(v, 1024); return 1; }
SQInteger screen_height(HSQUIRRELVM v) { sq_pushinteger(v, 576); return 1; }

SQInteger random_i(HSQUIRRELVM v) { // RandomI(min, max): inclusive range
	SQInteger lo = sqb::arg_int(v, 2), hi = sqb::arg_int(v, 3);
	if (hi < lo) std::swap(lo, hi);
	sq_pushinteger(v, std::uniform_int_distribution<SQInteger>(lo, hi)(rng()));
	return 1;
}

SQInteger random_f(HSQUIRRELVM v) {
	float lo = sqb::arg_float(v, 2), hi = sqb::arg_float(v, 3);
	if (hi < lo) std::swap(lo, hi);
	sq_pushfloat(v, lo + (hi - lo) * std::uniform_real_distribution<float>(0.f, 1.f)(rng()));
	return 1;
}

std::string& save_root() {
	static std::string root = "save/";
	return root;
}
SQInteger get_save_root(HSQUIRRELVM v) { sq_pushstring(v, save_root().c_str(), -1); return 1; }
SQInteger set_save_root(HSQUIRRELVM v) { save_root() = sqb::arg_string(v, 2); return 0; }
SQInteger get_temp_root(HSQUIRRELVM v) { sq_pushstring(v, "temp/", -1); return 1; }

SQInteger system_call(HSQUIRRELVM v) { // (command) -> 0 on success; no external commands are run
	sq_pushinteger(v, 0);
	return 1;
}

SQInteger file_delete(HSQUIRRELVM v) {
	std::error_code ec;
	sq_pushinteger(v, std::filesystem::remove(std::filesystem::u8path(sqb::arg_string(v, 2)), ec) ? 0 : -1);
	return 1;
}
SQInteger file_copy(HSQUIRRELVM v) {
	std::error_code ec;
	std::filesystem::copy_file(std::filesystem::u8path(sqb::arg_string(v, 2)), std::filesystem::u8path(sqb::arg_string(v, 3)),
							   std::filesystem::copy_options::overwrite_existing, ec);
	sq_pushinteger(v, ec ? -1 : 0);
	return 1;
}
SQInteger file_move(HSQUIRRELVM v) {
	std::error_code ec;
	std::filesystem::rename(std::filesystem::u8path(sqb::arg_string(v, 2)), std::filesystem::u8path(sqb::arg_string(v, 3)), ec);
	sq_pushinteger(v, ec ? -1 : 0);
	return 1;
}

} // namespace

void register_global_natives(ScriptHost& h) {
	h.add_global("GetScreenWidth", screen_width);
	h.add_global("GetScreenHeight", screen_height);
	h.add_global("RandomI", random_i);
	h.add_global("RandomF", random_f);
	h.add_global("GetSavePathRoot", get_save_root);
	h.add_global("SetSavePathRoot", set_save_root);
	h.add_global("GetTempPathRoot", get_temp_root);
	h.add_global("SystemCall", system_call);
	h.add_global("FileDelete", file_delete);
	h.add_global("FileCopy", file_copy);
	h.add_global("FileMove", file_move);
}

} // namespace omw
