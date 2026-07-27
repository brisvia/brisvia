// Brisvia - MAINNET fair-launch barrier regression tests.
//
// These pin the behaviour of the pre-T0 fair-launch barrier so a later merge cannot silently
// drop it. The barrier (BrisviaPostGenesisBeforeLaunch, used by ContextualCheckBlockHeader and
// AcceptBlock) rejects any post-genesis block on Brisvia mainnet while the local clock is still
// before the launch time T0 (== the genesis timestamp), with reason "brisvia-before-launch" and
// result BLOCK_TIME_FUTURE (temporary, NOT a permanent invalidity), and accepts the SAME object
// once the clock reaches T0.
//
// The test vector is a real block-1 HEADER mined on the Brisvia mainnet genesis (aa6bc268), with
// nTime = T0+1, valid RandomX proof of work and the ASERT nBits expected at height 1. Reusing a
// pre-mined header keeps the test fast (no mining) while exercising the full acceptance path.
#include <chain.h>
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <node/blockstorage.h>
#include <primitives/block.h>
#include <sync.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <memory>

namespace {
// Brisvia mainnet launch time == the genesis timestamp (chainparams: genesisTime).
constexpr int64_t BRISVIA_T0{1785596400};

// Real block-1 header (80 bytes, 160 hex chars): prev == genesis aa6bc268, nTime == T0+1,
// valid RandomX PoW, correct ASERT nBits for height 1. Produced by mining one block against a
// Brisvia mainnet node and taking getblock <hash> 0 (first 160 hex chars = the header).
const std::string BRISVIA_BLOCK1_HEADER_HEX{
    "00000020f7baf70a8928f8087cd3083d0395e3487e7bca3ae39ae3f2f4a99a3368c26baa"
    "2f19453b15117441b46c59038fa79fac3c2052b5cd299613f6abc4e01bf58fc0f1096e6a"
    "ffff0f1e265d0200"};

// The SAME block, full serialization (header + coinbase body). Used to exercise the block-body
// barrier in AcceptBlock on an already-known header.
const std::string BRISVIA_BLOCK1_FULL_HEX{
    "00000020f7baf70a8928f8087cd3083d0395e3487e7bca3ae39ae3f2f4a99a3368c26baa"
    "2f19453b15117441b46c59038fa79fac3c2052b5cd299613f6abc4e01bf58fc0f1096e6a"
    "ffff0f1e265d020001020000000001010000000000000000000000000000000000000000"
    "000000000000000000000000ffffffff1451080100000000000000092f42726973766961"
    "2fffffffff0200f2052a0100000016001449272552f2692215d5981c6ac8161130f92c6d"
    "e50000000000000000266a24aa21a9ede2f61c3f71d1defd3fa999dfa36953755c690689"
    "799962b48bebd836974e8cf9012000000000000000000000000000000000000000000000"
    "0000000000000000000000000000"};

struct BrisviaLaunchSetup : public TestingSetup {
    BrisviaLaunchSetup() : TestingSetup{ChainType::BRISVIA_MAIN} {}
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(brisvia_launch_barrier_tests, BrisviaLaunchSetup)

// A valid post-genesis header is rejected before T0 with the exact barrier reason/result, and the
// SAME header is accepted at exactly T0 (the strict `now < T0` boundary).
BOOST_AUTO_TEST_CASE(header_rejected_before_T0_and_accepted_at_T0)
{
    CBlockHeader header;
    BOOST_REQUIRE(DecodeHexBlockHeader(header, BRISVIA_BLOCK1_HEADER_HEX));
    // The vector must build on the mainnet genesis, or it would fail for the wrong reason.
    BOOST_REQUIRE(header.hashPrevBlock == Params().GenesisBlock().GetHash());

    // now = T0 - 1: rejected by the fair-launch barrier, temporarily (not permanently invalid).
    SetMockTime(BRISVIA_T0 - 1);
    {
        BlockValidationState state;
        const CBlockIndex* pindex{nullptr};
        const bool ok{Assert(m_node.chainman)->ProcessNewBlockHeaders({{header}}, /*min_pow_checked=*/true, state, &pindex)};
        BOOST_CHECK_MESSAGE(!ok, "post-genesis header must be rejected before T0");
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "brisvia-before-launch");
        BOOST_CHECK(state.GetResult() == BlockValidationResult::BLOCK_TIME_FUTURE);
    }

