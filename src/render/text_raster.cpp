#include "render/text_raster.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace omw {

struct FontFace {
	std::vector<uint8_t> data;
	stbtt_fontinfo info{};
	bool ok = false;
};

namespace {

// The font files shipped in font.npk.
const char* const kFontFiles[] = {"media/font/notosanssc-light.otf", "media/font/notoserifsc-regular.otf"};

std::string normalize(const std::string& s) {
	std::string r;
	for (char c : s) {
		if (std::isalnum(static_cast<unsigned char>(c))) r += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return r;
}

std::string file_key(const std::string& path) {
	const size_t slash = path.find_last_of('/');
	std::string base = path.substr(slash == std::string::npos ? 0 : slash + 1);
	const size_t dot = base.find_last_of('.');
	if (dot != std::string::npos) base.resize(dot);
	return normalize(base);
}

std::vector<uint32_t> decode_utf8(const std::string& s) {
	std::vector<uint32_t> out;
	for (size_t i = 0; i < s.size();) {
		const unsigned char c = static_cast<unsigned char>(s[i]);
		uint32_t cp = c;
		int n = 0;
		if (c >= 0xF0) { cp = c & 0x07; n = 3; }
		else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
		else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
		i++;
		for (; n > 0 && i < s.size(); n--, i++) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
		out.push_back(cp);
	}
	return out;
}

bool is_half_width(uint32_t cp) { return cp < 0x80 || (cp >= 0xFF61 && cp <= 0xFF9F); }

// Characters that must not begin a line (closing brackets, punctuation, small kana, long vowel mark).
bool no_line_start(uint32_t cp) {
	static const uint32_t set[] = {U'、', U'。', U'，', U'．', U'）', U'」', U'』', U'】', U'〉', U'》', U'〕', U'！', U'？', U'ー',
								   U'…', U'・', U'：', U'；', U'”', U'’', U')', U']', U'}', U',', U'.', U'!', U'?', U'ぁ', U'ぃ',
								   U'ぅ', U'ぇ', U'ぉ', U'っ', U'ゃ', U'ゅ', U'ょ', U'ァ', U'ィ', U'ゥ', U'ェ', U'ォ', U'ッ', U'ャ', U'ュ', U'ョ'};
	return std::find(std::begin(set), std::end(set), cp) != std::end(set);
}

float advance_of(const FontEntry& f, uint32_t cp) {
	if (!f.face || !f.face->ok) return f.size;
	const float scale = stbtt_ScaleForMappingEmToPixels(&f.face->info, f.size);
	int adv = 0, lsb = 0;
	stbtt_GetCodepointHMetrics(&f.face->info, static_cast<int>(cp), &adv, &lsb);
	return adv * scale;
}

float ascent_of(const FontEntry& f) {
	if (!f.face || !f.face->ok) return f.size * 0.88f;
	const float scale = stbtt_ScaleForMappingEmToPixels(&f.face->info, f.size);
	int a = 0, d = 0, g = 0;
	stbtt_GetFontVMetrics(&f.face->info, &a, &d, &g);
	return a * scale;
}

} // namespace

FontLibrary::FontLibrary() = default;
FontLibrary::~FontLibrary() = default;

void FontLibrary::register_font(const std::string& name, const std::string& family, float size) { fonts_[name] = {family, size}; }

FontFace* FontLibrary::face_for_family(const std::string& family) {
	const std::string want = normalize(family);
	std::string best_path;
	for (const char* path : kFontFiles) {
		const std::string key = file_key(path);
		if (!want.empty() && (key.rfind(want, 0) == 0 || want.rfind(key, 0) == 0)) {
			best_path = path;
			break;
		}
	}
	if (best_path.empty()) best_path = kFontFiles[0];
	auto it = faces_.find(best_path);
	if (it != faces_.end()) return it->second.get();
	auto face = std::make_unique<FontFace>();
	if (files_ && files_->read(best_path, face->data)) {
		const int off = stbtt_GetFontOffsetForIndex(face->data.data(), 0);
		face->ok = stbtt_InitFont(&face->info, face->data.data(), off) != 0;
	}
	FontFace* raw = face.get();
	faces_[best_path] = std::move(face);
	return raw;
}

FontEntry FontLibrary::lookup(const std::string& name) {
	FontEntry e;
	auto it = fonts_.find(name);
	if (it != fonts_.end()) {
		e.face = face_for_family(it->second.first);
		e.size = it->second.second;
	} else {
		e.face = face_for_family(name);
		e.size = 21.f;
	}
	return e;
}

TextLayout layout_text(const std::vector<TextRun>& runs, const TextStyle& st) {
	TextLayout out;
	const float size = st.font.size;
	const float ruby_h = st.ruby_font.size;
	const float base_dy = std::max(0.f, ruby_h + st.ruby_offset); // top of the base glyph box inside a line
	const float pitch = base_dy + size + st.row_space;
	const float limit = st.field_w > 0 ? static_cast<float>(st.field_w) : 1e9f;
	const float asc = ascent_of(st.font), ruby_asc = ascent_of(st.ruby_font);

	float x = 0, line_top = 0;
	bool line_empty = true;
	int ruby_index = 0;
	auto newline = [&]() {
		x = 0;
		line_top += pitch;
		line_empty = true;
	};

	for (const TextRun& run : runs) {
		const std::vector<uint32_t> cps = decode_utf8(run.text);
		const int first_base = out.char_count;
		float run_x0 = x, run_line_top = line_top;
		bool run_started = false;
		for (uint32_t cp : cps) {
			if (cp == '\r' || cp == '\t') continue;
			if (cp == '\n') {
				newline();
				continue;
			}
			float adv = is_half_width(cp) ? advance_of(st.font, cp) + st.pitch_half : size + st.pitch_full;
			if (!line_empty && x + adv > limit && !no_line_start(cp)) newline();
			if (!run_started) {
				run_x0 = x;
				run_line_top = line_top;
				run_started = true;
			}
			LaidGlyph g;
			g.cp = cp;
			g.x = x;
			g.baseline = line_top + base_dy + asc;
			g.size = size;
			g.face = st.font.face;
			g.index = out.char_count++;
			out.glyphs.push_back(g);
			out.char_rects.push_back({x, line_top + base_dy, adv, size});
			x += adv;
			line_empty = false;
		}
		if (!run.ruby.empty() && run_started) {
			const std::vector<uint32_t> rc = decode_utf8(run.ruby);
			float rw = 0;
			for (uint32_t cp : rc) rw += advance_of(st.ruby_font, cp);
			const float base_w = x - run_x0;
			float rx = run_x0 + (base_w - rw) * 0.5f;
			const float start = rx;
			for (uint32_t cp : rc) {
				LaidGlyph g;
				g.cp = cp;
				g.x = rx;
				g.baseline = run_line_top + ruby_asc;
				g.size = ruby_h;
				g.face = st.ruby_font.face;
				g.index = ruby_index;
				g.ruby = true;
				g.base_first = first_base;
				out.glyphs.push_back(g);
				rx += advance_of(st.ruby_font, cp);
			}
			out.ruby_rects.push_back({start, run_line_top, rw, ruby_h});
			ruby_index++;
		}
		out.width = std::max(out.width, x);
	}
	out.height = line_top + (line_empty ? 0.f : pitch);
	return out;
}

namespace {

void over(uint8_t* px, const float rgb[3], float a) {
	if (a <= 0.f) return;
	const float da = px[3] / 255.f;
	const float oa = a + da * (1.f - a);
	for (int c = 0; c < 3; c++) {
		const float dc = px[c] / 255.f;
		const float oc = oa > 0.f ? (rgb[c] * a + dc * da * (1.f - a)) / oa : 0.f;
		px[c] = static_cast<uint8_t>(std::clamp(oc, 0.f, 1.f) * 255.f + 0.5f);
	}
	px[3] = static_cast<uint8_t>(std::clamp(oa, 0.f, 1.f) * 255.f + 0.5f);
}

void draw_glyph(const LaidGlyph& g, float ox, float oy, const float rgb[3], float alpha, int w, int h, std::vector<uint8_t>& rgba) {
	if (!g.face || !g.face->ok) return;
	const float scale = stbtt_ScaleForMappingEmToPixels(&g.face->info, g.size);
	int x0, y0, x1, y1;
	stbtt_GetCodepointBitmapBox(&g.face->info, static_cast<int>(g.cp), scale, scale, &x0, &y0, &x1, &y1);
	const int gw = x1 - x0, gh = y1 - y0;
	if (gw <= 0 || gh <= 0) return;
	std::vector<uint8_t> cov(static_cast<size_t>(gw) * gh);
	stbtt_MakeCodepointBitmap(&g.face->info, cov.data(), gw, gh, gw, scale, scale, static_cast<int>(g.cp));
	const int px0 = static_cast<int>(std::floor(g.x + ox)) + x0;
	const int py0 = static_cast<int>(std::floor(g.baseline + oy)) + y0;
	for (int y = 0; y < gh; y++) {
		const int py = py0 + y;
		if (py < 0 || py >= h) continue;
		for (int x = 0; x < gw; x++) {
			const int px = px0 + x;
			if (px < 0 || px >= w) continue;
			const float a = cov[static_cast<size_t>(y) * gw + x] / 255.f * alpha;
			over(&rgba[(static_cast<size_t>(py) * w + px) * 4], rgb, a);
		}
	}
}

} // namespace

void rasterize_text(const TextLayout& layout, const TextStyle& st, int visible, int w, int h, std::vector<uint8_t>& rgba) {
	rgba.assign(static_cast<size_t>(w) * h * 4, 0);
	auto shown = [&](const LaidGlyph& g) { return g.ruby ? g.base_first < visible : g.index < visible; };
	if (st.shadow_type != 0 && st.shadow_gap > 0) {
		const float gap = static_cast<float>(st.shadow_gap);
		static const float dirs[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {0.7f, 0.7f}, {-0.7f, 0.7f}, {0.7f, -0.7f}, {-0.7f, -0.7f}};
		for (const LaidGlyph& g : layout.glyphs) {
			if (!shown(g)) continue;
			for (const auto& d : dirs) draw_glyph(g, d[0] * gap, d[1] * gap, st.shadow_color, st.shadow_power * 0.5f, w, h, rgba);
		}
	}
	for (const LaidGlyph& g : layout.glyphs)
		if (shown(g)) draw_glyph(g, 0, 0, st.color, 1.f, w, h, rgba);
}

} // namespace omw
