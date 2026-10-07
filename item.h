#ifndef ITEM_H
#define ITEM_H

#include "geometry.h"
#include <raylib.h>

#define MASO 1
#define PECIVO 2
#define MLECNE_VYROBKY 3
#define OVOCE_ZELENINA 4
#define MALL_SECTIONS 4

#define NUM_ITEMS 112
#define SHOPPING_LIST_MAX_LEN 10

typedef struct {
  Texture2D image;
  Vector2 pos;
  float rot;
  float scale;
  float collision_radius;
  float radius;
  float mass;
} Item;

typedef struct {
  Item *items;
  int len;
  int cap;
} ItemArr;

extern Item items_table[NUM_ITEMS];
extern int item_sections[NUM_ITEMS];
extern int used_items[NUM_ITEMS];
extern int used_items_len;
extern int shopping_list[SHOPPING_LIST_MAX_LEN];
extern int shopping_list_len;

Item item_new(Texture2D image, Vector2 pos, float rot, float scale,
              float col_rad, float rad);
void item_set_scale_from_width(Item *item, float n_width);
Item item_new_width(Texture2D image, Vector2 pos, float rot, float new_width,
                    float col_rad, float rad);
void item_free(Item item);
void item_draw(Item item, Vector2 offset, float rot);
ColliPoly item_get_world_collider(Item item);

ItemArr itemarr_new(void);
void itemarr_free_all(ItemArr *crl);
void itemarr_draw(ItemArr crl, Vector2 offset, float rot, Vector2 topleft,
                  Vector2 botright);
void itemarr_push(ItemArr *crl, Item cr);
void itemarr_remove(ItemArr *crl, int index);
void itemarr_remove_free(ItemArr *crl, int index);

void fill_items_table(void);
void fill_shopping_list(void);
bool shopping_list_consume(Item item);

#endif // ITEM_H
