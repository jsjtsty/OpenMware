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
	bool is_mask = false;   // SQSpriteMask: never drawn itself, only as the mask of other sprites
	std::weak_ptr<Sprite> mask;           // SetMask: sprite whose picture multiplies this one (screen space)
	std::weak_ptr<Sprite> displacement;   // SetDisplacementMap
	// Transition (SQSprite.transition): the sprite shows through a rule image; `trans` runs 0..1 (invisible..visible)
	bool trans_on = false;
	Anim<1> trans{0.f};
	float trans_rate = 0.3f;
	std::string trans_file;
	Texture* trans_tex = nullptr;
	bool clickable = false, check_alpha = false; // SetClickable: takes part in mouse hit testing
	// SQSpriteRenderTarget: `texture` is an offscreen surface (w x h) that the sprites assigned to it are drawn into;
	// a capture target instead copies what has been drawn below it (shake / filter sprites), while `capturing`.
	bool is_target = false, is_capture = false, capturing = true;
	std::weak_ptr<Sprite> surface; // SetSurface: the target this sprite is drawn into (none: the screen)

	// Post effects of the sprite's picture (values as the scripts give them after their /1000 scaling).
	struct Effects {
		Anim<1> hue, saturation, brightness, glow; // pshsv: hue shift 0..1, saturation/brightness added, glow added to the value
		Anim<1> sepia, nega, mono;                 // 0..1 mixes
		Anim<1> shade;                             // "shading": soft-focus blur strength 0..1 (guess, see docs/render-notes.md)
		float mono_cb = 0, mono_cr = 0;
		int shade_level = 0;                       // SHADE_LEVEL_LOW/MIDDLE/HIGH = 0/2/4
		float shade_density = 0;
		bool any_color() const {
			return hue.value[0] != 0 || saturation.value[0] != 0 || brightness.value[0] != 0 || glow.value[0] != 0 || sepia.value[0] > 0 ||
				   nega.value[0] > 0 || mono.value[0] > 0;
		}
		bool any() const { return any_color() || shade.value[0] > 0; }
		void update(float dt) {
			for (Anim<1>* a : {&hue, &saturation, &brightness, &glow, &sepia, &nega, &mono, &shade}) a->update(dt);
		}
	} fx;
	float pivot_x = 0.5f, pivot_y = 0.5f; // rotation/scale centre as a fraction of the size

	std::function<void(float dt_ms)> tick; // optional per-frame hook (text typing)

	Sprite();
	void update(float dt_ms);
};

// One quad of the frame, in virtual 1024x576 pixel coordinates before projection.
struct DrawQuad {
	Texture* texture = nullptr;
	float x[4], y[4];       // corner positions in game pixels (TL, TR, BR, BL)
	float u[4], v[4];       // texture coordinates
	float r[4], g[4], b[4]; // corner colours 0..1
	float a = 1.f;
};

struct DrawItem : DrawQuad {
	int blend = BLEND_NORMAL;
	// mask / displacement sprites are drawn into screen-space helper textures; transition rule image + progress
	bool has_mask = false, has_disp = false;
	DrawQuad mask, disp;
	Texture* trans_tex = nullptr; // null: no transition
	float trans_now = 0.f, trans_rate = 0.3f;
	// Snapshots of the pass target drawn so far: Screen sprites show the region (rx, ry, rw, rh) of it; capture sprites first
	// copy it into their own texture.
	enum class Snap { None, Screen, Capture } snap = Snap::None;
	float rx = 0, ry = 0, rw = 0, rh = 0;
	bool premult = false; // the texture is an offscreen surface (premultiplied alpha)
	int temp = -1;        // >= 0: the picture is the renderer's temporary texture of that slot (result of an effect chain)
};

// One render pass. Draw: the items drawn into `target` (null: the game frame) or temporary slot `temp_target`, in order.
// Blur/Color: a post effect of temporary slot `in_temp` written to `temp_target`.
struct RenderPass {
	enum class Op { Draw, Blur, Color } op = Op::Draw;
	Texture* target = nullptr;
	int temp_target = -1;
	int in_temp = -1;
	int w = 0, h = 0;
	std::vector<DrawItem> items;
	// Blur: p[0..1] = step in UV, p[2] = sigma in texels.  Color: p[0..3] hue/saturation/value/glow, p[4..6] sepia/nega/mono,
	// p[8..9] mono cb/cr.
	float p[12] = {};
};
using DrawList = std::vector<RenderPass>; // surfaces first, the game frame last

class Stage {
public:
	static constexpr int kWidth = 1024, kHeight = 576;

	std::shared_ptr<Sprite> create(const std::string& name);
	void remove(const Sprite* s);
	std::shared_ptr<Sprite> find(const std::string& name) const; // most recent sprite with that name
	void update(float dt_ms);
	// Visible sprites back-to-front as quads, grouped into the passes of their surfaces.
	void build_draw_list(DrawList& out) const;
	// Finds the clickable sprite under the cursor (topmost wins); call once per frame before the script runs.
	void update_pointer(float x, float y);
	const Sprite* hot() const { return hot_; }
	const Sprite* previous_hot() const { return prev_hot_; }
	size_t size() const { return sprites_.size(); }
	void dump() const; // one line per sprite on stdout (debugging)

private:
	std::vector<std::shared_ptr<Sprite>> sprites_;
	uint64_t next_order_ = 0;
	const Sprite* hot_ = nullptr;
	const Sprite* prev_hot_ = nullptr;
};

} // namespace omw
