// One run of the game: opens the archives, creates the window and renderer, plays until the window closes. Used by the
// command-line program (apps/openmware.cpp) and by the C API (mware.h) that the launchers call.
#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace omw {

struct RunOptions {
	std::filesystem::path game_dir, keys, shaders, data_dir;
	std::string title = "OpenMware";

	// Developer / test options (command line only).
	int frames = -1; // stop after N frames
	bool trace = false;
	std::string shot; // with frames: save the last frame as PNG (offscreen)
	std::vector<std::pair<int, std::string>> evals; // FRAME:CODE, a script snippet run at a frame
	struct MouseCmd { int frame; float x, y; bool click; int button = 0; };
	std::vector<MouseCmd> mouse_cmds; // FRAME:X:Y[:click|:rclick]
	struct TouchCmd { int frame; float x, y; int frames; };
	std::vector<TouchCmd> touch_cmds; // FRAME:X:Y:FRAMES, a finger rests at (X,Y) for FRAMES frames (real SDL finger events)
	struct AutoClick { int first, period; float x, y; };
	std::vector<AutoClick> auto_clicks; // a click every PERIOD frames from FIRST on
	std::vector<std::pair<int, std::string>> shots_at; // FRAME:PATH, extra screenshots (offscreen)
	struct EvalEvery { int first, period; std::string code; };
	std::vector<EvalEvery> eval_every;
	std::vector<std::pair<int, int>> holds; // FRAME:KEYCODE, the key stays down from that frame on
	std::vector<std::pair<int, int>> taps; // FRAME:KEYCODE, scripted key taps
};

// Returns 0 when the game ran and ended normally; otherwise a non-zero code with `*error` describing why.
// `data_dir` (when set) becomes the working directory while the game runs: scripts keep "save/" and "temp/" there.
int run_session(const RunOptions& opt, std::string* error);

} // namespace omw
