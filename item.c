#include "item.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

Item items_table[NUM_ITEMS];
int item_sections[NUM_ITEMS];
int used_items[NUM_ITEMS];
int used_items_len = 0;
int shopping_list[SHOPPING_LIST_MAX_LEN];
int shopping_list_len = SHOPPING_LIST_MAX_LEN;

Item item_new(Texture2D image, Vector2 pos, float rot, float scale,
              float col_rad, float rad) {
  return (Item){image, pos, rot, scale, col_rad, rad, 1.2f};
}

void item_set_scale_from_width(Item *item, float n_width) {
  item->scale = n_width / item->image.width;
}

Item item_new_width(Texture2D image, Vector2 pos, float rot, float new_width,
                    float col_rad, float rad) {
  Item res = item_new(image, pos, rot, 0, col_rad, rad);
  item_set_scale_from_width(&res, new_width);
  return res;
}

void item_free(Item item) { UnloadTexture(item.image); }

void item_draw(Item item, Vector2 offset, float rot) {
  Vector2 half_size = {
      (float)item.image.width / 2 * item.scale,
      (float)item.image.height / 2 * item.scale,
  };
  float total_rot = item.rot + rot;
  DrawTextureEx(
      item.image,
      Vector2Subtract(Vector2Add(offset, Vector2Rotate(item.pos, rot)),
                      Vector2Rotate(half_size, total_rot)),
      total_rot * RAD2DEG, item.scale, WHITE);
}

ColliPoly item_get_world_collider(Item item) {
  Vector2 half_size = {
      (float)item.image.width / 2 * item.scale,
      (float)item.image.height / 2 * item.scale,
  };
  return collipoly_new(4,
                       Vector2Rotate((Vector2){item.pos.x - half_size.x,
                                               item.pos.y - half_size.y},
                                     item.rot),
                       Vector2Rotate((Vector2){item.pos.x - half_size.x,
                                               item.pos.y + half_size.y},
                                     item.rot),
                       Vector2Rotate((Vector2){item.pos.x + half_size.x,
                                               item.pos.y + half_size.y},
                                     item.rot),
                       Vector2Rotate((Vector2){item.pos.x + half_size.x,
                                               item.pos.y - half_size.y},
                                     item.rot));
}

ItemArr itemarr_new(void) {
  ItemArr res = {NULL, 0, 0};
  return res;
}

void itemarr_free_all(ItemArr *crl) {
  if (crl->items) {
    free(crl->items);
    crl->items = NULL;
  }
  crl->len = 0;
  crl->cap = 0;
}

void itemarr_draw(ItemArr crl, Vector2 offset, float rot, Vector2 topleft,
                  Vector2 botright) {
  for (int i = 0; i < crl.len; i++) {
    Vector2 p = Vector2Add(offset, Vector2Rotate(crl.items[i].pos, rot));
    float r = crl.items[i].radius + 0.35f;
    if (p.x + r < topleft.x || p.x - r > botright.x ||
        p.y + r < topleft.y || p.y - r > botright.y)
      continue;
    item_draw(crl.items[i], offset, rot);
  }
}

void itemarr_push(ItemArr *crl, Item cr) {
  if (!crl->items) {
    crl->len = 0;
    crl->cap = 10;
    crl->items = malloc(crl->cap * sizeof(Item));
  } else if (crl->len >= crl->cap) {
    crl->cap *= 2;
    crl->items = realloc(crl->items, crl->cap * sizeof(Item));
  }
  crl->items[crl->len] = cr;
  crl->len++;
}

void itemarr_remove(ItemArr *crl, int index) {
  if (index >= crl->len)
    return;
  crl->len--;
  for (int i = index; i < crl->len; i++)
    crl->items[i] = crl->items[i + 1];
}

void itemarr_remove_free(ItemArr *crl, int index) {
  if (index >= crl->len)
    return;
  item_free(crl->items[index]);
  crl->len--;
  for (int i = index; i < crl->len; i++)
    crl->items[i] = crl->items[i + 1];
}

