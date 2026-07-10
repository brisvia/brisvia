// Brisvia - minimal RandomX wrapper for the node.
// Computes the RandomX Proof of Work hash. Basis of Commit 2 of the controlled port.
#ifndef BRISVIA_CRYPTO_RANDOMX_HASH_H
#define BRISVIA_CRYPTO_RANDOMX_HASH_H

#include <cstddef>

#define BRISVIA_RANDOMX_HASH_SIZE 32

// Computes the RandomX hash (light mode, for verification) of `input` using `key` (the seed).
// Returns true if it succeeded and leaves the 32-byte hash in `out`.
bool brisvia_randomx_hash(const unsigned char* key, size_t key_size,
                          const unsigned char* input, size_t input_size,
                          unsigned char out[BRISVIA_RANDOMX_HASH_SIZE]);

#endif // BRISVIA_CRYPTO_RANDOMX_HASH_H
