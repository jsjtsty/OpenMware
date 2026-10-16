// Audio engine: decoded Ogg Vorbis voices mixed on the SDL audio thread. Voices model the original's sound objects
// (volume/pan/frequency timers, loop segment, play count, type volumes); control calls come from the script thread.
#pragma once

#include "game/anim.h"

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace omw {

struct Pcm {
	int channels = 2, rate = 44100;
	std::vector<int16_t> data; // interleaved
	size_t frames() const { return channels > 0 ? data.size() / static_cast<size_t>(channels) : 0; }
};

// Decodes an Ogg Vorbis file held in memory; null on failure.
std::shared_ptr<const Pcm> decode_ogg(const std::vector<uint8_t>& file);

class AudioEngine {
public:
	static constexpr int kTypes = 8; // SOUND_TYPE_*: BGM, VOICE, SE, SYSTEM, MOVIE, BGV ...
	~AudioEngine() { shutdown(); }

	// Opens the default output device. Without a device the engine still runs its state machine (headless tests).
	bool init();
	void shutdown();
	void update(float dt_ms); // once per game frame: timers, and playback itself when there is no device

	int create_voice(std::shared_ptr<const Pcm> pcm);
	void destroy_voice(int id);
	void set_pcm(int id, std::shared_ptr<const Pcm> pcm);
	bool has_pcm(int id);

	void play(int id);
	void stop(int id);
	void pause(int id);
	void resume(int id);
	void seek(int id, float ms);
	void set_loop(int id, int count, float start_ms, float end_ms); // count < 0: forever; 0/1: once
	void set_volume(int id, float v, float time_ms, int tempo);
	void set_pan(int id, float v, float time_ms, int tempo);
	void set_frequency(int id, float v, float time_ms, int tempo);
	void set_time_scale(int id, float scale);
	void set_type(int id, int type);
	void set_type_volume(int type, float v, float time_ms, int tempo);

	bool is_playing(int id);
	bool is_paused(int id);
	float volume(int id);
	float pan(int id);
	float frequency(int id);
	float position_ms(int id);
	float total_ms(int id);
	float peak() const { return peak_; } // loudest output sample so far (diagnostics)
	bool has_device() const { return stream_ != nullptr; }

private:
	struct Voice {
		std::shared_ptr<const Pcm> pcm;
		double pos = 0;       // source frames
		double start_pos = 0; // where play() begins (seek)
		bool playing = false, paused = false;
		int plays = 0, play_count = 0; // play_count < 0: forever
		double loop_start = 0, loop_end = 0; // source frames (derived from the ms values at play time); end 0 = file end
		float loop_start_ms = 0, loop_end_ms = 0, start_ms = 0;
		Anim<1> vol{1.f}, pan{0.f}, freq{1.f};
		float time_scale = 1.f;
		int type = -1;
		float last_gain = 1.f;
	};
	Voice* find(int id);
	void render(float* out, int frames);
	static void SDLCALL on_audio(void* user, SDL_AudioStream* s, int additional, int total);

	std::mutex mutex_;
	std::map<int, Voice> voices_;
	int next_id_ = 1;
	Anim<1> type_vol_[kTypes] = {Anim<1>(1.f), Anim<1>(1.f), Anim<1>(1.f), Anim<1>(1.f), Anim<1>(1.f), Anim<1>(1.f), Anim<1>(1.f), Anim<1>(1.f)};
	SDL_AudioStream* stream_ = nullptr;
	float peak_ = 0.f;
	static constexpr int kRate = 48000;
};

} // namespace omw
