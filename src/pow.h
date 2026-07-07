// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_H
#define BITCOIN_POW_H

#include <consensus/params.h>

#include <array>
#include <cstdint>
#include <vector>

class CBlockHeader;
class CBlockIndex;
class uint256;
class arith_uint256;

// Brisvia: canonical size of the RandomX input = standard 80-byte header (without hashRandomX).
static constexpr size_t BRISVIA_RANDOMX_INPUT_SIZE = 80;

/**
 * Convert nBits value to target.
 *
 * @param[in] nBits     compact representation of the target
 * @param[in] pow_limit PoW limit (consensus parameter)
 *
 * @return              the proof-of-work target or nullopt if the nBits value
 *                      is invalid (due to overflow or exceeding pow_limit)
 */
std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit);

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params&);
unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params&);

/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params&);
bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params&);

/**
 * Return false if the proof-of-work requirement specified by new_nbits at a
 * given height is not possible, given the proof-of-work on the prior block as
 * specified by old_nbits.
 *
 * This function only checks that the new value is within a factor of 4 of the
 * old value for blocks at the difficulty adjustment interval, and otherwise
 * requires the values to be the same.
 *
 * Always returns true on networks where min difficulty blocks are allowed,
 * such as regtest/testnet.
 */
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits);

// ===== Brisvia: ASERT-BRVA-v1 (difficulty adjustment) =====
/**
 * Computes the next target by ASERT (absolute formulation from the anchor), clamped to powLimit.
 * Math validated against the Python oracle (asert_oracle.py): 8160 vectors, 0 failures.
 */
arith_uint256 CalculateASERT(const arith_uint256& refTarget,
                             int64_t nPowTargetSpacing,
                             int64_t nTimeDiff,
                             int64_t nHeightDiff,
                             const arith_uint256& powLimit,
                             int64_t nHalfLife) noexcept;

/** Next nBits by ASERT using the params anchor (asertAnchorParams, genesis with a synthetic parent). */
uint32_t GetNextASERTWorkRequired(const CBlockIndex* pindexPrev,
                                  const CBlockHeader* pblock,
                                  const Consensus::Params& params) noexcept;

/**
 * PURE by-VALUES ASERT variant: it only needs the parent's HEIGHT and TIME (absolute ASERT from the anchor
 * does not walk ancestors). Used by the TRANSIENT branch of the headers sync, where the parent may be a header
 * not yet indexed (without CBlockIndex). The CBlockIndex version delegates to this one.
 */
uint32_t GetNextASERTWorkRequiredFromValues(int prevHeight, int64_t prevTime,
                                            const CBlockHeader* pblock,
                                            const Consensus::Params& params) noexcept;

// ===== Brisvia: PURE RandomX input/output functions =====
/**
 * Serializes the header into the 80 canonical bytes passed to RandomX, with EXPLICIT endianness
 * (never implicit). Layout: version(4 LE) | hashPrevBlock(32) | hashMerkleRoot(32) |
 * nTime(4 LE) | nBits(4 LE) | nNonce(4 LE) = 80. The nonce sits at offset 76. It is identical to Bitcoin's
 * standard network format; it is written field by field so it does not depend on the struct layout nor on the
 * header still having 6 fields. Single definition: used by consensus, RPC, tests, miner and vectors.
 */
std::array<unsigned char, BRISVIA_RANDOMX_INPUT_SIZE> SerializeHeaderForRandomX(const CBlockHeader& block);

/**
 * Interprets the 32 RandomX output bytes as the 256-bit integer compared against the target.
 * FIXED and documented rule: little-endian (rx[0] is the least significant byte), same as Bitcoin's
 * uint256/UintToArith256 convention. Explicit endianness. Covered by byte-for-byte vectors.
 */
arith_uint256 RandomXOutputToTargetInteger(const unsigned char (&rx)[32]);

/** Copies the 32 RandomX output bytes into a uint256 (to cache/display the pow_hash). */
uint256 RandomXOutputToUint256(const unsigned char (&rx)[32]);

