// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/randomx_seed.h>
#include <crypto/common.h>
#include <logging.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/check.h>

#include <randomx.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);

    // Brisvia: with RandomX + ASERT anchor, difficulty is adjusted by ASERT-BRVA-v1 from the first block
    // (synthetic parent at the genesis anchor). The classic 2016 retarget (below) stays inert on main/test.
    // Additive: on regtest and on the Bitcoin tree without these params, the original behavior is preserved.
    if (params.fPowRandomX && params.asertAnchorParams.has_value()) {
        // regtest: constant difficulty (ASERT must not react to the instant blocks of the tests).
        if (params.fPowNoRetargeting) return pindexLast->nBits;
        return GetNextASERTWorkRequired(pindexLast, pblock, params);
    }

    unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Only change once per difficulty adjustment interval
    if ((pindexLast->nHeight+1) % params.DifficultyAdjustmentInterval() != 0)
    {
        if (params.fPowAllowMinDifficultyBlocks)
        {
            // Special difficulty rule for testnet:
            // If the new block's timestamp is more than 2* 10 minutes
            // then it MUST be a min-difficulty block.
            if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing*2)
                return nProofOfWorkLimit;
            else
            {
                // Return the last non-special-min-difficulty-rules-block
                const CBlockIndex* pindex = pindexLast;
                while (pindex->pprev && pindex->nHeight % params.DifficultyAdjustmentInterval() != 0 && pindex->nBits == nProofOfWorkLimit)
                    pindex = pindex->pprev;
                return pindex->nBits;
            }
        }
        return pindexLast->nBits;
    }

    // Go back by what we want to be 14 days worth of blocks
    int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
    assert(nHeightFirst >= 0);
    const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
    assert(pindexFirst);

    return CalculateNextWorkRequired(pindexLast, pindexFirst->GetBlockTime(), params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    arith_uint256 bnNew;

    // Special difficulty rule for Testnet4
    if (params.enforce_BIP94) {
        // Here we use the first block of the difficulty period. This way
        // the real difficulty is always preserved in the first block as
        // it is not allowed to use the min-difficulty exception.
        int nHeightFirst = pindexLast->nHeight - (params.DifficultyAdjustmentInterval()-1);
        const CBlockIndex* pindexFirst = pindexLast->GetAncestor(nHeightFirst);
        bnNew.SetCompact(pindexFirst->nBits);
    } else {
        bnNew.SetCompact(pindexLast->nBits);
    }

    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

// Check that on difficulty adjustments, the new difficulty does not increase
// or decrease beyond the permitted limits.
bool PermittedDifficultyTransition(const Consensus::Params& params, int64_t height, uint32_t old_nbits, uint32_t new_nbits)
{
    if (params.fPowAllowMinDifficultyBlocks) return true;

    // Brisvia (audit): ASERT-BRVA-v1 changes nBits EVERY block, so the "factor 4 only at 2016 boundaries"
    // window below would REJECT valid ASERT blocks. With ASERT active, the exact nBits value is validated by
    // comparing against GetNextWorkRequired (ASERT) in ContextualCheckBlockHeader; the 2016 rule is not applied
    // here. Additive: without these params (regtest / base tree), the original logic is preserved.
    if (params.fPowRandomX && params.asertAnchorParams.has_value()) return true;

    if (height % params.DifficultyAdjustmentInterval() == 0) {
        int64_t smallest_timespan = params.nPowTargetTimespan/4;
        int64_t largest_timespan = params.nPowTargetTimespan*4;

        const arith_uint256 pow_limit = UintToArith256(params.powLimit);
        arith_uint256 observed_new_target;
        observed_new_target.SetCompact(new_nbits);

        // Calculate the largest difficulty value possible:
        arith_uint256 largest_difficulty_target;
        largest_difficulty_target.SetCompact(old_nbits);
        largest_difficulty_target *= largest_timespan;
        largest_difficulty_target /= params.nPowTargetTimespan;

        if (largest_difficulty_target > pow_limit) {
            largest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 maximum_new_target;
        maximum_new_target.SetCompact(largest_difficulty_target.GetCompact());
        if (maximum_new_target < observed_new_target) return false;

        // Calculate the smallest difficulty value possible:
        arith_uint256 smallest_difficulty_target;
        smallest_difficulty_target.SetCompact(old_nbits);
        smallest_difficulty_target *= smallest_timespan;
        smallest_difficulty_target /= params.nPowTargetTimespan;

        if (smallest_difficulty_target > pow_limit) {
            smallest_difficulty_target = pow_limit;
        }

        // Round and then compare this new calculated value to what is
        // observed.
        arith_uint256 minimum_new_target;
        minimum_new_target.SetCompact(smallest_difficulty_target.GetCompact());
        if (minimum_new_target > observed_new_target) return false;
    } else if (old_nbits != new_nbits) {
        return false;
    }
    return true;
}

// Bypasses the actual proof of work check during fuzz testing with a simplified validation checking whether
// the most significant bit of the last byte of the hash is set.
bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    if (EnableFuzzDeterminism()) return (hash.data()[31] & 0x80) == 0;
    return CheckProofOfWorkImpl(hash, nBits, params);
}

std::optional<arith_uint256> DeriveTarget(unsigned int nBits, const uint256 pow_limit)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(pow_limit))
        return {};

    return bnTarget;
}

