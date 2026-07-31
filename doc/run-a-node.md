# Running a Brisvia node

This is the operator/developer guide for running a standalone Brisvia Core node. Most users do not need
this: the [Brisvia Desktop app](https://github.com/brisvia/brisvia-desktop) bundles a full node, wallet and
miner in one click. A friendly step-by-step version is available at
[brisvia.com/run-a-node](https://brisvia.com/run-a-node/). Full network parameters are on the
[Integration page](https://brisvia.com/integration/).

A node validates the chain independently and helps keep the network decentralised. It does **not** mine and
does **not** earn BRVA, and it never needs your address, private keys or 12-word recovery phrase.

## Build from source

Brisvia Core is a fork of Bitcoin Core v30.2; the build system and most dependencies are inherited from
Bitcoin Core (see [INSTALL.md](../INSTALL.md) and the platform build guides in this directory). The extra
piece is RandomX, included as a pinned git submodule.

1. Clone with submodules (RandomX is pinned):

       git clone --recursive https://github.com/brisvia/brisvia.git
       cd brisvia

   If you already cloned without `--recursive`:

       git submodule update --init --recursive

2. Build RandomX, then Brisvia Core:

       cmake -S src/randomx -B src/randomx/build -DARCH=native
       cmake --build src/randomx/build -j"$(nproc)"
       cmake -B build
       cmake --build build -j"$(nproc)"

   `-DARCH=native` may enable instructions specific to the current CPU. It is suitable for running on the
   same machine, but the resulting build may not run on other processors. Use a portable, documented build
   configuration for binaries intended for redistribution.

3. Verify the binaries:

       ./build/bin/bitcoind --version
       ./build/bin/bitcoin-cli --version

## Configuration

Create the data directory (this guide uses `$HOME/brisvia-mainnet`):

    mkdir -p "$HOME/brisvia-mainnet"
    chmod 700 "$HOME/brisvia-mainnet"

The configuration file is named `bitcoin.conf` (inherited from Bitcoin Core) and lives in the data
directory. Save the following as `$HOME/brisvia-mainnet/bitcoin.conf`:

    server=1
    listen=1
    port=9342
    rpcport=9338
    rpcbind=127.0.0.1

You do **not** need `rpcuser`/`rpcpassword` for local use — `bitcoin-cli` authenticates with the RPC cookie
created inside the data directory. Do **not** set `rpcbind=0.0.0.0` or a global `rpcallowip`, and do not put
real credentials in a config you might share.

## Start and stop

Start the daemon:

    ./build/bin/bitcoind -chain=brisvia -datadir="$HOME/brisvia-mainnet" -daemon

Stop it cleanly — let it close its databases, do not kill the process:

    ./build/bin/bitcoin-cli -chain=brisvia -datadir="$HOME/brisvia-mainnet" stop

For the test network, use `-chain=brisvia-test` (and a separate data directory — never mix mainnet and
testnet in the same directory).

## Health checks

    ./build/bin/bitcoin-cli -chain=brisvia -datadir="$HOME/brisvia-mainnet" getblockchaininfo

- `chain` must be `brisvia`.
- `blocks` is the validated height (it should climb).
- `initialblockdownload` turns `false` once synced.
- `verificationprogress` approaches `1`.

Confirm you are on the right chain by checking the genesis block:

    ./build/bin/bitcoin-cli -chain=brisvia -datadir="$HOME/brisvia-mainnet" getblockhash 0

The result must be `aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7`.

Check peers with `getconnectioncount` (or `getpeerinfo` for detail). The node finds peers automatically
through the fixed seed nodes shipped in the daemon; DNS seeds are not available yet.

Follow the log while it syncs:

    tail -f "$HOME/brisvia-mainnet/debug.log"

Confirm the RPC listens on loopback only (it should be bound to 127.0.0.1, never 0.0.0.0 or a public IP):

    ss -tln | grep ':9338'

## Making the node publicly reachable (optional)

A validating node does not need this. Accepting inbound connections on TCP **9342** lets you help other
nodes. You need both:

1. **Host firewall** — allow inbound TCP 9342. For example:
   - UFW: `sudo ufw allow 9342/tcp`
   - firewalld: `sudo firewall-cmd --add-port=9342/tcp --permanent && sudo firewall-cmd --reload`
   - Windows Firewall: add an inbound rule for TCP 9342.
2. **Home router** — forward TCP 9342 to your machine's local IP. Do **not** forward 9338 (RPC).

You are only publicly reachable once `getpeerinfo` shows *inbound* peers. A local listener, outbound
connections, or an open host firewall do not prove it — a router, CGNAT, your ISP, a changing public IP or a
separate IPv6 firewall can still block inbound traffic. Test from another network, not your own LAN.

## systemd example (Linux)

Create the `brisvia` system user before enabling the service, and do not set `daemon=1` when running under
this unit (systemd manages the process in the foreground).

`/etc/systemd/system/brisvia-node.service`:

    [Unit]
    Description=Brisvia Core node
    After=network-online.target
    Wants=network-online.target

    [Service]
    Type=simple
    User=brisvia
    Group=brisvia
    StateDirectory=brisvia
    StateDirectoryMode=0750
    ExecStart=/opt/brisvia/bin/bitcoind -chain=brisvia -datadir=/var/lib/brisvia
    ExecStop=/opt/brisvia/bin/bitcoin-cli -chain=brisvia -datadir=/var/lib/brisvia stop
    Restart=on-failure
    RestartSec=5
    TimeoutStopSec=600

    [Install]
    WantedBy=multi-user.target

Then `sudo systemctl enable --now brisvia-node`. Adjust paths and the user to your setup.

## Upgrading

Stop the node cleanly (`bitcoin-cli ... stop`), pull the new tag with submodules, rebuild, and start again on
the **same** data directory. Verify `getblockchaininfo` after the restart. Do not delete the data directory
or use `-reindex` as a first step for a problem.

## Troubleshooting

**Zero peers.** Wait a few minutes; confirm `networkactive=true` in `getnetworkinfo`; check the system
clock; confirm you are running with `-chain=brisvia` and not a testnet data directory; read `debug.log`. Use
`addnode` only as a temporary diagnostic — the daemon already ships fixed seeds.

**No inbound connections.** Check `listen=1`, the host firewall, port forwarding, CGNAT, your public IP, and
IPv4 vs IPv6.

**RPC not responding.** Same `-chain` and `-datadir`, the process is running, the cookie exists, and you are
using local port 9338. Do not open it to the Internet.

**Wrong chain.** Mainnet is `chain=brisvia`, P2P 9342, RPC 9338, genesis
`aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7`.

## Security checklist

- Never expose the RPC port (9338) to the Internet, even with a password — it is not hardened for public
  traffic.
- Keep RPC on localhost (`rpcbind=127.0.0.1`) with cookie authentication.
- Do not confuse the ports: 9342 is P2P (network), 9338 is RPC (local control).
- Running a node never requires your address, private keys or recovery phrase.
- Only download binaries from official sources and verify their checksums.