/**
 * Returns the RandomX SEED (32-byte key) used to mine/validate the block at height `nHeight`.
 * Seed by HEIGHT (not by time):
 *  - heights 0..63: fixed launch constant (params.brisviaInitialSeed).
 *  - heights >= 64: hash of the block at height BrisviaSeedHeight(nHeight), taken from the CANDIDATE BRANCH
 *    via pindexPrev->GetAncestor(...). Since seedHeight <= nHeight-64, the ancestor always exists.
 * `pindexPrev` is the parent of the block to validate (height nHeight-1). For heights 0..63 it may be nullptr.
 */
uint256 BrisviaGetSeedHash(const CBlockIndex* pindexPrev, int nHeight, const Consensus::Params& params);

// ===== Brisvia: contextual verification of the PoW via RandomX =====
/**
 * TRI-STATE result: a `bool` cannot mean both "invalid PoW" and "local RandomX failure" (OOM, the VM could not
 * be created, JIT missing, shutting down). Mixing them would ban peers and contaminate the index according to
 * each node's memory -> possible divergence. INTERNAL_ERROR is NOT INVALID.
 */
enum class PoWCheckResult { VALID, INVALID, INTERNAL_ERROR };

/**
 * Verifies a header's RandomX PoW with its context (height + candidate branch). No commitment.
 * Steps: nBits range -> branch seed (BrisviaGetSeedHash) -> VM by seed (GetVM, LRU) ->
 * RandomX(seed, 80 bytes) -> compare against the target. Leaves the pow_hash in `outPowHash` if passed.
 */
PoWCheckResult CheckRandomXProofOfWorkContextual(const CBlockHeader& block,
                                                 const CBlockIndex* pindexPrev,
                                                 int nHeight,
                                                 const Consensus::Params& params,
                                                 uint256* outPowHash = nullptr);

// ===== Brisvia: central work-verification layer =====
/**
 * LOWER helper: given the expected nBits and the seed ALREADY resolved, validates target range,
 * nBits == expected and RandomX. It does NOT look up ancestors nor compute height. Used both by the normal
 * chain (context from CBlockIndex) and by the headers sync (transient branch context).
 */
PoWCheckResult CheckRandomXHeaderWithResolvedContext(const CBlockHeader& block,
                                                     uint32_t expectedBits,
                                                     const uint256& seedHash,
                                                     const Consensus::Params& params,
                                                     uint256* outPowHash = nullptr);

/**
 * Contextual verification of the header's work from the global index. It ALWAYS checks the nBits range and
 * nBits == GetNextWorkRequired (ASERT). It runs RandomX only if `checkRandomX` (received headers = true;
 * TestBlockValidity of an unmined template = false, but nBits IS checked: turning off the PoW never turns off
 * the nBits check). ContextualCheckBlockHeader must call this function ONLY once.
 */
PoWCheckResult CheckContextualHeaderWork(const CBlockHeader& block,
                                         const CBlockIndex* pindexPrev,
                                         int nHeight,
                                         const Consensus::Params& params,
                                         bool checkRandomX,
                                         uint256* outPowHash = nullptr);

// ===== Brisvia: header verification layer for P2P =====
/**
 * Status of the verification of ONE header, with the CAUSE distinguished (unlike the consensus tri-state).
 * P2P needs to know why it failed (difficulty vs mining vs parent) to score/disconnect peers sensibly and for
 * diagnostics/logs. Refinement requested by the review (separate BAD_BITS from BAD_RANDOMX_POW).
 */
enum class RandomXHeaderStatus {
    VALID,
    BAD_PREV_BLOCK,   // the header does not continue the expected parent
    BAD_BITS,         // nBits out of range, or != the one expected by ASERT (incorrect difficulty)
    BAD_RANDOMX_POW,  // the RandomX hash does not meet the target
    INTERNAL_ERROR,   // LOCAL node failure (VM/OOM): NOT a consensus invalidity, do NOT ban, retry
};

/**
 * Verifies ONE header whose parent is an already-indexed CBlockIndex (pure, isolated layer, without touching
 * P2P/index/chainwork). Checks continuity -> nBits range -> nBits == expected by ASERT (receives the header,
 * in case difficulty depends on nTime) -> BRANCH seed -> RandomX. Reuses GetNextWorkRequired,
 * BrisviaGetSeedHash and CheckRandomXHeaderWithResolvedContext. Requires cs_main (for GetAncestor/tip).
 */
RandomXHeaderStatus CheckRandomXHeaderFromIndexedParent(const CBlockIndex* pindexPrev,
                                                        const CBlockHeader& header,
                                                        const Consensus::Params& params,
                                                        uint256* outPowHash = nullptr);

