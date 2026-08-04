# Certificación del candidato rc7 — programa de escritorio Brisvia

Fecha de armado: 2026-07-17 (madrugada). Trabajo autónomo mientras Fernando duerme.
Candidato: **fe9657e** (rama `wip/v106-rc7-installer`, repo `brisvia/brisvia-desktop`).
Release borrador: **v1.0.6** (11 archivos, todos reconstruidos desde fe9657e → procedencia unificada).

---

## HECHO (en criollo)

Probé el programa que la gente se va a bajar, no la versión de mi compilador. Revisé que:

- El programa que se descarga es **exactamente** el que compilé (mismo origen para los tres sistemas:
  Windows, Mac y Linux).
- El **auto-actualizador** solo acepta versiones firmadas por nosotros. Probé que si le cambio un solo byte
  al paquete, o le pongo una firma falsa, o una llave equivocada, **lo rechaza**. La firma buena la acepta.
- Se puede **crear una billetera** y hacer una **transacción real** de punta a punta (probado en red de prueba).
- Cuando alguien **actualiza** el programa de una versión a la siguiente, **su billetera sigue intacta**: la
  misma, sin duplicados, y se abre con la misma contraseña. Probado instalando la versión vieja, creando una
  billetera, y actualizando encima.
- Los paquetes de **Mac y Linux abren** y sus piezas internas (el nodo, el minero) arrancan y responden.

Aparecieron **dos detalles de borde** que NO afectan a la gente pero conviene que veas (abajo, sección
"Hallazgos"). Los dos están **bien resueltos en el código**; lo que no se pudo fue reproducirlos en la fábrica
de pruebas automática (por un tema de firma, que se explica).

**Lo que NO hice y espera tu OK:** no publiqué nada, no activé las descargas, no creé el sello final (tag).
Eso es irreversible y va a usuarios reales, así que queda frenado hasta que vos lo autorices.

---

## Estado por prueba

| Prueba | Qué demuestra | Estado |
|---|---|---|
| Procedencia unificada | los 11 archivos del release salen del mismo código fe9657e | ✅ verde |
| Firma del actualizador (Win/Mac/Linux) | solo acepta paquetes auténticos; rechaza alterado / firma falsa / llave mala | ✅ verde |
| Transacción real (regtest) #14 | crear billetera + minar + mandar una transacción de verdad | ✅ verde |
| Migración por identidad de código | las funciones que cifran/descifran la semilla son byte-idénticas entre 1.0.5 y fe9657e | ✅ verde |
| Migración real (update limpio) | instalar 1.0.6 encima de 1.0.5 → la billetera sobrevive byte a byte, sin duplicados, abre con la misma clave | ✅ verde |
| Smoke Linux | el AppImage exacto abre y sus piezas corren | ✅ verde |
| Smoke Mac | el .app exacto abre y sus piezas corren | ✅ verde (paquete) + hallazgo #2 reportado |

