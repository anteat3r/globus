#include "wall.h"
#include <math.h>
#include <stdlib.h>

void wall_calculate_max_dist(Wall *wall) {
  if (wall->collider.len < 1) {
    wall->max_dist = 0.0f;
    return;
  }
  wall->max_dist = 0.0f;
  Vector2 first_vrt = wall->collider.items[0];
  for (int i = 0; i < wall->collider.len; i++) {
    float dist = Vector2Distance(first_vrt, wall->collider.items[i]);
    if (dist > wall->max_dist)
      wall->max_dist = dist;
  }
}

Wall wall_new_va(Color color, int num_vertices, va_list args) {
  Wall res;
  res.color = color;
  res.collider.len = num_vertices;
  res.collider.items = malloc(num_vertices * sizeof(Vector2));
  if (!res.collider.items)
    perror_exit("wall_new malloc");

  for (int i = 0; i < num_vertices; i++)
    res.collider.items[i] = va_arg(args, Vector2);

  wall_calculate_max_dist(&res);
  return res;
}

Wall wall_new_ptr(Color color, int num_vertices, Vector2 *items) {
  Wall res;
  res.color = color;
  res.collider.len = num_vertices;
  res.collider.items = malloc(num_vertices * sizeof(Vector2));
  if (!res.collider.items)
    perror_exit("wall_new malloc");

  for (int i = 0; i < num_vertices; i++)
    res.collider.items[i] = items[i];

  wall_calculate_max_dist(&res);
  return res;
}

Wall wall_new_rect(Color color, Vector2 corner_a, Vector2 corner_b) {
  Wall res;
  res.color = color;
  res.collider.len = 4;
  res.collider.items = malloc(4 * sizeof(Vector2));
  if (!res.collider.items)
    perror_exit("wall_new malloc");

  Vector2 topleft = {
      fminf(corner_a.x, corner_b.x),
      fminf(corner_a.y, corner_b.y),
  };
  Vector2 botright = {
      fmaxf(corner_a.x, corner_b.x),
      fmaxf(corner_a.y, corner_b.y),
  };

  res.collider.items[0] = topleft;
  res.collider.items[1] = (Vector2){topleft.x, botright.y};
  res.collider.items[2] = botright;
  res.collider.items[3] = (Vector2){botright.x, topleft.y};

  wall_calculate_max_dist(&res);
  return res;
}

Wall wall_new(Color color, int num_vertices, ...) {
  va_list args;
  va_start(args, num_vertices);
  Wall res = wall_new_va(color, num_vertices, args);
  va_end(args);
  return res;
}

void wall_free(Wall wall) {
  if (wall.collider.items) {
    free(wall.collider.items);
  }
}

void wall_draw(Wall wall) {
  DrawTriangleFan(wall.collider.items, wall.collider.len, wall.color);
}

WallArr wallarr_new(void) {
  WallArr res = {NULL, 0, 0};
  return res;
}

void wallarr_free_all(WallArr *crl) {
  for (int i = 0; i < crl->len; i++) {
    wall_free(crl->items[i]);
  }
  free(crl->items);
  crl->items = NULL;
  crl->len = 0;
  crl->cap = 0;
}

void wallarr_draw(WallArr crl, Vector2 topleft, Vector2 botright) {
  (void)topleft;
  (void)botright;
  for (int i = 0; i < crl.len; i++) {
    wall_draw(crl.items[i]);
  }
}

void wallarr_push(WallArr *crl, Wall cr) {
  if (!crl->items) {
    crl->len = 0;
    crl->cap = 10;
    crl->items = malloc(crl->cap * sizeof(Wall));
  } else if (crl->len >= crl->cap) {
    crl->cap *= 2;
    crl->items = realloc(crl->items, crl->cap * sizeof(Wall));
  }
  crl->items[crl->len] = cr;
  crl->len++;
}

void wallarr_push_malloc(WallArr *crl, Color color, int num, ...) {
  va_list args;
  va_start(args, num);
  wallarr_push(crl, wall_new_va(color, num, args));
  va_end(args);
}

void wallarr_push_malloc_rect(WallArr *crl, Color color, Vector2 topleft,
                              Vector2 botright) {
  wallarr_push(crl, wall_new_rect(color, topleft, botright));
}

void wallarr_remove(WallArr *crl, int index) {
  if (index >= crl->len)
    return;
  crl->len--;
  for (int i = index; i < crl->len; i++)
    crl->items[i] = crl->items[i + 1];
}

void wallarr_remove_free(WallArr *crl, int index) {
  if (index >= crl->len)
    return;
  wall_free(crl->items[index]);
  crl->len--;
  for (int i = index; i < crl->len; i++)
    crl->items[i] = crl->items[i + 1];
}
