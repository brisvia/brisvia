# -*- coding: utf-8 -*-
"""Custodia OFICIAL de la pool Brisvia (MAINNET) — generación y recuperación OFFLINE.

Objetivo (revisión de release 2026-07-19): crear la custodia definitiva del pool con la semilla y las
claves privadas SOLO en la máquina de Fernando (offline). El servidor queda watch-only: recibe únicamente
la DIRECCIÓN y el DESCRIPTOR PÚBLICO (nunca la semilla). La intervención de Fernando se reduce a:
  1) generar y VER las 12 palabras (esta herramienta las muestra en una ventana),
  2) copiarlas en papel,
  3) confirmar la recuperación (esta herramienta la verifica sola).

Nunca escribe la semilla a disco. El manifiesto que sí escribe (para el server) contiene SOLO datos
públicos: dirección de custodia, scriptPubKey, descriptor watch-only, huella y ruta de derivación.

Parámetros de MAINNET Brisvia: coin type 9339' (BIP84), hrp "brv", P2WPKH, ruta 84h/9339h/0h /0/0.

Uso:
  python custodia_mainnet.py --selftest   # prueba end-to-end con datos DESCARTABLES (no genera nada real)
  python custodia_mainnet.py --generar     # genera la custodia real y muestra las 12 palabras (Fernando)
  python custodia_mainnet.py --recuperar    # ingresás las 12 palabras y verifica que dan la misma dirección
"""
import sys, os, json, hashlib

try:
    from embit import bip39, bip32
except Exception as e:
    print("Falta embit. Instalá con:  pip install embit\n detalle:", e); sys.exit(2)

HRP = "brv"                      # mainnet
ACCOUNT_PATH = "m/84h/9339h/0h"  # BIP84, coin type Brisvia 9339'
CHAIN = 0                        # rama externa (recepción)
INDEX = 0                        # dirección de custodia del pool
MANIFEST = os.path.join(os.path.expanduser("~"), "Desktop", "brisvia-custodia-pool-manifiesto.json")

# --- bech32 (hrp propio "brv") ---
CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"
def _polymod(v):
    g=[0x3b6a57b2,0x26508e6d,0x1ea119fa,0x3d4233dd,0x2a1462b3]; c=1
    for d in v:
        b=c>>25; c=((c&0x1ffffff)<<5)^d
        for i in range(5):
            if (b>>i)&1: c^=g[i]
    return c
def _hexp(h): return [ord(x)>>5 for x in h]+[0]+[ord(x)&31 for x in h]
def _cb(data,frm,to,pad=True):
    acc=0;bits=0;ret=[];maxv=(1<<to)-1
    for b in data:
        acc=(acc<<frm)|b;bits+=frm
        while bits>=to: bits-=to;ret.append((acc>>bits)&maxv)
    if pad and bits: ret.append((acc<<(to-bits))&maxv)
    return ret
def bech32_addr(hrp, witver, prog):
    data=[witver]+_cb(list(prog),8,5); values=_hexp(hrp)+data
    const=0x2bc830a3 if witver>0 else 1
    pm=_polymod(values+[0,0,0,0,0,0])^const
    chk=[(pm>>5*(5-i))&31 for i in range(6)]
    return hrp+"1"+"".join(CHARSET[d] for d in data+chk)
def hash160(b): return hashlib.new("ripemd160", hashlib.sha256(b).digest()).digest()

def derive_custody(mnemonic):
    """Deriva la custodia (dirección, spk, huella, xpub de cuenta, descriptor watch-only) desde la semilla.
    La semilla NO se persiste; solo se devuelve material PÚBLICO."""
    seed = bip39.mnemonic_to_seed(mnemonic.strip())
    root = bip32.HDKey.from_seed(seed)
    fp = hash160(root.key.get_public_key().sec())[:4].hex()
    acct = root.derive(ACCOUNT_PATH)
    acct_xpub = acct.to_public().to_base58()
    child = acct.derive([CHAIN, INDEX])
    pub = child.key.get_public_key().sec()
    h160 = hash160(pub)
    addr = bech32_addr(HRP, 0, h160)
    spk = "0014" + h160.hex()
    path_str = ACCOUNT_PATH.replace("m/", "") + f"/{CHAIN}/{INDEX}"
    descriptor = f"wpkh([{fp}/{path_str}]{pub.hex()})"           # watch-only de la dirección exacta
    account_desc = f"wpkh([{fp}/{ACCOUNT_PATH.replace('m/','')}]{acct_xpub}/{CHAIN}/*)"  # rama externa
    return {"address": addr, "spk_hex": spk, "fingerprint": fp, "path": path_str,
            "descriptor_watchonly": descriptor, "account_descriptor": account_desc,
            "account_xpub": acct_xpub}

def write_manifest(pub_info):
    manifest = {
        "red": "brisvia-mainnet",
        "proposito": "custodia oficial de la pool (watch-only en el servidor)",
        "generado_utc": "OFFLINE",
        "address": pub_info["address"],
        "scriptPubKey_hex": pub_info["spk_hex"],
        "derivation_path": "m/84h/9339h/0h/%d/%d" % (CHAIN, INDEX),
        "fingerprint": pub_info["fingerprint"],
        "descriptor_watchonly": pub_info["descriptor_watchonly"],
        "account_descriptor": pub_info["account_descriptor"],
        "account_xpub": pub_info["account_xpub"],
        "nota": ("SOLO datos públicos. La semilla NO está aquí ni en ningún archivo. Importar en el "
                 "servidor como WATCH-ONLY (importdescriptors, sin claves privadas). La firma se hace "
                 "OFFLINE con las 12 palabras.")
    }
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    return MANIFEST