bool CheckProofOfWorkImpl(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    auto bnTarget{DeriveTarget(nBits, params.powLimit)};
    if (!bnTarget) return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}

// ===================== Brisvia: ASERT-BRVA-v1 (difficulty adjustment) =====================
// See the Brisvia PoW design notes section 8 and the chainparams design notes.
// CalculateASERT: aserti3 formula (Bitcoin Cash/ScashX) with arith_uint512 in the intermediates. Validated
// against the Python oracle: 8160 vectors, 0 failures. Additive: GetNextWorkRequired does not call it yet
// (it is wired in once the Brisvia chainparams are present), so it does not change the current consensus.
arith_uint256 CalculateASERT(const arith_uint256& refTarget,
                             const int64_t nPowTargetSpacing,
                             const int64_t nTimeDiff,
                             const int64_t nHeightDiff,
                             const arith_uint256& powLimit,
                             const int64_t nHalfLife) noexcept
{
    assert(refTarget > 0 && refTarget <= powLimit);
    assert(nHeightDiff >= 0);
    assert(llabs(nTimeDiff - nPowTargetSpacing * nHeightDiff) < (1ll << (63 - 16)));

    const int64_t exponent = ((nTimeDiff - nPowTargetSpacing * (nHeightDiff + 1)) * 65536) / nHalfLife;

    static_assert(int64_t(-1) >> 1 == int64_t(-1), "ASERT needs arithmetic shift");

    int64_t shifts = exponent >> 16;
    const auto frac = uint16_t(exponent);
    assert(exponent == (shifts * 65536) + frac);

    const uint32_t factor = 65536 + ((
        + 195766423245049ull * frac
        + 971821376ull * frac * frac
        + 5127ull * frac * frac * frac
        + (1ull << 47)
        ) >> 48);

    arith_uint512 nextTarget512 = arith_uint512::from(refTarget) * factor;
    arith_uint512 powLimit512 = arith_uint512::from(powLimit);

    shifts -= 16;
    if (shifts <= 0) {
        nextTarget512 >>= -shifts;
    } else {
        const auto nextTarget512Shifted = nextTarget512 << shifts;
        if ((nextTarget512Shifted >> shifts) != nextTarget512) {
            nextTarget512 = powLimit512;
        } else {
            nextTarget512 = nextTarget512Shifted;
        }
    }

    if (nextTarget512 > powLimit512) {
        nextTarget512 = powLimit512;
    }
    arith_uint256 nextTarget = arith_uint256::from(nextTarget512);

    if (nextTarget == 0) {
        nextTarget = arith_uint256(1);
    } else if (nextTarget > powLimit) {
        nextTarget = powLimit;
    }
    return nextTarget;
}

// GetNextASERTWorkRequired: next nBits using the Brisvia anchor (genesis with a "synthetic parent").
// Unlike ScashX, it does NOT require pindexPrev->pprev: the synthetic parent lives in anchor.nPrevBlockTime.
// PURE by-VALUES variant: absolute ASERT only needs the parent's HEIGHT and TIME (it does not walk ancestors).
// Used by the transient branch of the headers sync (Phase 3A-2), where the parent may have no CBlockIndex.
uint32_t GetNextASERTWorkRequiredFromValues(int prevHeight, int64_t prevTime,
                                            const CBlockHeader* pblock,
                                            const Consensus::Params& params) noexcept
{
    assert(params.asertAnchorParams.has_value());
    const Consensus::Params::ASERTAnchor& anchor = *params.asertAnchorParams;
    assert(prevHeight >= anchor.nHeight);

    // Special min-difficulty rule (only if enabled: testnet/regtest).
    if (params.fPowAllowMinDifficultyBlocks &&
        (pblock->GetBlockTime() > prevTime + 2 * params.nPowTargetSpacing)) {
        return UintToArith256(params.powLimit).GetCompact();
    }

    const arith_uint256 powLimit = UintToArith256(params.powLimit);
    arith_uint256 refBlockTarget;
    refBlockTarget.SetCompact(anchor.nBits);

    const int64_t nTimeDiff = prevTime - anchor.nPrevBlockTime;
    const int nHeightDiff = prevHeight - anchor.nHeight;

    arith_uint256 nextTarget = CalculateASERT(refBlockTarget, params.nPowTargetSpacing,
                                              nTimeDiff, nHeightDiff, powLimit, params.nASERTHalfLife);
    return nextTarget.GetCompact();
}

uint32_t GetNextASERTWorkRequired(const CBlockIndex* pindexPrev,
                                  const CBlockHeader* pblock,
                                  const Consensus::Params& params) noexcept
{
    assert(pindexPrev != nullptr);
    return GetNextASERTWorkRequiredFromValues(pindexPrev->nHeight, pindexPrev->GetBlockTime(), pblock, params);
}

