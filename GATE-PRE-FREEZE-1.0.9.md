# Gate obligatorio antes de congelar 1.0.9 (ChatGPT RELEASE_REVIEW, ALTA)

APROBADO CON 5 AJUSTES OBLIGATORIOS. Cerrar estos 5 → congelar alcance → versión 1.0.9 una sola vez →
repack AppImage sin GLib → POOL_ENABLED=true en candidato + adaptar guard → builds 3 OS → migración
1.0.8→1.0.9 → E2E real. No publicar, no tocar latest.json.

Riesgo de FONDOS (críticos, no dejar para después): C (badge falso) y B (onboarding en billetera heredada).

## ESTADO 2026-07-19 — PARTE CRÍTICA DE A/B/C/D APLICADA Y VERIFICADA (node + cargo verdes)
- **A (cifrado desconocido) — HECHO** (app.js): tri-estado `walletCryptoKnown`; `kind()` que falla = UNKNOWN, nunca "unencrypted"; re-consulta al abrir Enviar; en UNKNOWN no oculta el campo de contraseña, bloquea SOLO enviar, muestra `send.crypto_unknown`, reintenta cada 3s; recibir/dirección siguen OK. `walletEncrypted=true` tras proteger también marca `walletCryptoKnown=true`.
- **B (heredadas) — CRÍTICO HECHO** (lib.rs `wallet_seed_on_disk`): existe billetera si hay `wallet_seed.enc` O `wallet_seed_phrase.txt` → un heredado NUNCA dispara onboarding de creación. PENDIENTE (refinamiento): validación BIP39 → `legacy_wallet_valid`/`legacy_wallet_corrupt` + migración atómica byte-consistente antes de retirar el .txt.
- **C (badge respaldo) — HECHO** (lib.rs): quitado el `true` fijo; flag persistido `backup_verified.json` {verified, fingerprint, method, date} escrito SOLO en `wallet_verify_backup` OK; `wallet_summary`/`wallet_confirm_backup` leen el flag (default false); "ya las guardé" NO lo prende.
- **D ("Preparando…" infinito) — CRÍTICO HECHO** (app.js refreshMine): el badge sigue "prep" (nunca "minando"); aviso suave a 30s (`preparing_slow`), aviso de posible bloqueo + cómo reintentar a 120s (`preparing_stuck`). PENDIENTE (refinamiento): cancelación automática por heartbeat real del worker (emitir progreso desde el Rust del worker).
- **E (QR) — HECHO**: QR falso quitado.
**Veredicto ChatGPT (2ª ronda, ALTA): gate NO cerrado. B (validación+migración heredada) es BLOQUEANTE; D-heartbeat NO. Ajustes menores a A/C aplicados.**

### AJUSTES A/C/D aplicados tras el veredicto (verificados node+cargo)
- **A**: reintento PROGRESIVO 3→5→10s (cap) con `sendCryptoTimer` + `clearSendCryptoTimer`; se cancela al cerrar Enviar o cuando deja de ser unknown.
- **C**: `write_backup_verified` ahora ATÓMICO (tmp+rename) + perms 0600 (unix); nuevo `clear_backup_verified` llamado en `wallet_create_bip39`/`wallet_restore_bip39` → una wallet nueva/restaurada empieza NO verificada (el badge no se hereda).
- **D**: botón VISIBLE `#mine-retry` ("Detener y reintentar") que aparece a los 120s; su handler hace `stop` (termina el worker → mining=false) → `start` fresco (stop-before-start = nunca dos workers). El aviso escalonado 30s/120s queda; heartbeat automático = P1 posterior.

### B — CERRADO (2026-07-19, verificado node+cargo+cargo test)
- Backend: `wallet_legacy_status` + `classify_legacy_phrase` (12/24 palabras + checksum BIP39 → `legacy_valid`/`legacy_corrupt`), registrado en el handler; `wallet_seed_on_disk` reconoce `.enc` O `.txt`; `wallet_migrate_encrypt` ahora REABRE el `.enc` y verifica MISMA HUELLA antes de borrar el `.txt` (`ERR:MIGRATE_VERIFY` si no coincide). Test `legacy_phrase_classification` PASA.
- Frontend: `wallet.legacyStatus()` en el bridge (+mock); en el arranque `legacy_corrupt` → recuperación (`setupStep('import')`), NUNCA create.
- Regresiones: `gate-regressions.spec.js` (corrupto→recuperación; válido→normal; A nodo caído→Enviar bloqueado+contraseña visible+recibir OK). Sintaxis verificada; corren en CI.

**GATE COMPLETO: A/B/C/D/E aplicados y verificados + regresiones escritas. Pendiente: confirmación de ChatGPT y luego el freeze (versión 1.0.9 + POOL_ENABLED en candidata + guard + repack AppImage + builds 3 OS + E2E real, sin publicar).**

