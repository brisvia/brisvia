# UX Mining Audit — Brisvia 1.0.9

Investigación acotada de 8 mineros y paneles de pool reales (fuentes oficiales) para orientar la pantalla de Minería del minero de escritorio Brisvia (Tauri, SOLO + POOL oficial, RandomX/CPU, comisión 0%, pagos PPLNS con madurez de coinbase).

Regla dura del proyecto que atraviesa todo el análisis:
- Una **share aceptada NO es un bloque ni un pago**.
- **Nunca** mostrar importes de pool que no vengan del ledger real (nada de estimados/proyecciones).

Muestra elegida: 4 mineros (3 técnicos + 1 principiante) y 4 paneles de pool (2 técnicos PPLNS + 2 orientados a principiante/multi-coin).

---

## 1) Tabla resumen

| Producto | Tipo | Pantalla principal | Conexión vs trabajo real | Términos shares/pagos | Fuente oficial |
|---|---|---|---|---|---|
| **XMRig** | Minero técnico CPU (RandomX) | Log de consola por línea con timestamp | Línea de conexión (`use pool`) separada de líneas `accepted`/`speed` | `accepted (a/r)`, `rejected`, `diff`, `speed 10s/60s/15m`, `new job` | xmrig.com/docs/miner, github.com/xmrig/xmrig |
| **T-Rex** | Minero técnico GPU | Tabla periódica por GPU + resumen | Estado de pool vs contadores de shares por GPU | `accepted`, `rejected`, `invalid`, `solved` (bloques) | github.com/trexminer/T-Rex (+ wiki) |
| **lolMiner** | Minero técnico GPU | Caja de estadísticas (compact/default/extended) | Conexión de pool vs `accepted/stale/rejected` + effective hashrate | `accepted`, `stale`, `rejected`, `defect`, `effective hashrate`, `average hashrate` | github.com/Lolliedieb/lolMiner-releases |
| **NiceHash QuickMiner** | Minero principiante | Panel simple: hashrate, ganancias, consumo, temp en vivo | No expone socket; muestra estado global y `unpaid balance` | `unpaid balance`, `payment threshold`, `efficiency` | nicehash.com/quickminer + support |
| **2Miners** | Panel pool PPLNS | Dashboard por worker + balances | `Workers online/offline` derivado de shares, no de socket | `reported`/`current` hashrate, `valid`/`stale`/`invalid shares`, `immature`/`unconfirmed`/`confirmed balance`, `PPLNS`, `luck`, `confirmations` | 2miners.com/faq |
| **Ethermine** | Panel pool | Dashboard: hashrate, shares, balance | `reported hashrate` vs `effective/current hashrate` (ventana 60 min) | `valid`/`stale`/`invalid shares`, `% invalid`, `unpaid balance`, `estimated daily earnings` (estimado) | ethermine.org + help |
| **f2pool** | Panel pool | Dashboard/worker con `last share` y hashrate 15m | `Online` = envió shares últimos 10 min; `Dead` = sin shares 24 h | `last share`, `15m hashrate`, worker `Online/Offline/Dead` | f2pool.zendesk.com |
| **unMineable** | Panel pool principiante multi-coin | Dashboard unificado: balance, workers, hashrate, payouts | `Active workers` + hashrate actual, sin socket crudo | `unpaid`/`pending balance`, `payment threshold` (por asset), fee 1% | unmineable.com/support |

---

## 2) Por producto

