#include "audio/audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

// Implemented by stb_vorbis (vorbis_impl.cpp).
extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace omw {

std::shared_ptr<const Pcm> decode_ogg(const std::vector<uint8_t>& file) {
	int ch = 0, rate = 0;
	short* out = nullptr;
	const int n = stb_vorbis_decode_memory(file.data(), static_cast<int>(file.size()), &ch, &rate, &out);
	if (n <= 0 || !out) return nullptr;
	auto pcm = std::make_shared<Pcm>();
	pcm->channels = ch;
	pcm->rate = rate;
	pcm->data.assign(out, out + static_cast<size_t>(n) * ch);
	std::free(out);
	return pcm;
}

bool AudioEngine::init() {
	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return false;
	SDL_AudioSpec spec{SDL_AUDIO_F32, 2, kRate};
	stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioEngine::on_audio, this);
	if (!stream_) return false;
	SDL_ResumeAudioStreamDevice(stream_);
	return true;
}

void AudioEngine::shutdown() {
	if (stream_) {
		SDL_DestroyAudioStream(stream_); // stops the callback before the engine goes away
		stream_ = nullptr;
	}
	std::lock_guard lock(mutex_);
	voices_.clear();
}

void AudioEngine::on_audio(void* user, SDL_AudioStream* s, int additional, int) {
	auto* self = static_cast<AudioEngine*>(user);
	float buf[1024 * 2];
	int frames_left = additional / static_cast<int>(sizeof(float) * 2);
	while (frames_left > 0) {
		const int n = std::min(frames_left, 1024);
		self->render(buf, n);
		SDL_PutAudioStreamData(s, buf, n * static_cast<int>(sizeof(float) * 2));
		frames_left -= n;
	}
}

AudioEngine::Voice* AudioEngine::find(int id) {
	const auto it = voices_.find(id);
	return it == voices_.end() ? nullptr : &it->second;
}

void AudioEngine::update(float dt_ms) {
	{
		std::lock_guard lock(mutex_);
		for (auto& a : type_vol_) a.update(dt_ms);
		for (auto& [id, v] : voices_) {
			v.vol.update(dt_ms);
			v.pan.update(dt_ms);
			v.freq.update(dt_ms);
			if (v.disused && v.playing && !v.vol.active && v.vol.value[0] <= 0.f) { // faded out: the sound is over
				v.playing = v.paused = false;
				v.pos = v.start_pos;
			}
			if (!v.playing && !v.paused) v.pcm.reset(); // idle voices give their samples back; play() reloads them
		}
	}
	if (!stream_) { // no device: run the voices in game time so isPlaying() etc. still behave
		static float scratch[2048 * 2];
		int frames = static_cast<int>(dt_ms * kRate / 1000.f);
		while (frames > 0) {
			const int n = std::min(frames, 2048);
			render(scratch, n);
			frames -= n;
		}
	}
}

void AudioEngine::render(float* out, int frames) {
	std::lock_guard lock(mutex_);
	std::fill(out, out + frames * 2, 0.f);
	for (auto& [id, v] : voices_) {
		if (!v.playing || v.paused || !v.pcm || v.pcm->frames() == 0) continue;
		const Pcm& p = *v.pcm;
		const double total = static_cast<double>(p.frames());
		const double end = v.loop_end > 0 ? std::min(v.loop_end, total) : total;
		const double step = static_cast<double>(p.rate) / kRate * v.freq.value[0] * v.time_scale;
		const float tv = v.type >= 0 && v.type < kTypes ? type_vol_[v.type].value[0] : 1.f;
		const float gain_to = std::max(0.f, v.vol.value[0]) * tv;
		const float pan = std::clamp(v.pan.value[0], -1.f, 1.f);
		const float lg = pan > 0 ? 1.f - pan : 1.f, rg = pan < 0 ? 1.f + pan : 1.f;
		const float gain_from = v.last_gain;
		v.last_gain = gain_to;
		for (int i = 0; i < frames && v.playing; i++) {
			const float g = gain_from + (gain_to - gain_from) * (static_cast<float>(i) / static_cast<float>(frames));
			const size_t i0 = std::min(static_cast<size_t>(v.pos), p.frames() - 1);
			const size_t i1 = std::min(i0 + 1, p.frames() - 1);
			const float t = static_cast<float>(v.pos - static_cast<double>(i0));
			const size_t c = static_cast<size_t>(p.channels);
			float l, r;
			if (p.channels >= 2) {
				l = (p.data[i0 * c] * (1 - t) + p.data[i1 * c] * t) / 32768.f;
				r = (p.data[i0 * c + 1] * (1 - t) + p.data[i1 * c + 1] * t) / 32768.f;
			} else {
				l = r = (p.data[i0] * (1 - t) + p.data[i1] * t) / 32768.f;
			}
			out[i * 2] += l * g * lg;
			out[i * 2 + 1] += r * g * rg;
			v.pos += step;
			if (v.pos >= end) {
				v.plays++;
				if (v.play_count < 0 || v.plays < std::max(1, v.play_count)) {
					v.pos = v.loop_start + (v.pos - end);
					if (v.pos >= end) v.pos = v.loop_start;
				} else {
					v.playing = false;
					v.pos = v.start_pos;
				}
			}
		}
	}
	for (int i = 0; i < frames * 2; i++) {
		out[i] = std::clamp(out[i], -1.f, 1.f);
		peak_ = std::max(peak_, std::fabs(out[i]));
	}
}

