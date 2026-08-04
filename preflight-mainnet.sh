#!/bin/bash
# Preflight del nodo mainnet Brisvia. Correr A MANO a T-24h y T-60min en cada nodo semilla.
# Verifica en un datadir TEMPORAL (NO toca el datadir oficial): SHA del binario, bind P2P 9342,
# RPC default 9338, genesis aa6bc268, ausencia de 9333/9332, y que el bitcoind del PATH sea el bueno.
# Uso: sudo ./preflight-mainnet.sh /ruta/al/bitcoind
set -u
BD="${1:?uso: preflight-mainnet.sh <ruta bitcoind>}"
GOOD_SHA="f435d8ff797bbe5a07e2b17513a0d0508702251f7a134908f929b1856c8b191b"
GEN="aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7"
GENESIS_TIME=1785596400
FAIL=0
ok(){ echo "  OK  $1"; }
bad(){ echo "  FAIL $1"; FAIL=1; }

echo "== Preflight nodo mainnet ($(date -u)) =="
# 1) SHA del binario objetivo
A=$(sha256sum "$BD" 2>/dev/null | cut -d' ' -f1)
[ "$A" = "$GOOD_SHA" ] && ok "SHA del binario = release 1.0.8 (9342)" || bad "SHA distinto: $A"

# 2) correr en datadir TEMPORAL (mocktime SOLO en temp, nunca en el oficial) -> puertos + genesis
T=$(mktemp -d)
"$BD" -datadir="$T" -chain=brisvia -mocktime=$((GENESIS_TIME+60)) -listen=1 -connect=0 -dnsseed=0 -debug=net >/dev/null 2>&1 &
BPID=$!
sleep 10
L=$(find "$T" -name debug.log 2>/dev/null | head -1)
grep -qiE "Bound to .*:9342" "$L" 2>/dev/null && ok "P2P bind 9342" || bad "no bindeó P2P 9342"
grep -qiE "Binding RPC on address 127.0.0.1 port 9338" "$L" 2>/dev/null && ok "RPC default 9338 (localhost)" || bad "RPC no es 9338"
grep -qiE "Bound to .*:(9333|9332)" "$L" 2>/dev/null && bad "bindeó 9333/9332 (PUERTO VIEJO)" || ok "sin bind 9333/9332"
GN=$("${BD%bitcoind}bitcoin-cli" -datadir="$T" -chain=brisvia getblockhash 0 2>/dev/null)
[ "$GN" = "$GEN" ] && ok "genesis aa6bc268" || bad "genesis distinto: $GN"
"${BD%bitcoind}bitcoin-cli" -datadir="$T" -chain=brisvia stop >/dev/null 2>&1
sleep 2; kill -9 "$BPID" 2>/dev/null; rm -rf "$T"

# 3) el bitcoind del PATH global debe ser el bueno (no un 9333 viejo)
if command -v bitcoind >/dev/null 2>&1; then
  PS=$(sha256sum "$(command -v bitcoind)" 2>/dev/null | cut -d' ' -f1)
  [ "$PS" = "$GOOD_SHA" ] && ok "PATH bitcoind = 9342" || bad "PATH bitcoind NO es el 9342: $(command -v bitcoind)"
fi
# 4) ningún ejecutable viejo (1e4063e7) suelto
OLD=$(find / -name "bitcoind" -type f -perm -u+x 2>/dev/null | grep -v OBSOLETE | while read f; do sha256sum "$f" 2>/dev/null; done | grep -c "1e4063e748365d5c78795c90feac2cdf0143aab956958377ec754897952dc731")
[ "$OLD" = 0 ] && ok "sin binario viejo 9333 ejecutable" || bad "hay $OLD binario(s) viejo(s) 9333 ejecutables"

echo
[ "$FAIL" = 0 ] && echo "=== PREFLIGHT PASS ===" || echo "=== PREFLIGHT FAIL (revisar arriba) ==="
exit "$FAIL"
