#include "mall.h"
#include "geometry.h"
#include "wfc.h"
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Color mall_colors[NUM_COLORS] = {
    MAROON,
    ORANGE,
    DARKBLUE,
    DARKGREEN,
};

Color GetRandomColor(void) {
  return mall_colors[GetRandomValue(0, NUM_COLORS - 1)];
}

typedef struct {
  IntVector2 *tiles;
  int tile_count;
  float center_x;
  float center_y;
  int section;
} ShelfIsland;

static inline Color isle_dim_color(Color c) {
  return (Color){
      (unsigned char)(c.r * 0.65f),
      (unsigned char)(c.g * 0.65f),
      (unsigned char)(c.b * 0.65f),
      255,
  };
}

void mall_pack_grid(const int grid[MALL_HEIGHT][MALL_WIDTH], uint8_t *mask) {
  memset(mask, 0, MALL_GRID_MASK_SIZE);
  for (int y = 0; y < MALL_HEIGHT; y++) {
    for (int x = 0; x < MALL_WIDTH; x++) {
      if (grid[y][x]) {
        int bit = y * MALL_WIDTH + x;
        mask[bit / 8] |= (uint8_t)(1 << (bit % 8));
      }
    }
  }
}

void mall_unpack_grid(const uint8_t *mask, int grid[MALL_HEIGHT][MALL_WIDTH]) {
  for (int y = 0; y < MALL_HEIGHT; y++) {
    for (int x = 0; x < MALL_WIDTH; x++) {
      int bit = y * MALL_WIDTH + x;
      grid[y][x] = (mask[bit / 8] & (1 << (bit % 8))) ? 1 : 0;
    }
  }
}