    // now = T0: the exact same header is accepted (proves the rejection was temporary and the
    // boundary is strict at T0).
    SetMockTime(BRISVIA_T0);
    {
        BlockValidationState state;
        const CBlockIndex* pindex{nullptr};
        const bool ok{Assert(m_node.chainman)->ProcessNewBlockHeaders({{header}}, /*min_pow_checked=*/true, state, &pindex)};
        BOOST_CHECK_MESSAGE(ok, "same header must be accepted at T0: " + state.ToString());
        BOOST_REQUIRE(pindex != nullptr);
        BOOST_CHECK_EQUAL(pindex->nHeight, 1);
    }

    SetMockTime(0);
}

// The block-BODY barrier in AcceptBlock. This is the case a build without the barrier could have
// created: the HEADER is already known in the block index, so AcceptBlockHeader returns it WITHOUT
// re-running ContextualCheckBlockHeader. The body must still be rejected before T0 -- temporarily,
// never permanently -- and the SAME body must be accepted at T0 on the SAME CBlockIndex.
BOOST_AUTO_TEST_CASE(body_barrier_rejects_before_T0_on_a_known_header_then_accepts_at_T0)
{
    CBlock block;
    BOOST_REQUIRE(DecodeHexBlk(block, BRISVIA_BLOCK1_FULL_HEX));
    BOOST_REQUIRE(block.hashPrevBlock == Params().GenesisBlock().GetHash());
    const uint256 block_hash{block.GetHash()};
    auto& chainman{*Assert(m_node.chainman)};

    // Step 1 (T0): accept ONLY the header, so it becomes a known CBlockIndex at height 1 with no body
    // and no failure bit -- exactly the "header known, body missing" state.
    SetMockTime(BRISVIA_T0);
    {
        BlockValidationState state;
        const CBlockIndex* pindex{nullptr};
        BOOST_REQUIRE(chainman.ProcessNewBlockHeaders({{block.GetBlockHeader()}}, /*min_pow_checked=*/true, state, &pindex));
        BOOST_REQUIRE(pindex != nullptr);
        BOOST_CHECK_EQUAL(pindex->nHeight, 1);
        BOOST_CHECK(!(pindex->nStatus & BLOCK_HAVE_DATA));
        BOOST_CHECK(!(pindex->nStatus & BLOCK_FAILED_MASK));
    }

    // Step 2 (T0-1): send the full BODY. The body barrier in AcceptBlock must reject it temporarily.
    SetMockTime(BRISVIA_T0 - 1);
    {
        bool new_block{false};
        const bool ok{chainman.ProcessNewBlock(std::make_shared<const CBlock>(block), /*force_processing=*/true, /*min_pow_checked=*/true, &new_block)};
        BOOST_CHECK_MESSAGE(!ok, "the body must be rejected before T0 even though the header is known");
        const CBlockIndex* pindex{WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block_hash))};
        BOOST_REQUIRE(pindex != nullptr);
        BOOST_CHECK(!(pindex->nStatus & BLOCK_HAVE_DATA));   // the body was NOT stored
        BOOST_CHECK(!(pindex->nStatus & BLOCK_FAILED_MASK)); // NOT permanently invalidated
        BOOST_CHECK_EQUAL(WITH_LOCK(::cs_main, return chainman.ActiveChain().Height()), 0);
    }

    // Step 3 (T0): send the SAME body on the SAME index -> accepted, connected, data stored, not failed.
    SetMockTime(BRISVIA_T0);
    {
        bool new_block{false};
        const bool ok{chainman.ProcessNewBlock(std::make_shared<const CBlock>(block), /*force_processing=*/true, /*min_pow_checked=*/true, &new_block)};
        BOOST_CHECK_MESSAGE(ok, "the same body must be accepted at T0");
        BOOST_CHECK_EQUAL(WITH_LOCK(::cs_main, return chainman.ActiveChain().Height()), 1);
        const CBlockIndex* pindex{WITH_LOCK(::cs_main, return chainman.m_blockman.LookupBlockIndex(block_hash))};
        BOOST_REQUIRE(pindex != nullptr);
        BOOST_CHECK(pindex->nStatus & BLOCK_HAVE_DATA);
        BOOST_CHECK(!(pindex->nStatus & BLOCK_FAILED_MASK));
        BOOST_CHECK(WITH_LOCK(::cs_main, return chainman.ActiveChain().Tip()->GetBlockHash()) == block_hash);
    }

    SetMockTime(0);
}

BOOST_AUTO_TEST_SUITE_END()
