# Guía mínima — verificar y firmar un pago de la pool (Fernando)

La pool NUNCA firma (no tiene la clave). Arma el pago sin firmar y lo deja en un artefacto.
Fernando solo tiene que **verificar** y **firmar offline**. Probado end-to-end en testnet (2026-07-18).

## LOTE ACTUAL LISTO PARA FIRMAR (testnet) — ⏰ FECHA LÍMITE: antes del 29-jul-2026 03:00 UTC
Es el pago de PRUEBA de la pool (BRVA de testnet, sin valor real). Hay que firmarlo/transmitirlo ANTES de que se apague la testnet (29-jul 03:00 UTC), porque es la única red donde se valida el flujo de pago con RandomX. Después no hay ventana.
- **Archivo exacto** (en el VPS Hostinger, `/root/brisvia-pool/`): `payouts_testnet/testnet-payout-1.json`. Ya trae los `comandos_sugeridos` (walletcreatefundedpsbt con los UTXO exactos).
- **Monto total**: 100 BRVA (10.000.000.000 sats). Comisión de pool 0%. Comisión de red reservada: 5.000 sats (tope 1.000.000).
- **Destinatarios** (2, reparto 75/25):
  - `tbrv1qwk84fz4v99we9l584tweldegd3d032t9240hn0` → 74,99996250 BRVA (7.499.996.250 sats)
  - `tbrv1qgztgzvy5zu8ul6cxp7ugntd5d8p8452s7cevp2` → 24,99998750 BRVA (2.499.998.750 sats)
- **UTXO reservados** (2 coinbase maduras, 50 BRVA c/u): `07ac6208…de87:0` y `c47f526c…3292:0`.
- **Tu acción**: en tu máquina OFFLINE con la clave del pool, seguir los 7 pasos de abajo. NO lo firmo/transmito yo (custodia tuya). Es testnet: sirve para dejar el flujo probado antes de mainnet.
- **Revalidado 2026-07-18** (sin tocar el paquete): los 2 UTXO reservados (`07ac6208…:0` y `c47f526c…:0`) siguen **SIN GASTAR**, son coinbase del pool (a `tbrv1q0t5nlyvr265ve94sn8fjc34urqks4m9etpnyul`), **MADUROS** (217 y 216 confirmaciones ≥ 100), 50 BRVA c/u = 100 BRVA. Hash del artefacto: `80bedce2…` (inmutable). Todo listo; falta solo tu firma (paso 4: `testmempoolaccept` antes de transmitir).

## Qué dejó armado la pool
- Artefacto: `payouts_<red>/<batch_id>.json` (ej. `payouts_testnet/testnet-payout-1.json`).
- Contiene: los **UTXO reservados** (las coinbase maduras que se van a gastar), los **destinatarios** con su
  monto neto, la comisión de pool (0%) y la comisión de red (la absorben los mineros, proporcional).
- En el ledger, esos saldos ya están **congelados** (pending -> reserved). Estado del lote: `prepared`.

## Pasos para Fernando (en la máquina OFFLINE con la clave del pool)

1. **Verificar el artefacto** (mirar el JSON):
   - que los `reserved_inputs` (txid:vout) sean coinbase maduras del pool y estén **sin gastar**
     (`bitcoin-cli gettxout <txid> <vout>` -> debe existir);
   - que los `destinations_sats` sumen = total de saldos pagables, y que las direcciones sean las de los mineros;
   - que la comisión total (`fee_reserve_sats`) sea razonable y no supere `max_fee_sats` (1.000.000 sats).

2. **Armar el PSBT** gastando EXACTAMENTE esos UTXO, restando la comisión de las salidas
   (para que la absorban los mineros), con el comando `walletcreatefundedpsbt` que ya viene en el artefacto
   (campo `comandos_sugeridos`). Sin `changeAddress` externo salvo el vuelto del pool.

3. **Firmar OFFLINE**: `walletprocesspsbt <psbt>` -> `finalizepsbt <psbt>` -> obtenés el `hex`.

4. **Verificar la firma antes de transmitir**: `testmempoolaccept '["<hex>"]'` -> debe dar `allowed: true`.
   Revisar de nuevo: entradas = las reservadas, salidas = destinatarios exactos, sin salidas extra,
   comisión bajo el máximo.

5. **Persistir la tx firmada ANTES de transmitir** (idempotencia): registrar el `hex` + `txid` en el lote
   (`attach_signed_tx`). Esto evita el doble pago si algo se cae: ante caída se retransmiten los MISMOS bytes,
   nunca se reconstruye.

6. **Transmitir**: `sendrawtransaction <hex>` -> guardar el `txid`.

7. **Confirmar el lote**: cuando el txid alcance la profundidad (10 confirmaciones), el lote pasa
   `reserved -> paid` (neto). Reconciliar: la contabilidad tiene que cerrar.

## Control posterior (cuando Fernando transmita) — lo hago yo
Apenas Fernando pase el `txid` (o después de transmitir), verifico sin tocar claves:
- `bitcoin-cli ... gettransaction <txid>` / `getrawtransaction <txid> 1` → confirma que entró y sus salidas = los 2 destinatarios exactos, montos correctos, comisión bajo el máximo.
- Seguir `confirmations` hasta ≥10 → el lote pasa `reserved → paid`.
- `reconcile()` del ledger → la contabilidad cierra (sin faltantes ni doble pago).
- Si `sendrawtransaction` diera "already in UTXO set" = ya estaba transmitida (reconciliación, NO re-pago).
Esto deja la prueba de pago cerrada en testnet antes de que se apague (29-jul).

## Reglas de oro
- Si `sendrawtransaction` dice "ya está en el UTXO set" = ya se transmitió antes: es **reconciliación**, NO re-pago.
- Nunca firmar en el servidor stratum. La clave vive OFFLINE, en otra máquina.
- Sin RBF automático en el lanzamiento.
- Las primeras semanas: madurez 200 confirmaciones + revisión manual antes de firmar.
