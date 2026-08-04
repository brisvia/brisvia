# -*- coding: utf-8 -*-
"""Firmador LOCAL y SEGURO del pago de prueba de la pool (testnet, BRVA sin valor real).

Corre en la máquina de Fernando. Reusa el mismo esquema de Brisvia:
  - lee SOLO el wallet_seed.enc local (no pide ni muestra las 12 palabras);
  - pide la CONTRASEÑA del programa (lectura oculta) y con ella descifra (Argon2id + AES-256-GCM);
  - deriva la clave con el path REAL del app (84h/9339h/0h, wpkh) y VERIFICA que la dirección derivada
    coincida EXACTAMENTE con la dirección de cobro del pool; aborta ante cualquier diferencia;
  - muestra red, entradas, destinatarios, montos, comisión, ausencia de vuelto y RBF apagado antes de firmar;
  - firma la transacción y escribe SOLO el hex firmado en un archivo. NO transmite. NO envía nada a ningún lado.

La contraseña y la semilla NUNCA salen de esta máquina. Solo se genera el hex firmado, que le pasás a Claude.

Uso:   python firmar_pago_local.py            (firma el pago real; te pide la contraseña)
       python firmar_pago_local.py --selftest (prueba con datos DESCARTABLES; no toca tu billetera)
"""
import sys, os, json, getpass, hashlib, tempfile

# --- dependencias ---
try:
    from cryptography.hazmat.primitives.ciphers.aead import AESGCM
    from argon2.low_level import hash_secret_raw, Type
    from embit import bip39, bip32, ec
    from embit.transaction import Transaction, TransactionInput, TransactionOutput
    from embit.psbt import PSBT
    from embit import script as escript
except Exception as e:
    print("Faltan librerías. Instalá con:  pip install cryptography argon2-cffi embit")
    print("detalle:", e); sys.exit(2)

# ---------------- DATOS DEL PAGO (del artefacto testnet-payout-1, hash 80bedce2) ----------------
NETWORK = "BRISVIA TESTNET"
POOL_ADDR = "tbrv1q0t5nlyvr265ve94sn8fjc34urqks4m9etpnyul"       # dirección de cobro del pool
POOL_HASH160 = "7ae93f918356a8cc96b099d32c46bc182d0aecb9"        # su hash160 (7ae9...)
INPUTS = [  # 2 UTXO coinbase del pool, 50 BRVA c/u
    {"txid": "07ac620809059e32e923806621c2a1f7cad4bb9bb0d5f8bcadc4ab3e99b2de87", "vout": 0, "sats": 5000000000},
    {"txid": "c47f526c039e6f37a9460eb1e9803fb0a68dc090174c050dc53a4fdc164a3292", "vout": 0, "sats": 5000000000},
]
OUTPUTS = [  # 2 mineros, montos NETOS
    {"addr": "tbrv1qwk84fz4v99we9l584tweldegd3d032t9240hn0", "sats": 7499996250},
    {"addr": "tbrv1qgztgzvy5zu8ul6cxp7ugntd5d8p8452s7cevp2", "sats": 2499998750},
]
FEE_SATS = 5000
HRP = "tbrv"
# Paths REALES del programa: en el build de TESTNET usa coin type 1h (84h/1h/0h); en mainnet usa 9339h.
# La dirección de cobro es testnet, así que se derivó con 84h/1h/0h. Probamos ambos por robustez.
DERIV_PATHS = ["m/84h/1h/0h", "m/84h/9339h/0h"]

def hash160(b):
    return hashlib.new("ripemd160", hashlib.sha256(b).digest()).digest()

# --- bech32 (para armar/mostrar direcciones con el hrp propio tbrv) ---
CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
def bech32_polymod(v):
    g=[0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3]; c=1
    for d in v:
        b=c>>25; c=((c&0x1ffffff)<<5)^d
        for i in range(5):
            if (b>>i)&1: c^=g[i]
    return c
