// Brisvia - integration test of the RandomX engine inside the Bitcoin Core tree.
// Validates the wrapper against the OFFICIAL RandomX vector (tevador). Using the wrapper forces the
// effective link of librandomx.a into test_bitcoin (no longer dead code) and confirms the engine produces
// the correct hash inside the node binary. Basis for the project's own vectors in Commit 9.
#include <crypto/randomx_hash.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/amount.h>
#include <consensus/brisvia_emission.h>
#include <consensus/brisvia_genesis.h>
#include <consensus/merkle.h>
#include <consensus/randomx_seed.h>
#include <consensus/params.h>
#include <kernel/chainparams.h>
#include <pow.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <streams.h>
#include <uint256.h>
#include <util/strencodings.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

BOOST_AUTO_TEST_SUITE(brisvia_randomx_tests)

// Official RandomX vector: key="test key 000", input="This is a test" (light mode).
BOOST_AUTO_TEST_CASE(official_vector)
{
    const std::string key = "test key 000";
    const std::string input = "This is a test";
    unsigned char out[BRISVIA_RANDOMX_HASH_SIZE];

    bool ok = brisvia_randomx_hash(
        reinterpret_cast<const unsigned char*>(key.data()), key.size(),
        reinterpret_cast<const unsigned char*>(input.data()), input.size(),
        out);

    BOOST_REQUIRE(ok);
    const std::string got = HexStr(std::vector<unsigned char>(out, out + BRISVIA_RANDOMX_HASH_SIZE));
    BOOST_CHECK_EQUAL(got, "639183aae1bf4c9a35884cb46b09cad9175f04efd7684e7262a0ac1c2f0b4e3f");
}

// SerializeHeaderForRandomX must yield 80 bytes IDENTICAL to Bitcoin's standard network serialization,
// with the nonce at offset 76 (little-endian). Hardens the canonical input to RandomX (audit).
BOOST_AUTO_TEST_CASE(header_serialization_matches_canonical)
{
    CBlockHeader header;
    header.nVersion = 0x20000000;
    std::memset(header.hashPrevBlock.begin(), 0xAB, 32);
    std::memset(header.hashMerkleRoot.begin(), 0xCD, 32);
    header.nTime  = 0x11223344;
    header.nBits  = 0x1e0fffff;
    header.nNonce = 0x04030201;

    const auto rx_in = SerializeHeaderForRandomX(header);
    BOOST_REQUIRE_EQUAL(rx_in.size(), 80u);

    // Byte-for-byte identical to the tree's canonical serialization.
    DataStream ss;
    ss << header;
    BOOST_REQUIRE_EQUAL(ss.size(), 80u);
    const auto* canon = reinterpret_cast<const unsigned char*>(ss.data());
    BOOST_CHECK(std::equal(rx_in.begin(), rx_in.end(), canon));

    // Explicit offsets.
    BOOST_CHECK_EQUAL(rx_in[0], 0x00);  // version LE
    BOOST_CHECK_EQUAL(rx_in[3], 0x20);
    BOOST_CHECK_EQUAL(rx_in[4], 0xAB);  // hashPrevBlock
    BOOST_CHECK_EQUAL(rx_in[36], 0xCD); // hashMerkleRoot
    BOOST_CHECK_EQUAL(rx_in[68], 0x44); // nTime LE
    BOOST_CHECK_EQUAL(rx_in[72], 0xff); // nBits LE
    BOOST_CHECK_EQUAL(rx_in[76], 0x01); // nonce at offset 76, LE
    BOOST_CHECK_EQUAL(rx_in[77], 0x02);
    BOOST_CHECK_EQUAL(rx_in[78], 0x03);
    BOOST_CHECK_EQUAL(rx_in[79], 0x04);
}

// RandomXOutputToTargetInteger interprets the 32 bytes as little-endian (rx[0] = least significant byte).
BOOST_AUTO_TEST_CASE(randomx_output_endianness)
{
    unsigned char rx_one[32] = {0};
    rx_one[0] = 0x01;
    BOOST_CHECK(RandomXOutputToTargetInteger(rx_one) == arith_uint256(1));

    unsigned char rx_high[32] = {0};
    rx_high[31] = 0x01; // most significant byte
    BOOST_CHECK(RandomXOutputToTargetInteger(rx_high) == (arith_uint256(1) << 248));

    // RandomXOutputToUint256 copies the bytes as-is (internal LE buffer).
    unsigned char rx_seq[32];
    for (int i = 0; i < 32; ++i) rx_seq[i] = static_cast<unsigned char>(i);
    const uint256 h = RandomXOutputToUint256(rx_seq);
    BOOST_CHECK(std::memcmp(h.begin(), rx_seq, 32) == 0);
}

// BrisviaGetSeedHash: seed by height over a synthetic chain. Covers the fixed initial stretch,
// height 64 (first use of a block hash) and the first rotation at 2112 (64 + 2048).
BOOST_AUTO_TEST_CASE(seed_hash_by_height)
{
    Consensus::Params params{};
    params.brisviaInitialSeed = ArithToUint256(arith_uint256(0x1234)); // recognizable fixed seed

    const int N = 2113; // heights 0..2112
    std::vector<uint256> hashes(N);
    std::vector<CBlockIndex> idx(N);
    for (int i = 0; i < N; ++i) {
        hashes[i] = ArithToUint256(arith_uint256(1000 + i)); // distinct, nonzero hash per block
        idx[i].nHeight = i;
        idx[i].pprev = (i == 0) ? nullptr : &idx[i - 1];
        idx[i].phashBlock = &hashes[i];
    }

    // Initial stretch 0..63: always the fixed launch seed (does not depend on the chain).
    BOOST_CHECK(BrisviaGetSeedHash(nullptr, 0, params) == params.brisviaInitialSeed);
    BOOST_CHECK(BrisviaGetSeedHash(&idx[62], 63, params) == params.brisviaInitialSeed);

    // Height 64: first time the key is a block hash. seedHeight = 0.
    BOOST_CHECK(BrisviaGetSeedHash(&idx[63], 64, params) == hashes[0]);
    // 65..2111 keep using block 0.
    BOOST_CHECK(BrisviaGetSeedHash(&idx[99], 100, params) == hashes[0]);
    BOOST_CHECK(BrisviaGetSeedHash(&idx[2110], 2111, params) == hashes[0]);
    // Height 2112 = 64 + 2048: first real rotation, seedHeight = 2048.
    BOOST_CHECK(BrisviaGetSeedHash(&idx[2111], 2112, params) == hashes[2048]);
}

// "On-schedule" times for the test chains: t0 and one block every 120 s. With the ASERT anchor at
// nPrevBlockTime = kChainT0 - 120, this keeps the expected target constant == anchor.
static constexpr int64_t kChainT0 = 1700000000;
static constexpr int64_t kChainSpacing = 120;

// Builds a synthetic chain idx[0..N-1] with distinct hashes, linked parents and on-schedule times.
static void BuildChain(int N, std::vector<uint256>& hashes, std::vector<CBlockIndex>& idx)
{
    hashes = std::vector<uint256>(N);      // in-place construction (no copy of CBlockIndex required)
    idx = std::vector<CBlockIndex>(N);
    for (int i = 0; i < N; ++i) {
        hashes[i] = ArithToUint256(arith_uint256(1000 + i));
        idx[i].nHeight = i;
        idx[i].pprev = (i == 0) ? nullptr : &idx[i - 1];
        idx[i].phashBlock = &hashes[i];
        idx[i].nTime = static_cast<uint32_t>(kChainT0 + int64_t(i) * kChainSpacing);
    }
}

// Configures params for active RandomX PoW with an "on-schedule" ASERT anchor and easy target (to mine fast).
static void SetupRandomXParams(Consensus::Params& params, uint32_t initialSeedWord, uint32_t easyBits)
{
    params.fPowRandomX = true;
    params.fPowAllowMinDifficultyBlocks = false;
    params.nPowTargetSpacing = kChainSpacing;
    params.nASERTHalfLife = 21600;
    { arith_uint256 t; t.SetCompact(easyBits); params.powLimit = ArithToUint256(t); }
    params.brisviaInitialSeed = ArithToUint256(arith_uint256(initialSeedWord));
    params.asertAnchorParams = Consensus::Params::ASERTAnchor{0, easyBits, kChainT0 - kChainSpacing};
}

