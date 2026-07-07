// Brisvia: functional test of the RandomX integration in HeadersSyncState.
// In presync and redownload, no header adds work nor is released without a VALID RandomX.
// The RandomX work is processed in bounded QUANTA (budget), draining the rest of the message with
// ContinuePendingHeaders(), and the "end of message" logic runs ONLY when the last header is consumed.
// Uses the Brisvia regtest (-brisviapow) and mines real RandomX headers; it stays below height 64
// (fixed initial seed -> a single light VM, fast mining).
#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <consensus/params.h>
#include <consensus/randomx_seed.h>
#include <headerssync.h>
#include <pow.h>
#include <test/util/setup_common.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <memory>
#include <vector>

namespace {
struct BrisviaHeadersSyncSetup : public TestingSetup {
    BrisviaHeadersSyncSetup() : TestingSetup{ChainType::REGTEST, {.extra_args = {"-brisviapow"}}} {}

    void GenerateBrisviaHeaders(std::vector<CBlockHeader>& headers, size_t count,
                                const uint256& start_hash, int64_t start_height,
                                uint32_t nTime0, uint32_t nBits, const uint256& merkle)
    {
        const Consensus::Params& params = Params().GetConsensus();
        uint256 prev = start_hash;
        for (size_t i = 0; i < count; ++i) {
            const int64_t height = start_height + int64_t(i) + 1;
            BOOST_REQUIRE(BrisviaUsesInitialSeed(static_cast<int>(height)));
            CBlockHeader h;
            h.nVersion = 1;
            h.hashPrevBlock = prev;
            h.hashMerkleRoot = merkle;
            h.nTime = nTime0 + static_cast<uint32_t>(i) + 1;
            h.nBits = nBits;
            for (uint32_t n = 0;; ++n) {
                h.nNonce = n;
                if (CheckRandomXHeaderWithResolvedContext(h, nBits, params.brisviaInitialSeed, params, nullptr) == PoWCheckResult::VALID) break;
            }
            headers.push_back(h);
            prev = h.GetHash();
        }
    }

    void BreakRandomX(CBlockHeader& h)
    {
        const Consensus::Params& params = Params().GetConsensus();
        for (uint32_t n = h.nNonce + 1;; ++n) {
            h.nNonce = n;
            if (CheckRandomXHeaderWithResolvedContext(h, h.nBits, params.brisviaInitialSeed, params, nullptr) != PoWCheckResult::VALID) break;
        }
    }

    // Simulates net_processing's scheduling: delivers the batch and drains all pending quanta with
    // ContinuePendingHeaders(), accumulating the released headers. Verifies the invariant more_internal_work =>
    // !request_more (nothing is requested from the peer while local work remains).
    HeadersSyncState::ProcessingResult DrainProcess(HeadersSyncState& hss,
                                                    const std::vector<CBlockHeader>& headers, bool full)
    {
        auto result = hss.ProcessNextHeaders(headers, full);
        std::vector<CBlockHeader> acc = std::move(result.pow_validated_headers);
        while (result.more_internal_work) {
            BOOST_CHECK(!result.request_more);
            BOOST_CHECK(hss.HasPendingHeaders());
            result = hss.ContinuePendingHeaders();
            for (auto& h : result.pow_validated_headers) acc.push_back(h);
        }
        result.pow_validated_headers = std::move(acc);
        return result;
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(brisvia_headerssync_tests, BrisviaHeadersSyncSetup)

// Valid chain syncs (presync -> redownload -> final with all headers); an invalid RandomX aborts and does not
// credit. It is drained in quanta.
BOOST_AUTO_TEST_CASE(brisvia_headers_sync_randomx)
{
    const CBlockHeader genesis = Params().GenesisBlock();
    const uint256 genhash = genesis.GetHash();
    const CBlockIndex* chain_start = WITH_LOCK(::cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(genhash));
    BOOST_REQUIRE(chain_start != nullptr);
    BOOST_REQUIRE(Params().GetConsensus().fPowRandomX);

    const arith_uint256 min_work = GetBlockProof(*chain_start) * 20;

    std::vector<CBlockHeader> chain;
    GenerateBrisviaHeaders(chain, 40, genhash, 0, genesis.nTime, genesis.nBits, ArithToUint256(7));

    // (1) Valid chain: presync reaches the work -> REDOWNLOAD; redownload -> FINAL with ALL the headers.
    {
        std::unique_ptr<HeadersSyncState> hss(new HeadersSyncState(0, Params().GetConsensus(), chain_start, min_work));
        auto result = DrainProcess(*hss, chain, true);
        BOOST_CHECK(result.success);
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::REDOWNLOAD);

        result = DrainProcess(*hss, chain, true); // redownload
        BOOST_CHECK(result.success);
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::FINAL);
        BOOST_CHECK_EQUAL(result.pow_validated_headers.size(), chain.size());
    }

