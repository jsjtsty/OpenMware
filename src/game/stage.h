// The 2D stage: every live sprite of the script world, its animated properties, and the draw list for a frame.
#pragma once

#include "game/anim.h"
#include "render/blend.h"

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace omw {

struct Texture;

struct Sprite {
	enum class Kind { None, Image, Color, Screen };

	std::string name;
	Kind kind = Kind::None;
	int priority = 0;
	bool erase = false;    // script-visible flag (SetErase); does not remove the sprite by itself
	bool released = false; // the script dropped its last reference: the sprite leaves the stage
	float time_scale = 1.f;
	int blend = BLEND_NORMAL;
	uint64_t order = 0; // creation order, tie-break for equal priorities

	// source
	std::string file;
	Texture* texture = nullptr;
	int sub_x = 0, sub_y = 0, sub_w = -1, sub_h = -1; // sub-rectangle of the image (-1: whole)
	float width = 0, height = 0;

	// animated properties
	Anim<3> move{0.f};
	Anim<2> scale{1.f};
	Anim<3> rotate{0.f};
	Anim<1> fade{1.f};
	std::array<Anim<3>, 4> color; // per-corner RGB (0..255)
	Anim<2> scroll{0.f};
	float pivot_x = 0.5f, pivot_y = 0.5f; // rotation/scale centre as a fraction of the size

	std::function<void(float dt_ms)> tick; // optional per-frame hook (text typing)

	Sprite();
	void update(float dt_ms);
};

// One quad of the frame, in virtual 1024x576 pixel coordinates before projection.
struct DrawItem {
	Texture* texture = nullptr;
	int blend = BLEND_NORMAL;
	float x[4], y[4];       // corner positions in game pixels (TL, TR, BR, BL)
	float u[4], v[4];       // texture coordinates
	float r[4], g[4], b[4]; // corner colours 0..1
	float a = 1.f;
};

class Stage {
public:
	static constexpr int kWidth = 1024, kHeight = 576;

	std::shared_ptr<Sprite> create(const std::string& name);
	void remove(const Sprite* s);
	void update(float dt_ms);
	// Visible sprites back-to-front as quads.
	void build_draw_list(std::vector<DrawItem>& out) const;
	size_t size() const { return sprites_.size(); }
	void dump() const; // one line per sprite on stdout (debugging)

private:
	std::vector<std::shared_ptr<Sprite>> sprites_;
	uint64_t next_order_ = 0;
};

} // namespace omw
