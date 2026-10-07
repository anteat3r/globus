#!/usr/bin/env python3
"""
revise_groceries.py

Revises the weights (mass in kg) and sizes (width in m, reach collision radius,
and shelf/cart padding radius) for all 112 grocery items in item.c so that they are
physically realistic based on actual supermarket package weights and dimensions.
"""

import os
import re

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ITEM_C_PATH = os.path.join(BASE_DIR, "item.c")

FRUIT_ITEMS = [
    ("watermelon.png", 4.50, 0.32, 0.55, 0.14, "Watermelon (whole, ~4.5kg)"),
    ("orange.png", 0.22, 0.13, 0.45, 0.06, "Orange (single, ~220g)"),
    ("avocado.png", 0.18, 0.12, 0.45, 0.06, "Avocado (single, ~180g)"),
    ("blueberries.png", 0.15, 0.13, 0.45, 0.06, "Blueberries (punnet 125g, ~150g total)"),
    ("grapes.png", 0.50, 0.18, 0.48, 0.08, "Grapes (bunch, ~500g)"),
    ("peanuts.png", 0.20, 0.14, 0.45, 0.06, "Peanuts (bag, 200g)"),
    ("pineapple.png", 1.60, 0.20, 0.50, 0.10, "Pineapple (whole, ~1.6kg)"),
    ("apple.png", 0.18, 0.12, 0.45, 0.06, "Apple (single, ~180g)"),
    ("coconut.png", 0.65, 0.15, 0.46, 0.07, "Coconut (whole, ~650g)"),
    ("mango.png", 0.35, 0.14, 0.45, 0.07, "Mango (single, ~350g)"),
    ("carrots.png", 1.00, 0.22, 0.50, 0.10, "Carrots (bag 1kg)"),
    ("cauliflower.png", 0.90, 0.22, 0.50, 0.10, "Cauliflower (head, ~900g)"),
    ("onion.png", 0.14, 0.12, 0.45, 0.06, "Onion (single, ~140g)"),
    ("potatoes.png", 2.50, 0.26, 0.52, 0.12, "Potatoes (sack 2.5kg)"),
    ("tomato.png", 0.14, 0.12, 0.45, 0.06, "Tomato (single, ~140g)"),
    ("artichoke.png", 0.25, 0.14, 0.45, 0.07, "Artichoke (single, ~250g)"),
    ("broccoli.png", 0.50, 0.18, 0.48, 0.08, "Broccoli (head, 500g)"),
    ("corn.png", 0.30, 0.20, 0.48, 0.08, "Corn cob (~300g)"),
    ("garlic.png", 0.06, 0.10, 0.42, 0.05, "Garlic (single bulb, ~60g)"),
    ("pumpkin.png", 2.20, 0.25, 0.52, 0.12, "Pumpkin Hokkaido (~2.2kg)"),
    ("pomegranate.png", 0.30, 0.13, 0.45, 0.06, "Pomegranate (single, ~300g)"),
    ("pear.png", 0.18, 0.12, 0.45, 0.06, "Pear (single, ~180g)"),
    ("raspberries.png", 0.15, 0.12, 0.45, 0.06, "Raspberries (punnet 125g)"),
    ("cherries.png", 0.28, 0.14, 0.45, 0.07, "Cherries (pack 250g)"),
    ("dates.png", 0.22, 0.14, 0.45, 0.06, "Dates (box 200g)"),
    ("lemon.png", 0.12, 0.11, 0.44, 0.05, "Lemon (single, ~120g)"),
    ("lime.png", 0.08, 0.10, 0.42, 0.05, "Lime (single, ~80g)"),
    ("bananas.png", 1.10, 0.22, 0.50, 0.10, "Bananas (bunch ~1.1kg)"),
]