void mall_generation(const char *filename, WallArr *walls, ItemArr *items,
                     IslandPosterArr *posters, IslandShelfArr *shelves,
                     unsigned int seed, const uint8_t *in_grid_mask,
                     uint8_t *out_grid_mask) {
  mall_seed_prng(seed);

  int grid[MALL_HEIGHT][MALL_WIDTH];

  if (in_grid_mask != NULL) {
    // Client path: unpack the authoritative grid sent by host!
    mall_unpack_grid(in_grid_mask, grid);
  } else {
    // Host / Singleplayer path: run WFC
    Image wfc_image = LoadImage(filename);

    Color *wfc_image_colors = malloc(wfc_image.width * wfc_image.height * 4);
    if (!wfc_image_colors)
      perror_exit("wfc_image_colors malloc");

    for (int y = 0; y < wfc_image.height; y++) {
      for (int x = 0; x < wfc_image.width; x++) {
        int idx = y * wfc_image.width + x;
        wfc_image_colors[idx] = GetImageColor(wfc_image, x, y);
      }
    }

    struct wfc_image image = {
        .data = (unsigned char *)wfc_image_colors,
        .width = wfc_image.width,
        .height = wfc_image.height,
        .component_cnt = 4,
    };
    struct wfc *wfc =
        wfc_overlapping(MALL_WIDTH, MALL_HEIGHT, &image, 3, 3, 1, 1, 1, 1);
    if (!wfc)
      perror_exit("wfc_overlapping");

    wfc_init_seed(wfc, seed);
    while (!wfc_run(wfc, -1)) {
      wfc_init(wfc);
    }

    struct wfc_image *res_plan = wfc_output_image(wfc);

    for (int y = 0; y < MALL_HEIGHT; y++) {
      for (int x = 0; x < MALL_WIDTH; x++) {
        int idx = y * MALL_WIDTH + x;
        grid[y][x] = (res_plan->data[idx * 4] != 0) ? 1 : 0;
      }
    }

    // Ensure a 3x3 open aisle area around the player starting position (10, 10)
    for (int dy = -1; dy <= 1; dy++) {
      for (int dx = -1; dx <= 1; dx++) {
        int sx = 10 + dx;
        int sy = 10 + dy;
        if (sx >= 0 && sx < MALL_WIDTH && sy >= 0 && sy < MALL_HEIGHT) {
          grid[sy][sx] = 0;
        }
      }
    }

    if (out_grid_mask != NULL) {
      mall_pack_grid(grid, out_grid_mask);
    }

    free(wfc_image_colors);
    UnloadImage(wfc_image);
    wfc_destroy(wfc);
    wfc_img_destroy(res_plan);
  }

  // Deterministically re-seed before building shelves and items
  mall_seed_prng(seed);

  // Find all shelf islands using BFS
  bool visited[MALL_HEIGHT][MALL_WIDTH] = {0};
  ShelfIsland *islands = malloc(MALL_WIDTH * MALL_HEIGHT * sizeof(ShelfIsland));
  IntVector2 *tile_pool = malloc(MALL_WIDTH * MALL_HEIGHT * sizeof(IntVector2));
  if (!islands || !tile_pool)
    perror_exit("islands malloc");

  int tile_pool_idx = 0;
  int island_count = 0;

  for (int y = 0; y < MALL_HEIGHT; y++) {
    for (int x = 0; x < MALL_WIDTH; x++) {
      if (grid[y][x] == 1 && !visited[y][x]) {
        int start_pool_idx = tile_pool_idx;
        int qx[4096], qy[4096];
        int qh = 0, qt = 0;

        qx[qt] = x;
        qy[qt] = y;
        qt++;
        visited[y][x] = true;

        float sum_x = 0.0f, sum_y = 0.0f;
        while (qh < qt) {
          int cx = qx[qh];
          int cy = qy[qh];
          qh++;
          tile_pool[tile_pool_idx++] = (IntVector2){cx, cy};
          sum_x += cx;
          sum_y += cy;

          int dx[4] = {1, -1, 0, 0};
          int dy[4] = {0, 0, 1, -1};
          for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d];
            int ny = cy + dy[d];
            if (nx >= 0 && nx < MALL_WIDTH && ny >= 0 && ny < MALL_HEIGHT) {
              if (grid[ny][nx] == 1 && !visited[ny][nx]) {
                visited[ny][nx] = true;
                qx[qt] = nx;
                qy[qt] = ny;
                qt++;
              }
            }
          }
        }

        int count = tile_pool_idx - start_pool_idx;
        islands[island_count].tiles = &tile_pool[start_pool_idx];
        islands[island_count].tile_count = count;
        islands[island_count].center_x = sum_x / count;
        islands[island_count].center_y = sum_y / count;
        island_count++;
      }
    }
  }

  IntVector2 section_centers[MALL_SECTIONS] = {
      {0, 0},                           // Top-Left: MASO
      {MALL_WIDTH - 1, 0},              // Top-Right: PECIVO
      {0, MALL_HEIGHT - 1},             // Bottom-Left: MLECNE_VYROBKY
      {MALL_WIDTH - 1, MALL_HEIGHT - 1} // Bottom-Right: OVOCE_ZELENINA
  };

  // Group items by section
  int sec_item_indices[MALL_SECTIONS + 1][NUM_ITEMS];
  int sec_item_counts[MALL_SECTIONS + 1] = {0};

  for (int i = 0; i < NUM_ITEMS; i++) {
    int s = item_sections[i];
    if (s >= 1 && s <= MALL_SECTIONS) {
      sec_item_indices[s][sec_item_counts[s]++] = i;
    }
  }

  int sec_assigned_count[MALL_SECTIONS + 1] = {0};
  used_items_len = 0;

  for (int i = 0; i < island_count; i++) {
    float min_dist = FLT_MAX;
    int best_sec = 1;
    for (int s = 0; s < MALL_SECTIONS; s++) {
      float d = Vector2Distance(
          (Vector2){islands[i].center_x, islands[i].center_y},
          (Vector2){section_centers[s].x, section_centers[s].y});
      if (d < min_dist) {
        min_dist = d;
        best_sec = s + 1;
      }
    }
    islands[i].section = best_sec;

    // Create walls for every tile in this shelf island
    for (int t = 0; t < islands[i].tile_count; t++) {
      int tx = islands[i].tiles[t].x;
      int ty = islands[i].tiles[t].y;
      wallarr_push_malloc_rect(walls, mall_colors[best_sec - 1],
                               (Vector2){(float)tx, (float)ty},
                               (Vector2){(float)(tx + 1), (float)(ty + 1)});
    }

    // Assign item to this island
    int item_idx = -1;
    if (sec_item_counts[best_sec] > 0) {
      int offset = sec_assigned_count[best_sec] % sec_item_counts[best_sec];
      item_idx = sec_item_indices[best_sec][offset];
      sec_assigned_count[best_sec]++;

      bool already_used = false;
      for (int u = 0; u < used_items_len; u++) {
        if (used_items[u] == item_idx) {
          already_used = true;
          break;
        }
      }
      if (!already_used && used_items_len < NUM_ITEMS) {
        used_items[used_items_len++] = item_idx;
      }
    }

    if (item_idx < 0)
      continue;

    if (posters) {
      // Pick the tile closest to island centroid
      float best_dist = FLT_MAX;
      Vector2 poster_pos =
          (Vector2){islands[i].center_x + 0.5f, islands[i].center_y + 0.5f};
      for (int t = 0; t < islands[i].tile_count; t++) {
        Vector2 tile_center = (Vector2){(float)islands[i].tiles[t].x + 0.5f,
                                        (float)islands[i].tiles[t].y + 0.5f};
        float d = Vector2Distance(
            (Vector2){islands[i].center_x + 0.5f, islands[i].center_y + 0.5f},
            tile_center);
        if (d < best_dist) {
          best_dist = d;
          poster_pos = tile_center;
        }
      }

      island_posterarr_push(posters, (IslandPoster){
                                         .pos = poster_pos,
                                         .item_idx = item_idx,
                                         .section = best_sec,
                                     });
    }

    Item item = items_table[item_idx];

    const float shelf_depth = 0.22f;
    Color shelf_bg = isle_dim_color(mall_colors[best_sec - 1]);
    Color shelf_rail = (Color){100, 100, 110, 255};

    // Place shelves, connected outer/inner lines, and 3-5 items on all free edges
    for (int t = 0; t < islands[i].tile_count; t++) {
      int tx = islands[i].tiles[t].x;
      int ty = islands[i].tiles[t].y;

      bool top_open = (ty == 0 || grid[ty - 1][tx] == 0);
      bool bot_open = (ty == MALL_HEIGHT - 1 || grid[ty + 1][tx] == 0);
      bool left_open = (tx == 0 || grid[ty][tx - 1] == 0);
      bool right_open = (tx == MALL_WIDTH - 1 || grid[ty][tx + 1] == 0);

      // Top edge (-Y)
      if (top_open) {
        if (shelves) {
          island_shelfarr_push_rect(
              shelves, (Rectangle){(float)tx, (float)ty, 1.0f, shelf_depth},
              shelf_bg);
          island_shelfarr_push_line(
              shelves, (Vector2){(float)tx, (float)ty},
              (Vector2){(float)(tx + 1), (float)ty}, shelf_rail);

          float x_start = left_open ? ((float)tx + shelf_depth) : (float)tx;
          float x_end = right_open ? ((float)(tx + 1) - shelf_depth)
                                   : (float)(tx + 1);
          island_shelfarr_push_line(
              shelves, (Vector2){x_start, (float)ty + shelf_depth},
              (Vector2){x_end, (float)ty + shelf_depth}, shelf_rail);

          if (!left_open && (ty > 0 && tx > 0 && grid[ty - 1][tx - 1] == 1)) {
            island_shelfarr_push_line(
                shelves, (Vector2){(float)tx, (float)ty + shelf_depth},
                (Vector2){(float)tx, (float)ty}, shelf_rail);
          }
          if (!right_open &&
              (ty > 0 && tx < MALL_WIDTH - 1 && grid[ty - 1][tx + 1] == 1)) {
            island_shelfarr_push_line(
                shelves, (Vector2){(float)(tx + 1), (float)ty + shelf_depth},
                (Vector2){(float)(tx + 1), (float)ty}, shelf_rail);
          }
        }
        int count_edge = 3 + (mall_rand_u32() % 3);
        for (int k = 0; k < count_edge; k++) {
          float tk = ((float)k + 0.5f) / (float)count_edge;
          float jitter = (mall_rand_float() - 0.5f) * 0.05f;
          item.pos = (Vector2){
              (float)tx + 0.12f + 0.76f * tk + jitter,
              (float)ty + (shelf_depth * 0.5f),
          };
          item.rot = mall_rand_float() * 2.0f * PI;
          itemarr_push(items, item);
        }
      }

      // Bottom edge (+Y)
      if (bot_open) {
        if (shelves) {
          island_shelfarr_push_rect(
              shelves,
              (Rectangle){(float)tx, (float)(ty + 1) - shelf_depth, 1.0f,
                          shelf_depth},
              shelf_bg);
          island_shelfarr_push_line(
              shelves, (Vector2){(float)tx, (float)(ty + 1)},
              (Vector2){(float)(tx + 1), (float)(ty + 1)}, shelf_rail);

          float x_start = left_open ? ((float)tx + shelf_depth) : (float)tx;
          float x_end = right_open ? ((float)(tx + 1) - shelf_depth)
                                   : (float)(tx + 1);
          island_shelfarr_push_line(
              shelves, (Vector2){x_start, (float)(ty + 1) - shelf_depth},
              (Vector2){x_end, (float)(ty + 1) - shelf_depth}, shelf_rail);

          if (!left_open &&
              (ty < MALL_HEIGHT - 1 && tx > 0 && grid[ty + 1][tx - 1] == 1)) {
            island_shelfarr_push_line(
                shelves, (Vector2){(float)tx, (float)(ty + 1) - shelf_depth},
                (Vector2){(float)tx, (float)(ty + 1)}, shelf_rail);
          }
          if (!right_open && (ty < MALL_HEIGHT - 1 && tx < MALL_WIDTH - 1 &&
                              grid[ty + 1][tx + 1] == 1)) {
            island_shelfarr_push_line(
                shelves,
                (Vector2){(float)(tx + 1), (float)(ty + 1) - shelf_depth},
                (Vector2){(float)(tx + 1), (float)(ty + 1)}, shelf_rail);
          }
        }
        int count_edge = 3 + (mall_rand_u32() % 3);
        for (int k = 0; k < count_edge; k++) {
          float tk = ((float)k + 0.5f) / (float)count_edge;
          float jitter = (mall_rand_float() - 0.5f) * 0.05f;
          item.pos = (Vector2){
              (float)tx + 0.12f + 0.76f * tk + jitter,
              (float)(ty + 1) - (shelf_depth * 0.5f),
          };
          item.rot = mall_rand_float() * 2.0f * PI;
          itemarr_push(items, item);
        }
      }

      // Left edge (-X)
      if (left_open) {
        if (shelves) {
          island_shelfarr_push_rect(
              shelves, (Rectangle){(float)tx, (float)ty, shelf_depth, 1.0f},
              shelf_bg);
          island_shelfarr_push_line(
              shelves, (Vector2){(float)tx, (float)ty},
              (Vector2){(float)tx, (float)(ty + 1)}, shelf_rail);

          float y_start = top_open ? ((float)ty + shelf_depth) : (float)ty;
          float y_end = bot_open ? ((float)(ty + 1) - shelf_depth)
                                 : (float)(ty + 1);
          island_shelfarr_push_line(
              shelves, (Vector2){(float)tx + shelf_depth, y_start},
              (Vector2){(float)tx + shelf_depth, y_end}, shelf_rail);

          if (!top_open && (ty > 0 && tx > 0 && grid[ty - 1][tx - 1] == 1)) {
            island_shelfarr_push_line(
                shelves, (Vector2){(float)tx + shelf_depth, (float)ty},
                (Vector2){(float)tx, (float)ty}, shelf_rail);
          }
          if (!bot_open &&
              (ty < MALL_HEIGHT - 1 && tx > 0 && grid[ty + 1][tx - 1] == 1)) {
            island_shelfarr_push_line(
                shelves, (Vector2){(float)tx + shelf_depth, (float)(ty + 1)},
                (Vector2){(float)tx, (float)(ty + 1)}, shelf_rail);
          }
        }
        int count_edge = 3 + (mall_rand_u32() % 3);
        for (int k = 0; k < count_edge; k++) {
          float tk = ((float)k + 0.5f) / (float)count_edge;
          float jitter = (mall_rand_float() - 0.5f) * 0.05f;
          item.pos = (Vector2){
              (float)tx + (shelf_depth * 0.5f),
              (float)ty + 0.12f + 0.76f * tk + jitter,
          };
          item.rot = mall_rand_float() * 2.0f * PI;
          itemarr_push(items, item);
        }
      }

      // Right edge (+X)
      if (right_open) {
        if (shelves) {
          island_shelfarr_push_rect(
              shelves,
              (Rectangle){(float)(tx + 1) - shelf_depth, (float)ty,
                          shelf_depth, 1.0f},
              shelf_bg);
          island_shelfarr_push_line(
              shelves, (Vector2){(float)(tx + 1), (float)ty},
              (Vector2){(float)(tx + 1), (float)(ty + 1)}, shelf_rail);

          float y_start = top_open ? ((float)ty + shelf_depth) : (float)ty;
          float y_end = bot_open ? ((float)(ty + 1) - shelf_depth)
                                 : (float)(ty + 1);
          island_shelfarr_push_line(
              shelves, (Vector2){(float)(tx + 1) - shelf_depth, y_start},
              (Vector2){(float)(tx + 1) - shelf_depth, y_end}, shelf_rail);

          if (!top_open &&
              (ty > 0 && tx < MALL_WIDTH - 1 && grid[ty - 1][tx + 1] == 1)) {
            island_shelfarr_push_line(
                shelves,
                (Vector2){(float)(tx + 1) - shelf_depth, (float)ty},
                (Vector2){(float)(tx + 1), (float)ty}, shelf_rail);
          }
          if (!bot_open && (ty < MALL_HEIGHT - 1 && tx < MALL_WIDTH - 1 &&
                            grid[ty + 1][tx + 1] == 1)) {
            island_shelfarr_push_line(
                shelves,
                (Vector2){(float)(tx + 1) - shelf_depth, (float)(ty + 1)},
                (Vector2){(float)(tx + 1), (float)(ty + 1)}, shelf_rail);
          }
        }
        int count_edge = 3 + (mall_rand_u32() % 3);
        for (int k = 0; k < count_edge; k++) {
          float tk = ((float)k + 0.5f) / (float)count_edge;
          float jitter = (mall_rand_float() - 0.5f) * 0.05f;
          item.pos = (Vector2){
              (float)(tx + 1) - (shelf_depth * 0.5f),
              (float)ty + 0.12f + 0.76f * tk + jitter,
          };
          item.rot = mall_rand_float() * 2.0f * PI;
          itemarr_push(items, item);
        }
      }
    }
  }

  // Outer boundary walls
  wallarr_push_malloc_rect(walls, BLACK, (Vector2){-2, -2},
                           (Vector2){MALL_WIDTH + 2, -1});
  wallarr_push_malloc_rect(walls, BLACK, (Vector2){-2, -2},
                           (Vector2){-1, MALL_HEIGHT + 1});
  wallarr_push_malloc_rect(walls, BLACK, (Vector2){-2, MALL_HEIGHT + 1},
                           (Vector2){MALL_WIDTH + 2, MALL_HEIGHT + 2});
  wallarr_push_malloc_rect(walls, BLACK, (Vector2){MALL_WIDTH + 1, -2},
                           (Vector2){MALL_WIDTH + 2, MALL_HEIGHT + 2});

  free(islands);
  free(tile_pool);
}