// Contextual end-to-end PoW verification with RandomX active + ASERT anchor (hardened gate).
// On schedule, the expected nBits == anchor (easyBits), so the valid header passes the gate and mines fast.
BOOST_AUTO_TEST_CASE(contextual_pow_end_to_end)
{
    const uint32_t easyBits = 0x207fffff; // target ~2^255
    Consensus::Params params{};
    SetupRandomXParams(params, 0xBEEF, easyBits);

    std::vector<uint256> hashes; std::vector<CBlockIndex> idx;
    BuildChain(11, hashes, idx); // heights 0..10, on schedule

    CBlockHeader header;
    header.nVersion = 1;
    header.nTime = static_cast<uint32_t>(kChainT0 + 10 * kChainSpacing);
    header.nBits = easyBits; // == nBits expected by ASERT on schedule

    // Find one VALID and one INVALID nonce (both with nBits==expected -> both reach RandomX and exercise the
    // two branches of hash<=target). With target ~2^255, ~50% falls on each side.
    uint256 powHashValid; uint32_t validNonce = 0;
    bool foundValid = false, foundInvalid = false;
    for (uint32_t nonce = 0; nonce < 256 && !(foundValid && foundInvalid); ++nonce) {
        header.nNonce = nonce;
        uint256 ph;
        const auto r = CheckRandomXProofOfWorkContextual(header, &idx[9], 10, params, &ph);
        if (r == PoWCheckResult::VALID && !foundValid) { foundValid = true; powHashValid = ph; validNonce = nonce; }
        else if (r == PoWCheckResult::INVALID && !foundInvalid) { foundInvalid = true; }
    }
    BOOST_REQUIRE(foundValid);
    BOOST_REQUIRE(foundInvalid);

    // The validator's pow_hash (valid case) == the standalone engine's hash (same seed + same 80 bytes).
    header.nNonce = validNonce;
    const auto in = SerializeHeaderForRandomX(header);
    unsigned char rx_ref[BRISVIA_RANDOMX_HASH_SIZE];
    BOOST_REQUIRE(brisvia_randomx_hash(params.brisviaInitialSeed.begin(), 32, in.data(), in.size(), rx_ref));
    BOOST_CHECK(powHashValid == RandomXOutputToUint256(rx_ref));

    // nBits != the one expected by ASERT -> INVALID at the gate (before RandomX).
    header.nNonce = validNonce;
    header.nBits = 0x1e0fffff; // valid in range but != the expected easyBits
    BOOST_CHECK(CheckRandomXProofOfWorkContextual(header, &idx[9], 10, params, nullptr) == PoWCheckResult::INVALID);

    // Inconsistent context (parent at the wrong height) -> INTERNAL_ERROR.
    header.nBits = easyBits;
    BOOST_CHECK(CheckRandomXProofOfWorkContextual(header, &idx[9], 50, params, nullptr) == PoWCheckResult::INTERNAL_ERROR);

    // Bad config (no fPowRandomX) -> INTERNAL_ERROR, not a reduced validation.
    Consensus::Params bad = params;
    bad.fPowRandomX = false;
    BOOST_CHECK(CheckRandomXProofOfWorkContextual(header, &idx[9], 10, bad, nullptr) == PoWCheckResult::INTERNAL_ERROR);
}

// Single-flight: 20 threads request the SAME seed at once -> a single VM is built.
BOOST_AUTO_TEST_CASE(contextual_pow_single_flight)
{
    const uint32_t easyBits = 0x207fffff;
    Consensus::Params params{};
    SetupRandomXParams(params, 0xCAFE, easyBits); // fresh seed (different from other tests)

    std::vector<uint256> hashes; std::vector<CBlockIndex> idx;
    BuildChain(11, hashes, idx);

    CBlockHeader header;
    header.nVersion = 1;
    header.nTime = static_cast<uint32_t>(kChainT0 + 10 * kChainSpacing);
    header.nBits = easyBits;

    const uint64_t before = BrisviaRandomXBuildCount();

    std::vector<std::thread> ths;
    for (int i = 0; i < 20; ++i) {
        ths.emplace_back([&]() {
            CheckRandomXProofOfWorkContextual(header, &idx[9], 10, params, nullptr);
        });
    }
    for (auto& t : ths) t.join();

    // Exactly one build for the shared seed, despite the 20 concurrent requests.
    BOOST_CHECK_EQUAL(BrisviaRandomXBuildCount() - before, 1u);
}

// ASERT end-to-end: validates the HOOKUP (GetNextWorkRequired -> ASERT) with fPowRandomX + synthetic anchor.
// The CalculateASERT formula is already validated (8160 vectors); here we test that the flow uses it correctly.
BOOST_AUTO_TEST_CASE(asert_hookup_end_to_end)
{
    Consensus::Params params{};
    params.fPowRandomX = true;
    params.fPowAllowMinDifficultyBlocks = false;
    params.nPowTargetSpacing = 120;
    params.nASERTHalfLife = 21600;
    { arith_uint256 pl; pl.SetCompact(0x1e3fffff); params.powLimit = ArithToUint256(pl); } // ~2^238
    const uint32_t anchorBits = 0x1e0fffff; // genesis target (~2^236), below powLimit
    const int64_t t0 = 1700000000;
    params.asertAnchorParams = Consensus::Params::ASERTAnchor{0, anchorBits, t0 - 120};

    arith_uint256 anchorTarget; anchorTarget.SetCompact(anchorBits);

    auto makeChain = [&](std::vector<uint256>& hashes, std::vector<CBlockIndex>& idx, int N, auto timeFn) {
        hashes = std::vector<uint256>(N);
        idx = std::vector<CBlockIndex>(N);
        for (int i = 0; i < N; ++i) {
            hashes[i] = ArithToUint256(arith_uint256(1000 + i));
            idx[i].nHeight = i;
            idx[i].pprev = (i == 0) ? nullptr : &idx[i - 1];
            idx[i].phashBlock = &hashes[i];
            idx[i].nTime = static_cast<uint32_t>(timeFn(i));
            idx[i].nBits = anchorBits;
        }
    };

    CBlockHeader dummy; // pblock->GetBlockTime() is only used in the min-difficulty exception (disabled)
    dummy.nTime = static_cast<uint32_t>(t0);

    // 1) ON SCHEDULE (exactly 120 s): constant target == anchor, and the hookup == direct ASERT.
    {
        std::vector<uint256> h; std::vector<CBlockIndex> idx;
        makeChain(h, idx, 20, [&](int i){ return t0 + int64_t(i) * 120; });
        for (int i = 1; i < 20; ++i) {
            const uint32_t viaHook = GetNextWorkRequired(&idx[i], &dummy, params);
            const uint32_t viaAsert = GetNextASERTWorkRequired(&idx[i], &dummy, params);
            BOOST_CHECK_EQUAL(viaHook, viaAsert);   // the hookup routes to ASERT
            BOOST_CHECK_EQUAL(viaHook, anchorBits);  // on schedule -> constant target
        }
    }

    // 2) FAST (blocks at the same time): difficulty rises (target drops) relative to the anchor.
    {
        std::vector<uint256> h; std::vector<CBlockIndex> idx;
        makeChain(h, idx, 20, [&](int){ return t0; });
        arith_uint256 tgt; tgt.SetCompact(GetNextWorkRequired(&idx[19], &dummy, params));
        BOOST_CHECK(tgt < anchorTarget);
    }

    // 3) SLOW (240 s per block): difficulty drops (target rises), without exceeding powLimit.
    {
        std::vector<uint256> h; std::vector<CBlockIndex> idx;
        makeChain(h, idx, 20, [&](int i){ return t0 + int64_t(i) * 240; });
        arith_uint256 tgt; tgt.SetCompact(GetNextWorkRequired(&idx[19], &dummy, params));
        BOOST_CHECK(tgt > anchorTarget);
        BOOST_CHECK(tgt <= UintToArith256(params.powLimit));
    }
}

