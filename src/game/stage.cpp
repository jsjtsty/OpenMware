#include "game/stage.h"

#include "game/texture.h"

#include <algorithm>
#include <cmath>

namespace omw {

Sprite::Sprite() {
	for (auto& c : color) c.set({255.f, 255.f, 255.f});
}

void Sprite::update(float dt_ms) {
	const float dt = dt_ms * time_scale;
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
	// Erased sprites disappear from the stage.
	sprites_.erase(std::remove_if(sprites_.begin(), sprites_.end(), [](const auto& p) { return p->erase; }), sprites_.end());
}

void Stage::build_draw_list(std::vector<DrawItem>& out) const {
	std::vector<const Sprite*> order;
	order.reserve(sprites_.size());
	for (const auto& s : sprites_)
		if (s->kind != Sprite::Kind::None && s->fade.value[0] > 0.f) order.push_back(s.get());
	std::stable_sort(order.begin(), order.end(), [](const Sprite* a, const Sprite* b) {
		return a->priority != b->priority ? a->priority < b->priority : a->order < b->order;
	});
	for (const Sprite* s : order) {
		DrawItem d;
		d.texture = s->texture;
		d.blend = s->blend;
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
		out.push_back(d);
	}
}

} // namespace omw
