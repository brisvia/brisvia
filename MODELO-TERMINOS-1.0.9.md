# Modelo canónico de estados y saldos — Brisvia 1.0.9 (ES/EN)

Única fuente de la verdad para la interfaz. Regla dura: **una share aceptada NO es un bloque ni un pago**, y
**nunca** se muestran importes de pool que no provengan del **ledger real** (nada de estimados, proyecciones,
ni conversión a fiat inventada). Si no hay dato del ledger para un importe, se muestra el estado y una
explicación honesta, nunca un número inventado.

## Tabla de términos (clave técnica → etiqueta ES / EN)

| clave técnica | ES (etiqueta) | EN (label) | definición |
|---|---|---|---|
| `accepted_shares` | Aportes aceptados | Accepted shares | Trabajo aportado y **confirmado por la pool**. Prueba de trabajo real; NO es un bloque ni un pago. |
| `rejected_shares` | Aportes rechazados | Rejected shares | Aportes que la pool no aceptó (dificultad baja, duplicado, job viejo…). Se muestra el motivo. |
| `pending_rewards` | Recompensas pendientes | Pending rewards | Importes **ya asignados** desde bloques de la pool que todavía esperan confirmaciones/madurez. |
| `available_for_payout` | Disponible para pago | Available for payout | Confirmado y **elegible** para el próximo lote de pago. |
| `paid` | Pagado | Paid | **Transmitido** por la pool. Internamente se distingue transmitido de confirmado cuando corresponde. |
| `last_payout` | Último pago | Last payout | Fecha/monto del último pago efectivo (del ledger). |

## Definiciones obligatorias (texto de ayuda)
- **Aporte aceptado / Accepted share:** demuestra trabajo aportado, pero **no equivale a un bloque ni a un pago**.
- **Recompensas pendientes / Pending rewards:** importes ya asignados desde bloques de la pool que **todavía
  esperan confirmaciones o madurez** (madurez de coinbase). No retirables aún.
- **Disponible para pago / Available for payout:** **confirmado y elegible** para el siguiente lote.
- **Pagado / Paid:** **transmitido** por la pool. Cuando el dato exista, distinguir "transmitido" de
  "confirmado en la cadena".

## Prohibido como etiqueta principal
- "Saldo madurando" / "Maturing balance" (ambiguo).
- "Mining balance" a secas (ambiguo: ¿pendiente? ¿disponible? ¿pagado?).
- Montos **estimados** no respaldados por el ledger.
- **Ganancias proyectadas** / rentabilidad estimada / conversión a fiat.

## Máquina de estados de conexión de la pool (ya implementada en 1.0.9)
Fuente única Rust→UI (`miner_status().pool.phase`). Nunca "conectado" == "aportando trabajo".

| fase | ES | EN | significado |
|---|---|---|---|
| `connecting` | Conectando al grupo… | Connecting to the pool… | abriendo socket / antes del login |
| `authenticated` | Autenticado, esperando trabajo… | Authenticated, waiting for work… | login aceptado, sin job todavía |
| `waiting` | Esperando trabajo del grupo… | Waiting for work from the pool… | autenticado, sin job activo |
| `working` | Aportando trabajo | Contributing work | **job activo + calculando** (único estado de trabajo real) |
| `reconnecting` | Reconectando… (+ cuenta regresiva) | Reconnecting… (+ countdown) | caída temporal, backoff con `retrySecs` |
| `suspended` | Grupo en mantenimiento… (+ cuenta regresiva) | Pool under maintenance… (+ countdown) | mantenimiento server-side; prioridad sobre backoff |
| `disconnected` | Sin conexión al grupo | Not connected to the pool | detenido / sin reconexión activa |

Regla: el minero **nunca** cae silenciosamente de POOL a SOLO; una pool apagada/inactiva se ve como
connecting/reconnecting/disconnected, no como solo.

## Estado de recompensas/pagos en 1.0.9
El importe de saldo **en vivo** (pending/available/paid por dirección) requiere el endpoint de saldo con
autenticación por firma, **diferido**. Mientras no exista ese endpoint confiable, la UI:
- muestra `accepted_shares` / `rejected_shares` (datos reales del minero);
- muestra el aviso honesto: "una share aceptada quedó registrada, pero no es un bloque ni un pago; las
  recompensas maduran; los primeros pagos quedan pendientes hasta el pago de control; el monto exacto todavía
  no se muestra acá";
- **no** inventa ni estima montos. Estado vacío honesto ("Sin pagos todavía / No payouts yet") cuando aplique.

Cuando el endpoint del ledger exista, se llenan `pending_rewards`/`available_for_payout`/`paid`/`last_payout`
con los valores reales, usando exactamente las etiquetas de la tabla.
