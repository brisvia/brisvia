# Únicas acciones pendientes al lanzamiento — Brisvia (actualizado 2026-07-19)

Todo el camino crítico está congelado y verificado. Lo que queda son SOLO acciones por FECHA (automáticas o de vigilancia) o de FERNANDO. Nada más para "tocar" en lo validado.

## Por FERNANDO — SOLO 1 acción real de lanzamiento
| # | Acción | Fecha límite | Cómo |
|---|---|---|---|
| F3 | Coordinar/convocar mineros para el 1-ago 12:00 ART | 1-ago | Publicar `guia-mineros-comunidad.md` en Discord/Telegram (ES/EN). |

### Sacados de la lista (NO son obligatorios para el lanzamiento)
- ~~F1 Firmar el pago de prueba~~ → **OPCIONAL / se puede SALTAR.** Es testnet (plata de prueba, SIN valor real), y la pool arranca APAGADA el 1-ago. Era solo un ensayo del flujo de pago, que YA está probado en staging (26 tests + E2E). La firma necesita la billetera del pool de Fernando (por diseño, la clave nunca vive en el servidor). Si Fernando no quiere hacerlo: se saltea, CERO impacto en el lanzamiento. El primer pago REAL se firma post-lanzamiento cuando la pool se habilite. Archivos listos por si acaso en `firma-pago/`.
- ~~F2 Probar la app en la ThinkPad (Ubuntu 26.04)~~ → **NO bloqueante, sacado del camino del lanzamiento.** Ubuntu 26.04 es una versión recién salida que casi nadie usa; el lanzamiento va con Windows/Mac (instaladores principales) + Linux normal (22.04/24.04, donde la app anda). El arreglo (EGL en Rust + recomendar el .deb, que empíricamente evita el conflicto de GLib) está listo en la rama `fix/ubuntu-2604-appimage` para la próxima actualización. Si algún usuario real lo reporta, el diagnóstico de 1 comando queda listo. Fernando NO tiene que probar nada.

## Por FECHA (automático — yo solo vigilo/confirmo)
| # | Qué pasa solo | Cuándo | Vigilancia |
|---|---|---|---|
| D1 | Preflight T−24h corre solo en los 3 nodos | 31-jul 15:00 UTC | timer systemd; alerta a Discord solo si falla |
| D2 | Preflight T−60min corre solo | 1-ago 14:00 UTC | idem |
| D3 | Preflight T−5min corre solo | 1-ago 14:55 UTC | idem |
| D4 | Testnet se apaga sola (stop+disable nodo) | 29-jul 03:00 UTC | no destructivo; ledger+pago preservados |
| D5 | **Mainnet arranca sola** (los 3 nodos, guarda ExecStartPre) | **1-ago 15:00 UTC** | `panel-lanzamiento.sh` para ver los 3; monitor + freeze-watch alertan |
| D6 | freeze-watch diario (huella binario + timers) | cada día 12:00 UTC | alerta a Discord solo ante drift |

## Post-lanzamiento (después de ver estabilidad real de mainnet)
| # | Acción | Cuándo | Doc |
|---|---|---|---|
| P1 | Verificar el pago de Fernando (txid, confirmaciones, reconcile) | apenas él transmita | `GUIA-firma-pago-pool.md` §Control posterior |
| P2 | Desplegar el arreglo de suspensión de la pool + habilitar pool | tras estabilidad mainnet | `POOL-despliegue-rollback.md`. **BLOQUEANTE (ChatGPT): antes de abrir la pool pública, completar 1 pago firmado E2E** (testnet o lote mínimo controlado). NO bloquea el lanzamiento de mainnet. |
| P3 | Cerrar Ubuntu 26.04 (tras prueba física F2) | tras F2 | rama `fix/ubuntu-2604-appimage` |

## Congelado — NO tocar (salvo evidencia de fallo)
Núcleo (69c48b2), bitcoind (f435d8ff), los 3 nodos, config, timers, monitor, web, instaladores v1.0.8, scripts operativos. Guarda: `verificar-freeze.sh` (local) + `freeze-watch` (en los nodos). Manifiesto: `FREEZE-LANZAMIENTO.md`.

## Día del lanzamiento
Seguir `CHECKLIST-DIA-LANZAMIENTO.md` (T−24h → primera hora + acciones por falla). Mensajes listos: `mensajes-discord-lanzamiento.md` + `faq-lanzamiento.md`. Registro: `registro-lanzamiento.md`.