// Phase 1 (core layer): checkRandomX=false validates nBits WITHOUT running RandomX; the lower helper validates
// against an already-resolved context (expectedBits + seedHash).
BOOST_AUTO_TEST_CASE(contextual_header_work_layers)
{
    const uint32_t easyBits = 0x207fffff;
    Consensus::Params params{};
    SetupRandomXParams(params, 0xF00D, easyBits);

    std::vector<uint256> hashes; std::vector<CBlockIndex> idx;
    BuildChain(11, hashes, idx);

    CBlockHeader header;
    header.nVersion = 1;
    header.nTime = static_cast<uint32_t>(kChainT0 + 10 * kChainSpacing);
    header.nBits = easyBits; // == expected on schedule
    header.nNonce = 7;

    // checkRandomX=false: validates nBits, does NOT run RandomX (the build counter does not change).
    const uint64_t before = BrisviaRandomXBuildCount();
    BOOST_CHECK(CheckContextualHeaderWork(header, &idx[9], 10, params, /*checkRandomX=*/false, nullptr) == PoWCheckResult::VALID);
    BOOST_CHECK_EQUAL(BrisviaRandomXBuildCount() - before, 0u);

    // checkRandomX=false with nBits != expected -> INVALID.
    header.nBits = 0x1e0fffff;
    BOOST_CHECK(CheckContextualHeaderWork(header, &idx[9], 10, params, false, nullptr) == PoWCheckResult::INVALID);

    // Without fPowRandomX -> INTERNAL_ERROR even if RandomX is not requested.
    header.nBits = easyBits;
    Consensus::Params bad = params; bad.fPowRandomX = false;
    BOOST_CHECK(CheckContextualHeaderWork(header, &idx[9], 10, bad, false, nullptr) == PoWCheckResult::INTERNAL_ERROR);

    // Lower helper with resolved context: mine against the initial seed + easyBits.
    uint256 ph; bool foundValid = false;
    for (uint32_t nonce = 0; nonce < 256 && !foundValid; ++nonce) {
        header.nNonce = nonce;
        if (CheckRandomXHeaderWithResolvedContext(header, easyBits, params.brisviaInitialSeed, params, &ph) == PoWCheckResult::VALID)
            foundValid = true;
    }
    BOOST_REQUIRE(foundValid);
    const auto in = SerializeHeaderForRandomX(header);
    unsigned char rx_ref[BRISVIA_RANDOMX_HASH_SIZE];
    BOOST_REQUIRE(brisvia_randomx_hash(params.brisviaInitialSeed.begin(), 32, in.data(), in.size(), rx_ref));
    BOOST_CHECK(ph == RandomXOutputToUint256(rx_ref));

    // Context with expectedBits != the header's nBits -> INVALID.
    BOOST_CHECK(CheckRandomXHeaderWithResolvedContext(header, 0x1e0fffff, params.brisviaInitialSeed, params, nullptr) == PoWCheckResult::INVALID);
}

// Phase 3A-1: PURE verification layer for ONE header over a branch with an ALREADY-indexed parent. Checks that
// the failure CAUSE is distinguished (parent / difficulty / mining / local error), a typed basis for future P2P.
BOOST_AUTO_TEST_CASE(header_from_indexed_parent_statuses)
{
    const uint32_t easyBits = 0x207fffff; // target ~2^255 (mines fast)
    Consensus::Params params{};
    SetupRandomXParams(params, 0xD00D, easyBits); // fresh seed (different from other tests)

    std::vector<uint256> hashes; std::vector<CBlockIndex> idx;
    BuildChain(11, hashes, idx); // heights 0..10, on schedule

    const CBlockIndex* parent = &idx[9]; // height 9 -> the candidate header is height 10

    CBlockHeader header;
    header.nVersion = 1;
    header.hashPrevBlock = hashes[9];   // correct continuity with the indexed parent
    header.nTime = static_cast<uint32_t>(kChainT0 + 10 * kChainSpacing);
    header.nBits = easyBits;            // == expected by ASERT on schedule

    // Find one nonce whose RandomX MEETS the target (VALID) and another whose RandomX does NOT (BAD_RANDOMX_POW).
    // With target ~2^255 ~50% falls on each side. Both have nBits==expected, so both reach the RandomX stage.
    uint256 powHashValid; uint32_t validNonce = 0, invalidNonce = 0;
    bool foundValid = false, foundInvalid = false;
    for (uint32_t nonce = 0; nonce < 512 && !(foundValid && foundInvalid); ++nonce) {
        header.nNonce = nonce;
        uint256 ph;
        const auto st = CheckRandomXHeaderFromIndexedParent(parent, header, params, &ph);
        if (st == RandomXHeaderStatus::VALID && !foundValid) { foundValid = true; powHashValid = ph; validNonce = nonce; }
        else if (st == RandomXHeaderStatus::BAD_RANDOMX_POW && !foundInvalid) { foundInvalid = true; invalidNonce = nonce; }
    }
    BOOST_REQUIRE(foundValid);
    BOOST_REQUIRE(foundInvalid);

    // 1) VALID: the returned pow_hash == the standalone engine's (same initial seed + same 80 bytes).
    header.nNonce = validNonce;
    const auto in = SerializeHeaderForRandomX(header);
    unsigned char rx_ref[BRISVIA_RANDOMX_HASH_SIZE];
    BOOST_REQUIRE(brisvia_randomx_hash(params.brisviaInitialSeed.begin(), 32, in.data(), in.size(), rx_ref));
    BOOST_CHECK(powHashValid == RandomXOutputToUint256(rx_ref));

    // 2) BAD_RANDOMX_POW: the same header (nBits/parent OK) with a nonce that does not meet the target.
    header.nNonce = invalidNonce;
    BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(parent, header, params, nullptr) == RandomXHeaderStatus::BAD_RANDOMX_POW);

    // 3) BAD_PREV_BLOCK: header that does NOT chain to the given parent (detected before RandomX, with a valid nonce).
    header.nNonce = validNonce;
    { CBlockHeader h = header; h.hashPrevBlock = ArithToUint256(arith_uint256(999999)); // != parent hash (1009)
      BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(parent, h, params, nullptr) == RandomXHeaderStatus::BAD_PREV_BLOCK); }
    // null parent -> also BAD_PREV_BLOCK (this layer requires an indexed parent; genesis is not verified here).
    BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(nullptr, header, params, nullptr) == RandomXHeaderStatus::BAD_PREV_BLOCK);

    // 4) BAD_BITS: nBits in range but != the one expected by ASERT (wrong difficulty). Cut off before RandomX.
    { CBlockHeader h = header; h.nBits = 0x1e0fffff; // valid in range (~2^236), different from the expected easyBits
      BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(parent, h, params, nullptr) == RandomXHeaderStatus::BAD_BITS); }
    // nBits out of range (0) -> BAD_BITS.
    { CBlockHeader h = header; h.nBits = 0;
      BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(parent, h, params, nullptr) == RandomXHeaderStatus::BAD_BITS); }

    // 5) INTERNAL_ERROR: LOCAL node failure/misconfiguration, NEVER a consensus invalidity (do not ban peers).
    { Consensus::Params bad = params; bad.fPowRandomX = false;
      BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(parent, header, bad, nullptr) == RandomXHeaderStatus::INTERNAL_ERROR); }
    { Consensus::Params bad = params; bad.asertAnchorParams.reset();
      BOOST_CHECK(CheckRandomXHeaderFromIndexedParent(parent, header, bad, nullptr) == RandomXHeaderStatus::INTERNAL_ERROR); }
}

// Phase 3A-2 (refactor equivalence): the by-VALUES ASERT variant yields EXACTLY the same as the indexed one
// (the latter delegates to the former). Compared across three time profiles, for several heights.
BOOST_AUTO_TEST_CASE(asert_values_equivalence)
{
    Consensus::Params params{};
    params.fPowRandomX = true;
    params.fPowAllowMinDifficultyBlocks = false;
    params.nPowTargetSpacing = 120;
    params.nASERTHalfLife = 21600;
    { arith_uint256 pl; pl.SetCompact(0x1e3fffff); params.powLimit = ArithToUint256(pl); }
    const uint32_t anchorBits = 0x1e0fffff;
    const int64_t t0 = 1700000000;
    params.asertAnchorParams = Consensus::Params::ASERTAnchor{0, anchorBits, t0 - 120};

    const int N = 40;
    std::vector<uint256> h(N); std::vector<CBlockIndex> idx(N);
    CBlockHeader cand; cand.nTime = static_cast<uint32_t>(t0 + int64_t(N) * 120);

    for (int prof = 0; prof < 3; ++prof) { // 0=on schedule, 1=fast (same t), 2=slow (240 s)
        for (int i = 0; i < N; ++i) {
            h[i] = ArithToUint256(arith_uint256(1000 + i));
            idx[i].nHeight = i; idx[i].pprev = (i == 0) ? nullptr : &idx[i - 1];
            idx[i].phashBlock = &h[i];
            const int64_t tt = (prof == 0) ? t0 + int64_t(i) * 120 : (prof == 1) ? t0 : t0 + int64_t(i) * 240;
            idx[i].nTime = static_cast<uint32_t>(tt);
            idx[i].nBits = anchorBits;
        }
        for (int i = 1; i < N; ++i) {
            const uint32_t viaIndex  = GetNextASERTWorkRequired(&idx[i], &cand, params);
            const uint32_t viaValues = GetNextASERTWorkRequiredFromValues(idx[i].nHeight, idx[i].GetBlockTime(), &cand, params);
            BOOST_CHECK_EQUAL(viaIndex, viaValues);
        }
    }
}

