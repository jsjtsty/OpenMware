#include "game/input.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace omw {
namespace {

// SDL scancode -> DirectInput scan code (the numbering behind KEY_CODE_*). 0 = not mapped.
int to_dik(SDL_Scancode sc) {
	switch (sc) {
	case SDL_SCANCODE_ESCAPE:
	case SDL_SCANCODE_AC_BACK: return 1; // the Android back button
	case SDL_SCANCODE_1: return 2;
	case SDL_SCANCODE_2: return 3;
	case SDL_SCANCODE_3: return 4;
	case SDL_SCANCODE_4: return 5;
	case SDL_SCANCODE_5: return 6;
	case SDL_SCANCODE_6: return 7;
	case SDL_SCANCODE_7: return 8;
	case SDL_SCANCODE_8: return 9;
	case SDL_SCANCODE_9: return 10;
	case SDL_SCANCODE_0: return 11;
	case SDL_SCANCODE_MINUS: return 12;
	case SDL_SCANCODE_EQUALS: return 13;
	case SDL_SCANCODE_BACKSPACE: return 14;
	case SDL_SCANCODE_TAB: return 15;
	case SDL_SCANCODE_Q: return 16;
	case SDL_SCANCODE_W: return 17;
	case SDL_SCANCODE_E: return 18;
	case SDL_SCANCODE_R: return 19;
	case SDL_SCANCODE_T: return 20;
	case SDL_SCANCODE_Y: return 21;
	case SDL_SCANCODE_U: return 22;
	case SDL_SCANCODE_I: return 23;
	case SDL_SCANCODE_O: return 24;
	case SDL_SCANCODE_P: return 25;
	case SDL_SCANCODE_LEFTBRACKET: return 26;
	case SDL_SCANCODE_RIGHTBRACKET: return 27;
	case SDL_SCANCODE_RETURN: return 28;
	case SDL_SCANCODE_LCTRL: return 29;
	case SDL_SCANCODE_A: return 30;
	case SDL_SCANCODE_S: return 31;
	case SDL_SCANCODE_D: return 32;
	case SDL_SCANCODE_F: return 33;
	case SDL_SCANCODE_G: return 34;
	case SDL_SCANCODE_H: return 35;
	case SDL_SCANCODE_J: return 36;
	case SDL_SCANCODE_K: return 37;
	case SDL_SCANCODE_L: return 38;
	case SDL_SCANCODE_SEMICOLON: return 39;
	case SDL_SCANCODE_APOSTROPHE: return 40;
	case SDL_SCANCODE_GRAVE: return 41;
	case SDL_SCANCODE_LSHIFT: return 42;
	case SDL_SCANCODE_BACKSLASH: return 43;
	case SDL_SCANCODE_Z: return 44;
	case SDL_SCANCODE_X: return 45;
	case SDL_SCANCODE_C: return 46;
	case SDL_SCANCODE_V: return 47;
	case SDL_SCANCODE_B: return 48;
	case SDL_SCANCODE_N: return 49;
	case SDL_SCANCODE_M: return 50;
	case SDL_SCANCODE_COMMA: return 51;
	case SDL_SCANCODE_PERIOD: return 52;
	case SDL_SCANCODE_SLASH: return 53;
	case SDL_SCANCODE_RSHIFT: return 54;
	case SDL_SCANCODE_KP_MULTIPLY: return 55;
	case SDL_SCANCODE_LALT: return 56;
	case SDL_SCANCODE_SPACE: return 57;
	case SDL_SCANCODE_CAPSLOCK: return 58;
	case SDL_SCANCODE_F1: return 59;
	case SDL_SCANCODE_F2: return 60;
	case SDL_SCANCODE_F3: return 61;
	case SDL_SCANCODE_F4: return 62;
	case SDL_SCANCODE_F5: return 63;
	case SDL_SCANCODE_F6: return 64;
	case SDL_SCANCODE_F7: return 65;
	case SDL_SCANCODE_F8: return 66;
	case SDL_SCANCODE_F9: return 67;
	case SDL_SCANCODE_F10: return 68;
	case SDL_SCANCODE_NUMLOCKCLEAR: return 69;
	case SDL_SCANCODE_SCROLLLOCK: return 70;
	case SDL_SCANCODE_KP_7: return 71;
	case SDL_SCANCODE_KP_8: return 72;
	case SDL_SCANCODE_KP_9: return 73;
	case SDL_SCANCODE_KP_MINUS: return 74;
	case SDL_SCANCODE_KP_4: return 75;
	case SDL_SCANCODE_KP_5: return 76;
	case SDL_SCANCODE_KP_6: return 77;
	case SDL_SCANCODE_KP_PLUS: return 78;
	case SDL_SCANCODE_KP_1: return 79;
	case SDL_SCANCODE_KP_2: return 80;
	case SDL_SCANCODE_KP_3: return 81;
	case SDL_SCANCODE_KP_0: return 82;
	case SDL_SCANCODE_KP_PERIOD: return 83;
	case SDL_SCANCODE_F11: return 87;
	case SDL_SCANCODE_F12: return 88;
	case SDL_SCANCODE_KP_ENTER: return 156;
	case SDL_SCANCODE_RCTRL: return 157;
	case SDL_SCANCODE_KP_DIVIDE: return 181;
	case SDL_SCANCODE_RALT: return 184;
	case SDL_SCANCODE_PAUSE: return 197;
	case SDL_SCANCODE_HOME: return 199;
	case SDL_SCANCODE_UP: return 200;
	case SDL_SCANCODE_PAGEUP: return 201;
	case SDL_SCANCODE_LEFT: return 203;
	case SDL_SCANCODE_RIGHT: return 205;
	case SDL_SCANCODE_END: return 207;
	case SDL_SCANCODE_DOWN: return 208;
	case SDL_SCANCODE_PAGEDOWN: return 209;
	case SDL_SCANCODE_INSERT: return 210;
	case SDL_SCANCODE_DELETE: return 211;
	case SDL_SCANCODE_LGUI: return 219;
	case SDL_SCANCODE_RGUI: return 220;
	default: return 0;
	}
}

} // namespace