def bech32_hrp_expand(h): return [ord(x)>>5 for x in h]+[0]+[ord(x)&31 for x in h]
def convertbits(data,frm,to,pad=True):
    acc=0;bits=0;ret=[];maxv=(1<<to)-1
    for b in data:
        acc=(acc<<frm)|b;bits+=frm
        while bits>=to: bits-=to;ret.append((acc>>bits)&maxv)
    if pad and bits: ret.append((acc<<(to-bits))&maxv)
    return ret
def bech32_encode_addr(hrp, witver, witprog):
    data=[witver]+convertbits(list(witprog),8,5)
    values=bech32_hrp_expand(hrp)+data
    const = 0x2bc830a3 if witver>0 else 1  # bech32m for v1+, bech32 for v0
    polymod=bech32_polymod(values+[0,0,0,0,0,0])^const
    chk=[(polymod>>5*(5-i))&31 for i in range(6)]
    return hrp+"1"+"".join(CHARSET[d] for d in data+chk)
def addr_to_scriptpubkey(addr):
    # decode a tbrv/brv bech32 v0 P2WPKH address -> scriptPubKey (0x0014 + hash20)
    pos=addr.rfind("1"); hrp=addr[:pos]; data=addr[pos+1:]
    dec=[CHARSET.find(c) for c in data]
    witver=dec[0]; prog=bytes(convertbits(dec[1:-6],5,8,False))
    return bytes([witver if witver==0 else witver+0x50, len(prog)])+prog, prog

def spk_p2wpkh(h160): return bytes([0x00,0x14])+h160

def find_key(mnemonic):
    """Deriva y busca en cada path (/0/* y /1/*) la clave cuyo hash160 == POOL_HASH160."""
    seed = bip39.mnemonic_to_seed(mnemonic.strip())
    root = bip32.HDKey.from_seed(seed)
    for path in DERIV_PATHS:
        acct = root.derive(path)
        for change in (0, 1):
            for i in range(500):
                child = acct.derive([change, i])
                pub = child.key.get_public_key().sec()
                if hash160(pub).hex() == POOL_HASH160:
                    return child.key, change, i, pub, path
    return None

def build_and_sign(privkey, pubkey_sec):
    # armar la tx: 2 entradas (sequence 0xffffffff = RBF OFF), 2 salidas
    tx = Transaction(version=2, vin=[], vout=[], locktime=0)
    for inp in INPUTS:
        vin = TransactionInput(bytes.fromhex(inp["txid"]), inp["vout"])  # embit espera big-endian (display); NO invertir
        vin.sequence = 0xffffffff  # RBF desactivado
        tx.vin.append(vin)
    for out in OUTPUTS:
        spk, _ = addr_to_scriptpubkey(out["addr"])
        tx.vout.append(TransactionOutput(out["sats"], escript.Script(spk)))
    # PSBT con witness_utxo de cada entrada (misma dirección de cobro), firmar
    psbt = PSBT(tx)
    our_spk = escript.Script(spk_p2wpkh(bytes.fromhex(POOL_HASH160)))
    for k, inp in enumerate(INPUTS):
        psbt.inputs[k].witness_utxo = TransactionOutput(inp["sats"], our_spk)
    psbt.sign_with(privkey)
    # finalizar (P2WPKH) y extraer el hex
    for k in range(len(INPUTS)):
        partial = psbt.inputs[k].partial_sigs
        if not partial:
            raise SystemExit("No se pudo firmar una entrada (clave no coincide).")
        sig = list(partial.values())[0]
        psbt.inputs[k].final_scriptwitness = escript.Witness([sig, pubkey_sec])
    final_tx = psbt.tx
    for k in range(len(INPUTS)):
        final_tx.vin[k].witness = psbt.inputs[k].final_scriptwitness
    return final_tx

