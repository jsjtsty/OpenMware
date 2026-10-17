// Movie sprites (SQSprite.createMovie and the play/stop/seek/volume methods): the video frames are decoded by FFmpeg into a
// dynamic texture shown by the sprite; the audio track plays as a voice of the audio engine.
#include "audio/audio.h"
#include "game/game_files.h"
#include "game/texture.h"
#include "render/renderer2d.h"
#include "script/bind.h"
#include "script/host.h"
#include "script/sprite_ref.h"
#include "video/movie.h"

#include <cstdio>

namespace omw {
namespace {

using sqb::arg_float;
using sqb::arg_int;
using sqb::arg_string;

constexpr int kSoundTypeMovie = 4;

ScriptHost& host() { return *ScriptHost::current(); }

struct MovieState {
	std::unique_ptr<Movie> movie;
	Texture* tex = nullptr;
	std::vector<uint8_t> rgba;
	std::shared_ptr<const Pcm> pcm;
	int voice = -1;
	bool playing = false, paused = false;
	int loop = 0; // < 0: forever
	int plays = 0;
	double t_ms = 0;
	std::string path;

	~MovieState() {
		ScriptHost* h = ScriptHost::current();
		if (voice >= 0 && h && h->audio) h->audio->destroy_voice(voice);
		if (tex && h && h->renderer) h->renderer->destroy_dynamic_texture(tex);
	}

