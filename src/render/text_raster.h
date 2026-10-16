// Text layout and rasterisation: marked-up UTF-8 text -> RGBA bitmap, with per-character rectangles for the scripts.
#pragma once

#include "game/game_files.h"
#include "game/text_markup.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace omw {

struct FontFace;

// Fonts the game registers with loadSystemFont(name, family, size, weight, ...).
struct FontEntry {
	FontFace* face = nullptr;
	float size = 20.f;
};

class FontLibrary {
public:
	FontLibrary();
	~FontLibrary();
	void set_files(const GameFiles* files) { files_ = files; }
	void register_font(const std::string& name, const std::string& family, float size);
	// Registered name (e.g. "Noto Sans SC Light") -> face and size. Unknown names fall back to the first font file found.
	FontEntry lookup(const std::string& name);

private:
	FontFace* face_for_family(const std::string& family);
	const GameFiles* files_ = nullptr;
	std::map<std::string, std::unique_ptr<FontFace>> faces_;       // by file path
	std::map<std::string, std::pair<std::string, float>> fonts_;   // name -> (family, size)
};

struct Rect { float x = 0, y = 0, w = 0, h = 0; };

struct TextStyle {
	FontEntry font, ruby_font;
	int field_w = 0, field_h = 0;
	int row_space = 0, pitch_full = 0, pitch_half = 0, ruby_offset = 0;
	float color[3] = {1, 1, 1};
	int shadow_type = 0, shadow_gap = 0;
	float shadow_power = 1.f;
	float shadow_color[3] = {0, 0, 0};
};

struct LaidGlyph {
	uint32_t cp = 0;
	float x = 0, baseline = 0, size = 0;
	FontFace* face = nullptr;
	int index = 0;       // visible-character index (base glyphs) or ruby index (ruby glyphs)
	bool ruby = false;
	int base_first = 0;  // for ruby glyphs: index of the first base character it annotates
};

struct TextLayout {
	std::vector<LaidGlyph> glyphs;
	std::vector<Rect> char_rects, ruby_rects;
	int char_count = 0;
	float width = 0, height = 0;
};

TextLayout layout_text(const std::vector<TextRun>& runs, const TextStyle& style);

// Draws the first `visible` characters (and the ruby over them) into an RGBA bitmap of w x h.
void rasterize_text(const TextLayout& layout, const TextStyle& style, int visible, int w, int h, std::vector<uint8_t>& rgba);

} // namespace omw