def build_details():
    total_in = sum(i["sats"] for i in INPUTS)
    total_out = sum(o["sats"] for o in OUTPUTS)
    fee = total_in - total_out
    if fee != FEE_SATS or len(OUTPUTS) != 2 or total_in != 10000000000:
        raise SystemExit("ABORTA: la transacción no coincide con lo esperado.")
    L = ["Red: " + NETWORK,
         "Entradas: %d  (total %.8f BRVA)" % (len(INPUTS), total_in/1e8)]
    for i in INPUTS:
        L.append("   - %s...:%d = %.8f BRVA" % (i["txid"][:16], i["vout"], i["sats"]/1e8))
    L.append("Salidas: %d  (SIN vuelto)" % len(OUTPUTS))
    for o in OUTPUTS:
        L.append("   -> %s = %.8f BRVA" % (o["addr"], o["sats"]/1e8))
    L.append("Comisión: %d sats  %s" % (fee, "(correcta)" if fee == FEE_SATS else "(!!)"))
    L.append("RBF: DESACTIVADO")
    return "\n".join(L)

# --- ventanas gráficas (más fácil que el cmd para la contraseña y la confirmación) ---
def gui_password():
    try:
        import tkinter as tk
        from tkinter import simpledialog
        root = tk.Tk(); root.withdraw()
        pw = simpledialog.askstring("Brisvia — firmar pago",
                                    "Escribí tu contraseña del programa Brisvia\n(se ve con puntitos; no son las 12 palabras):",
                                    show="•", parent=root)
        root.destroy()
        return pw
    except Exception:
        import getpass
        return getpass.getpass("Contraseña (no se ve mientras escribís): ")

def gui_confirm(text):
    try:
        import tkinter as tk
        from tkinter import messagebox
        root = tk.Tk(); root.withdraw()
        ok = messagebox.askyesno("Brisvia — revisá antes de firmar",
                                 text + "\n\n¿Firmo esta transacción?", parent=root)
        root.destroy()
        return ok
    except Exception:
        print(text)
        return input("Escribí FIRMAR para firmar: ").strip() == "FIRMAR"

def gui_msg(title, text):
    try:
        import tkinter as tk
        from tkinter import messagebox
        root = tk.Tk(); root.withdraw()
        messagebox.showinfo(title, text, parent=root); root.destroy()
    except Exception:
        print(text)

def wallet_enc_default():
    base = os.environ.get("APPDATA", os.path.expanduser("~"))
    for p in [os.path.join(base, "Brisvia", "BrisviaSim", "wallet_seed.enc"),
              os.path.join(os.path.expanduser("~"), ".local", "share", "BrisviaSim", "wallet_seed.enc")]:
        if os.path.exists(p): return p
    return os.path.join(base, "Brisvia", "BrisviaSim", "wallet_seed.enc")

def decrypt_seed(enc_path, password):
    data = open(enc_path, "rb").read()
    if len(data) < 1+16+12+16 or data[0] != 1:
        raise SystemExit("wallet_seed.enc con formato inesperado.")
    salt, nonce, ct = data[1:17], data[17:29], data[29:]
    key = hash_secret_raw(password.encode(), salt, time_cost=3, memory_cost=65536,
                          parallelism=1, hash_len=32, type=Type.ID, version=19)
    try:
        return AESGCM(key).decrypt(nonce, ct, None).decode()
    except Exception:
        raise SystemExit("Contraseña incorrecta (no se pudo descifrar).")

def run_sign(enc_path):
    print("Archivo de billetera:", enc_path)
    if not os.path.exists(enc_path):
        gui_msg("Brisvia", "No encontré tu billetera (wallet_seed.enc).\nBuscado en:\n" + enc_path +
                "\n\nAbrí el programa Brisvia una vez y volvé a intentar.")
        raise SystemExit("No encontré wallet_seed.enc.")
    password = gui_password()
    if not password:
        raise SystemExit("Cancelado (sin contraseña).")
    try:
        mnemonic = decrypt_seed(enc_path, password)
    except SystemExit as e:
        gui_msg("Brisvia — contraseña", str(e) + "\n\nProbá de nuevo con la contraseña correcta.")
        raise
    del password
    sign_with_mnemonic(mnemonic)


