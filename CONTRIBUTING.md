Contributing to Brisvia Core
============================

Brisvia Core is a fork of [Bitcoin Core](https://github.com/bitcoin/bitcoin) v30.2 that replaces the
Proof-of-Work with RandomX and the difficulty adjustment with ASERT. Most of this codebase is Bitcoin
Core's, and most of what you need to know about working in it is Bitcoin Core's too: its coding style,
its review culture and its test framework all still apply, and the
[developer notes](doc/developer-notes.md) remain the reference for how the code is written.

What follows is only what is different, because Brisvia is a small young project and not the Bitcoin Core
process.

Where to contribute what
------------------------

- **This repository** holds the node and consensus code: validation, Proof-of-Work, difficulty
  adjustment, networking, the wallet backend, RPC.
- **The desktop app** — the wallet and miner most people actually install — lives in
  [github.com/brisvia/brisvia-desktop](https://github.com/brisvia/brisvia-desktop). Interface bugs,
  translations and installer problems belong there.
- **Security problems go nowhere public.** Read [SECURITY.md](SECURITY.md) first.

Consensus changes
-----------------

Anything that changes which blocks are valid — Proof-of-Work, difficulty, block reward, emission, the
genesis block, message headers, ports — splits the network if it ships unevenly. The parameters frozen for
the main network are written down in [BRISVIA-MAINNET-SPEC.md](BRISVIA-MAINNET-SPEC.md), which is the
single source of truth for them.

Open an issue and describe the problem before writing code. A consensus change needs a reason that
survives argument, not just a patch that compiles.

Before you open a pull request
------------------------------

    cmake -B build && cmake --build build       # build
    ctest --test-dir build                      # unit tests
    build/test/functional/test_runner.py        # functional tests

Say in the description what you tested and on which platform. "It builds" is not a test result.

Conventions
-----------

- **Everything in this repository is written in English** — code, comments, commit messages,
  documentation.
- Keep diffs minimal against upstream Bitcoin Core. The smaller the divergence, the easier it is to pull
  in upstream security fixes, and that matters more here than elegance.
- Where Brisvia deliberately differs from Bitcoin Core, say so in a comment and explain why. Someone will
  eventually diff the two files and needs to know the change was intentional.
- Commit messages explain why. The diff already shows what.

Where to talk
-------------

Upstream Bitcoin Core documentation points at Bitcoin Core's IRC channels and mailing lists. Those are for
Bitcoin Core — please do not take Brisvia questions there. Use this repository's issues, or the community
channels listed at [brisvia.com](https://brisvia.com).

Upstream documentation
----------------------

Bitcoin Core's own contributing guide is kept alongside this one as
[doc/CONTRIBUTING-upstream.md](doc/CONTRIBUTING-upstream.md). Its guidance on code review, commit
discipline and testing is good and still applies; only its project-specific process and channels do not.
