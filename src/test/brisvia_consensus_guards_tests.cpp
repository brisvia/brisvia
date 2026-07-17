// Brisvia - MAINNET consensus guards.
//
// These are tripwires: unit tests that FAIL LOUDLY if anyone changes a mainnet consensus
// rule (genesis, RandomX activation, difficulty flags, network identity, address prefixes,
// emission schedule, money cap, clean-start assumptions) without meaning to. They pin the
// canonical values of ChainType::BRISVIA_MAIN (class CBrisviaMainParams) so an accidental
// edit to kernel/chainparams.cpp can never silently ship a hard fork.
//
// If one of these breaks, DO NOT relax the test to make it green: either the change was a
// mistake (revert it) or it is an intentional, coordinated consensus change (update the
// frozen value here on purpose, in the same commit, with an explanation).
#include <chainparamsbase.h>
#include <consensus/amount.h>
#include <consensus/params.h>
#include <kernel/chainparams.h>
#include <primitives/block.h>
#include <uint256.h>
#include <util/chaintype.h>
#include <util/strencodings.h>
#include <tinyformat.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(brisvia_consensus_guards_tests)

// Guard 1: BRISVIA_MAIN is a REAL-money chain (IsTestChain == false), while the Brisvia
// testnet and regtest must stay test chains (IsTestChain == true). A regression here would
// turn mainnet into a "test" chain (relaxed rules, wrong policy defaults).
BOOST_AUTO_TEST_CASE(is_test_chain_classification)
{
    const auto main = CChainParams::BrisviaMain();
    BOOST_CHECK_MESSAGE(!main->IsTestChain(), "BRISVIA_MAIN must NOT be a test chain");
    BOOST_CHECK_EQUAL(static_cast<int>(main->GetChainType()), static_cast<int>(ChainType::BRISVIA_MAIN));

    const auto tnet = CChainParams::BrisviaTestNet();
    BOOST_CHECK_MESSAGE(tnet->IsTestChain(), "BRISVIA_TESTNET must be a test chain");

    const auto regtest = CChainParams::RegTest(CChainParams::RegTestOptions{});
    BOOST_CHECK_MESSAGE(regtest->IsTestChain(), "REGTEST must be a test chain");
}

// Guard 2: the mainnet genesis block is FROZEN. Any change to the phrase, anchor, time,
// nonce, bits or merkle root produces a different genesis => a different (incompatible)
// network. These are the irreproducible launch constants.
BOOST_AUTO_TEST_CASE(mainnet_genesis_frozen)
{
    const auto params = CChainParams::BrisviaMain();
    const Consensus::Params& c = params->GetConsensus();
    const CBlock& genesis = params->GenesisBlock();

    BOOST_CHECK_EQUAL(c.hashGenesisBlock.GetHex(),
                      "aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7");
    BOOST_CHECK_EQUAL(genesis.GetHash().GetHex(),
                      "aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7");
    BOOST_CHECK_EQUAL(genesis.hashMerkleRoot.GetHex(),
                      "c17012d733986f74e43d3a2852646e44c5d3fc9f0b45d165ac8998d96b6eb98d");
    BOOST_CHECK_EQUAL(genesis.nTime, 1785596400u);   // 2026-08-01 15:00:00 UTC (Aug 1, 12:00 ART)
    BOOST_CHECK_EQUAL(genesis.nNonce, 79118u);
    BOOST_CHECK_EQUAL(genesis.nBits, 0x1e0fffffu);

    // The ASERT anchor must ride on the genesis bits/height (difficulty seed of the network).
    BOOST_REQUIRE(c.asertAnchorParams.has_value());
    BOOST_CHECK_EQUAL(c.asertAnchorParams->nHeight, 0);
    BOOST_CHECK_EQUAL(c.asertAnchorParams->nBits, 0x1e0fffffu);
}

// Guard 3: RandomX PoW is active on mainnet. If this flips to false, the chain would fall
// back to SHA256d and reject every RandomX-mined block (total consensus break).
BOOST_AUTO_TEST_CASE(randomx_active)
{
    const auto params = CChainParams::BrisviaMain();
    const Consensus::Params& c = params->GetConsensus();
    BOOST_CHECK_MESSAGE(c.fPowRandomX, "mainnet must use RandomX PoW");
    // Initial RandomX seed is part of consensus (must not drift).
    BOOST_CHECK_EQUAL(c.brisviaInitialSeed.GetHex(),
                      "5454545454545454545454545454545454545454545454545454545454545454");
}

