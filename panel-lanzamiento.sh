#!/usr/bin/env bash
# Mesa de control Brisvia — vista única de los 3 nodos semilla para el lanzamiento (1-ago-2026 15:00 UTC).
# Uso: ./panel-lanzamiento.sh   (una sola corrida, solo lectura, no toca nada).
# Corre desde Git Bash en la PC. Usa las claves SSH de C:\secure\fernando-secrets.
set -u
HKEY="/c/secure/fernando-secrets/brisvia/vps_brisvia_key"
OKEY="/c/secure/fernando-secrets/oracle/brisvia_node_ssh"
chmod 600 "$HKEY" "$OKEY" 2>/dev/null
GOOD="f435d8ff797bbe5a07e2b17513a0d0508702251f7a134908f929b1856c8b191b"

# nombre|user@ip|clave|bindir|datadir
NODES=(
  "Hostinger|root@187.77.240.145|$HKEY|/root/bitcoin/build-main/bin|/root/brisvia-mainnet-data"
  "Oracle-1|ubuntu@129.80.250.36|$OKEY|/home/ubuntu/bitcoin/bin|/home/ubuntu/brisvia-mainnet-data"
  "Oracle-2|ubuntu@129.159.108.102|$OKEY|/home/ubuntu/bitcoin/bin|/home/ubuntu/brisvia-mainnet-data"
)

# script remoto (mismo para los 3; recibe GOOD BIN DD por args)
read -r -d '' REMOTE <<'EOS'
GOOD="$1"; BINDIR="$2"; DD="$3"
BIN="$BINDIR/bitcoind"; CLI="$BINDIR/bitcoin-cli -datadir=$DD -chain=brisvia"
SHA=$(sha256sum "$BIN" 2>/dev/null | cut -d" " -f1)
[ "$SHA" = "$GOOD" ] && SHAOK="OK(1.0.8)" || SHAOK="!!DISTINTO!!"
echo "  binario SHA : ${SHA:0:16}... $SHAOK"
echo "  servicio    : $(systemctl is-active brisvia-mainnet.service 2>/dev/null) | enciende: $(systemctl list-timers --all 2>/dev/null | grep -oE 'Sat 2026-08-01 15:00:00 UTC' | head -1)"
echo "  reloj/NTP   : sync=$(timedatectl show -p NTPSynchronized --value 2>/dev/null) | $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo "  recursos    : disco $(df -h / 2>/dev/null | awk 'NR==2{print $4}') libre | RAM $(free -m 2>/dev/null | awk '/Mem:/{print $7}')MB disp | swap $(free -m 2>/dev/null | awk '/Swap:/{print $2}')MB"
if systemctl is-active --quiet brisvia-mainnet.service 2>/dev/null; then
  H=$($CLI getblockcount 2>/dev/null); TIP=$($CLI getbestblockhash 2>/dev/null)
  echo "  cadena      : altura=${H:-?} | tope=${TIP:0:20}... | pares=$($CLI getconnectioncount 2>/dev/null)"
  echo "  pares (ip)  : $($CLI getpeerinfo 2>/dev/null | grep -oE '\"addr\": \"[^\"]+\"' | sed 's/.*: //' | tr -d '\"' | tr '\n' ' ' | cut -c1-90)"
else
  echo "  cadena      : nodo mainnet APAGADO (correcto pre-lanzamiento; enciende solo el 1-ago 15:00 UTC)"
fi
echo "  9342 P2P    : $(ss -ltn 2>/dev/null | grep -cE ':9342') escuchando | 9333/9332(viejo): $(ss -ltn 2>/dev/null | grep -cE ':(9333|9332)')"
EOS

echo "=================================================================="
echo " MESA DE CONTROL BRISVIA — $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
echo " Lanzamiento: sábado 1-ago-2026 15:00 UTC (12:00 ART)"
echo "=================================================================="
for entry in "${NODES[@]}"; do
  IFS='|' read -r NAME DEST KEY BINDIR DD <<< "$entry"
  echo "── $NAME ($DEST) ─────────────────────────────"
  ssh -i "$KEY" -o ConnectTimeout=12 -o StrictHostKeyChecking=accept-new "$DEST" \
    "bash -s -- '$GOOD' '$BINDIR' '$DD'" <<< "$REMOTE" 2>/dev/null || echo "  (sin respuesta / SSH falló)"
  echo
done
echo "=================================================================="
echo "Chequeo profundo (SHA+puertos+genesis en datadir temporal): ./preflight en cada nodo a T-24h y T-60min."
