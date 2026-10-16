#include "video/movie.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstring>

namespace omw {
namespace {

// Reads from the in-memory file; every demuxer gets its own cursor.
struct MemSource {
	const std::vector<uint8_t>* data = nullptr;
	size_t pos = 0;
};

int mem_read(void* opaque, uint8_t* buf, int size) {
	auto* m = static_cast<MemSource*>(opaque);
	const size_t left = m->data->size() - m->pos;
	if (left == 0) return AVERROR_EOF;
	const size_t n = std::min(left, static_cast<size_t>(size));
	std::memcpy(buf, m->data->data() + m->pos, n);
	m->pos += n;
	return static_cast<int>(n);
}

int64_t mem_seek(void* opaque, int64_t offset, int whence) {
	auto* m = static_cast<MemSource*>(opaque);
	const int64_t size = static_cast<int64_t>(m->data->size());
	if (whence == AVSEEK_SIZE) return size;
	int64_t p = offset;
	switch (whence & ~AVSEEK_FORCE) {
	case SEEK_CUR: p = static_cast<int64_t>(m->pos) + offset; break;
	case SEEK_END: p = size + offset; break;
	default: break;
	}
	if (p < 0 || p > size) return -1;
	m->pos = static_cast<size_t>(p);
	return p;
}

struct Demux {
	MemSource src;
	AVFormatContext* fmt = nullptr;
	AVIOContext* io = nullptr;
	AVCodecContext* codec = nullptr;
	int stream = -1;

	~Demux() {
		avcodec_free_context(&codec);
		if (fmt) avformat_close_input(&fmt);
		if (io) {
			av_freep(&io->buffer);
			avio_context_free(&io);
		}
	}

	bool open(const std::vector<uint8_t>& file, AVMediaType type) {
		src.data = &file;
		const int bufsize = 1 << 16;
		auto* buf = static_cast<uint8_t*>(av_malloc(bufsize));
		io = avio_alloc_context(buf, bufsize, 0, &src, mem_read, nullptr, mem_seek);
		fmt = avformat_alloc_context();
		fmt->pb = io;
		if (avformat_open_input(&fmt, nullptr, nullptr, nullptr) < 0) return false; // fmt is freed on failure
		if (avformat_find_stream_info(fmt, nullptr) < 0) return false;
		const AVCodec* dec = nullptr;
		stream = av_find_best_stream(fmt, type, -1, -1, &dec, 0);
		if (stream < 0 || !dec) return false;
		codec = avcodec_alloc_context3(dec);
		avcodec_parameters_to_context(codec, fmt->streams[stream]->codecpar);
		return avcodec_open2(codec, dec, nullptr) >= 0;
	}
};

} // namespace

struct Movie::Impl {
	std::vector<uint8_t> file;
	Demux video;
	AVFrame* frame = nullptr;
	AVPacket* pkt = nullptr;
	SwsContext* sws = nullptr;
	int w = 0, h = 0;
	double duration = 0;
	double frame_time = -1; // time of the frame currently held in `frame`
	bool have_frame = false, eof = false, drained = false;
	std::shared_ptr<const Pcm> pcm;

	~Impl() {
		sws_freeContext(sws);
		av_frame_free(&frame);
		av_packet_free(&pkt);
	}

	double to_ms(int64_t pts) const {
		const AVStream* st = video.fmt->streams[video.stream];
		return static_cast<double>(pts) * av_q2d(st->time_base) * 1000.0;
	}

	// Decodes the next video frame into `frame`; false at the end of the stream.
	bool next_frame() {
		while (true) {
			const int r = avcodec_receive_frame(video.codec, frame);
			if (r == 0) {
				const int64_t pts = frame->best_effort_timestamp;
				frame_time = pts == AV_NOPTS_VALUE ? frame_time + 33.3 : to_ms(pts);
				have_frame = true;
				return true;
			}
			if (r == AVERROR_EOF) return false;
			if (r != AVERROR(EAGAIN)) return false;
			if (eof) {
				if (drained) return false;
				avcodec_send_packet(video.codec, nullptr);
				drained = true;
				continue;
			}
			const int rr = av_read_frame(video.fmt, pkt);
			if (rr < 0) {
				eof = true;
				continue;
			}
			if (pkt->stream_index == video.stream) avcodec_send_packet(video.codec, pkt);
			av_packet_unref(pkt);
		}
	}

