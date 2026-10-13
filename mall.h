#ifndef MALL_H
#define MALL_H

#include "item.h"
#include "wall.h"
#include <raylib.h>
#include <stdint.h>
#include <stdbool.h>

#define MALL_MIN_DIM 16
#define MALL_WIDTH 24
#define MALL_HEIGHT 24
#define MALL_MAX_WIDTH 36
#define MALL_MAX_HEIGHT 36
#define MALL_MAX_GRID_MASK_SIZE (MALL_MAX_WIDTH * MALL_MAX_HEIGHT / 8) // 162 bytes
#define MALL_GRID_MASK_SIZE MALL_MAX_GRID_MASK_SIZE

#define NUM_COLORS 4
extern Color mall_colors[NUM_COLORS];

#pragma pack(push, 1)

typedef struct {
  // Supermarket geometry & WFC
  uint8_t mall_width;           // 16..36 (default 24)
  uint8_t mall_height;          // 16..36 (default 24)
  uint8_t wfc_preset;           // 0=Classic, 1=Aisles, 2=Warehouse, 3=Maze, 4=Plaza, 5=Custom
  uint8_t wfc_custom[32];       // 16x16 1-bit bitmap (256 bits = 32 bytes)

  // Shopping list rules
  uint8_t shopping_list_count;  // 1..16 (default 5)
  uint8_t item_variety;         // 0=All Sections, 1=Fresh (Bakery/Produce), 2=Pantry (Meat/Dairy)

  // Driving & Handling physics
  float push_force;             // 25.0f .. 80.0f (default 45.0f)
  float roll_friction;          // 0.01f .. 0.15f (default 0.05f)
  float angular_friction;       // 0.5f .. 5.0f (default 2.2f)
  float lateral_grip;           // 0.2f .. 2.5f (default 1.0f)
  float cart_mass;              // 15.0f .. 80.0f (default 35.0f)
  float wall_restitution;       // 0.05f .. 0.85f (default 0.20f)

  // Combat, collision & items
  uint8_t theft_mode;           // 0=Disabled, 1=Steal to cart (default), 2=Floor drop only
  uint8_t floor_drop_count;     // 0=None, 1=Drop 1 (default), 2=Drop 2, 3=Drop all
  float crash_impulse_mult;     // 0.3f .. 2.5f (default 1.0f)
  float min_crash_speed;        // 0.0f .. 1.0f (default 0.02f)

  // Visuals
  uint8_t floor_theme;          // 0=Classic, 1=Warm Terrazzo, 2=Midnight Cyber, 3=Fresh Mint

  // Multi-cart & Teams
  uint8_t game_mode;            // 0=FFA, 1=2v2 Teams (default: 0)
  uint8_t max_players;          // 2..4 (default: 4)
} RoomConfig;

#pragma pack(pop)

enum {
  GAME_MODE_FFA = 0,
  GAME_MODE_2V2 = 1,
};

extern RoomConfig g_config;
RoomConfig room_config_default(void);

enum {
  WFC_PRESET_ID_CLASSIC = 0,
  WFC_PRESET_ID_AISLES = 1,
  WFC_PRESET_ID_WAREHOUSE = 2,
  WFC_PRESET_ID_MAZE = 3,
  WFC_PRESET_ID_PLAZA = 4,
  WFC_PRESET_ID_CUSTOM = 5,
};

// 32-byte (16x16 1-bit) WFC pattern presets
extern const uint8_t WFC_PRESET_CLASSIC[32];
extern const uint8_t WFC_PRESET_AISLES[32];
extern const uint8_t WFC_PRESET_WAREHOUSE[32];
extern const uint8_t WFC_PRESET_MAZE[32];
extern const uint8_t WFC_PRESET_PLAZA[32];

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

void mall_pack_grid(int w, int h, const int grid[MALL_MAX_HEIGHT][MALL_MAX_WIDTH], uint8_t *mask);
void mall_unpack_grid(int w, int h, const uint8_t *mask, int grid[MALL_MAX_HEIGHT][MALL_MAX_WIDTH]);

void mall_generation(const RoomConfig *config, WallArr *walls, ItemArr *items,
                     IslandPosterArr *posters, IslandShelfArr *shelves,
                     unsigned int seed, const uint8_t *in_grid_mask,
                     uint8_t *out_grid_mask);

#endif // MALL_H