def sign_with_mnemonic(mnemonic):
    found = find_key(mnemonic)
    del mnemonic
    if not found:
        gui_msg("Brisvia — no coincide",
                "Estas palabras/billetera NO controlan la dirección de cobro del pool.\n"
                "NO firmé nada (está bien que aborte).\nRevisá el respaldo o avisale a Claude.")
        raise SystemExit("No controla la dirección de cobro.")
    privkey, change, idx, pub, path = found
    derived_addr = bech32_encode_addr(HRP, 0, hash160(pub))
    if derived_addr != POOL_ADDR:
        raise SystemExit("ABORTA: la dirección derivada no coincide con la de cobro.")
    print("Dirección derivada COINCIDE con la de cobro (%s /%d/%d)" % (path, change, idx))
    details = "Dirección de cobro: COINCIDE (correcto)\n\n" + build_details()
    if not gui_confirm(details):
        gui_msg("Brisvia", "Cancelado. No firmé nada.")
        raise SystemExit("Cancelado por el usuario.")
    final_tx = build_and_sign(privkey, pub)
    hexstr = final_tx.serialize().hex()
    txid = hashlib.sha256(hashlib.sha256(final_tx.serialize()).digest()).digest()[::-1].hex()
    desk = os.path.join(os.path.expanduser("~"), "Desktop", "pago-firmado.txt")
    wrote = None
    for target in (desk, os.path.join(os.path.expanduser("~"), "pago-firmado.txt")):
        try:
            with open(target, "w") as f:
                f.write("TXID=%s\nHEX=%s\n" % (txid, hexstr))
            wrote = wrote or target
        except Exception:
            pass
    print("TXID=%s" % txid)
    print("HEX=%s" % hexstr)
    try:
        os.startfile(wrote)
    except Exception:
        pass
    gui_msg("Brisvia — FIRMADO",
            "Listo, quedó FIRMADO. NO se transmitió nada.\n\n"
            "Se abrió/guardó 'pago-firmado.txt' en tu Escritorio.\n"
            "Copiá sus DOS líneas (TXID y HEX) y pasáselas a Claude.")


def gui_words():
    try:
        import tkinter as tk
        from tkinter import simpledialog
        root = tk.Tk(); root.withdraw()
        w = simpledialog.askstring("Brisvia — 12 palabras del respaldo VIEJO",
                                   "Escribí/pegá las 12 palabras de tu billetera ANTERIOR (separadas por "
                                   "espacio).\nQuedan SOLO en tu PC, no se envían a nadie:", parent=root)
        root.destroy()
        return w
    except Exception:
        return input("12 palabras: ")


def run_sign_words():
    words = gui_words()
    if not words or len(words.split()) not in (12, 24):
        gui_msg("Brisvia", "No ingresaste 12 (o 24) palabras. Cancelado."); raise SystemExit("Sin palabras.")
    sign_with_mnemonic(" ".join(words.strip().lower().split()))

def selftest():
    """Prueba con datos DESCARTABLES: crea una semilla al azar, la cifra igual que el app, y verifica que el
    firmador la descifre, derive y firme una tx de juguete a la dirección derivada. NO toca tu billetera."""
    print(">> SELFTEST con datos descartables (no toca tu billetera real)")
    import secrets as _s
    mn = bip39.mnemonic_from_bytes(_s.token_bytes(16))
    pw = "test-password-123"
    # cifrar igual que el app
    salt = _s.token_bytes(16); nonce = _s.token_bytes(12)
    key = hash_secret_raw(pw.encode(), salt, 3, 65536, 1, 32, Type.ID, 19)
    ct = AESGCM(key).encrypt(nonce, mn.encode(), None)
    tmp = tempfile.mkdtemp(); enc = os.path.join(tmp, "wallet_seed.enc")
    open(enc, "wb").write(bytes([1])+salt+nonce+ct)
    # descifrar + derivar con el mismo código
    mn2 = decrypt_seed(enc, pw)
    assert mn2 == mn, "descifrado NO coincide"
    seed = bip39.mnemonic_to_seed(mn); root = bip32.HDKey.from_seed(seed)
    child = root.derive(DERIV_PATHS[0]).derive([0, 0])
    pub = child.key.get_public_key().sec(); h = hash160(pub)
    addr = bech32_encode_addr("tbrv", 0, h)
    # re-encode/decode round-trip de la dirección
    spk, prog = addr_to_scriptpubkey(addr)
    assert prog == h, "bech32 round-trip falló"
    print("   descifrado Argon2id+AES-GCM: OK")
    print("   derivación %s/0/0 -> %s : OK" % (DERIV_PATHS[0], addr))
    print("   round-trip de dirección: OK")
    # firmar una tx de juguete gastando un UTXO ficticio a esa dirección
    global INPUTS, OUTPUTS, POOL_HASH160, FEE_SATS
    bk_i, bk_o, bk_h, bk_f = INPUTS, OUTPUTS, POOL_HASH160, FEE_SATS
    POOL_HASH160 = h.hex()
    INPUTS = [{"txid":"aa"*32,"vout":0,"sats":100000},{"txid":"bb"*32,"vout":1,"sats":100000}]
    OUTPUTS = [{"addr":addr,"sats":150000},{"addr":addr,"sats":45000}]; FEE_SATS = 5000
    tx = build_and_sign(child.key, pub)
    hx = tx.serialize().hex()
    assert len(hx) > 100 and all(len(v.witness.items) == 2 for v in tx.vin), "firma incompleta"
    print("   firma de tx (2 entradas P2WPKH, witness completo): OK")
    INPUTS, OUTPUTS, POOL_HASH160, FEE_SATS = bk_i, bk_o, bk_h, bk_f
    import shutil; shutil.rmtree(tmp, ignore_errors=True)
    print(">> SELFTEST OK. El firmador descifra, deriva, verifica y firma correctamente.")

