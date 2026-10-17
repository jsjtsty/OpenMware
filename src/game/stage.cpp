#include "game/stage.h"

#include "game/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace omw {

void Sprite::Shake::start(float fx, float fy, float tx, float ty, float freq_ms, float time_ms, bool rnd) {
	from[0] = fx; from[1] = fy; to[0] = tx; to[1] = ty;
	freq = freq_ms > 0 ? freq_ms : 17.f;
	time = time_ms;
	timer = 0;
	random = rnd;
	active = time_ms > 0;
	off[0] = off[1] = 0;
	if (active) update(0);
}

void Sprite::Shake::update(float dt_ms) {
	if (!active) return;
	timer += dt_ms;
	if (timer >= time) {
		active = false;
		off[0] = off[1] = 0;
		return;
	}
	const float k = timer / time;
	const int step = static_cast<int>(timer / freq);
	for (int i = 0; i < 2; i++) {
		const float amp = from[i] + (to[i] - from[i]) * k;
		float sign = (step & 1) ? -1.f : 1.f;
		if (random) {
			seed = seed * 1664525u + 1013904223u + static_cast<uint32_t>(step) * 2654435761u + i;
			sign = static_cast<float>((seed >> 8) & 0xFFFF) / 32767.5f - 1.f;
		}
		off[i] = amp * sign;
	}
}

Sprite::Sprite() {
	for (auto& c : color) c.set({255.f, 255.f, 255.f});
}

void Sprite::update(float dt_ms) {
	const float dt = dt_ms * time_scale;
	if (tick) tick(dt);
	move.update(dt);
	scale.update(dt);
	rotate.update(dt);
	fade.update(dt);
	scroll.update(dt);
	shake.update(dt);
	trans.update(dt);
	for (auto& c : color) c.update(dt);
	fx.update(dt);
}

std::shared_ptr<Sprite> Stage::create(const std::string& name) {
	auto s = std::make_shared<Sprite>();
	s->name = name;
	s->order = next_order_++;
	sprites_.push_back(s);
	return s;
}

void Stage::remove(const Sprite* s) {
	sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(), [s](const auto& p) { return p.get() == s; }), sprites_.end());
}

void Stage::update(float dt_ms) {
	for (auto& s : sprites_) s->update(dt_ms);
	// Sprites the script no longer holds disappear from the stage.
	sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(), [](const auto& p) { return p->released; }), sprites_.end());
}