IslandPosterArr island_posterarr_new(void) {
  return (IslandPosterArr){NULL, 0, 0};
}

void island_posterarr_free(IslandPosterArr *posters) {
  if (posters->items) {
    free(posters->items);
    posters->items = NULL;
  }
  posters->len = 0;
  posters->cap = 0;
}

void island_posterarr_push(IslandPosterArr *posters, IslandPoster poster) {
  if (!posters->items) {
    posters->len = 0;
    posters->cap = 16;
    posters->items = malloc(posters->cap * sizeof(IslandPoster));
    if (!posters->items)
      perror_exit("island_posterarr_push malloc");
  } else if (posters->len >= posters->cap) {
    posters->cap *= 2;
    posters->items =
        realloc(posters->items, posters->cap * sizeof(IslandPoster));
    if (!posters->items)
      perror_exit("island_posterarr_push realloc");
  }
  posters->items[posters->len++] = poster;
}

void island_posterarr_draw(IslandPosterArr posters, Vector2 topleft,
                           Vector2 botright) {
  float time = (float)GetTime();

  for (int i = 0; i < posters.len; i++) {
    IslandPoster p = posters.items[i];
    if (p.item_idx < 0 || p.item_idx >= NUM_ITEMS)
      continue;

    // Check if item is in the current shopping list
    bool is_wanted = false;
    for (int s = 0; s < shopping_list_len; s++) {
      if (shopping_list[s] == p.item_idx) {
        is_wanted = true;
        break;
      }
    }

    // Cartoonish pulsating square size if wanted on shopping list
    float side = is_wanted ? (0.86f + 0.04f * sinf(time * 6.0f)) : 0.80f;
    float half = side * 0.5f;

    // View frustum culling
    if (p.pos.x + half < topleft.x || p.pos.x - half > botright.x ||
        p.pos.y + half < topleft.y || p.pos.y - half > botright.y)
      continue;

    Color rim_color =
        is_wanted ? GOLD : mall_colors[(p.section - 1) % NUM_COLORS];
    Item item = items_table[p.item_idx];

    float max_dim = fmaxf((float)item.image.width, (float)item.image.height);
    if (max_dim <= 0.0f)
      continue;
    float scale = (side * 0.70f) / max_dim;
    Vector2 tex_pos = {
        p.pos.x - ((float)item.image.width * scale) * 0.5f,
        p.pos.y - ((float)item.image.height * scale) * 0.5f,
    };

    float border_thick = 0.05f;
    Rectangle outer_rec = {p.pos.x - half, p.pos.y - half, side, side};
    float inner_side = side - border_thick * 2.0f;
    Rectangle inner_rec = {p.pos.x - inner_side * 0.5f,
                           p.pos.y - inner_side * 0.5f, inner_side,
                           inner_side};

    // Exactly 3 simple Raylib primitives matching the cartoon stickman style:
    // 1. Outer rounded square rim (department color, or pulsating gold if wanted)
    DrawRectangleRounded(outer_rec, 0.22f, 4, rim_color);
    // 2. Inner clean white cartoon card
    DrawRectangleRounded(inner_rec, 0.20f, 4, WHITE);
    // 3. Upright large product icon
    DrawTextureEx(item.image, tex_pos, 0.0f, scale, WHITE);
  }
}

