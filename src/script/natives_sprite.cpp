// SQSprite: script-visible 2D sprites on the Stage. The property animations (move/scale/rotate/fade/colour) follow the
// original's "target value, time in ms, tempo curve" model; details that are not yet verified against the original are
// marked TODO and tracked in docs/script-notes.md.
#include "game/input.h"
#include "game/stage.h"
#include "render/renderer2d.h"
#include "script/bind.h"
#include "script/host.h"
#include "script/sprite_ref.h"

#include <cmath>
#include <cstdio>

namespace omw {

SpriteRef::~SpriteRef() {
	if (!sprite) return;
	if (aux) { // the movie state's tick hook points into it
		sprite->tick = nullptr;
		sprite->texture = nullptr;
		sprite->texture_hold.reset();
	}
	ScriptHost* h = ScriptHost::current();
	if ((sprite->is_target || sprite->is_capture) && sprite->texture) { // the surface belongs to the renderer
		if (h && h->renderer) h->renderer->destroy_dynamic_texture(sprite->texture);
		sprite->texture = nullptr;
	}
	sprite->released = true; // the script dropped its last reference: the sprite leaves the stage
	if (ScriptHost::current() && ScriptHost::current()->verbose)
		std::printf("[f%d] [sprite %s] released\n", ScriptHost::current()->frame, sprite->name.c_str());
}

namespace {

using sqb::arg_float;
using sqb::arg_int;
using sqb::arg_string;

constexpr float kDeg2Rad = 3.14159265358979f / 180.f;

ScriptHost& host() { return *ScriptHost::current(); }

#define TRACE_SPRITE(...) do { if (host().verbose) { std::printf(__VA_ARGS__); } } while (0)
Sprite* spr(HSQUIRRELVM v) {
	auto* r = sqb::self<SpriteRef>(v);
	return r ? r->sprite.get() : nullptr;
}

void set_ret(HSQUIRRELVM v, int n, float value) {
	const std::string name = "SQMATH_RET_" + std::to_string(n);
	sq_pushroottable(v);
	sq_pushstring(v, name.c_str(), -1);
	sq_pushfloat(v, value);
	sq_newslot(v, -3, SQFalse);
	sq_pop(v, 1);
}

SQInteger ctor(HSQUIRRELVM v) {
	auto* r = sqb::attach<SpriteRef>(v);
	if (host().stage) r->sprite = host().stage->create(arg_string(v, 2));
	else r->sprite = std::make_shared<Sprite>();
	return 0;
}

void init_image(Sprite* s, const std::string& file, int x, int y, int w, int h) {
	s->kind = Sprite::Kind::Image;
	s->file = file;
	s->sub_x = x < 0 ? 0 : x;
	s->sub_y = y < 0 ? 0 : y;
	s->sub_w = w;
	s->sub_h = h;
	s->texture_hold = host().renderer ? host().renderer->texture(file) : nullptr;
	s->texture = s->texture_hold.get();
	if (s->texture) {
		s->width = static_cast<float>(w > 0 ? w : s->texture->w - s->sub_x);
		s->height = static_cast<float>(h > 0 ? h : s->texture->h - s->sub_y);
	}
}

SQInteger create(HSQUIRRELVM v) { // (file, x, y, w, h)
	Sprite* s = spr(v);
	TRACE_SPRITE("[f%d] [sprite %s] create %s\n", host().frame, s->name.c_str(), arg_string(v, 2).c_str());
	init_image(s, arg_string(v, 2), static_cast<int>(arg_int(v, 3, -1)), static_cast<int>(arg_int(v, 4, -1)),
			   static_cast<int>(arg_int(v, 5, -1)), static_cast<int>(arg_int(v, 6, -1)));
	sq_pushinteger(v, (s->texture != nullptr || !host().renderer) ? 0 : -1); // 0 = success
	return 1;
}

SQInteger create_color(HSQUIRRELVM v) { // (w, h, r, g, b)
	Sprite* s = spr(v);
	s->kind = Sprite::Kind::Color;
	s->width = arg_float(v, 2);
	s->height = arg_float(v, 3);
	for (auto& c : s->color) c.set({arg_float(v, 4), arg_float(v, 5), arg_float(v, 6)});
	sq_pushinteger(v, 0); // 0 = success
	return 1;
}

SQInteger create_screen(HSQUIRRELVM v) { // (u, v, w, h): shows the region of what has been drawn below it
	Sprite* s = spr(v);
	s->kind = Sprite::Kind::Screen;
	s->sub_x = static_cast<int>(std::max(0.f, arg_float(v, 2, 0)));
	s->sub_y = static_cast<int>(std::max(0.f, arg_float(v, 3, 0)));
	s->width = arg_float(v, 4, -1) > 0 ? arg_float(v, 4) : static_cast<float>(Stage::kWidth);
	s->height = arg_float(v, 5, -1) > 0 ? arg_float(v, 5) : static_cast<float>(Stage::kHeight);
	s->sub_w = static_cast<int>(s->width);
	s->sub_h = static_cast<int>(s->height);
	if (s->sub_x == 0 && s->sub_y == 0 && s->width >= Stage::kWidth && s->height >= Stage::kHeight && host().renderer) {
		// The whole frame: a capture sprite, so that stop() (the game's "Passive" request) can freeze the picture while
		// the scene behind it is hidden (menus, dialogs, save thumbnails).
		const int ss = host().renderer->supersample();
		if ((s->texture = host().renderer->create_target(Stage::kWidth * ss, Stage::kHeight * ss))) {
			s->kind = Sprite::Kind::Image;
			s->is_capture = true;
			s->capturing = true;
			s->sub_x = s->sub_y = s->sub_w = s->sub_h = 0;
			s->width = static_cast<float>(Stage::kWidth);
			s->height = static_cast<float>(Stage::kHeight);
		}
	}
	TRACE_SPRITE("[f%d] [sprite %s] createScreen %dx%d at %d,%d capture=%d tex=%d\n", host().frame, s->name.c_str(), s->sub_w, s->sub_h, s->sub_x, s->sub_y, s->is_capture ? 1 : 0, s->texture ? 1 : 0);
	sq_pushinteger(v, 0);
	return 1;
}

// SQSpriteRenderTarget.create(w, h, capture): an offscreen surface; a capture target instead copies what is drawn below it.
SQInteger target_create(HSQUIRRELVM v) {
	Sprite* s = spr(v);
	const int w = std::max(1, static_cast<int>(arg_int(v, 2))), h = std::max(1, static_cast<int>(arg_int(v, 3)));
	s->is_capture = arg_int(v, 4, 0) != 0;
	s->is_target = !s->is_capture;
	s->capturing = true;
	s->texture = host().renderer ? host().renderer->create_target(w * (s->is_capture ? host().renderer->supersample() : 1), h * (s->is_capture ? host().renderer->supersample() : 1)) : nullptr; // a capture copies from the supersampled frame
	if (!s->texture) {
		sq_pushinteger(v, -1);
		return 1;
	}
	s->kind = Sprite::Kind::Image;
	s->width = static_cast<float>(w);
	s->height = static_cast<float>(h);
	TRACE_SPRITE("[f%d] [sprite %s] target %dx%d capture=%d\n", host().frame, s->name.c_str(), w, h, s->is_capture ? 1 : 0);
	sq_pushinteger(v, 0);
	return 1;
}
SQInteger target_set_capture(HSQUIRRELVM v) { spr(v)->capturing = arg_int(v, 2) != 0; return 0; }
SQInteger target_play(HSQUIRRELVM v) { spr(v)->capturing = true; return 0; }
SQInteger target_stop(HSQUIRRELVM v) { spr(v)->capturing = false; return 0; }
SQInteger target_is_capture(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->is_capture && spr(v)->capturing); return 1; }
SQInteger target_false(HSQUIRRELVM v) { sq_pushbool(v, SQFalse); return 1; }
SQInteger target_true(HSQUIRRELVM v) { sq_pushbool(v, SQTrue); return 1; }
SQInteger target_noop(HSQUIRRELVM) { return 0; }
// saveImage(file, w, h, mode): writes what the sprite's surface holds (a screen snapshot, a surface) as a scaled PNG.
SQInteger save_image(HSQUIRRELVM v) {
	Sprite* s = spr(v);
	if (!s || !s->texture || !host().renderer) return 0;
	host().renderer->save_texture_png(s->texture, static_cast<int>(arg_int(v, 3)), static_cast<int>(arg_int(v, 4)), arg_string(v, 2));
	return 0;
}

SQInteger set_surface(HSQUIRRELVM v) { // (target sprite name; "null"/empty: the screen)
	const std::string name = arg_string(v, 2);
	Sprite* s = spr(v);
	const auto t = host().stage && !name.empty() && name != "null" && name != s->name ? host().stage->find(name) : nullptr;
	s->surface = t && t->is_target ? t : nullptr;
	TRACE_SPRITE("[f%d] [sprite %s] setSurface %s -> %d\n", host().frame, s->name.c_str(), name.c_str(), s->surface.lock() ? 1 : 0);
	return 0;
}

SQInteger set_priority(HSQUIRRELVM v) { spr(v)->priority = static_cast<int>(arg_int(v, 2)); return 0; }
SQInteger get_priority(HSQUIRRELVM v) { sq_pushinteger(v, spr(v)->priority); return 1; }
SQInteger set_erase(HSQUIRRELVM v) {
	TRACE_SPRITE("[f%d] [sprite %s] setErase %d\n", host().frame, spr(v)->name.c_str(), static_cast<int>(arg_int(v, 2)));
	spr(v)->erase = arg_int(v, 2) != 0;
	return 0;
}
SQInteger get_erase(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->erase); return 1; }
SQInteger set_blend_mode(HSQUIRRELVM v) { spr(v)->blend = static_cast<int>(arg_int(v, 2)); return 0; }
SQInteger set_time_scale(HSQUIRRELVM v) { spr(v)->time_scale = arg_float(v, 2, 1.f); return 0; }