int AudioEngine::create_voice(std::shared_ptr<const Pcm> pcm) {
	std::lock_guard lock(mutex_);
	const int id = next_id_++;
	voices_[id].pcm = std::move(pcm);
	return id;
}
void AudioEngine::set_pcm(int id, std::shared_ptr<const Pcm> pcm) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->pcm = std::move(pcm);
}
bool AudioEngine::has_pcm(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v && v->pcm;
}
void AudioEngine::destroy_voice(int id) {
	std::lock_guard lock(mutex_);
	voices_.erase(id);
}

void AudioEngine::play(int id) {
	std::lock_guard lock(mutex_);
	Voice* v = find(id);
	if (!v) return;
	if (v->paused) {
		v->paused = false;
		return;
	}
	if (v->pcm) {
		const double rate = v->pcm->rate;
		v->start_pos = std::clamp(v->start_ms * rate / 1000.0, 0.0, static_cast<double>(v->pcm->frames()));
		v->loop_start = v->loop_start_ms * rate / 1000.0;
		v->loop_end = v->loop_end_ms * rate / 1000.0;
	}
	v->pos = v->start_pos;
	v->plays = 0;
	v->playing = true;
	v->last_gain = std::max(0.f, v->vol.value[0]) * (v->type >= 0 && v->type < kTypes ? type_vol_[v->type].value[0] : 1.f);
}
void AudioEngine::stop(int id) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) {
		v->playing = v->paused = false;
		v->pos = v->start_pos;
	}
}
void AudioEngine::pause(int id) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id); v && v->playing) v->paused = true;
}
void AudioEngine::resume(int id) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->paused = false;
}
void AudioEngine::seek(int id, float ms) {
	std::lock_guard lock(mutex_);
	Voice* v = find(id);
	if (!v) return;
	v->start_ms = std::max(0.f, ms);
	if (v->playing && v->pcm) {
		v->start_pos = std::clamp(static_cast<double>(ms) * v->pcm->rate / 1000.0, 0.0, static_cast<double>(v->pcm->frames()));
		v->pos = v->start_pos;
	}
}
void AudioEngine::set_loop(int id, int count, float start_ms, float end_ms) {
	std::lock_guard lock(mutex_);
	Voice* v = find(id);
	if (!v) return;
	v->play_count = count;
	v->loop_start_ms = std::max(0.f, start_ms);
	v->loop_end_ms = std::max(0.f, end_ms);
	if (v->pcm) { // already loaded (playing): apply right away
		v->loop_start = v->loop_start_ms * v->pcm->rate / 1000.0;
		v->loop_end = v->loop_end_ms * v->pcm->rate / 1000.0;
	}
}
void AudioEngine::set_volume(int id, float val, float time_ms, int tempo) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->vol.start({val}, time_ms, tempo);
}
void AudioEngine::set_pan(int id, float val, float time_ms, int tempo) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->pan.start({val}, time_ms, tempo);
}
void AudioEngine::set_frequency(int id, float val, float time_ms, int tempo) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->freq.start({val}, time_ms, tempo);
}
void AudioEngine::set_time_scale(int id, float scale) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->time_scale = scale;
}
void AudioEngine::set_disused(int id, bool on) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->disused = on;
}
void AudioEngine::set_type(int id, int type) {
	std::lock_guard lock(mutex_);
	if (Voice* v = find(id)) v->type = type;
}
void AudioEngine::set_type_volume(int type, float val, float time_ms, int tempo) {
	std::lock_guard lock(mutex_);
	if (type >= 0 && type < kTypes) type_vol_[type].start({val}, time_ms, tempo);
}

bool AudioEngine::is_playing(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v && v->playing && !v->paused;
}
bool AudioEngine::is_paused(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v && v->paused;
}
float AudioEngine::volume(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v ? v->vol.value[0] : 0.f;
}
float AudioEngine::pan(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v ? v->pan.value[0] : 0.f;
}
float AudioEngine::frequency(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v ? v->freq.value[0] : 1.f;
}
float AudioEngine::position_ms(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v && v->pcm ? static_cast<float>(v->pos * 1000.0 / v->pcm->rate) : 0.f;
}
float AudioEngine::total_ms(int id) {
	std::lock_guard lock(mutex_);
	const Voice* v = find(id);
	return v && v->pcm ? static_cast<float>(static_cast<double>(v->pcm->frames()) * 1000.0 / v->pcm->rate) : 0.f;
}

} // namespace omw