IslandShelfArr island_shelfarr_new(void) {
  return (IslandShelfArr){
      .rects = NULL,
      .rect_count = 0,
      .rect_cap = 0,
      .lines = NULL,
      .line_count = 0,
      .line_cap = 0,
      .len = 0,
  };
}

void island_shelfarr_free(IslandShelfArr *shelves) {
  if (shelves->rects) {
    free(shelves->rects);
    shelves->rects = NULL;
  }
  if (shelves->lines) {
    free(shelves->lines);
    shelves->lines = NULL;
  }
  shelves->rect_count = 0;
  shelves->rect_cap = 0;
  shelves->line_count = 0;
  shelves->line_cap = 0;
  shelves->len = 0;
}

void island_shelfarr_push_rect(IslandShelfArr *shelves, Rectangle rect,
                               Color color) {
  if (!shelves->rects) {
    shelves->rect_count = 0;
    shelves->rect_cap = 64;
    shelves->rects = malloc(shelves->rect_cap * sizeof(IslandShelfRect));
    if (!shelves->rects)
      perror_exit("island_shelfarr_push_rect malloc");
  } else if (shelves->rect_count >= shelves->rect_cap) {
    shelves->rect_cap *= 2;
    shelves->rects =
        realloc(shelves->rects, shelves->rect_cap * sizeof(IslandShelfRect));
    if (!shelves->rects)
      perror_exit("island_shelfarr_push_rect realloc");
  }
  shelves->rects[shelves->rect_count++] = (IslandShelfRect){rect, color};
  shelves->len = shelves->rect_count;
}

