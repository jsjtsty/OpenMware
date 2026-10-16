// Inline markup of the dialogue text: "#{base}ruby#" shorthand for furigana and <TAG attr='value'>...</TAG> tags.
#pragma once
#include <string>
#include <vector>

namespace omw {

struct TextRun {
	std::string text; // visible characters (UTF-8), raw, may contain line breaks
	std::string ruby; // furigana for this run (empty when none)
};

// Splits marked-up UTF-8 text into runs. Unknown tags are dropped, their content is kept.
std::vector<TextRun> parse_text_markup(const std::string& src);

// Removes carriage returns, line feeds and tabs (they do not count as characters).
std::string strip_controls(const std::string& s);

// Number of UTF-16 code units, the unit the original counts in.
size_t utf16_length(const std::string& utf8);

} // namespace omw
