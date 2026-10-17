// SQSound: a playable sound backed by a voice of the audio engine. Files are decoded once and shared.
#include "audio/audio.h"
#include "game/game_files.h"
#include "script/bind.h"
#include "script/host.h"
#include "util/shared_cache.h"

#include <cstdio>
#include <map>

namespace omw {
namespace {

using sqb::arg_float;
using sqb::arg_int;
using sqb::arg_string;

ScriptHost& host() { return *ScriptHost::current(); }

struct SoundRef {
	int id = -1;
	std::string path;
	~SoundRef() {
		if (id >= 0 && ScriptHost::current() && ScriptHost::current()->audio) ScriptHost::current()->audio->destroy_voice(id);
	}
};

SQInteger ctor(HSQUIRRELVM v) {
	sqb::attach<SoundRef>(v);
	return 0;
}

SQInteger ret_null(HSQUIRRELVM) { return 0; }
SQInteger ret_zero_f(HSQUIRRELVM v) { sq_pushfloat(v, 0.f); return 1; }
SQInteger ret_false(HSQUIRRELVM v) { sq_pushbool(v, SQFalse); return 1; }

// Decoded files by archive path. Every voice using a file shares one decode. Short files (effects, voice lines) are also kept
// while idle, least recently used first, within a 64 MB budget; long ones (music) live only while a voice plays them.
SharedCache<const Pcm>& cache() {
	static SharedCache<const Pcm> c(64u << 20);
	return c;
}

std::shared_ptr<const Pcm> load_pcm(const std::string& path) {
	ScriptHost& h = host();
	if (auto p = cache().find(path)) return p;
	std::vector<uint8_t> data;
	if (!h.files || !h.files->read(path, data)) return nullptr;
	auto pcm = decode_ogg(data);
	if (!pcm) return nullptr;
	const size_t bytes = pcm->data.size() * sizeof(int16_t);
	cache().insert(path, pcm, bytes, bytes < (6u << 20));
	return pcm;
}

// Makes sure the voice has its samples (they are dropped while idle).
bool ensure_loaded(SoundRef* s) {
	AudioEngine* a = host().audio;
	if (!s || s->id < 0 || !a) return false;
	if (a->has_pcm(s->id)) return true;
	const auto pcm = load_pcm(s->path);
	if (!pcm) return false;
	a->set_pcm(s->id, pcm);
	return true;
}

SQInteger create(HSQUIRRELVM v) { // (path) -> 0 on success, -1 when the file does not exist or cannot be decoded
	SoundRef* s = sqb::self<SoundRef>(v);
	ScriptHost& h = host();
	if (!s || !h.audio || !h.files) {
		sq_pushinteger(v, 0); // no audio layer: pretend success so scripts carry on
		return 1;
	}
	s->path = arg_string(v, 2);
	std::vector<uint8_t> probe;
	if (!h.files->read(s->path, probe)) {
		if (h.verbose) std::printf("[sound] missing %s\n", s->path.c_str());
		sq_pushinteger(v, -1);
		return 1;
	}
	if (s->id < 0) s->id = h.audio->create_voice(nullptr);
	sq_pushinteger(v, 0);
	return 1;
}

#define VOICE_CALL(name, expr) \
	SQInteger name(HSQUIRRELVM v) { \
		SoundRef* s = sqb::self<SoundRef>(v); \
		if (s && s->id >= 0 && host().audio) { \
			AudioEngine& a = *host().audio; \
			const int id = s->id; \
			(void)id; \
			expr; \
		} \
		return 0; \
	}

SQInteger play(HSQUIRRELVM v) {
	SoundRef* s = sqb::self<SoundRef>(v);
	if (!ensure_loaded(s)) return 0;
	if (host().verbose) std::printf("[sound] play %s\n", s->path.c_str());
	host().audio->play(s->id);
	return 0;
}
VOICE_CALL(stop, a.stop(id))
VOICE_CALL(pause, a.pause(id))
VOICE_CALL(resume, a.resume(id))
VOICE_CALL(seek, a.seek(id, arg_float(v, 2)))
VOICE_CALL(set_loop, a.set_loop(id, static_cast<int>(arg_int(v, 2)), arg_float(v, 3), arg_float(v, 4)))
VOICE_CALL(set_volume, a.set_volume(id, arg_float(v, 2), arg_float(v, 3), static_cast<int>(arg_int(v, 4))))
VOICE_CALL(set_pan, a.set_pan(id, arg_float(v, 2), arg_float(v, 3), static_cast<int>(arg_int(v, 4))))
VOICE_CALL(set_frequency, a.set_frequency(id, arg_float(v, 2), arg_float(v, 3), static_cast<int>(arg_int(v, 4))))
VOICE_CALL(set_time_scale, a.set_time_scale(id, arg_float(v, 2, 1.f)))
VOICE_CALL(set_disused, a.set_disused(id, arg_int(v, 2, 1) != 0))
VOICE_CALL(set_type, a.set_type(id, static_cast<int>(arg_int(v, 2))))

template <class F>
SQInteger query_bool(HSQUIRRELVM v, F f) {
	SoundRef* s = sqb::self<SoundRef>(v);
	sq_pushbool(v, s && s->id >= 0 && host().audio && f(*host().audio, s->id) ? SQTrue : SQFalse);
	return 1;
}
template <class F>
SQInteger query_float(HSQUIRRELVM v, F f) {
	SoundRef* s = sqb::self<SoundRef>(v);
	sq_pushfloat(v, s && s->id >= 0 && host().audio ? f(*host().audio, s->id) : 0.f);
	return 1;
}

SQInteger is_playing(HSQUIRRELVM v) { return query_bool(v, [](AudioEngine& a, int id) { return a.is_playing(id); }); }
SQInteger is_paused(HSQUIRRELVM v) { return query_bool(v, [](AudioEngine& a, int id) { return a.is_paused(id); }); }
SQInteger get_volume(HSQUIRRELVM v) { return query_float(v, [](AudioEngine& a, int id) { return a.volume(id); }); }
SQInteger get_pan(HSQUIRRELVM v) { return query_float(v, [](AudioEngine& a, int id) { return a.pan(id); }); }
SQInteger get_frequency(HSQUIRRELVM v) { return query_float(v, [](AudioEngine& a, int id) { return a.frequency(id); }); }
SQInteger get_total(HSQUIRRELVM v) { return query_float(v, [](AudioEngine& a, int id) { return a.total_ms(id); }); }
SQInteger get_remain(HSQUIRRELVM v) {
	return query_float(v, [](AudioEngine& a, int id) { return a.total_ms(id) - a.position_ms(id); });
}

} // namespace

void register_sound_class(ScriptHost& h) {
	h.begin_class("SQSound");
	h.add_method("constructor", ctor);
	h.add_method("create", create);
	h.add_method("load", create);
	h.add_method("play", play);
	h.add_method("stop", stop);
	h.add_method("pause", pause);
	h.add_method("onResume", resume);
	h.add_method("seek", seek);
	h.add_method("setLoop", set_loop);
	h.add_method("setVolume", set_volume);
	h.add_method("setPan", set_pan);
	h.add_method("setFrequency", set_frequency);
	h.add_method("setTimeScale", set_time_scale);
	h.add_method("setType", set_type);
	h.add_method("setDisused", set_disused);
	h.add_method("isPlaying", is_playing);
	h.add_method("isPaused", is_paused);
	h.add_method("getVolume", get_volume);
	h.add_method("getPan", get_pan);
	h.add_method("getFrequency", get_frequency);
	h.add_method("getTotalTime", get_total);
	h.add_method("getDurationTime", get_total);
	h.add_method("getRemainTime", get_remain);
	for (const char* n : {"getVolumeTimer", "getPanTimer", "getFrequencyTimer", "getPulse", "getAmplitude"}) h.add_method(n, ret_zero_f);
	for (const char* n : {"getLock", "getDisused"}) h.add_method(n, ret_false);
	// Timers, effects and grouping are accepted without effect for now (echo/reverb, group priorities, fade-time control).
	for (const char* n : {"setLock", "setVolumeTimer", "setPanTimer", "setFrequencyTimer", "setEcho", "setPresetReverb",
						  "addSoundGroup", "setVolumeControlFadeTime"})
		h.add_method(n, ret_null);
	h.end_class();
}

} // namespace omw
