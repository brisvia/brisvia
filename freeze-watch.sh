#!/usr/bin/env bash
# Vigilancia de congelación (complementa al monitor de 5 min, que ya mira mainnet.timer/NTP/disco/recursos).
# Chequea lo que el monitor NO mira: la HUELLA del binario (invariante) + que los timers de preflight sigan
# activos hasta el lanzamiento. Alerta a Discord SOLO ante una diferencia real. Sin reportes de rutina.
# Uso (lo dispara un timer diario): freeze-watch.sh <ruta-bitcoind> <ruta-monitor-conf>
set -u
BIN="${1:?falta bitcoind}"; CONF="${2:-}"
GOOD=f435d8ff797bbe5a07e2b17513a0d0508702251f7a134908f929b1856c8b191b
LAUNCH=1785596400   # 2026-08-01 15:00:00 UTC
NOW=$(date -u +%s)
D=""

# 1) Huella del binario (NUNCA debe cambiar: si cambia, alguien swapeó el nodo)
CUR=$(sha256sum "$BIN" 2>/dev/null | cut -d' ' -f1)
[ "$CUR" = "$GOOD" ] || D="${D}binario NO es 1.0.8 (SHA=${CUR:0:16}…); "

# 2) Antes del lanzamiento: timers de preflight activos + mainnet.timer habilitado
if [ "$NOW" -lt "$LAUNCH" ]; then
  for t in t24 t60 t5; do
    systemctl is-active --quiet "brisvia-preflight-$t.timer" 2>/dev/null || D="${D}preflight-$t inactivo; "
  done
  systemctl is-enabled --quiet brisvia-mainnet.timer 2>/dev/null || D="${D}mainnet.timer NO habilitado; "
fi

if [ -n "$D" ]; then
  echo "FREEZE-WATCH DRIFT en $(hostname): $D"
  if [ -n "$CONF" ] && [ -f "$CONF" ]; then
    TOK=$(grep -E "^discord_bot_token=" "$CONF" | head -1 | cut -d= -f2-)
    CH=$(grep -E "^discord_channel_id=" "$CONF" | head -1 | cut -d= -f2-)
    if [ -n "$TOK" ] && [ -n "$CH" ]; then
      MSG="🔒 FREEZE-WATCH: DRIFT en $(hostname) ($(date -u '+%Y-%m-%d %H:%M UTC')): $D"
      BODY=$(python3 -c "import json,sys;print(json.dumps({'content':sys.argv[1][:1900]}))" "$MSG" 2>/dev/null)
      curl -s -X POST "https://discord.com/api/v10/channels/$CH/messages" \
        -H "Authorization: Bot $TOK" -H "Content-Type: application/json" -d "$BODY" >/dev/null 2>&1
    fi
  fi
  exit 1
else
  echo "freeze-watch OK en $(hostname) (sin drift)"   # solo al log, NO a Discord
  exit 0
fi