BAKERY_ITEMS = [
    ("chleba.png", 0.90, 0.25, 0.52, 0.11, "Chléb Šumava (loaf 900g)"),
    ("houska.png", 0.06, 0.12, 0.44, 0.06, "Pletená houska (60g)"),
    ("rohlik.png", 0.043, 0.17, 0.45, 0.06, "Standardní rohlík (43g)"),
    ("cake.png", 1.20, 0.24, 0.52, 0.11, "Birthday Cake (~1.2kg)"),
    ("croissant.png", 0.06, 0.14, 0.45, 0.06, "Butter Croissant (60g)"),
    ("macarons.png", 0.10, 0.13, 0.45, 0.06, "Macarons box (100g)"),
    ("sandwitch.png", 0.18, 0.15, 0.46, 0.07, "Sandwich (180g)"),
    ("toast.png", 0.50, 0.20, 0.48, 0.09, "Toast Bread pack (500g)"),
    ("kobliha.png", 0.065, 0.11, 0.44, 0.05, "Kobliha marmeládová (65g)"),
    ("loupak.png", 0.055, 0.15, 0.45, 0.06, "Makový loupák (55g)"),
    ("snek.png", 0.085, 0.12, 0.44, 0.06, "Skořicový šnek (85g)"),
    ("syrovy_rohlik.png", 0.07, 0.18, 0.46, 0.06, "Sýrový rohlík (70g)"),
    ("vanocka.png", 0.40, 0.26, 0.52, 0.11, "Vánočka s mandlemi (400g)"),
    ("zbojnicka_placka.png", 0.11, 0.15, 0.46, 0.07, "Zbojnická placka (110g)"),
    ("kolacek.png", 0.05, 0.10, 0.42, 0.05, "Svatební / moravský koláček (50g)"),
    ("babovka.png", 0.40, 0.22, 0.50, 0.10, "Bábovka mramorová (400g)"),
    ("strudl.png", 0.35, 0.24, 0.50, 0.10, "Jablečný štrúdl (350g)"),
    ("bageta.png", 0.12, 0.26, 0.52, 0.08, "Francouzská bageta (120g)"),
    ("pletynka.png", 0.08, 0.14, 0.45, 0.06, "Pletýnka se sádlem (80g)"),
    ("kaiserka.png", 0.06, 0.12, 0.44, 0.06, "Kaiserka natural (60g)"),
    ("kvasovy_chleb.png", 0.80, 0.25, 0.52, 0.11, "Tradiční kvasový chléb (800g)"),
    ("moravsky_kolac.png", 0.13, 0.15, 0.46, 0.07, "Moravský koláč borůvkový (130g)"),
    ("pernik.png", 0.06, 0.12, 0.44, 0.06, "Medový perník (60g)"),
    ("vetrnik.png", 0.15, 0.13, 0.45, 0.06, "Karamelový větrník (150g)"),
    ("spicka.png", 0.08, 0.11, 0.44, 0.05, "Likérová špička (80g)"),
    ("rakevicka.png", 0.05, 0.12, 0.44, 0.05, "Rakevička se šlehačkou (50g)"),
    ("linecke.png", 0.08, 0.11, 0.44, 0.05, "Linecká kolečka (80g)"),
    ("cesnekova_bageta.png", 0.17, 0.24, 0.50, 0.08, "Česneková bageta s máslem (170g)"),
]