static int fill_items_table_fruit(int idx) {
  // Watermelon (whole, ~4.5kg)
  items_table[idx] = item_new_width(LoadTexture("res/watermelon.png"),
                                    (Vector2){}, 0, 0.320f, 0.55f, 0.14f);
  items_table[idx].mass = 4.500f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Orange (single, ~220g)
  items_table[idx] = item_new_width(LoadTexture("res/orange.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.220f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Avocado (single, ~180g)
  items_table[idx] = item_new_width(LoadTexture("res/avocado.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.180f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Blueberries (punnet 125g, ~150g total)
  items_table[idx] = item_new_width(LoadTexture("res/blueberries.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.150f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Grapes (bunch, ~500g)
  items_table[idx] = item_new_width(LoadTexture("res/grapes.png"),
                                    (Vector2){}, 0, 0.180f, 0.48f, 0.08f);
  items_table[idx].mass = 0.500f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Peanuts (bag, 200g)
  items_table[idx] = item_new_width(LoadTexture("res/peanuts.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.200f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Pineapple (whole, ~1.6kg)
  items_table[idx] = item_new_width(LoadTexture("res/pineapple.png"),
                                    (Vector2){}, 0, 0.200f, 0.50f, 0.10f);
  items_table[idx].mass = 1.600f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Apple (single, ~180g)
  items_table[idx] = item_new_width(LoadTexture("res/apple.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.180f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Coconut (whole, ~650g)
  items_table[idx] = item_new_width(LoadTexture("res/coconut.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.650f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Mango (single, ~350g)
  items_table[idx] = item_new_width(LoadTexture("res/mango.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.07f);
  items_table[idx].mass = 0.350f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Carrots (bag 1kg)
  items_table[idx] = item_new_width(LoadTexture("res/carrots.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.10f);
  items_table[idx].mass = 1.000f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Cauliflower (head, ~900g)
  items_table[idx] = item_new_width(LoadTexture("res/cauliflower.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.10f);
  items_table[idx].mass = 0.900f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Onion (single, ~140g)
  items_table[idx] = item_new_width(LoadTexture("res/onion.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.140f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Potatoes (sack 2.5kg)
  items_table[idx] = item_new_width(LoadTexture("res/potatoes.png"),
                                    (Vector2){}, 0, 0.260f, 0.52f, 0.12f);
  items_table[idx].mass = 2.500f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Tomato (single, ~140g)
  items_table[idx] = item_new_width(LoadTexture("res/tomato.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.140f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Artichoke (single, ~250g)
  items_table[idx] = item_new_width(LoadTexture("res/artichoke.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.07f);
  items_table[idx].mass = 0.250f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Broccoli (head, 500g)
  items_table[idx] = item_new_width(LoadTexture("res/broccoli.png"),
                                    (Vector2){}, 0, 0.180f, 0.48f, 0.08f);
  items_table[idx].mass = 0.500f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Corn cob (~300g)
  items_table[idx] = item_new_width(LoadTexture("res/corn.png"),
                                    (Vector2){}, 0, 0.200f, 0.48f, 0.08f);
  items_table[idx].mass = 0.300f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Garlic (single bulb, ~60g)
  items_table[idx] = item_new_width(LoadTexture("res/garlic.png"),
                                    (Vector2){}, 0, 0.100f, 0.42f, 0.05f);
  items_table[idx].mass = 0.060f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Pumpkin Hokkaido (~2.2kg)
  items_table[idx] = item_new_width(LoadTexture("res/pumpkin.png"),
                                    (Vector2){}, 0, 0.250f, 0.52f, 0.12f);
  items_table[idx].mass = 2.200f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Pomegranate (single, ~300g)
  items_table[idx] = item_new_width(LoadTexture("res/pomegranate.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.300f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Pear (single, ~180g)
  items_table[idx] = item_new_width(LoadTexture("res/pear.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.180f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Raspberries (punnet 125g)
  items_table[idx] = item_new_width(LoadTexture("res/raspberries.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.150f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Cherries (pack 250g)
  items_table[idx] = item_new_width(LoadTexture("res/cherries.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.07f);
  items_table[idx].mass = 0.280f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Dates (box 200g)
  items_table[idx] = item_new_width(LoadTexture("res/dates.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.220f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Lemon (single, ~120g)
  items_table[idx] = item_new_width(LoadTexture("res/lemon.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.120f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Lime (single, ~80g)
  items_table[idx] = item_new_width(LoadTexture("res/lime.png"),
                                    (Vector2){}, 0, 0.100f, 0.42f, 0.05f);
  items_table[idx].mass = 0.080f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  // Bananas (bunch ~1.1kg)
  items_table[idx] = item_new_width(LoadTexture("res/bananas.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.10f);
  items_table[idx].mass = 1.100f;
  item_sections[idx] = OVOCE_ZELENINA;
  idx++;

  return idx;
}

static int fill_items_table_bakery(int idx) {
  // Chléb Šumava (loaf 900g)
  items_table[idx] = item_new_width(LoadTexture("res/chleba.png"),
                                    (Vector2){}, 0, 0.250f, 0.52f, 0.11f);
  items_table[idx].mass = 0.900f;
  item_sections[idx] = PECIVO;
  idx++;

  // Pletená houska (60g)
  items_table[idx] = item_new_width(LoadTexture("res/houska.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.060f;
  item_sections[idx] = PECIVO;
  idx++;

  // Standardní rohlík (43g)
  items_table[idx] = item_new_width(LoadTexture("res/rohlik.png"),
                                    (Vector2){}, 0, 0.170f, 0.45f, 0.06f);
  items_table[idx].mass = 0.043f;
  item_sections[idx] = PECIVO;
  idx++;

  // Birthday Cake (~1.2kg)
  items_table[idx] = item_new_width(LoadTexture("res/cake.png"),
                                    (Vector2){}, 0, 0.240f, 0.52f, 0.11f);
  items_table[idx].mass = 1.200f;
  item_sections[idx] = PECIVO;
  idx++;

  // Butter Croissant (60g)
  items_table[idx] = item_new_width(LoadTexture("res/croissant.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.060f;
  item_sections[idx] = PECIVO;
  idx++;

  // Macarons box (100g)
  items_table[idx] = item_new_width(LoadTexture("res/macarons.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = PECIVO;
  idx++;

  // Sandwich (180g)
  items_table[idx] = item_new_width(LoadTexture("res/sandwitch.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.180f;
  item_sections[idx] = PECIVO;
  idx++;

  // Toast Bread pack (500g)
  items_table[idx] = item_new_width(LoadTexture("res/toast.png"),
                                    (Vector2){}, 0, 0.200f, 0.48f, 0.09f);
  items_table[idx].mass = 0.500f;
  item_sections[idx] = PECIVO;
  idx++;

  // Kobliha marmeládová (65g)
  items_table[idx] = item_new_width(LoadTexture("res/kobliha.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.065f;
  item_sections[idx] = PECIVO;
  idx++;

  // Makový loupák (55g)
  items_table[idx] = item_new_width(LoadTexture("res/loupak.png"),
                                    (Vector2){}, 0, 0.150f, 0.45f, 0.06f);
  items_table[idx].mass = 0.055f;
  item_sections[idx] = PECIVO;
  idx++;

  // Skořicový šnek (85g)
  items_table[idx] = item_new_width(LoadTexture("res/snek.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.085f;
  item_sections[idx] = PECIVO;
  idx++;

  // Sýrový rohlík (70g)
  items_table[idx] = item_new_width(LoadTexture("res/syrovy_rohlik.png"),
                                    (Vector2){}, 0, 0.180f, 0.46f, 0.06f);
  items_table[idx].mass = 0.070f;
  item_sections[idx] = PECIVO;
  idx++;

  // Vánočka s mandlemi (400g)
  items_table[idx] = item_new_width(LoadTexture("res/vanocka.png"),
                                    (Vector2){}, 0, 0.260f, 0.52f, 0.11f);
  items_table[idx].mass = 0.400f;
  item_sections[idx] = PECIVO;
  idx++;

  // Zbojnická placka (110g)
  items_table[idx] = item_new_width(LoadTexture("res/zbojnicka_placka.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.110f;
  item_sections[idx] = PECIVO;
  idx++;

  // Svatební / moravský koláček (50g)
  items_table[idx] = item_new_width(LoadTexture("res/kolacek.png"),
                                    (Vector2){}, 0, 0.100f, 0.42f, 0.05f);
  items_table[idx].mass = 0.050f;
  item_sections[idx] = PECIVO;
  idx++;

  // Bábovka mramorová (400g)
  items_table[idx] = item_new_width(LoadTexture("res/babovka.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.10f);
  items_table[idx].mass = 0.400f;
  item_sections[idx] = PECIVO;
  idx++;

  // Jablečný štrúdl (350g)
  items_table[idx] = item_new_width(LoadTexture("res/strudl.png"),
                                    (Vector2){}, 0, 0.240f, 0.50f, 0.10f);
  items_table[idx].mass = 0.350f;
  item_sections[idx] = PECIVO;
  idx++;

  // Francouzská bageta (120g)
  items_table[idx] = item_new_width(LoadTexture("res/bageta.png"),
                                    (Vector2){}, 0, 0.260f, 0.52f, 0.08f);
  items_table[idx].mass = 0.120f;
  item_sections[idx] = PECIVO;
  idx++;

  // Pletýnka se sádlem (80g)
  items_table[idx] = item_new_width(LoadTexture("res/pletynka.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.080f;
  item_sections[idx] = PECIVO;
  idx++;

  // Kaiserka natural (60g)
  items_table[idx] = item_new_width(LoadTexture("res/kaiserka.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.060f;
  item_sections[idx] = PECIVO;
  idx++;

  // Tradiční kvasový chléb (800g)
  items_table[idx] = item_new_width(LoadTexture("res/kvasovy_chleb.png"),
                                    (Vector2){}, 0, 0.250f, 0.52f, 0.11f);
  items_table[idx].mass = 0.800f;
  item_sections[idx] = PECIVO;
  idx++;

  // Moravský koláč borůvkový (130g)
  items_table[idx] = item_new_width(LoadTexture("res/moravsky_kolac.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.130f;
  item_sections[idx] = PECIVO;
  idx++;

  // Medový perník (60g)
  items_table[idx] = item_new_width(LoadTexture("res/pernik.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.060f;
  item_sections[idx] = PECIVO;
  idx++;

  // Karamelový větrník (150g)
  items_table[idx] = item_new_width(LoadTexture("res/vetrnik.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.150f;
  item_sections[idx] = PECIVO;
  idx++;

  // Likérová špička (80g)
  items_table[idx] = item_new_width(LoadTexture("res/spicka.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.080f;
  item_sections[idx] = PECIVO;
  idx++;

  // Rakevička se šlehačkou (50g)
  items_table[idx] = item_new_width(LoadTexture("res/rakevicka.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.05f);
  items_table[idx].mass = 0.050f;
  item_sections[idx] = PECIVO;
  idx++;

  // Linecká kolečka (80g)
  items_table[idx] = item_new_width(LoadTexture("res/linecke.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.080f;
  item_sections[idx] = PECIVO;
  idx++;

  // Česneková bageta s máslem (170g)
  items_table[idx] = item_new_width(LoadTexture("res/cesnekova_bageta.png"),
                                    (Vector2){}, 0, 0.240f, 0.50f, 0.08f);
  items_table[idx].mass = 0.170f;
  item_sections[idx] = PECIVO;
  idx++;

  return idx;
}

static int fill_items_table_meat(int idx) {
  // Buřt / špekáček kus (~100g)
  items_table[idx] = item_new_width(LoadTexture("res/burt.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Celé chlazené kuře (~1.5kg)
  items_table[idx] = item_new_width(LoadTexture("res/chicken.png"),
                                    (Vector2){}, 0, 0.240f, 0.52f, 0.11f);
  items_table[idx].mass = 1.500f;
  item_sections[idx] = MASO;
  idx++;

  // Vepřová pečeně / maso (~800g)
  items_table[idx] = item_new_width(LoadTexture("res/meat.png"),
                                    (Vector2){}, 0, 0.200f, 0.48f, 0.09f);
  items_table[idx].mass = 0.800f;
  item_sections[idx] = MASO;
  idx++;

  // Jehněčí maso (~600g)
  items_table[idx] = item_new_width(LoadTexture("res/jehneci.png"),
                                    (Vector2){}, 0, 0.180f, 0.48f, 0.08f);
  items_table[idx].mass = 0.600f;
  item_sections[idx] = MASO;
  idx++;

  // Kuřecí prsní řízky balení (600g)
  items_table[idx] = item_new_width(LoadTexture("res/kureci_rizky.png"),
                                    (Vector2){}, 0, 0.200f, 0.48f, 0.09f);
  items_table[idx].mass = 0.600f;
  item_sections[idx] = MASO;
  idx++;

  // Vídeňské párky balení (250g)
  items_table[idx] = item_new_width(LoadTexture("res/parky.png"),
                                    (Vector2){}, 0, 0.180f, 0.46f, 0.07f);
  items_table[idx].mass = 0.250f;
  item_sections[idx] = MASO;
  idx++;

  // Hovězí steak (300g)
  items_table[idx] = item_new_width(LoadTexture("res/steak.png"),
                                    (Vector2){}, 0, 0.160f, 0.46f, 0.07f);
  items_table[idx].mass = 0.300f;
  item_sections[idx] = MASO;
  idx++;

  // Hovězí zadní balení (800g)
  items_table[idx] = item_new_width(LoadTexture("res/hovezi.png"),
                                    (Vector2){}, 0, 0.200f, 0.48f, 0.09f);
  items_table[idx].mass = 0.800f;
  item_sections[idx] = MASO;
  idx++;

  // Špekáčky vázané balení (600g)
  items_table[idx] = item_new_width(LoadTexture("res/spekacky.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.10f);
  items_table[idx].mass = 0.600f;
  item_sections[idx] = MASO;
  idx++;

  // Vysočina krájená (100g)
  items_table[idx] = item_new_width(LoadTexture("res/vysocina.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Poličan krájený (100g)
  items_table[idx] = item_new_width(LoadTexture("res/polican.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Herkules krájený (100g)
  items_table[idx] = item_new_width(LoadTexture("res/herkules.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Lovecký salám krájený (100g)
  items_table[idx] = item_new_width(LoadTexture("res/lovecky_salam.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Uherský salám krájený (100g)
  items_table[idx] = item_new_width(LoadTexture("res/uherak.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Gothajský salám (200g)
  items_table[idx] = item_new_width(LoadTexture("res/gothaj.png"),
                                    (Vector2){}, 0, 0.160f, 0.46f, 0.07f);
  items_table[idx].mass = 0.200f;
  item_sections[idx] = MASO;
  idx++;

  // Dušená šunka 95% (100g)
  items_table[idx] = item_new_width(LoadTexture("res/sunka_dusena.png"),
                                    (Vector2){}, 0, 0.160f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Pražská šunka (100g)
  items_table[idx] = item_new_width(LoadTexture("res/prazska_sunka.png"),
                                    (Vector2){}, 0, 0.160f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Anglická slanina (100g)
  items_table[idx] = item_new_width(LoadTexture("res/anglicka_slanina.png"),
                                    (Vector2){}, 0, 0.160f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MASO;
  idx++;

  // Moravské uzené maso (300g)
  items_table[idx] = item_new_width(LoadTexture("res/uzene_maso.png"),
                                    (Vector2){}, 0, 0.180f, 0.48f, 0.08f);
  items_table[idx].mass = 0.300f;
  item_sections[idx] = MASO;
  idx++;

  // Papriková klobása (200g)
  items_table[idx] = item_new_width(LoadTexture("res/klobasa.png"),
                                    (Vector2){}, 0, 0.180f, 0.46f, 0.07f);
  items_table[idx].mass = 0.200f;
  item_sections[idx] = MASO;
  idx++;

  // Pečená sekaná (500g)
  items_table[idx] = item_new_width(LoadTexture("res/sekana.png"),
                                    (Vector2){}, 0, 0.180f, 0.48f, 0.08f);
  items_table[idx].mass = 0.500f;
  item_sections[idx] = MASO;
  idx++;

  // Tlačenka světlá plátky (200g)
  items_table[idx] = item_new_width(LoadTexture("res/tlacenka.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.200f;
  item_sections[idx] = MASO;
  idx++;

  // Jitrnice zabijačková (220g)
  items_table[idx] = item_new_width(LoadTexture("res/jitrnice.png"),
                                    (Vector2){}, 0, 0.180f, 0.46f, 0.07f);
  items_table[idx].mass = 0.220f;
  item_sections[idx] = MASO;
  idx++;

  // Jelito zabijačkové (220g)
  items_table[idx] = item_new_width(LoadTexture("res/jelito.png"),
                                    (Vector2){}, 0, 0.180f, 0.46f, 0.07f);
  items_table[idx].mass = 0.220f;
  item_sections[idx] = MASO;
  idx++;

  // Mleté maso mix (500g)
  items_table[idx] = item_new_width(LoadTexture("res/mlete_maso.png"),
                                    (Vector2){}, 0, 0.180f, 0.48f, 0.08f);
  items_table[idx].mass = 0.500f;
  item_sections[idx] = MASO;
  idx++;

  // Hamé Májka paštika (75g)
  items_table[idx] = item_new_width(LoadTexture("res/pastika.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.080f;
  item_sections[idx] = MASO;
  idx++;

  // Český kapr čerstvý filet (500g)
  items_table[idx] = item_new_width(LoadTexture("res/kapr.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.09f);
  items_table[idx].mass = 0.500f;
  item_sections[idx] = MASO;
  idx++;

  // Norský losos filet (350g)
  items_table[idx] = item_new_width(LoadTexture("res/losos.png"),
                                    (Vector2){}, 0, 0.220f, 0.50f, 0.08f);
  items_table[idx].mass = 0.350f;
  item_sections[idx] = MASO;
  idx++;

  return idx;
}

static int fill_items_table_milk(int idx) {
  // Mléko čerstvé 1L (1.03kg)
  items_table[idx] = item_new_width(LoadTexture("res/milk.png"),
                                    (Vector2){}, 0, 0.140f, 0.48f, 0.07f);
  items_table[idx].mass = 1.030f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Máslo Jihočeské kostka (250g)
  items_table[idx] = item_new_width(LoadTexture("res/butter.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.250f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Tvrdý sýr blok (300g)
  items_table[idx] = item_new_width(LoadTexture("res/cheese.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.300f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Hollandia Selský bílý jogurt (500g)
  items_table[idx] = item_new_width(LoadTexture("res/jogurt_bily.png"),
                                    (Vector2){}, 0, 0.140f, 0.46f, 0.07f);
  items_table[idx].mass = 0.520f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Florian jahodový jogurt (150g)
  items_table[idx] = item_new_width(LoadTexture("res/jogurt_jahodovy.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.160f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Choceňský tvaroh měkký (250g)
  items_table[idx] = item_new_width(LoadTexture("res/tvaroh.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.260f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Pribináček vanilka (125g)
  items_table[idx] = item_new_width(LoadTexture("res/pribinacek.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.130f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Termix kakao (90g)
  items_table[idx] = item_new_width(LoadTexture("res/termix.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.095f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Sedlčanský Hermelín (100g)
  items_table[idx] = item_new_width(LoadTexture("res/hermelin.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Olomoucké tvarůžky (100g)
  items_table[idx] = item_new_width(LoadTexture("res/olomoucke_tvaruzky.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Eidam 30% plátky (100g)
  items_table[idx] = item_new_width(LoadTexture("res/eidam.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Jihočeská Niva (110g)
  items_table[idx] = item_new_width(LoadTexture("res/niva.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.110f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Lučina čistá (100g)
  items_table[idx] = item_new_width(LoadTexture("res/lucina.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Korbáčiky pařené (80g)
  items_table[idx] = item_new_width(LoadTexture("res/korbaciky.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.080f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Kefírové mléko (500g)
  items_table[idx] = item_new_width(LoadTexture("res/kefir.png"),
                                    (Vector2){}, 0, 0.130f, 0.46f, 0.06f);
  items_table[idx].mass = 0.520f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Acidofilní mléko (950g)
  items_table[idx] = item_new_width(LoadTexture("res/acidofilni_mleko.png"),
                                    (Vector2){}, 0, 0.140f, 0.48f, 0.07f);
  items_table[idx].mass = 0.980f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Zakysaná smetana 16% (180g)
  items_table[idx] = item_new_width(LoadTexture("res/zakysana_smetana.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.190f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Smetana ke šlehání 33% (250ml)
  items_table[idx] = item_new_width(LoadTexture("res/slehacka.png"),
                                    (Vector2){}, 0, 0.120f, 0.45f, 0.06f);
  items_table[idx].mass = 0.260f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Podmáslí kysané (500ml)
  items_table[idx] = item_new_width(LoadTexture("res/podmasli.png"),
                                    (Vector2){}, 0, 0.130f, 0.46f, 0.06f);
  items_table[idx].mass = 0.520f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Cottage sýr bílý (150g)
  items_table[idx] = item_new_width(LoadTexture("res/cottage.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.160f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Smetanito tavený sýr (140g)
  items_table[idx] = item_new_width(LoadTexture("res/taveny_syr.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.150f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Liptovská bryndza (125g)
  items_table[idx] = item_new_width(LoadTexture("res/bryndza.png"),
                                    (Vector2){}, 0, 0.120f, 0.44f, 0.06f);
  items_table[idx].mass = 0.130f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Lipánek Maxi (130g)
  items_table[idx] = item_new_width(LoadTexture("res/lipanek.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.135f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Balkánský sýr Žirovnice (180g)
  items_table[idx] = item_new_width(LoadTexture("res/balkansky_syr.png"),
                                    (Vector2){}, 0, 0.140f, 0.45f, 0.06f);
  items_table[idx].mass = 0.190f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Mozzarella v nálevu (125g/220g)
  items_table[idx] = item_new_width(LoadTexture("res/mozzarella.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.220f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Gouda 48% plátky (100g)
  items_table[idx] = item_new_width(LoadTexture("res/gouda.png"),
                                    (Vector2){}, 0, 0.150f, 0.46f, 0.07f);
  items_table[idx].mass = 0.100f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Bobík Maxi vanilka (70g)
  items_table[idx] = item_new_width(LoadTexture("res/bobik.png"),
                                    (Vector2){}, 0, 0.110f, 0.44f, 0.05f);
  items_table[idx].mass = 0.075f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  // Parenica uzená (110g)
  items_table[idx] = item_new_width(LoadTexture("res/parenica.png"),
                                    (Vector2){}, 0, 0.130f, 0.45f, 0.06f);
  items_table[idx].mass = 0.110f;
  item_sections[idx] = MLECNE_VYROBKY;
  idx++;

  return idx;
}

static uint32_t s_mall_rng = 123456789U;

void mall_seed_prng(uint32_t seed) {
  s_mall_rng = seed ? seed : 123456789U;
}

uint32_t mall_rand_u32(void) {
  s_mall_rng = s_mall_rng * 1664525U + 1013904223U;
  return s_mall_rng;
}

float mall_rand_float(void) {
  return (float)(mall_rand_u32() & 0x00FFFFFF) / (float)0x01000000;
}

void fill_items_table(void) {
  static bool s_loaded = false;
  static Item s_base_items[NUM_ITEMS];
  static int s_base_sections[NUM_ITEMS];

  if (!s_loaded) {
    int idx = 0;
    idx = fill_items_table_fruit(idx);
    idx = fill_items_table_bakery(idx);
    idx = fill_items_table_meat(idx);
    idx = fill_items_table_milk(idx);

    for (int i = 0; i < NUM_ITEMS; i++) {
      s_base_items[i] = items_table[i];
      s_base_sections[i] = item_sections[i];
    }
    s_loaded = true;
  } else {
    for (int i = 0; i < NUM_ITEMS; i++) {
      items_table[i] = s_base_items[i];
      item_sections[i] = s_base_sections[i];
    }
  }

  for (int i = 0; i < NUM_ITEMS - 1; i++) {
    size_t j = i + (mall_rand_u32() % (NUM_ITEMS - i));
    Item t = items_table[j];
    items_table[j] = items_table[i];
    items_table[i] = t;
    int s = item_sections[j];
    item_sections[j] = item_sections[i];
    item_sections[i] = s;
  }
}

void fill_shopping_list(void) {
  int target_len = SHOPPING_LIST_MAX_LEN;
  if (used_items_len < target_len)
    target_len = used_items_len;
  shopping_list_len = target_len;

  for (int i = 0; i < target_len; i++) {
  loopstart:;
    int num = used_items_len > 0
                  ? (int)(mall_rand_u32() % (uint32_t)used_items_len)
                  : 0;
    int item_idx = used_items[num];
    for (int j = 0; j < i; j++)
      if (shopping_list[j] == item_idx)
        goto loopstart;
    shopping_list[i] = item_idx;
  }
}

bool shopping_list_consume(ShoppingList *slist, Item item) {
  if (!slist)
    return false;

  int slist_idx = -1;
  for (int i = 0; i < slist->len; i++) {
    if (items_table[slist->items[i]].image.id == item.image.id) {
      slist_idx = i;
      break;
    }
  }
  if (slist_idx == -1)
    return false;

  slist->len--;
  for (int i = slist_idx; i < slist->len; i++)
    slist->items[i] = slist->items[i + 1];

  return true;
}