namespace {
DrawQuad make_quad(const Sprite* s) {
	DrawQuad d;
	d.texture = s->texture;
	d.a = s->fade.value[0];
	const float w = s->width, h = s->height;
	const float px = s->pivot_x * w, py = s->pivot_y * h;
	const float cs = std::cos(s->rotate.value[2]), sn = std::sin(s->rotate.value[2]);
	const float lx[4] = {0, w, w, 0}, ly[4] = {0, 0, h, h};
	for (int i = 0; i < 4; i++) {
		const float dx = (lx[i] - px) * s->scale.value[0], dy = (ly[i] - py) * s->scale.value[1];
		d.x[i] = s->move.value[0] + s->shake.off[0] + px + dx * cs - dy * sn;
		d.y[i] = s->move.value[1] + s->shake.off[1] + py + dx * sn + dy * cs;
		d.r[i] = s->color[i].value[0] / 255.f;
		d.g[i] = s->color[i].value[1] / 255.f;
		d.b[i] = s->color[i].value[2] / 255.f;
	}
	float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
	if (s->texture && s->kind == Sprite::Kind::Image) {
		const float tw = static_cast<float>(s->texture->w), th = static_cast<float>(s->texture->h);
		const float sw = s->sub_w > 0 ? static_cast<float>(s->sub_w) : tw, sh = s->sub_h > 0 ? static_cast<float>(s->sub_h) : th;
		u0 = s->sub_x / tw;
		v0 = s->sub_y / th;
		u1 = (s->sub_x + sw) / tw;
		v1 = (s->sub_y + sh) / th;
	}
	u0 += s->scroll.value[0];
	u1 += s->scroll.value[0];
	v0 += s->scroll.value[1];
	v1 += s->scroll.value[1];
	d.u[0] = u0; d.v[0] = v0;
	d.u[1] = u1; d.v[1] = v0;
	d.u[2] = u1; d.v[2] = v1;
	d.u[3] = u0; d.v[3] = v1;
	return d;
}

DrawItem make_item(const Sprite* s) {
	DrawItem d;
	static_cast<DrawQuad&>(d) = make_quad(s);
	d.blend = s->blend;
	if (s->kind == Sprite::Kind::Screen) {
		d.snap = DrawItem::Snap::Screen;
		d.rx = static_cast<float>(s->sub_x);
		d.ry = static_cast<float>(s->sub_y);
		d.rw = s->sub_w > 0 ? static_cast<float>(s->sub_w) : s->width;
		d.rh = s->sub_h > 0 ? static_cast<float>(s->sub_h) : s->height;
	} else if (s->is_capture && s->capturing) {
		d.snap = DrawItem::Snap::Capture;
		s->drawn_capturing = true;
		if (s->freeze_after_draw) {
			s->freeze_after_draw = false;
			const_cast<Sprite*>(s)->capturing = false; // this frame still takes the picture
		}
	}
	d.premult = s->is_target && !s->is_capture;
	if (const auto m = s->mask.lock(); m && m->kind != Sprite::Kind::None) {
		d.has_mask = true;
		d.mask = make_quad(m.get());
	}
	if (const auto m = s->displacement.lock(); m && m->texture) {
		d.has_disp = true;
		d.disp = make_quad(m.get());
	}
	if (s->trans_on && s->trans_tex) {
		d.trans_tex = s->trans_tex;
		d.trans_now = std::clamp(s->trans.value[0], 0.f, 1.f);
		d.trans_rate = s->trans_rate;
	}
	return d;
}
} // namespace

std::shared_ptr<Sprite> Stage::find(const std::string& name) const {
	for (auto it = sprites_.rbegin(); it != sprites_.rend(); ++it)
		if ((*it)->name == name && !(*it)->released) return *it;
	return nullptr;
}