// ===================== Brisvia: PURE RandomX input/output functions =====================
// See the Brisvia PoW design notes section 2. EXPLICIT endianness (audit).
std::array<unsigned char, BRISVIA_RANDOMX_INPUT_SIZE> SerializeHeaderForRandomX(const CBlockHeader& block)
{
    static_assert(BRISVIA_RANDOMX_INPUT_SIZE == 80, "the Brisvia RandomX header is 80 bytes");
    std::array<unsigned char, BRISVIA_RANDOMX_INPUT_SIZE> out{};
    unsigned char* p = out.data();

    // Field by field, with fixed offsets and explicit little-endian. Matches byte for byte the
    // standard Bitcoin network serialization (CBlockHeader::Serialize), but does not depend on it.
    WriteLE32(p + 0,  static_cast<uint32_t>(block.nVersion));   // version   [0..3]
    std::memcpy(p + 4,  block.hashPrevBlock.begin(), 32);       // prevBlock  [4..35]
    std::memcpy(p + 36, block.hashMerkleRoot.begin(), 32);      // merkleRoot [36..67]
    WriteLE32(p + 68, block.nTime);                            // time      [68..71]
    WriteLE32(p + 72, block.nBits);                            // bits      [72..75]
    WriteLE32(p + 76, block.nNonce);                           // nonce     [76..79]
    return out;
}

arith_uint256 RandomXOutputToTargetInteger(const unsigned char (&rx)[32])
{
    // Fixed rule: little-endian. The 32 bytes are copied into uint256's internal buffer (which is LE) and
    // converted with UintToArith256; thus rx[0] is the least significant byte. Same convention as the
    // rest of the Bitcoin consensus (target/hash). Documented and covered by vectors.
    uint256 h;
    std::memcpy(h.begin(), rx, 32);
    return UintToArith256(h);
}

uint256 RandomXOutputToUint256(const unsigned char (&rx)[32])
{
    uint256 h;
    std::memcpy(h.begin(), rx, 32);
    return h;
}

// RandomX seed by HEIGHT (see the PoW design notes section 3). Deterministic and robust against
// timestamp manipulation and reorgs: the key comes from an earlier block of the CANDIDATE BRANCH.
uint256 BrisviaGetSeedHash(const CBlockIndex* pindexPrev, int nHeight, const Consensus::Params& params)
{
    assert(nHeight >= 0);
    // Initial stretch (heights 0..63): fixed launch key, does not depend on the chain.
    if (BrisviaUsesInitialSeed(nHeight)) {
        return params.brisviaInitialSeed;
    }
    // Height of the block whose hash is the seed. seedHeight <= nHeight-64 <= pindexPrev->nHeight is guaranteed.
    const int seedHeight = BrisviaSeedHeight(nHeight);
    assert(pindexPrev != nullptr);
    assert(seedHeight >= 0 && seedHeight <= pindexPrev->nHeight);
    const CBlockIndex* pindexSeed = pindexPrev->GetAncestor(seedHeight);
    assert(pindexSeed != nullptr);
    return pindexSeed->GetBlockHash();
}