SQInteger fade(HSQUIRRELVM v) { // (dst, time_ms, tempo)
	TRACE_SPRITE("[f%d] [sprite %s] fade %g %g\n", host().frame, spr(v)->name.c_str(), arg_float(v, 2), arg_float(v, 3));
	spr(v)->fade.start({arg_float(v, 2)}, arg_float(v, 3), static_cast<int>(arg_int(v, 4)));
	return 0;
}
SQInteger get_fade(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->fade.value[0]); return 1; }

SQInteger move(HSQUIRRELVM v) { // (x, y, time_ms, tempo)
	Sprite* s = spr(v);
	TRACE_SPRITE("[f%d] [sprite %s] move %g %g t=%g\n", host().frame, s->name.c_str(), arg_float(v, 2), arg_float(v, 3), arg_float(v, 4));
	s->move.start({arg_float(v, 2), arg_float(v, 3), s->move.value[2]}, arg_float(v, 4), static_cast<int>(arg_int(v, 5)));
	return 0;
}
SQInteger move3d(HSQUIRRELVM v) { // (x, y, z, time_ms, tempo)
	spr(v)->move.start({arg_float(v, 2), arg_float(v, 3), arg_float(v, 4)}, arg_float(v, 5), static_cast<int>(arg_int(v, 6)));
	return 0;
}
SQInteger get_move(HSQUIRRELVM v) {
	const Sprite* s = spr(v);
	for (int i = 0; i < 3; i++) set_ret(v, i + 1, s->move.value[i]);
	TRACE_SPRITE("[f%d] [sprite %s] getMove %g %g\n", host().frame, s->name.c_str(), s->move.value[0], s->move.value[1]);
	return 0;
}

