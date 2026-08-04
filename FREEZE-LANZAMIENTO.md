# CONGELACIÓN DEFINITIVA — Brisvia mainnet 1-ago-2026 15:00 UTC

Paquete canónico e inmutable. Congelado 2026-07-18. **NO modificar ninguno de estos valores/archivos.** La guarda `verificar-freeze.sh` detecta cualquier cambio posterior.

## 1. Núcleo y binarios
| Item | Valor |
|---|---|
| Commit del núcleo (pineado en los 3 builds) | `69c48b2dfcf20fba70f6b5aef6958786e61ab7ed` |
| Versión del escritorio | `1.0.8` |
| **bitcoind desplegado (SHA-256)** | `f435d8ff797bbe5a07e2b17513a0d0508702251f7a134908f929b1856c8b191b` |
| Genesis mainnet | `aa6bc268339aa9f4f2e39ae33aca7b7e48e395033d08d37c08f828890af7baf7` |
| Puerto P2P | `9342` |
| Puerto RPC (solo localhost) | `9338` |
| coin_type (BIP44) | `9339'` |

## 2. Hashes de los instaladores (release v1.0.8, deben coincidir con la web)
| Archivo | SHA-256 |
|---|---|
| Brisvia-Miner-Windows.exe | `04456725a24cd7ed860d53c2d13f8ccc8108bd9b1b8435baab23d336229ab37a` |
| Brisvia-Miner-macOS.dmg | `9800137cf1ade3ed68e8a7558775615b03b45cfc73a67fa9df8a158942f30983` |
| Brisvia-Miner-Linux.AppImage | `bbc81069bea4c009bfad9a1272f1e5cb23ada419fecf327af0926bdd424b1eec` |
| Brisvia.Miner_1.0.8_amd64.deb | `2d13ac45ffce506915d8c94f5f3b99cf8199b2bb49e4a3c9a0aa36253c82f9a2` |

## 3. Configuración efectiva de los 3 nodos
| Nodo | IP | Usuario | datadir | Servicio / Timer | Binario |
|---|---|---|---|---|---|
| Hostinger | 187.77.240.145 | root | /root/brisvia-mainnet-data | brisvia-mainnet.service / .timer | /root/bitcoin/build-main/bin/bitcoind |
| Oracle-1 | 129.80.250.36 | ubuntu | /home/ubuntu/brisvia-mainnet-data | brisvia-mainnet.service / .timer | /home/ubuntu/bitcoin/bin/bitcoind |
| Oracle-2 | 129.159.108.102 | ubuntu | /home/ubuntu/brisvia-mainnet-data | brisvia-mainnet.service / .timer | /home/ubuntu/bitcoin/bin/bitcoind |
- **Timer de encendido**: `OnCalendar=2026-08-01 15:00:00 UTC` en los 3 (enabled). `Restart=always`, `RestartSec=15`.
- **Guarda ExecStartPre**: `check-node-sha.sh <binario>` en los 3 (aborta si el SHA no es f435d8ff).
- **Firewall**: 9342 ACCEPT (abierto) en los 3; 9338 solo localhost (no expuesto).
- **NTP**: sincronizado en los 3. **Swap**: 4GB Hostinger, 2GB cada Oracle.
- **Monitor**: cron cada 5 min en los 3, config `rpcport=9338 p2p_port=9342 service_name=brisvia-mainnet.service`, alertas a Discord #monitor-alertas.

## 4. Archivos operativos congelados (SHA-256 — la guarda los revisa)
| Archivo | SHA-256 |
|---|---|
| preflight-mainnet.sh | `e6eda1c70eec64f74e257d07d40821857d2c53babbb2b9b5102e8526eea64f00` |
| check-node-sha.sh | `e4e785ef21e4fe40af16d1c887eb1ef700b91b82c559a9be55b09779f7d37714` |
| panel-lanzamiento.sh | `c4d4f2a8acb21d33ec1e048626cc03a80e2f1a625e81d0dbdf83a58039c1db72` |
| CHECKLIST-DIA-LANZAMIENTO.md | `5c45ea6085b6af5c936060648dd33464f41b068ab3af5177401d0e99cd5d4128` |
| PROCEDIMIENTO-apagado-testnet.md | `6a0383edb5ca94c328b5439ab43157eff541da47deb4287bddea6ed94ad9e34b` |
| PLAN-emergencia-dia1.md | `330ffb8964d93ff4a0485f5dee61b3ff4a49f41fbe6e37c5c943894a2d27ba1f` |
| GUIA-firma-pago-pool.md | `794790f35317fb583476b7170a9a9094557c182fbef18c992566bedfb59bd7d6` |
| adversarial-evidence.md | `61b1ba6b859b227a688fa3107e766e7e6db493298e7707ae3c75d6bd819226ff` |
| BRISVIA-MAINNET-SPEC.md | `a7d3daff71eab91b186fd39aebd1233f41c13457332b3fc62fe5fdb84f99b6d6` |

## 5. Verificación
- **Diaria / antes de tocar**: `./verificar-freeze.sh` → recomputa las huellas locales + chequea el bitcoind desplegado en los 3 + timers. Cualquier DRIFT (huella distinta) = algo cambió: investigar antes de seguir.
- **Profunda (T−24h/T−60min)**: `preflight-mainnet.sh` en cada nodo.

Fechas clave: testnet se apaga 29-jul 03:00 UTC · firma del pago de prueba antes del 29 · mainnet arranca 1-ago 15:00 UTC.