// Phase 3A-2 (resolution): the transient branch correctly resolves the SEED by height at the crossings (63->64,
// epoch 2048), the 2112 circular window suffices (sufficiency), and the difficulty dispatch (constant vs expected)
// is correct. Without running RandomX: SYNTHETIC block ids are populated via CommitRandomXBranchHeader (fast).
BOOST_AUTO_TEST_CASE(branch_context_resolution)
{
    const uint32_t bits = 0x207fffff;
    Consensus::Params params{};
    params.fPowRandomX = true;
    params.fPowNoRetargeting = true; // constant difficulty: expectedBits = tip's nBits (avoids ASERT here)
    { arith_uint256 t; t.SetCompact(bits); params.powLimit = ArithToUint256(t); }
    params.brisviaInitialSeed = ArithToUint256(arith_uint256(0xABCD));

    // Anchor at height 0 (known block id). Epoch-0 seeds (heights 64..2111) point to this block.
    uint256 anchorHash = ArithToUint256(arith_uint256(5000));
    CBlockIndex anchor; anchor.nHeight = 0; anchor.pprev = nullptr; anchor.phashBlock = &anchorHash;
    anchor.nTime = 1700000000; anchor.nBits = bits;

    RandomXHeaderBranchContext ctx = MakeRandomXHeaderBranchContext(&anchor);

    std::vector<uint256> blockIdByHeight(1); blockIdByHeight[0] = anchorHash; // height -> block id

    // Helper: creates a candidate consistent with the current tip (continuity + tip's nBits).
    auto candidato = [&](int64_t height) {
        CBlockHeader hd; hd.nVersion = 1; hd.hashPrevBlock = ctx.tipBlockId;
        hd.nTime = static_cast<uint32_t>(1700000000 + height * 120); hd.nBits = bits;
        hd.nNonce = static_cast<uint32_t>(height); // unique block id per height
        return hd;
    };
    // Helper: appends (without mining) a synthetic header as the new tip and records its block id.
    auto avanzar = [&]() {
        CBlockHeader hd = candidato(ctx.tipHeight + 1);
        blockIdByHeight.push_back(hd.GetHash());
        CommitRandomXBranchHeader(ctx, hd);
    };

    // --- Crossing 63 -> 64 ---
    while (ctx.tipHeight < 62) avanzar();       // tip = 62
    { BranchResolved r; CBlockHeader c = candidato(63);
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::OK);
      BOOST_CHECK_EQUAL(r.height, 63);
      BOOST_CHECK(r.seedHash == params.brisviaInitialSeed); }   // height 63 -> fixed initial seed
    avanzar();                                   // tip = 63
    { BranchResolved r; CBlockHeader c = candidato(64);
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::OK);
      BOOST_CHECK(r.seedHash == blockIdByHeight[0]); }          // height 64 -> hash of block 0 (the anchor)

    // --- Sufficiency of the 2112 window: epoch-2048 seed (transient) used at 2112 ---
    while (ctx.tipHeight < 2111) avanzar();      // tip = 2111 (the height-2048 block has already been committed)
    { BranchResolved r; CBlockHeader c = candidato(2112);
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::OK);
      BOOST_CHECK(r.seedHash == blockIdByHeight[2048]); }       // hash of the height-2048 transient

    // Edge: block id 2048 must stay alive until the last candidate of its epoch (4159), and only then
    // rotate to 4096. We advance to 4158 and check both sides of the edge.
    while (ctx.tipHeight < 4158) avanzar();      // tip = 4158
    { BranchResolved r; CBlockHeader c = candidato(4159);
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::OK);
      BOOST_CHECK(r.seedHash == blockIdByHeight[2048]); }       // 4159 still uses seed 2048 (lower edge)
    avanzar();                                   // tip = 4159
    { BranchResolved r; CBlockHeader c = candidato(4160);
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::OK);
      BOOST_CHECK(r.seedHash == blockIdByHeight[4096]); }       // 4160 already rotates to seed 4096

    // --- Difficulty dispatch: with nBits != the expected -> BAD_BITS (constant difficulty) ---
    { BranchResolved r; CBlockHeader c = candidato(ctx.tipHeight + 1); c.nBits = 0x1e0fffff;
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::BAD_BITS); }
    // Broken continuity -> BAD_PREV_BLOCK.
    { BranchResolved r; CBlockHeader c = candidato(ctx.tipHeight + 1); c.hashPrevBlock = ArithToUint256(arith_uint256(1));
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::BAD_PREV_BLOCK); }
}

// Phase 3A-2 (seed indexed via a side branch): when an epoch's seed falls in the ALREADY-indexed part, it is
// resolved via pindexAnchor->GetAncestor (the anchor's branch), NOT the active chain. Anchor at 2050.
BOOST_AUTO_TEST_CASE(branch_context_seed_indexed_via_ancestor)
{
    const uint32_t bits = 0x207fffff;
    Consensus::Params params{};
    params.fPowRandomX = true; params.fPowNoRetargeting = true;
    { arith_uint256 t; t.SetCompact(bits); params.powLimit = ArithToUint256(t); }
    params.brisviaInitialSeed = ArithToUint256(arith_uint256(0x1111));

    std::vector<uint256> hashes; std::vector<CBlockIndex> idx;
    BuildChain(2051, hashes, idx);          // heights 0..2050 indexed
    idx[2050].nBits = bits;                 // the anchor needs a valid nBits (initial tipBits)

    RandomXHeaderBranchContext ctx = MakeRandomXHeaderBranchContext(&idx[2050]);

    auto candidato = [&](int64_t height) {
        CBlockHeader hd; hd.nVersion = 1; hd.hashPrevBlock = ctx.tipBlockId;
        hd.nTime = static_cast<uint32_t>(kChainT0 + height * kChainSpacing); hd.nBits = bits;
        hd.nNonce = static_cast<uint32_t>(height);
        return hd;
    };
    while (ctx.tipHeight < 2111) { CBlockHeader hd = candidato(ctx.tipHeight + 1); CommitRandomXBranchHeader(ctx, hd); }

    // candidate 2112: seedHeight=2048 <= anchor height (2050) -> resolved via the anchor's GetAncestor(2048).
    { BranchResolved r; CBlockHeader c = candidato(2112);
      BOOST_CHECK(ResolveRandomXBranchContext(ctx, c, params, r) == BranchResolveStatus::OK);
      BOOST_CHECK(r.seedHash == hashes[2048]); } // indexed block id of height 2048, via the anchor's branch
}

// Phase 3A-2 (full validation + advance): real RandomX mining over the transient branch, crossing the seed change
// at 64 (initial seed -> hash of block 0). Verifies distinguished causes and NO-mutation on error.
BOOST_AUTO_TEST_CASE(branch_context_validate_and_advance)
{
    const uint32_t bits = 0x207fffff; // target ~2^255 (mines fast)
    Consensus::Params params{};
    params.fPowRandomX = true; params.fPowNoRetargeting = true; params.fPowAllowMinDifficultyBlocks = false;
    { arith_uint256 t; t.SetCompact(bits); params.powLimit = ArithToUint256(t); }
    params.brisviaInitialSeed = ArithToUint256(arith_uint256(0xF33D));

    std::vector<uint256> hashes; std::vector<CBlockIndex> idx;
    BuildChain(61, hashes, idx);   // heights 0..60 indexed; block 0 will be the seed for height 64
    idx[60].nBits = bits;

    RandomXHeaderBranchContext ctx = MakeRandomXHeaderBranchContext(&idx[60]);
    const int64_t startHeight = ctx.tipHeight; // 60

    // Mine 61..65 (61,62,63 use the initial seed; 64,65 use the hash of block 0). Same constant target.
    for (int64_t target = 61; target <= 65; ++target) {
        // Before mining, a header with wrong nBits -> BAD_BITS and the context does NOT change (no-mutation).
        const int64_t hBefore = ctx.tipHeight; const uint256 idBefore = ctx.tipBlockId;
        { CBlockHeader bad; bad.nVersion = 1; bad.hashPrevBlock = ctx.tipBlockId; bad.nBits = 0x1e0fffff; bad.nNonce = 0;
          bad.nTime = static_cast<uint32_t>(kChainT0 + target * kChainSpacing);
          BOOST_CHECK(CheckAndAdvanceRandomXHeader(ctx, bad, params, nullptr) == RandomXHeaderStatus::BAD_BITS);
          BOOST_CHECK_EQUAL(ctx.tipHeight, hBefore); BOOST_CHECK(ctx.tipBlockId == idBefore); }
        // Broken continuity -> BAD_PREV_BLOCK, without mutating.
        { CBlockHeader bad; bad.nVersion = 1; bad.hashPrevBlock = ArithToUint256(arith_uint256(7)); bad.nBits = bits; bad.nNonce = 1;
          bad.nTime = static_cast<uint32_t>(kChainT0 + target * kChainSpacing);
          BOOST_CHECK(CheckAndAdvanceRandomXHeader(ctx, bad, params, nullptr) == RandomXHeaderStatus::BAD_PREV_BLOCK);
          BOOST_CHECK_EQUAL(ctx.tipHeight, hBefore); }

        // Mine the valid header (find a nonce that meets the target). Nonce-based invalids are PoW-only and do not mutate.
        CBlockHeader hd; hd.nVersion = 1; hd.hashPrevBlock = ctx.tipBlockId; hd.nBits = bits;
        hd.nTime = static_cast<uint32_t>(kChainT0 + target * kChainSpacing);
        bool mined = false; uint256 pow;
        for (uint32_t nonce = 0; nonce < 4096; ++nonce) {
            hd.nNonce = nonce;
            const auto st = CheckAndAdvanceRandomXHeader(ctx, hd, params, &pow);
            if (st == RandomXHeaderStatus::VALID) { mined = true; break; }
            BOOST_REQUIRE(st == RandomXHeaderStatus::BAD_RANDOMX_POW); // nBits/parent OK -> only the PoW can fail
            BOOST_CHECK_EQUAL(ctx.tipHeight, hBefore);                 // each failure does NOT advance the context
        }
        BOOST_REQUIRE(mined);
        BOOST_CHECK_EQUAL(ctx.tipHeight, target);        // advanced exactly one block
        BOOST_CHECK(ctx.tipBlockId == hd.GetHash());     // the tip is the just-validated header
    }
    BOOST_CHECK_EQUAL(ctx.tipHeight, startHeight + 5);

    // Bad config (no fPowRandomX) -> INTERNAL_ERROR, not a consensus invalidity.
    { Consensus::Params badp = params; badp.fPowRandomX = false;
      CBlockHeader hd; hd.nVersion = 1; hd.hashPrevBlock = ctx.tipBlockId; hd.nBits = bits; hd.nNonce = 0;
      hd.nTime = static_cast<uint32_t>(kChainT0 + 66 * kChainSpacing);
      BOOST_CHECK(CheckAndAdvanceRandomXHeader(ctx, hd, badp, nullptr) == RandomXHeaderStatus::INTERNAL_ERROR); }
}

