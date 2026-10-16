#pragma once

#include <cstddef>
#include <cstdint>

namespace omw {

// AES-256 in CBC mode, decrypt only (the engine never writes archives).
// Uses AES-NI when the CPU has it, otherwise a portable implementation.
class Aes256Cbc {
public:
	explicit Aes256Cbc(const uint8_t key[32]);

	// `len` must be a multiple of 16. `in` and `out` may alias.
	void decrypt(const uint8_t iv[16], const uint8_t* in, uint8_t* out, size_t len) const;

private:
	alignas(16) uint8_t round_keys_[15 * 16];
	bool use_aesni_;
};

} // namespace omw
