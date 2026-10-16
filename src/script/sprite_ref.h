// The native object behind SQSprite instances, shared with the other sprite-like classes (SQText).
#pragma once

#include "game/stage.h"

#include <memory>

namespace omw {

class ScriptHost;

struct SpriteRef {
	std::shared_ptr<Sprite> sprite;
	std::shared_ptr<void> aux; // extra state of special sprites (movies); released with the sprite
	~SpriteRef();
};

// createMovie/play/stop/... of movie sprites (natives_movie.cpp).
void register_movie_methods(ScriptHost& h);

// setPriority, fade, move, scale, rotate, ... : the property methods every sprite-like class shares.
void register_sprite_property_methods(ScriptHost& h);

} // namespace omw
