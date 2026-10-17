// SQApp: the application object (window, config, input, resources). Platform-facing parts keep their state in
// ScriptHost::app; everything that needs a real window/audio device is a harmless default until the platform layer exists.
#include "audio/audio.h"
#include "game/input.h"
#include "render/renderer2d.h"
#include "script/bind.h"
#include "script/host.h"

#include <chrono>
#include <ctime>
#include <filesystem>

namespace omw {
namespace {

using sqb::arg_float;
using sqb::arg_int;
using sqb::arg_string;

AppState& app() { return ScriptHost::current()->app; }

// Vector results travel through the root variables SQMATH_RET_1.. which scripts read back with SQMATH_GetResult_Vec2/3.
void set_ret(HSQUIRRELVM v, int n, float value) {
	const std::string name = "SQMATH_RET_" + std::to_string(n);
	sq_pushroottable(v);
	sq_pushstring(v, name.c_str(), -1);
	sq_pushfloat(v, value);
	sq_newslot(v, -3, SQFalse);
	sq_pop(v, 1);
}

SQInteger ret_null(HSQUIRRELVM) { return 0; }
SQInteger ret_true(HSQUIRRELVM v) { sq_pushbool(v, SQTrue); return 1; }
SQInteger ret_false(HSQUIRRELVM v) { sq_pushbool(v, SQFalse); return 1; }
SQInteger ret_zero(HSQUIRRELVM v) { sq_pushinteger(v, 0); return 1; }
SQInteger ret_zero_f(HSQUIRRELVM v) { sq_pushfloat(v, 0.f); return 1; }
SQInteger ret_vec2_zero(HSQUIRRELVM v) { set_ret(v, 1, 0.f); set_ret(v, 2, 0.f); return 0; }
SQInteger ret_empty_array(HSQUIRRELVM v) { sq_newarray(v, 0); return 1; }

SQInteger ctor(HSQUIRRELVM) { return 0; }

InputState* input() { return ScriptHost::current()->input; }
SQInteger is_key_down(HSQUIRRELVM v) { // (code, trigger, repeat_ms): trigger = only on the frame it went down, else held (re-firing at repeat_ms)
	const bool on = input() && input()->key(static_cast<int>(sqb::arg_int(v, 2)), sqb::arg_int(v, 3) != 0, sqb::arg_float(v, 4));
	if (ScriptHost::current()->verbose) std::printf("[f%d] isKeyDown(%d) -> %d\n", ScriptHost::current()->frame, static_cast<int>(sqb::arg_int(v, 2)), on ? 1 : 0);
	sq_pushbool(v, on ? SQTrue : SQFalse);
	return 1;
}
SQInteger get_key_state(HSQUIRRELVM v) { // KEY_INPUT_STATE_FREE/DOWN/DOWN_HOLD/UP = 0/1/2/3
	const int code = static_cast<int>(sqb::arg_int(v, 2));
	int state = 0;
	if (input() && code >= 0 && code < 256) state = input()->key_pressed[code] ? 1 : input()->key_down[code] ? 2 : input()->key_released[code] ? 3 : 0; // KEY_INPUT_STATE_*
	sq_pushinteger(v, state);
	return 1;
}

// --- config ------------------------------------------------------------------------------------
std::string free_name(HSQUIRRELVM v) {
	const SQChar* n = "";
	sq_getstring(v, -1, &n);
	return n;
}
SQInteger cfg_get(HSQUIRRELVM v) {
	const std::string name = free_name(v);
	const auto it = app().config.find(name);
	sq_pushinteger(v, it == app().config.end() ? (name == "FrameRate" ? 60 : 0) : static_cast<SQInteger>(it->second));
	return 1;
}
SQInteger cfg_set(HSQUIRRELVM v) {
	app().config[free_name(v)] = arg_float(v, 2);
	return 0;
}
#define CFG(name) 	h.add_method_s("getConfig_" #name, cfg_get, #name); 	h.add_method_s("setConfig_" #name, cfg_set, #name);

SQInteger get_config_screen_size(HSQUIRRELVM v) {
	set_ret(v, 1, 1024.f);
	set_ret(v, 2, 576.f);
	return 0;
}
// --- time / system -----------------------------------------------------------------------------
SQInteger get_milli_time(HSQUIRRELVM v) {
	using namespace std::chrono;
	static const auto t0 = steady_clock::now();
	sq_pushinteger(v, static_cast<SQInteger>(duration_cast<milliseconds>(steady_clock::now() - t0).count()));
	return 1;
}
SQInteger get_system_time(HSQUIRRELVM v) { sq_pushinteger(v, static_cast<SQInteger>(std::time(nullptr))); return 1; }
SQInteger get_frame_speed(HSQUIRRELVM v) { sq_pushfloat(v, app().frame_speed); return 1; }
SQInteger get_product_name(HSQUIRRELVM v) { sq_pushstring(v, "OpenMware", -1); return 1; }
SQInteger get_platform_name(HSQUIRRELVM v) { sq_pushstring(v, "Windows", -1); return 1; }
SQInteger set_window_title(HSQUIRRELVM v) { app().title = arg_string(v, 2); return 0; }
SQInteger exit_app(HSQUIRRELVM v) {
	std::printf("[app] exit requested by the script (frame %d)\n", ScriptHost::current()->frame);
	if (ScriptHost::current()->verbose) { // where from: the Squirrel call stack
		SQStackInfos si;
		for (SQInteger lvl = 1; SQ_SUCCEEDED(sq_stackinfos(v, lvl, &si)); lvl++)
			std::printf("    at %s (%s:%d)\n", si.funcname ? si.funcname : "?", si.source ? si.source : "?", static_cast<int>(si.line));
	}
	app().closing = true;
	return 0;
}
SQInteger set_full_screen(HSQUIRRELVM v) { app().full_screen = arg_int(v, 2) != 0; return 0; }
SQInteger is_full_screen(HSQUIRRELVM v) { sq_pushbool(v, app().full_screen); return 1; }
SQInteger create_directory(HSQUIRRELVM v) {
	std::error_code ec;
	std::filesystem::create_directories(std::filesystem::u8path(arg_string(v, 2)), ec);
	sq_pushinteger(v, ec ? -1 : 0); // 0 = success, like the other natives
	return 1;
}

} // namespace

void register_app_class(ScriptHost& h) {
	h.begin_class("SQApp");
	h.add_method("constructor", ctor);
	CFG(AntiAliasType) CFG(FixedAspect) CFG(FullScreen) CFG(PointLightNum) CFG(SceneDegreeSizeRatio) CFG(ShadowQuality)
	CFG(UseDOF) CFG(UseHDR) CFG(UseSelfShadow) CFG(UseShadow) CFG(UseSoftFocus) CFG(UseToneMapping)
	CFG(FrameRate)
	h.add_method_s("getFrameRate", cfg_get, "FrameRate");
	h.add_method_s("setFrameRate", cfg_set, "FrameRate");
	h.add_method("getConfig_ScreenSize", get_config_screen_size);
	h.add_method("setConfig_ScreenSize", ret_null);
	h.add_method("getFrameSpeed", get_frame_speed);
	h.add_method("setMaxFrameTime", ret_null);
	h.add_method("getMilliTime", get_milli_time);
	h.add_method("getSystemTime", get_system_time);
	h.add_method("getProductName", get_product_name);
	h.add_method("getPlatformName", get_platform_name);
	h.add_method("setWindowTitle", set_window_title);
	h.add_method("exit", exit_app);
	h.add_method("setFullScreen", set_full_screen);
	h.add_method("isFullScreen", is_full_screen);
	h.add_method("createDirectory", create_directory);
	h.add_method("showMessageBox", ret_zero);
	h.add_method("setCloseCallback", ret_null);
	// resources: the renderer/audio layers will take these over; accepting the request is enough for now
	h.add_method("setSoundTypeVolume", [](HSQUIRRELVM v) -> SQInteger { // (type, power, time_ms, tempo)
		if (ScriptHost::current()->verbose) std::printf("[sound] type %d volume %.2f over %.0fms\n", static_cast<int>(sqb::arg_int(v, 2)), sqb::arg_float(v, 3), sqb::arg_float(v, 4));
		if (ScriptHost::current()->audio) ScriptHost::current()->audio->set_type_volume(static_cast<int>(sqb::arg_int(v, 2)), sqb::arg_float(v, 3), sqb::arg_float(v, 4), static_cast<int>(sqb::arg_int(v, 5)));
		return 0;
	});
	h.add_method("isKeyDown", is_key_down);
	h.add_method("getKeyState", get_key_state);
	h.add_method("getMouseButtonStateL", [](HSQUIRRELVM v) -> SQInteger { sq_pushinteger(v, input() ? input()->mouse_state(0) : 0); return 1; });
	h.add_method("getMouseButtonStateR", [](HSQUIRRELVM v) -> SQInteger { sq_pushinteger(v, input() ? input()->mouse_state(1) : 0); return 1; });
	h.add_method("getMouseWheelState", [](HSQUIRRELVM v) -> SQInteger { sq_pushinteger(v, input() ? (input()->wheel > 0 ? 6 : input()->wheel < 0 ? 5 : 4) : 4); return 1; });
	h.add_method("getCursorPosition", [](HSQUIRRELVM v) -> SQInteger { set_ret(v, 1, input() ? input()->mouse_x : 0.f); set_ret(v, 2, input() ? input()->mouse_y : 0.f); return 0; });
	h.add_method("getMouseMove", [](HSQUIRRELVM v) -> SQInteger { set_ret(v, 1, input() ? input()->mouse_dx : 0.f); set_ret(v, 2, input() ? input()->mouse_dy : 0.f); return 0; });
	h.add_method("loadSystemFont", [](HSQUIRRELVM v) -> SQInteger { // (name, family, size, weight, code_page, ...)
		if (ScriptHost::current()->renderer)
			ScriptHost::current()->renderer->fonts().register_font(sqb::arg_string(v, 2), sqb::arg_string(v, 3), sqb::arg_float(v, 4, 21.f));
		sq_pushbool(v, SQTrue);
		return 1;
	});
	for (const char* n : {"loadTexture", "loadFont", "loadParticle", "mountFile", "loadSystemConfig"}) h.add_method(n, ret_true);
	for (const char* n : {"unloadTexture", "unloadFont", "unloadParticle", "unmountFile", "clearFontCache", "setParticleDebugDrawEnable",
						  "injectSoundFile", "ejectSoundFile", "defineSoundGroup", "setSoundGroupVolume",
						  "createPhysicsWorld", "deletePhysicsWorld", "saveSystemConfig", "applyPresetConfig", "applySystemConfig",
						  "setCursor", "showCursor", "setCursorPosition", "clearMouseInput", "clearTouchInput", "setGamepadVibration",
						  "fadeoutScreen", "fadeoutColor", "callTask", "bindModule", "loadModule"})
		h.add_method(n, ret_null);
	for (const char* n : {"isMounted", "isExistUserSetting"}) h.add_method(n, ret_false);
	for (const char* n : {"getFileTime", "getCursor", "isButtonDown",
						  "getGamepadButtonState", "getTouchContactNum", "getTouchState", "isTouchMoving"})
		h.add_method(n, ret_zero);
	for (const char* n : {"getGamepadThumbValue", "getGamepadTriggerValue", "getTouchTime"}) h.add_method(n, ret_zero_f);
	for (const char* n : {"getTouchPosition", "getTouchMove", "getTouchVelocity"}) h.add_method(n, ret_vec2_zero);
	for (const char* n : {"getParticleNameList"}) h.add_method(n, ret_empty_array);
	h.end_class();
}

} // namespace omw