// Mines the regtest GENESIS block with RandomX (not SHA256d) and records the values for chainparams.
// Key point: the block's SHA256d ID does NOT have to meet the target; the one that meets it is
// RandomX. Reward 0 with OP_RETURN <anchor> (fair-launch lock, no one's key).
BOOST_AUTO_TEST_CASE(mine_regtest_genesis)
{
    const char* pszTimestamp = "Brisvia regtest genesis - rx/brva-v1";
    uint256 seed; std::memset(seed.begin(), 0x42, 32); // fixed regtest RandomX seed
    const uint32_t nBits = 0x207fffff;                  // easy regtest target (real RandomX, near-instant mining)
    const uint32_t nTime = 1751760000;

    Consensus::Params params{};
    params.fPowRandomX = true;
    { arith_uint256 t; t.SetCompact(nBits); params.powLimit = ArithToUint256(t); }

    // Genesis via the reusable constructor (the same chainparams will use). Fixed 32-byte test anchor.
    unsigned char anchor[32]; std::memset(anchor, 0xAB, sizeof(anchor));
    CBlock genesis = CreateBrisviaGenesisBlock(pszTimestamp, anchor, nTime, /*nNonce=*/0, nBits, /*nVersion=*/1);

    // Mine by RandomX: iterate nNonce until the RandomX hash meets the target (reuses the cached VM).
    uint256 powHash;
    bool mined = false;
    for (uint32_t nNonce = 0; nNonce < (1u << 20); ++nNonce) {
        genesis.nNonce = nNonce;
        if (CheckRandomXHeaderWithResolvedContext(genesis, nBits, seed, params, &powHash) == PoWCheckResult::VALID) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);

    arith_uint256 target; target.SetCompact(nBits);
    BOOST_CHECK(UintToArith256(powHash) <= target); // RandomX meets the target

    // FROZEN values of the regtest genesis (regression: the reusable constructor always reproduces them).
    BOOST_CHECK_EQUAL(genesis.nNonce, 2u);
    BOOST_CHECK_EQUAL(genesis.GetHash().GetHex(),
                      "38bfbc99c2f734111485d6a1ad4650e89b51759ebb8ae5299abaf42cc1f6cdc2");
    BOOST_CHECK_EQUAL(genesis.hashMerkleRoot.GetHex(),
                      "7a0e51313ff1263e13262540463f531f41d4215c75f2ccfab091a387a5837325");

    BOOST_TEST_MESSAGE("=== regtest GENESIS mined with RandomX (values for chainparams) ===");
    BOOST_TEST_MESSAGE("nTime      = " << genesis.nTime);
    BOOST_TEST_MESSAGE("nBits      = 0x207fffff");
    BOOST_TEST_MESSAGE("nNonce     = " << genesis.nNonce);
    BOOST_TEST_MESSAGE("merkleRoot = " << genesis.hashMerkleRoot.GetHex());
    BOOST_TEST_MESSAGE("blockID    = " << genesis.GetHash().GetHex());
    BOOST_TEST_MESSAGE("powHash    = " << powHash.GetHex());
    BOOST_TEST_MESSAGE("seed       = " << seed.GetHex());
}

// Mines the Brisvia TESTNET GENESIS block with RandomX. Its own constants (phrase, anchor, seed, time)
// differ from regtest -> different genesis -> separate network identity. Prints the values to freeze in
// chainparams (same procedure as mine_regtest_genesis). The genesis target is easy (mines instantly);
// the testnet's REAL difficulty is governed by ASERT (fPowNoRetargeting=false in chainparams).
BOOST_AUTO_TEST_CASE(mine_testnet_genesis)
{
    const char* pszTimestamp = "Brisvia testnet genesis - rx/brva-v1";
    uint256 seed; std::memset(seed.begin(), 0x54, 32); // initial testnet RandomX seed ('T'), different from regtest (0x42)
    const uint32_t nBits = 0x207fffff;                  // easy target (real RandomX, near-instant mining)
    const uint32_t nTime = 1751846400;                  // 2025-07-07 00:00:00 UTC

    Consensus::Params params{};
    params.fPowRandomX = true;
    { arith_uint256 t; t.SetCompact(nBits); params.powLimit = ArithToUint256(t); }

    unsigned char anchor[32]; std::memset(anchor, 0xB2, sizeof(anchor)); // testnet test anchor (different from regtest 0xAB)
    CBlock genesis = CreateBrisviaGenesisBlock(pszTimestamp, anchor, nTime, /*nNonce=*/0, nBits, /*nVersion=*/1);

    uint256 powHash;
    bool mined = false;
    for (uint32_t nNonce = 0; nNonce < (1u << 20); ++nNonce) {
        genesis.nNonce = nNonce;
        if (CheckRandomXHeaderWithResolvedContext(genesis, nBits, seed, params, &powHash) == PoWCheckResult::VALID) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);

    arith_uint256 target; target.SetCompact(nBits);
    BOOST_CHECK(UintToArith256(powHash) <= target); // RandomX meets the target

    BOOST_TEST_MESSAGE("=== testnet GENESIS mined with RandomX (values for chainparams) ===");
    BOOST_TEST_MESSAGE("nTime      = " << genesis.nTime);
    BOOST_TEST_MESSAGE("nBits      = 0x207fffff");
    BOOST_TEST_MESSAGE("nNonce     = " << genesis.nNonce);
    BOOST_TEST_MESSAGE("merkleRoot = " << genesis.hashMerkleRoot.GetHex());
    BOOST_TEST_MESSAGE("blockID    = " << genesis.GetHash().GetHex());
    BOOST_TEST_MESSAGE("powHash    = " << powHash.GetHex());
    BOOST_TEST_MESSAGE("seed       = " << seed.GetHex());
}

