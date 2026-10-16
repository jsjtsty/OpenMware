// SQSprite: script-visible 2D sprites on the Stage. The property animations (move/scale/rotate/fade/colour) follow the
// original's "target value, time in ms, tempo curve" model; details that are not yet verified against the original are
// marked TODO and tracked in docs/script-notes.md.
#include "game/stage.h"
#include "render/renderer2d.h"
#include "script/bind.h"
#include "script/host.h"

#include <cmath>
#include <cstdio>

namespace omw {
namespace {

using sqb::arg_float;
using sqb::arg_int;
using sqb::arg_string;

constexpr float kDeg2Rad = 3.14159265358979f / 180.f;

struct SpriteRef {
	std::shared_ptr<Sprite> sprite;
	~SpriteRef() {
		if (sprite) sprite->erase = true; // the script dropped its last reference: the sprite leaves the stage
	}
};

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
	s->texture = host().renderer ? host().renderer->texture(file) : nullptr;
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
	return 0;
}

SQInteger create_screen(HSQUIRRELVM v) { // (x, y, w, h): capture of the screen; not drawn yet (TODO)
	Sprite* s = spr(v);
	s->kind = Sprite::Kind::Screen;
	s->width = arg_float(v, 4, -1) > 0 ? arg_float(v, 4) : static_cast<float>(Stage::kWidth);
	s->height = arg_float(v, 5, -1) > 0 ? arg_float(v, 5) : static_cast<float>(Stage::kHeight);
	return 0;
}

SQInteger set_priority(HSQUIRRELVM v) { spr(v)->priority = static_cast<int>(arg_int(v, 2)); return 0; }
SQInteger get_priority(HSQUIRRELVM v) { sq_pushinteger(v, spr(v)->priority); return 1; }
SQInteger set_erase(HSQUIRRELVM v) { spr(v)->erase = arg_int(v, 2) != 0; return 0; }
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
SQInteger is_moving(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->move.active); return 1; }
SQInteger is_fading(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->fade.active); return 1; }
SQInteger is_scaling(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->scale.active); return 1; }
SQInteger is_rotating(HSQUIRRELVM v) { sq_pushbool(v, spr(v)->rotate.active); return 1; }

} // namespace

void register_sprite_class(ScriptHost& h) {
	h.begin_class("SQSprite");
	h.add_method("constructor", ctor);
	h.add_method("create", create);
	h.add_method("createSubresource", create);
	h.add_method("createColor", create_color);
	h.add_method("createScreen", create_screen);
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
	h.add_method("scroll", scroll);
	h.add_method("getScroll", get_scroll);
	h.add_method("setPivot", set_pivot);
	h.add_method("getSize", get_size);
	h.add_method("setColor", set_color);
	h.add_method("getColor", get_color);
	h.add_method("isDeletedTexture", is_deleted_texture);
	h.add_method("isMoving", is_moving);
	h.add_method("isFading", is_fading);
	h.add_method("isScaling", is_scaling);
	h.add_method("isRotating", is_rotating);
	h.end_class();
}

} // namespace omw
