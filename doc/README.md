Brisvia Core
=============

Setup
---------------------
Brisvia Core is the reference client for the Brisvia network (ticker BRVA), a
CPU-mineable Proof-of-Work cryptocurrency. It is a fork of [Bitcoin Core](https://github.com/bitcoin/bitcoin)
v30.2 that replaces the SHA-256 Proof-of-Work with [RandomX](https://github.com/tevador/RandomX)
and the difficulty adjustment with ASERT. It downloads and, by default, stores
the entire history of Brisvia transactions.

To download Brisvia Core, visit [brisvia.com](https://brisvia.com) or the
[releases page](https://github.com/brisvia/brisvia/releases). To build from
source, see the [root README](/README.md).

> Note: Brisvia Core is derived from Bitcoin Core and, for now, keeps the
> upstream binary names (`bitcoind`, `bitcoin-cli`, `bitcoin-qt`, …) and the
> default `bitcoin.conf` / data directory layout.

Running
---------------------
The following are some helpful notes on how to run Brisvia Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/bitcoin-qt` (GUI) or
- `bin/bitcoind` (headless)
- `bin/bitcoin` (wrapper command)

The `bitcoin` command supports subcommands like `bitcoin gui`, `bitcoin node`, and `bitcoin rpc` exposing different functionality. Subcommands can be listed with `bitcoin help`.

### Windows

Unpack the files into a directory, and then run bitcoin-qt.exe.

### macOS

Drag the application to your applications folder, and then run it.

### Need Help?

* See the documentation in this repository and at [brisvia.com](https://brisvia.com)
for help and more information.
* Open an issue on the [Brisvia GitHub repository](https://github.com/brisvia/brisvia/issues).

Building
---------------------
The following are developer notes on how to build Brisvia Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows-msvc.md)
- [FreeBSD Build Notes](build-freebsd.md)
- [OpenBSD Build Notes](build-openbsd.md)
- [NetBSD Build Notes](build-netbsd.md)

Development
---------------------
The Brisvia repo's [root README](/README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Productivity Notes](productivity.md)
- [Release Process](release-process.md)
- [Translation Process](translation_process.md)
- [Translation Strings Policy](translation_strings_policy.md)
- [JSON-RPC Interface](JSON-RPC-interface.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [BIPS](bips.md)
- [Dnsseed Policy](dnsseed-policy.md)
- [Benchmarking](benchmarking.md)
- [Internal Design Docs](design/)

Brisvia Core is a fork of Bitcoin Core; much of the developer documentation
above is inherited from the upstream project and still refers to Bitcoin
tooling and conventions.

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [bitcoin.conf Configuration File](bitcoin-conf.md)
- [CJDNS Support](cjdns.md)
- [Files](files.md)
- [Fuzz-testing](fuzzing.md)
- [I2P Support](i2p.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)
- [Managing Wallets](managing-wallets.md)
- [Multisig Tutorial](multisig-tutorial.md)
- [Offline Signing Tutorial](offline-signing-tutorial.md)
- [P2P bad ports definition and list](p2p-bad-ports.md)
- [PSBT support](psbt.md)
- [Reduce Memory](reduce-memory.md)
- [Reduce Traffic](reduce-traffic.md)
- [Tor Support](tor.md)
- [Transaction Relay Policy](policy/README.md)
- [ZMQ](zmq.md)

License
---------------------
Distributed under the [MIT software license](/COPYING).