SQInteger scale(HSQUIRRELVM v) { // (x, y, time_ms, tempo)
	spr(v)->scale.start({arg_float(v, 2), arg_float(v, 3)}, arg_float(v, 4), static_cast<int>(arg_int(v, 5)));
	return 0;
}
SQInteger get_scale(HSQUIRRELVM v) {
	const Sprite* s = spr(v);
	set_ret(v, 1, s->scale.value[0]);
	set_ret(v, 2, s->scale.value[1]);
	return 0;
}

SQInteger rotate(HSQUIRRELVM v) { // (x, y, z in degrees, time_ms, tempo); only the z (in-plane) rotation is drawn
	spr(v)->rotate.start({arg_float(v, 2) * kDeg2Rad, arg_float(v, 3) * kDeg2Rad, arg_float(v, 4) * kDeg2Rad}, arg_float(v, 5),
						 static_cast<int>(arg_int(v, 6)));
	return 0;
}
SQInteger get_rotate(HSQUIRRELVM v) {
	const Sprite* s = spr(v);
	for (int i = 0; i < 3; i++) set_ret(v, i + 1, s->rotate.value[i] / kDeg2Rad);
	return 0;
}

SQInteger scroll(HSQUIRRELVM v) { // (x, y, time_ms, tempo)
	spr(v)->scroll.start({arg_float(v, 2), arg_float(v, 3)}, arg_float(v, 4), static_cast<int>(arg_int(v, 5)));
	return 0;
}
SQInteger get_scroll(HSQUIRRELVM v) {
	const Sprite* s = spr(v);
	set_ret(v, 1, s->scroll.value[0]);
	set_ret(v, 2, s->scroll.value[1]);
	return 0;
}

SQInteger set_pivot(HSQUIRRELVM v) { // TODO: unit unverified (assumed pixels from the top-left corner)
	Sprite* s = spr(v);
	if (s->width > 0) s->pivot_x = arg_float(v, 2) / s->width;
	if (s->height > 0) s->pivot_y = arg_float(v, 3) / s->height;
	return 0;
}
SQInteger get_size(HSQUIRRELVM v) {
	const Sprite* s = spr(v);
	set_ret(v, 1, s->width);
	set_ret(v, 2, s->height);
	return 0;
}

SQInteger set_color(HSQUIRRELVM v) { // (num 0..3 corner, r, g, b, time_ms, tempo)
	Sprite* s = spr(v);
	const int n = static_cast<int>(arg_int(v, 2));
	if (n >= 0 && n < 4) s->color[n].start({arg_float(v, 3), arg_float(v, 4), arg_float(v, 5)}, arg_float(v, 6), static_cast<int>(arg_int(v, 7)));
	return 0;
}
SQInteger get_color(HSQUIRRELVM v) {
	const Sprite* s = spr(v);
	const int n = static_cast<int>(arg_int(v, 2));
	for (int i = 0; i < 3; i++) set_ret(v, i + 1, (n >= 0 && n < 4) ? s->color[n].value[i] : 0.f);
	return 0;
}