// Mines the CANONICAL GENESIS of the Brisvia testnet (ChainType::BRISVIA_TESTNET). Unlike the rehearsal, it uses:
//  - real launch nTime (prevents ASERT from starting "behind" and leaving difficulty at the floor).
//  - CALIBRATED nBits (harder than powLimit) as the ASERT anchor, to avoid a block storm at the start.
// Fixes validated (2026-07-07). Prints the values to freeze in chainparams.
BOOST_AUTO_TEST_CASE(mine_brisvia_testnet_genesis)
{
    const char* pszTimestamp = "Brisvia testnet genesis - rx/brva-v1";
    uint256 seed; std::memset(seed.begin(), 0x54, 32);  // initial testnet RandomX seed
    const uint32_t nBits = 0x1e7fffff;                  // target ~2^239 (~131k hashes/block): realistic initial difficulty for a small testnet
    const uint32_t nTime = 1783382400;                  // 2026-07-07 00:00:00 UTC (launch date)

    Consensus::Params params{};
    params.fPowRandomX = true;
    { arith_uint256 t; t.SetCompact(nBits); params.powLimit = ArithToUint256(t); } // powLimit >= genesis target so it can be mined

    unsigned char anchor[32]; std::memset(anchor, 0xB2, sizeof(anchor));
    CBlock genesis = CreateBrisviaGenesisBlock(pszTimestamp, anchor, nTime, /*nNonce=*/0, nBits, /*nVersion=*/1);

    uint256 powHash;
    bool mined = false;
    for (uint32_t nNonce = 0; nNonce < (1u << 23); ++nNonce) { // wide margin (~64x the expectation) for target ~2^239
        genesis.nNonce = nNonce;
        if (CheckRandomXHeaderWithResolvedContext(genesis, nBits, seed, params, &powHash) == PoWCheckResult::VALID) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);

    arith_uint256 target; target.SetCompact(nBits);
    BOOST_CHECK(UintToArith256(powHash) <= target);

    // FROZEN values of the canonical genesis (regression).
    BOOST_CHECK_EQUAL(genesis.nNonce, 25314u);
    BOOST_CHECK_EQUAL(genesis.GetHash().GetHex(),
                      "c5a8d09048ea1957010a555490d9f3ea8b30d30f7432b1d84e5e068c666712d1");
    BOOST_CHECK_EQUAL(genesis.hashMerkleRoot.GetHex(),
                      "2568cf9e1033415283e52c8ef0f548958edf434f428496df25085a637033004b");

    BOOST_TEST_MESSAGE("=== CANONICAL GENESIS testnet Brisvia (values for chainparams) ===");
    BOOST_TEST_MESSAGE("nTime      = " << genesis.nTime);
    BOOST_TEST_MESSAGE("nBits      = 0x1e7fffff");
    BOOST_TEST_MESSAGE("nNonce     = " << genesis.nNonce);
    BOOST_TEST_MESSAGE("merkleRoot = " << genesis.hashMerkleRoot.GetHex());
    BOOST_TEST_MESSAGE("blockID    = " << genesis.GetHash().GetHex());
    BOOST_TEST_MESSAGE("powHash    = " << powHash.GetHex());
}

// Mines the CANONICAL GENESIS of the Brisvia MAINNET (ChainType::BRISVIA_MAIN). Same mechanics as the testnet:
//  - real mainnet launch nTime (1 Aug 2026 12:00 ART = 15:00 UTC = Unix 1785596400).
//  - genesis phrase in English (fair launch, no premine).
//  - mainnet's own anchor 0xB3 (testnet uses 0xB2) + initial nBits 0x1e7fffff (ASERT anchor).
// Prints the values to freeze in CBrisviaMainParams.
BOOST_AUTO_TEST_CASE(mine_brisvia_mainnet_genesis)
{
    const char* pszTimestamp = "No privilege at genesis: an open network, mined and sustained by the people.";
    uint256 seed; std::memset(seed.begin(), 0x54, 32);  // initial RandomX seed (same as testnet)
    const uint32_t nBits = 0x1e7fffff;
    const uint32_t nTime = 1785596400;                  // 2026-08-01 15:00:00 UTC (1 Aug 12:00 ART)

    Consensus::Params params{};
    params.fPowRandomX = true;
    { arith_uint256 t; t.SetCompact(nBits); params.powLimit = ArithToUint256(t); }

    unsigned char anchor[32]; std::memset(anchor, 0xB3, sizeof(anchor)); // mainnet's own anchor
    CBlock genesis = CreateBrisviaGenesisBlock(pszTimestamp, anchor, nTime, /*nNonce=*/0, nBits, /*nVersion=*/1);

    uint256 powHash;
    bool mined = false;
    for (uint32_t nNonce = 0; nNonce < (1u << 23); ++nNonce) {
        genesis.nNonce = nNonce;
        if (CheckRandomXHeaderWithResolvedContext(genesis, nBits, seed, params, &powHash) == PoWCheckResult::VALID) {
            mined = true;
            break;
        }
    }
    BOOST_REQUIRE(mined);

    arith_uint256 target; target.SetCompact(nBits);
    BOOST_CHECK(UintToArith256(powHash) <= target);

    // FROZEN values of the mainnet canonical genesis (regression).
    BOOST_CHECK_EQUAL(genesis.nNonce, 90424u);
    BOOST_CHECK_EQUAL(genesis.GetHash().GetHex(),
                      "7f1cf9cfc74095157a6a56f1de75034f0ac514aadffb507040c0351a4db4c1ff");
    BOOST_CHECK_EQUAL(genesis.hashMerkleRoot.GetHex(),
                      "c17012d733986f74e43d3a2852646e44c5d3fc9f0b45d165ac8998d96b6eb98d");

    BOOST_TEST_MESSAGE("=== CANONICAL GENESIS mainnet Brisvia (values for chainparams) ===");
    BOOST_TEST_MESSAGE("nTime      = " << genesis.nTime);
    BOOST_TEST_MESSAGE("nBits      = 0x1e7fffff");
    BOOST_TEST_MESSAGE("nNonce     = " << genesis.nNonce);
    BOOST_TEST_MESSAGE("merkleRoot = " << genesis.hashMerkleRoot.GetHex());
    BOOST_TEST_MESSAGE("blockID    = " << genesis.GetHash().GetHex());
    BOOST_TEST_MESSAGE("powHash    = " << powHash.GetHex());
}

// RandomX C++ <-> Rust consistency VECTOR (miner Step 2.1). FIXED input and seed -> expected hash.
// The Rust miner MUST reproduce exactly these 32 bytes; otherwise mining is not started.
BOOST_AUTO_TEST_CASE(brisvia_randomx_vector)
{
    unsigned char key[32], input[80];
    for (int i = 0; i < 32; ++i) key[i]   = (unsigned char)i;   // seed = 00 01 02 ... 1f
    for (int i = 0; i < 80; ++i) input[i] = (unsigned char)i;   // input   = 00 01 02 ... 4f
    unsigned char out[BRISVIA_RANDOMX_HASH_SIZE];
    BOOST_REQUIRE(brisvia_randomx_hash(key, sizeof(key), input, sizeof(input), out));
    std::string hex;
    for (int i = 0; i < 32; ++i) { char b[3]; std::snprintf(b, sizeof(b), "%02x", out[i]); hex += b; }
    BOOST_TEST_MESSAGE("=== RandomX Brisvia VECTOR (reference for the Rust miner) ===");
    BOOST_TEST_MESSAGE("key(32)   = 000102...1f");
    BOOST_TEST_MESSAGE("input(80) = 000102...4f");
    BOOST_TEST_MESSAGE("hash(32)  = " << hex);
    // Frozen as an assert once the Rust miner reproduces it (bilateral contract).
}

// Brisvia emission: genesis 0, 25 BRVA until the 1st halving (from block 1), halving, perpetual tail 1.
// Small halving interval (10) to cover the edges fast. See consensus/brisvia_emission.h.
BOOST_AUTO_TEST_CASE(brisvia_emission)
{
    const int64_t INI = 25 * COIN; // initial reward
    const int64_t TAIL = 1 * COIN;  // perpetual tail
    const int64_t H = 10;           // test halving interval

    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(0,  INI, TAIL, H), 0);            // genesis: 0
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(1,  INI, TAIL, H), INI);          // first block: 25
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(10, INI, TAIL, H), INI);          // last of the initial stretch
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(11, INI, TAIL, H), INI >> 1);     // 1st halving -> 12.5
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(20, INI, TAIL, H), INI >> 1);     // 12.5
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(21, INI, TAIL, H), INI >> 2);     // 6.25
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(41, INI, TAIL, H), INI >> 4);     // 1.5625
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(50, INI, TAIL, H), INI >> 4);     // still 1.5625
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(51, INI, TAIL, H), TAIL);         // 0.78125 -> tail 1
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(100000, INI, TAIL, H), TAIL);     // perpetual tail
}

