#include "game/stage.h"

#include "game/texture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>

namespace omw {

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
	for (auto& c : color) c.update(dt);
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
		d.x[i] = s->move.value[0] + px + dx * cs - dy * sn;
		d.y[i] = s->move.value[1] + py + dx * sn + dy * cs;
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
		if (s->kind != Sprite::Kind::None && !s->is_mask && !s->released && s->fade.value[0] > 0.f) order.push_back(s.get());
	std::stable_sort(order.begin(), order.end(), [](const Sprite* a, const Sprite* b) {
		return a->priority != b->priority ? a->priority < b->priority : a->order < b->order;
	});

	// Sprites per surface (null: the game frame), then the passes with the surfaces a surface contains first.
	std::map<const Sprite*, std::vector<std::pair<const Sprite*, DrawItem>>> buckets;
	for (const Sprite* s : order) {
		const auto t = s->surface.lock();
		const Sprite* target = t && t->is_target && !t->released && t.get() != s && t->texture ? t.get() : nullptr;
		buckets[target].emplace_back(s, make_item(s));
	}
	std::set<const Sprite*> emitted;
	std::function<void(const Sprite*, int)> emit = [&](const Sprite* target, int depth) {
		if (!emitted.insert(target).second || depth > 16) return;
		auto& list = buckets[target];
		for (const auto& [s, item] : list)
			if (s->is_target && !s->is_capture) emit(s, depth + 1);
		RenderPass pass;
		if (target) {
			pass.target = target->texture;
			pass.w = target->texture->w;
			pass.h = target->texture->h;
		} else {
			pass.w = kWidth;
			pass.h = kHeight;
		}
		for (auto& [s, item] : list) pass.items.push_back(std::move(item));
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
		std::printf("  sprite %-28s kind=%d prio=%d fade=%.2f pos=(%.0f,%.0f) size=%.0fx%.0f scale=(%.2f,%.2f) tex=%d erase=%d\n", s->name.c_str(),
					static_cast<int>(s->kind), s->priority, s->fade.value[0], s->move.value[0], s->move.value[1], s->width, s->height,
					s->scale.value[0], s->scale.value[1], s->texture ? 1 : 0, s->erase ? 1 : 0);
}

} // namespace omw
