#!/usr/bin/env python3
"""
fetch_and_balance_groceries.py

This script:
1. Defines authentic Czech groceries across categories to balance the game sections:
   - Ovoce & Zelenina: 28 items (already present)
   - Pečivo: 28 items (15 existing + 13 added)
   - Maso & Uzeniny: 28 items (8 existing + 20 added)
   - Mléčné výrobky: 28 items (3 existing + 25 added)
   Total: 112 items (perfect 28:28:28:28 balance!)
2. Fetches high-resolution studio photos of the groceries from Czech store catalog (Rohlík.cz CDN).
3. Automatically isolates products onto clean transparent RGBA backgrounds using border flood-fill.
4. Saves optimized PNGs into the res/ directory.
5. Updates item.h (#define NUM_ITEMS 112) and item.c with the new item tables and realistic masses.
"""

import os
import io
import re
import sys
import requests
import numpy as np
from PIL import Image
from collections import deque

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES_DIR = os.path.join(BASE_DIR, "res")

ITEMS_TO_ADD = [
    # --- MLECNE_VYROBKY (25 items) ---
    {
        "category": "MLECNE_VYROBKY",
        "file": "jogurt_bily.png",
        "query": "Hollandia Selský jogurt bílý 3,5%",
        "mass": 0.50,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "jogurt_jahodovy.png",
        "query": "Olma Florian smetanový jogurt jahoda",
        "mass": 0.15,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "tvaroh.png",
        "query": "Choceňská mlékárna Choceňský jemný tvaroh",
        "mass": 0.25,
        "width": 0.30,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "pribinacek.png",
        "query": "Pribináček velký vanilkový",
        "mass": 0.125,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "termix.png",
        "query": "Ekomilk Termix kakao",
        "mass": 0.09,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "hermelin.png",
        "query": "Sedlčanský Hermelín originál",
        "mass": 0.10,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "olomoucke_tvaruzky.png",
        "query": "A.W. Olomoucké tvarůžky malé 1%",
        "mass": 0.10,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "eidam.png",
        "query": "Miil Eidam 30% plátky",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "niva.png",
        "query": "Madeta Jihočeská niva 45%",
        "mass": 0.11,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "lucina.png",
        "query": "Lučina",
        "mass": 0.10,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "korbaciky.png",
        "query": "Korbáčky přírodní pařené",
        "mass": 0.08,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "kefir.png",
        "query": "Miil Kefírové mléko 1%",
        "mass": 0.50,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "acidofilni_mleko.png",
        "query": "Miil Acidofilní mléko 3,6 %",
        "mass": 0.95,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "zakysana_smetana.png",
        "query": "Miil Zakysaná smetana 16 % tuku",
        "mass": 0.18,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "slehacka.png",
        "query": "Miil Čerstvá smetana ke šlehání 33%",
        "mass": 0.25,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "podmasli.png",
        "query": "Miil Podmáslí kysané 1 %",
        "mass": 0.50,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "cottage.png",
        "query": "Madeta Jihočeský cottage sýr",
        "mass": 0.15,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "taveny_syr.png",
        "query": "Smetanito Smetanové tavený sýr",
        "mass": 0.15,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "bryndza.png",
        "query": "Liptov Bryndza letní kostka",
        "mass": 0.125,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "lipanek.png",
        "query": "Madeta Lipánek Maxi vanilkový",
        "mass": 0.13,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "balkansky_syr.png",
        "query": "Balsýr Žirovnický sýr balkánského typu",
        "mass": 0.18,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "mozzarella.png",
        "query": "Miil Mozzarella 42%",
        "mass": 0.125,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "gouda.png",
        "query": "Miil Gouda 47% plátky",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "bobik.png",
        "query": "Bobík Maxi smetanový krém s vanilkovou příchutí",
        "mass": 0.07,
        "width": 0.25,
    },
    {
        "category": "MLECNE_VYROBKY",
        "file": "parenica.png",
        "query": "Miil Parenica uzená",
        "mass": 0.11,
        "width": 0.25,
    },

    # --- MASO (20 items) ---
    {
        "category": "MASO",
        "file": "spekacky.png",
        "query": "Váhala Špekáčky extra vázané bez lepku",
        "mass": 0.60,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "vysocina.png",
        "query": "Dacello Vysočina krájená",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "polican.png",
        "query": "Dacello Poličan krájený",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "herkules.png",
        "query": "Dacello Herkules krájený",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "lovecky_salam.png",
        "query": "Kmotr Lovecký salám",
        "mass": 0.18,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "uherak.png",
        "query": "Dacello Uherský salám krájený",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "gothaj.png",
        "query": "Chodura Gothajský salám",
        "mass": 0.20,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "sunka_dusena.png",
        "query": "Dacello Tradiční pražská šunka 95 %",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "prazska_sunka.png",
        "query": "Rohlik.cz Šunka nejvyšší jakosti",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "anglicka_slanina.png",
        "query": "Dacello Anglická slanina shaved 94 %",
        "mass": 0.10,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "uzene_maso.png",
        "query": "Dacello Moravské uzené 97 %",
        "mass": 0.30,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "klobasa.png",
        "query": "Dacello Klobása 96 %",
        "mass": 0.20,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "sekana.png",
        "query": "Primas Pečená sekaná",
        "mass": 0.50,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "tlacenka.png",
        "query": "tlačenka",
        "mass": 0.30,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "jitrnice.png",
        "query": "Qualivo Jitrnice",
        "mass": 0.25,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "jelito.png",
        "query": "Maso Klouda Jelita",
        "mass": 0.25,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "mlete_maso.png",
        "query": "Mělněný polotovar mix vepřové a hovězí",
        "mass": 0.50,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "pastika.png",
        "query": "Hamé Májka",
        "mass": 0.10,
        "width": 0.25,
    },
    {
        "category": "MASO",
        "file": "kapr.png",
        "query": "Český Kapr z Chlumce - hranolky (čerstvé)",
        "mass": 0.50,
        "width": 0.30,
    },
    {
        "category": "MASO",
        "file": "losos.png",
        "query": "Norský losos filet s kůží",
        "mass": 0.30,
        "width": 0.30,
    },

    # --- PECIVO (13 items) ---
    {
        "category": "PECIVO",
        "file": "babovka.png",
        "query": "Kuchenmeister Bábovka mramorová",
        "mass": 0.40,
        "width": 0.30,
    },
    {
        "category": "PECIVO",
        "file": "strudl.png",
        "query": "Jablečný závin",
        "mass": 0.30,
        "width": 0.30,
    },
    {
        "category": "PECIVO",
        "file": "bageta.png",
        "query": "Francouzská bageta střední",
        "mass": 0.12,
        "width": 0.35,
    },
    {
        "category": "PECIVO",
        "file": "pletynka.png",
        "query": "Rohlíkova pletýnka se sádlem bez posypu",
        "mass": 0.08,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "kaiserka.png",
        "query": "Kaiserka natural",
        "mass": 0.06,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "kvasovy_chleb.png",
        "query": "Rohlíkův Chléb tradiční kvasový",
        "mass": 0.75,
        "width": 0.30,
    },
    {
        "category": "PECIVO",
        "file": "moravsky_kolac.png",
        "query": "Zrno zrnko Moravský koláč velký borůvkový",
        "mass": 0.12,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "pernik.png",
        "query": "Perníkář Tradiční medový perník švestka",
        "mass": 0.06,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "vetrnik.png",
        "query": "Cukrárna Myšák Větrník",
        "mass": 0.15,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "spicka.png",
        "query": "Likérová špička",
        "mass": 0.08,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "rakevicka.png",
        "query": "Rakvičky",
        "mass": 0.05,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "linecke.png",
        "query": "Klasa Linecké koláčky",
        "mass": 0.07,
        "width": 0.25,
    },
    {
        "category": "PECIVO",
        "file": "cesnekova_bageta.png",
        "query": "Česneková bageta s máslem",
        "mass": 0.17,
        "width": 0.30,
    },
]

