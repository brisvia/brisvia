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
#include <chainparams.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <primitives/block.h>
#include <test/util/setup_common.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/time.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

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

BOOST_AUTO_TEST_SUITE_END()
