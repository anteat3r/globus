#ifndef WALL_H
#define WALL_H

#include "geometry.h"
#include <raylib.h>
#include <stdarg.h>

typedef struct {
  ColliPoly collider;
  Color color;
  float max_dist;
} Wall;

void wall_calculate_max_dist(Wall *wall);
Wall wall_new_va(Color color, int num_vertices, va_list args);
Wall wall_new_ptr(Color color, int num_vertices, Vector2 *items);
Wall wall_new_rect(Color color, Vector2 corner_a, Vector2 corner_b);
Wall wall_new(Color color, int num_vertices, ...);
void wall_free(Wall wall);
void wall_draw(Wall wall);

typedef struct {
  Wall *items;
  int len;
  int cap;
} WallArr;

WallArr wallarr_new(void);
void wallarr_free_all(WallArr *crl);
void wallarr_draw(WallArr crl, Vector2 topleft, Vector2 botright);
void wallarr_push(WallArr *crl, Wall cr);
void wallarr_push_malloc(WallArr *crl, Color color, int num, ...);
void wallarr_push_malloc_rect(WallArr *crl, Color color, Vector2 topleft,
                              Vector2 botright);
void wallarr_remove(WallArr *crl, int index);
void wallarr_remove_free(WallArr *crl, int index);

#endif // WALL_H