void Stage::build_draw_list(DrawList& out) const {
	out.clear();
	std::vector<const Sprite*> order;
	order.reserve(sprites_.size());
	for (const auto& s : sprites_)
		if (s->kind != Sprite::Kind::None && !s->is_mask && !s->released && !s->erase && s->fade.value[0] > 0.f) order.push_back(s.get());
	std::stable_sort(order.begin(), order.end(), [](const Sprite* a, const Sprite* b) {
		return a->priority != b->priority ? a->priority < b->priority : a->order < b->order;
	});

	// Sprites per surface (null: the game frame), then the passes with the surfaces a surface contains first.
	struct Entry {
		const Sprite* s;
		DrawItem item;
		std::vector<RenderPass> pre; // effect chain that has to run before the pass this item is drawn in
	};
	std::map<const Sprite*, std::vector<Entry>> buckets;
	int next_temp = 0;
	for (const Sprite* s : order) {
		const auto t = s->surface.lock();
		const Sprite* target = t && t->is_target && !t->released && t.get() != s && t->texture ? t.get() : nullptr;
		const int w = target ? target->texture->w : kWidth, h = target ? target->texture->h : kHeight;
		Entry e{s, make_item(s), {}};
		if (s->fx.any() && e.item.snap == DrawItem::Snap::None) {
			// Draw the sprite alone into a temporary texture, run the effects over it, then composite the result.
			const int a = next_temp++, b = next_temp++;
			int cur = a;
			RenderPass draw;
			draw.temp_target = a;
			draw.hi = !target;
			draw.w = w;
			draw.h = h;
			DrawItem single = e.item;
			single.blend = BLEND_NORMAL;
			single.a = 1.f;
			draw.items.push_back(single);
			e.pre.push_back(std::move(draw));
			auto effect = [&](RenderPass::Op op, int in, int dst) {
				RenderPass p;
				p.op = op;
				p.in_temp = in;
				p.hi = !target;
				p.temp_target = dst;
				p.w = w;
				p.h = h;
				return p;
			};
			if (const float shade = s->fx.shade.value[0]; shade > 0.f) {
				const float sigma = shade * (10.f + 8.f * static_cast<float>(s->fx.shade_level));
				RenderPass bx = effect(RenderPass::Op::Blur, cur, b);
				bx.p[0] = 1.f / static_cast<float>(w);
				bx.p[2] = sigma;
				RenderPass by = effect(RenderPass::Op::Blur, b, cur);
				by.p[1] = 1.f / static_cast<float>(h);
				by.p[2] = sigma;
				e.pre.push_back(std::move(bx));
				e.pre.push_back(std::move(by));
			}
			if (s->fx.any_warp()) {
					// bounding box of the sprite in the temporary texture (uv); centres are given in sprite pixels
					float bx0 = e.item.x[0], bx1 = bx0, by0 = e.item.y[0], by1 = by0;
					for (int i = 1; i < 4; i++) {
						bx0 = std::min(bx0, e.item.x[i]);
						bx1 = std::max(bx1, e.item.x[i]);
						by0 = std::min(by0, e.item.y[i]);
						by1 = std::max(by1, e.item.y[i]);
					}
					const float fw = static_cast<float>(w), fh = static_cast<float>(h);
					const float sw = std::max(1.f, s->width), sh = std::max(1.f, s->height);
					auto warp = [&](int kind, float cx, float cy, float power, float freq, float phase, float extra) {
						RenderPass p = effect(RenderPass::Op::Warp, cur, cur == a ? b : a);
						p.p[0] = static_cast<float>(kind);
						p.p[1] = (bx0 + cx / sw * (bx1 - bx0)) / fw;
						p.p[2] = (by0 + cy / sh * (by1 - by0)) / fh;
						p.p[3] = fw / fh;
						p.p[4] = power;
						p.p[5] = freq;
						p.p[6] = phase;
						p.p[7] = extra;
						p.p[8] = sw;
						p.p[9] = sh;
						p.p[12] = bx0 / fw;
						p.p[13] = by0 / fh;
						p.p[14] = bx1 / fw;
						p.p[15] = by1 / fh;
						cur = p.temp_target;
						e.pre.push_back(std::move(p));
					};
					const auto& fx = s->fx;
					if (fx.lens.value[0] != 0) warp(1, fx.lens_cx, fx.lens_cy, fx.lens.value[0], 0.f, 0.f, fx.lens.value[1]);
					if (fx.ripple.value[0] != 0) warp(0, fx.ripple_cx, fx.ripple_cy, fx.ripple.value[0], fx.ripple.value[1], fx.ripple_phase, 0.f);
					if (fx.raster.value[0] != 0) warp(2, 0.f, 0.f, fx.raster.value[0], fx.raster.value[1], fx.raster_phase, fx.raster_rotate * 0.017453292f);
				}
				if (s->fx.any_color()) {
				RenderPass c = effect(RenderPass::Op::Color, cur, cur == a ? b : a);
				c.p[0] = s->fx.hue.value[0];
				c.p[1] = s->fx.saturation.value[0];
				c.p[2] = s->fx.brightness.value[0];
				c.p[3] = s->fx.glow.value[0];
				c.p[4] = s->fx.sepia.value[0];
				c.p[5] = s->fx.nega.value[0];
				c.p[6] = s->fx.mono.value[0];
				c.p[8] = s->fx.mono_cb;
				c.p[9] = s->fx.mono_cr;
				cur = c.temp_target;
				e.pre.push_back(std::move(c));
			}
			DrawItem comp;
			comp.x[0] = comp.x[3] = comp.y[0] = comp.y[1] = 0.f;
			comp.x[1] = comp.x[2] = static_cast<float>(w);
			comp.y[2] = comp.y[3] = static_cast<float>(h);
			comp.u[0] = comp.u[3] = comp.v[0] = comp.v[1] = 0.f;
			comp.u[1] = comp.u[2] = comp.v[2] = comp.v[3] = 1.f;
			for (int i = 0; i < 4; i++) comp.r[i] = comp.g[i] = comp.b[i] = 1.f;
			comp.a = e.item.a;
			comp.blend = s->blend;
			comp.premult = true;
			comp.temp = cur;
			e.item = comp;
		}
		buckets[target].push_back(std::move(e));
	}
	std::set<const Sprite*> emitted;
	std::function<void(const Sprite*, int)> emit = [&](const Sprite* target, int depth) {
		if (!emitted.insert(target).second || depth > 16) return;
		auto& list = buckets[target];
		for (const auto& e : list)
			if (e.s->is_target && !e.s->is_capture) emit(e.s, depth + 1);
		for (auto& e : list)
			for (auto& p : e.pre) out.push_back(std::move(p));
		RenderPass pass;
		pass.hi = !target;
		if (target) {
			pass.target = target->texture;
			pass.w = target->texture->w;
			pass.h = target->texture->h;
		} else {
			pass.w = kWidth;
			pass.h = kHeight;
		}
		for (auto& e : list) pass.items.push_back(std::move(e.item));
		out.push_back(std::move(pass));
	};
	emit(nullptr, 0); // surfaces whose own parent is invisible are not reached, and so not drawn
}