### XMRig (fuente: xmrig.com/docs/miner, github.com/xmrig/xmrig)
- **Pantalla principal:** log lineal con timestamp. Ej. real: `accepted (183/0) diff 959126 (327 ms)` y `speed 10s/60s/15m 16690.4 16676.3 16594.5 H/s max 17788.9 H/s`.
- **Conexión vs trabajo:** son líneas distintas. La conexión (`use pool ...`) y `new job` no implican trabajo; el trabajo real aparece recién con `accepted` y con `speed` distinto de `n/a`.
- **Términos:** `accepted (aceptadas/rechazadas)`, `rejected`, `diff` (dificultad de la share), `speed 10s/60s/15m` (3 promedios), `max`.
- **Claridad:** para principiante es denso (todo en una tira de texto); el `(a/r)` acumulado es entendible. Confuso: `diff` de la share se confunde con dificultad de red; `speed n/a` inicial asusta.
- **Info técnica:** hashrate 10s/60s/15m + máximo, tiempo de respuesta de la share (ms), dificultad por share.
- **ADOPTAR:** el patrón `accepted (aceptadas/rechazadas)` acumulado + tres ventanas de hashrate + latencia de share en ms.
- **EVITAR:** volcar todo como log crudo sin estado resumido arriba; el `n/a` inicial sin explicación.

### T-Rex (fuente: github.com/trexminer/T-Rex + wiki)
- **Pantalla principal:** tabla periódica por GPU + línea resumen; intervalos configurables (`--gpu-report-interval`, `--hashrate-avr`, `--sharerate-avr`).
- **Conexión vs trabajo:** separa contadores por dispositivo (`accepted_count`, `rejected_count`, `invalid_count`, `solved_count`).
- **Términos clave:** distingue 4 tipos: `accepted` (válida), `rejected` (rechazada por pool), `invalid` (mal calculada/stale local), `solved` = **bloques encontrados** (no shares).
- **Claridad:** separar `solved` (bloque real) de `accepted` (share) es exactamente la distinción que Brisvia necesita.
- **Info técnica:** hashrate promedio día/hora/minuto, ventana deslizante configurable, `--validate-shares`.
- **ADOPTAR:** contador aparte y explícito de `solved`/bloques vs `accepted`/shares. Refuerza "una share no es un bloque".
- **EVITAR:** colapsar `stale` e `invalid` en un solo número (genera diagnósticos errados).

### lolMiner (fuente: github.com/Lolliedieb/lolMiner-releases)
- **Pantalla principal:** "statistic box" con formato `compact`/`default`/`extended`.
- **Conexión vs trabajo:** el estado de pool va aparte; el trabajo se mide con shares y **effective hashrate**.
- **Términos:** `accepted`, `stale`, `rejected`, `defect shares` (los separa; ya no colapsa `stale` y `defect`), `average hashrate`, `effective hashrate`.
- **Claridad:** distinguir `average` (lo que calcula tu equipo) de `effective` (lo que la pool reconoce por shares) evita el reclamo típico "mi hashrate no coincide".
- **ADOPTAR:** mostrar `effective hashrate` (reconocido por shares) además del hashrate local; niveles de detalle (compact/extended).
- **EVITAR:** un único formato denso sin modo compacto para principiante.

### NiceHash QuickMiner (fuente: nicehash.com/quickminer + support)
- **Pantalla principal:** panel simple y en vivo: hashrate, ganancias, consumo, temperatura; setup mínimo.
- **Conexión vs trabajo:** no expone el socket; muestra estado global "andando/no" + `unpaid balance`.
- **Términos:** `unpaid balance`, `payment threshold` (paga al cruzar el umbral), `efficiency`.
- **Claridad:** muy accesible para principiante; el riesgo es que muestra ganancias estimadas (marketplace) que NO son ledger — eso es justamente lo que Brisvia debe evitar.
- **ADOPTAR:** simplicidad radical de la vista por defecto + un solo indicador de estado claro.
- **EVITAR:** mezclar estimaciones de ganancia con balance real; para Brisvia, cero estimados.