// ===================== Brisvia: RandomX engine (VM) management =====================
// Design notes. Corrections applied from audit and design review:
//  - LRU indexed by SEED (uint256), NEVER by epoch.
//  - REAL single-flight even under eviction: construction is serialized with g_rx_build_mutex (one cache
//    of 256 MiB at a time) and the map is re-checked after acquiring it, so two threads NEVER build the same
//    seed (nor two different seeds at once -> avoids RAM/OOM spikes).
//  - NON-permanent failure: a local failure marks the seed FAILED with backoff (5s/30s/2m/5m); after the
//    cooldown it is retried once. A transient OOM does not disable the epoch until restart.
//  - BuildLightVM is RAII + exception-safe: a bad_alloc does not leak cache/vm nor take down the node.
//  - Sanitized flag ladder [6.3]: light (FULL_MEM/LARGE_PAGES off), no duplicates, and when JIT is dropped
//    SECURE is dropped too. JIT and interpreter produce the SAME hash (RandomX guarantee), it is not consensus.
//  - Evicted objects are destroyed OUTSIDE g_rx_mutex (freeing ~256 MiB does not block the queries).
namespace {

struct RandomXCacheWrapper {
    randomx_cache* cache{nullptr};
    explicit RandomXCacheWrapper(randomx_cache* c) : cache(c) {}
    ~RandomXCacheWrapper() { if (cache) randomx_release_cache(cache); }
    RandomXCacheWrapper(const RandomXCacheWrapper&) = delete;
    RandomXCacheWrapper& operator=(const RandomXCacheWrapper&) = delete;
};
using RandomXCacheRef = std::shared_ptr<RandomXCacheWrapper>;

struct RandomXVMWrapper {
    randomx_vm* vm{nullptr};
    RandomXCacheRef cache;          // keeps the cache alive while the VM exists
    std::mutex hashing_mutex;       // a VM cannot hash from two threads at once
    RandomXVMWrapper(randomx_vm* v, RandomXCacheRef c) : vm(v), cache(std::move(c)) {}
    ~RandomXVMWrapper() { if (vm) randomx_destroy_vm(vm); } // destroy the VM before releasing the cache
    RandomXVMWrapper(const RandomXVMWrapper&) = delete;
    RandomXVMWrapper& operator=(const RandomXVMWrapper&) = delete;
};
using RandomXVMRef = std::shared_ptr<RandomXVMWrapper>;

// Deleters to manage the raw RandomX pointers with RAII during construction (exception-safe).
struct RandomXCacheDeleter { void operator()(randomx_cache* p) const noexcept { if (p) randomx_release_cache(p); } };
struct RandomXVMDeleter    { void operator()(randomx_vm* p)    const noexcept { if (p) randomx_destroy_vm(p); } };
using CacheUnique = std::unique_ptr<randomx_cache, RandomXCacheDeleter>;
using VMUnique    = std::unique_ptr<randomx_vm, RandomXVMDeleter>;

// Per-seed state in the LRU: ONLY successful VMs (failures go to a separate negative cache).
struct RandomXSlot {
    RandomXVMRef vm;
};
// Negative cache SEPARATE from the VM LRU: a failure does not take 256 MiB, must not evict
// a healthy VM, nor lose its backoff when the LRU rotates. It is just metadata, with its own larger limit.
struct RandomXFailure {
    int fail_count{0};
    std::chrono::steady_clock::time_point retry_after{};
};

// uint256 hash for the map: the first 8 bytes are already practically uniform (it is a block hash).
// The map's equality still compares the 256 bits, so this does not affect correctness, only distribution.
struct SeedHasher {
    size_t operator()(const uint256& h) const noexcept { return ReadLE64(h.begin()); }
};

std::mutex g_rx_mutex;            // protects the map + the recency list (short sections)
std::mutex g_rx_build_mutex;      // serializes VM CONSTRUCTION (one cache of 256 MiB at a time)
std::list<uint256> g_rx_lru;      // front = most recent
std::unordered_map<uint256, std::pair<std::shared_ptr<RandomXSlot>, std::list<uint256>::iterator>, SeedHasher> g_rx_slots;
std::unordered_map<uint256, RandomXFailure, SeedHasher> g_rx_failures; // negative cache (lightweight, separate)
// How many seeds (light caches) to keep. Lowered to 2 (current + rival branch) to bound RAM on the ~956 MB
// Oracle seed nodes: with 3 caches steady use is 768 MB and the peak while building a new one reaches ~1 GB
// (the 3 old ones stay resident until the 4th is built), which OOMs a 956 MB box without swap when crossing
// an epoch (every 2048 blocks) or validating a reorg. With 2 the peak is ~768 MB. Not consensus; only
// affects memory/performance. Configurable by arg later (-randomxcache=1..4).
size_t g_rx_capacity{2};
constexpr size_t g_rx_failures_cap{64}; // the negative cache is metadata: its own limit, much larger than the VMs
std::atomic<uint64_t> g_rx_build_count{0}; // instrumentation only, for the single-flight test

// Leaves the flags for the VALIDATOR: light mode (FULL_MEM off, that belongs to the miner) and LARGE_PAGES off
// (avoids permission failures). Keeps JIT/HARD_AES/ARGON2/SECURE if the OS offers them.
randomx_flags SanitizeValidatorFlags(randomx_flags f)
{
    // Test/portability hook: force the portable interpreter (no JIT) if the operator requests it via env.
    // Main use: run sanitizers (TSan/ASan) without the JIT, whose runtime-generated code is neither observable
    // nor compatible with the instrumentation. Inactive by default: does not change the node's normal behavior.
    if (std::getenv("BRISVIA_RX_FORCE_INTERPRETER") != nullptr) return RANDOMX_FLAG_DEFAULT;
    int v = static_cast<int>(f);
    v &= ~static_cast<int>(RANDOMX_FLAG_FULL_MEM);
    v &= ~static_cast<int>(RANDOMX_FLAG_LARGE_PAGES);
    return static_cast<randomx_flags>(v);
}

std::chrono::seconds BackoffFor(int fail_count)
{
    switch (fail_count) {
        case 1:  return std::chrono::seconds{5};
        case 2:  return std::chrono::seconds{30};
        case 3:  return std::chrono::seconds{120};
        default: return std::chrono::seconds{300};
    }
}

// Builds a light VM for `seedHash`. RAII + exception-safe. Returns null if it could not (local failure).
RandomXVMRef BuildLightVM(const uint256& seedHash)
{
    g_rx_build_count.fetch_add(1, std::memory_order_relaxed);
    const randomx_flags base = SanitizeValidatorFlags(randomx_get_flags());
    const int b = static_cast<int>(base);
    // When JIT is dropped SECURE is dropped too (it only makes sense with JIT).
    const randomx_flags noJit = static_cast<randomx_flags>(
        b & ~static_cast<int>(RANDOMX_FLAG_JIT) & ~static_cast<int>(RANDOMX_FLAG_SECURE));
    const randomx_flags candidates[] = { base, noJit, RANDOMX_FLAG_DEFAULT };

    try {
        randomx_flags tried[3] = {};
        int nTried = 0;
        for (const randomx_flags f : candidates) {
            bool dup = false;                                  // do not repeat expensive attempts if they match
            for (int i = 0; i < nTried; ++i) if (tried[i] == f) { dup = true; break; }
            if (dup) continue;
            tried[nTried++] = f;

            CacheUnique cache{randomx_alloc_cache(f)};
            if (!cache) continue;
            randomx_init_cache(cache.get(), seedHash.begin(), seedHash.size()); // key = 32 bytes
            VMUnique vm{randomx_create_vm(f, cache.get(), nullptr)};            // light: cache, no dataset
            if (!vm) continue;
            // Release the unique_ptrs ONLY after a definitive owner exists (avoids a leak if make_shared
            // throws bad_alloc).
            auto cacheRef = std::make_shared<RandomXCacheWrapper>(cache.get());
            cache.release();
            auto vmRef = std::make_shared<RandomXVMWrapper>(vm.get(), cacheRef);
            vm.release();
            return vmRef;
        }
    } catch (const std::exception& e) {
        LogPrintf("Brisvia: exception creating the RandomX VM (%s)\n", e.what());
        return nullptr;
    } catch (...) {
        LogPrintf("Brisvia: unknown exception creating the RandomX VM\n");
        return nullptr;
    }
    LogPrintf("Brisvia: could not create the RandomX VM for seed %s\n", seedHash.GetHex());
    return nullptr;
}

// Marks the seed as recent in the LRU (assumes g_rx_mutex held). Returns the slot or nullptr if it does not exist.
std::shared_ptr<RandomXSlot> TouchSlot(const uint256& seedHash)
{
    auto it = g_rx_slots.find(seedHash);
    if (it == g_rx_slots.end()) return nullptr;
    // splice moves the existing node to the front WITHOUT allocating memory (no-throw): it does not leave
    // dangling iterators as erase+push_front would if push_front threw.
    g_rx_lru.splice(g_rx_lru.begin(), g_rx_lru, it->second.second);
    it->second.second = g_rx_lru.begin();
    return it->second.first;
}

// Queries the caches (requires g_rx_mutex held). Returns the VM if it is ready. Sets `inCooldown` to true
// if the seed is in failure cooldown (=> INTERNAL_ERROR without rebuilding). nullptr + !inCooldown => build.
RandomXVMRef LookupLocked(const uint256& seedHash, bool& inCooldown)
{
    inCooldown = false;
    if (auto slot = TouchSlot(seedHash)) {
        if (slot->vm) return slot->vm;
    }
    auto fit = g_rx_failures.find(seedHash);
    if (fit != g_rx_failures.end() && std::chrono::steady_clock::now() < fit->second.retry_after) {
        inCooldown = true;
    }
    return nullptr;
}

// Returns the VM for `seedHash` (LRU by seed), building it only once. Null = local failure.
RandomXVMRef GetBrisviaVM(const uint256& seedHash)
{
    // 1) Fast path.
    {
        std::lock_guard<std::mutex> lk(g_rx_mutex);
        bool cooldown = false;
        if (auto vm = LookupLocked(seedHash, cooldown)) return vm;
        if (cooldown) return nullptr;
    }

    // 2) Serialize the construction (one at a time): single-flight even if the slot was evicted.
    std::lock_guard<std::mutex> build(g_rx_build_mutex);
    {
        std::lock_guard<std::mutex> lk(g_rx_mutex); // re-check after waiting on the build mutex
        bool cooldown = false;
        if (auto vm = LookupLocked(seedHash, cooldown)) return vm;
        if (cooldown) return nullptr;
    }

    // 3) Build outside g_rx_mutex (expensive operation), but under g_rx_build_mutex (one at a time).
    RandomXVMRef vm = BuildLightVM(seedHash);

    // 4) Store. A success goes to the LRU (at most ONE victim, destroyed outside the lock). A failure goes ONLY to
    //    the negative cache: it does not evict healthy VMs nor lose the backoff when the LRU rotates.
    std::shared_ptr<RandomXSlot> evicted;
    {
        std::lock_guard<std::mutex> lk(g_rx_mutex);
        if (vm) {
            g_rx_failures.erase(seedHash); // success: clear any previous failure mark
            if (auto slot = TouchSlot(seedHash)) {
                slot->vm = vm;
            } else {
                auto newSlot = std::make_shared<RandomXSlot>();
                newSlot->vm = vm;
                g_rx_lru.push_front(seedHash);
                try {
                    g_rx_slots.emplace(seedHash, std::make_pair(newSlot, g_rx_lru.begin()));
                } catch (...) {
                    g_rx_lru.pop_front(); // rollback: do not leave an orphan node. The VM is still returned (uncached).
                }
            }
            if (g_rx_slots.size() > g_rx_capacity) {
                const uint256 old = g_rx_lru.back();
                auto oit = g_rx_slots.find(old);
                assert(oit != g_rx_slots.end()); // invariant: every LRU node exists in the map
                if (oit != g_rx_slots.end()) { evicted = oit->second.first; g_rx_slots.erase(oit); }
                g_rx_lru.pop_back();
            }
        } else {
            RandomXFailure& f = g_rx_failures[seedHash];
            f.fail_count = std::min(f.fail_count + 1, 4); // saturate: the max backoff is already 5 min
            f.retry_after = std::chrono::steady_clock::now() + BackoffFor(f.fail_count);
            if (g_rx_failures.size() > g_rx_failures_cap) g_rx_failures.clear(); // metadata: simple purge if it grows
        }
    }
    evicted.reset(); // here the evicted cache is freed, now without g_rx_mutex
    return vm;
}

} // namespace

