// Movie decoding (FFmpeg): the game's .wmv files, played as a sprite texture plus an audio voice.
#pragma once

#include "audio/audio.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace omw {

class Movie {
public:
	// Takes the whole file; null (and `error`) when it cannot be demuxed or has no video stream.
	static std::unique_ptr<Movie> open(std::vector<uint8_t> file, std::string* error);
	~Movie();

	int width() const;
	int height() const;
	double duration_ms() const;
	std::shared_ptr<const Pcm> audio() const; // whole audio track (48 kHz stereo), null when the file has none

	void rewind(); // back to the first frame
	// Decodes forward until the newest frame whose time is <= t_ms is current. Returns true when `rgba`
	// (width*height*4) changed.
	bool advance(double t_ms, std::vector<uint8_t>& rgba);
	bool at_end() const;

private:
	Movie();
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace omw