// ===== Brisvia: TRANSIENT branch context (headers whose parent is not yet indexed) =====
/**
 * Verification context for a BRANCH of headers during the sync, whose parent may not have a CBlockIndex.
 * Starts at an INDEXED block (pindexAnchor, which may belong to a side branch -> it is ALWAYS resolved via
 * pindexAnchor->GetAncestor, NEVER via the active chain) and advances header by header. It keeps:
 *  - the TIP state (source of truth: height, block id, time, nBits) to compute the next difficulty;
 *  - a BOUNDED CIRCULAR window of transient block ids to resolve the historical RandomX seed (which may be up
 *    to BRISVIA_SEED_PERIOD + BRISVIA_SEED_DELAY = 2112 blocks back). The bound prevents a memory DoS by a peer
 *    sending an extremely long branch; the 2112 window is sufficient (provably) because the largest tip->seed
 *    distance occurs at the end of an epoch and equals 2110.
 * Isolated structure + verification + tests. Does NOT touch net_processing/HeadersSyncState yet.
 */
struct RandomXHeaderBranchContext {
    const CBlockIndex* pindexAnchor{nullptr}; // INDEXED base of the branch (may be a side branch)
    int64_t tipHeight{-1};                    // height of the last validated header (or of the anchor if none yet)
    uint256 tipBlockId;                       // block id (SHA256d) of the tip
    int64_t tipTime{0};                       // nTime of the tip
    uint32_t tipBits{0};                      // nBits of the tip
    std::vector<uint256> ring;                // circular window of transient block ids (fixed capacity)
    size_t ringBegin{0};                      // index of the oldest block id in the window
    size_t ringCount{0};                      // valid block ids in the window
    int64_t ringFirstHeight{0};               // height of the block id located at ringBegin
};

/** Result of resolving a header's context on the branch (without running RandomX yet). */
enum class BranchResolveStatus {
    OK,
    BAD_PREV_BLOCK,          // the header does not continue the branch tip
    BAD_BITS,                // nBits out of range or != the expected one (ASERT or constant difficulty)
    MISSING_BRANCH_HISTORY,  // the seed falls outside the window / the ancestors -> internal context error
    BAD_CONTEXT,             // malformed context (no anchor / no ASERT anchor / misconfiguration)
};
struct BranchResolved { int64_t height{0}; uint32_t expectedBits{0}; uint256 seedHash; };

/** Creates a transient context anchored to an indexed block (the real parent of the branch). */
RandomXHeaderBranchContext MakeRandomXHeaderBranchContext(const CBlockIndex* pindexAnchor);

/**
 * Resolves the height + expected difficulty + seed of `header` as a continuation of the branch, WITHOUT running
 * RandomX. Difficulty: replicates GetNextWorkRequired (fPowNoRetargeting -> tip's nBits; otherwise -> ASERT by
 * values, which only needs the tip's height+time). Seed: by height, resolving the historical block id in the
 * transient window or via the anchor's GetAncestor. It does NOT modify the context.
 */
BranchResolveStatus ResolveRandomXBranchContext(const RandomXHeaderBranchContext& ctx, const CBlockHeader& header,
                                                const Consensus::Params& params, BranchResolved& out);

/** Adds an ALREADY-validated header as the new branch tip (advances the tip + inserts its block id in the window). */
void CommitRandomXBranchHeader(RandomXHeaderBranchContext& ctx, const CBlockHeader& header);

/**
 * Validates ONE header as a continuation of the branch (continuity -> difficulty -> seed -> RandomX) and, ONLY
 * if VALID, commits it (advances the context). On any status != VALID the context stays EXACTLY the same.
 * Distinguishes the cause (RandomXHeaderStatus). Reuses ResolveRandomXBranchContext + CheckRandomXHeaderWithResolvedContext.
 */
RandomXHeaderStatus CheckAndAdvanceRandomXHeader(RandomXHeaderBranchContext& ctx, const CBlockHeader& header,
                                                 const Consensus::Params& params, uint256* outPowHash = nullptr);

/** For tests only: how many RandomX VMs were built in total (verifies the single-flight). */
uint64_t BrisviaRandomXBuildCount();

#endif // BITCOIN_POW_H
