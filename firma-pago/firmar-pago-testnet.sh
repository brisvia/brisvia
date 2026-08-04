#!/usr/bin/env bash
# Firma OFFLINE del pago de PRUEBA de la pool (testnet, BRVA sin valor real). Una sola acción para Fernando.
# Lo corrés en tu máquina con la billetera del pool cargada (la que controla la dirección de cobro
# tbrv1q0t5nlyvr265ve94sn8fjc34urqks4m9etpnyul). El script arma la transacción EXACTA, la firma con tu
# billetera y te imprime el HEX firmado + el txid para pasárselo a Claude. NO transmite nada (eso lo hace
# Claude tras validar). NUNCA te pide ni muestra tus 12 palabras: firma con la billetera ya cargada.
#
# Uso:   ./firmar-pago-testnet.sh [ruta-bitcoin-cli] [-datadir=... -rpcwallet=...]
#   ej:  ./firmar-pago-testnet.sh bitcoin-cli
#   ej:  ./firmar-pago-testnet.sh /ruta/bitcoin-cli "-datadir=/mi/dir -rpcwallet=pool"
set -euo pipefail

CLIBIN="${1:-bitcoin-cli}"; shift || true
EXTRA="${*:-}"
CLI="$CLIBIN -chain=brisvia-test $EXTRA"

# --- Datos EXACTOS del lote (del artefacto testnet-payout-1.json, hash 80bedce2) ---
INPUTS='[{"txid":"07ac620809059e32e923806621c2a1f7cad4bb9bb0d5f8bcadc4ab3e99b2de87","vout":0},{"txid":"c47f526c039e6f37a9460eb1e9803fb0a68dc090174c050dc53a4fdc164a3292","vout":0}]'
# destinos NETOS (ya con la comisión descontada). La comisión (5000 sats) = 100 BRVA de entradas - 99.99995 de salidas.
OUTPUTS='{"tbrv1qgztgzvy5zu8ul6cxp7ugntd5d8p8452s7cevp2":24.99998750,"tbrv1qwk84fz4v99we9l584tweldegd3d032t9240hn0":74.99996250}'

echo "== 1) Armo la transacción EXACTA (2 entradas de 50 BRVA -> 2 mineros, comisión 5000 sats, SIN vuelto) =="
RAW=$($CLI createrawtransaction "$INPUTS" "$OUTPUTS")

echo "== 2) Revisá antes de firmar: entradas y salidas de la transacción =="
$CLI decoderawtransaction "$RAW" | python3 - <<'PY'
import sys, json
d = json.load(sys.stdin)
print("  Entradas:")
for vin in d["vin"]:
    print("   -", vin["txid"][:20]+"...:"+str(vin["vout"]))
print("  Salidas:")
tot = 0
for vo in d["vout"]:
    a = vo.get("scriptPubKey", {}).get("address", "?")
    print("   ->", a, ":", vo["value"], "BRVA")
    tot += vo["value"]
print("  Total salidas:", round(tot, 8), "BRVA  (entradas = 100 BRVA -> comisión ~", round(100-tot,8), "BRVA = 5000 sats)")
PY

echo "== 3) Convierto a PSBT, COMPLETO los UTXO (para firmar segwit) y FIRMO con tu billetera (offline) =="
PSBT=$($CLI converttopsbt "$RAW")
# utxoupdatepsbt completa el witness_utxo de cada entrada (necesario para firmar segwit). Si tu nodo tiene
# esas monedas (las coinbase del pool) las rellena; si no, walletprocesspsbt igual las completa desde la billetera.
PSBT=$($CLI utxoupdatepsbt "$PSBT" 2>/dev/null || echo "$PSBT")
SIGNED_JSON=$($CLI walletprocesspsbt "$PSBT")
SIGNED=$(echo "$SIGNED_JSON" | python3 -c "import sys,json;print(json.load(sys.stdin)['psbt'])")

echo "== 4) Finalizo la firma =="
FINAL=$($CLI finalizepsbt "$SIGNED")
COMPLETE=$(echo "$FINAL" | python3 -c "import sys,json;print(json.load(sys.stdin).get('complete'))")
HEX=$(echo "$FINAL" | python3 -c "import sys,json;print(json.load(sys.stdin).get('hex',''))")
if [ "$COMPLETE" != "True" ] || [ -z "$HEX" ]; then
  echo "  !! La firma NO quedó completa (complete=$COMPLETE). Revisá que la billetera controle la dirección de cobro."
  exit 1
fi
TXID=$($CLI decoderawtransaction "$HEX" | python3 -c "import sys,json;print(json.load(sys.stdin)['txid'])")

echo
echo "=================================================================="
echo " LISTO. Copiá y pegale ESTO a Claude (no transmití nada):"
echo "   TXID=$TXID"
echo "   HEX=$HEX"
echo "=================================================================="
echo " Claude valida la firma contra el lote, transmite y confirma. Vos no hacés nada más."
