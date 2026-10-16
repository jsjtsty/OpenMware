/* see copyright notice in squirrel.h */
#ifndef _SQFORMAT_H_
#define _SQFORMAT_H_

#ifdef __cplusplus

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

inline SQInteger sq_vformat_to_buffer(SQChar *buffer, SQUnsignedInteger size, const SQChar *fmt, va_list args)
{
	if(buffer == nullptr || size == 0) {
		return -1;
	}

#ifdef SQUNICODE
	std::vector<SQChar> scratch(static_cast<std::size_t>(size));
	while(true) {
		va_list copy;
		va_copy(copy, args);
		const int written = std::vswprintf(scratch.data(), scratch.size(), fmt, copy);
		va_end(copy);
		if(written >= 0 && static_cast<std::size_t>(written) < scratch.size()) {
			std::memcpy(buffer, scratch.data(), (static_cast<std::size_t>(written) + 1u) * sizeof(SQChar));
			return static_cast<SQInteger>(written);
		}
		if(scratch.size() > (1u << 20)) {
			buffer[0] = 0;
			return -1;
		}
		scratch.resize(scratch.size() * 2u);
	}
#else
	va_list copy;
	va_copy(copy, args);
	const int written = std::vsnprintf(buffer, static_cast<std::size_t>(size), fmt, copy);
	va_end(copy);
	if(written < 0) {
		buffer[0] = 0;
		return -1;
	}
	if(static_cast<SQUnsignedInteger>(written) >= size) {
		buffer[size - 1] = 0;
	}
	return static_cast<SQInteger>(written);
#endif
}

inline SQInteger sq_format_to_buffer(SQChar *buffer, SQUnsignedInteger size, const SQChar *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	SQInteger result = sq_vformat_to_buffer(buffer, size, fmt, args);
	va_end(args);
	return result;
}

inline std::basic_string<SQChar> sq_vformat_string(const SQChar *fmt, va_list args)
{
	std::basic_string<SQChar> result;

#ifdef SQUNICODE
	std::vector<SQChar> scratch(256);
	while(true) {
		va_list copy;
		va_copy(copy, args);
		const int written = std::vswprintf(scratch.data(), scratch.size(), fmt, copy);
		va_end(copy);
		if(written >= 0 && static_cast<std::size_t>(written) < scratch.size()) {
			result.assign(scratch.data(), static_cast<std::size_t>(written));
			return result;
		}
		if(scratch.size() > (1u << 20)) {
			return result;
		}
		scratch.resize(scratch.size() * 2u);
	}
#else
	va_list copy;
	va_copy(copy, args);
	const int needed = std::vsnprintf(nullptr, 0, fmt, copy);
	va_end(copy);
	if(needed < 0) {
		return result;
	}
	result.resize(static_cast<std::size_t>(needed) + 1u);
	va_copy(copy, args);
	const int written = std::vsnprintf(result.data(), result.size(), fmt, copy);
	va_end(copy);
	if(written < 0) {
		result.clear();
		return result;
	}
	result.resize(static_cast<std::size_t>(written));
	return result;
#endif
}

inline std::basic_string<SQChar> sq_format_string(const SQChar *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	std::basic_string<SQChar> result = sq_vformat_string(fmt, args);
	va_end(args);
	return result;
}

#endif /* __cplusplus */

#endif /* _SQFORMAT_H_ */
