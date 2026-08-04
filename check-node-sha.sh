#!/bin/bash
# ExecStartPre del nodo mainnet Brisvia: chequeo RÁPIDO y determinista.
# Aborta el arranque del servicio si el binario NO es el release 1.0.8 (SHA con P2P 9342 / RPC 9338).
# NO corre el nodo (no ocupa puerto ni demora el encendido). Uso: check-node-sha.sh <ruta bitcoind>
GOOD="f435d8ff797bbe5a07e2b17513a0d0508702251f7a134908f929b1856c8b191b"
BD="$1"
A=$(sha256sum "$BD" 2>/dev/null | cut -d' ' -f1)
if [ "$A" != "$GOOD" ]; then
  echo "ExecStartPre ABORTA: '$BD' no es el nodo 1.0.8 esperado (SHA=$A, esperado=$GOOD). Puerto/consenso podrían estar mal." >&2
  exit 1
fi
exit 0