void island_shelfarr_push_line(IslandShelfArr *shelves, Vector2 start,
                               Vector2 end, Color color) {
  if (!shelves->lines) {
    shelves->line_count = 0;
    shelves->line_cap = 128;
    shelves->lines = malloc(shelves->line_cap * sizeof(IslandShelfLine));
    if (!shelves->lines)
      perror_exit("island_shelfarr_push_line malloc");
  } else if (shelves->line_count >= shelves->line_cap) {
    shelves->line_cap *= 2;
    shelves->lines =
        realloc(shelves->lines, shelves->line_cap * sizeof(IslandShelfLine));
    if (!shelves->lines)
      perror_exit("island_shelfarr_push_line realloc");
  }
  shelves->lines[shelves->line_count++] = (IslandShelfLine){start, end, color};
}

void island_shelfarr_draw(IslandShelfArr shelves, Vector2 topleft,
                          Vector2 botright) {
  const float thick = 0.04f;
  const float radius = thick * 0.5f;

  // 1. Draw shelf background rects (dim version of isle color)
  for (int i = 0; i < shelves.rect_count; i++) {
    IslandShelfRect r = shelves.rects[i];
    if (r.rect.x + r.rect.width < topleft.x || r.rect.x > botright.x ||
        r.rect.y + r.rect.height < topleft.y || r.rect.y > botright.y)
      continue;
    DrawRectangleRec(r.rect, r.color);
  }

  // 2. Draw connected outer lines, inner lines, end caps, and joint caps
  for (int i = 0; i < shelves.line_count; i++) {
    IslandShelfLine l = shelves.lines[i];
    float min_x = fminf(l.start.x, l.end.x);
    float max_x = fmaxf(l.start.x, l.end.x);
    float min_y = fminf(l.start.y, l.end.y);
    float max_y = fmaxf(l.start.y, l.end.y);
    if (max_x < topleft.x - thick || min_x > botright.x + thick ||
        max_y < topleft.y - thick || min_y > botright.y + thick)
      continue;

    DrawLineEx(l.start, l.end, thick, l.color);
    DrawCircleV(l.start, radius, l.color);
    DrawCircleV(l.end, radius, l.color);
  }
}