MEAT_ITEMS = [
    ("burt.png", 0.10, 0.14, 0.45, 0.06, "Buřt / špekáček kus (~100g)"),
    ("chicken.png", 1.50, 0.24, 0.52, 0.11, "Celé chlazené kuře (~1.5kg)"),
    ("meat.png", 0.80, 0.20, 0.48, 0.09, "Vepřová pečeně / maso (~800g)"),
    ("jehneci.png", 0.60, 0.18, 0.48, 0.08, "Jehněčí maso (~600g)"),
    ("kureci_rizky.png", 0.60, 0.20, 0.48, 0.09, "Kuřecí prsní řízky balení (600g)"),
    ("parky.png", 0.25, 0.18, 0.46, 0.07, "Vídeňské párky balení (250g)"),
    ("steak.png", 0.30, 0.16, 0.46, 0.07, "Hovězí steak (300g)"),
    ("hovezi.png", 0.80, 0.20, 0.48, 0.09, "Hovězí zadní balení (800g)"),
    ("spekacky.png", 0.60, 0.22, 0.50, 0.10, "Špekáčky vázané balení (600g)"),
    ("vysocina.png", 0.10, 0.15, 0.46, 0.07, "Vysočina krájená (100g)"),
    ("polican.png", 0.10, 0.15, 0.46, 0.07, "Poličan krájený (100g)"),
    ("herkules.png", 0.10, 0.15, 0.46, 0.07, "Herkules krájený (100g)"),
    ("lovecky_salam.png", 0.10, 0.15, 0.46, 0.07, "Lovecký salám krájený (100g)"),
    ("uherak.png", 0.10, 0.15, 0.46, 0.07, "Uherský salám krájený (100g)"),
    ("gothaj.png", 0.20, 0.16, 0.46, 0.07, "Gothajský salám (200g)"),
    ("sunka_dusena.png", 0.10, 0.16, 0.46, 0.07, "Dušená šunka 95% (100g)"),
    ("prazska_sunka.png", 0.10, 0.16, 0.46, 0.07, "Pražská šunka (100g)"),
    ("anglicka_slanina.png", 0.10, 0.16, 0.46, 0.07, "Anglická slanina (100g)"),
    ("uzene_maso.png", 0.30, 0.18, 0.48, 0.08, "Moravské uzené maso (300g)"),
    ("klobasa.png", 0.20, 0.18, 0.46, 0.07, "Papriková klobása (200g)"),
    ("sekana.png", 0.50, 0.18, 0.48, 0.08, "Pečená sekaná (500g)"),
    ("tlacenka.png", 0.20, 0.15, 0.46, 0.07, "Tlačenka světlá plátky (200g)"),
    ("jitrnice.png", 0.22, 0.18, 0.46, 0.07, "Jitrnice zabijačková (220g)"),
    ("jelito.png", 0.22, 0.18, 0.46, 0.07, "Jelito zabijačkové (220g)"),
    ("mlete_maso.png", 0.50, 0.18, 0.48, 0.08, "Mleté maso mix (500g)"),
    ("pastika.png", 0.08, 0.11, 0.44, 0.05, "Hamé Májka paštika (75g)"),
    ("kapr.png", 0.50, 0.22, 0.50, 0.09, "Český kapr čerstvý filet (500g)"),
    ("losos.png", 0.35, 0.22, 0.50, 0.08, "Norský losos filet (350g)"),
]

MILK_ITEMS = [
    ("milk.png", 1.03, 0.14, 0.48, 0.07, "Mléko čerstvé 1L (1.03kg)"),
    ("butter.png", 0.25, 0.14, 0.45, 0.06, "Máslo Jihočeské kostka (250g)"),
    ("cheese.png", 0.30, 0.15, 0.46, 0.07, "Tvrdý sýr blok (300g)"),
    ("jogurt_bily.png", 0.52, 0.14, 0.46, 0.07, "Hollandia Selský bílý jogurt (500g)"),
    ("jogurt_jahodovy.png", 0.16, 0.12, 0.44, 0.06, "Florian jahodový jogurt (150g)"),
    ("tvaroh.png", 0.26, 0.15, 0.46, 0.07, "Choceňský tvaroh měkký (250g)"),
    ("pribinacek.png", 0.13, 0.11, 0.44, 0.05, "Pribináček vanilka (125g)"),
    ("termix.png", 0.095, 0.11, 0.44, 0.05, "Termix kakao (90g)"),
    ("hermelin.png", 0.10, 0.12, 0.44, 0.06, "Sedlčanský Hermelín (100g)"),
    ("olomoucke_tvaruzky.png", 0.10, 0.12, 0.44, 0.06, "Olomoucké tvarůžky (100g)"),
    ("eidam.png", 0.10, 0.15, 0.46, 0.07, "Eidam 30% plátky (100g)"),
    ("niva.png", 0.11, 0.13, 0.45, 0.06, "Jihočeská Niva (110g)"),
    ("lucina.png", 0.10, 0.12, 0.44, 0.06, "Lučina čistá (100g)"),
    ("korbaciky.png", 0.08, 0.13, 0.45, 0.06, "Korbáčiky pařené (80g)"),
    ("kefir.png", 0.52, 0.13, 0.46, 0.06, "Kefírové mléko (500g)"),
    ("acidofilni_mleko.png", 0.98, 0.14, 0.48, 0.07, "Acidofilní mléko (950g)"),
    ("zakysana_smetana.png", 0.19, 0.12, 0.44, 0.06, "Zakysaná smetana 16% (180g)"),
    ("slehacka.png", 0.26, 0.12, 0.45, 0.06, "Smetana ke šlehání 33% (250ml)"),
    ("podmasli.png", 0.52, 0.13, 0.46, 0.06, "Podmáslí kysané (500ml)"),
    ("cottage.png", 0.16, 0.12, 0.44, 0.06, "Cottage sýr bílý (150g)"),
    ("taveny_syr.png", 0.15, 0.13, 0.45, 0.06, "Smetanito tavený sýr (140g)"),
    ("bryndza.png", 0.13, 0.12, 0.44, 0.06, "Liptovská bryndza (125g)"),
    ("lipanek.png", 0.135, 0.11, 0.44, 0.05, "Lipánek Maxi (130g)"),
    ("balkansky_syr.png", 0.19, 0.14, 0.45, 0.06, "Balkánský sýr Žirovnice (180g)"),
    ("mozzarella.png", 0.22, 0.13, 0.45, 0.06, "Mozzarella v nálevu (125g/220g)"),
    ("gouda.png", 0.10, 0.15, 0.46, 0.07, "Gouda 48% plátky (100g)"),
    ("bobik.png", 0.075, 0.11, 0.44, 0.05, "Bobík Maxi vanilka (70g)"),
    ("parenica.png", 0.11, 0.13, 0.45, 0.06, "Parenica uzená (110g)"),
]