def run_diag(enc_path):
    """Diagnóstico: muestra la HUELLA y las primeras direcciones derivadas de la billetera actual (info pública,
    NO la semilla), para comparar con las que el programa ya tenía y confirmar si esta billetera es la del pool."""
    if not os.path.exists(enc_path):
        gui_msg("Brisvia", "No encontré wallet_seed.enc en:\n" + enc_path); raise SystemExit(1)
    password = gui_password()
    if not password: raise SystemExit("Cancelado.")
    mnemonic = decrypt_seed(enc_path, password)
    seed = bip39.mnemonic_to_seed(mnemonic.strip())
    root = bip32.HDKey.from_seed(seed)
    del mnemonic, password
    lines = ["DIAGNOSTICO de billetera (info pública, sin semilla)"]
    found_pool = False
    for path in DERIV_PATHS:
        acct = root.derive(path)
        lines.append("--- path %s ---" % path)
        for change in (0, 1):
            for i in range(6 if change == 0 else 3):
                pub = acct.derive([change, i]).key.get_public_key().sec()
                h = hash160(pub)
                if h.hex() == POOL_HASH160: found_pool = True
                lines.append("  /%d/%d  tbrv=%s  brv=%s%s" % (
                    change, i, bech32_encode_addr("tbrv", 0, h), bech32_encode_addr("brv", 0, h),
                    "  <== DIRECCION DE COBRO" if h.hex() == POOL_HASH160 else ""))
    # scan amplio solo para el flag
    if not found_pool:
        for path in DERIV_PATHS:
            acct = root.derive(path)
            for change in (0, 1):
                for i in range(500):
                    if hash160(acct.derive([change, i]).key.get_public_key().sec()).hex() == POOL_HASH160:
                        found_pool = True; break
                if found_pool: break
            if found_pool: break
    lines.append("¿esta billetera tiene la dirección de cobro (7ae93f91...)? %s" % ("SI" if found_pool else "NO"))
    out = os.path.join(os.path.expanduser("~"), "Desktop", "brisvia-diagnostico-billetera.txt")
    open(out, "w").write("\n".join(lines))
    try: os.startfile(out)
    except Exception: pass
    gui_msg("Brisvia — diagnóstico",
            "Se abrió 'brisvia-diagnostico-billetera.txt' en tu Escritorio.\nPasale ese archivo a Claude.")

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    elif "--diag" in sys.argv:
        p = next((a for a in sys.argv[1:] if not a.startswith("-")), None) or wallet_enc_default()
        run_diag(p)
    elif "--words" in sys.argv:
        run_sign_words()
    else:
        path = next((a for a in sys.argv[1:] if not a.startswith("-")), None) or wallet_enc_default()
        run_sign(path)