SQInteger is_deleted_texture(HSQUIRRELVM v) { sq_pushbool(v, SQFalse); return 1; }
SQInteger is_moving(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->move.active || spr(v)->bezier.on); return 1; }

// Bezier paths: resetBezier(), setBezier(x, y[, z]) appends a control point, beginBezier(time_ms, tempo) starts the move.
SQInteger reset_bezier(HSQUIRRELVM v) {
	spr(v)->bezier.pts.clear();
	spr(v)->bezier.on = false;
	return 0;
}
SQInteger set_bezier(HSQUIRRELVM v) {
	const float z = sq_gettop(v) >= 4 ? arg_float(v, 4) : 0.f;
	spr(v)->bezier.pts.push_back({arg_float(v, 2), arg_float(v, 3), z});
	return 0;
}
SQInteger begin_bezier(HSQUIRRELVM v) {
	Sprite* s = spr(v);
	auto& b = s->bezier;
	b.origin = {s->move.value[0], s->move.value[1], s->move.value[2]};
	b.u.set({0.f});
	b.u.start({1.f}, arg_float(v, 2), static_cast<int>(arg_int(v, 3)));
	b.on = true;
	if (!b.u.active) {
		b.apply(1.f, s->move);
		b.on = false;
	}
	return 0;
}
SQInteger get_bezier_timer(HSQUIRRELVM v) {
	const auto& b = spr(v)->bezier;
	sq_pushfloat(v, b.on && b.u.active ? b.u.timer : 0.f);
	return 1;
}
SQInteger set_bezier_timer(HSQUIRRELVM v) {
	Sprite* s = spr(v);
	if (s->bezier.on) {
		s->bezier.u.set_timer(arg_float(v, 2));
		s->bezier.apply(s->bezier.u.value[0], s->move);
		if (!s->bezier.u.active) s->bezier.on = false;
	}
	return 0;
}
SQInteger is_fading(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->fade.active); return 1; }
SQInteger is_scaling(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->scale.active); return 1; }
SQInteger is_rotating(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->rotate.active); return 1; }

} // namespace

void register_sprite_property_methods(ScriptHost& h) {
	h.add_method("setPriority", set_priority);
	h.add_method("getPriority", get_priority);
	h.add_method("setErase", set_erase);
	h.add_method("getErase", get_erase);
	h.add_method("setBlendMode", set_blend_mode);
	h.add_method("setTimeScale", set_time_scale);
	h.add_method("fade", fade);
	h.add_method("getFade", get_fade);
	h.add_method("move", move);
	h.add_method("move3D", move3d);
	h.add_method("getMove", get_move);
	h.add_method("scale", scale);
	h.add_method("getScale", get_scale);
	h.add_method("rotate", rotate);
	h.add_method("getRotate", get_rotate);
	h.add_method("setPivot", set_pivot);
	h.add_method("isMoving", is_moving);
	h.add_method("resetBezier", reset_bezier);
	h.add_method("setBezier", set_bezier);
	h.add_method("beginBezier", begin_bezier);
	h.add_method("getBezierTimer", get_bezier_timer);
	h.add_method("setBezierTimer", set_bezier_timer);
	h.add_method("isFading", is_fading);
	h.add_method("isScaling", is_scaling);
	h.add_method("isRotating", is_rotating);
}

SQInteger set_clickable(HSQUIRRELVM v) { // (clickable, check_alpha)
	spr(v)->clickable = arg_int(v, 2) != 0;
	spr(v)->check_alpha = arg_int(v, 3) != 0;
	TRACE_SPRITE("[f%d] [sprite %s] clickable %d alpha %d\n", host().frame, spr(v)->name.c_str(), static_cast<int>(arg_int(v, 2)), static_cast<int>(arg_int(v, 3)));
	return 0;
}
// Mouse queries: the stage resolves the topmost clickable sprite under the cursor once per frame.
bool is_hot(HSQUIRRELVM v) { return host().stage && host().stage->is_hit(spr(v)); }
SQInteger is_over(HSQUIRRELVM v) { // (trigger): trigger != 0 -> only on the frame the cursor enters
	bool on = is_hot(v);
	if (on && arg_int(v, 2) != 0) on = !host().stage->was_hit(spr(v));
	sq_pushbool(v, on);
	return 1;
}
SQInteger is_down(HSQUIRRELVM v) { // (trigger): left button pressed over the sprite (trigger: only the press frame)
	const int st = host().input ? host().input->mouse_state(0) : 0;
	sq_pushbool(v, is_hot(v) && (st == MOUSE_DOWN || (arg_int(v, 2) == 0 && st == MOUSE_HOLD)));
	return 1;
}
SQInteger is_up(HSQUIRRELVM v) { // left button released over the sprite
	sq_pushbool(v, is_hot(v) && host().input && host().input->mouse_state(0) == MOUSE_UP);
	return 1;
}
SQInteger ret_null(HSQUIRRELVM) { return 0; }

