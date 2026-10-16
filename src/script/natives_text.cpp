// SQText: marked-up text drawn into a bitmap that the stage shows as a sprite. Typing (the character-by-character reveal)
// advances from the sprite's per-frame tick. TODO: per-character effects (fadeCharacter, moveCharacter, shake, ...) and the
// transition/mask/displacement variants are accepted but not drawn.
#include "game/text_markup.h"
#include "render/renderer2d.h"
#include "render/text_raster.h"
#include "script/bind.h"
#include "script/host.h"
#include "script/sprite_ref.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace omw {
namespace {

using sqb::arg_float;
using sqb::arg_int;
using sqb::arg_string;

ScriptHost& host() { return *ScriptHost::current(); }

struct TextRef : SpriteRef {
	TextStyle style;
	std::string face, ruby_face;
	std::string raw;
	std::vector<TextRun> runs;
	TextLayout layout;
	float font_color[6][3];
	float shadow_color[6][3];
	bool dirty = true;
	int shown = -1;     // characters currently rasterised
	int bmp_w = 0, bmp_h = 0;
	Texture* tex = nullptr;

	// typing
	bool typing = false;
	float typing_speed = 0.f, typing_elapsed = 0.f, min_time = 0.f, max_time = 0.f, typing_total = 0.f;

	TextRef() {
		for (auto& c : font_color) c[0] = c[1] = c[2] = 1.f;
		for (auto& c : shadow_color) c[0] = c[1] = c[2] = 0.f;
	}
	~TextRef() {
		if (tex && ScriptHost::current() && ScriptHost::current()->renderer) ScriptHost::current()->renderer->destroy_dynamic_texture(tex);
		if (sprite) {
			sprite->texture = nullptr;
			sprite->tick = nullptr; // the lambda points at this object
		}
	}

	int visible() const {
		if (!typing || typing_total <= 0.f) return layout.char_count;
		const float f = std::clamp(typing_elapsed / typing_total, 0.f, 1.f);
		return std::min(layout.char_count, static_cast<int>(std::ceil(f * layout.char_count)));
	}

	void relayout() {
		Renderer2D* r = host().renderer;
		if (!r) return;
		style.font = r->fonts().lookup(face);
		if (host().verbose) std::printf("[text] face '%s' size %.0f color %.2f %.2f %.2f\n", face.c_str(), style.font.size, font_color[0][0], font_color[0][1], font_color[0][2]);
		style.ruby_font = r->fonts().lookup(ruby_face.empty() ? face : ruby_face);
		for (int i = 0; i < 3; i++) {
			style.color[i] = font_color[0][i];
			style.shadow_color[i] = shadow_color[0][i];
		}
		layout = layout_text(runs, style);
		dirty = false;
		shown = -1;
	}