### (histórico) B — dimensionamiento previo, ya resuelto:
YA existe en `lib.rs`: `wallet_fingerprint(datadir)` (huella vía listdescriptors, :1520); `wallet_check_backup(words)` (valida BIP39 con `Mnemonic::parse` + `descriptors_from_mnemonic` → compara huella, :1539); `wallet_migrate_encrypt` (:~1493, migra viejo→cifrado). `Mnemonic::parse` disponible para validar BIP39.
FALTA (tramo dedicado, toca recuperación de FONDOS — no hacer a medias):
1. Comando `wallet_legacy_status`: lee `wallet_seed_phrase.txt`, `Mnemonic::parse` → devuelve `legacy_valid` / `legacy_corrupt` / `none`.
2. `wallet_migrate_encrypt` (:1512-1514): hoy `encrypt_phrase_file` → `remove_file(.txt)` SIN verificar. Agregar: tras escribir `wallet_seed.enc`, ABRIRLO y comprobar que deriva la MISMA huella (usar `wallet_fingerprint`/decrypt) ANTES de `remove_file` del .txt. Si no coincide → NO borrar, error.
3. Frontend (app.js arranque, tras `seedOnDisk`): si existe billetera pero es HEREDADA (solo .txt, sin .enc) → consultar `wallet_legacy_status`: `legacy_valid` → ofrecer migración (proteger, reusar `openProtect`), NO crear; `legacy_corrupt` → mostrar recuperación/reparación (NO onboarding de creación).
4. Regresiones (ChatGPT, obligatorias antes del freeze): nodo caído en Enviar; heredado válido; heredado corrupto; migración interrumpida; huella distinta; preparación trabada; detener+reintentar sin worker duplicado.

## Los 5 ajustes obligatorios

### A. Estado de cifrado de billetera DESCONOCIDO (casi P0)
`app.js:459` lee `walletEncrypted` de `wallet.kind()` best-effort (`catch {}`); `app.js:677` oculta el campo de
contraseña si es false. Fix (NO usar `encrypted=true` a secas):
- estado tri-valor: `encrypted` / `unencrypted` / `unknown`;
- ante `unknown`: NO ocultar el campo de contraseña, NO intentar enviar; mostrar "No se pudo verificar la
  billetera. Reintentando…" + permitir reintentar; re-fetch `kind()` al abrir Enviar;
- nunca interpretar un fallo del nodo como "billetera sin contraseña".

### B. Billeteras antiguas + onboarding (P0, riesgo de fondos)
`wallet_seed_on_disk` (`lib.rs:956`) solo mira `wallet_seed.enc`. Una billetera heredada (solo
`wallet_seed_phrase.txt`) se lee como "no hay billetera" → onboarding reaparece tras update. Fix:
- detección local canónica reconoce `wallet_seed.enc` Y el formato heredado válido (`wallet_seed_phrase.txt`
  cuando corresponda / wallet `brisvia` cargada en Core);
- migración 1.0.8→1.0.9 + prueba con nodo caído;
- NO borrar el heredado hasta confirmar que el reemplazo cifrado se escribió atómicamente, abre y es
  byte-consistente donde corresponda.

### C. "Respaldo verificado" hardcodeado (P0, riesgo de fondos)
`lib.rs:1004` fuerza `"backed_up": true` en `wallet_summary`; `app.js:458` muestra el badge. Un cartel falso de
respaldo verificado puede causar pérdida real. Fix: hasta tener evidencia PERSISTIDA de recuperación/verificación
real → mostrar "Respaldo no verificado" o NO mostrar el badge. Nunca asumir true.

### D. "Preparando…" infinito
`app.js:321-331` — si el worker se cuelga (dataset RandomX lento, o fallo blando que no emite `fatal`
`lib.rs:2572`), el hero queda en "Preparando…" para siempre con `mining=true` leído como activo. Fix: timeout
razonable + cancelación + explicación del motivo + botón Reintentar + limpieza del worker colgado + estado final
no ambiguo.

### E. QR decorativo (aplicado: quitado)
`app.js:623` `fakeQR` (módulos random) en `#qr` (`index.html:336`) parece escaneable pero no codifica nada. Fix:
QR real que codifique EXACTAMENTE la dirección, o quitarlo. Recomendación de ChatGPT: quitarlo ahora, QR real
después (salvo que la librería ya esté integrada). → **APLICADO: se oculta/quita el QR falso.**