SQInteger set_mask(HSQUIRRELVM v) { // (sprite name)
	const std::string name = arg_string(v, 2);
	spr(v)->mask = host().stage && !name.empty() ? host().stage->find(name) : nullptr;
	return 0;
}
SQInteger set_displacement_map(HSQUIRRELVM v) {
	const std::string name = arg_string(v, 2);
	spr(v)->displacement = host().stage && !name.empty() ? host().stage->find(name) : nullptr;
	return 0;
}
SQInteger set_diffuse_only(HSQUIRRELVM) { return 0; }
SQInteger mask_ctor(HSQUIRRELVM v) {
	const SQInteger r = ctor(v);
	if (Sprite* s = spr(v)) s->is_mask = true;
	return r;
}

// transition(src, dst, rate, rule_image, time_ms, tempo): the sprite appears/disappears through the rule image.
SQInteger transition(HSQUIRRELVM v) {
	Sprite* s = spr(v);
	const std::string file = arg_string(v, 5);
	s->trans_file = file;
	s->trans_on = !file.empty();
	s->trans_hold = s->trans_on && host().renderer ? host().renderer->texture(file) : nullptr;
	s->trans_tex = s->trans_hold.get();
	s->trans_rate = arg_float(v, 4);
	s->trans.set({arg_float(v, 2)});
	s->trans.start({arg_float(v, 3)}, arg_float(v, 6), static_cast<int>(arg_int(v, 7)));
	return 0;
}
SQInteger is_transitioning(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->trans.active); return 1; }
SQInteger get_transition_timer(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->trans.active ? spr(v)->trans.duration - spr(v)->trans.timer : 0.f); return 1; }
SQInteger get_transition_file(HSQUIRRELVM v) { sq_pushstring(v, spr(v)->trans_file.c_str(), -1); return 1; }

// Post effects: set(value, time_ms, tempo) / get / timer (remaining ms) of one animated value of Sprite::fx.
using FxAnim = Anim<1> Sprite::Effects::*;
template <FxAnim M> SQInteger fx_set(HSQUIRRELVM v) {
	(spr(v)->fx.*M).start({arg_float(v, 2)}, arg_float(v, 3), static_cast<int>(arg_int(v, 4)));
	return 0;
}
template <FxAnim M> SQInteger fx_get(HSQUIRRELVM v) { sq_pushfloat(v, (spr(v)->fx.*M).value[0]); return 1; }
template <FxAnim M> SQInteger fx_timer(HSQUIRRELVM v) {
	const Anim<1>& a = spr(v)->fx.*M;
	sq_pushfloat(v, a.active ? a.duration - a.timer : 0.f);
	return 1;
}
SQInteger set_monochrome(HSQUIRRELVM v) { // (power, cb, cr, time, tempo)
	Sprite* s = spr(v);
	s->fx.mono_cb = arg_float(v, 3);
	s->fx.mono_cr = arg_float(v, 4);
	s->fx.mono.start({arg_float(v, 2)}, arg_float(v, 5), static_cast<int>(arg_int(v, 6)));
	return 0;
}
SQInteger get_mono_cb(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->fx.mono_cb); return 1; }
SQInteger get_mono_cr(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->fx.mono_cr); return 1; }
SQInteger set_shading_parameter(HSQUIRRELVM v) { // (level, density)
	spr(v)->fx.shade_level = static_cast<int>(arg_int(v, 2));
	spr(v)->fx.shade_density = arg_float(v, 3);
	return 0;
}
SQInteger get_shading_level(HSQUIRRELVM v) { sq_pushinteger(v, spr(v)->fx.shade_level); return 1; }
SQInteger get_shading_density(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->fx.shade_density); return 1; }

// setBlur(on, max, alpha, latency_ms, back): afterimages. off drops the recorded history; the latency is at least 16 ms.
SQInteger set_blur(HSQUIRRELVM v) {
	auto& b = spr(v)->blur;
	b.on = arg_int(v, 2) != 0;
	b.hist.clear();
	if (!b.on) return 0;
	const int max = static_cast<int>(arg_int(v, 3)), latency = static_cast<int>(arg_int(v, 5));
	b.count = max != 0 ? max : 1;
	b.alpha = arg_float(v, 4);
	b.latency = latency > 15 ? latency : 16;
	b.back = arg_int(v, 6) != 0;
	return 0;
}
SQInteger set_blur_mode(HSQUIRRELVM v) { // 0: a ghost is skipped when equal to the next one, 1: when equal to the sprite
	spr(v)->blur.mode = static_cast<int>(arg_int(v, 2));
	return 0;
}

