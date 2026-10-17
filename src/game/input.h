// Keyboard and mouse state as the scripts see it: DirectInput-style key codes (KEY_CODE_*), and the mouse in game pixels.
#pragma once

#include <SDL3/SDL.h>

#include <vector>

namespace omw {

enum MouseState { MOUSE_FREE = 0, MOUSE_UP = 1, MOUSE_DOWN = 2, MOUSE_HOLD = 3 };

struct InputState {
	bool key_down[256] = {};
	bool key_pressed[256] = {};  // went down this frame
	bool key_released[256] = {}; // went up this frame
	float key_held_ms[256] = {};  // how long it has been held (for key repeat)
	float key_repeat_ms[256] = {}; // time since the last repeat trigger

	bool mouse_down[2] = {};
	bool mouse_pressed[2] = {};
	bool mouse_released[2] = {};
	float mouse_x = 0, mouse_y = 0, mouse_dx = 0, mouse_dy = 0;
	int wheel = 0; // +1 up, -1 down this frame
	bool quit = false;

	// Call once per frame before the script runs: drains SDL events and updates the state.
	void poll(SDL_Window* window, int game_w, int game_h, float dt_ms);

	// Test hook: the key goes down on the next frame and is released on the one after (as a single tap).
	void tap_key(int code) { tap_[code & 255] = 2; }
	void hold_key(int code) { hold_[code & 255] = true; } // scripted: the key stays down from now on (tests)

	// Test hooks: place the cursor (overrides the real one from then on) and click the left button (down next frame, up after).
	void set_cursor(float x, float y) { override_ = true; mouse_x = x; mouse_y = y; }
	void tap_mouse(int button = 0) { mouse_tap_[button & 1] = 2; } // 0 left, 1 right

	// Fingers on a touch screen (the scripts' TOUCH_INPUT_STATE_*: FREE 0, DOWN 1, DOWN_HOLD 2, UP 3). A finger is listed from the
	// frame it touches down until the frame it lifts (state UP, once). Positions are in game pixels, times in milliseconds.
	struct Touch {
		SDL_FingerID id = 0;
		float x = 0, y = 0, dx = 0, dy = 0, vx = 0, vy = 0; // dx/dy: movement during this frame; v: px per second
		float start_x = 0, start_y = 0, time_ms = 0;
		int state = 1;
		bool moving = false;      // has left the spot it touched down on
		bool pending_up = false;  // lifted in the frame it came down: reports UP on the next frame
		bool hidden = false;      // taken by clearTouchInput: invisible to the scripts until it lifts
	};
	std::vector<Touch> touches;
	int touch_count() const;
	const Touch* touch(int index) const; // the index-th visible finger
	void clear_touch();                  // script: the gesture was used (a held finger must not trigger it again)

	int mouse_state(int button) const;
	// Script key query: held (trigger=false), or only on the frame it went down (trigger=true). A positive repeat_ms
	// re-fires a held key at that interval.
	bool key(int code, bool trigger, float repeat_ms) const;
private:
	int tap_[256] = {};
	bool hold_[256] = {};
	int mouse_tap_[2] = {};
	bool override_ = false;
	bool swallow_touch_mouse_ = false; // the mouse button emulated from a finger whose gesture the script used: ignore its release
	float last_x_ = 0, last_y_ = 0;
};

} // namespace omw
