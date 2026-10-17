#include "game/text_markup.h"

namespace omw {
namespace {

// "#{base}ruby#" -> "<RUBY text='ruby'>base</RUBY>" (the original rewrites the string the same way before tokenizing)
std::string expand_shorthand(const std::string& s) {
	std::string out;
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] == '#' && i + 1 < s.size() && s[i + 1] == '{') {
			const size_t close = s.find('}', i + 2);
			const size_t end = close == std::string::npos ? close : s.find('#', close + 1);
			if (end != std::string::npos) {
				out += "<RUBY text='" + s.substr(close + 1, end - close - 1) + "'>" + s.substr(i + 2, close - i - 2) + "</RUBY>";
				i = end + 1;
				continue;
			}
		}
		out += s[i++];
	}
	return out;
}

} // namespace

std::vector<TextRun> parse_text_markup(const std::string& src) {
	const std::string s = expand_shorthand(src);
	std::vector<TextRun> runs;
	TextRun cur;
	std::string ruby_pending;
	bool in_ruby = false, centered = false;
	auto flush = [&]() {
		cur.center = centered;
		if (!cur.text.empty() || !cur.ruby.empty()) runs.push_back(cur);
		cur = {};
	};
	size_t i = 0;
	while (i < s.size()) {
		if (s[i] == '<') {
			const size_t gt = s.find('>', i);
			if (gt != std::string::npos) {
				const std::string tag = s.substr(i + 1, gt - i - 1);
				if (tag.rfind("RUBY", 0) == 0) {
					flush();
					const size_t q = tag.find("text='");
					if (q != std::string::npos) {
						const size_t e = tag.find('\'', q + 6);
						cur.ruby = tag.substr(q + 6, e == std::string::npos ? e : e - q - 6);
					}
					in_ruby = true;
				} else if (tag == "/RUBY") {
					flush();
					in_ruby = false;
				} else if (tag == "center" || tag == "CENTER") {
					flush();
					centered = true;
				} else if (tag == "/center" || tag == "/CENTER") {
					flush();
					centered = false;
				}
				i = gt + 1;
				continue;
			}
		}
		cur.text += s[i++];
	}
	(void)in_ruby;
	flush();
	return runs;
}

std::string strip_controls(const std::string& s) {
	std::string r;
	for (char c : s)
		if (c != '\r' && c != '\n' && c != '\t') r += c;
	return r;
}

size_t utf16_length(const std::string& u) {
	size_t n = 0;
	for (unsigned char c : u) {
		if ((c & 0xC0) == 0x80) continue; // continuation byte
		n += c >= 0xF0 ? 2 : 1;
	}
	return n;
}

} // namespace omw
