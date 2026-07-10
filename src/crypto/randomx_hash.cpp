// Brisvia - minimal RandomX wrapper. Computes the Proof of Work hash.
// "light" mode (cache only, ~256 MiB): used to VERIFY blocks. The miner
// will use "fast" mode (with dataset, ~2 GiB). Basis of Commit 2 of the port.
#include "randomx_hash.h"

#include <randomx.h>

bool brisvia_randomx_hash(const unsigned char* key, size_t key_size,
                          const unsigned char* input, size_t input_size,
                          unsigned char out[BRISVIA_RANDOMX_HASH_SIZE]) {
    // Flags recommended by the library itself for this platform (JIT, etc.).
    randomx_flags flags = randomx_get_flags();

    randomx_cache* cache = randomx_alloc_cache(flags);
    if (cache == nullptr) return false;

    // The "key" is the RandomX seed (in Brisvia it will be derived from a block by height).
    randomx_init_cache(cache, key, key_size);

    // dataset = nullptr -> light mode (verification). Without allocating the ~2 GiB of mining mode.
    randomx_vm* vm = randomx_create_vm(flags, cache, nullptr);
    if (vm == nullptr) {
        randomx_release_cache(cache);
        return false;
    }

    randomx_calculate_hash(vm, input, input_size, out);

    randomx_destroy_vm(vm);
    randomx_release_cache(cache);
    return true;
}