def isolate_background(im: Image.Image) -> Image.Image:
    im = im.convert("RGBA")
    im.thumbnail((512, 512), Image.Resampling.LANCZOS)
    arr = np.array(im)
    h, w = arr.shape[:2]

    # If alpha channel already has significant transparency, keep it
    if np.any(arr[:, :, 3] < 200):
        bbox = im.getbbox()
        return im.crop(bbox) if bbox else im

    is_white = (arr[:, :, 0] > 238) & (arr[:, :, 1] > 238) & (arr[:, :, 2] > 238)
    visited = np.zeros((h, w), dtype=bool)
    q = deque()

    for x in range(w):
        if is_white[0, x]:
            q.append((0, x))
            visited[0, x] = True
        if is_white[h - 1, x]:
            q.append((h - 1, x))
            visited[h - 1, x] = True
    for y in range(h):
        if is_white[y, 0]:
            q.append((y, 0))
            visited[y, 0] = True
        if is_white[y, w - 1]:
            q.append((y, w - 1))
            visited[y, w - 1] = True

    while q:
        cy, cx = q.popleft()
        for dy, dx in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            ny, nx = cy + dy, cx + dx
            if 0 <= ny < h and 0 <= nx < w and not visited[ny, nx] and is_white[ny, nx]:
                visited[ny, nx] = True
                q.append((ny, nx))

    arr[visited, 3] = 0
    res = Image.fromarray(arr)
    bbox = res.getbbox()
    if bbox:
        res = res.crop(bbox)
    return res

