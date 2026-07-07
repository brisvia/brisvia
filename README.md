Brisvia Core [BRVA]
===================

https://brisvia.com

What is Brisvia?
----------------

Brisvia is a **transparent**, **CPU-mineable** Proof-of-Work cryptocurrency with a **fair launch**
(0% premine). It is a port of [Bitcoin Core](https://github.com/bitcoin/bitcoin) v30.2: it keeps
Bitcoin's UTXO model and transparency, and replaces the SHA-256 Proof-of-Work with
[RandomX](https://github.com/tevador/RandomX) and the difficulty adjustment with **ASERT**.

The goal is simple: keep mining in the hands of ordinary people. RandomX is optimised for general-purpose
CPUs and is resistant to ASIC and GPU farms, so anyone with a normal computer can take part.

Key parameters
--------------

| Parameter            | Value                                                            |
|----------------------|------------------------------------------------------------------|
| Ticker               | BRVA                                                             |
| Base                 | Fork of Bitcoin Core v30.2 (UTXO, transparent)                  |
| Proof-of-Work        | RandomX (ASIC-resistant; mines on any common CPU)              |
| Difficulty           | ASERT (adjusts every block)                                    |
| Block target         | 120 seconds                                                    |
| Block reward         | 50 BRVA, halving every 1,000,000 blocks                        |
| Emission             | Finite, no tail (Bitcoin-style): 50 → 25 → 12.5 → … → 0        |
| Maximum supply       | 100,000,000 BRVA                                               |
| Premine              | 0% — the genesis block has no spendable reward                 |

Emission is finite and predictable. The block reward halves every 1,000,000 blocks with no perpetual tail,
so the total supply converges to exactly 100 million BRVA, with the last coins mined around block 33,000,000.

License
-------

Brisvia Core is released under the terms of the MIT license. See [COPYING](COPYING) for more information.
It inherits the license of Bitcoin Core, from which it is derived.

Building
--------

Brisvia links against RandomX, which is included as a git submodule pinned to v1.2.2. Clone with
submodules and build RandomX first, then build Brisvia.

    # 1. Clone including the RandomX submodule
    git clone --recursive https://github.com/brisvia/brisvia.git
    cd brisvia

    # 2. Build the RandomX static library (once)
    cmake -S src/randomx -B src/randomx/build -DARCH=native
    cmake --build src/randomx/build -j"$(nproc)"

    # 3. Configure and build Brisvia Core
    cmake -B build
    cmake --build build -j"$(nproc)"

The RandomX paths default to the submodule build (`src/randomx/build/librandomx.a` and `src/randomx/src`).
To cross-compile — for example, a Windows binary with mingw — build RandomX for the target and pass
`-DBRISVIA_RANDOMX_LIB=<librandomx.a>` and `-DBRISVIA_RANDOMX_INCLUDE=<headers>` to the Brisvia configure step.

The rest of the build system, dependencies and platform notes are inherited from Bitcoin Core; see
[INSTALL.md](INSTALL.md) and the [doc/](doc/) directory.

Running a node
--------------

Brisvia runs its own networks. To join the shared test network:

    build/bin/bitcoind -chain=brisvia-test

The node discovers peers automatically (DNS seed + fixed seeds) and validates the RandomX Proof-of-Work of
every block as it syncs.

The port is additive and gated: without Brisvia's consensus parameters, behaviour is identical to upstream
Bitcoin Core. Brisvia's own code lives in `src/consensus/brisvia_*`, `src/consensus/randomx_seed.h` and
`src/crypto/randomx_hash.*`; the rest of the tree is Bitcoin Core v30.2 with the port applied.