// Network config: the Brisvia regtest (brisvia_pow option) gets RandomX PoW + emission + ASERT anchor +
// frozen mined genesis + brvrt prefix; the Bitcoin regtest (brisvia_pow=false) stays intact.
BOOST_AUTO_TEST_CASE(brisvia_regtest_chainparams)
{
    CChainParams::RegTestOptions opts;
    opts.brisvia_pow = true;
    const auto params = CChainParams::RegTest(opts);
    const Consensus::Params& c = params->GetConsensus();

    BOOST_CHECK(c.fPowRandomX);
    BOOST_CHECK_EQUAL(c.nPowTargetSpacing, 120);
    BOOST_REQUIRE(c.asertAnchorParams.has_value());
    BOOST_CHECK_EQUAL(c.asertAnchorParams->nHeight, 0);
    BOOST_CHECK_EQUAL(c.asertAnchorParams->nBits, 0x207fffffu);
    BOOST_CHECK_EQUAL(c.nBrisviaInitialSubsidy, 25 * COIN);
    BOOST_CHECK_EQUAL(c.nBrisviaTailSubsidy, 1 * COIN);
    BOOST_CHECK(c.fBrisviaSubsidy);
    BOOST_CHECK_EQUAL(params->GenesisBlock().GetHash().GetHex(),
                      "38bfbc99c2f734111485d6a1ad4650e89b51759ebb8ae5299abaf42cc1f6cdc2");
    BOOST_CHECK_EQUAL(params->Bech32HRP(), "brvrt");

    // Bitcoin regtest left untouched.
    CChainParams::RegTestOptions optsNormal;
    const auto normal = CChainParams::RegTest(optsNormal);
    BOOST_CHECK(!normal->GetConsensus().fPowRandomX);
    BOOST_CHECK_EQUAL(normal->GenesisBlock().GetHash().GetHex(),
                      "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206");
    BOOST_CHECK_EQUAL(normal->Bech32HRP(), "bcrt");
}

// RandomX seed-height selection: fixed initial seed for height < 64, then the seed is the hash of an
// earlier block, rotating every 2048 blocks with a 64-block lag. Uses the SAME function the node/miner
// use (BrisviaSeedHeight). See consensus/randomx_seed.h. Exact edges audited pre-mainnet.
BOOST_AUTO_TEST_CASE(brisvia_seed_height_edges)
{
    // Heights 0..63: fixed initial launch seed (a chainparams constant, not a block hash).
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(0),  BRISVIA_SEED_INITIAL);
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(63), BRISVIA_SEED_INITIAL);  // last height with the fixed seed
    BOOST_CHECK(BrisviaUsesInitialSeed(63));
    BOOST_CHECK(!BrisviaUsesInitialSeed(64));

    // From height 64 the seed is a real block hash, rotating every 2048 with a lag of 64.
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(64),   0);      // genesis (block 0)
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(2111), 0);      // still genesis (window has not rotated yet)
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(2112), 2048);   // first rotation -> block 2048
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(4159), 2048);   // still block 2048
    BOOST_CHECK_EQUAL(BrisviaSeedHeight(4160), 4096);   // second rotation -> block 4096

    // The seed changes exactly at the boundaries 64 + k*2048 and nowhere else in between.
    BOOST_CHECK(BrisviaSeedChangesAt(64));    // first switch away from the fixed seed
    BOOST_CHECK(!BrisviaSeedChangesAt(65));
    BOOST_CHECK(BrisviaSeedChangesAt(2112));  // 64 + 2048
    BOOST_CHECK(!BrisviaSeedChangesAt(2113));
}

// Brisvia MAINNET emission (real parameters, see kernel/chainparams.cpp CBrisviaMainParams):
// 50 BRVA per block, halving every 1,000,000 blocks, NO tail (finite, Bitcoin-style, ~100M cap).
// Note on the edges: genesis (height 0) pays 0 (no-premine signal) and halvings are counted FROM
// block 1 (halvings = (nHeight-1)/interval), so the interval-0 reward (50 BRVA) covers blocks
// 1..1,000,000 and the FIRST halving lands at block 1,000,001 (not at 1,000,000).
BOOST_AUTO_TEST_CASE(brisvia_emission_mainnet)
{
    const int64_t INI = 50 * COIN; // 50 BRVA = 5,000,000,000 sat
    const int64_t TAIL = 0;        // no tail: finite emission, 100M cap
    const int H = 1000000;         // halving interval (mainnet)

    // Interval 0 (50 BRVA): blocks 1..1,000,000. Genesis is 0.
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(0,       INI, TAIL, H), 0);        // genesis: no reward
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(1,       INI, TAIL, H), INI);      // first mined block: 50 BRVA
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(999999,  INI, TAIL, H), INI);      // 50 BRVA
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(1000000, INI, TAIL, H), INI);      // last block of interval 0: still 50 BRVA

    // Interval 1 (25 BRVA): blocks 1,000,001..2,000,000.
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(1000001, INI, TAIL, H), INI >> 1); // first halving: 25 BRVA
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(1999999, INI, TAIL, H), INI >> 1); // 25 BRVA
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(2000000, INI, TAIL, H), INI >> 1); // last block of interval 1: 25 BRVA
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(2000001, INI, TAIL, H), INI >> 2); // second halving: 12.5 BRVA

    // Halving 32 pays 1 sat (the last positive subsidy); halving 33 onward pays 0 (finite emission).
    BOOST_CHECK_EQUAL(INI >> 32, 1);                                            // sanity: 5e9 >> 32 == 1
    BOOST_CHECK_EQUAL(INI >> 33, 0);                                            // sanity: 5e9 >> 33 == 0
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(32 * H,     INI, TAIL, H), 2);     // halvings=31: 2 sat
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(32 * H + 1, INI, TAIL, H), 1);     // halvings=32: 1 sat (first block)
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(33 * H,     INI, TAIL, H), 1);     // halvings=32: 1 sat (last block)
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(33 * H + 1, INI, TAIL, H), 0);     // halvings=33: 0
    BOOST_CHECK_EQUAL(BrisviaGetBlockSubsidy(50 * H,     INI, TAIL, H), 0);     // well past exhaustion: 0

    // Total emission = sum over the ~33 halving intervals of (per-block subsidy * blocks per interval).
    // Each interval is exactly 1,000,000 blocks; the per-block subsidy is taken from the SAME function
    // (a representative block of each interval). Must equal 9,999,999,989,000,000 sat = 99,999,999.89 BRVA,
    // which stays just under the 100,000,000 BRVA nominal cap (the ~0.11 BRVA gap comes from integer
    // truncation of the right-shifts).
    int64_t total = 0;
    int intervals = 0;
    for (int k = 0; ; ++k) {
        const int64_t s = BrisviaGetBlockSubsidy(k * H + 1, INI, TAIL, H); // representative of interval k
        if (s == 0) break;
        total += s * static_cast<int64_t>(H);
        ++intervals;
    }
    BOOST_CHECK_EQUAL(intervals, 33);                       // k = 0..32
    BOOST_CHECK_EQUAL(total, 9999999989000000LL);           // 99,999,999.89 BRVA
    BOOST_CHECK(total < 100000000LL * COIN);                // below the 100M BRVA nominal cap
}

// ===================== Brisvia: mainnet ASERT half-life (21600 s) propagation =====================
// FUNCTIONAL proof (requested by the external audit) that the mainnet ASERT half-life of 21600 s (6 h),
// set only in CBrisviaMainParams (kernel/chainparams.cpp: consensus.nASERTHalfLife = 21600), actually reaches
// the node's live difficulty computation
//     GetNextWorkRequired -> GetNextASERTWorkRequired -> GetNextASERTWorkRequiredFromValues -> CalculateASERT
// and is NOT shadowed by any hardcoded constant anywhere on that path. This goes beyond the math differential
// and beyond the consensus tripwire (brisvia_consensus_guards_tests): it drives the REAL mainnet params object
// end to end and checks the nBits sequence the node would demand.

// Independent REFERENCE oracle: a self-contained mirror of the aserti3 formula (CalculateASERT in pow.cpp),
// taking the half-life as an EXPLICIT argument. Not wired to consensus; used only to compute expected nBits with
// an arbitrary half-life. The formula itself is already audited (Python oracle, 8160 vectors), so any mismatch
// against the real path signals a WIRING/propagation bug, not a formula bug.
static arith_uint256 AsertRefTarget(const arith_uint256& refTarget, int64_t spacing, int64_t timeDiff,
                                    int64_t heightDiff, const arith_uint256& powLimit, int64_t halfLife)
{
    const int64_t exponent = ((timeDiff - spacing * (heightDiff + 1)) * 65536) / halfLife;
    int64_t shifts = exponent >> 16;
    const auto frac = uint16_t(exponent);
    const uint32_t factor = 65536 + ((
        + 195766423245049ull * frac
        + 971821376ull * frac * frac
        + 5127ull * frac * frac * frac
        + (1ull << 47)
        ) >> 48);
    arith_uint512 next512 = arith_uint512::from(refTarget) * factor;
    arith_uint512 powLimit512 = arith_uint512::from(powLimit);
    shifts -= 16;
    if (shifts <= 0) {
        next512 >>= -shifts;
    } else {
        const auto shifted = next512 << shifts;
        if ((shifted >> shifts) != next512) {
            next512 = powLimit512;
        } else {
            next512 = shifted;
        }
    }
    if (next512 > powLimit512) next512 = powLimit512;
    arith_uint256 next = arith_uint256::from(next512);
    if (next == 0) next = arith_uint256(1);
    else if (next > powLimit) next = powLimit;
    return next;
}