uint64_t BrisviaRandomXBuildCount() { return g_rx_build_count.load(std::memory_order_relaxed); }

// LOWER helper (Phase 1): context ALREADY resolved (expectedBits + seedHash). Validates target range,
// nBits == expected and RandomX. Does not look up ancestors nor compute height. Reusable by the normal chain and by
// the headers sync with transient context.
PoWCheckResult CheckRandomXHeaderWithResolvedContext(const CBlockHeader& block, uint32_t expectedBits,
                                                     const uint256& seedHash, const Consensus::Params& params,
                                                     uint256* outPowHash)
{
    if (outPowHash) outPowHash->SetNull();
    const std::optional<arith_uint256> bnTarget = DeriveTarget(block.nBits, params.powLimit);
    if (!bnTarget) return PoWCheckResult::INVALID;              // nBits out of range
    if (block.nBits != expectedBits) return PoWCheckResult::INVALID; // difficulty != the required one

    RandomXVMRef vm = GetBrisviaVM(seedHash);
    if (!vm) return PoWCheckResult::INTERNAL_ERROR;             // local failure: NOT INVALID, do NOT ban

    const std::array<unsigned char, BRISVIA_RANDOMX_INPUT_SIZE> in = SerializeHeaderForRandomX(block);
    unsigned char rx[RANDOMX_HASH_SIZE];
    {
        std::lock_guard<std::mutex> lk(vm->hashing_mutex);
        randomx_calculate_hash(vm->vm, in.data(), in.size(), rx);
    }
    if (outPowHash) *outPowHash = RandomXOutputToUint256(rx); // pow_hash: real useful data even if it does not suffice
    return (RandomXOutputToTargetInteger(rx) <= *bnTarget) ? PoWCheckResult::VALID : PoWCheckResult::INVALID;
}