    // (2) INVALID RandomX (that chains) -> aborts the sync, releases nothing.
    {
        std::vector<CBlockHeader> short_chain(chain.begin(), chain.begin() + 10);
        BreakRandomX(short_chain.back());
        std::unique_ptr<HeadersSyncState> hss(new HeadersSyncState(0, Params().GetConsensus(), chain_start, min_work));
        auto result = DrainProcess(*hss, short_chain, true);
        BOOST_CHECK(!result.success);
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::FINAL);
        BOOST_CHECK(result.pow_validated_headers.empty());
    }
}

// Budget per quanta. The batch is cut at the per-call maximum; the rest is drained with
// ContinuePendingHeaders(); while local work remains, nothing is requested from the peer.
BOOST_AUTO_TEST_CASE(brisvia_headers_sync_quantum)
{
    const CBlockHeader genesis = Params().GenesisBlock();
    const uint256 genhash = genesis.GetHash();
    const CBlockIndex* chain_start = WITH_LOCK(::cs_main, return m_node.chainman->m_blockman.LookupBlockIndex(genhash));
    BOOST_REQUIRE(chain_start != nullptr);

    std::vector<CBlockHeader> chain; // 40 headers > quantum (32)
    GenerateBrisviaHeaders(chain, 40, genhash, 0, genesis.nTime, genesis.nBits, ArithToUint256(9));

    // (A) Quantum cut: the first call processes one quantum (<40), leaves pending, more_internal_work, does NOT
    // request from the peer, stays PRESYNC. High minimum work so it does not transition.
    {
        const arith_uint256 min_work_high = GetBlockProof(*chain_start) * 100000;
        std::unique_ptr<HeadersSyncState> hss(new HeadersSyncState(0, Params().GetConsensus(), chain_start, min_work_high));
        auto r = hss->ProcessNextHeaders(chain, true);
        BOOST_CHECK(r.success);
        BOOST_CHECK(r.more_internal_work);      // it was cut: headers of the same message remain
        BOOST_CHECK(!r.request_more);           // nothing is requested from the peer while there is local work
        BOOST_CHECK(hss->HasPendingHeaders());
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::PRESYNC);

        r = hss->ContinuePendingHeaders();      // drain the rest
        BOOST_CHECK(r.success);
        BOOST_CHECK(!r.more_internal_work);
        BOOST_CHECK(!hss->HasPendingHeaders());
        BOOST_CHECK(r.request_more);            // full message + insufficient work -> keeps requesting
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::PRESYNC);
    }

    // (B) CRITICAL POINT: the threshold is reached mid-message, but the transition to REDOWNLOAD happens ONLY
    // when the last header is consumed (not mid-message).
    {
        const arith_uint256 min_work_low = GetBlockProof(*chain_start) * 5; // reached in ~5 headers
        std::unique_ptr<HeadersSyncState> hss(new HeadersSyncState(0, Params().GetConsensus(), chain_start, min_work_low));
        auto r = hss->ProcessNextHeaders(chain, true);
        BOOST_CHECK(r.success);
        BOOST_CHECK(r.more_internal_work);
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::PRESYNC); // has NOT transitioned yet
        r = hss->ContinuePendingHeaders();
        BOOST_CHECK(r.success);
        BOOST_CHECK(!r.more_internal_work);
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::REDOWNLOAD); // only when the message is exhausted
        BOOST_CHECK(r.request_more);
    }

    // (C) Invalid RandomX in a header AFTER the first quantum -> aborts at that header, does not credit, cleans up.
    {
        std::vector<CBlockHeader> tampered = chain;
        BreakRandomX(tampered[34]); // height 35 (index 34), after the first quantum of 32
        const arith_uint256 min_work_high = GetBlockProof(*chain_start) * 100000;
        std::unique_ptr<HeadersSyncState> hss(new HeadersSyncState(0, Params().GetConsensus(), chain_start, min_work_high));
        auto r = hss->ProcessNextHeaders(tampered, true);
        BOOST_CHECK(r.success);                 // first quantum (0..31) valid
        BOOST_CHECK(r.more_internal_work);
        r = hss->ContinuePendingHeaders();      // processes 32..34 -> fails at 34 (RandomX)
        BOOST_CHECK(!r.success);
        BOOST_CHECK(hss->GetState() == HeadersSyncState::State::FINAL);
        BOOST_CHECK(!hss->HasPendingHeaders()); // pending batch cleaned up
    }
}

BOOST_AUTO_TEST_SUITE_END()