// Expected next nBits for a chosen half-life, replicating the anchor math of GetNextASERTWorkRequiredFromValues.
static uint32_t AsertRefNextBits(const Consensus::Params& p, int prevHeight, int64_t prevTime, int64_t halfLife)
{
    const auto& a = *p.asertAnchorParams;
    arith_uint256 refTarget; refTarget.SetCompact(a.nBits);
    const arith_uint256 powLimit = UintToArith256(p.powLimit);
    const int64_t timeDiff = prevTime - a.nPrevBlockTime;
    const int64_t heightDiff = int64_t(prevHeight) - a.nHeight;
    return AsertRefTarget(refTarget, p.nPowTargetSpacing, timeDiff, heightDiff, powLimit, halfLife).GetCompact();
}

BOOST_AUTO_TEST_CASE(mainnet_asert_halflife_propagation)
{
    // REAL mainnet params (no hand-copied constants): the object actually built by CBrisviaMainParams.
    const auto chain = CChainParams::BrisviaMain();
    const Consensus::Params& params = chain->GetConsensus();

    // Pin the test to the live mainnet source of truth (fails loudly if any of these drifts).
    BOOST_REQUIRE(params.asertAnchorParams.has_value());
    BOOST_REQUIRE(params.fPowRandomX);
    BOOST_REQUIRE(!params.fPowNoRetargeting);            // mainnet retargets for real -> routes to ASERT
    BOOST_REQUIRE(!params.fPowAllowMinDifficultyBlocks); // no min-difficulty escape hatch
    BOOST_CHECK_EQUAL(params.nASERTHalfLife, 21600);     // <-- the value under audit
    BOOST_CHECK_EQUAL(params.nPowTargetSpacing, 120);
    BOOST_CHECK_EQUAL(params.asertAnchorParams->nBits, 0x1e7fffffu);
    BOOST_CHECK_EQUAL(UintToArith256(params.powLimit).GetCompact(), 0x207fffffu);

    const auto& anchor = *params.asertAnchorParams;
    const int64_t spacing = params.nPowTargetSpacing;   // 120
    const int64_t t0 = anchor.nPrevBlockTime;           // synthetic parent time = genesisTime - spacing
    arith_uint256 anchorTarget; anchorTarget.SetCompact(anchor.nBits);
    const arith_uint256 powLimitTarget = UintToArith256(params.powLimit);

    CBlockHeader pblock; // only read by the (disabled) min-difficulty rule; value is irrelevant on mainnet
    pblock.nTime = static_cast<uint32_t>(t0 + 1);

    // Drive the REAL top-level consensus entry point (GetNextWorkRequired) AND the pure-values variant, require
    // they agree, and return the nBits the node would demand for the block after (prevHeight, prevTime).
    auto nodeNextBits = [&](int prevHeight, int64_t prevTime) -> uint32_t {
        CBlockIndex prev;
        prev.nHeight = prevHeight;
        prev.nTime   = static_cast<uint32_t>(prevTime);
        const uint32_t viaTop    = GetNextWorkRequired(&prev, &pblock, params);                          // routing proof
        const uint32_t viaValues = GetNextASERTWorkRequiredFromValues(prevHeight, prevTime, &pblock, params);
        BOOST_CHECK_EQUAL(viaTop, viaValues);
        return viaTop;
    };

    // ---------- (a) blocks at the target pace: difficulty STABLE (target == anchor) ----------
    {
        const int prevHeight = 100;
        const int64_t prevTime = t0 + int64_t(prevHeight + 1) * spacing; // exactly on schedule -> exponent 0
        const uint32_t got = nodeNextBits(prevHeight, prevTime);
        BOOST_CHECK_EQUAL(got, anchor.nBits);                                            // stays at the anchor
        BOOST_CHECK_EQUAL(got, AsertRefNextBits(params, prevHeight, prevTime, 21600));   // oracle @ 21600
    }

    // ---------- (b) FAST blocks (60 s each): difficulty UP (target DOWN) ----------
    {
        const int prevHeight = 100;
        const int64_t prevTime = t0 + int64_t(prevHeight + 1) * 60;      // half the target pace
        const uint32_t got = nodeNextBits(prevHeight, prevTime);
        arith_uint256 target; target.SetCompact(got);
        BOOST_CHECK(target < anchorTarget);                                              // difficulty rose
        BOOST_CHECK_EQUAL(got, AsertRefNextBits(params, prevHeight, prevTime, 21600));
        // Propagation: the real path uses 21600, NOT 10800. A longer half-life reacts SOFTER (target moves less).
        const uint32_t bits10800 = AsertRefNextBits(params, prevHeight, prevTime, 10800);
        BOOST_CHECK(got != bits10800);
        arith_uint256 t10; t10.SetCompact(bits10800);
        BOOST_CHECK(target > t10);   // 21600 (real) drops less than a 3 h half-life would
    }

    // ---------- (c) SLOW blocks (240 s each): difficulty DOWN (target UP), never above powLimit ----------
    {
        const int prevHeight = 100;
        const int64_t prevTime = t0 + int64_t(prevHeight + 1) * 240;     // double the target pace
        const uint32_t got = nodeNextBits(prevHeight, prevTime);
        arith_uint256 target; target.SetCompact(got);
        BOOST_CHECK(target > anchorTarget);                                              // difficulty eased
        BOOST_CHECK(target <= powLimitTarget);
        BOOST_CHECK_EQUAL(got, AsertRefNextBits(params, prevHeight, prevTime, 21600));
        const uint32_t bits10800 = AsertRefNextBits(params, prevHeight, prevTime, 10800);
        BOOST_CHECK(got != bits10800);
        arith_uint256 t10; t10.SetCompact(bits10800);
        BOOST_CHECK(target < t10);   // 21600 (real) eases slower than a 3 h half-life would
    }

    // ---------- (d) STRONG stall (very long interval): strong recovery (target UP a lot) ----------
    {
        const int prevHeight = 10;
        const int64_t onSchedule = t0 + int64_t(prevHeight + 1) * spacing;
        const int64_t gap = 86400;                                        // a full day with no new blocks
        const int64_t prevTime = onSchedule + gap;
        const uint32_t got = nodeNextBits(prevHeight, prevTime);
        arith_uint256 target; target.SetCompact(got);
        BOOST_CHECK(target > anchorTarget);
        // At the 6 h half-life, a 1-day stall from height 10 gives ASERT exponent = 86400/21600 = 4, so the target
        // rises ~16x (2^4). (With a 3 h half-life it would be ~256x.) Assert a strong recovery (> 8x), not >64x.
        BOOST_CHECK((target >> 3) > anchorTarget);   // rose ~16x: a strong recovery, not a marginal nudge
        BOOST_CHECK(target < powLimitTarget);        // not clamped, so the differential below is meaningful
        BOOST_CHECK_EQUAL(got, AsertRefNextBits(params, prevHeight, prevTime, 21600));
        // The 6 h half-life recovers more slowly than a 3 h one would; 6 h was chosen for stability on a new small network.
        const uint32_t bits10800 = AsertRefNextBits(params, prevHeight, prevTime, 10800);
        BOOST_CHECK(got != bits10800);
        arith_uint256 t10; t10.SetCompact(bits10800);
        BOOST_CHECK(target < t10);   // 21600 (real) recovers less than a 3 h half-life would
    }
}

// Genesis PoW under RandomX (audit blocker CF-13 / C-04). The consensus guards pin the genesis SHA256d id and
// merkle root, but that does NOT prove the frozen nonce actually satisfies RandomX. A hard-coded genesis never
// re-traverses PoW at startup, so this proves it explicitly with the REAL mainnet genesis object: the validator
// runs RandomX over the 80-byte header under the INITIAL launch seed (height 0, no parent) and only returns VALID
// if the hash meets target(genesisBits). If the mined nonce were wrong, mainnet could not produce a valid genesis.
BOOST_AUTO_TEST_CASE(mainnet_genesis_randomx_pow)
{
    const auto chain = CChainParams::BrisviaMain();
    const Consensus::Params& params = chain->GetConsensus();
    const CBlock& genesis = chain->GenesisBlock();
    BOOST_REQUIRE(params.fPowRandomX);
    uint256 powHash;
    const PoWCheckResult r = CheckRandomXProofOfWorkContextual(genesis, /*pindexPrev=*/nullptr, /*nHeight=*/0, params, &powHash);
    BOOST_CHECK_EQUAL(static_cast<int>(r), static_cast<int>(PoWCheckResult::VALID));
    BOOST_CHECK(!powHash.IsNull());
}

BOOST_AUTO_TEST_SUITE_END()