namespace {
bool sprite_hit(const Sprite& s, float x, float y) {
	if (s.width <= 0 || s.height <= 0 || s.scale.value[0] == 0 || s.scale.value[1] == 0) return false;
	const float px = s.pivot_x * s.width, py = s.pivot_y * s.height;
	const float cs = std::cos(s.rotate.value[2]), sn = std::sin(s.rotate.value[2]);
	const float ox = x - s.move.value[0] - px, oy = y - s.move.value[1] - py;
	const float dx = ox * cs + oy * sn, dy = -ox * sn + oy * cs; // inverse rotation
	const float lx = dx / s.scale.value[0] + px, ly = dy / s.scale.value[1] + py;
	if (lx < 0 || ly < 0 || lx >= s.width || ly >= s.height) return false;
	if (s.check_alpha && s.texture && !s.texture->alpha.empty()) {
		const float sw = s.sub_w > 0 ? static_cast<float>(s.sub_w) : static_cast<float>(s.texture->w - s.sub_x);
		const float sh = s.sub_h > 0 ? static_cast<float>(s.sub_h) : static_cast<float>(s.texture->h - s.sub_y);
		const int tx = std::clamp(s.sub_x + static_cast<int>(lx * sw / s.width), 0, s.texture->w - 1);
		const int ty = std::clamp(s.sub_y + static_cast<int>(ly * sh / s.height), 0, s.texture->h - 1);
		return s.texture->alpha[static_cast<size_t>(ty) * s.texture->w + tx] > 0;
	}
	return true;
}
} // namespace

void Stage::update_pointer(float x, float y) {
	prev_hot_ = hot_;
	hot_ = nullptr;
	for (const auto& sp : sprites_) {
		const Sprite* s = sp.get();
		if (!s->clickable || s->released || !sprite_hit(*s, x, y)) continue;
		if (!hot_ || s->priority > hot_->priority || (s->priority == hot_->priority && s->order > hot_->order)) hot_ = s;
	}
}

void Stage::dump() const {
	for (const auto& s : sprites_)
		std::printf("  sprite %-28s kind=%d prio=%d fade=%.2f pos=(%.0f,%.0f) size=%.0fx%.0f scale=(%.2f,%.2f) tex=%d erase=%d trans=%.2f/%d fadeanim=%d moveanim=%d\n", s->name.c_str(),
					static_cast<int>(s->kind), s->priority, s->fade.value[0], s->move.value[0], s->move.value[1], s->width, s->height,
					s->scale.value[0], s->scale.value[1], s->texture ? 1 : 0, s->erase ? 1 : 0, s->trans.value[0], s->trans.active ? 1 : 0, s->fade.active ? 1 : 0,
					s->move.active ? 1 : 0);
}

} // namespace omw
