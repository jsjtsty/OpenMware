// Global native functions of the script runtime (the ones InitializeSquirrelRuntime registers in the original exe).
#include "game/input.h"
#include "game/text_markup.h"
#include "script/bind.h"
#include "script/host.h"

#include <chrono>
#include <cstdio>
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

SQInteger count_text_length(HSQUIRRELVM v) { // (text) -> {text = visible characters, ruby = furigana characters}
	size_t text = 0, ruby = 0;
	for (const TextRun& r : parse_text_markup(sqb::arg_string(v, 2))) {
		text += utf16_length(strip_controls(r.text));
		ruby += utf16_length(r.ruby);
	}
	sq_newtable(v);
	sq_pushstring(v, "text", -1);
	sq_pushinteger(v, static_cast<SQInteger>(text));
	sq_newslot(v, -3, SQFalse);
	sq_pushstring(v, "ruby", -1);
	sq_pushinteger(v, static_cast<SQInteger>(ruby));
	sq_newslot(v, -3, SQFalse);
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
	h.add_global("OMW_Stack", [](HSQUIRRELVM v) -> SQInteger { // debugging aid: print the call stack of a script thread
		HSQUIRRELVM t = nullptr;
		if (SQ_FAILED(sq_getthread(v, 2, &t)) || !t) return 0;
		SQStackInfos si;
		for (SQInteger lvl = 0; SQ_SUCCEEDED(sq_stackinfos(t, lvl, &si)); lvl++)
			std::fprintf(stderr, "    at %s (%s:%d)\n", si.funcname ? si.funcname : "?", si.source ? si.source : "?", static_cast<int>(si.line));
		return 0;
	});
	h.add_global("OMW_TapKey", [](HSQUIRRELVM v) -> SQInteger { // debugging aid: a scripted key tap (DirectInput scancode)
		if (ScriptHost::current()->input) ScriptHost::current()->input->tap_key(static_cast<int>(sqb::arg_int(v, 2)));
		return 0;
	});
	h.add_global("RebootRequest", [](HSQUIRRELVM) -> SQInteger { // the game restarts itself after g_App.exit() (the story's "reboot" into the second playthrough)
		ScriptHost::current()->app.reboot = true;
		return 0;
	});
	h.add_global("OMW_Log",[](HSQUIRRELVM v) -> SQInteger { std::fprintf(stderr, "%s", sqb::arg_string(v, 2).c_str()); return 0; }); // debugging aid for OMW_EVAL
	h.add_global("CountTextLength", count_text_length);
	h.add_global("SetClearTrue", [](HSQUIRRELVM) -> SQInteger { return 0; }); // platform achievement hook (Steam); nothing to do here
	h.add_global("FileDelete", file_delete);
	h.add_global("FileCopy", file_copy);
	h.add_global("FileMove", file_move);
}

} // namespace omw