def generate_function(func_name, items, section_macro):
    lines = [f"static int {func_name}(int idx) {{"]
    for file, mass, width, col_rad, rad, desc in items:
        lines.append(f"  // {desc}")
        lines.append(f'  items_table[idx] = item_new_width(LoadTexture("res/{file}"),')
        lines.append(f"                                    (Vector2){{}}, 0, {width:.3f}f, {col_rad:.2f}f, {rad:.2f}f);")
        lines.append(f"  items_table[idx].mass = {mass:.3f}f;")
        lines.append(f"  item_sections[idx] = {section_macro};")
        lines.append("  idx++;")
        lines.append("")
    lines.append("  return idx;")
    lines.append("}")
    return "\n".join(lines)

def run():
    fruit_code = generate_function("fill_items_table_fruit", FRUIT_ITEMS, "OVOCE_ZELENINA")
    bakery_code = generate_function("fill_items_table_bakery", BAKERY_ITEMS, "PECIVO")
    meat_code = generate_function("fill_items_table_meat", MEAT_ITEMS, "MASO")
    milk_code = generate_function("fill_items_table_milk", MILK_ITEMS, "MLECNE_VYROBKY")

    with open(ITEM_C_PATH, "r", encoding="utf-8") as f:
        content = f.read()

    # Find where fill_items_table_fruit starts and where fill_items_table starts
    idx_start = content.find("static int fill_items_table_fruit(int idx)")
    idx_end = content.find("void fill_items_table(void)")

    if idx_start == -1 or idx_end == -1:
        print("Error: Could not locate function markers in item.c")
        return

    all_funcs = "\n\n".join([fruit_code, bakery_code, meat_code, milk_code]) + "\n\n"
    new_content = content[:idx_start] + all_funcs + content[idx_end:]

    with open(ITEM_C_PATH, "w", encoding="utf-8") as f:
        f.write(new_content)

    print(f"Successfully updated item.c with realistic weights and sizes!")
    print(f"OVOCE_ZELENINA: {len(FRUIT_ITEMS)} items")
    print(f"PECIVO:         {len(BAKERY_ITEMS)} items")
    print(f"MASO:           {len(MEAT_ITEMS)} items")
    print(f"MLECNE_VYROBKY: {len(MILK_ITEMS)} items")
    print(f"Total:          {len(FRUIT_ITEMS) + len(BAKERY_ITEMS) + len(MEAT_ITEMS) + len(MILK_ITEMS)} items")

if __name__ == "__main__":
    run()