// Guard 4: difficulty flags. Mainnet must have REAL retargeting (fPowNoRetargeting == false)
// and NO min-difficulty exception (fPowAllowMinDifficultyBlocks == false). Flipping either
// one would let miners mint blocks at the floor difficulty.
BOOST_AUTO_TEST_CASE(difficulty_flags)
{
    const auto params = CChainParams::BrisviaMain();
    const Consensus::Params& c = params->GetConsensus();
    BOOST_CHECK_MESSAGE(!c.fPowAllowMinDifficultyBlocks, "mainnet must NOT allow min-difficulty blocks");
    BOOST_CHECK_MESSAGE(!c.fPowNoRetargeting, "mainnet must retarget difficulty (ASERT)");
    BOOST_CHECK_EQUAL(c.nPowTargetSpacing, 120);
    BOOST_CHECK_EQUAL(c.nASERTHalfLife, 21600);
}

// Guard 5: network identity. Magic bytes "BRV1", P2P port 9342, RPC port 9338 and datadir
// "brisvia-mainnet". These keep the mainnet isolated from Bitcoin and from the Brisvia
// testnet; a collision here would let foreign peers/messages cross networks.
BOOST_AUTO_TEST_CASE(network_identity)
{
    const auto params = CChainParams::BrisviaMain();
    const auto& magic = params->MessageStart();
    BOOST_CHECK_EQUAL(magic[0], 0x42); // 'B'
    BOOST_CHECK_EQUAL(magic[1], 0x52); // 'R'
    BOOST_CHECK_EQUAL(magic[2], 0x56); // 'V'
    BOOST_CHECK_EQUAL(magic[3], 0x31); // '1'  -> "BRV1"
    BOOST_CHECK_EQUAL(params->GetDefaultPort(), 9342);

    const auto base = CreateBaseChainParams(ChainType::BRISVIA_MAIN);
    BOOST_CHECK_EQUAL(base->RPCPort(), 9338);
    BOOST_CHECK_EQUAL(base->DataDir(), "brisvia-mainnet");
}

// Guard 6: address prefixes. base58 PUBKEY=25, SCRIPT=85, SECRET=153 and bech32 HRP "brv".
// Changing these would silently break every wallet address and every derived key format.
BOOST_AUTO_TEST_CASE(address_prefixes)
{
    const auto params = CChainParams::BrisviaMain();

    const auto& pubkey = params->Base58Prefix(CChainParams::PUBKEY_ADDRESS);
    BOOST_REQUIRE_EQUAL(pubkey.size(), 1u);
    BOOST_CHECK_EQUAL(pubkey[0], 25);

    const auto& script = params->Base58Prefix(CChainParams::SCRIPT_ADDRESS);
    BOOST_REQUIRE_EQUAL(script.size(), 1u);
    BOOST_CHECK_EQUAL(script[0], 85);

    const auto& secret = params->Base58Prefix(CChainParams::SECRET_KEY);
    BOOST_REQUIRE_EQUAL(secret.size(), 1u);
    BOOST_CHECK_EQUAL(secret[0], 153);

    BOOST_CHECK_EQUAL(params->Bech32HRP(), "brv");
}

// Guard 7: emission + money cap. Finite Bitcoin-style schedule: 50 BRVA initial subsidy,
// halving every 1,000,000 blocks, NO perpetual tail, 100M hard cap. A change here alters
// monetary policy for everyone.
BOOST_AUTO_TEST_CASE(emission_and_money_cap)
{
    const auto params = CChainParams::BrisviaMain();
    const Consensus::Params& c = params->GetConsensus();

    BOOST_CHECK_EQUAL(c.nSubsidyHalvingInterval, 1000000);
    BOOST_CHECK_EQUAL(c.nBrisviaInitialSubsidy, 50 * COIN);
    BOOST_CHECK_EQUAL(c.nBrisviaTailSubsidy, 0);      // finite emission, no tail
    BOOST_CHECK_MESSAGE(c.fBrisviaSubsidy, "mainnet must use the Brisvia emission schedule");

    // 100,000,000 BRVA hard cap (MAX_MONEY is a compile-time consensus constant).
    BOOST_CHECK_EQUAL(MAX_MONEY, 100000000 * COIN);
}