void InputState::poll(SDL_Window* window, int game_w, int game_h, float dt_ms) {
	std::fill(std::begin(key_pressed), std::end(key_pressed), false);
	std::fill(std::begin(key_released), std::end(key_released), false);
	mouse_pressed[0] = mouse_pressed[1] = mouse_released[0] = mouse_released[1] = false;
	wheel = 0;

	// fingers: a lifted one is gone after the frame that reported UP, a new one turns into a held one
	for (auto& t : touches) {
		t.dx = t.dy = t.vx = t.vy = 0;
		if (t.state == 3) continue;
		if (t.state == 1) t.state = 2;
		t.time_ms += dt_ms;
	}
	touches.erase(std::remove_if(touches.begin(), touches.end(), [](const Touch& t) { return t.state == 3 && !t.pending_up; }), touches.end());
	for (auto& t : touches)
		if (t.pending_up) { t.pending_up = false; t.state = 3; }

	for (int i = 0; i < 256; i++) {
		if (tap_[i] == 2) {
			key_down[i] = key_pressed[i] = true;
			key_held_ms[i] = key_repeat_ms[i] = 0;
			tap_[i] = 1;
		} else if (tap_[i] == 1) {
			key_down[i] = false;
			key_released[i] = true;
			tap_[i] = 0;
		}
	}
	for (int i = 0; i < 256; i++) {
		if (hold_[i] && !key_down[i]) {
			key_down[i] = key_pressed[i] = true;
			key_held_ms[i] = key_repeat_ms[i] = 0;
		}
	}
	for (int b = 0; b < 2; b++) {
		if (mouse_tap_[b] == 2) {
			mouse_down[b] = mouse_pressed[b] = true;
			mouse_tap_[b] = 1;
		} else if (mouse_tap_[b] == 1) {
			mouse_down[b] = false;
			mouse_released[b] = true;
			mouse_tap_[b] = 0;
		}
	}
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_EVENT_QUIT: quit = true; break;
		case SDL_EVENT_KEY_DOWN:
		case SDL_EVENT_KEY_UP: {
			const int dik = to_dik(e.key.scancode);
			if (!dik || e.key.repeat) break;
			const bool down = e.type == SDL_EVENT_KEY_DOWN;
			if (down && !key_down[dik]) {
				key_pressed[dik] = true;
				key_held_ms[dik] = 0;
				key_repeat_ms[dik] = 0;
			}
			if (!down && key_down[dik]) key_released[dik] = true;
			key_down[dik] = down;
			break;
		}
		case SDL_EVENT_FINGER_DOWN:
		case SDL_EVENT_FINGER_MOTION:
		case SDL_EVENT_FINGER_UP:
		case SDL_EVENT_FINGER_CANCELED: {
			int ww = 0, wh = 0;
			if (window) SDL_GetWindowSize(window, &ww, &wh);
			if (ww <= 0 || wh <= 0) break;
			const float scale = std::min(static_cast<float>(ww) / game_w, static_cast<float>(wh) / game_h);
			const float gx = std::clamp((e.tfinger.x * ww - (ww - game_w * scale) * 0.5f) / scale, 0.f, static_cast<float>(game_w));
			const float gy = std::clamp((e.tfinger.y * wh - (wh - game_h * scale) * 0.5f) / scale, 0.f, static_cast<float>(game_h));
			auto it = std::find_if(touches.begin(), touches.end(), [&](const Touch& t) { return t.id == e.tfinger.fingerID && t.state != 3 && !t.pending_up; });
			if (e.type == SDL_EVENT_FINGER_DOWN) {
				Touch t;
				t.id = e.tfinger.fingerID;
				t.x = t.start_x = gx;
				t.y = t.start_y = gy;
				touches.push_back(t);
				break;
			}
			if (it == touches.end()) break;
			it->dx += gx - it->x;
			it->dy += gy - it->y;
			it->x = gx;
			it->y = gy;
			if (dt_ms > 0) {
				it->vx = it->dx * 1000.f / dt_ms;
				it->vy = it->dy * 1000.f / dt_ms;
			}
			if (std::hypot(gx - it->start_x, gy - it->start_y) > 12.f) it->moving = true; // a finger is never perfectly still
			if (e.type != SDL_EVENT_FINGER_MOTION) {
				if (it->state == 1) it->pending_up = true; // down and up within one frame: show DOWN first
				else it->state = 3;
			}
			break;
		}
		case SDL_EVENT_MOUSE_BUTTON_DOWN:
		case SDL_EVENT_MOUSE_BUTTON_UP: {
			const int b = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1 : -1;
			if (b < 0) break;
			const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
			if (e.button.which == SDL_TOUCH_MOUSEID && swallow_touch_mouse_) { // the release of a long press the script already used
				if (!down) swallow_touch_mouse_ = false;
				break;
			}
			if (down && !mouse_down[b]) mouse_pressed[b] = true;
			if (!down && mouse_down[b]) mouse_released[b] = true;
			mouse_down[b] = down;
			break;
		}
		case SDL_EVENT_MOUSE_WHEEL: wheel = e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0; break;
		default: break;
		}
	}
	for (int i = 0; i < 256; i++) {
		if (key_down[i]) {
			key_held_ms[i] += dt_ms;
			key_repeat_ms[i] += dt_ms;
		}
	}

	// window pixels -> game pixels (the game frame is letterboxed into the window)
	int ww = 0, wh = 0;
	float mx = 0, my = 0;
	SDL_GetMouseState(&mx, &my);
	if (window) SDL_GetWindowSize(window, &ww, &wh);
	if (override_) { // scripted cursor: derive the movement from the last frame
		mouse_dx = mouse_x - last_x_;
		mouse_dy = mouse_y - last_y_;
		last_x_ = mouse_x;
		last_y_ = mouse_y;
		return;
	}
	if (ww > 0 && wh > 0) {
		const float scale = std::min(static_cast<float>(ww) / game_w, static_cast<float>(wh) / game_h);
		const float ox = (ww - game_w * scale) * 0.5f, oy = (wh - game_h * scale) * 0.5f;
		const float gx = std::clamp((mx - ox) / scale, 0.f, static_cast<float>(game_w));
		const float gy = std::clamp((my - oy) / scale, 0.f, static_cast<float>(game_h));
		mouse_dx = gx - mouse_x;
		mouse_dy = gy - mouse_y;
		mouse_x = gx;
		mouse_y = gy;
	}
}