	void refresh() {
		Renderer2D* r = host().renderer;
		if (!r) return;
		if (dirty) relayout();
		const int vis = visible();
		if (vis == shown) return;
		shown = vis;
		bmp_w = std::max(1, style.field_w > 0 ? style.field_w : static_cast<int>(std::ceil(layout.width)) + 1);
		bmp_h = std::max(1, std::max(style.field_h, static_cast<int>(std::ceil(layout.height))));
		const int ss = r->supersample(); // the bitmap carries ss x the logical resolution; the sprite keeps the logical size
		std::vector<uint8_t> px;
		rasterize_text(layout, style, vis, bmp_w * ss, bmp_h * ss, px, static_cast<float>(ss));
		if (!tex) tex = r->create_dynamic_texture(bmp_w * ss, bmp_h * ss, px.data());
		else r->replace_dynamic_texture(tex, bmp_w * ss, bmp_h * ss, px.data());
		if (sprite) {
			sprite->kind = Sprite::Kind::Image;
			sprite->texture = tex;
			sprite->width = static_cast<float>(bmp_w);
			sprite->height = static_cast<float>(bmp_h);
		}
	}
};

TextRef* txt(HSQUIRRELVM v) { return sqb::self<TextRef>(v); }

void set_ret(HSQUIRRELVM v, int n, float value) {
	const std::string name = "SQMATH_RET_" + std::to_string(n);
	sq_pushroottable(v);
	sq_pushstring(v, name.c_str(), -1);
	sq_pushfloat(v, value);
	sq_newslot(v, -3, SQFalse);
	sq_pop(v, 1);
}

SQInteger ctor(HSQUIRRELVM v) {
	TextRef* t = sqb::attach<TextRef>(v);
	if (host().stage) t->sprite = host().stage->create(arg_string(v, 2));
	else t->sprite = std::make_shared<Sprite>();
	t->sprite->kind = Sprite::Kind::None;
	TextRef* raw = t;
	t->sprite->tick = [raw](float dt_ms) {
		if (raw->typing) {
			raw->typing_elapsed += dt_ms / 1000.f;
			if (raw->typing_elapsed >= raw->typing_total) raw->typing = false;
		}
		raw->refresh();
	};
	return 0;
}

SQInteger ret_null(HSQUIRRELVM) { return 0; }

SQInteger set_text(HSQUIRRELVM v) { // create(text) / print(text)
	TextRef* t = txt(v);
	t->raw = arg_string(v, 2);
	if (host().verbose) std::printf("[f%d] [text %s] set \"%s\" field %dx%d\n", host().frame, t->sprite->name.c_str(), t->raw.c_str(), t->style.field_w, t->style.field_h);
	t->runs = parse_text_markup(t->raw);
	t->dirty = true;
	t->typing = false;
	t->typing_elapsed = 0;
	t->refresh();
	return 0;
}
SQInteger clear_text(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	t->raw.clear();
	t->runs.clear();
	t->dirty = true;
	t->typing = false;
	t->refresh();
	return 0;
}
SQInteger get_string(HSQUIRRELVM v) { sq_pushstring(v, txt(v)->raw.c_str(), -1); return 1; }

SQInteger set_font(HSQUIRRELVM v) { TextRef* t = txt(v); t->face = arg_string(v, 2); t->dirty = true; return 0; }
SQInteger set_ruby_font(HSQUIRRELVM v) { TextRef* t = txt(v); t->ruby_face = arg_string(v, 2); t->dirty = true; return 0; }

SQInteger set_shadow(HSQUIRRELVM v) { // (type, gap, power, alpha exponent)
	TextRef* t = txt(v);
	t->style.shadow_type = static_cast<int>(arg_int(v, 2));
	t->style.shadow_gap = static_cast<int>(arg_int(v, 3));
	t->style.shadow_power = arg_float(v, 4, 1.f);
	t->dirty = true;
	return 0;
}

SQInteger set_font_color(HSQUIRRELVM v) { // (slot, r, g, b, time, tempo)
	TextRef* t = txt(v);
	const int i = static_cast<int>(arg_int(v, 2));
	for (int k = i < 0 ? 0 : i; k < (i < 0 ? 6 : i + 1) && k < 6; k++) { // slot -1 = every slot
		t->font_color[k][0] = arg_float(v, 3);
		t->font_color[k][1] = arg_float(v, 4);
		t->font_color[k][2] = arg_float(v, 5);
		t->dirty = true;
	}
	return 0;
}
SQInteger get_font_color(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	const int i = std::clamp(static_cast<int>(arg_int(v, 2)), 0, 5);
	for (int c = 0; c < 3; c++) set_ret(v, c + 1, t->font_color[i][c]);
	return 0;
}
SQInteger set_shadow_color(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	const int i = static_cast<int>(arg_int(v, 2));
	for (int k = i < 0 ? 0 : i; k < (i < 0 ? 6 : i + 1) && k < 6; k++) { // slot -1 = every slot
		t->shadow_color[k][0] = arg_float(v, 3);
		t->shadow_color[k][1] = arg_float(v, 4);
		t->shadow_color[k][2] = arg_float(v, 5);
		t->dirty = true;
	}
	return 0;
}

SQInteger set_field(HSQUIRRELVM v) { // (w, h, type)
	TextRef* t = txt(v);
	t->style.field_w = static_cast<int>(arg_int(v, 2));
	t->style.field_h = static_cast<int>(arg_int(v, 3));
	t->dirty = true;
	return 0;
}
SQInteger get_field_size(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	set_ret(v, 1, static_cast<float>(t->style.field_w));
	set_ret(v, 2, static_cast<float>(t->style.field_h));
	return 0;
}
SQInteger get_size(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	t->refresh();
	set_ret(v, 1, t->layout.width);
	set_ret(v, 2, t->layout.height);
	return 0;
}
SQInteger set_pitch_space(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	t->style.pitch_full = static_cast<int>(arg_int(v, 2));
	t->style.pitch_half = static_cast<int>(arg_int(v, 3, t->style.pitch_full));
	t->dirty = true;
	return 0;
}
SQInteger set_row_space(HSQUIRRELVM v) { TextRef* t = txt(v); t->style.row_space = static_cast<int>(arg_int(v, 2)); t->dirty = true; return 0; }
SQInteger set_ruby_offset(HSQUIRRELVM v) { TextRef* t = txt(v); t->style.ruby_offset = static_cast<int>(arg_int(v, 2)); t->dirty = true; return 0; }

SQInteger rect_result(HSQUIRRELVM v, const std::vector<Rect>& rects, SQInteger idx) {
	Rect r;
	if (idx >= 0 && idx < static_cast<SQInteger>(rects.size())) r = rects[static_cast<size_t>(idx)];
	set_ret(v, 1, r.x);
	set_ret(v, 2, r.y);
	set_ret(v, 3, r.w);
	set_ret(v, 4, r.h);
	return 0;
}
SQInteger get_character_rect(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	if (t->dirty) t->relayout();
	return rect_result(v, t->layout.char_rects, arg_int(v, 2));
}
SQInteger get_ruby_rect(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	if (t->dirty) t->relayout();
	return rect_result(v, t->layout.ruby_rects, arg_int(v, 2));
}

// ---- typing ----
void restart_typing(TextRef* t) {
	t->refresh();
	if (t->typing_speed <= 0.f || t->layout.char_count == 0) {
		t->typing = false;
		t->typing_total = 0;
		return;
	}
	float total = static_cast<float>(t->layout.char_count) / (t->typing_speed * 30.f);
	if (t->max_time > 0.f) total = std::min(total, t->max_time);
	total = std::max(total, t->min_time);
	t->typing_total = total;
	t->typing_elapsed = 0;
	t->typing = true;
	t->shown = -1;
	t->refresh();
}
SQInteger begin_type(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	if (arg_int(v, 2)) restart_typing(t);
	return 0;
}
SQInteger reset_type(HSQUIRRELVM v) { TextRef* t = txt(v); t->typing = false; t->refresh(); return 0; }
SQInteger push_all(HSQUIRRELVM v) { TextRef* t = txt(v); t->typing = false; t->refresh(); return 0; }
SQInteger is_typing(HSQUIRRELVM v) { sq_pushbool(v, txt(v)->typing); return 1; }
SQInteger set_typing_speed(HSQUIRRELVM v) { txt(v)->typing_speed = arg_float(v, 2); return 0; }
SQInteger get_typing_speed(HSQUIRRELVM v) { sq_pushfloat(v, txt(v)->typing_speed); return 1; }
SQInteger set_min_time(HSQUIRRELVM v) { txt(v)->min_time = arg_float(v, 2); return 0; }
SQInteger set_max_time(HSQUIRRELVM v) { txt(v)->max_time = arg_float(v, 2); return 0; }
SQInteger get_min_time(HSQUIRRELVM v) { sq_pushfloat(v, txt(v)->min_time); return 1; }
SQInteger get_max_time(HSQUIRRELVM v) { sq_pushfloat(v, txt(v)->max_time); return 1; }
SQInteger get_total_time(HSQUIRRELVM v) { sq_pushfloat(v, txt(v)->typing_total); return 1; }
SQInteger get_remain_time(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	sq_pushfloat(v, t->typing ? std::max(0.f, t->typing_total - t->typing_elapsed) : 0.f);
	return 1;
}
SQInteger get_typing_timer(HSQUIRRELVM v) { TextRef* t = txt(v); sq_pushfloat(v, t->typing ? t->typing_elapsed : 0.f); return 1; }
SQInteger set_typing_timer(HSQUIRRELVM v) {
	TextRef* t = txt(v);
	if (t->typing) {
		t->typing_elapsed = std::min(std::max(0.f, arg_float(v, 2)), t->typing_total);
		t->refresh();
	}
	return 0;
}
SQInteger ret_zero(HSQUIRRELVM v) { sq_pushinteger(v, 0); return 1; }
SQInteger ret_false(HSQUIRRELVM v) { sq_pushbool(v, SQFalse); return 1; }

} // namespace

