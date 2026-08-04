#!/usr/bin/env bash
# Envoltorio de preflight PROGRAMADO (lo disparan los timers T-24h / T-60min / T-5min).
# Corre preflight-mainnet.sh en un datadir TEMPORAL (no toca cadena ni arranca mainnet), guarda evidencia,
# y alerta a Discord SOLO si falla. Uso: preflight-scheduled.sh <label> <ruta-bitcoind> <ruta-monitor-conf>
set -u
HERE="$(dirname "$0")"
LABEL="${1:?falta label}"; BIN="${2:?falta bitcoind}"; CONF="${3:-}"
LOGDIR="$HERE/preflight-logs"; mkdir -p "$LOGDIR"
TS=$(date -u +%Y%m%d_%H%M%S)
LOG="$LOGDIR/${LABEL}_${TS}.log"

"$HERE/preflight-mainnet.sh" "$BIN" > "$LOG" 2>&1
RC=$?

if [ "$RC" != 0 ] || ! grep -q "PREFLIGHT PASS" "$LOG"; then
  echo "PREFLIGHT $LABEL en $(hostname): FALLÓ (rc=$RC). Log: $LOG"
  # alerta a Discord (bot token + canal del monitor), truncada
  if [ -n "$CONF" ] && [ -f "$CONF" ]; then
    TOK=$(grep -E "^discord_bot_token=" "$CONF" | head -1 | cut -d= -f2-)
    CH=$(grep -E "^discord_channel_id=" "$CONF" | head -1 | cut -d= -f2-)
    if [ -n "$TOK" ] && [ -n "$CH" ]; then
      MSG="⚠ PREFLIGHT $LABEL FALLÓ en $(hostname) ($(date -u '+%H:%M UTC')). Últimas líneas: $(tail -5 "$LOG" | tr '\n' ' ' | cut -c1-1500)"
      BODY=$(python3 -c "import json,sys;print(json.dumps({'content':sys.argv[1][:1900]}))" "$MSG" 2>/dev/null)
      curl -s -X POST "https://discord.com/api/v10/channels/$CH/messages" \
        -H "Authorization: Bot $TOK" -H "Content-Type: application/json" -d "$BODY" >/dev/null 2>&1 \
        && echo "  alerta Discord enviada" || echo "  (no se pudo enviar alerta Discord)"
    fi
  fi
  exit 1
else
  echo "PREFLIGHT $LABEL en $(hostname): PASS (sin alerta, como corresponde). Log: $LOG"
  exit 0
fi