// Distortions (ripple / raster / lens): animated parameters, plain centre / angle / phase.
SQInteger set_ripple(HSQUIRRELVM v) { // (x, y, power, frequency, speed, time, tempo)
	auto& f = spr(v)->fx;
	f.ripple_cx = arg_float(v, 2);
	f.ripple_cy = arg_float(v, 3);
	f.ripple.start({arg_float(v, 4), arg_float(v, 5), arg_float(v, 6)}, arg_float(v, 7), static_cast<int>(arg_int(v, 8)));
	return 0;
}
SQInteger set_raster(HSQUIRRELVM v) { // (rotate, power, frequency, speed, time, tempo)
	auto& f = spr(v)->fx;
	f.raster_rotate = arg_float(v, 2);
	f.raster.start({arg_float(v, 3), arg_float(v, 4), arg_float(v, 5)}, arg_float(v, 6), static_cast<int>(arg_int(v, 7)));
	return 0;
}
SQInteger set_lens(HSQUIRRELVM v) { // (x, y, power, radius, time, tempo)
	auto& f = spr(v)->fx;
	f.lens_cx = arg_float(v, 2);
	f.lens_cy = arg_float(v, 3);
	f.lens.start({arg_float(v, 4), arg_float(v, 5)}, arg_float(v, 6), static_cast<int>(arg_int(v, 7)));
	return 0;
}
template <auto M, int I> SQInteger warp_get(HSQUIRRELVM v) { sq_pushfloat(v, (spr(v)->fx.*M).value[I]); return 1; }
template <auto M> SQInteger warp_get_timer(HSQUIRRELVM v) {
	const auto& a = spr(v)->fx.*M;
	sq_pushfloat(v, a.active ? a.timer : 0.f);
	return 1;
}
template <auto M> SQInteger warp_set_timer(HSQUIRRELVM v) { (spr(v)->fx.*M).set_timer(arg_float(v, 2)); return 0; }
template <auto M> SQInteger fx_get_float(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->fx.*M); return 1; }
template <auto M> SQInteger fx_set_float(HSQUIRRELVM v) { spr(v)->fx.*M = arg_float(v, 2); return 0; }
SQInteger get_ripple_center(HSQUIRRELVM v) {
	set_ret(v, 1, spr(v)->fx.ripple_cx);
	set_ret(v, 2, spr(v)->fx.ripple_cy);
	return 0;
}
SQInteger get_lens_center(HSQUIRRELVM v) {
	set_ret(v, 1, spr(v)->fx.lens_cx);
	set_ret(v, 2, spr(v)->fx.lens_cy);
	return 0;
}

// setXTimer(ms): jumps the animation to that point; getXTimer(): elapsed ms (used when saving).
template <auto M> SQInteger anim_set_timer(HSQUIRRELVM v) {
	(spr(v)->*M).set_timer(arg_float(v, 2));
	return 0;
}
template <auto M> SQInteger anim_get_timer(HSQUIRRELVM v) {
	const auto& a = spr(v)->*M;
	sq_pushfloat(v, a.active ? a.timer : 0.f);
	return 1;
}
// Per-corner colour animations (index 0..3).
SQInteger set_color_timer(HSQUIRRELVM v) {
	const SQInteger i = arg_int(v, 2);
	if (i >= 0 && i < 4) spr(v)->color[static_cast<size_t>(i)].set_timer(arg_float(v, 3));
	return 0;
}
SQInteger get_color_timer(HSQUIRRELVM v) {
	const SQInteger i = arg_int(v, 2);
	const auto& a = spr(v)->color[static_cast<size_t>(i < 0 || i > 3 ? 0 : i)];
	sq_pushfloat(v, a.active ? a.timer : 0.f);
	return 1;
}
SQInteger is_shaking(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->shake.active); return 1; }
// Vector3D argument (instance with x / y members) -> x, y.
void vec_xy(HSQUIRRELVM v, SQInteger idx, float out[2]) {
	out[0] = out[1] = 0.f;
	const char* keys[2] = {"x", "y"};
	for (int i = 0; i < 2; i++) {
		sq_push(v, idx);
		sq_pushstring(v, keys[i], -1);
		if (SQ_SUCCEEDED(sq_get(v, -2))) {
			SQFloat f = 0;
			if (SQ_FAILED(sq_getfloat(v, -1, &f))) { SQInteger n = 0; sq_getinteger(v, -1, &n); f = static_cast<SQFloat>(n); }
			out[i] = static_cast<float>(f);
			sq_pop(v, 1);
		}
		sq_pop(v, 1);
	}
}
template <bool Random> SQInteger shake(HSQUIRRELVM v) { // (start vec, end vec, frequency ms, interpolation, time ms, tempo, beattempo)
	float a[2], b[2];
	vec_xy(v, 2, a);
	vec_xy(v, 3, b);
	spr(v)->shake.start(a[0], a[1], b[0], b[1], arg_float(v, 4), arg_float(v, 6), Random);
	return 0;
}
SQInteger get_shake_timer(HSQUIRRELVM v) { sq_pushfloat(v, spr(v)->shake.active ? spr(v)->shake.time - spr(v)->shake.timer : 0.f); return 1; }
SQInteger set_shake_timer(HSQUIRRELVM v) {
	Sprite* s = spr(v);
	if (s->shake.active) { s->shake.timer = 0; s->shake.update(arg_float(v, 2)); }
	return 0;
}
// Vector3D: plain x / y / z members.
SQInteger vec3_ctor(HSQUIRRELVM v) {
	static const char* keys[3] = {"x", "y", "z"};
	const SQInteger n = sq_gettop(v);
	for (int i = 0; i < 3; i++) {
		SQFloat f = 0;
		if (n >= 2 + i && SQ_FAILED(sq_getfloat(v, 2 + i, &f))) { SQInteger k = 0; sq_getinteger(v, 2 + i, &k); f = static_cast<SQFloat>(k); }
		sq_push(v, 1);
		sq_pushstring(v, keys[i], -1);
		sq_pushfloat(v, f);
		sq_set(v, -3);
		sq_pop(v, 1);
	}
	return 0;
}

