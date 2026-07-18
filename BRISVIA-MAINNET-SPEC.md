# Brisvia Mainnet — Canonical Network Specification

**Single source of truth for the Brisvia (BRVA) main network consensus and identity.**
Every component (node, desktop app, pool, explorer, monitors, seeds) must match these values exactly.
If any component diverges from this file, it is a bug in that component, not in this file.

- Status: **frozen for the 2026-08-01 15:00:00 UTC mainnet launch.**
- Consensus last changed on `brisvia/brisvia@main`: commit `bb72054a2903` (2026-07-14, "recalibrate mainnet genesis difficulty to 0x1e0fffff").
- Verification date: 2026-07-17 (source-level review against the exact code that built the 1.0.7 release).

---

## 1. Network identity

| Field | Value |
|---|---|
| Coin name / ticker | Brisvia / BRVA |
| Chain type | `ChainType::BRISVIA_MAIN` (`-chain=brisvia`) |
| Magic bytes | `BRV1` = `0x42 0x52 0x56 0x31` |
| P2P port | **9342** (moved off Litecoin's 9333; done in 1.0.8 — see §9) |
| RPC port (node default) | **9338** (localhost only; moved off Litecoin's 9332 — see §9) |
| bech32 HRP | `brv` (addresses `brv1...`) |
| base58 PUBKEY_ADDRESS | 25 |
| base58 SCRIPT_ADDRESS | 85 |
| base58 SECRET_KEY | 153 |
| EXT_PUBLIC_KEY | `0x0488B21E` |
| EXT_SECRET_KEY | `0x0488ADE4` |

## 2. Genesis block (immutable)

| Field | Value |
|---|---|
| Genesis hash | `aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7` |
| Merkle root | `c17012d733986f74e43d3a2852646e44c5d3fc9f0b45d165ac8998d96b6eb98d` |
| nTime | 1785596400 = **2026-08-01 15:00:00 UTC** (= 12:00 ART) |
| nNonce | 79118 |
| nBits | `0x1e0fffff` |
| nVersion | 1 |
| Coinbase phrase | "No privilege at genesis: an open network, mined and sustained by the people." |
| ASERT anchor byte | `0xB3` (mainnet; testnet uses `0xB2`) |
| Genesis reward | 0 (unspendable — a zero reward unambiguously signals **no premine**) |

## 3. Proof of Work

| Field | Value |
|---|---|
| Algorithm | RandomX (`fPowRandomX = true`) |
| RandomX submodule | `tevador/RandomX @ 6c4340ba4561aec9a3611c1aedf9931239777fb3` |
| RandomX initial seed | `0x5454...54` (32 bytes of `0x54`) |
| Target spacing | 120 s (2 min) |
| powLimit | `0x207fffff` (SetCompact floor) |
| Difficulty algorithm | ASERT |
| **ASERT half-life** | **21600 s (6 h)** — confirmed, no remnant of the 3 h branch |
| ASERT anchor | height 0, nBits `0x1e0fffff`, prevBlockTime = nTime − 120 |
| nPowTargetTimespan | 14 days (legacy field; ASERT retargets per block) |
| fPowAllowMinDifficultyBlocks | false |
| fPowNoRetargeting | false |

## 4. Emission (finite, Bitcoin-style)

Pure function `BrisviaGetBlockSubsidy` (`src/consensus/brisvia_emission.h`), selected by `fBrisviaSubsidy = true`.

- Height 0 (genesis): **0** (no premine).
- Initial subsidy: **50 BRVA/block**.
- Halvings counted from block 1: `halvings = (nHeight − 1) / 1000000`.
- Halving interval: **1,000,000 blocks**.
- Tail subsidy: **0** → finite emission (50 → 25 → 12.5 → … → 0).
- Total supply ≈ **100,000,000 BRVA** (last coin ~block 33M, ~125 years).
- `MAX_MONEY = 100,000,000 * COIN` (`COIN = 100,000,000`, i.e. 8 decimals).
- Verified: the sum of the geometric series (50 × 1M × 2) = 100M, at or below the MAX_MONEY ceiling.

## 5. Inherited-rule activation (a fresh chain, no Bitcoin history)

| Field | Value |
|---|---|
| BIP34 / BIP65 / BIP66 / CSV height | 1 |
| SegWit height | 0 |
| Taproot | always active (from block 0) |
| nMinimumChainWork | 0 (empty) |
| defaultAssumeValid | 0 (empty) |
| Checkpoints | none |
| script_flag_exceptions | none |
| IsTestChain() | **false** for BRISVIA_MAIN (correctly treated as a real chain) |

## 6. Wallet derivation

- Scheme: BIP84 native SegWit.
- Path: **`m/84'/9339'/0'`** (coin_type **9339'**, free in SLIP-0044; NOT Bitcoin's `0'`).
- Enforced by tests + golden vectors in the desktop app (`src-tauri/src/lib.rs`).

## 7. Provenance of the 1.0.7 release

| Field | Value |
|---|---|
| Desktop tag | `v1.0.7` → SHA `3e62c5572629e9e01443234b3cddc455d6441009` |
| Core repo (node) | `brisvia/brisvia@main`, HEAD at build time `22de9153e65d` |
| RandomX submodule | `6c4340ba4561aec9a3611c1aedf9931239777fb3` |
| Node built by | GitHub Actions (each build compiles bitcoind from source; nothing pre-existing survives) |

**Build-time genesis guard** (`build-*.yml`): every build runs the freshly-compiled node and fails unless
`getblockhash 0 == aa6bc268...baf7`. The three 1.0.7 builds passed, so the packaged node provably carries the
real Brisvia mainnet genesis. (The guard does not check the ASERT half-life — that was verified separately at
the source level: 6 h, above.)

### Published installer SHA-256

```
Brisvia.Miner_1.0.7_x64-setup.exe   fcf81eb6212db98f440505abc5a711e48ac9659c193114897487640431776a55
Brisvia.Miner_1.0.7_amd64.deb       c93fdf8723ab8d03501b06af0c4ee8adb5fc96ff36660fb43dbcc59f809f454a
Brisvia.Miner_1.0.7_amd64.AppImage  ce19fa9ea3c5b6f511dadac82e9bef19404982efa025b43bd5e0f511ba9d4c20
Brisvia.Miner_1.0.7_aarch64.dmg     03ddaa6641a15e0f71932007291ca08438dcd2b8152e542d80db601560a4dc78
```

## 8. Launch mechanics

- Mainnet opens: **2026-08-01 15:00:00 UTC** (the genesis nTime).
- The network has ~24 h to mine block 1 or it stalls (the genesis is dated to the launch instant; load-bearing).
- 0% premine: no server mines. Contention plan lives in the monitor (alerts at 6 h / 20 h without block 1).
- Fixed seeds (mainnet): `187.77.240.145:9342`, `129.80.250.36:9342`, `129.159.108.102:9342`.

## 9. Port choice — moved off Litecoin's 9333/9332 (RESOLVED, done in 1.0.8)

The original defaults (P2P 9333 / RPC 9332) collided with Litecoin: magic bytes (`BRV1`) prevent protocol
confusion, but not port-bind conflicts, router forwards, wasted connection attempts, or JSON-RPC port
competition. So both were moved before launch, shipped in the **1.0.8** (the same forced update that ships
the pool), with a source guard in the build workflows forbidding the old values.

**Final pair (decided 2026-07-17, applied + verified 2026-07-18):**
- **P2P mainnet: `9342`** (IANA-unassigned; no collision with Bitcoin/Litecoin/Dogecoin/Monero/Zcash).
- **RPC mainnet: `9338`** (localhost only — least movement, the app already used it).
- **coin_type stays `9339'`** (unchanged — identity comes from the SLIP-44 coin_type, not the network port).
  Port **9339 is NOT used** (IANA-registered for gNMI). Later, formally register 9342 with IANA.

**Applied and verified (2026-07-18):** core `main` = commit `69c48b2` has `chainparams.cpp` (BRISVIA_MAIN)
`nDefaultPort = 9342` and `chainparamsbase.cpp` RPC `9338`; the published **1.0.8 desktop** bundles that exact
core (build genesis+port guards green on Win/Mac/Linux); the compiled fixed-seed header, the seed nodes'
config/firewall (both the OS iptables and the Oracle Cloud security list), and the web were all updated to
9342/9338. The build workflows are pinned to core `69c48b2` and run a source+binary guard that forbids
9333/9332 and requires 9342/9338. Nothing runs on the old 9333/9332 anymore.

## 10. Verification status

**Verified at the source level (2026-07-17):** genesis params + hash, ASERT 6 h, emission (no premine, 50/1M,
finite, 100M cap), magic/ports/hrp/base58, coin_type, no inherited Bitcoin chainwork/assumevalid/checkpoints,
IsTestChain, provenance (core SHA + RandomX SHA + genesis guard). ChatGPT verdict: **consensus approved; 1.0.7
needs no consensus correction.**

**Recommended additional evidence (run against the compiled node, not just the source):** ASERT vectors on both
sides of target incl. rounding/negative-exponent/saturation; emission summation to the exact final supply;
80-byte header serialization + nBits↔target + endianness; Median-Time-Past and the exact pre-launch mining rule
+ 24 h window behavior; genesis guard output captured from all three OS builds.
