// Brisvia - genesis block builder. Shared by chainparams.cpp and the tests (same byte-for-byte construction,
// so the mined nNonce always reproduces).
//
// Coinbase: the phrase in the scriptSig (Bitcoin style: two pushes + text); one 0-value output with
// OP_RETURN <32-byte anchor>. Zero reward (fair launch, no one's key); the anchor commits the public,
// unpredictable launch data. The genesis PoW is satisfied by RANDOMX (not SHA256d), so the already-mined
// nNonce is passed in.
#ifndef BRISVIA_CONSENSUS_GENESIS_H
#define BRISVIA_CONSENSUS_GENESIS_H

#include <consensus/merkle.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <cstdint>
#include <cstring>
#include <vector>

inline CBlock CreateBrisviaGenesisBlock(const char* pszTimestamp, const unsigned char anchor[32],
                                        uint32_t nTime, uint32_t nNonce, uint32_t nBits, int32_t nVersion)
{
    CScript scriptSig = CScript() << 486604799 << CScriptNum(4)
        << std::vector<unsigned char>((const unsigned char*)pszTimestamp,
                                      (const unsigned char*)pszTimestamp + std::strlen(pszTimestamp));
    CMutableTransaction txNew;
    txNew.version = 1;
    txNew.vin.resize(1);
    txNew.vout.resize(1);
    txNew.vin[0].scriptSig = scriptSig;
    txNew.vout[0].nValue = 0; // zero reward: no premine, no reserved reward
    txNew.vout[0].scriptPubKey = CScript() << OP_RETURN << std::vector<unsigned char>(anchor, anchor + 32);

    CBlock genesis;
    genesis.nVersion = nVersion;
    genesis.hashPrevBlock.SetNull();
    genesis.nTime = nTime;
    genesis.nBits = nBits;
    genesis.nNonce = nNonce;
    genesis.vtx.push_back(MakeTransactionRef(std::move(txNew)));
    genesis.hashMerkleRoot = BlockMerkleRoot(genesis);
    return genesis;
}

#endif // BRISVIA_CONSENSUS_GENESIS_H
