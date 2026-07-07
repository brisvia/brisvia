// Brisvia — RandomX seed selection BY HEIGHT (not by time, unlike ScashX).
// The RandomX key is derived from the hash of a previous block. It rotates every 2048 blocks with a
// delay of 64. Deterministic and robust against timestamp manipulation and reorgs.
#ifndef BRISVIA_CONSENSUS_RANDOMX_SEED_H
#define BRISVIA_CONSENSUS_RANDOMX_SEED_H

static const int BRISVIA_SEED_PERIOD = 2048; // rotates every 2048 blocks (~2.84 days at 120 s)
static const int BRISVIA_SEED_DELAY  = 64;   // delay of 64 blocks (~2 h 8 min)
static const int BRISVIA_SEED_INITIAL = -1;  // heights 0..63: fixed initial launch seed

// Returns the HEIGHT of the block whose hash is used as the RandomX seed to mine/validate `nHeight`.
// Returns BRISVIA_SEED_INITIAL (-1) while the chain is too young (height < 64).
inline int BrisviaSeedHeight(int nHeight) {
    if (nHeight < BRISVIA_SEED_DELAY) return BRISVIA_SEED_INITIAL;
    return ((nHeight - BRISVIA_SEED_DELAY) / BRISVIA_SEED_PERIOD) * BRISVIA_SEED_PERIOD;
}

// Does the height use the fixed initial launch seed? (heights 0..63).
// In that range the RandomX key is a chainparams constant, not a block hash.
inline bool BrisviaUsesInitialSeed(int nHeight) {
    return nHeight < BRISVIA_SEED_DELAY;
}

// Seed epoch index: -1 in the initial range (0..63); 0,1,2,... afterwards.
// It is the cache key: two heights with the SAME epoch share the RandomX cache/dataset,
// so it is not reinitialized (an expensive operation: ~256 MB in light, ~2 GB in fast) on every block.
inline int BrisviaSeedEpoch(int nHeight) {
    if (nHeight < BRISVIA_SEED_DELAY) return -1;
    return (nHeight - BRISVIA_SEED_DELAY) / BRISVIA_SEED_PERIOD;
}

// Does the seed change at this height relative to the previous height?
// True at block 0 (startup) and at every 64 + k*2048 boundary. When true, the
// validator/miner must (re)initialize the RandomX cache/dataset with the new key.
inline bool BrisviaSeedChangesAt(int nHeight) {
    if (nHeight <= 0) return true;
    return BrisviaSeedEpoch(nHeight) != BrisviaSeedEpoch(nHeight - 1);
}

#endif // BRISVIA_CONSENSUS_RANDOMX_SEED_H
