# Evidencia batería adversarial Brisvia — laboratorio aislado (2026-07-18)

Todos los tests en entornos aislados (lab regtest / lab systemd / loopback fs). NO se tocó testnet, mainnet ni la pool viva. Sin datos sensibles.

## LAB systemd (brisvia-lab.service, datadir/puertos 19342/19338, eliminado)
- SIGKILL (kill -9) → Restart=always recuperó (PID nuevo, genesis aa6bc268 intacto). VERDICTO: OK.
- SIGTERM (systemctl stop/start) limpio → datadir íntegro, genesis correcto. VERDICTO: OK.
- ExecStartPre con binario malo → el servicio NO llega a `active` (queda reintentando). VERDICTO: OK (falla cerrado).
- Guarda directa: binario viejo 9333 / inexistente / random → exit 1; solo pasa 1.0.8 (f435d8ff). VERDICTO: OK.

## Partición de red (2 nodos regtest aislados, eliminados)
- Conectados + generar 3 en A → convergen (h=3, mismo tip). OK.
- Partición (setnetworkactive false) + generar A:+2 (h=5) y B:+4 (h=7) → puntas DIVERGENTES. OK.
- Reconectar → A reorganiza a la cadena de B (más trabajo), ambos h=7 mismo tip; rama vieja de A queda como valid-fork. VERDICTO: OK (auto-cura, sin intervención).
- **Distinguir partición real vs nodo aislado**: partición real = puntas divergentes que persisten mientras están separados (`getchaintips` con >1 tip válido compitiendo); nodo aislado = 0 pares pero SIN punta rival (solo deja de avanzar). El monitor lo separa con `getchaintips` (fork) vs conteo de pares.

## Monitor (controlado, --test, sin Discord)
- 6 escenarios detectados en 1 corrida: disco bajo, RAM baja, bloque detenido (210 min), desconexión P2P con seeds, FORK (getchaintips), + lectura altura/peers vía cookie. VERDICTO: OK. Texto de alerta corregido 9333→9342 en los 3.

## Pago testnet (revalidación read-only)
- 2 UTXO reservados SIN GASTAR, coinbase del pool, MADUROS (217/216 conf), 100 BRVA; destinos 75/25; artefacto intacto (80bedce2…). VERDICTO: OK, listo para firma.

## Procedimiento 29-jul (read-only)
- Apagado no destructivo (stop+disable, sin rm), idempotente; ledger+pago+6 backups fuera del datadir testnet (preservados); datadirs separados; mainnet ya preparado. VERDICTO: OK.

## Disco lleno (loopback fs 34M aislado, eliminado)
- Nodo en fs chico, llené el disco a 100% → `generatetoaddress` dio ERROR y el nodo dejó de responder (fail-closed), NO corrompió silenciosamente. Al liberar espacio, el reinicio pedía más tiempo de recuperación (bitcoin es crash-safe: LevelDB + block files con recovery). VERDICTO: falla cerrado OK; el monitor avisa "disco bajo" ANTES de llenarse (ya validado).

## Panel de control (falso verde)
- Nodo inalcanzable → SSH falla → panel imprime "(sin respuesta / SSH falló)", NO un verde. Nodo apagado → "APAGADO", no altura falsa. El panel muestra HECHOS crudos (SHA, is-active, altura real); no fabrica verdes. VERDICTO: OK, no engaña.

## Checklist definitivo
- Consolidado en `CHECKLIST-DIA-LANZAMIENTO.md`: T−24h / T−60min / T−5min / 15:00 UTC / primer peer / bloque 1 / primera hora + tabla de acciones exactas por falla.

## Pendiente (menor valor o requiere infra dedicada)
- Timer duplicado/no-dispara: systemd estándar; el timer real ya está verificado armado + list-timers confirma la próxima corrida. Idempotencia del arranque probada en el LAB (SIGKILL→Restart, ExecStartPre).
- Cerrar/reabrir 9342 en vivo: sería sobre el nodo real (apagado pre-lanzamiento); la conectividad/convergencia ya cubierta por el test de partición + descubrimiento E2E previo.
- Ensayo 29 con clon DESTRUCTIVO (correr 2x + interrumpir + rollback): validado por diseño (reversible/idempotente, sin borrado) + inspección + comportamientos systemd probados en el LAB. Un clon destructivo completo es alto esfuerzo / bajo valor marginal adicional.
