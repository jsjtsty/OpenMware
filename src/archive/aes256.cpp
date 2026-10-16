#include "archive/aes256.h"

#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define OMW_X86 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace omw {
namespace {

struct Tables {
	uint8_t sbox[256];
	uint8_t inv_sbox[256];
	uint8_t mul9[256], mul11[256], mul13[256], mul14[256];

	static uint8_t xtime(uint8_t x) { return static_cast<uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
	static uint8_t gmul(uint8_t a, uint8_t b) {
		uint8_t r = 0;
		while (b) {
			if (b & 1) r ^= a;
			a = xtime(a);
			b >>= 1;
		}
		return r;
	}
	static uint8_t rotl8(uint8_t x, int s) { return static_cast<uint8_t>((x << s) | (x >> (8 - s))); }

	Tables() {
		uint8_t p = 1, q = 1;
		do {
			p = static_cast<uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
			q ^= static_cast<uint8_t>(q << 1);
			q ^= static_cast<uint8_t>(q << 2);
			q ^= static_cast<uint8_t>(q << 4);
			if (q & 0x80) q ^= 0x09;
			uint8_t x = static_cast<uint8_t>(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4));
			sbox[p] = static_cast<uint8_t>(x ^ 0x63);
		} while (p != 1);
		sbox[0] = 0x63;
		for (int i = 0; i < 256; i++) inv_sbox[sbox[i]] = static_cast<uint8_t>(i);
		for (int i = 0; i < 256; i++) {
			mul9[i] = gmul(static_cast<uint8_t>(i), 9);
			mul11[i] = gmul(static_cast<uint8_t>(i), 11);
			mul13[i] = gmul(static_cast<uint8_t>(i), 13);
			mul14[i] = gmul(static_cast<uint8_t>(i), 14);
		}
	}
};

const Tables& tables() {
	static const Tables t;
	return t;
}

void expand_key(const uint8_t key[32], uint8_t* out /* 240 bytes */) {
	const Tables& t = tables();
	std::memcpy(out, key, 32);
	uint8_t rcon = 1;
	for (int i = 8; i < 60; i++) {
		uint8_t w[4];
		std::memcpy(w, out + (i - 1) * 4, 4);
		if (i % 8 == 0) {
			const uint8_t first = w[0];
			w[0] = static_cast<uint8_t>(t.sbox[w[1]] ^ rcon);
			w[1] = t.sbox[w[2]];
			w[2] = t.sbox[w[3]];
			w[3] = t.sbox[first];
			rcon = Tables::xtime(rcon);
		} else if (i % 8 == 4) {
			for (auto& b : w) b = t.sbox[b];
		}
		for (int j = 0; j < 4; j++) out[i * 4 + j] = static_cast<uint8_t>(out[(i - 8) * 4 + j] ^ w[j]);
	}
}

void decrypt_block_portable(const uint8_t* rk, const uint8_t in[16], uint8_t out[16]) {
	const Tables& t = tables();
	uint8_t s[16];
	for (int i = 0; i < 16; i++) s[i] = in[i] ^ rk[14 * 16 + i];
	for (int round = 13; round >= 0; round--) {
		// InvShiftRows (state is column-major: s[col * 4 + row]).
		uint8_t u[16];
		for (int c = 0; c < 4; c++)
			for (int r = 0; r < 4; r++) u[((c + r) & 3) * 4 + r] = s[c * 4 + r];
		for (int i = 0; i < 16; i++) s[i] = static_cast<uint8_t>(t.inv_sbox[u[i]] ^ rk[round * 16 + i]);
		if (round == 0) break;
		for (int c = 0; c < 4; c++) {
			const uint8_t a0 = s[c * 4], a1 = s[c * 4 + 1], a2 = s[c * 4 + 2], a3 = s[c * 4 + 3];
			s[c * 4 + 0] = static_cast<uint8_t>(t.mul14[a0] ^ t.mul11[a1] ^ t.mul13[a2] ^ t.mul9[a3]);
			s[c * 4 + 1] = static_cast<uint8_t>(t.mul9[a0] ^ t.mul14[a1] ^ t.mul11[a2] ^ t.mul13[a3]);
			s[c * 4 + 2] = static_cast<uint8_t>(t.mul13[a0] ^ t.mul9[a1] ^ t.mul14[a2] ^ t.mul11[a3]);
			s[c * 4 + 3] = static_cast<uint8_t>(t.mul11[a0] ^ t.mul13[a1] ^ t.mul9[a2] ^ t.mul14[a3]);
		}
	}
	std::memcpy(out, s, 16);
}

#ifdef OMW_X86
bool cpu_has_aesni() {
#if defined(_MSC_VER)
	int info[4];
	__cpuid(info, 1);
	return (info[2] & (1 << 25)) != 0;
#else
	unsigned a, b, c, d;
	return __get_cpuid(1, &a, &b, &c, &d) && (c & (1u << 25));
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("aes,sse2")))
#endif
void decrypt_aesni(const uint8_t* rk, const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t len) {
	__m128i dk[15];
	dk[0] = _mm_load_si128(reinterpret_cast<const __m128i*>(rk));
	for (int i = 1; i < 14; i++) dk[i] = _mm_aesimc_si128(_mm_load_si128(reinterpret_cast<const __m128i*>(rk + i * 16)));
	dk[14] = _mm_load_si128(reinterpret_cast<const __m128i*>(rk + 14 * 16));
	__m128i prev = _mm_loadu_si128(reinterpret_cast<const __m128i*>(iv));
	for (size_t off = 0; off < len; off += 16) {
		const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + off));
		__m128i x = _mm_xor_si128(c, dk[14]);
		for (int r = 13; r >= 1; r--) x = _mm_aesdec_si128(x, dk[r]);
		x = _mm_aesdeclast_si128(x, dk[0]);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(out + off), _mm_xor_si128(x, prev));
		prev = c;
	}
}
#endif

} // namespace

Aes256Cbc::Aes256Cbc(const uint8_t key[32]) {
	expand_key(key, round_keys_);
#ifdef OMW_X86
	use_aesni_ = cpu_has_aesni();
#else
	use_aesni_ = false;
#endif
}

void Aes256Cbc::decrypt(const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t len) const {
#ifdef OMW_X86
	if (use_aesni_) {
		decrypt_aesni(round_keys_, iv, in, out, len);
		return;
	}
#endif
	uint8_t prev[16];
	std::memcpy(prev, iv, 16);
	for (size_t off = 0; off < len; off += 16) {
		uint8_t cipher[16], plain[16];
		std::memcpy(cipher, in + off, 16);
		decrypt_block_portable(round_keys_, cipher, plain);
		for (int i = 0; i < 16; i++) out[off + i] = static_cast<uint8_t>(plain[i] ^ prev[i]);
		std::memcpy(prev, cipher, 16);
	}
}

} // namespace omw
