#ifndef MALL_H
#define MALL_H

#include "item.h"
#include "wall.h"
#include <raylib.h>

#define MALL_WIDTH 64
#define MALL_HEIGHT 64

#define NUM_COLORS 4
extern Color mall_colors[NUM_COLORS];

typedef struct {
  Vector2 pos;
  int item_idx;
  int section;
} IslandPoster;

typedef struct {
  IslandPoster *items;
  int len;
  int cap;
} IslandPosterArr;

typedef struct {
  Rectangle rect;
  Color color;
} IslandShelfRect;

typedef struct {
  Vector2 start;
  Vector2 end;
  Color color;
} IslandShelfLine;

typedef struct {
  IslandShelfRect *rects;
  int rect_count;
  int rect_cap;

  IslandShelfLine *lines;
  int line_count;
  int line_cap;

  int len;
} IslandShelfArr;

Color GetRandomColor(void);
IslandPosterArr island_posterarr_new(void);
void island_posterarr_free(IslandPosterArr *posters);
void island_posterarr_push(IslandPosterArr *posters, IslandPoster poster);
void island_posterarr_draw(IslandPosterArr posters, Vector2 topleft,
                           Vector2 botright);

IslandShelfArr island_shelfarr_new(void);
void island_shelfarr_free(IslandShelfArr *shelves);
void island_shelfarr_push_rect(IslandShelfArr *shelves, Rectangle rect,
                               Color color);
void island_shelfarr_push_line(IslandShelfArr *shelves, Vector2 start,
                               Vector2 end, Color color);
void island_shelfarr_draw(IslandShelfArr shelves, Vector2 topleft,
                          Vector2 botright);

#define MALL_GRID_MASK_SIZE (MALL_WIDTH * MALL_HEIGHT / 8)

void mall_pack_grid(const int grid[MALL_HEIGHT][MALL_WIDTH], uint8_t *mask);
void mall_unpack_grid(const uint8_t *mask, int grid[MALL_HEIGHT][MALL_WIDTH]);

void mall_generation(const char *filename, WallArr *walls, ItemArr *items,
                     IslandPosterArr *posters, IslandShelfArr *shelves,
                     unsigned int seed, const uint8_t *in_grid_mask,
                     uint8_t *out_grid_mask);

#endif // MALL_H