def download_and_process_images():
    headers = {
        "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Accept": "application/json",
    }
    os.makedirs(RES_DIR, exist_ok=True)

    print(f"Fetching {len(ITEMS_TO_ADD)} items...")
    for idx, item in enumerate(ITEMS_TO_ADD, 1):
        target_path = os.path.join(RES_DIR, item["file"])
        if os.path.exists(target_path) and os.path.getsize(target_path) > 1000:
            print(f"[{idx}/{len(ITEMS_TO_ADD)}] {item['file']} already exists, skipping download.")
            continue

        print(f"[{idx}/{len(ITEMS_TO_ADD)}] Searching for '{item['query']}'...")
        r = requests.get(
            "https://www.rohlik.cz/services/frontend-service/search-metadata",
            params={"search": item["query"], "offset": 0, "limit": 1, "companyId": 1},
            headers=headers,
            timeout=10,
        )
        if r.status_code != 200:
            print(f"  Error searching {item['query']}: {r.status_code}")
            continue

        data = r.json()
        products = data.get("data", {}).get("productList", [])
        if not products or not products[0].get("imgPath"):
            print(f"  No products found for {item['query']}!")
            continue

        prod = products[0]
        img_url = "https://cdn.rohlik.cz" + prod["imgPath"]
        print(f"  Downloading {prod.get('productName')} from {img_url}...")
        img_res = requests.get(img_url, headers=headers, timeout=10)
        if img_res.status_code != 200:
            print(f"  Failed to download image {img_url}")
            continue

        im = Image.open(io.BytesIO(img_res.content))
        processed = isolate_background(im)
        processed.save(target_path, format="PNG")
        print(f"  Saved {target_path} ({processed.size})")

def update_c_sources():
    # 1. Update item.h to set NUM_ITEMS = 112
    item_h_path = os.path.join(BASE_DIR, "item.h")
    with open(item_h_path, "r", encoding="utf-8") as f:
        content_h = f.read()

    new_content_h = re.sub(r"#define NUM_ITEMS \d+", f"#define NUM_ITEMS {28 * 4}", content_h)
    with open(item_h_path, "w", encoding="utf-8") as f:
        f.write(new_content_h)
    print("Updated item.h with NUM_ITEMS = 112.")

    # 2. Update item.c with realistic weights and sizes
    import revise_groceries
    revise_groceries.run()

if __name__ == "__main__":
    download_and_process_images()
    update_c_sources()
    print("Done! All 112 groceries added and balanced (28 per category).")