	void decode_audio() {
		Demux a;
		if (!a.open(file, AVMEDIA_TYPE_AUDIO)) return;
		SwrContext* swr = nullptr;
		AVChannelLayout out_layout;
		av_channel_layout_default(&out_layout, 2);
		if (swr_alloc_set_opts2(&swr, &out_layout, AV_SAMPLE_FMT_S16, 48000, &a.codec->ch_layout, a.codec->sample_fmt,
								a.codec->sample_rate, 0, nullptr) < 0 || swr_init(swr) < 0) {
			swr_free(&swr);
			return;
		}
		auto out = std::make_shared<Pcm>();
		out->channels = 2;
		out->rate = 48000;
		AVPacket* p = av_packet_alloc();
		AVFrame* f = av_frame_alloc();
		std::vector<int16_t> tmp;
		auto drain = [&](bool flush) {
			while (flush || avcodec_receive_frame(a.codec, f) == 0) {
				const int max_out = swr_get_out_samples(swr, flush ? 0 : f->nb_samples) + 64;
				tmp.resize(static_cast<size_t>(max_out) * 2);
				uint8_t* dst[1] = {reinterpret_cast<uint8_t*>(tmp.data())};
				const int n = swr_convert(swr, dst, max_out, flush ? nullptr : const_cast<const uint8_t**>(f->extended_data),
										  flush ? 0 : f->nb_samples);
				if (n > 0) out->data.insert(out->data.end(), tmp.begin(), tmp.begin() + static_cast<size_t>(n) * 2);
				if (flush) break;
			}
		};
		while (av_read_frame(a.fmt, p) >= 0) {
			if (p->stream_index == a.stream && avcodec_send_packet(a.codec, p) >= 0) drain(false);
			av_packet_unref(p);
		}
		avcodec_send_packet(a.codec, nullptr);
		drain(false);
		drain(true);
		av_frame_free(&f);
		av_packet_free(&p);
		swr_free(&swr);
		av_channel_layout_uninit(&out_layout);
		if (!out->data.empty()) pcm = out;
	}
};

Movie::Movie() : impl_(std::make_unique<Impl>()) {}
Movie::~Movie() = default;

std::unique_ptr<Movie> Movie::open(std::vector<uint8_t> file, std::string* error) {
	std::unique_ptr<Movie> m(new Movie());
	Impl& d = *m->impl_;
	d.file = std::move(file);
	if (!d.video.open(d.file, AVMEDIA_TYPE_VIDEO)) {
		if (error) *error = "no decodable video stream";
		return nullptr;
	}
	d.w = d.video.codec->width;
	d.h = d.video.codec->height;
	const AVFormatContext* f = d.video.fmt;
	d.duration = f->duration > 0 ? static_cast<double>(f->duration) * 1000.0 / AV_TIME_BASE : 0.0;
	d.frame = av_frame_alloc();
	d.pkt = av_packet_alloc();
	d.decode_audio();
	return m;
}

int Movie::width() const { return impl_->w; }
int Movie::height() const { return impl_->h; }
double Movie::duration_ms() const { return impl_->duration; }
std::shared_ptr<const Pcm> Movie::audio() const { return impl_->pcm; }
bool Movie::at_end() const { return impl_->drained && !impl_->have_frame; }

void Movie::rewind() {
	Impl& d = *impl_;
	av_seek_frame(d.video.fmt, -1, 0, AVSEEK_FLAG_BACKWARD);
	avcodec_flush_buffers(d.video.codec);
	d.eof = d.drained = d.have_frame = false;
	d.frame_time = -1;
}

bool Movie::advance(double t_ms, std::vector<uint8_t>& rgba) {
	Impl& d = *impl_;
	bool changed = false;
	// `frame` holds the next undisplayed frame; show every frame that is due, keeping only the newest
	while (true) {
		if (!d.have_frame && !d.next_frame()) break;
		if (d.frame_time > t_ms) break;
		d.sws = sws_getCachedContext(d.sws, d.frame->width, d.frame->height, static_cast<AVPixelFormat>(d.frame->format), d.w, d.h,
									 AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
		rgba.resize(static_cast<size_t>(d.w) * d.h * 4);
		uint8_t* dst[1] = {rgba.data()};
		const int stride[1] = {d.w * 4};
		sws_scale(d.sws, d.frame->data, d.frame->linesize, 0, d.frame->height, dst, stride);
		changed = true;
		d.have_frame = false;
	}
	return changed;
}

} // namespace omw