static void register_effect_methods(ScriptHost& h) {
	h.add_method("setFadeTimer", anim_set_timer<&Sprite::fade>);
	h.add_method("getFadeTimer", anim_get_timer<&Sprite::fade>);
	h.add_method("setMoveTimer", anim_set_timer<&Sprite::move>);
	h.add_method("getMoveTimer", anim_get_timer<&Sprite::move>);
	h.add_method("setScaleTimer", anim_set_timer<&Sprite::scale>);
	h.add_method("getScaleTimer", anim_get_timer<&Sprite::scale>);
	h.add_method("setRotateTimer", anim_set_timer<&Sprite::rotate>);
	h.add_method("getRotateTimer", anim_get_timer<&Sprite::rotate>);
	h.add_method("setScrollTimer", anim_set_timer<&Sprite::scroll>);
	h.add_method("getScrollTimer", anim_get_timer<&Sprite::scroll>);
	h.add_method("setColorTimer", set_color_timer);
	h.add_method("getColorTimer", get_color_timer);
	h.add_method("isShaking", is_shaking);
	h.add_method("shake", shake<false>);
	h.add_method("randomShake", shake<true>);
	h.add_method("getShakeTimer", get_shake_timer);
	h.add_method("setShakeTimer", set_shake_timer);
	h.add_method("setActionCompatibleMode", ret_null);
	h.add_method("setColorHue", fx_set<&Sprite::Effects::hue>);
	h.add_method("getColorHue", fx_get<&Sprite::Effects::hue>);
	h.add_method("getColorHueTimer", fx_timer<&Sprite::Effects::hue>);
	h.add_method("setColorSaturation", fx_set<&Sprite::Effects::saturation>);
	h.add_method("getColorSaturation", fx_get<&Sprite::Effects::saturation>);
	h.add_method("getColorSaturationTimer", fx_timer<&Sprite::Effects::saturation>);
	h.add_method("setColorBrightness", fx_set<&Sprite::Effects::brightness>);
	h.add_method("getColorBrightness", fx_get<&Sprite::Effects::brightness>);
	h.add_method("getColorBrightnessTimer", fx_timer<&Sprite::Effects::brightness>);
	h.add_method("setColorGlow", fx_set<&Sprite::Effects::glow>);
	h.add_method("getColorGlow", fx_get<&Sprite::Effects::glow>);
	h.add_method("getColorGlowTimer", fx_timer<&Sprite::Effects::glow>);
	h.add_method("setSepiaPower", fx_set<&Sprite::Effects::sepia>);
	h.add_method("getSepiaPower", fx_get<&Sprite::Effects::sepia>);
	h.add_method("getSepiaPowerTimer", fx_timer<&Sprite::Effects::sepia>);
	h.add_method("setNegaposiPower", fx_set<&Sprite::Effects::nega>);
	h.add_method("getNegaposiPower", fx_get<&Sprite::Effects::nega>);
	h.add_method("getNegaposiPowerTimer", fx_timer<&Sprite::Effects::nega>);
	h.add_method("setMonochromePower", set_monochrome);
	h.add_method("getMonochromePower", fx_get<&Sprite::Effects::mono>);
	h.add_method("getMonochromePowerTimer", fx_timer<&Sprite::Effects::mono>);
	h.add_method("getMonochromeCb", get_mono_cb);
	h.add_method("getMonochromeCr", get_mono_cr);
	h.add_method("setShadingPower", fx_set<&Sprite::Effects::shade>);
	h.add_method("getShadingPower", fx_get<&Sprite::Effects::shade>);
	h.add_method("getShadingPowerTimer", fx_timer<&Sprite::Effects::shade>);
	h.add_method("setShadingParameter", set_shading_parameter);
	h.add_method("getShadingLevel", get_shading_level);
	h.add_method("getShadingDensity", get_shading_density);
	h.add_method("setBlur", set_blur);
	h.add_method("setBlurMode", set_blur_mode);
	using E = Sprite::Effects;
	h.add_method("setRipple", set_ripple);
	h.add_method("getRippleCenter", get_ripple_center);
	h.add_method("getRipplePower", warp_get<&E::ripple, 0>);
	h.add_method("getRippleFrequency", warp_get<&E::ripple, 1>);
	h.add_method("getRippleSpeed", warp_get<&E::ripple, 2>);
	h.add_method("getRippleTimer", warp_get_timer<&E::ripple>);
	h.add_method("setRippleTimer", warp_set_timer<&E::ripple>);
	h.add_method("getRipplePhase", fx_get_float<&E::ripple_phase>);
	h.add_method("setRipplePhase", fx_set_float<&E::ripple_phase>);
	h.add_method("setRaster", set_raster);
	h.add_method("getRasterRotate", fx_get_float<&E::raster_rotate>);
	h.add_method("getRasterPower", warp_get<&E::raster, 0>);
	h.add_method("getRasterFrequency", warp_get<&E::raster, 1>);
	h.add_method("getRasterSpeed", warp_get<&E::raster, 2>);
	h.add_method("getRasterTimer", warp_get_timer<&E::raster>);
	h.add_method("setRasterTimer", warp_set_timer<&E::raster>);
	h.add_method("getRasterPhase", fx_get_float<&E::raster_phase>);
	h.add_method("setRasterPhase", fx_set_float<&E::raster_phase>);
	h.add_method("setLens", set_lens);
	h.add_method("getLensCenter", get_lens_center);
	h.add_method("getLensPower", warp_get<&E::lens, 0>);
	h.add_method("getLensRadius", warp_get<&E::lens, 1>);
	h.add_method("getLensTimer", warp_get_timer<&E::lens>);
	h.add_method("setLensTimer", warp_set_timer<&E::lens>);
}