## Especificación refinada de A/B/C/D (ChatGPT 2ª ronda, ALTA) — implementar EXACTO
### A — cifrado desconocido
Tri-valor `encrypted`/`unencrypted`/`unknown`. Ante `unknown`: no ocultar el campo de contraseña, NO permitir
ENVIAR, no asumir sin cifrar, mostrar "el nodo todavía no permitió verificarla", reintento automático con
backoff + botón Reintentar. Bloquear SOLO las operaciones que requieren conocer el cifrado (enviar/gastar);
**Recibir, ver dirección y funciones locales NO se inutilizan**.

### B — billetera heredada: SEPARAR "existe" de "utilizable"
1. **Existe billetera local** (impide crear otra / onboarding de creación) si aparece CUALQUIERA en la ubicación
   canónica: `wallet_seed.enc` | `wallet_seed_phrase.txt` | metadatos históricos reconocidos. La sola presencia
   basta. (Core cargada = corroboración, NUNCA requisito → no reintroducir dependencia del nodo.)
2. **Heredada utilizable**: `wallet_seed_phrase.txt` debe: ser archivo regular, tamaño razonable, leerse/
   normalizarse, 12/24 palabras, **checksum BIP39 OK**, derivar por el camino canónico Brisvia, preferentemente
   coincidir con dirección/huella local conocida si existe.
   - válido → `legacy_wallet_valid`: permitir migración.
   - presente pero inválido → `legacy_wallet_corrupt`: NO onboarding ni creación nueva; mostrar recuperación/
     reparación. (CRÍTICO: corrupto ≠ "no existe billetera".)
   - ausente y sin ningún otro artefacto → recién ahí creación/restauración.
3. **Tras migrar**: escribir `wallet_seed.enc` atómico → abrir y verificar → comprobar misma huella/dirección →
   recién ahí retirar el texto plano → no afirmar borrado forense seguro en SSD.

### C — respaldo verificado
Quitar el `true` fijo. Flag pasa a true SOLO si el usuario re-introduce las palabras correctamente o completa un
ensayo de recuperación que deriva la MISMA huella/dirección. Persistir: estado verificado + huella asociada +
versión del método + fecha. Si cambia la billetera o la huella no coincide → flag vuelve a false. "Ya las
guardé" NO alcanza para mostrar "Respaldo verificado".

### D — timeout de "Preparando…" POR FASE, con progreso real (no solo tiempo)
- 30s: aviso suave "la preparación está tardando más de lo habitual".
- 120s sin progreso ni heartbeat: declarar bloqueo, cancelar, ofrecer Reintentar.
- hasta 180s SOLO si el worker informa progreso válido (heartbeat) durante la creación del dataset.
- Fases: nodo/plantilla/conexión ~20–30s; init RandomX 120–180s con heartbeat; sin progreso → cancelar.
- Al cancelar/expirar: detener+limpiar el worker, descartar trabajos parciales, `mining=false`, mostrar fase+
  motivo, reintento limpio, impedir que queden DOS workers tras el retry.
- SI_HAY_ALGO_CRITICO: el timeout debe depender del PROGRESO REAL, no solo del tiempo transcurrido.

## P1 no bloqueante (traducir explicación principal, detalle técnico desplegable; sin refactor grande)
- `import_descriptors` (`lib.rs:1386`) y fallo de spawn del minero (`lib.rs:2492`) devuelven inglés crudo →
  ERR:* code + i18n de la explicación principal.

## POST-LAUNCH
- "BRVA minado" hardcodea 50/bloque (`app.js:336`) → el backend reporta el reward actual (post-halving).
- Pools personalizadas, saldos estimados, gráficos históricos, proyecciones, cambios de arquitectura, funciones
  nuevas de billetera: NO en 1.0.9.

## Mockups (sección 4): NO un proyecto aparte
Generar 2 capturas de la implementación real de la pestaña Minería: (1) vista simple por defecto; (2) misma vista
con detalles técnicos desplegados. Alcanza para revisar jerarquía/textos/claridad antes de las builds.

## Últimos controles antes del freeze (checklist ChatGPT)
- ninguna ruta de onboarding depende del RPC/nodo (✓ 2.A);
- ningún estado dice "minando" sin trabajo activo (✓ máquina de estados: working exige job);
- no aparecen importes de pool que no vengan del ledger (✓ términos + aviso honesto);
- el cambio SOLO↔POOL no deja dos procesos, jobs viejos ni threads activos (verificar en el E2E real);
- reiniciar conserva el modo elegido sin iniciarlo antes de conocer el estado real;
- update 1.0.8→1.0.9 conserva `wallet_seed.enc` byte-idéntico;
- interfaz principal y errores críticos simétricos ES/EN (✓ i18n 458/458; sumar ERR nuevos);
- badge de respaldo con fuente de verdad persistente (ajuste C);
- nota de testnet oculta fail-safe en mainnet (✓ P0 ya aplicado).