### 2Miners (fuente: 2miners.com/faq)
- **Pantalla principal:** dashboard por worker con hashrate, shares, balances y bloques.
- **Conexión vs trabajo:** `workers online/offline` se deriva de shares enviadas, no de un socket abierto.
- **Términos:** `reported hashrate` (lo que dice el software) vs `current/average hashrate` (calculado por shares); `valid`/`stale`/`invalid shares`; `immature`/`unconfirmed balance` (bloque hallado, esperando confirmaciones) vs `confirmed balance` (retirable); `PPLNS` = Pay Per Last N Shares; `confirmations`/madurez; `luck`; `payout threshold`.
- **Claridad:** el modelo `immature → confirmed` es el mapa mental correcto para coinbase con madurez. `luck` puede confundir a principiantes.
- **ADOPTAR:** estados de balance en 3 niveles (`immature/pending` → `confirmed`) atados a confirmaciones; explicación de PPLNS = "últimas N shares".
- **EVITAR:** exponer `luck` sin explicación; puede leerse como que "el pool decide" el pago.

### Ethermine (fuente: ethermine.org + help)
- **Pantalla principal:** dashboard con hashrate, shares, `unpaid balance`, y por worker `Effective/Reported Hashrate` + `Shares (Invalid/Stale/Valid/% Invalid)`.
- **Conexión vs trabajo:** `effective/current hashrate` se calcula por shares en ventana de 60 min ("tarda hasta 2 h en ser preciso"); el reported es solo declarado.
- **Términos:** `valid`/`stale`/`invalid shares`, `% invalid`, `reported` vs `effective/current hashrate`, `unpaid balance`, `estimated daily earnings`.
- **Claridad:** buena separación reported/effective y desglose de shares. **Anti-patrón para Brisvia:** `estimated daily earnings` es una proyección → prohibido replicar.
- **ADOPTAR:** desglose `Valid/Stale/Invalid` + `% invalid` como salud de la conexión; aviso de "tarda X en estabilizarse".
- **EVITAR:** `estimated daily earnings`/proyecciones de ganancia.

### f2pool (fuente: f2pool.zendesk.com)
- **Pantalla principal:** dashboard/worker con `15m hashrate` y `last share`.
- **Conexión vs trabajo (patrón estrella):** NO muestran socket. Definen el estado del worker por **trabajo real**: `Online` = envió shares en los últimos 10 min; `Offline` = sin shares 10 min; `Dead/Inactive` = sin shares 24 h. `last share` = último envío (normalmente < 3 min).
- **Términos:** `last share`, `15m hashrate` (recibido real), worker `Online/Offline/Dead`.
- **Claridad:** "estás minando de verdad" = "enviaste una share hace poco". Modelo mental perfecto y honesto.
- **ADOPTAR:** definir "minando" por `última share aceptada hace X`, no por socket conectado. Copiar el umbral tipo "sin shares por N min → no estás trabajando".
- **EVITAR:** advertir que el `15m hashrate` fluctúa mucho y no sirve como medida instantánea (mostrarlo como promedio, no como dato duro).

### unMineable (fuente: unmineable.com/support)
- **Pantalla principal:** dashboard unificado: balance total, `active workers`, hashrate actual, payouts, actividad reciente.
- **Conexión vs trabajo:** `active workers` + hashrate actual; sin socket crudo.
- **Términos:** `unpaid`/`pending balance`, `payment threshold` (varía por asset), fee 1%, historial de payouts.
- **Claridad:** todo en un solo lugar es bueno para principiante; el umbral por asset es claro.
- **ADOPTAR:** vista unificada (estado + balance + últimos payouts) en una sola pantalla; historial de pagos reales visible.
- **EVITAR:** ("N/A" para Brisvia: comisión 0%, no 1%) — no copiar lenguaje de fee.

---

## 3) Conclusiones para Brisvia 1.0.9