static void register_sprite_members(ScriptHost& h, SQFUNCTION constructor, bool render_target = false);
void register_sprite_class(ScriptHost& h) {
	h.begin_class("Vector3D");
	for (const char* k : {"x", "y", "z"}) { // members must exist as class slots for scripts to assign them
		sq_pushstring(h.vm(), k, -1);
		sq_pushfloat(h.vm(), 0.f);
		sq_newslot(h.vm(), -3, SQFalse);
	}
	h.add_method("constructor", vec3_ctor);
	h.end_class();
	h.begin_class("SQSprite");
	register_sprite_members(h, ctor);
	h.end_class();
	h.begin_class("SQSpriteMask");
	register_sprite_members(h, mask_ctor);
	h.add_method("setDiffuseOnly", set_diffuse_only);
	h.end_class();
	h.begin_class("SQSpriteRenderTarget");
	register_sprite_members(h, ctor, true);
	h.end_class();
}

static void register_sprite_members(ScriptHost& h, SQFUNCTION constructor, bool render_target) {
	h.add_method("constructor", constructor);
	if (render_target) {
		h.add_method("create", target_create);
		h.add_method("setCapture", target_set_capture);
		h.add_method("play", target_play);
		h.add_method("pause", target_stop);
		h.add_method("stop", target_stop);
		h.add_method("isCaptureEnabled", target_is_capture);
		h.add_method("saveImage", save_image);
		h.add_method("isDelayedRendering", target_false);
		h.add_method("serialize", target_true); // save files keep no native state: the scripts rebuild the surface
		h.add_method("deserialize", target_true);
		for (const char* n : {"setDelayRendering", "setCaptureTarget", "setCopyTarget", "createCustomEffect", "setCustomEffectParameter",
							  "clear", "setTextureFiltering"})
			h.add_method(n, target_noop);
	} else {
		h.add_method("create", create);
		h.add_method("createSubresource", create);
		h.add_method("createColor", create_color);
		h.add_method("createScreen", create_screen);
		h.add_method("saveImage", save_image);
		register_movie_methods(h);
	}
	h.add_method("setSurface", set_surface);
	register_effect_methods(h);
	register_sprite_property_methods(h);
	h.add_method("setClickable", set_clickable);
	h.add_method("isOver", is_over);
	h.add_method("isDown", is_down);
	h.add_method("isUp", is_up);
	h.add_method("setChangeCursor", ret_null);
	h.add_method("scroll", scroll);
	h.add_method("getScroll", get_scroll);
	h.add_method("getSize", get_size);
	h.add_method("setColor", set_color);
	h.add_method("getColor", get_color);
	h.add_method("isDeletedTexture", is_deleted_texture);
	h.add_method("setMask", set_mask);
	h.add_method("setDisplacementMap", set_displacement_map);
	h.add_method("transition", transition);
	h.add_method("isTransitioning", is_transitioning);
	h.add_method("getTransitionTimer", get_transition_timer);
	h.add_method("getTransitionFilename", get_transition_file);
}

} // namespace omw