	void upload() {
		if (!tex) return;
		host().renderer->replace_dynamic_texture(tex, movie->width(), movie->height(), rgba.data());
	}
	void start() {
		if (paused) {
			paused = false;
			if (voice >= 0) host().audio->resume(voice);
			return;
		}
		movie->rewind();
		t_ms = 0;
		plays = 0;
		playing = true;
		if (movie->advance(0, rgba)) upload();
		if (voice >= 0) {
			host().audio->set_pcm(voice, pcm);
			host().audio->play(voice);
		}
	}
	void stop() {
		playing = paused = false;
		if (voice >= 0) host().audio->stop(voice);
	}
	void tick(float dt_ms, float scale) {
		if (!playing || paused) return;
		t_ms += static_cast<double>(dt_ms) * scale;
		if (movie->advance(t_ms, rgba)) upload();
		if (t_ms >= movie->duration_ms() && movie->at_end()) {
			plays++;
			if (loop < 0 || plays < loop) {
				movie->rewind();
				t_ms = 0;
				if (voice >= 0) host().audio->play(voice);
			} else {
				playing = false;
			}
		}
	}
};

MovieState* movie_of(HSQUIRRELVM v) {
	auto* r = sqb::self<SpriteRef>(v);
	return r ? static_cast<MovieState*>(r->aux.get()) : nullptr;
}

SQInteger create_movie(HSQUIRRELVM v) { // (path, alpha) -> 0 on success
	auto* r = sqb::self<SpriteRef>(v);
	ScriptHost& h = host();
	if (!r || !r->sprite || !h.files || !h.renderer) {
		sq_pushinteger(v, 0);
		return 1;
	}
	auto st = std::make_shared<MovieState>();
	st->path = arg_string(v, 2);
	std::vector<uint8_t> file;
	std::string err;
	if (!h.files->read(st->path, file) || !(st->movie = Movie::open(std::move(file), &err))) {
		if (h.verbose) std::printf("[movie] cannot open %s %s\n", st->path.c_str(), err.c_str());
		sq_pushinteger(v, -1);
		return 1;
	}
	const int w = st->movie->width(), hgt = st->movie->height();
	st->rgba.assign(static_cast<size_t>(w) * hgt * 4, 0);
	for (size_t i = 3; i < st->rgba.size(); i += 4) st->rgba[i] = 255;
	st->tex = h.renderer->create_dynamic_texture(w, hgt, st->rgba.data());
	st->pcm = st->movie->audio();
	if (h.audio && st->pcm) {
		st->voice = h.audio->create_voice(nullptr);
		h.audio->set_type(st->voice, kSoundTypeMovie);
	}
	Sprite* s = r->sprite.get();
	s->kind = Sprite::Kind::Image;
	s->texture = st->tex;
	s->width = static_cast<float>(w);
	s->height = static_cast<float>(hgt);
	MovieState* raw = st.get();
	s->tick = [raw, s](float dt_ms) { raw->tick(dt_ms, s->time_scale); };
	r->aux = st;
	if (h.verbose) std::printf("[movie] open %s %dx%d %.1fs audio=%d\n", st->path.c_str(), w, hgt, st->movie->duration_ms() / 1000.0, st->pcm ? 1 : 0);
	sq_pushinteger(v, 0);
	return 1;
}

// A screen snapshot (createScreen) shares the movie verbs: play() follows the screen, stop() / pause() freeze it.
bool screen_snapshot(HSQUIRRELVM v, bool follow) {
	auto* r = sqb::self<SpriteRef>(v);
	Sprite* s = r ? r->sprite.get() : nullptr;
	if (host().verbose) std::printf("[f%d] [screen] %s follow=%d capture=%d drawn=%d\n", host().frame, s ? s->name.c_str() : "?", follow ? 1 : 0, s && s->is_capture ? 1 : 0, s && s->drawn_capturing ? 1 : 0);
	if (!s || !s->is_capture) return false;
	if (follow) {
		s->capturing = true;
		s->freeze_after_draw = false;
	} else if (s->capturing && !(s->texture && s->texture->copies > 0)) {
		s->freeze_after_draw = true; // nothing has been captured yet: freeze after the renderer has taken the first picture
	} else {
		s->capturing = false;
	}
	return true;
}
SQInteger play(HSQUIRRELVM v) {
	if (screen_snapshot(v, true)) return 0;
	if (MovieState* m = movie_of(v)) {
		if (host().verbose) std::printf("[f%d] [movie] play %s\n", host().frame, m->path.c_str());
		m->start();
	}
	return 0;
}
SQInteger stop(HSQUIRRELVM v) {
	if (screen_snapshot(v, false)) return 0;
	if (MovieState* m = movie_of(v)) m->stop();
	return 0;
}
SQInteger pause(HSQUIRRELVM v) {
	if (screen_snapshot(v, false)) return 0;
	if (MovieState* m = movie_of(v); m && m->playing) {
		m->paused = true;
		if (m->voice >= 0) host().audio->pause(m->voice);
	}
	return 0;
}
SQInteger resume(HSQUIRRELVM v) {
	if (MovieState* m = movie_of(v)) {
		m->paused = false;
		if (m->voice >= 0) host().audio->resume(m->voice);
	}
	return 0;
}
SQInteger is_playing(HSQUIRRELVM v) {
	const MovieState* m = movie_of(v);
	sq_pushbool(v, m && m->playing && !m->paused ? SQTrue : SQFalse);
	return 1;
}
SQInteger is_paused(HSQUIRRELVM v) {
	const MovieState* m = movie_of(v);
	sq_pushbool(v, m && m->paused ? SQTrue : SQFalse);
	return 1;
}
SQInteger get_total(HSQUIRRELVM v) {
	const MovieState* m = movie_of(v);
	sq_pushfloat(v, m ? static_cast<float>(m->movie->duration_ms()) : 0.f);
	return 1;
}
SQInteger get_remain(HSQUIRRELVM v) {
	const MovieState* m = movie_of(v);
	sq_pushfloat(v, m ? static_cast<float>(std::max(0.0, m->movie->duration_ms() - m->t_ms)) : 0.f);
	return 1;
}
SQInteger set_loop(HSQUIRRELVM v) { // (count, ...): < 0 forever
	if (MovieState* m = movie_of(v)) m->loop = static_cast<int>(arg_int(v, 2));
	return 0;
}
SQInteger set_volume(HSQUIRRELVM v) { // (volume, time_ms, tempo)
	if (MovieState* m = movie_of(v); m && m->voice >= 0)
		host().audio->set_volume(m->voice, arg_float(v, 2), arg_float(v, 3), static_cast<int>(arg_int(v, 4)));
	return 0;
}
SQInteger get_volume(HSQUIRRELVM v) {
	const MovieState* m = movie_of(v);
	sq_pushfloat(v, m && m->voice >= 0 ? host().audio->volume(m->voice) : 0.f);
	return 1;
}
SQInteger seek(HSQUIRRELVM v) {
	if (MovieState* m = movie_of(v)) { // only rewinding is supported
		if (arg_float(v, 2) <= 0.f) {
			m->movie->rewind();
			m->t_ms = 0;
		}
	}
	return 0;
}
SQInteger ret_null(HSQUIRRELVM) { return 0; }

} // namespace

void register_movie_methods(ScriptHost& h) {
	h.add_method("createMovie", create_movie);
	h.add_method("play", play);
	h.add_method("stop", stop);
	h.add_method("pause", pause);
	h.add_method("onResume", resume);
	h.add_method("isPlaying", is_playing);
	h.add_method("isPaused", is_paused);
	h.add_method("getTotalTime", get_total);
	h.add_method("getDurationTime", get_total);
	h.add_method("getRemainTime", get_remain);
	h.add_method("setLoop", set_loop);
	h.add_method("setVolume", set_volume);
	h.add_method("getVolume", get_volume);
	h.add_method("seek", seek);
	for (const char* n : {"setVolumeTimer", "setVolumeControlFadeTime", "addSoundGroup"}) h.add_method(n, ret_null);
}

} // namespace omw