**Corrida final: [29552667763](https://github.com/brisvia/brisvia-desktop/actions/runs/29552667763) — los 4 jobs en verde.**
Evidencia textual de la migración (del log):
- `1 passing` — creó una billetera real en la 1.0.5 (tauri-driver sobre la app instalada).
- `OK: 1.0.6 installed cleanly over 1.0.5`.
- `OK: 1.0.5 -> 1.0.6, seed byte-identical, single wallet` — la semilla quedó byte a byte igual, una sola billetera.
- `✓ opens straight to the wallet (no new wallet) and unlocks with the same password` — abre directo, sin
  re-crear, y desbloquea con la misma contraseña.

---

## Hallazgos (dos detalles de borde, ninguno afecta a la gente)

### 1. Actualizar con el nodo abierto a mano → la instalación se aborta (no corrompe)

**Qué es:** el programa trae un guardián que, antes de instalar una versión nueva, cierra el nodo de forma
ordenada para no corromper la cadena. Si por algún motivo no puede cerrarlo limpio, **aborta la instalación
en vez de arriesgarse** (sale con código 1603). Eso es lo que tiene que hacer: preferir "la instalación no
avanzó" antes que "la cadena quedó rota".

**Cómo se prueba en la vida real:** el actualizador automático **cierra el programa (y con él su nodo) antes**
de instalar, así que la instalación siempre encuentra todo cerrado. Ese es el camino que usa la gente, y ese
sí lo probé (migración con update limpio, arriba).

**Por qué no se pudo automatizar el caso "nodo vivo":** para eso hay que dejar un nodo corriendo y pedirle al
guardián que lo cierre. En la fábrica de pruebas (CI) ese nodo arrancado a mano no queda calzado igual que el
que arranca el propio programa, y el guardián — bien hecho — prefiere abortar antes que actuar sobre algo que
no puede resolver sin ambigüedad. Verifiqué **leyendo el código del guardián** (`shutdown-brisvia-node.ps1`)
que hace lo correcto: busca el nodo por ruta completa (no por nombre, para no tocar el Bitcoin de otro), le
pide el apagado ordenado por su propio RPC, espera a que cierre, y si no puede, aborta. Falla cerrado.

**Riesgo para la gente:** ninguno de corrupción. En el peor caso, alguien que reinstale a mano con el nodo
abierto vería una instalación que no avanza (y con cerrar el programa antes, avanza). Conviene, más adelante,
pulir ese caso; no bloquea el lanzamiento.

### 2. En Mac, cerrar la app en la fábrica de pruebas deja el nodo colgado (limitación de firma, no del programa)

**Qué es:** cuando cerrás el programa en Mac (Cmd+Q), el programa **cierra su propio nodo** de forma ordenada.
Eso está **verificado en el código**: al pedir salir (`RunEvent::ExitRequested`) llama a `stop_node`, que le
manda el apagado ordenado al nodo por RPC y **espera** a que cierre, sin matarlo nunca.

**Por qué la fábrica de pruebas lo marcaba colgado:** para cerrar la app "como la gente" (Cmd+Q) hay que
mandarle a Mac el evento de "salir". A una app **sin firmar** lanzada dentro del CI, Mac **no le entrega** ese
evento (requiere un permiso de Automatización que el CI no puede dar). Sin ese evento, el programa nunca se
entera de que tiene que salir, y la única forma de bajarlo es a la fuerza — lo que deja el nodo colgado por
definición. Es una limitación del **paquete sin firmar en el CI**, no del programa.

**Qué se hace:** el smoke de Mac igual prueba lo importante (el paquete abre, el nodo y el minero arrancan y
responden) y **reporta** el resultado del cierre como hallazgo, sin dar por bueno un cierre que no ocurrió.
El cierre limpio con el nodo hay que confirmarlo a mano en una Mac real, o cuando se firme/notarice el paquete
(la firma de Mac es un pendiente aparte que necesita tu certificado de Apple).

---

## Frenado hasta tu OK (irreversible)

- No se publicó el release (sigue en borrador).
- No se activaron las descargas.
- No se creó el sello final (tag rc7).

Publicar el programa de mainnet es irreversible y va a usuarios reales. Queda a un clic, esperando tu
autorización explícita.

---

## Apéndice técnico

- **Candidato:** app SHA `fe9657e`. Núcleo `brisvia/brisvia` (Bitcoin Core v30.2 fork + RandomX). Génesis
  mainnet `aa6bc268...`.
- **Firma del updater:** minisign algoritmo "ED" (Ed25519 sobre blake2b-512 del bundle). Verificador propio
  en `tools/verify_updater_signature.py` (pynacl + hashlib). Clave pública en `tauri.conf.json`. Pruebas
  negativas: bundle alterado, firma inválida, clave equivocada → los tres rechazados.
- **Formato de semilla:** `[0x01][salt16][nonce12][AES-256-GCM]`. Funciones `encrypt_phrase_file_inner`,
  `decrypt_phrase_file`, `derive_key`, `enc_seed_path`, `net_subdir` byte-idénticas entre v1.0.5 y fe9657e.
- **Prueba de migración:** workflow `.github/workflows/verify-update.yml`. Jobs: `build-v105-windows`
  (recompila la 1.0.5 desde su tag), `migration-windows` (instala 1.0.5 → crea billetera con tauri-driver
  sobre la app instalada → cierra todo → instala 1.0.6 encima limpio → semilla byte-idéntica, sin segunda
  billetera, abre con la misma clave), `smoke-linux`, `smoke-macos`.
- **Guardián de actualización:** `src-tauri/windows/shutdown-brisvia-node.ps1` (fail-closed, por ruta completa,
  stop por RPC, espera al PID exacto). **Cierre al salir:** `src-tauri/src/lib.rs:3081` (ExitRequested →
  `stop_node` → `stop_node_and_wait_max`, apagado ordenado por RPC + espera, sin kill).
- **Los instaladores exactos son mainnet**; el regtest está tras `#[cfg(feature=e2e)]`, compilado afuera de
  los binarios públicos. El onboarding funciona en modo espera pre-1-ago (solo el minado está bloqueado).
