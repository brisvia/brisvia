#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Validación AUTOMÁTICA del pago de PRUEBA tras la firma de Fernando. Corre en el SERVIDOR (nodo testnet +
ledger de la pool). NO firma (la tx ya viene firmada) ni accede a la semilla. Usa la verificación PROPIA de la
pool (tx_verifier), y solo si pasa transmite y confirma. Si se corre sin HEX (dry-run), solo construye y muestra
el estado del lote.

Uso:  python3 validar-pago-firmado.py [<HEX> <TXID>]
"""
import sys, json, subprocess, time
sys.path.insert(0, "/root/brisvia-pool")
from pplns_ledger import Ledger
from payouts import PayoutManager
from chain_source import BitcoinCliChain

BATCH = "testnet-payout-1"
CLIP = "/root/bitcoin/build-main/bin/bitcoin-cli"
CLI = [CLIP, "-chain=brisvia-test", "-datadir=/root/brisvia-testnet-data", "-rpcport=19332"]

def cli(*a):
    return subprocess.check_output(CLI + list(a), text=True).strip()

def make_pm():
    ledger = Ledger("/root/brisvia-pool/testnet_run_ledger.db")
    chain = BitcoinCliChain(chain="brisvia-test", datadir="/root/brisvia-testnet-data",
                            rpcport=19332, cli_path=CLIP)
    pm = PayoutManager(ledger, artifact_dir="/root/brisvia-pool/payouts_testnet",
                       min_payout_sats=100000000, coinbase_maturity=100, security_depth=20,
                       chain_name="brisvia-test",
                       pool_spk_hex="00147ae93f918356a8cc96b099d32c46bc182d0aecb9",
                       hrp="tbrv", chain=chain, maturity_confirmations=200,
                       payout_confirm_depth=10, require_two_nodes=False, max_fee_sats=1000000)
    return ledger, pm

def main():
    ledger, pm = make_pm()
    b = ledger.get_batch(BATCH)
    print("estado del lote:", b["status"] if b else "(no existe)")
    print("entradas esperadas del lote:", list(ledger.get_batch_inputs(BATCH)))

    if len(sys.argv) < 3:
        print("\n(dry-run: sin HEX. Construcción OK. Pasá <HEX> <TXID> cuando Fernando firme.)")
        return

    HEX, TXID = sys.argv[1], sys.argv[2]
    dec = json.loads(cli("decoderawtransaction", HEX))
    if dec["txid"] != TXID:
        print("ABORTA: el txid no coincide con el hex."); sys.exit(1)

    inputs, in_total = [], 0
    for vin in dec["vin"]:
        raw = cli("gettxout", vin["txid"], str(vin["vout"])) or ""
        o = json.loads(raw) if raw else None
        val = int(round(o["value"] * 1e8)) if o else 5000000000  # 50 BRVA (conocido) si ya no está en el UTXO set
        inputs.append({"txid": vin["txid"], "vout": vin["vout"], "value_sats": val})
        in_total += val
    outputs, out_total = [], 0
    for vo in dec["vout"]:
        v = int(round(vo["value"] * 1e8))
        outputs.append({"spk_hex": vo["scriptPubKey"]["hex"], "value_sats": v})
        out_total += v
    fee = in_total - out_total
    print(f"decodificada: {len(inputs)} entradas ({in_total} sats), {len(outputs)} salidas ({out_total} sats), comisión {fee} sats")

    # VERIFICAR contra el lote (tx_verifier de la pool). NO transmite: solo registra si pasa.
    # La política determinista (secuencia final, locktime 0, versión 2) la deriva verify_and_attach_signed
    # DIRECTAMENTE del hex firmado (revisión E2E 2026-07-19); no se pasan metadatos externos.
    res = pm.verify_and_attach_signed(BATCH, HEX, TXID, inputs, outputs, fee, complete=True)
    if not res.get("ok"):
        print("VERIFICACIÓN FALLÓ (no transmito nada):", res); sys.exit(1)
    print("verificación OK:", res)

    # CONTROL PREVIO A TRANSMITIR: testmempoolaccept debe decir allowed=True EXPLÍCITAMENTE (ChatGPT).
    tma = json.loads(cli("testmempoolaccept", json.dumps([HEX])))[0]
    if not tma.get("allowed"):
        print("ABORTA: testmempoolaccept NO allowed ->", tma.get("reject-reason", "?"), "(no transmito)")
        sys.exit(1)
    print("testmempoolaccept: allowed=True (ok para transmitir)")

    # TRANSMITIR los bytes firmados (idempotente por txid)
    bc = pm.broadcast_batch(BATCH)
    print("broadcast:", bc)
    if not bc.get("ok"):
        print("no se pudo transmitir."); sys.exit(1)

    # CONFIRMAR (poll hasta profundidad)
    for i in range(30):
        cf = pm.confirm_batch(BATCH)
        print(f"  confirm[{i}]: {cf}")
        if cf.get("confirmed"):
            print("=== PAGO CONFIRMADO. Lote reserved->paid. ==="); break
        time.sleep(30)

if __name__ == "__main__":
    main()