// Contextual work verification from the global index (Phase 1). Resolves the expected nBits (ASERT) and
// the seed, and delegates to the lower helper. With checkRandomX=false it validates only nBits (no PoW).
PoWCheckResult CheckContextualHeaderWork(const CBlockHeader& block, const CBlockIndex* pindexPrev, int nHeight,
                                         const Consensus::Params& params, bool checkRandomX, uint256* outPowHash)
{
    if (outPowHash) outPowHash->SetNull();

    // Only for chains with RandomX PoW. Without fPowRandomX it is a node misconfiguration -> INTERNAL_ERROR.
    if (!params.fPowRandomX) return PoWCheckResult::INTERNAL_ERROR;

    // Context consistency. Inconsistent caller = LOCAL ERROR (not a malicious block): do not ban.
    if (nHeight < 0) return PoWCheckResult::INTERNAL_ERROR;
    if (nHeight == 0 && pindexPrev != nullptr) return PoWCheckResult::INTERNAL_ERROR;
    if (nHeight > 0 && pindexPrev == nullptr) return PoWCheckResult::INTERNAL_ERROR;
    if (pindexPrev != nullptr && pindexPrev->nHeight + 1 != nHeight) return PoWCheckResult::INTERNAL_ERROR;

    // Expected nBits: ASERT for every block > genesis; genesis uses its own nBits (set by chainparams).
    uint32_t expectedBits;
    if (nHeight > 0) {
        if (!params.asertAnchorParams.has_value()) return PoWCheckResult::INTERNAL_ERROR;
        expectedBits = GetNextWorkRequired(pindexPrev, &block, params);
    } else {
        expectedBits = block.nBits;
    }

    if (!checkRandomX) {
        // Unmined template / TestBlockValidity: nBits is validated (range + expected) but NOT the PoW.
        if (!DeriveTarget(block.nBits, params.powLimit)) return PoWCheckResult::INVALID;
        if (block.nBits != expectedBits) return PoWCheckResult::INVALID;
        return PoWCheckResult::VALID;
    }

    const uint256 seedHash = BrisviaGetSeedHash(pindexPrev, nHeight, params);
    return CheckRandomXHeaderWithResolvedContext(block, expectedBits, seedHash, params, outPowHash);
}

// Compatibility: full contextual verification with RandomX (== CheckContextualHeaderWork with checkRandomX=true).
PoWCheckResult CheckRandomXProofOfWorkContextual(const CBlockHeader& block, const CBlockIndex* pindexPrev,
                                                 int nHeight, const Consensus::Params& params, uint256* outPowHash)
{
    return CheckContextualHeaderWork(block, pindexPrev, nHeight, params, /*checkRandomX=*/true, outPowHash);
}