### Qué ADOPTAR
1. **Estado por trabajo real, no por socket (patrón f2pool):** el indicador "Estás minando" debe basarse en `última share aceptada hace X segundos`, no en "conectado a la pool". Socket conectado ≠ minando.
2. **Contador acumulado de shares (patrón XMRig):** `Aceptadas / Rechazadas` visible arriba, más 3 ventanas de hashrate (10s / 60s / 15m) y latencia de la última share en ms.
3. **Separar explícitamente bloque de share (patrón T-Rex):** un contador aparte de `Bloques encontrados` (solved) distinto de `Shares aceptadas`. Refuerza la regla dura del proyecto.
4. **Hashrate local vs efectivo (patrón lolMiner/Ethermine):** mostrar el hashrate del equipo y, en modo pool, el `effective` reconocido por shares, con aviso "tarda unos minutos en estabilizarse".
5. **Balance en 3 estados atados a madurez de coinbase (patrón 2Miners):** `Inmaduro/pendiente` (bloque hallado, esperando confirmaciones) → `Confirmado` (retirable). Mostrar número de confirmaciones faltantes.
6. **Modo simple por defecto (patrón QuickMiner) + modo detallado (patrón lolMiner extended):** vista limpia para el principiante, con un despliegue técnico opcional.
7. **Salud de conexión con desglose Valid/Stale/Invalid + % (patrón Ethermine):** útil como diagnóstico, sin inventar ganancias.

### Cómo nombrar CONEXIÓN vs TRABAJO (evitar el error clásico)
- **Conexión (socket/login):** "Conectado a la pool" / "Autenticado". Estado técnico, NO implica minar.
- **Trabajo real (shares/hashrate):** "Minando" solo cuando hay shares aceptadas recientes. Indicador central: `Última share aceptada: hace 8 s`.
- Regla de copy: nunca decir "Minando" ni "Ganando" por estar conectado. Estado en 3 niveles: `Conectando` → `Conectado (sin shares aún)` → `Minando (share hace Xs)`.
- Términos en inglés a reutilizar tal cual en la UI técnica: `accepted`, `rejected`, `stale`, `effective hashrate`, `last share`, `PPLNS`, `immature/confirmed balance`.

### Cómo tratar recompensas y pagos HONESTAMENTE (sin estimados)
- **Prohibido:** `estimated daily earnings`, proyecciones, "vas a ganar ~X", conversión a fiat inventada. (Anti-patrón visto en Ethermine y NiceHash.)
- **Permitido solo desde el ledger real de la pool:** `balance inmaduro/pendiente`, `balance confirmado`, `total pagado`, historial de payouts reales, `payout threshold`, confirmaciones faltantes.
- Si el ledger aún no devolvió un dato, mostrar vacío honesto ("Sin pagos todavía"), nunca un número calculado localmente.
- Una share aceptada suma "trabajo aportado a la ronda PPLNS", no dinero. El dinero aparece solo cuando el bloque madura y el ledger lo acredita.

---

## 4) Riesgos / anti-patrones más frecuentes detectados

1. **Confundir conexión con minado:** varios mineros muestran "connected/new job" y el principiante cree que ya gana. → Brisvia: estado atado a shares, no a socket.
2. **Ganancias estimadas presentadas como reales:** `estimated daily earnings` (Ethermine) y ganancias de marketplace (NiceHash). → Prohibido en Brisvia.
3. **Colapsar tipos de share:** juntar `stale`/`invalid`/`defect` en un número oculta la causa (latencia vs overclock). → Separarlos.
4. **`reported` vs `effective` sin explicar:** genera el reclamo eterno "mi hashrate no coincide". → Mostrar ambos y aclarar la ventana de estabilización.
5. **Log crudo sin estado resumido (XMRig):** denso para principiantes. → Resumen arriba, log detallado opcional abajo.
6. **`luck` sin contexto:** se lee como que el pool manipula pagos. → Explicar o esconder en modo simple.
7. **Balance sin distinguir madurez:** mostrar un único "balance" sin `inmaduro → confirmado` sugiere plata retirable que no lo es. → 3 estados + confirmaciones.
8. **Hashrate instantáneo tratado como dato duro:** el 15m fluctúa (f2pool lo advierte). → Presentar como promedio, no como medición exacta.
