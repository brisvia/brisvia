# Firma del pago de prueba de la pool — resumen y única acción (Fernando)

Esto es un pago de **PRUEBA en testnet** (monedas de prueba, **sin valor real**). Sirve para dejar probado el flujo de pago antes del lanzamiento. **Fecha límite: antes del 29-jul 03:00 UTC** (cuando se apaga la red de prueba).

## Qué vas a firmar (revisá esto)
| Dato | Valor |
|---|---|
| Sale de (2 monedas maduras del pool) | `07ac6208…de87:0` (50 BRVA) + `c47f526c…3292:0` (50 BRVA) = **100 BRVA** |
| Va a (2 mineros, reparto 75/25) | `tbrv1qwk84fz…9240hn0` → **74,99996250 BRVA** · `tbrv1qgztgz…7cevp2` → **24,99998750 BRVA** |
| Comisión de red | **5.000 sats** (0,00005 BRVA) — la absorben los mineros, sin vuelto |
| Comisión de pool | 0% |
| Artefacto (no cambió) | `testnet-payout-1.json`, hash `80bedce2…` |

Ya revalidé (sin tocar el paquete): las 2 monedas siguen **sin gastar**, son coinbase del pool y están **maduras** (217/216 confirmaciones). Todo cierra. Además **pre-probé el armado de la transacción** (sin firmar ni transmitir): produce exactamente las 2 entradas + los 2 destinatarios con los montos correctos + comisión de 5000 sats sin vuelto. Solo falta tu firma.

## Tu ÚNICA acción
En tu máquina offline con la billetera del pool cargada, corré:

    ./firmar-pago-testnet.sh bitcoin-cli

(si tu `bitcoin-cli` está en otro lado o tu billetera tiene otro nombre, ajustás:
`./firmar-pago-testnet.sh /ruta/bitcoin-cli "-datadir=/mi/dir -rpcwallet=pool"`)

El script arma la transacción exacta, te la muestra para revisar, la **firma con tu billetera**, y te imprime dos líneas (`TXID=…` y `HEX=…`). **Copiá y pegámelas.** Nada más.

## Qué hago yo (después de que me pases el HEX)
Automáticamente, sin tu clave ni tu semilla:
1. Verifico la firma **contra el lote** con la propia verificación de la pool (entradas, destinatarios, montos, comisión, sin salidas raras).
2. Transmito la transacción a la red.
3. Sigo las confirmaciones hasta que el pago quede confirmado y la contabilidad cierre.

## Seguridad (garantías)
- **Nunca veo ni te pido tus 12 palabras.** El script firma con la billetera ya cargada; yo no toco la clave.
- **No firmo yo** — firmás vos, offline.
- **No transmito nada antes de tu firma.** Recién transmito el hex que YA firmaste, después de verificarlo.
- Es testnet: si algo saliera mal, no se pierde plata real.
