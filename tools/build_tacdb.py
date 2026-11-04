"""Genera build/tac.bin (base TAC binaria que se incrusta en el .exe) a partir de data/tac_full.csv.

Cada entrada: TAC -> [marca, nombre_comercial, referencia/detalles, año]
"""
import csv
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "data" / "tac_full.csv"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "build" / "tac.bin"

BRAND_CASE = {
    "APPLE": "Apple", "SAMSUNG": "Samsung", "XIAOMI": "Xiaomi", "REDMI": "Redmi", "POCO": "POCO",
    "HUAWEI": "Huawei", "HONOR": "Honor", "OPPO": "OPPO", "REALME": "realme", "VIVO": "vivo",
    "MOTOROLA": "Motorola", "NOKIA": "Nokia", "HMD": "HMD", "LG": "LG", "ZTE": "ZTE", "TCL": "TCL",
    "ALCATEL": "Alcatel", "SONY": "Sony", "GOOGLE": "Google", "ONEPLUS": "OnePlus", "HTC": "HTC",
    "ASUS": "ASUS", "LENOVO": "Lenovo", "TECNO": "TECNO", "INFINIX": "Infinix", "ITEL": "itel",
    "NOTHING": "Nothing", "DORO": "Doro", "FAIRPHONE": "Fairphone", "CROSSCALL": "Crosscall",
    "BLACKBERRY": "BlackBerry", "ERICSSON": "Ericsson", "SAGEM": "Sagem", "SIEMENS": "Siemens",
    "WIKO": "Wiko", "BLACKVIEW": "Blackview", "ULEFONE": "Ulefone", "OUKITEL": "Oukitel",
    "QUECTEL": "Quectel", "SIERRA": "Sierra Wireless", "TELIT": "Telit", "MICROMAX": "Micromax",
    "MEIZU": "Meizu", "CAT": "Cat", "SPC": "SPC", "HISENSE": "Hisense", "SHARP": "Sharp",
    "KYOCERA": "Kyocera", "PANASONIC": "Panasonic", "PHILIPS": "Philips", "ENERGIZER": "Energizer",
}

WORD_CASE = {
    "IPHONE": "iPhone", "IPAD": "iPad", "IPOD": "iPod", "WATCH": "Watch", "PRO": "Pro", "MAX": "Max",
    "PLUS": "Plus", "MINI": "Mini", "ULTRA": "Ultra", "NOTE": "Note", "GALAXY": "Galaxy",
    "REDMI": "Redmi", "POCO": "POCO", "XPERIA": "Xperia", "PIXEL": "Pixel", "EDGE": "Edge",
    "LITE": "Lite", "FOLD": "Fold", "FLIP": "Flip", "SE": "SE", "XR": "XR", "XS": "XS", "FE": "FE",
    "NXTPAPER": "NXTPAPER", "AIR": "Air", "NEO": "Neo", "PLAY": "Play", "POWER": "Power",
    "STYLUS": "Stylus", "RAZR": "razr", "MOTO": "moto", "NOVA": "nova", "MATE": "Mate",
    "MAGIC": "Magic", "RENO": "Reno", "FIND": "Find", "NORD": "Nord", "ZENFONE": "Zenfone",
    "CELLULAR": "Cellular", "WIFI": "Wi-Fi", "LTE": "LTE", "GPS": "GPS", "DS": "DS",
}

YEAR_RE = re.compile(r"^(19[89]\d|20[0-2]\d)$")
ATTACHED_YEAR_RE = re.compile(r"([A-Za-z/)\]])(19[89]\d|20[0-2]\d)$")


def smart_case(text: str) -> str:
    if any(c.islower() for c in text):
        return text
    words = []
    for w in text.split(" "):
        up = w.upper()
        if up in WORD_CASE:
            words.append(WORD_CASE[up])
        elif up in BRAND_CASE:
            words.append(BRAND_CASE[up])
        elif any(ch.isdigit() for ch in w) or len(w) <= 2:
            words.append(w)  # S25, 5G, A3102, X2...
        else:
            words.append(w[:1] + w[1:].lower())
    return " ".join(words)


def brand_display(brand: str) -> str:
    b = brand.strip()
    if b.upper() in BRAND_CASE:
        return BRAND_CASE[b.upper()]
    return b[:1].upper() + b[1:].lower() if b.isupper() else b


def parse(brand: str, specs: str):
    parts = [p.strip() for p in specs.split(", ") if p.strip()]
    if not parts:
        return None
    year = ""
    if parts[0].upper() == brand.upper() and len(parts) >= 2:
        name, rest = parts[1], parts[2:]
    else:
        name, rest = parts[0], parts[1:]

    clean_rest = []
    for r in rest:
        if YEAR_RE.match(r):
            year = year or r
            continue
        m = ATTACHED_YEAR_RE.search(r)
        if m:
            year = year or m.group(2)
            r = r[: m.start(2)].rstrip()
        clean_rest.append(r)
    m = ATTACHED_YEAR_RE.search(name)
    if m:
        year = year or m.group(2)
        name = name[: m.start(2)].rstrip()

    # Si alguna parte repite el nombre con mejor capitalización (p. ej. "Apple iPhone 15 Pro"), usarla.
    pretty = None
    for r in clean_rest:
        if r.upper() == name.upper():
            pretty = r
            break
    if pretty:
        clean_rest.remove(pretty)
        name = pretty
    else:
        name = smart_case(name)

    details = " | ".join(clean_rest)
    return [brand_display(brand), name, details, year]


def main():
    import struct
    data = {}
    with SRC.open(encoding="utf-8") as f:
        for row in csv.DictReader(f):
            tac = (row.get("TAC") or "").strip()
            if not tac.isdigit() or len(tac) < 5 or len(tac) > 8:
                continue
            tac = tac.zfill(8)  # Excel quitó el 0 inicial en TACs antiguos
            entry = parse(row.get("Brand") or "", row.get("SPECS") or "")
            if not entry:
                continue
            prev = data.get(tac)
            if prev is None or len(entry[2]) + len(entry[3]) > len(prev[2]) + len(prev[3]):
                data[tac] = entry

    # Formato: "TACB" u32 versión, u32 n, u32 tamaño_pool, n × (u32 tac, u32 offset), pool
    # Cada cadena del pool: marca \x1f nombre \x1f referencia \x1f año \0 (UTF-8), deduplicadas.
    pool = bytearray()
    offsets = {}
    recs = []
    for tac in sorted(data, key=int):
        s = "\x1f".join(x.replace("\x1f", " ") for x in data[tac]).encode("utf-8") + b"\0"
        off = offsets.get(s)
        if off is None:
            off = len(pool)
            offsets[s] = off
            pool += s
        recs.append((int(tac), off))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with OUT.open("wb") as o:
        o.write(b"TACB" + struct.pack("<III", 1, len(recs), len(pool)))
        for t, off in recs:
            o.write(struct.pack("<II", t, off))
        o.write(pool)
    print(f"{len(recs)} TAC, {len(offsets)} modelos distintos, {OUT.stat().st_size/1e6:.1f} MB -> {OUT}")


if __name__ == "__main__":
    main()
