// Brisvia — block subsidy (emission). A PURE, self-contained function: it does not depend on the Bitcoin tree,
// is tested on its own, and GetBlockSubsidy (validation.cpp) calls it when Brisvia's parameters are set.
//
// Rules:
//  - Height 0 (genesis): 0. A zero reward unambiguously signals there is no premine.
//  - Halvings counted FROM BLOCK 1 (genesis excluded): halvings = (nHeight-1) / halvingInterval.
//    Heights 1..halvingInterval pay the initial reward; the first halving lands at halvingInterval+1.
//  - Tail: when the reduced reward falls below tailSubsidy, tailSubsidy is paid from then on. On Brisvia
//    mainnet tailSubsidy = 0, so emission is FINITE (Bitcoin-style): 50 -> 25 -> 12.5 -> ... -> 0.
//    Total ~100M, last coin around block 33M (~125 years).
//  - Units: the smallest subunit (satoshi-equivalent). The caller passes initialSubsidy/tailSubsidy in COIN.
#ifndef BRISVIA_CONSENSUS_EMISSION_H
#define BRISVIA_CONSENSUS_EMISSION_H

#include <cstdint>

inline int64_t BrisviaGetBlockSubsidy(int nHeight, int64_t initialSubsidy, int64_t tailSubsidy,
                                      int64_t halvingInterval)
{
    if (nHeight <= 0) return 0;                       // genesis: no reward
    if (halvingInterval <= 0) return tailSubsidy;     // defensive guard
    const int64_t halvings = (static_cast<int64_t>(nHeight) - 1) / halvingInterval; // from block 1
    if (halvings >= 63) return tailSubsidy;           // avoid undefined shift; already in the tail
    const int64_t subsidy = initialSubsidy >> halvings;
    return subsidy < tailSubsidy ? tailSubsidy : subsidy;
}

#endif // BRISVIA_CONSENSUS_EMISSION_H