int InputState::touch_count() const {
	int n = 0;
	for (const auto& t : touches) n += !t.hidden;
	return n;
}

const InputState::Touch* InputState::touch(int index) const {
	for (const auto& t : touches) {
		if (t.hidden) continue;
		if (index-- == 0) return &t;
	}
	return nullptr;
}

void InputState::clear_touch() {
	for (auto& t : touches) {
		if (t.state == 3 || t.hidden) continue;
		t.hidden = true;
		// The emulated left button of this finger is released now; the real release that follows must not click what is under it.
		if (mouse_down[0] && t.time_ms >= 300.f) {
			mouse_down[0] = false;
			swallow_touch_mouse_ = true;
		}
	}
}

int InputState::mouse_state(int b) const {
	if (mouse_pressed[b]) return MOUSE_DOWN;
	if (mouse_down[b]) return MOUSE_HOLD;
	if (mouse_released[b]) return MOUSE_UP;
	return MOUSE_FREE;
}

bool InputState::key(int code, bool trigger, float repeat_ms) const {
	if (code < 0 || code > 255) return false;
	// A press counts on its frame even when the key also came up within that frame (a quick tap).
	if (trigger || !key_down[code]) return key_pressed[code];
	if (repeat_ms > 0.f) return key_pressed[code] || key_held_ms[code] >= repeat_ms * 2.f;
	return true;
}

} // namespace omw
