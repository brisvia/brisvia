# Pool — despliegue del arreglo de suspensión + rollback (POST-lanzamiento)

**NO desplegar todavía.** La pool va APAGADA públicamente el 1-ago. Este arreglo se despliega recién cuando: (1) mainnet muestre estabilidad real, y (2) se decida habilitar la pool oficial. Hasta entonces, la pool de testnet sigue con el código viejo funcional.

## 🔒 CONDICIÓN BLOQUEANTE para habilitar la pool pública (ChatGPT, 2026-07-19)
**NO habilitar la pool pública ni aceptar fondos reales hasta completar AL MENOS UN pago firmado de punta a punta** (firma offline → verificación → testmempoolaccept allowed=true → transmisión → confirmación → reconcile). Puede hacerse DESPUÉS del lanzamiento, pero ANTES de abrir la pool a usuarios. Preferentemente en testnet; si testnet ya no está, con un lote controlado de importe MÍNIMO. El primer ensayo del firmador offline NO debe ser un pago grande acumulado de mineros reales. Esta condición NO bloquea el lanzamiento de mainnet — solo la apertura futura de la pool pública. Flujo congelado y listo en `firma-pago/` (PSBT con UTXO, montos exactos, comisión determinista, RBF off, testmempoolaccept, broadcast/confirm idempotentes).

## Qué arregla
El bug de causa raíz de la suspensión: `refresh_suspended` se había insertado en el medio del `__init__` de `PoolState`; su `return` dejaba el bloque de la etapa 4 (incluido `self.payouts`) como código muerto → el servicio crasheaba en `process_payouts` con `AttributeError: 'PoolState' has no attribute 'payouts'`. El arreglo mueve ese bloque de vuelta adentro del `__init__` + cierra el ledger si una init posterior falla + reemplaza el test que salteaba `__init__` por una prueba de arranque real + smoke de `process_payouts`.

## Estado actual (2026-07-18)
- **Producción** `/root/brisvia-pool/` (VPS Hostinger): código VIEJO funcional (stratum_server.py 36849 bytes). Corre la pool de testnet.
- **Staging** `/root/brisvia-pool-staging/`: código ARREGLADO (stratum_server.py 41075 bytes), validado: 26 tests + E2E con minero conectado 11/11 (suspender→pool_suspended, submit-en-mantenimiento→pool_suspended nunca acreditado, login-nuevo sin trabajo, reanudar→trabajo, nunca cae a solo).
- **Backup** `/root/brisvia-pool-code-bak-2026-07-18/`: código previo (por si hace falta volver).
- Commit local canónico: `540fde6` (repo pool local, sin push).

## Despliegue (cuando corresponda, post-lanzamiento)
1. **Backup del actual**: `cp -a /root/brisvia-pool /root/brisvia-pool-bak-$(date -u +%Y%m%d)`.
2. **Copiar los 2 archivos arreglados** desde staging (mismo entorno ya validado):
   `cp /root/brisvia-pool-staging/stratum_server.py /root/brisvia-pool/stratum_server.py`
   `cp /root/brisvia-pool-staging/test_suspend.py /root/brisvia-pool/test_suspend.py`
3. **Tests antes de reiniciar**: `cd /root/brisvia-pool && /usr/bin/python3 -m pytest test_suspend.py -q` → 4 pass (incluye smoke de process_payouts + constructor real).
4. **Reiniciar el servicio**: `systemctl restart brisvia-pool.service`; `sleep 8`.
5. **Verificar arranque** (donde antes crasheaba): `journalctl -u brisvia-pool.service -n 20 --no-pager | grep -iE "AttributeError|escuchando|dirección de cobro"` → debe verse "escuchando" + cobro listo, SIN AttributeError.
6. **E2E de suspensión** (opcional, con el cliente de prueba): tocar el flag de suspensión (`--suspend-flag`), verificar que un minero conectado recibe `pool_suspended` y no cae a solo; quitar el flag → reanuda.

## Rollback (si algo falla)
1. `cp /root/brisvia-pool-code-bak-2026-07-18/stratum_server.py /root/brisvia-pool/stratum_server.py` (y `test_suspend.py` si aplica), o restaurar el backup del paso 1 del despliegue.
2. `systemctl restart brisvia-pool.service`; verificar `journalctl` → "escuchando" sin errores.
3. La pool vuelve al código viejo funcional. Sin pérdida de ledger (el `.db` y los backups no se tocan en este flujo).

## Reglas
- No desplegar a la pool viva mientras sea testnet en uso (necesaria hasta el 29 para el pago).
- No habilitar la pool públicamente sin decisión de Fernando + estabilidad mainnet.
- La custodia sigue igual: el server NUNCA firma; los pagos se firman offline (ver `GUIA-firma-pago-pool.md`).