// Guard 8: clean start. A brand-new chain must NOT inherit Bitcoin's accumulated work or an
// assumed-valid checkpoint. Both must be empty so nodes validate from genesis.
BOOST_AUTO_TEST_CASE(clean_start_no_inherited_checkpoints)
{
    const auto params = CChainParams::BrisviaMain();
    const Consensus::Params& c = params->GetConsensus();
    BOOST_CHECK_MESSAGE(c.nMinimumChainWork.IsNull(), "nMinimumChainWork must be empty (fresh chain)");
    BOOST_CHECK_MESSAGE(c.defaultAssumeValid.IsNull(), "defaultAssumeValid must be empty (fresh chain)");
}

// Guard: the compiled bootstrap list. There is no DNS seed on mainnet (vSeeds is empty), so vFixedSeeds
// is the ONLY way a fresh client finds the network. A missing address is a seed node nobody can reach; an
// empty list is a launch where no client ever connects to anything.
//
// This decodes what the BINARY actually carries, not what chainparamsseeds.h looks like: on 2026-07-15 the
// source listed two of the three seed nodes and the third (Oracle-2) was invisible to every client. It also
// pins the port, because a testnet address or a wrong port here fails exactly the same way — silently, and
// only on launch day.
BOOST_AUTO_TEST_CASE(mainnet_fixed_seeds_are_the_three_seed_nodes)
{
    const auto params = CChainParams::BrisviaMain();
    const std::vector<uint8_t>& raw = params->FixedSeeds();

    // Serialised form, one entry per 8 bytes: 0x01 (IPv4), 0x04 (len), 4 IP bytes, 2 port bytes big-endian.
    BOOST_REQUIRE_MESSAGE(!raw.empty(), "mainnet has NO fixed seeds: a fresh client would find nobody");
    BOOST_REQUIRE_MESSAGE(raw.size() % 8 == 0, "fixed seed list is not a whole number of IPv4 entries");

    std::vector<std::string> got;
    for (size_t i = 0; i + 8 <= raw.size(); i += 8) {
        BOOST_CHECK_MESSAGE(raw[i] == 0x01 && raw[i + 1] == 0x04, "entry is not IPv4 (0x01,0x04)");
        const uint16_t port = static_cast<uint16_t>((raw[i + 6] << 8) | raw[i + 7]);
        got.push_back(strprintf("%d.%d.%d.%d:%d", raw[i + 2], raw[i + 3], raw[i + 4], raw[i + 5], port));
    }

    const std::vector<std::string> expected{
        "187.77.240.145:9342",   // Hostinger
        "129.80.250.36:9342",    // Oracle-1
        "129.159.108.102:9342",  // Oracle-2
    };

    BOOST_CHECK_MESSAGE(got.size() == expected.size(),
                        strprintf("mainnet must ship exactly %d fixed seeds, found %d", expected.size(), got.size()));
    for (const auto& want : expected) {
        BOOST_CHECK_MESSAGE(std::find(got.begin(), got.end(), want) != got.end(),
                            strprintf("seed node %s is missing from the compiled list: no client can reach it", want));
    }
    // No duplicates, and nothing from another network sneaking in on the mainnet port.
    std::vector<std::string> uniq = got;
    std::sort(uniq.begin(), uniq.end());
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
    BOOST_CHECK_MESSAGE(uniq.size() == got.size(), "the fixed seed list has duplicate addresses");
    for (const auto& g : got) {
        BOOST_CHECK_MESSAGE(g.find(":9342") != std::string::npos,
                            strprintf("fixed seed %s is not on the mainnet port 9342", g));
    }
}

// The testnet list must never leak into the mainnet build, and vice versa.
BOOST_AUTO_TEST_CASE(mainnet_and_testnet_fixed_seeds_are_different)
{
    const auto main = CChainParams::BrisviaMain();
    const auto tnet = CChainParams::BrisviaTestNet();
    BOOST_CHECK_MESSAGE(main->FixedSeeds() != tnet->FixedSeeds(),
                        "mainnet and testnet ship the SAME fixed seeds: one of the two builds is wrong");
}

BOOST_AUTO_TEST_SUITE_END()