// ===== Brisvia Phase 3A-1: PURE layer to verify ONE header on a branch (parent ALREADY indexed). =====
// Distinguishes the CAUSE of the failure (parent / difficulty / mining / local error) for the future P2P (score peers,
// logs, diagnostics). Isolated: does NOT touch net_processing / index / chainwork / HeadersSyncState. Reuses
// GetNextWorkRequired (expected difficulty, takes the header in case it depends on nTime), BrisviaGetSeedHash
// (seed of the BRANCH via the parent's ancestors) and the lower helper CheckRandomXHeaderWithResolvedContext.
RandomXHeaderStatus CheckRandomXHeaderFromIndexedParent(const CBlockIndex* pindexPrev, const CBlockHeader& header,
                                                        const Consensus::Params& params, uint256* outPowHash)
{
    if (outPowHash) outPowHash->SetNull();

    // Node misconfiguration (not a RandomX chain): local failure, not a consensus invalidity.
    if (!params.fPowRandomX) return RandomXHeaderStatus::INTERNAL_ERROR;
    // This layer requires an indexed parent (height >= 1). The genesis is not verified here.
    if (pindexPrev == nullptr) return RandomXHeaderStatus::BAD_PREV_BLOCK;
    // Continuity: the header must chain EXACTLY to the given parent.
    if (header.hashPrevBlock != pindexPrev->GetBlockHash()) return RandomXHeaderStatus::BAD_PREV_BLOCK;

    const int nHeight = pindexPrev->nHeight + 1;

    // Expected difficulty via ASERT. Requires the anchor; without it, it is a misconfiguration -> local failure.
    if (!params.asertAnchorParams.has_value()) return RandomXHeaderStatus::INTERNAL_ERROR;
    if (!DeriveTarget(header.nBits, params.powLimit)) return RandomXHeaderStatus::BAD_BITS; // nBits out of range
    const uint32_t expectedBits = GetNextWorkRequired(pindexPrev, &header, params);
    if (header.nBits != expectedBits) return RandomXHeaderStatus::BAD_BITS;                 // wrong difficulty

    // Seed of the BRANCH (ancestors of the indexed parent) + RandomX. We pass header.nBits as expected: we
    // already validated it above, so the only INVALID the helper can return is the RandomX hash not meeting it.
    const uint256 seedHash = BrisviaGetSeedHash(pindexPrev, nHeight, params);
    const PoWCheckResult rx = CheckRandomXHeaderWithResolvedContext(header, header.nBits, seedHash, params, outPowHash);
    switch (rx) {
        case PoWCheckResult::VALID:          return RandomXHeaderStatus::VALID;
        case PoWCheckResult::INTERNAL_ERROR: return RandomXHeaderStatus::INTERNAL_ERROR;
        case PoWCheckResult::INVALID:        return RandomXHeaderStatus::BAD_RANDOMX_POW; // nBits already OK -> only the PoW
    }
    return RandomXHeaderStatus::INTERNAL_ERROR; // unreachable (exhaustive switch)
}

// ===== Brisvia Phase 3A-2: TRANSIENT branch context (parent not yet indexed) =====
// Circular window capacity: the largest tip->seed distance happens at the end of an epoch and equals
// (BRISVIA_SEED_PERIOD + BRISVIA_SEED_DELAY - 2) = 2110; +1 to include both ends => 2111 is enough. We use
// period+delay = 2112 (clean boundary, a single extra uint256). Fixed anti-DoS memory bound.
static constexpr size_t kBrisviaBranchWindow = size_t(BRISVIA_SEED_PERIOD) + size_t(BRISVIA_SEED_DELAY);

RandomXHeaderBranchContext MakeRandomXHeaderBranchContext(const CBlockIndex* pindexAnchor)
{
    RandomXHeaderBranchContext ctx;
    ctx.pindexAnchor = pindexAnchor;
    if (pindexAnchor != nullptr) {
        ctx.tipHeight  = pindexAnchor->nHeight;
        ctx.tipBlockId = pindexAnchor->GetBlockHash();
        ctx.tipTime    = pindexAnchor->GetBlockTime();
        ctx.tipBits    = pindexAnchor->nBits;
        ctx.ringFirstHeight = int64_t(pindexAnchor->nHeight) + 1;
    }
    ctx.ring.assign(kBrisviaBranchWindow, uint256()); // single reservation: no allocations after the commit
    return ctx;
}

// Block id of the block at height h (h <= tip). In the indexed part it uses GetAncestor from the ANCHOR (correct
// branch, never the active chain); in the transient part, the circular window. false if it is not resolvable (out of
// window or range) -> the caller treats it as an internal context error, not as the peer's fault.
static bool BranchBlockIdAtHeight(const RandomXHeaderBranchContext& ctx, int64_t h, uint256& out)
{
    if (ctx.pindexAnchor == nullptr) return false;
    if (h < 0 || h > ctx.tipHeight) return false;
    const int64_t anchorH = ctx.pindexAnchor->nHeight;
    if (h <= anchorH) {
        const CBlockIndex* p = ctx.pindexAnchor->GetAncestor(int(h));
        if (p == nullptr) return false;
        out = p->GetBlockHash();
        return true;
    }
    // Transient part: live range [ringFirstHeight, ringFirstHeight + ringCount - 1]. If h fell below it,
    // it was dropped from the window (should not happen with a contiguous branch and window 2112, but it is reported explicitly).
    if (h < ctx.ringFirstHeight) return false;
    const int64_t offset = h - ctx.ringFirstHeight;
    if (offset < 0 || size_t(offset) >= ctx.ringCount) return false;
    const size_t idx = (ctx.ringBegin + size_t(offset)) % ctx.ring.size();
    out = ctx.ring[idx];
    return true;
}

