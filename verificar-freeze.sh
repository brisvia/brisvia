#!/usr/bin/env bash
# Guarda de congelación Brisvia: detecta cualquier cambio en el paquete canónico del lanzamiento.
# Recomputa las huellas de los archivos operativos + chequea el bitcoind desplegado en los 3 nodos + timers.
# Solo lectura. Uso: ./verificar-freeze.sh   (correr antes de tocar nada / a diario hasta el 1-ago).
set -u
cd "$(dirname "$0")"
DRIFT=0
ok(){ echo "  OK    $1"; }
bad(){ echo "  DRIFT $1"; DRIFT=1; }

echo "=================================================================="
echo " VERIFICACIÓN DE CONGELACIÓN — $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo "=================================================================="

echo "── Archivos operativos (huella no debe cambiar) ──"
# archivo:sha esperado
declare -A EXP=(
  [preflight-mainnet.sh]=e6eda1c70eec64f74e257d07d40821857d2c53babbb2b9b5102e8526eea64f00
  [check-node-sha.sh]=e4e785ef21e4fe40af16d1c887eb1ef700b91b82c559a9be55b09779f7d37714
  [panel-lanzamiento.sh]=c4d4f2a8acb21d33ec1e048626cc03a80e2f1a625e81d0dbdf83a58039c1db72
  [CHECKLIST-DIA-LANZAMIENTO.md]=5c45ea6085b6af5c936060648dd33464f41b068ab3af5177401d0e99cd5d4128
  [PROCEDIMIENTO-apagado-testnet.md]=6a0383edb5ca94c328b5439ab43157eff541da47deb4287bddea6ed94ad9e34b
  [PLAN-emergencia-dia1.md]=330ffb8964d93ff4a0485f5dee61b3ff4a49f41fbe6e37c5c943894a2d27ba1f
  [GUIA-firma-pago-pool.md]=794790f35317fb583476b7170a9a9094557c182fbef18c992566bedfb59bd7d6
  [adversarial-evidence.md]=61b1ba6b859b227a688fa3107e766e7e6db493298e7707ae3c75d6bd819226ff
  [BRISVIA-MAINNET-SPEC.md]=a7d3daff71eab91b186fd39aebd1233f41c13457332b3fc62fe5fdb84f99b6d6
)
for f in "${!EXP[@]}"; do
  if [ ! -f "$f" ]; then bad "$f (FALTA)"; continue; fi
  cur=$(sha256sum "$f" | cut -d' ' -f1)
  [ "$cur" = "${EXP[$f]}" ] && ok "$f" || bad "$f (huella distinta: ${cur:0:16}…)"
done

echo "── bitcoind desplegado en los 3 nodos (debe ser f435d8ff) + timer armado ──"
GOOD_BIN=f435d8ff797bbe5a07e2b17513a0d0508702251f7a134908f929b1856c8b191b
HKEY="/c/secure/fernando-secrets/brisvia/vps_brisvia_key"
OKEY="/c/secure/fernando-secrets/oracle/brisvia_node_ssh"
chmod 600 "$HKEY" "$OKEY" 2>/dev/null
check_node(){ # nombre user@ip clave binpath
  local n=$1 dest=$2 key=$3 bin=$4
  local out; out=$(ssh -i "$key" -o ConnectTimeout=12 -o StrictHostKeyChecking=accept-new "$dest" \
    "echo \"\$(sha256sum $bin 2>/dev/null | cut -d' ' -f1)|\$(systemctl list-timers --all 2>/dev/null | grep -oE 'Sat 2026-08-01 15:00:00 UTC' | head -1)|\$(systemctl is-active brisvia-mainnet.service 2>/dev/null)\"" 2>/dev/null)
  local sha=${out%%|*}; local rest=${out#*|}; local timer=${rest%%|*}; local act=${rest##*|}
  [ "$sha" = "$GOOD_BIN" ] && ok "$n bitcoind = 1.0.8" || bad "$n bitcoind DISTINTO (${sha:0:16}…)"
  [ "$timer" = "Sat 2026-08-01 15:00:00 UTC" ] && ok "$n timer armado 1-ago 15:00" || bad "$n timer NO armado ($timer)"
  [ "$act" = "inactive" ] && ok "$n mainnet apagado (pre-lanzamiento)" || echo "  nota  $n mainnet: $act"
}
check_node Hostinger root@187.77.240.145 "$HKEY" /root/bitcoin/build-main/bin/bitcoind
check_node Oracle-1  ubuntu@129.80.250.36 "$OKEY" /home/ubuntu/bitcoin/bin/bitcoind
check_node Oracle-2  ubuntu@129.159.108.102 "$OKEY" /home/ubuntu/bitcoin/bin/bitcoind

echo "=================================================================="
[ "$DRIFT" = 0 ] && echo " === CONGELACIÓN INTACTA (sin cambios) ===" || echo " === HAY DRIFT: revisar las líneas 'DRIFT' arriba ANTES de seguir ==="
exit "$DRIFT"