void register_text_class(ScriptHost& h) {
	h.begin_class("SQText");
	h.add_method("constructor", ctor);
	h.add_method("initialize", ret_null);
	register_sprite_property_methods(h);
	h.add_method("create", set_text);
	h.add_method("print", set_text);
	h.add_method("clear", clear_text);
	h.add_method("getString", get_string);
	h.add_method("setFont", set_font);
	h.add_method("setRubyFont", set_ruby_font);
	h.add_method("setShadow", set_shadow);
	h.add_method("setShadowShadingParameter", ret_null);
	h.add_method("setFontColor", set_font_color);
	h.add_method("getFontColor", get_font_color);
	h.add_method("setShadowColor", set_shadow_color);
	h.add_method("setField", set_field);
	h.add_method("getFieldSize", get_field_size);
	h.add_method("getSize", get_size);
	h.add_method("setPitchSpace", set_pitch_space);
	h.add_method("setRowSpace", set_row_space);
	h.add_method("setRubyOffset", set_ruby_offset);
	h.add_method("setRubyMarginRate", ret_null);
	h.add_method("getCharacterRect", get_character_rect);
	h.add_method("getRubyRect", get_ruby_rect);
	h.add_method("beginType", begin_type);
	h.add_method("resetType", reset_type);
	h.add_method("pushAll", push_all);
	h.add_method("isTyping", is_typing);
	h.add_method("setTypingSpeed", set_typing_speed);
	h.add_method("getTypingSpeed", get_typing_speed);
	h.add_method("setMinTypingTime", set_min_time);
	h.add_method("setMaxTypingTime", set_max_time);
	h.add_method("getMinTypingTime", get_min_time);
	h.add_method("getMaxTypingTime", get_max_time);
	h.add_method("setTypingThreshold", ret_null);
	h.add_method("getTotalTime", get_total_time);
	h.add_method("getDurationTime", get_total_time);
	h.add_method("getTypingTimer", get_typing_timer);
	h.add_method("setTypingTimer", set_typing_timer);
	h.add_method("getRemainTime", get_remain_time);
	h.add_method("setLock", ret_null);
	h.add_method("setDisused", ret_null);
	h.add_method("getLock", ret_zero);
	h.add_method("getDisused", ret_zero);
	h.add_method("resetSpline", ret_null);
	h.add_method("resetBezier", ret_null);
	h.add_method("isShaking", ret_false);
	h.end_class();
}

} // namespace omw