BranchResolveStatus ResolveRandomXBranchContext(const RandomXHeaderBranchContext& ctx, const CBlockHeader& header,
                                                const Consensus::Params& params, BranchResolved& out)
{
    if (!params.fPowRandomX) return BranchResolveStatus::BAD_CONTEXT;
    if (ctx.pindexAnchor == nullptr) return BranchResolveStatus::BAD_CONTEXT;

    // Continuity with the branch tip.
    if (header.hashPrevBlock != ctx.tipBlockId) return BranchResolveStatus::BAD_PREV_BLOCK;

    const int64_t height = ctx.tipHeight + 1;
    out.height = height;

    // Expected difficulty: replicates GetNextWorkRequired (dispatch by network configuration).
    if (!DeriveTarget(header.nBits, params.powLimit)) return BranchResolveStatus::BAD_BITS; // out of range
    uint32_t expectedBits;
    if (params.fPowNoRetargeting) {
        expectedBits = ctx.tipBits;   // constant difficulty (regtest): equal to the tip's nBits
    } else {
        if (!params.asertAnchorParams.has_value()) return BranchResolveStatus::BAD_CONTEXT;
        expectedBits = GetNextASERTWorkRequiredFromValues(ctx.tipHeight, ctx.tipTime, &header, params);
    }
    if (header.nBits != expectedBits) return BranchResolveStatus::BAD_BITS;
    out.expectedBits = expectedBits;

    // Seed by HEIGHT (same rule as BrisviaGetSeedHash, but resolving the hash over the transient branch).
    if (BrisviaUsesInitialSeed(int(height))) {
        out.seedHash = params.brisviaInitialSeed;
    } else {
        const int seedHeight = BrisviaSeedHeight(int(height)); // <= height-64 <= tipHeight => already exists in the branch
        uint256 seed;
        if (!BranchBlockIdAtHeight(ctx, seedHeight, seed)) return BranchResolveStatus::MISSING_BRANCH_HISTORY;
        out.seedHash = seed;
    }
    return BranchResolveStatus::OK;
}

void CommitRandomXBranchHeader(RandomXHeaderBranchContext& ctx, const CBlockHeader& header)
{
    const int64_t height = ctx.tipHeight + 1;
    const uint256 blockId = header.GetHash();

    // Advance the tip state (source of truth).
    ctx.tipHeight  = height;
    ctx.tipBlockId = blockId;
    ctx.tipTime    = header.nTime;
    ctx.tipBits    = header.nBits;

    // Insert the block id into the circular window (no allocations: the memory is already reserved).
    if (ctx.ring.empty()) return;
    if (ctx.ringCount < ctx.ring.size()) {
        if (ctx.ringCount == 0) ctx.ringFirstHeight = height;
        const size_t idx = (ctx.ringBegin + ctx.ringCount) % ctx.ring.size();
        ctx.ring[idx] = blockId;
        ctx.ringCount++;
    } else {
        // Window full: overwrite the oldest and advance the front (discards height ringFirstHeight).
        ctx.ring[ctx.ringBegin] = blockId;
        ctx.ringBegin = (ctx.ringBegin + 1) % ctx.ring.size();
        ctx.ringFirstHeight++;
    }
}

RandomXHeaderStatus CheckAndAdvanceRandomXHeader(RandomXHeaderBranchContext& ctx, const CBlockHeader& header,
                                                 const Consensus::Params& params, uint256* outPowHash)
{
    if (outPowHash) outPowHash->SetNull();

    // 1..4) Resolve everything into locals, WITHOUT touching the context.
    BranchResolved r;
    switch (ResolveRandomXBranchContext(ctx, header, params, r)) {
        case BranchResolveStatus::OK:                     break;
        case BranchResolveStatus::BAD_PREV_BLOCK:         return RandomXHeaderStatus::BAD_PREV_BLOCK;
        case BranchResolveStatus::BAD_BITS:               return RandomXHeaderStatus::BAD_BITS;
        case BranchResolveStatus::MISSING_BRANCH_HISTORY: return RandomXHeaderStatus::INTERNAL_ERROR;
        case BranchResolveStatus::BAD_CONTEXT:            return RandomXHeaderStatus::INTERNAL_ERROR;
    }

    // 5) RandomX with the resolved context (nBits already validated -> INVALID can only be the PoW).
    uint256 pow;
    const PoWCheckResult rx = CheckRandomXHeaderWithResolvedContext(header, r.expectedBits, r.seedHash, params, &pow);
    if (rx == PoWCheckResult::INTERNAL_ERROR) return RandomXHeaderStatus::INTERNAL_ERROR;
    if (rx == PoWCheckResult::INVALID)        return RandomXHeaderStatus::BAD_RANDOMX_POW;

    // 6) Only if VALID: commit (up to here the context stayed intact).
    if (outPowHash) *outPowHash = pow;
    CommitRandomXBranchHeader(ctx, header);
    return RandomXHeaderStatus::VALID;
}
