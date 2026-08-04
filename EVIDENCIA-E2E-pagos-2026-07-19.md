# Evidencia — Prueba E2E del motor de pagos de la pool (2026-07-19)

Red de PRUEBA `brisvia-test`. Aislada (base de datos y artefactos separados; NO se tocó `testnet_run_ledger.db`
ni la pool viva). Custodia DESCARTABLE (semilla testnet única, en memoria, no persistida, borrada al final).
Aprobado por ChatGPT (RELEASE_REVIEW, confianza ALTA). No bloquea el lanzamiento de mainnet del 1-ago.

## Camino ejercitado (los MISMOS módulos de producción)
`pplns_ledger` + `payouts.PayoutManager` + `tx_verifier` + `chain_source`, firma offline P2WPKH con `embit`.

1. 2 coinbase reales minadas a la custodia (50 BRVA c/u) + maduradas (107/106 confirmaciones).
2. El pool detecta y madura los 2 bloques **verificando contra la cadena** (`verify_block_payable`);
   acredita 100 BRVA al minero por PPLNS.
3. Prepara lote (reserva de los 2 UTXO coinbase + congela saldo). Firma offline.
4. `verify_and_attach_signed` OK → `testmempoolaccept allowed=True` → broadcast → **confirmado con 10 bloques**.
5. Saldo del minero: `reserved` → `paid` (9 999 995 000 sats). Comisión 5000 (la absorbe el minero; pool 0%).
6. `reconcile`: `ok=True, consistent=True`.

- **txid confirmado en cadena de prueba:** `af25d18e8c0a7c439da3ead75540b435c6cafa6c08c809bbe12a61bab45ed56d`
- transición `prepared → signed → broadcast → confirmed` verificada.

## Rechazos obligatorios (todos con el motivo correcto)
| Caso | Resultado |
|---|---|
| destinatario incorrecto | `unexpected_output` |
| monto incorrecto | `amount_mismatch` |
| salida extra del atacante | `unexpected_output` |
| comisión > máximo | `fee_too_high` |
| entrada inesperada | `inputs_mismatch` |
| entrada ausente | `inputs_mismatch` |
| firma incompleta | `not_complete` |
| firma corrupta (nodo) | `testmempoolaccept allowed=False` (OP_EQUALVERIFY) |
| doble gasto / entrada ya gastada (nodo) | `testmempoolaccept allowed=False` (missing-inputs); el gate impide transmitir |
| re-broadcast (idempotencia) | `already_broadcast` (no re-paga) |
| recuperación tras caída | `recover_on_startup` reconcilia por txid (no arma otra tx) |

## Hallazgos y correcciones
1. **Endianness del outpoint (bloqueante para transmitir, sin pérdida de fondos) — CORREGIDO.**
   El firmador construía la entrada con el txid invertido (`bytes.fromhex(txid)[::-1]`); embit espera el
   txid en orden de display. Corregido en `firma-pago/firmar_pago_local.py` (y su copia de escritorio).
   La prueba E2E real fue lo que lo detectó (el dry-run/unit no lo cazaba). Congelado con regresión:
   `pool/test_e2e_regression.py`. Búsqueda global: era la única inversión manual incorrecta de txid.

2. **Política de transacción determinista — AGREGADA al código canónico (sin desplegar).**
   `tx_verifier.verify_payout_tx` ahora exige (si se le pasan) secuencia final `0xffffffff` en todas las
   entradas, `locktime == 0` y `version == 2`. `payouts.verify_and_attach_signed` acepta `tx_meta` y lo
   pasa; `firma-pago/validar-pago-firmado.py` lo provee desde `decoderawtransaction`. Tests:
   `pool/test_e2e_regression.py`. Los tests existentes (`test_pool_security`, `test_pool_accounting`) siguen OK.

## Estado: bloque FORMALMENTE CERRADO (ChatGPT, confianza ALTA)
El motor de pagos queda probado para la fase actual. No bloquea mainnet del 1-ago (pool pública apagada).

## Paquete PRE-APERTURA del pool público (post-lanzamiento, no bloquea el 1-ago)
Gates obligatorios antes de habilitar la pool pública:
- Desplegar estos cambios canónicos al server (hoy NO desplegados).
- Crear y **respaldar** la custodia definitiva; ensayar su recuperación desde el respaldo.
- Primer pago real controlado de importe mínimo con esa custodia.
- La custodia testnet perdida (`7ae93f91...`, 750 BRVA) queda permanentemente marcada como irrecuperable
  y excluida de selección/balances (sumar un test que impida que sus UTXO vuelvan a ser candidatos).

Endurecimientos del parser de política (`parse_tx_policy`) para el paquete pre-apertura:
- comprobar que consume el hex COMPLETO y rechaza bytes sobrantes;
- probar datos truncados, varint malformados y SegWit incompleto;
- comparar una colección de transacciones contra `decoderawtransaction` del nodo.

Verificado ahora (adelantado): ninguna ruta llega a `broadcast_batch` sin pasar antes por
`verify_and_attach_signed` — `broadcast_batch` exige estado `signed`, que solo produce `record_signed_tx`
(invocado únicamente por `verify_and_attach_signed` tras verificar todo). `recover_on_startup` solo
retransmite bytes de lotes ya `signed`/`broadcast`.