# --- ventanas gráficas (para VER las 12 palabras y confirmar) ---
def gui_show_words(mnemonic, addr):
    try:
        import tkinter as tk
        from tkinter import messagebox
        root = tk.Tk(); root.withdraw()
        messagebox.showinfo(
            "Brisvia — CUSTODIA de la pool (12 palabras)",
            "ESCRIBÍ ESTAS 12 PALABRAS EN PAPEL Y GUARDALAS EN UN LUGAR SEGURO.\n"
            "No las saques foto ni las guardes en la computadora.\n\n"
            + mnemonic + "\n\n"
            "Dirección de custodia (pública):\n" + addr + "\n\n"
            "Cuando las tengas anotadas, cerrá esta ventana y confirmá la recuperación.",
            parent=root)
        root.destroy()
    except Exception:
        print("\n=== 12 PALABRAS (anotalas en papel) ===\n" + mnemonic + "\n")
        print("Dirección de custodia:", addr)

def gui_words_input():
    try:
        import tkinter as tk
        from tkinter import simpledialog
        root = tk.Tk(); root.withdraw()
        w = simpledialog.askstring("Brisvia — recuperar custodia",
                                   "Escribí las 12 palabras de la custodia (separadas por espacio):",
                                   parent=root)
        root.destroy(); return w
    except Exception:
        return input("12 palabras: ")

def cmd_generar():
    mnemonic = bip39.mnemonic_from_bytes(os.urandom(16))   # 128 bits -> 12 palabras
    info = derive_custody(mnemonic)
    gui_show_words(mnemonic, info["address"])
    path = write_manifest(info)
    del mnemonic
    print("Manifiesto (solo datos públicos) escrito en:", path)
    print("Dirección de custodia:", info["address"])
    print("Descriptor watch-only:", info["descriptor_watchonly"])
    print(">> Siguiente: verificá la recuperación con:  python custodia_mainnet.py --recuperar")

def cmd_recuperar():
    if not os.path.exists(MANIFEST):
        print("No encuentro el manifiesto. Primero corré --generar."); sys.exit(1)
    man = json.load(open(MANIFEST, encoding="utf-8"))
    words = gui_words_input()
    if not words or len(words.split()) not in (12, 24):
        print("No ingresaste 12 (o 24) palabras."); sys.exit(1)
    info = derive_custody(" ".join(words.strip().lower().split()))
    del words
    ok = info["address"] == man["address"] and info["spk_hex"] == man["scriptPubKey_hex"]
    print(("OK: la recuperación da EXACTAMENTE la misma dirección de custodia." if ok else
           "ERROR: la dirección recuperada NO coincide con el manifiesto. Revisá las palabras."))
    try:
        import tkinter as tk
        from tkinter import messagebox
        r = tk.Tk(); r.withdraw()
        messagebox.showinfo("Brisvia — recuperación",
            ("Perfecto: tus 12 palabras recuperan la misma custodia.\n" + info["address"]) if ok
            else "Las palabras NO recuperan la custodia. Revisá el papel.", parent=r)
        r.destroy()
    except Exception:
        pass
    sys.exit(0 if ok else 1)

def selftest():
    print(">> SELFTEST custodia (datos DESCARTABLES, no genera nada real)")
    mn = bip39.mnemonic_from_bytes(b"\x00" * 16)     # semilla fija descartable
    a = derive_custody(mn)
    b = derive_custody(mn)                            # determinista
    assert a["address"] == b["address"], "la derivación NO es determinista"
    assert a["address"].startswith("brv1q"), "la dirección mainnet debe ser brv1q (P2WPKH)"
    assert a["spk_hex"].startswith("0014") and len(a["spk_hex"]) == 44, "spk P2WPKH inválido"
    # round-trip: recuperar con las mismas palabras -> misma dirección
    rec = derive_custody(mn)
    assert rec["spk_hex"] == a["spk_hex"], "recuperación NO coincide"
    # una semilla distinta da otra dirección
    mn2 = bip39.mnemonic_from_bytes(b"\x01" * 16)
    assert derive_custody(mn2)["address"] != a["address"], "semillas distintas deberían dar direcciones distintas"
    print("   derivación 84h/9339h/0h/0/0 -> %s : OK" % a["address"])
    print("   descriptor watch-only: %s" % a["descriptor_watchonly"])
    print("   recuperación determinista: OK ; semillas distintas -> direcciones distintas: OK")
    print(">> SELFTEST OK. La herramienta genera custodia, emite manifiesto público y verifica recuperación.")

if __name__ == "__main__":
    if "--selftest" in sys.argv:
        selftest()
    elif "--generar" in sys.argv:
        cmd_generar()
    elif "--recuperar" in sys.argv:
        cmd_recuperar()
    else:
        print(__doc__)
