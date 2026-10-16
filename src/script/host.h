// Squirrel script host: loads the game's compiled .nut files out of script.npk and exposes the native API.
#pragma once

#include "archive/npk.h"

#include <squirrel.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace omw {

// State shared between the script natives and the platform layer (window, input, audio).
struct AppState {
	bool closing = false;
	std::string title;
	std::map<std::string, double> config; // Config_* values; unknown ones read as 0
	bool full_screen = false;
	float frame_speed = 1.0f;
};

class Stage;
class Renderer2D;
class GameFiles;

class ScriptHost {
public:
	static ScriptHost* current();
	AppState app;
	int frame = 0; // frames resumed so far
	Stage* stage = nullptr;       // set by the game; null when running headless (scriptrun)
	Renderer2D* renderer = nullptr;
	const GameFiles* files = nullptr; // archive file system (media/...); save files live on the real disk

	explicit ScriptHost(NpkArchive& scripts);
	~ScriptHost();

	// Loads (once) and runs a script file by archive path, with the root table as `this`.
	bool import(const std::string& path, std::string* error = nullptr);
	// Loads the script and returns its closure on the VM stack (without running it).
	bool load(const std::string& path, std::string* error = nullptr);

	HSQUIRRELVM vm() const { return vm_; }
	// Resumes the suspended main script for one frame. Returns false on a script error or when the script finished.
	bool resume_frame(std::string* error = nullptr);
	// Native class definition used by the real (non-stub) bindings. Members without a binding fall back to logging stubs.
	void begin_class(const char* script_name);
	void add_method(const char* name, SQFUNCTION fn);
	// Method whose closure carries one string free variable (read it with sq_getstring(v, -1, ...)).
	void add_method_s(const char* name, SQFUNCTION fn, const char* free_string);
	void end_class();
	void add_global(const char* name, SQFUNCTION fn);

	bool verbose = false;
	// Discovery mode: unknown root-table names resolve to logging stubs instead of raising errors.
	void enable_discovery();
	bool discovery = false;
	std::map<std::string, int> missing;

	// Stub bookkeeping: how often each not-yet-implemented native was called.
	std::map<std::string, int> stub_calls;
	std::vector<std::string> log;

private:
	void register_natives();
	void register_stubs();

	NpkArchive& scripts_;
	HSQUIRRELVM vm_ = nullptr;
	std::set<std::string> loaded_;
	std::set<std::string> real_classes_;
	std::string building_;
};

} // namespace omw
