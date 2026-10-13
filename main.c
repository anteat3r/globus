#include "cart.h"
#include "geometry.h"
#include "item.h"
#include "mall.h"
#include "net.h"
#include "wall.h"
#include <float.h>
#include <math.h>
#include <raylib.h>
#include <raymath.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

typedef enum {
  APP_STATE_LOBBY = 0,
  APP_STATE_PLAYING,
} AppState;

static AppState g_app_state = APP_STATE_PLAYING;
#define MAX_PLAYERS 4

typedef struct {
  Cart cart;
  bool active;
  bool initialized;
  uint8_t player_id; // 1..4
  uint8_t team_id;   // 1 or 2 (in 2v2: P1,P2 = 1, P3,P4 = 2; in FFA: team_id = player_id)
  Vector2 target_pos;
  Vector2 target_vel;
  float target_rot;
  float target_ang_vel;
  int pick_side;
  Vector2 pick_target;
  float last_packet_time;
  ShoppingList shopping_list;
  char name[16];
} PlayerSlot;

static PlayerSlot g_players[MAX_PLAYERS];
static int g_local_player_id = 1; // 1..4
#define main_cart (g_players[g_local_player_id - 1].cart)

// 2v2 team shopping lists (index 1 = Team 1 [Blue], index 2 = Team 2 [Red])
static ShoppingList team_shopping_lists[3] = {0};

static bool any_remote_player_active(void) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i != g_local_player_id - 1 && g_players[i].active) {
      return true;
    }
  }
  return false;
}

static ShoppingList *get_player_shopping_list(int player_id) {
  if (player_id < 1 || player_id > MAX_PLAYERS) player_id = 1;
  if (g_config.game_mode == GAME_MODE_2V2) {
    int team = g_players[player_id - 1].team_id;
    if (team < 1 || team > 2) team = (player_id <= 2 ? 1 : 2);
    return &team_shopping_lists[team];
  } else {
    return &g_players[player_id - 1].shopping_list;
  }
}

// Colors for FFA mode (distinct for all 4 players)
static const Color PLAYER_OUTLINE_COLORS[MAX_PLAYERS] = {
    DARKBLUE,                  // P1: Blue outline
    MAROON,                    // P2: Red outline
    DARKGREEN,                 // P3: Green outline
    (Color){180, 100, 0, 255}, // P4: Gold outline
};

static const Color PLAYER_BODY_COLORS[MAX_PLAYERS] = {
    BLUE,  // P1: Royal Blue
    RED,   // P2: Crimson Red
    LIME,  // P3: Green
    GOLD,  // P4: Yellow/Gold
};

// Colors for 2v2 mode: Team 1 = Shades of Blue, Team 2 = Shades of Red/Orange
static const Color TEAM_BODY_COLORS[MAX_PLAYERS] = {
    (Color){37, 99, 235, 255},  // P1: Royal Blue (Team 1)
    (Color){56, 189, 248, 255}, // P2: Sky Blue (Team 1)
    (Color){220, 38, 38, 255},  // P3: Crimson Red (Team 2)
    (Color){251, 146, 60, 255}, // P4: Coral Orange (Team 2)
};

static const Color TEAM_OUTLINE_COLORS[MAX_PLAYERS] = {
    DARKBLUE,
    (Color){3, 105, 161, 255},
    MAROON,
    (Color){194, 65, 12, 255},
};

static Vector2 get_spawn_pos(int player_id, float cx, float cy) {
  int max_p = (g_config.max_players >= 2 && g_config.max_players <= 4) ? g_config.max_players : 4;
  if (g_config.game_mode == GAME_MODE_2V2 || max_p > 2) {
    switch (player_id) {
      case 1: return (Vector2){cx - 0.9f, cy - 0.5f};
      case 2: return (Vector2){cx - 0.9f, cy + 0.5f};
      case 3: return (Vector2){cx + 0.9f, cy - 0.5f};
      case 4: return (Vector2){cx + 0.9f, cy + 0.5f};
      default: return (Vector2){cx, cy};
    }
  } else {
    // 1v1
    if (player_id == 1) return (Vector2){cx - 0.6f, cy};
    else return (Vector2){cx + 0.6f, cy};
  }
}

static void init_player_cart(int player_id, float cx, float cy) {
  if (player_id < 1 || player_id > MAX_PLAYERS) return;
  int idx = player_id - 1;
  Color outline, body;
  if (g_config.game_mode == GAME_MODE_2V2) {
    outline = TEAM_OUTLINE_COLORS[idx];
    body = TEAM_BODY_COLORS[idx];
  } else {
    outline = PLAYER_OUTLINE_COLORS[idx];
    body = PLAYER_BODY_COLORS[idx];
  }

  if (g_players[idx].initialized) {
    cart_free(&g_players[idx].cart);
    g_players[idx].initialized = false;
  }

  g_players[idx].cart = cart_new_goofy(
      outline, body, DARKGRAY, Vector2Zero(), 4,
      (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
      (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
  cart_set_mass(&g_players[idx].cart, g_config.cart_mass);

  Vector2 spawn = get_spawn_pos(player_id, cx, cy);
  g_players[idx].cart.pos = spawn;
  g_players[idx].cart.vel = Vector2Zero();
  g_players[idx].cart.rot = (player_id > 2 || (g_config.game_mode == GAME_MODE_2V2 && player_id > 2)) ? PI : 0.0f;
  g_players[idx].cart.ang_vel = 0.0f;
  g_players[idx].target_pos = spawn;
  g_players[idx].target_vel = Vector2Zero();
  g_players[idx].target_rot = g_players[idx].cart.rot;
  g_players[idx].target_ang_vel = 0.0f;
  g_players[idx].pick_side = -1;
  g_players[idx].pick_target = Vector2Zero();
  g_players[idx].last_packet_time = (float)GetTime();
  g_players[idx].player_id = (uint8_t)player_id;
  if (g_config.game_mode == GAME_MODE_2V2) {
    g_players[idx].team_id = (player_id <= 2) ? 1 : 2;
  } else {
    g_players[idx].team_id = (uint8_t)player_id;
  }
  snprintf(g_players[idx].name, sizeof(g_players[idx].name), "Player %d", player_id);
  g_players[idx].initialized = true;
}

static bool has_remote_cart = false;
static float last_cart_state_send_time = 0.0f;
static int last_sent_pick_side = -1;

static Camera2D cam;
static bool rot_follow = false;
static WallArr walls;
static bool dragging = false;
static int sel_vrt = 0;
static int sel_wall = 0;
static ItemArr items;
static IslandPosterArr posters;
static IslandShelfArr shelves;
static int pick_side = -1;
static int pick_item_idx = -1;
static float ela_time = 0.0f;
static float race_start_time = 0.0f;
static bool world_initialized = false;
static uint32_t net_seed = 0;
static uint8_t host_grid_mask[MALL_GRID_MASK_SIZE] = {0};
RoomConfig g_config;
#undef CART_PUSH_FORCE
#define CART_PUSH_FORCE ((g_config.push_force > 0.0f) ? g_config.push_force : 45.0f)

static int json_find_int(const char *json, const char *key, int default_val) {
  char search[64];
  snprintf(search, sizeof(search), "\"%s\"", key);
  const char *p = strstr(json, search);
  if (!p) return default_val;
  p += strlen(search);
  while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
  if (!*p) return default_val;
  return atoi(p);
}

static float json_find_float(const char *json, const char *key, float default_val) {
  char search[64];
  snprintf(search, sizeof(search), "\"%s\"", key);
  const char *p = strstr(json, search);
  if (!p) return default_val;
  p += strlen(search);
  while (*p && (*p == ' ' || *p == ':' || *p == '\t')) p++;
  if (!*p) return default_val;
  return (float)atof(p);
}

static void json_find_hex(const char *json, const char *key, uint8_t *out_bytes, int max_bytes) {
  char search[64];
  snprintf(search, sizeof(search), "\"%s\"", key);
  const char *p = strstr(json, search);
  if (!p) return;
  p += strlen(search);
  while (*p && (*p == ' ' || *p == ':' || *p == '\t' || *p == '\"')) p++;
  for (int i = 0; i < max_bytes && *p && *p != '\"'; i++) {
    unsigned int byte_val = 0;
    if (sscanf(p, "%2x", &byte_val) == 1) {
      out_bytes[i] = (uint8_t)byte_val;
      p += 2;
    } else {
      break;
    }
  }
}

__attribute__((unused))
static void room_config_from_json(const char *json, RoomConfig *cfg) {
  if (!json || !cfg) return;
  cfg->mall_width = (uint8_t)json_find_int(json, "mallWidth", cfg->mall_width);
  cfg->mall_height = (uint8_t)json_find_int(json, "mallHeight", cfg->mall_height);
  cfg->wfc_preset = (uint8_t)json_find_int(json, "wfcPreset", cfg->wfc_preset);
  json_find_hex(json, "wfcCustomHex", cfg->wfc_custom, sizeof(cfg->wfc_custom));
  cfg->shopping_list_count = (uint8_t)json_find_int(json, "shoppingListCount", cfg->shopping_list_count);
  cfg->item_variety = (uint8_t)json_find_int(json, "itemVariety", cfg->item_variety);
  cfg->push_force = json_find_float(json, "pushForce", cfg->push_force);
  cfg->roll_friction = json_find_float(json, "rollFriction", cfg->roll_friction);
  cfg->angular_friction = json_find_float(json, "angularFriction", cfg->angular_friction);
  cfg->lateral_grip = json_find_float(json, "lateralGrip", cfg->lateral_grip);
  cfg->cart_mass = json_find_float(json, "cartMass", cfg->cart_mass);
  cfg->wall_restitution = json_find_float(json, "wallRestitution", cfg->wall_restitution);
  cfg->theft_mode = (uint8_t)json_find_int(json, "theftMode", cfg->theft_mode);
  cfg->floor_drop_count = (uint8_t)json_find_int(json, "floorDropCount", cfg->floor_drop_count);
  cfg->crash_impulse_mult = json_find_float(json, "crashImpulseMult", cfg->crash_impulse_mult);
  cfg->min_crash_speed = json_find_float(json, "minCrashSpeed", cfg->min_crash_speed);
  cfg->floor_theme = (uint8_t)json_find_int(json, "floorTheme", cfg->floor_theme);
  cfg->game_mode = (uint8_t)json_find_int(json, "gameMode", cfg->game_mode);
  cfg->max_players = (uint8_t)json_find_int(json, "maxPlayers", cfg->max_players);
  if (cfg->max_players < 2) cfg->max_players = 2;
  if (cfg->max_players > 4) cfg->max_players = 4;

  if (cfg->mall_width < MALL_MIN_DIM) cfg->mall_width = MALL_MIN_DIM;
  if (cfg->mall_width > MALL_MAX_WIDTH) cfg->mall_width = MALL_MAX_WIDTH;
  if (cfg->mall_height < MALL_MIN_DIM) cfg->mall_height = MALL_MIN_DIM;
  if (cfg->mall_height > MALL_MAX_HEIGHT) cfg->mall_height = MALL_MAX_HEIGHT;
  if (cfg->shopping_list_count < 1) cfg->shopping_list_count = 1;
  if (cfg->shopping_list_count > SHOPPING_LIST_MAX_LEN) cfg->shopping_list_count = SHOPPING_LIST_MAX_LEN;
}

static ShoppingList local_shopping_list = {0};
static ShoppingList remote_shopping_list = {0};
static int winner =
    0; // 0 = Ongoing race, 1 = Player 1 (Blue), 2 = Player 2 (Red)
static float winning_time = 0.0f;
static bool rematch_requested = false;
static const char *screenshot_path = NULL;
static int screenshot_countdown = -1;
static float last_collision_time = 0.0f;
static char crash_toast_text[96] = {0};
static float crash_toast_time = 0.0f;

#define MAX_DROPPED_FLOOR_ITEMS 64
typedef struct {
  Vector2 pos;
  int texture_id;
  int owner_player_id; // 1 = Blue/Host, 2 = Red/Client
  float drop_time;
  bool active;
} DroppedFloorItem;
static DroppedFloorItem dropped_floor_items[MAX_DROPPED_FLOOR_ITEMS] = {0};

#define MAX_FLYING_DROPS 16
typedef struct {
  Item item;
  Vector2 start_pos;
  Vector2 end_pos;
  float start_time;
  float duration;
  int owner_player_id;
  bool active;
} FlyingDropAnim;
static FlyingDropAnim flying_drops[MAX_FLYING_DROPS] = {0};

#define MAX_SHOCKWAVES 32
typedef struct {
  Vector2 pos;
  float start_time;
  float duration;
  float max_radius;
  Color color;
  bool active;
} ShockwaveFx;
static ShockwaveFx shockwaves[MAX_SHOCKWAVES] = {0};

#define MAX_FLOATING_TEXTS 16
typedef struct {
  Vector2 pos;
  char text[32];
  Color color;
  float start_time;
  float duration;
  bool active;
} FloatingTextFx;
static FloatingTextFx floating_texts[MAX_FLOATING_TEXTS] = {0};

static void add_dropped_floor_item(Vector2 pos, int texture_id, int owner_id) {
  for (int i = 0; i < MAX_DROPPED_FLOOR_ITEMS; i++) {
    if (!dropped_floor_items[i].active) {
      dropped_floor_items[i] = (DroppedFloorItem){
          .pos = pos,
          .texture_id = texture_id,
          .owner_player_id = owner_id,
          .drop_time = (float)GetTime(),
          .active = true,
      };
      return;
    }
  }
}

static void remove_dropped_floor_item(Vector2 pos, int texture_id) {
  for (int i = 0; i < MAX_DROPPED_FLOOR_ITEMS; i++) {
    if (dropped_floor_items[i].active &&
        dropped_floor_items[i].texture_id == texture_id &&
        Vector2Distance(dropped_floor_items[i].pos, pos) < 0.65f) {
      dropped_floor_items[i].active = false;
      return;
    }
  }
}

static void spawn_flying_drop(Item item, Vector2 start_pos, Vector2 end_pos,
                              int owner_id) {
  for (int i = 0; i < MAX_FLYING_DROPS; i++) {
    if (!flying_drops[i].active) {
      flying_drops[i] = (FlyingDropAnim){
          .item = item,
          .start_pos = start_pos,
          .end_pos = end_pos,
          .start_time = (float)GetTime(),
          .duration = 0.55f,
          .owner_player_id = owner_id,
          .active = true,
      };
      return;
    }
  }
}

static void spawn_shockwave(Vector2 pos, float max_radius, Color color) {
  for (int i = 0; i < MAX_SHOCKWAVES; i++) {
    if (!shockwaves[i].active) {
      shockwaves[i] = (ShockwaveFx){
          .pos = pos,
          .start_time = (float)GetTime(),
          .duration = 0.50f,
          .max_radius = max_radius,
          .color = color,
          .active = true,
      };
      return;
    }
  }
}

static void spawn_floating_text(Vector2 pos, const char *text, Color color) {
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (!floating_texts[i].active) {
      floating_texts[i] = (FloatingTextFx){
          .pos = pos,
          .color = color,
          .start_time = (float)GetTime(),
          .duration = 1.25f,
          .active = true,
      };
      strncpy(floating_texts[i].text, text, sizeof(floating_texts[i].text) - 1);
      floating_texts[i].text[sizeof(floating_texts[i].text) - 1] = '\0';
      return;
    }
  }
}

static float LerpAngle(float a, float b, float t) {
  float diff = fmodf(b - a + PI, 2.0f * PI) - PI;
  if (diff < -PI)
    diff += 2.0f * PI;
  return a + diff * t;
}

static void sync_local_shopping_list(void) {
  ShoppingList *my_list = get_player_shopping_list(g_local_player_id);
  local_shopping_list.len = my_list->len;
  local_shopping_list.initial_len = my_list->initial_len;
  shopping_list_len = my_list->len;
  for (int i = 0; i < my_list->len; i++) {
    shopping_list[i] = my_list->items[i];
    local_shopping_list.items[i] = my_list->items[i];
  }
}

static void init_game_world(uint32_t seed, const uint8_t *in_grid_mask) {
  if (world_initialized)
    return;
  net_seed = seed;
  mall_seed_prng(seed);
  srand(seed);

  fill_items_table();

  items = itemarr_new();
  walls = wallarr_new();
  posters = island_posterarr_new();
  shelves = island_shelfarr_new();
  mall_generation(&g_config, &walls, &items, &posters, &shelves, seed,
                  in_grid_mask, host_grid_mask);

  printf("%d items loaded, %d shelves, %d posters generated (seed: %u)\n",
         items.len, shelves.len, posters.len, seed);
  fflush(stdout);

  if (in_grid_mask == NULL) {
    fill_shopping_list();
    for (int t = 1; t <= 2; t++) {
      team_shopping_lists[t].len = shopping_list_len;
      team_shopping_lists[t].initial_len = shopping_list_len;
      for (int i = 0; i < shopping_list_len; i++) {
        team_shopping_lists[t].items[i] = shopping_list[i];
        team_shopping_lists[t].initial_items[i] = shopping_list[i];
      }
    }
    for (int p = 0; p < MAX_PLAYERS; p++) {
      g_players[p].shopping_list.len = shopping_list_len;
      g_players[p].shopping_list.initial_len = shopping_list_len;
      for (int i = 0; i < shopping_list_len; i++) {
        g_players[p].shopping_list.items[i] = shopping_list[i];
        g_players[p].shopping_list.initial_items[i] = shopping_list[i];
      }
    }
    local_shopping_list.len = shopping_list_len;
    local_shopping_list.initial_len = shopping_list_len;
    remote_shopping_list.len = shopping_list_len;
    remote_shopping_list.initial_len = shopping_list_len;
    for (int i = 0; i < shopping_list_len; i++) {
      local_shopping_list.items[i] = shopping_list[i];
      local_shopping_list.initial_items[i] = shopping_list[i];
      remote_shopping_list.items[i] = shopping_list[i];
      remote_shopping_list.initial_items[i] = shopping_list[i];
    }
  }
  winner = 0;
  winning_time = 0.0f;
  race_start_time = 0.0f;
  ela_time = 0.0f;
  rematch_requested = false;
  last_collision_time = 0.0f;
  crash_toast_time = 0.0f;
  crash_toast_text[0] = '\0';
  memset(dropped_floor_items, 0, sizeof(dropped_floor_items));
  memset(flying_drops, 0, sizeof(flying_drops));
  memset(shockwaves, 0, sizeof(shockwaves));
  memset(floating_texts, 0, sizeof(floating_texts));
  world_initialized = true;
}

static void apply_remote_shopping_list(const PktInit *init) {
  shopping_list_len = init->shopping_list_count;
  for (int i = 0; i < shopping_list_len && i < SHOPPING_LIST_MAX_LEN; i++) {
    shopping_list[i] = init->shopping_list[i];
  }
  for (int t = 1; t <= 2; t++) {
    team_shopping_lists[t].len = shopping_list_len;
    team_shopping_lists[t].initial_len = shopping_list_len;
    for (int i = 0; i < shopping_list_len; i++) {
      team_shopping_lists[t].items[i] = shopping_list[i];
      team_shopping_lists[t].initial_items[i] = shopping_list[i];
    }
  }
  for (int p = 0; p < MAX_PLAYERS; p++) {
    g_players[p].shopping_list.len = shopping_list_len;
    g_players[p].shopping_list.initial_len = shopping_list_len;
    for (int i = 0; i < shopping_list_len; i++) {
      g_players[p].shopping_list.items[i] = shopping_list[i];
      g_players[p].shopping_list.initial_items[i] = shopping_list[i];
    }
  }
  local_shopping_list.len = shopping_list_len;
  local_shopping_list.initial_len = shopping_list_len;
  remote_shopping_list.len = shopping_list_len;
  remote_shopping_list.initial_len = shopping_list_len;
  for (int i = 0; i < shopping_list_len; i++) {
    local_shopping_list.items[i] = shopping_list[i];
    local_shopping_list.initial_items[i] = shopping_list[i];
    remote_shopping_list.items[i] = shopping_list[i];
    remote_shopping_list.initial_items[i] = shopping_list[i];
  }
  winner = 0;
  winning_time = 0.0f;
  race_start_time = 0.0f;
  ela_time = 0.0f;
  rematch_requested = false;
  last_collision_time = 0.0f;
  crash_toast_time = 0.0f;
  crash_toast_text[0] = '\0';
}

static void restart_match(void);

static void cleanup_game_world(void) {
  if (world_initialized) {
    island_shelfarr_free(&shelves);
    island_posterarr_free(&posters);
    itemarr_free_all(&items);
    wallarr_free_all(&walls);
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (g_players[i].initialized) {
        itemarr_free_all(&g_players[i].cart.items);
        g_players[i].cart.mass = g_players[i].cart.base_mass;
        g_players[i].cart.mom_inertia = g_players[i].cart.base_mom_inertia;
      }
    }
    world_initialized = false;
  }
}

static void free_carts(void) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (g_players[i].initialized) {
      cart_free(&g_players[i].cart);
      g_players[i].initialized = false;
      g_players[i].active = false;
    }
  }
}

static void start_singleplayer(void) {
  cleanup_game_world();
  free_carts();
  net_close();
  g_net.mode = NET_MODE_SINGLE;
  if (g_local_player_id < 1 || g_local_player_id > MAX_PLAYERS) {
    g_local_player_id = 1;
  }
  has_remote_cart = false;

  float cx = (float)((g_config.mall_width > 0 ? g_config.mall_width : 24) / 2);
  float cy = (float)((g_config.mall_height > 0 ? g_config.mall_height : 24) / 2);

  for (int i = 1; i <= MAX_PLAYERS; i++) {
    init_player_cart(i, cx, cy);
  }
  g_players[g_local_player_id - 1].active = true;
  g_players[g_local_player_id - 1].cart.pos = get_spawn_pos(g_local_player_id, cx, cy);

  init_game_world((uint32_t)time(NULL), NULL);
  g_app_state = APP_STATE_PLAYING;
#if defined(__EMSCRIPTEN__)
  EM_ASM({
    if (window.onGameStarted) window.onGameStarted();
  });
#endif
}

static void start_host(const char *room_code, const char *ws_url, int port) {
  cleanup_game_world();
  free_carts();

#if defined(__EMSCRIPTEN__)
  net_init_web(ws_url, room_code, NET_MODE_HOST);
#else
  (void)room_code;
  (void)ws_url;
  net_init_host(port);
#endif

  float cx = (float)((g_config.mall_width > 0 ? g_config.mall_width : 24) / 2);
  float cy = (float)((g_config.mall_height > 0 ? g_config.mall_height : 24) / 2);

  g_local_player_id = 1;
  for (int i = 1; i <= MAX_PLAYERS; i++) {
    init_player_cart(i, cx, cy);
  }
  g_players[0].active = true;
  has_remote_cart = false;

  init_game_world((uint32_t)time(NULL), NULL);
  g_app_state = APP_STATE_PLAYING;
}

static void start_client(const char *join_ip_or_room, const char *ws_url, int port) {
  cleanup_game_world();
  free_carts();

#if defined(__EMSCRIPTEN__)
  net_init_web(ws_url, join_ip_or_room, NET_MODE_CLIENT);
#else
  (void)ws_url;
  net_init_client(join_ip_or_room, port);
#endif

  float cx = (float)((g_config.mall_width > 0 ? g_config.mall_width : 24) / 2);
  float cy = (float)((g_config.mall_height > 0 ? g_config.mall_height : 24) / 2);

  if (g_local_player_id < 1 || g_local_player_id > MAX_PLAYERS) {
    g_local_player_id = 2;
  }
  for (int i = 1; i <= MAX_PLAYERS; i++) {
    init_player_cart(i, cx, cy);
  }
  g_players[g_local_player_id - 1].active = true;
  has_remote_cart = false;

  g_app_state = APP_STATE_PLAYING;
  net_send_hello_multi(g_players[g_local_player_id - 1].name,
                       (uint8_t)g_local_player_id,
                       g_players[g_local_player_id - 1].team_id);
  g_net.last_send_time = (float)GetTime();
}

static void return_to_lobby(void) {
  cleanup_game_world();
  free_carts();
  net_close();
  has_remote_cart = false;
  g_app_state = APP_STATE_LOBBY;
#if defined(__EMSCRIPTEN__)
  EM_ASM({
    if (window.onReturnToLobby) window.onReturnToLobby();
  });
#endif
}

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_KEEPALIVE
void web_start_singleplayer(void) {
  start_singleplayer();
}

EMSCRIPTEN_KEEPALIVE
void web_start_host(const char *room_code, const char *ws_url) {
  start_host(room_code, ws_url, DEFAULT_NET_PORT);
}

EMSCRIPTEN_KEEPALIVE
void web_start_join(const char *room_code, const char *ws_url) {
  start_client(room_code, ws_url, DEFAULT_NET_PORT);
}

EMSCRIPTEN_KEEPALIVE
void web_return_to_lobby(void) {
  return_to_lobby();
}

EMSCRIPTEN_KEEPALIVE
void web_request_rematch(void) {
  restart_match();
}

EMSCRIPTEN_KEEPALIVE
void web_resize(int width, int height) {
  if (width < 200 || height < 200) return;
  SetWindowSize(width, height);
}

EMSCRIPTEN_KEEPALIVE
void web_zoom_in(void) {
  cam.zoom += 15.0f;
  if (cam.zoom > 220.0f)
    cam.zoom = 220.0f;
}

EMSCRIPTEN_KEEPALIVE
void web_zoom_out(void) {
  cam.zoom -= 15.0f;
  if (cam.zoom < 60.0f)
    cam.zoom = 60.0f;
}

EMSCRIPTEN_KEEPALIVE
void web_set_room_config_json(const char *json_str) {
  room_config_from_json(json_str, &g_config);
}

EMSCRIPTEN_KEEPALIVE
void web_set_player_info(int player_id, int team_id) {
  if (player_id >= 1 && player_id <= MAX_PLAYERS) {
    g_local_player_id = player_id;
  }
  if (g_local_player_id >= 1 && g_local_player_id <= MAX_PLAYERS) {
    if (team_id == 1 || team_id == 2) {
      g_players[g_local_player_id - 1].team_id = (uint8_t)team_id;
    }
  }
}

EMSCRIPTEN_KEEPALIVE
void web_peer_joined(void) {
  if (g_net.mode == NET_MODE_HOST && world_initialized) {
    uint8_t mask = 0;
    uint8_t teams[MAX_NET_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (g_players[i].active) mask |= (uint8_t)(1 << i);
      teams[i] = g_players[i].team_id;
    }
    net_send_init_multi(net_seed, &g_config, shopping_list, shopping_list_len, host_grid_mask, mask, teams);
    g_net.state = NET_STATE_CONNECTING;
  }
}

EMSCRIPTEN_KEEPALIVE
void web_tab_resumed(void) {
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i != g_local_player_id - 1 && g_players[i].active) {
      g_players[i].cart.pos = g_players[i].target_pos;
      g_players[i].cart.rot = g_players[i].target_rot;
      g_players[i].cart.vel = g_players[i].target_vel;
      g_players[i].cart.ang_vel = g_players[i].target_ang_vel;
    }
  }
}
#endif

static void restart_match(void) {
  float cx = (float)((g_config.mall_width > 0 ? g_config.mall_width : 24) / 2);
  float cy = (float)((g_config.mall_height > 0 ? g_config.mall_height : 24) / 2);
  if (g_net.mode == NET_MODE_SINGLE) {
    cleanup_game_world();
    g_players[0].cart.pos = (Vector2){cx, cy};
    g_players[0].cart.vel = Vector2Zero();
    g_players[0].cart.rot = 0.0f;
    g_players[0].cart.ang_vel = 0.0f;
    init_game_world((uint32_t)time(NULL), NULL);
  } else if (g_net.mode == NET_MODE_HOST) {
    cleanup_game_world();
    for (int i = 0; i < MAX_PLAYERS; i++) {
      Vector2 sp = get_spawn_pos(i + 1, cx, cy);
      g_players[i].cart.pos = sp;
      g_players[i].cart.vel = Vector2Zero();
      g_players[i].cart.rot = (i >= 2 || (g_config.game_mode == GAME_MODE_2V2 && g_players[i].team_id == 2)) ? PI : 0.0f;
      g_players[i].cart.ang_vel = 0.0f;
      g_players[i].target_pos = sp;
      g_players[i].target_vel = Vector2Zero();
      g_players[i].target_rot = g_players[i].cart.rot;
      g_players[i].target_ang_vel = 0.0f;
      g_players[i].pick_side = -1;
      g_players[i].pick_target = Vector2Zero();
      g_players[i].last_packet_time = (float)GetTime();
    }
    last_cart_state_send_time = 0.0f;
    last_sent_pick_side = -1;

    uint32_t new_seed = (uint32_t)time(NULL);
    init_game_world(new_seed, NULL);
    if (g_net.state == NET_STATE_CONNECTED) {
      uint8_t mask = 0;
      uint8_t teams[MAX_NET_PLAYERS];
      for (int i = 0; i < MAX_PLAYERS; i++) {
        if (g_players[i].active) mask |= (uint8_t)(1 << i);
        teams[i] = g_players[i].team_id;
      }
      net_send_init_multi(new_seed, &g_config, shopping_list, shopping_list_len, host_grid_mask, mask, teams);
    }
  } else if (g_net.mode == NET_MODE_CLIENT) {
    rematch_requested = true;
    net_send_hello_multi(g_players[g_local_player_id - 1].name,
                         (uint8_t)g_local_player_id,
                         g_players[g_local_player_id - 1].team_id);
  }
}

static void handle_host_cart_collision(int p1_id, int p2_id,
                                       Vector2 bump_impulse, float bump_ang) {
  float now = (float)GetTime();
  if (now - last_collision_time < 0.35f || winner != 0) {
    return;
  }
  last_collision_time = now;

  int idx1 = p1_id - 1;
  int idx2 = p2_id - 1;
  Cart *c1 = &g_players[idx1].cart;
  Cart *c2 = &g_players[idx2].cart;

  bool same_team = (g_config.game_mode == GAME_MODE_2V2 &&
                    g_players[idx1].team_id == g_players[idx2].team_id);

  float bump_mult = (g_config.crash_impulse_mult > 0.0f) ? g_config.crash_impulse_mult : 1.0f;
  Vector2 crash_pos = Vector2Scale(Vector2Add(c1->pos, c2->pos), 0.5f);

  if (same_team) {
    // Friendly collision in 2v2: bump impulses apply, but NO item stealing or drops!
    Color team_col = (g_players[idx1].team_id == 1) ? BLUE : RED;
    spawn_shockwave(crash_pos, 0.9f * bump_mult, team_col);
    spawn_floating_text(crash_pos, "TEAM BUMP!", team_col);

    if (g_net.mode == NET_MODE_HOST && g_net.state == NET_STATE_CONNECTED) {
      PktCollisionEvent pkt = {
          .header = {.type = PKT_COLLISION_EVENT},
          .impulse_x = bump_impulse.x * bump_mult,
          .impulse_y = bump_impulse.y * bump_mult,
          .ang_impulse = bump_ang * bump_mult,
          .faster_player_id = 0,
          .transferred_texture_id = -1,
          .p1_dropped_texture_id = -1,
          .p1_drop_x = 0, .p1_drop_y = 0,
          .p2_dropped_texture_id = -1,
          .p2_drop_x = 0, .p2_drop_y = 0,
          .p1_id = (uint8_t)p1_id,
          .p2_id = (uint8_t)p2_id,
      };
      net_send_collision_event(&pkt);
    }
    return;
  }

  // Opponent collision: competitive stealing & grocery drops
  float p1_speed = Vector2Length(c1->vel);
  float p2_speed = Vector2Length(c2->vel);
  float min_speed = (g_config.min_crash_speed > 0.0f) ? g_config.min_crash_speed : 0.02f;
  int faster_id = 0;
  if (p1_speed > p2_speed + min_speed) {
    faster_id = p1_id;
  } else if (p2_speed > p1_speed + min_speed) {
    faster_id = p2_id;
  } else if (p1_speed > p2_speed) {
    faster_id = p1_id;
  } else if (p2_speed > p1_speed) {
    faster_id = p2_id;
  }

  int transferred_texture_id = -1;

  if (faster_id != 0 && g_config.theft_mode == 1) {
    int slower_id = (faster_id == p1_id) ? p2_id : p1_id;
    Cart *faster_cart = &g_players[faster_id - 1].cart;
    Cart *slower_cart = &g_players[slower_id - 1].cart;
    ShoppingList *faster_slist = get_player_shopping_list(faster_id);
    ShoppingList *slower_slist = get_player_shopping_list(slower_id);

    int best_cand_idx = -1;
    for (int i = 0; i < slower_cart->items.len; i++) {
      Item it = slower_cart->items.items[i];
      bool in_slower_initial = false;
      for (int k = 0; k < slower_slist->initial_len; k++) {
        if (items_table[slower_slist->initial_items[k]].image.id == it.image.id) {
          in_slower_initial = true;
          break;
        }
      }
      if (in_slower_initial) {
        bool faster_needs = false;
        for (int k = 0; k < faster_slist->len; k++) {
          if (items_table[faster_slist->items[k]].image.id == it.image.id) {
            faster_needs = true;
            break;
          }
        }
        if (faster_needs) {
          best_cand_idx = i;
          break;
        } else if (best_cand_idx == -1) {
          best_cand_idx = i;
        }
      }
    }

    if (best_cand_idx != -1) {
      Item stolen_item;
      if (cart_remove_item(slower_cart, best_cand_idx, &stolen_item)) {
        transferred_texture_id = (int)stolen_item.image.id;
        shopping_list_readd(slower_slist, stolen_item);
        cart_add_item(faster_cart, stolen_item);
        shopping_list_consume(faster_slist, stolen_item);

        spawn_floating_text(slower_cart->pos, "STOLEN!", ORANGE);
        spawn_floating_text(faster_cart->pos, "+1 STOLEN!", GOLD);
        spawn_shockwave(faster_cart->pos, 0.7f, GOLD);

        if (faster_slist->len == 0 && winner == 0) {
          if (g_config.game_mode == GAME_MODE_2V2) {
            winner = g_players[faster_id - 1].team_id;
            winning_time = ela_time;
            net_send_game_over_multi((uint8_t)faster_id, winning_time, (uint8_t)winner);
          } else {
            winner = faster_id;
            winning_time = ela_time;
            net_send_game_over_multi((uint8_t)winner, winning_time, 0);
          }
        }
      }
    }
  }

  // Crash impact shockwave
  spawn_shockwave(crash_pos, 1.3f * bump_mult, ORANGE);

  // Floor drop items based on room config
  int max_drops = (g_config.floor_drop_count == 3) ? 999 : (int)g_config.floor_drop_count;

  int p1_drop_tex_id = -1;
  Vector2 p1_drop_pos = Vector2Zero();
  for (int d = 0; d < max_drops && c1->items.len > 0; d++) {
    int drop_idx = c1->items.len - 1;
    Item dropped;
    if (cart_remove_item(c1, drop_idx, &dropped)) {
      shopping_list_readd(get_player_shopping_list(p1_id), dropped);
      float ang = mall_rand_float() * 2.0f * PI;
      float r = 0.5f + mall_rand_float() * 0.35f;
      dropped.pos = Vector2Add(c1->pos, (Vector2){cosf(ang) * r, sinf(ang) * r});
      dropped.rot = mall_rand_float() * 2.0f * PI;
      p1_drop_tex_id = (int)dropped.image.id;
      p1_drop_pos = dropped.pos;
      itemarr_push(&items, dropped);

      spawn_flying_drop(dropped, c1->pos, p1_drop_pos, p1_id);
      add_dropped_floor_item(p1_drop_pos, p1_drop_tex_id, p1_id);
      spawn_floating_text(c1->pos, "-1 DROPPED", (Color){255, 80, 80, 255});
    }
  }

  int p2_drop_tex_id = -1;
  Vector2 p2_drop_pos = Vector2Zero();
  for (int d = 0; d < max_drops && c2->items.len > 0; d++) {
    int drop_idx = c2->items.len - 1;
    Item dropped;
    if (cart_remove_item(c2, drop_idx, &dropped)) {
      shopping_list_readd(get_player_shopping_list(p2_id), dropped);
      float ang = mall_rand_float() * 2.0f * PI;
      float r = 0.5f + mall_rand_float() * 0.35f;
      dropped.pos = Vector2Add(c2->pos, (Vector2){cosf(ang) * r, sinf(ang) * r});
      dropped.rot = mall_rand_float() * 2.0f * PI;
      p2_drop_tex_id = (int)dropped.image.id;
      p2_drop_pos = dropped.pos;
      itemarr_push(&items, dropped);

      spawn_flying_drop(dropped, c2->pos, p2_drop_pos, p2_id);
      add_dropped_floor_item(p2_drop_pos, p2_drop_tex_id, p2_id);
      spawn_floating_text(c2->pos, "-1 DROPPED", (Color){255, 80, 80, 255});
    }
  }

  sync_local_shopping_list();

  if (g_net.mode == NET_MODE_HOST && g_net.state == NET_STATE_CONNECTED) {
    PktCollisionEvent pkt = {
        .header = {.type = PKT_COLLISION_EVENT},
        .impulse_x = bump_impulse.x * bump_mult,
        .impulse_y = bump_impulse.y * bump_mult,
        .ang_impulse = bump_ang * bump_mult,
        .faster_player_id = (uint8_t)faster_id,
        .transferred_texture_id = transferred_texture_id,
        .p1_dropped_texture_id = p1_drop_tex_id,
        .p1_drop_x = p1_drop_pos.x,
        .p1_drop_y = p1_drop_pos.y,
        .p2_dropped_texture_id = p2_drop_tex_id,
        .p2_drop_x = p2_drop_pos.x,
        .p2_drop_y = p2_drop_pos.y,
        .p1_id = (uint8_t)p1_id,
        .p2_id = (uint8_t)p2_id,
    };
    net_send_collision_event(&pkt);
  }

  if (p1_id == g_local_player_id || p2_id == g_local_player_id) {
    if (transferred_texture_id != -1) {
      if (faster_id == g_local_player_id) {
        snprintf(crash_toast_text, sizeof(crash_toast_text),
                 "CRASH! You stole an item from Opponent!");
      } else {
        snprintf(crash_toast_text, sizeof(crash_toast_text),
                 "CRASH! Opponent stole an item from you!");
      }
    } else {
      snprintf(crash_toast_text, sizeof(crash_toast_text),
               "CRASH! Groceries dropped on the floor!");
    }
    crash_toast_time = now;
  }
}

static void handle_client_collision_event(const PktCollisionEvent *ev) {
  float now = (float)GetTime();
  last_collision_time = now;

  int p1_id = (ev->p1_id >= 1 && ev->p1_id <= MAX_PLAYERS) ? ev->p1_id : 1;
  int p2_id = (ev->p2_id >= 1 && ev->p2_id <= MAX_PLAYERS) ? ev->p2_id : 2;

  Cart *c1 = &g_players[p1_id - 1].cart;
  Cart *c2 = &g_players[p2_id - 1].cart;

  Vector2 crash_pos = Vector2Scale(Vector2Add(c1->pos, c2->pos), 0.5f);
  spawn_shockwave(crash_pos, 1.3f, ORANGE);

  // Apply impulse to local cart if involved
  if (g_local_player_id == p1_id) {
    main_cart.vel = Vector2Add(
        main_cart.vel, (Vector2){ev->impulse_x, ev->impulse_y});
    main_cart.ang_vel += ev->ang_impulse;
  } else if (g_local_player_id == p2_id) {
    main_cart.vel = Vector2Add(
        main_cart.vel, (Vector2){-ev->impulse_x, -ev->impulse_y});
    main_cart.ang_vel -= ev->ang_impulse;
  }

  if (ev->transferred_texture_id != -1 && ev->faster_player_id != 0) {
    int faster_id = ev->faster_player_id;
    int slower_id = (faster_id == p1_id) ? p2_id : p1_id;
    Cart *faster_cart = &g_players[faster_id - 1].cart;
    Cart *slower_cart = &g_players[slower_id - 1].cart;
    ShoppingList *faster_slist = get_player_shopping_list(faster_id);
    ShoppingList *slower_slist = get_player_shopping_list(slower_id);

    Item stolen_item;
    bool found = cart_remove_item_by_texture_id(
        slower_cart, (unsigned int)ev->transferred_texture_id, &stolen_item);
    if (!found) {
      int tbl_idx = item_table_find_by_texture_id(
          (unsigned int)ev->transferred_texture_id);
      if (tbl_idx != -1) {
        stolen_item = items_table[tbl_idx];
      }
    }
    shopping_list_readd(slower_slist, stolen_item);
    cart_add_item(faster_cart, stolen_item);
    shopping_list_consume(faster_slist, stolen_item);

    spawn_floating_text(slower_cart->pos, "STOLEN!", ORANGE);
    spawn_floating_text(faster_cart->pos, "+1 STOLEN!", GOLD);
    spawn_shockwave(faster_cart->pos, 0.7f, GOLD);

    if (faster_slist->len == 0 && winner == 0) {
      if (g_config.game_mode == GAME_MODE_2V2) {
        winner = g_players[faster_id - 1].team_id;
      } else {
        winner = faster_id;
      }
      winning_time = ela_time;
    }
  }

  // Player 1 drop
  if (ev->p1_dropped_texture_id != -1) {
    Item p1_dropped;
    bool found = cart_remove_item_by_texture_id(
        c1, (unsigned int)ev->p1_dropped_texture_id, &p1_dropped);
    if (!found) {
      int tbl_idx = item_table_find_by_texture_id(
          (unsigned int)ev->p1_dropped_texture_id);
      if (tbl_idx != -1)
        p1_dropped = items_table[tbl_idx];
      else if (c1->items.len > 0)
        cart_remove_item(c1, c1->items.len - 1, &p1_dropped);
    }
    shopping_list_readd(get_player_shopping_list(p1_id), p1_dropped);
    p1_dropped.pos = (Vector2){ev->p1_drop_x, ev->p1_drop_y};
    p1_dropped.rot = mall_rand_float() * 2.0f * PI;
    itemarr_push(&items, p1_dropped);

    spawn_flying_drop(p1_dropped, c1->pos, p1_dropped.pos, p1_id);
    add_dropped_floor_item(p1_dropped.pos, (int)p1_dropped.image.id, p1_id);
    spawn_floating_text(c1->pos, "-1 DROPPED", (Color){255, 80, 80, 255});
  }

  // Player 2 drop
  if (ev->p2_dropped_texture_id != -1) {
    Item p2_dropped;
    bool found = cart_remove_item_by_texture_id(
        c2, (unsigned int)ev->p2_dropped_texture_id, &p2_dropped);
    if (!found) {
      int tbl_idx = item_table_find_by_texture_id(
          (unsigned int)ev->p2_dropped_texture_id);
      if (tbl_idx != -1)
        p2_dropped = items_table[tbl_idx];
      else if (c2->items.len > 0)
        cart_remove_item(c2, c2->items.len - 1, &p2_dropped);
    }
    shopping_list_readd(get_player_shopping_list(p2_id), p2_dropped);
    p2_dropped.pos = (Vector2){ev->p2_drop_x, ev->p2_drop_y};
    p2_dropped.rot = mall_rand_float() * 2.0f * PI;
    itemarr_push(&items, p2_dropped);

    spawn_flying_drop(p2_dropped, c2->pos, p2_dropped.pos, p2_id);
    add_dropped_floor_item(p2_dropped.pos, (int)p2_dropped.image.id, p2_id);
    spawn_floating_text(c2->pos, "-1 DROPPED", (Color){255, 80, 80, 255});
  }

  sync_local_shopping_list();

  if (p1_id == g_local_player_id || p2_id == g_local_player_id) {
    if (ev->transferred_texture_id != -1) {
      if (ev->faster_player_id == g_local_player_id) {
        snprintf(crash_toast_text, sizeof(crash_toast_text),
                 "CRASH! You stole an item from Opponent!");
      } else {
        snprintf(crash_toast_text, sizeof(crash_toast_text),
                 "CRASH! Opponent stole an item from you!");
      }
    } else {
      snprintf(crash_toast_text, sizeof(crash_toast_text),
               "CRASH! Groceries dropped on the floor!");
    }
    crash_toast_time = now;
  }
}

static void DrawTickFrame(void) {
  if (g_app_state == APP_STATE_LOBBY) {
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    float btn_w = 440.0f;
    if (btn_w > (float)sw - 40.0f) btn_w = (float)sw - 40.0f;
    float btn_h = 54.0f;
    float btn_x = ((float)sw - btn_w) * 0.5f;
    float btn_y = (float)sh * 0.5f + 65.0f;
    Rectangle btn_rect = (Rectangle){btn_x, btn_y, btn_w, btn_h};

    Vector2 mouse_pos = GetMousePosition();
    bool btn_hover = CheckCollisionPointRec(mouse_pos, btn_rect);
    if (btn_hover) {
      SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
    } else {
      SetMouseCursor(MOUSE_CURSOR_DEFAULT);
    }

    BeginDrawing();
    ClearBackground((Color){15, 23, 42, 255});

    const char *title = "gloBUS";
    int tw = MeasureText(title, 56);
    DrawText(title, sw / 2 - tw / 2, sh / 2 - 130, 56, (Color){96, 165, 250, 255});

    const char *sub = "Shopping Cart Simulator";
    int subw = MeasureText(sub, 20);
    DrawText(sub, sw / 2 - subw / 2, sh / 2 - 60, 20, (Color){148, 163, 184, 255});

#if defined(__EMSCRIPTEN__)
    const char *hint = "Use the lobby menu to create/join a room, or jump straight into solo practice!";
#else
    const char *hint = "Race your shopping cart through the aisles to collect groceries!";
#endif
    int hw = MeasureText(hint, 16);
    DrawText(hint, sw / 2 - hw / 2, sh / 2 - 10, 16, (Color){203, 213, 225, 255});

    const char *ctrls = "Controls: WASD or H/J/K/L to drive | SPACE/ENTER/E to pick items";
    int cw = MeasureText(ctrls, 14);
    DrawText(ctrls, sw / 2 - cw / 2, sh / 2 + 25, 14, (Color){100, 116, 139, 255});

    // Draw styled interactive button
    Color btn_bg = btn_hover ? (Color){37, 99, 235, 255} : (Color){29, 78, 216, 230};
    Color btn_border = btn_hover ? (Color){191, 219, 254, 255} : (Color){96, 165, 250, 255};
    DrawRectangleRounded(btn_rect, 0.28f, 6, btn_bg);
    DrawRectangleRoundedLinesEx(btn_rect, 0.28f, 6, 2.0f, btn_border);

    const char *btn_text = "🛒 Start Solo Practice";
    int btw = MeasureText(btn_text, 22);
    DrawText(btn_text, (int)(btn_x + (btn_w - btw) * 0.5f), (int)(btn_y + 16.0f), 22, WHITE);

    const char *key_hint = "Click button or press [SPACE] / [ENTER] / [1] to start";
    int khw = MeasureText(key_hint, 14);
    DrawText(key_hint, sw / 2 - khw / 2, (int)(btn_y + btn_h + 16.0f), 14, (Color){148, 163, 184, 255});

#if !defined(__EMSCRIPTEN__)
    const char *quit_hint = "Press [ESC] or [Q] to quit";
    int qhw = MeasureText(quit_hint, 13);
    DrawText(quit_hint, sw / 2 - qhw / 2, (int)(btn_y + btn_h + 40.0f), 13, (Color){100, 116, 139, 255});
#else
    const char *esc_hint = "Press [ESC] to toggle Web Lobby menu";
    int ehw = MeasureText(esc_hint, 13);
    DrawText(esc_hint, sw / 2 - ehw / 2, (int)(btn_y + btn_h + 40.0f), 13, (Color){100, 116, 139, 255});
#endif

    EndDrawing();

    bool should_start = false;
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1) ||
        IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) ||
        IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_E)) {
      should_start = true;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
      should_start = true;
    }
    if (GetTouchPointCount() > 0) {
      should_start = true;
    }

    if (should_start) {
      SetMouseCursor(MOUSE_CURSOR_DEFAULT);
      start_singleplayer();
      return;
    }

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_Q)) {
#if defined(__EMSCRIPTEN__)
      EM_ASM({
        if (window.onReturnToLobby) window.onReturnToLobby();
      });
#else
      CloseWindow();
      exit(0);
#endif
      return;
    }
    return;
  }

  if (IsKeyPressed(KEY_ESCAPE)) {
#if defined(__EMSCRIPTEN__)
    int just_exited_fs = EM_ASM_INT({
      return (window._justExitedFs && window._justExitedFs()) ? 1 : 0;
    });
    if (just_exited_fs) {
      // User pressed ESC to exit browser fullscreen, do not abandon match!
      return;
    }
#endif
    return_to_lobby();
    return;
  }

  float delta = GetFrameTime();
  if (delta > 0.1f) delta = 0.1f;
  float now = (float)GetTime();

  // Process networking
  if (g_net.mode != NET_MODE_SINGLE) {
    // Client connection handshake retry
    if (g_net.mode == NET_MODE_CLIENT && g_net.state == NET_STATE_CONNECTING) {
      if (now - g_net.last_send_time > 0.4f) {
        net_send_hello();
        g_net.last_send_time = now;
      }
    }

    // Ping / keepalive every second
    if (g_net.state == NET_STATE_CONNECTED) {
      if (now - g_net.last_ping_time > 1.0f) {
        net_send_ping();
        g_net.last_ping_time = now;
      }
    }

    NetPacket pkt;
    while (net_poll(&pkt) > 0) {
      switch (pkt.header.type) {
      case PKT_HELLO:
        if (g_net.mode == NET_MODE_HOST) {
          uint8_t pid = pkt.hello.player_id;
          if (pid < 1 || pid > MAX_PLAYERS) pid = 2;
          int idx = pid - 1;
          g_players[idx].active = true;
          if (pkt.hello.player_name[0] != '\0') {
            snprintf(g_players[idx].name, sizeof(g_players[idx].name), "%s", pkt.hello.player_name);
          }
          if (g_config.game_mode == GAME_MODE_2V2) {
            g_players[idx].team_id = (pkt.hello.team_id > 0) ? pkt.hello.team_id : (pid <= 2 ? 1 : 2);
          } else {
            g_players[idx].team_id = pid;
          }
          has_remote_cart = true;

          uint8_t mask = 0;
          uint8_t teams[MAX_NET_PLAYERS];
          for (int i = 0; i < MAX_PLAYERS; i++) {
            if (g_players[i].active) mask |= (uint8_t)(1 << i);
            teams[i] = g_players[i].team_id;
          }
          if (winner != 0) {
            restart_match();
          } else {
            net_send_init_multi(net_seed, &g_config, shopping_list, shopping_list_len,
                                host_grid_mask, mask, teams);
          }
          g_net.state = NET_STATE_CONNECTING;
        }
        break;

      case PKT_INIT:
        if (g_net.mode == NET_MODE_CLIENT) {
          g_config = pkt.init.config;
          cleanup_game_world();
          init_game_world(pkt.init.seed, pkt.init.grid_mask);
          apply_remote_shopping_list(&pkt.init);

          float cx = (float)((g_config.mall_width > 0 ? g_config.mall_width : 24) / 2);
          float cy = (float)((g_config.mall_height > 0 ? g_config.mall_height : 24) / 2);

          for (int i = 0; i < MAX_PLAYERS; i++) {
            init_player_cart(i + 1, cx, cy);
            if (pkt.init.active_players_mask & (1 << i)) {
              g_players[i].active = true;
              if (pkt.init.player_teams[i] > 0) {
                g_players[i].team_id = pkt.init.player_teams[i];
              }
            }
          }
          g_players[g_local_player_id - 1].active = true;
          has_remote_cart = any_remote_player_active();
          last_cart_state_send_time = 0.0f;
          last_sent_pick_side = -1;
          net_send_ready();
          g_net.state = NET_STATE_CONNECTED;
        }
        break;

      case PKT_READY:
        if (g_net.mode == NET_MODE_HOST) {
          has_remote_cart = true;
          g_net.state = NET_STATE_CONNECTED;
        }
        break;

      case PKT_CART_STATE: {
        uint8_t pid = pkt.cart.player_id;
        if (pid >= 1 && pid <= MAX_PLAYERS && pid != g_local_player_id) {
          int idx = pid - 1;
          g_players[idx].active = true;
          g_players[idx].target_pos = (Vector2){pkt.cart.pos_x, pkt.cart.pos_y};
          g_players[idx].target_vel = (Vector2){pkt.cart.vel_x, pkt.cart.vel_y};
          g_players[idx].target_rot = pkt.cart.rot;
          g_players[idx].target_ang_vel = pkt.cart.ang_vel;
          g_players[idx].pick_side = pkt.cart.pick_side;
          g_players[idx].pick_target = (Vector2){pkt.cart.pick_pos_x, pkt.cart.pick_pos_y};
          g_players[idx].cart.mass = pkt.cart.mass;
          g_players[idx].last_packet_time = now;
          has_remote_cart = true;
        }
        break;
      }

      case PKT_BUMP:
        main_cart.vel = Vector2Add(
            main_cart.vel, (Vector2){pkt.bump.impulse_x, pkt.bump.impulse_y});
        main_cart.ang_vel += pkt.bump.ang_impulse;
        break;

      case PKT_COLLISION_EVENT:
        if (g_net.mode == NET_MODE_CLIENT && world_initialized) {
          handle_client_collision_event(&pkt.collision);
        }
        break;

      case PKT_ITEM_PICK:
        if (world_initialized) {
          int picker_id = pkt.pick.player_id;
          if (picker_id < 1 || picker_id > MAX_PLAYERS) picker_id = 1;

          Vector2 target_pos = (Vector2){pkt.pick.pos_x, pkt.pick.pos_y};
          int found_idx = -1;
          float best_dist = 0.70f;
          for (int i = 0; i < items.len; i++) {
            float d = Vector2Distance(items.items[i].pos, target_pos);
            if (d < best_dist) {
              best_dist = d;
              found_idx = i;
            }
          }
          Cart *picker_cart = &g_players[picker_id - 1].cart;
          ShoppingList *picker_slist = get_player_shopping_list(picker_id);
          Item picked;
          if (found_idx != -1) {
            picked = cart_consume_item(picker_cart, &items, found_idx);
            shopping_list_consume(picker_slist, picked);
            remove_dropped_floor_item(picked.pos, (int)picked.image.id);
          } else {
            picked = (Item){.image = (Texture2D){.id = pkt.pick.texture_id}};
            shopping_list_consume(picker_slist, picked);
          }
          sync_local_shopping_list();

          if (g_config.game_mode == GAME_MODE_2V2) {
            int team = g_players[picker_id - 1].team_id;
            bool is_my_team = (team == g_players[g_local_player_id - 1].team_id);
            Color toast_c = (team == 1) ? (Color){96, 165, 250, 255} : (Color){248, 113, 113, 255};
            spawn_shockwave(target_pos, 0.45f, toast_c);
            if (is_my_team && picker_id != g_local_player_id) {
              spawn_floating_text(picker_cart->pos, "+1 TEAM ITEM!", GOLD);
            }
            if (picker_slist->len == 0 && winner == 0) {
              winner = team;
              winning_time = ela_time;
            }
          } else {
            spawn_shockwave(target_pos, 0.45f, PLAYER_BODY_COLORS[picker_id - 1]);
            if (picker_slist->len == 0 && winner == 0) {
              winner = picker_id;
              winning_time = ela_time;
            }
          }
        }
        break;

      case PKT_GAME_OVER:
        if (winner == 0 || winning_time == 0.0f ||
            pkt.game_over.final_time > 0.0f) {
          if (g_config.game_mode == GAME_MODE_2V2 && pkt.game_over.winning_team > 0) {
            winner = pkt.game_over.winning_team;
            winning_time = pkt.game_over.final_time;
            team_shopping_lists[winner].len = 0;
          } else {
            winner = pkt.game_over.winner_id;
            winning_time = pkt.game_over.final_time;
            if (winner >= 1 && winner <= MAX_PLAYERS) {
              g_players[winner - 1].shopping_list.len = 0;
            }
          }
        }
        break;

      case PKT_PING: {
        PktPingPong pong = {
            .header = {.type = PKT_PONG},
            .timestamp_ms = pkt.ping.timestamp_ms,
        };
        net_send(&pong, sizeof(pong));
        break;
      }

      case PKT_PONG: {
        uint32_t now_ms = (uint32_t)(GetTime() * 1000.0);
        if (now_ms >= pkt.ping.timestamp_ms) {
          g_net.rtt_ms = (float)(now_ms - pkt.ping.timestamp_ms);
        }
        break;
      }
      }
    }
  }

  // If client waiting for world init from host
  if (g_net.mode == NET_MODE_CLIENT && !world_initialized) {
    BeginDrawing();
    ClearBackground(RAYWHITE);
#if defined(__EMSCRIPTEN__)
    const char *status_str = TextFormat("Connecting to Room %s...", g_net.remote_ip);
    const char *sub_str = "Waiting for game world init from host...";
#else
    const char *status_str = TextFormat("Connecting to Host at %s:%d...",
                                        g_net.remote_ip, g_net.remote_port);
    const char *sub_str =
        "Waiting for game world init from host (Tailscale)...";
#endif
    int sw = MeasureText(status_str, 24);
    DrawText(status_str, GetScreenWidth() / 2 - sw / 2,
             GetScreenHeight() / 2 - 20, 24, DARKBLUE);

    int ssw = MeasureText(sub_str, 16);
    DrawText(sub_str, GetScreenWidth() / 2 - ssw / 2,
             GetScreenHeight() / 2 + 15, 16, GRAY);

    const char *esc_hint = "Press [ESC] to return to Lobby";
    int ehw = MeasureText(esc_hint, 14);
    DrawText(esc_hint, GetScreenWidth() / 2 - ehw / 2,
             GetScreenHeight() / 2 + 45, 14, DARKGRAY);

    EndDrawing();
    return;
  }

  // Tank controls: H/J (left hand), L/K (right hand)
  if (IsKeyDown(KEY_K)) {
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE}, delta);
  }

  if (IsKeyDown(KEY_J)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE}, delta);
  }

  if (IsKeyDown(KEY_H)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE}, delta);
  }

  if (IsKeyDown(KEY_L)) {
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE}, delta);
  }

  // Gamepad controls
  if (IsGamepadAvailable(0)) {
    Vector2 left_stick = {
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X),
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y),
    };
    Vector2 right_stick = {
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X),
        GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y),
    };
    if (Vector2LengthSqr(left_stick) > 0.04f) {
      cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                               Vector2Scale(left_stick, CART_PUSH_FORCE),
                               delta);
    }
    if (Vector2LengthSqr(right_stick) > 0.04f) {
      cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                               Vector2Scale(right_stick, CART_PUSH_FORCE),
                               delta);
    }
  }

  // Standard WASD controls
  if (IsKeyDown(KEY_W)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.5f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.5f}, delta);
  }

  if (IsKeyDown(KEY_S)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.5f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.5f}, delta);
  }

  if (IsKeyDown(KEY_A)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.9f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.9f}, delta);
  }

  if (IsKeyDown(KEY_D)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.9f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.9f}, delta);
  }

  if (IsKeyDown(KEY_O))
    cam.zoom /= 1.01f;

  if (IsKeyDown(KEY_P))
    cam.zoom *= 1.01f;

  if (IsKeyPressed(KEY_I))
    rot_follow = !rot_follow;

  Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), cam);

  // Local physics simulation
  cart_tick(&main_cart, walls, walls.len, delta);

  // Remote carts smooth interpolation and cart-on-cart collisions
  has_remote_cart = any_remote_player_active();
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (i == g_local_player_id - 1 || !g_players[i].active) continue;

    PlayerSlot *p = &g_players[i];
    float time_since_packet = now - p->last_packet_time;
    if (time_since_packet > 0.016f && time_since_packet < 0.75f) {
      p->target_pos = Vector2Add(p->target_pos, Vector2Scale(p->target_vel, delta));
      p->target_rot += p->target_ang_vel * delta;
      if (time_since_packet > 0.25f) {
        float damp = fmaxf(0.0f, 1.0f - 3.5f * delta);
        p->target_vel = Vector2Scale(p->target_vel, damp);
        p->target_ang_vel *= damp;
      }
    }

    float dist = Vector2Distance(p->cart.pos, p->target_pos);
    if (dist > 4.5f) {
      p->cart.pos = p->target_pos;
      p->cart.rot = p->target_rot;
    } else {
      float blend = 1.0f - expf(-18.0f * delta);
      if (blend > 0.65f) blend = 0.65f;
      p->cart.pos = Vector2Lerp(p->cart.pos, p->target_pos, blend);
      p->cart.rot = LerpAngle(p->cart.rot, p->target_rot, blend);
    }
    p->cart.vel = p->target_vel;
    p->cart.ang_vel = p->target_ang_vel;

    // Local cart vs Remote cart collision
    Vector2 bump_impulse = Vector2Zero();
    float bump_ang = 0.0f;
    if (cart_collide_cart(&main_cart, &p->cart, &bump_impulse, &bump_ang)) {
      p->target_vel = p->cart.vel;
      p->target_pos = p->cart.pos;
      if (g_net.mode == NET_MODE_HOST) {
        handle_host_cart_collision(g_local_player_id, i + 1, bump_impulse, bump_ang);
      }
    }
  }

  // Host simulates remote vs remote collisions
  if (g_net.mode == NET_MODE_HOST) {
    for (int a = 0; a < MAX_PLAYERS; a++) {
      if (a == g_local_player_id - 1 || !g_players[a].active) continue;
      for (int b = a + 1; b < MAX_PLAYERS; b++) {
        if (b == g_local_player_id - 1 || !g_players[b].active) continue;
        Vector2 bump_impulse = Vector2Zero();
        float bump_ang = 0.0f;
        if (cart_collide_cart(&g_players[a].cart, &g_players[b].cart, &bump_impulse, &bump_ang)) {
          g_players[a].target_vel = g_players[a].cart.vel;
          g_players[a].target_pos = g_players[a].cart.pos;
          g_players[b].target_vel = g_players[b].cart.vel;
          g_players[b].target_pos = g_players[b].cart.pos;
          handle_host_cart_collision(a + 1, b + 1, bump_impulse, bump_ang);
        }
      }
    }
  }

  // Item pickup highlight logic
  pick_item_idx = -1;
  pick_side = -1;
  float min_dist = FLT_MAX;
  Vector2 driver = Vector2Add(
      main_cart.pos, Vector2Rotate(main_cart.driver_pos, main_cart.rot));
  Vector2 lhaldle = Vector2Add(
      main_cart.pos, Vector2Rotate(main_cart.collider.items[1], main_cart.rot));
  Vector2 rhaldle = Vector2Add(
      main_cart.pos, Vector2Rotate(main_cart.collider.items[2], main_cart.rot));

  for (int i = 0; i < items.len; i++) {
    Item item = items.items[i];
    float ddist_driver = Vector2Distance(item.pos, driver);
    float ddist_cart = Vector2Distance(item.pos, main_cart.pos);
    float ddist = fminf(ddist_driver, ddist_cart);
    float reach_radius =
        fmaxf(item.collision_radius, 0.40f) + main_cart.driver_radius + 0.15f;
    if (ddist < reach_radius && ddist < min_dist) {
      min_dist = ddist;
      pick_item_idx = i;
      if (Vector2Distance(item.pos, lhaldle) <
          Vector2Distance(item.pos, rhaldle))
        pick_side = 0;
      else
        pick_side = 1;
    }
  }

  // Local item pickup action (supports Enter, Space, E, and Gamepad A)
  bool pick_pressed = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                      IsKeyPressed(KEY_E) ||
                      IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
  if (winner == 0 && pick_pressed && pick_side != -1 && pick_item_idx >= 0 &&
      pick_item_idx < items.len) {
    Item picked = items.items[pick_item_idx];
    cart_consume_item(&main_cart, &items, pick_item_idx);
    remove_dropped_floor_item(picked.pos, (int)picked.image.id);

    ShoppingList *my_slist = get_player_shopping_list(g_local_player_id);
    bool consumed = shopping_list_consume(my_slist, picked);
    if (consumed) {
      sync_local_shopping_list();
    }

    Color toast_c = GREEN;
    if (g_config.game_mode == GAME_MODE_2V2) {
      int team = g_players[g_local_player_id - 1].team_id;
      toast_c = (team == 1) ? (Color){96, 165, 250, 255} : (Color){248, 113, 113, 255};
      spawn_floating_text(main_cart.pos, "+1 TEAM ITEM", toast_c);
    } else {
      spawn_floating_text(main_cart.pos, "+1 ITEM", GREEN);
    }
    spawn_shockwave(picked.pos, 0.45f, toast_c);

    if (g_net.mode != NET_MODE_SINGLE) {
      net_send_item_pick((int)picked.image.id, picked.pos, g_local_player_id);
    }

    if (my_slist->len == 0 && winner == 0) {
      if (g_config.game_mode == GAME_MODE_2V2) {
        winner = g_players[g_local_player_id - 1].team_id;
        winning_time = ela_time;
        if (g_net.mode != NET_MODE_SINGLE) {
          net_send_game_over_multi((uint8_t)g_local_player_id, winning_time, (uint8_t)winner);
        }
      } else {
        winner = g_local_player_id;
        winning_time = ela_time;
        if (g_net.mode != NET_MODE_SINGLE) {
          net_send_game_over_multi((uint8_t)winner, winning_time, 0);
        }
      }
    }

    pick_item_idx = -1;
    pick_side = -1;
  }

  // Rematch / Reset hotkey (R key or Gamepad Start / Y or Space/Enter on game over)
  bool rematch_pressed =
      IsKeyPressed(KEY_R) ||
      IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
      IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP) ||
      (winner != 0 && (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)));
  if (rematch_pressed) {
    restart_match();
  }

  // Stream local cart state to network (~30 Hz to prevent TCP buffer bloat over WebSockets)
  if (g_net.state == NET_STATE_CONNECTED && world_initialized) {
    if (now - last_cart_state_send_time >= 0.033f || pick_side != last_sent_pick_side) {
      last_cart_state_send_time = now;
      last_sent_pick_side = pick_side;
      Vector2 pick_target =
          (pick_side != -1 && pick_item_idx >= 0 && pick_item_idx < items.len)
              ? items.items[pick_item_idx].pos
              : Vector2Zero();
      net_send_cart_state_multi(main_cart.pos, main_cart.vel, main_cart.rot,
                                main_cart.ang_vel, pick_side, pick_target,
                                main_cart.mass, (uint8_t)g_local_player_id);
    }
  }

  float wheel = GetMouseWheelMove();
  if (wheel != 0.0f) {
    cam.zoom += wheel * 10.0f;
  }
  if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
    cam.zoom += 15.0f;
  }
  if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
    cam.zoom -= 15.0f;
  }
  if (cam.zoom < 60.0f)
    cam.zoom = 60.0f;
  if (cam.zoom > 220.0f)
    cam.zoom = 220.0f;

  cam.target = main_cart.pos;
  if (rot_follow)
    cam.rotation = -main_cart.rot * RAD2DEG;
  else
    cam.rotation = 0.0f;
  cam.offset = (Vector2){(float)GetScreenWidth() / 2.0f,
                         (float)GetScreenHeight() / 2.0f};

  Vector2 topleft = GetScreenToWorld2D((Vector2){0, 0}, cam);
  Vector2 botright = GetScreenToWorld2D(
      (Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, cam);

  if (world_initialized &&
      (g_net.mode == NET_MODE_SINGLE ||
       (g_net.state == NET_STATE_CONNECTED && has_remote_cart))) {
    if (race_start_time == 0.0f) {
      race_start_time = (float)GetTime();
    }
    if (winner == 0) {
      ela_time = (float)GetTime() - race_start_time;
    }
  }

  BeginDrawing();
  Color bg_clear = RAYWHITE;
  Color tile_col = LIGHTGRAY;
  switch (g_config.floor_theme) {
    case 1: // Warm Terrazzo
      bg_clear = (Color){245, 238, 226, 255};
      tile_col = (Color){232, 220, 202, 255};
      break;
    case 2: // Midnight Cyber
      bg_clear = (Color){22, 24, 34, 255};
      tile_col = (Color){34, 38, 52, 255};
      break;
    case 3: // Fresh Mint
      bg_clear = (Color){232, 246, 238, 255};
      tile_col = (Color){212, 236, 222, 255};
      break;
    default: // Classic Supermarket
      bg_clear = RAYWHITE;
      tile_col = LIGHTGRAY;
      break;
  }
  ClearBackground(bg_clear);
  BeginMode2D(cam);

  int mw = (g_config.mall_width >= MALL_MIN_DIM) ? g_config.mall_width : MALL_WIDTH;
  int mh = (g_config.mall_height >= MALL_MIN_DIM) ? g_config.mall_height : MALL_HEIGHT;

  // Background checkerboard
  for (int i = -10; i < mw + 10; i++) {
    for (int j = -10; j < mh + 10; j++) {
      int posx = i;
      int posy = j;
      if ((posx + posy) % 2 == 0)
        continue;
      DrawRectangleV((Vector2){(float)posx, (float)posy}, (Vector2){1.0f, 1.0f},
                     tile_col);
    }
  }

  wallarr_draw(walls, topleft, botright);
  island_shelfarr_draw(shelves, topleft, botright);
  island_posterarr_draw(posters, topleft, botright);

  if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && dragging) {
    walls.items[sel_wall].collider.items[sel_vrt] = mouse;
    wall_calculate_max_dist(walls.items + sel_wall);
    DrawCircleV(mouse, 0.1f, ORANGE);
  }

  // Draw floor shockwaves
  for (int i = 0; i < MAX_SHOCKWAVES; i++) {
    if (!shockwaves[i].active)
      continue;
    float elapsed = now - shockwaves[i].start_time;
    if (elapsed >= shockwaves[i].duration) {
      shockwaves[i].active = false;
      continue;
    }
    float p = elapsed / shockwaves[i].duration;
    float r = shockwaves[i].max_radius * p;
    float alpha = 1.0f - p;
    Color c = shockwaves[i].color;
    c.a = (unsigned char)(255.0f * alpha);
    float thick = 0.04f * (1.0f - p * 0.5f);
    DrawRing(shockwaves[i].pos, fmaxf(r - thick, 0.0f), r, 0.0f, 360.0f, 32, c);
  }

  // Draw dropped item floor beacons / glowing auras
  for (int i = 0; i < MAX_DROPPED_FLOOR_ITEMS; i++) {
    if (!dropped_floor_items[i].active)
      continue;
    Vector2 pos = dropped_floor_items[i].pos;
    int owner = dropped_floor_items[i].owner_player_id;
    Color base_col = (owner == 1) ? (Color){40, 130, 255, 255}
                                  : (Color){240, 50, 50, 255};
    float pulse = 0.5f + 0.5f * sinf(now * 5.0f);
    float rad = 0.32f + 0.06f * pulse;

    // Glowing inner circle
    Color fill_col = base_col;
    fill_col.a = (unsigned char)(70 + 40 * pulse);
    DrawCircleV(pos, rad, fill_col);

    // Pulsing outer ring
    Color ring_col = base_col;
    ring_col.a = (unsigned char)(180 + 75 * pulse);
    DrawRing(pos, rad - 0.025f, rad, 0.0f, 360.0f, 32, ring_col);

    // 4 orbital energy dots rotating around the item
    for (int k = 0; k < 4; k++) {
      float ang = now * 2.8f + (float)k * (PI * 0.5f);
      Vector2 dot_pos = {pos.x + cosf(ang) * (rad + 0.05f),
                         pos.y + sinf(ang) * (rad + 0.05f)};
      DrawCircleV(dot_pos, 0.03f, ring_col);
    }
  }

  // Draw ground shadows for flying items
  for (int i = 0; i < MAX_FLYING_DROPS; i++) {
    if (!flying_drops[i].active)
      continue;
    float elapsed = now - flying_drops[i].start_time;
    if (elapsed >= flying_drops[i].duration)
      continue;
    float p = elapsed / flying_drops[i].duration;
    Vector2 ground_pos = Vector2Lerp(flying_drops[i].start_pos,
                                     flying_drops[i].end_pos, p);
    float h = 4.0f * 0.85f * p * (1.0f - p);
    float sh_rad = 0.16f * (1.0f - h * 0.4f);
    DrawCircleV(ground_pos, fmaxf(sh_rad, 0.04f),
                (Color){0, 0, 0, (unsigned char)(120 * (1.0f - h * 0.5f))});
  }

  // Draw all active carts
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!g_players[i].active) continue;
    if (i == g_local_player_id - 1) {
      cart_draw(&main_cart, pick_side,
                pick_item_idx >= 0 ? &items.items[pick_item_idx] : NULL,
                topleft, botright);
    } else {
      Item rem_item = {.pos = g_players[i].pick_target};
      Item *p_rem = (g_players[i].pick_side != -1) ? &rem_item : NULL;
      cart_draw(&g_players[i].cart, g_players[i].pick_side, p_rem,
                topleft, botright);
    }
  }

  itemarr_draw(items, Vector2Zero(), 0.0f, topleft, botright);

  // Draw flying dropped items in parabolic arc
  for (int i = 0; i < MAX_FLYING_DROPS; i++) {
    if (!flying_drops[i].active)
      continue;
    float elapsed = now - flying_drops[i].start_time;
    if (elapsed >= flying_drops[i].duration) {
      flying_drops[i].active = false;
      Color land_col = (flying_drops[i].owner_player_id == 1)
                           ? (Color){60, 150, 255, 255}
                           : (Color){255, 60, 60, 255};
      spawn_shockwave(flying_drops[i].end_pos, 0.65f, land_col);
      continue;
    }
    float p = elapsed / flying_drops[i].duration;
    Vector2 ground_pos = Vector2Lerp(flying_drops[i].start_pos,
                                     flying_drops[i].end_pos, p);
    float h = 4.0f * 0.85f * p * (1.0f - p);
    Item flying_item = flying_drops[i].item;
    flying_item.pos = (Vector2){ground_pos.x, ground_pos.y - h};
    flying_item.rot = flying_item.rot + p * 4.0f * PI;
    flying_item.scale *= (1.0f + 0.35f * sinf(p * PI));
    item_draw(flying_item, Vector2Zero(), 0.0f);
  }

  // Draw hands for all active carts
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!g_players[i].active) continue;
    if (i == g_local_player_id - 1) {
      cart_draw_hands(&main_cart, pick_side,
                      pick_item_idx >= 0 ? &items.items[pick_item_idx] : NULL);
    } else {
      Item rem_item = {.pos = g_players[i].pick_target};
      Item *p_rem = (g_players[i].pick_side != -1) ? &rem_item : NULL;
      cart_draw_hands(&g_players[i].cart, g_players[i].pick_side, p_rem);
    }
  }

  EndMode2D();

  // Draw overhead player name badges in screen coordinates
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (!g_players[i].active) continue;
    Vector2 cp = (i == g_local_player_id - 1) ? main_cart.pos : g_players[i].cart.pos;
    Vector2 sp = GetWorldToScreen2D(cp, cam);
    if (sp.x < -100 || sp.x > GetScreenWidth() + 100 || sp.y < -100 || sp.y > GetScreenHeight() + 100) continue;

    char tag[48];
    Color badge_bg, badge_border;
    bool is_local = (i == g_local_player_id - 1);

    if (g_config.game_mode == GAME_MODE_2V2) {
      int team = g_players[i].team_id;
      const char *team_name = (team == 1) ? "BLUE" : "RED";
      if (is_local) {
        snprintf(tag, sizeof(tag), "[%s] P%d (YOU)", team_name, i + 1);
        badge_bg = (team == 1) ? (Color){30, 70, 160, 230} : (Color){160, 30, 30, 230};
        badge_border = GOLD;
      } else {
        bool is_teammate = (team == g_players[g_local_player_id - 1].team_id);
        snprintf(tag, sizeof(tag), "[%s] P%d %s", team_name, i + 1, is_teammate ? "(TEAM)" : "(RIVAL)");
        badge_bg = (team == 1) ? (Color){30, 70, 160, 200} : (Color){160, 30, 30, 200};
        badge_border = is_teammate ? (Color){147, 197, 253, 255} : (Color){252, 165, 165, 255};
      }
    } else {
      if (is_local) {
        snprintf(tag, sizeof(tag), "P%d (YOU)", i + 1);
        badge_bg = (Color){30, 41, 59, 230};
        badge_border = GOLD;
      } else {
        snprintf(tag, sizeof(tag), "P%d", i + 1);
        badge_bg = (Color){30, 41, 59, 200};
        badge_border = (Color){148, 163, 184, 255};
      }
    }

    int font_sz = 12;
    int tw = MeasureText(tag, font_sz);
    float bw = tw + 14.0f;
    float bh = 18.0f;
    float bx = sp.x - bw * 0.5f;
    float by = sp.y - 48.0f;

    DrawRectangleRounded((Rectangle){bx, by, bw, bh}, 0.35f, 4, badge_bg);
    DrawRectangleRoundedLinesEx((Rectangle){bx, by, bw, bh}, 0.35f, 4, 1.5f, badge_border);
    DrawText(tag, (int)(bx + 7.0f), (int)(by + 3.0f), font_sz, WHITE);
  }

  // Floating badges above dropped floor items
  for (int i = 0; i < MAX_DROPPED_FLOOR_ITEMS; i++) {
    if (!dropped_floor_items[i].active)
      continue;
    Vector2 sp = GetWorldToScreen2D(dropped_floor_items[i].pos, cam);
    if (sp.x < -100 || sp.x > GetScreenWidth() + 100 || sp.y < -100 ||
        sp.y > GetScreenHeight() + 100)
      continue;

    float bob = sinf(now * 5.5f) * 4.0f;
    float badge_y = sp.y - 42.0f + bob;

    bool is_targeted = (pick_item_idx >= 0 &&
                        pick_item_idx < items.len &&
                        items.items[pick_item_idx].image.id ==
                            (unsigned int)dropped_floor_items[i].texture_id);

    if (is_targeted) {
      // Golden glowing "PICK UP [E]" prompt
      const char *prompt = "PICK UP [E]";
      int font_sz = 14;
      int tw = MeasureText(prompt, font_sz);
      float bw = tw + 20.0f;
      float bh = 22.0f;
      float bx = sp.x - bw * 0.5f;

      DrawRectangleRounded((Rectangle){bx, badge_y, bw, bh}, 0.35f, 4,
                           (Color){255, 230, 80, 240});
      DrawRectangleRoundedLinesEx((Rectangle){bx, badge_y, bw, bh}, 0.35f, 4,
                                  2.0f, (Color){200, 130, 0, 255});
      DrawText(prompt, (int)(bx + 10.0f), (int)(badge_y + 4.0f), font_sz,
               BLACK);

      // Downward pointer arrow
      DrawTriangle((Vector2){sp.x - 5.0f, badge_y + bh},
                   (Vector2){sp.x + 5.0f, badge_y + bh},
                   (Vector2){sp.x, badge_y + bh + 6.0f},
                   (Color){200, 130, 0, 255});
    } else {
      // Owner-tinted "DROPPED" badge
      int owner = dropped_floor_items[i].owner_player_id;
      Color bg_col = (owner == 1) ? (Color){30, 90, 200, 220}
                                  : (Color){180, 30, 30, 220};
      Color border_col = (owner == 1) ? (Color){100, 180, 255, 255}
                                      : (Color){255, 120, 120, 255};
      const char *label = "DROPPED";
      int font_sz = 11;
      int tw = MeasureText(label, font_sz);
      float bw = tw + 14.0f;
      float bh = 17.0f;
      float bx = sp.x - bw * 0.5f;

      DrawRectangleRounded((Rectangle){bx, badge_y, bw, bh}, 0.35f, 4, bg_col);
      DrawRectangleRoundedLinesEx((Rectangle){bx, badge_y, bw, bh}, 0.35f, 4,
                                  1.5f, border_col);
      DrawText(label, (int)(bx + 7.0f), (int)(badge_y + 3.0f), font_sz, WHITE);

      DrawTriangle((Vector2){sp.x - 4.0f, badge_y + bh},
                   (Vector2){sp.x + 4.0f, badge_y + bh},
                   (Vector2){sp.x, badge_y + bh + 4.0f}, border_col);
    }
  }

  // Floating text effects drifting upward
  for (int i = 0; i < MAX_FLOATING_TEXTS; i++) {
    if (!floating_texts[i].active)
      continue;
    float elapsed = now - floating_texts[i].start_time;
    if (elapsed >= floating_texts[i].duration) {
      floating_texts[i].active = false;
      continue;
    }
    float p = elapsed / floating_texts[i].duration;
    Vector2 world_pos = {floating_texts[i].pos.x,
                         floating_texts[i].pos.y - p * 0.9f};
    Vector2 sp = GetWorldToScreen2D(world_pos, cam);
    float alpha = 1.0f - p;
    Color c = floating_texts[i].color;
    c.a = (unsigned char)(255.0f * alpha);

    int font_sz = 18;
    int tw = MeasureText(floating_texts[i].text, font_sz);
    float bx = sp.x - tw * 0.5f;
    float by = sp.y;

    Color shadow_c = BLACK;
    shadow_c.a = (unsigned char)(160.0f * alpha);
    DrawText(floating_texts[i].text, (int)(bx + 1.0f), (int)(by + 1.0f), font_sz,
             shadow_c);
    DrawText(floating_texts[i].text, (int)bx, (int)by, font_sz, c);
  }

  // --- SHOPPING LIST HUD ---
  float top_y = (g_net.mode == NET_MODE_SINGLE) ? 20.0f : 34.0f;
  float spacing = 8.0f;
  float avail_h = (float)GetScreenHeight() - top_y - 20.0f;
  float item_height = (avail_h - spacing * (SHOPPING_LIST_MAX_LEN - 1)) /
                      (float)SHOPPING_LIST_MAX_LEN;
  if (item_height > 65.0f) item_height = 65.0f;
  if (item_height < 38.0f) item_height = 38.0f;

  if (g_config.game_mode == GAME_MODE_2V2) {
    // 2v2 Mode HUD: Team Blue vs Team Red
    int t1_rem = team_shopping_lists[1].len;
    DrawText(TextFormat("TEAM BLUE (%d)", t1_rem), 20, 12, 16, (Color){30, 70, 160, 255});
    for (int i = 0; i < t1_rem; i++) {
      Item item = items_table[team_shopping_lists[1].items[i]];
      float scale = item_height / (float)item.image.height;
      DrawTextureEx(item.image, (Vector2){20, top_y + (item_height + spacing) * i}, 0.0f, scale, WHITE);
    }

    int t2_rem = team_shopping_lists[2].len;
    const char *t2_hdr = TextFormat("TEAM RED (%d)", t2_rem);
    int t2_hdr_w = MeasureText(t2_hdr, 16);
    DrawText(t2_hdr, GetScreenWidth() - t2_hdr_w - 20, 12, 16, (Color){180, 30, 30, 255});
    for (int i = 0; i < t2_rem; i++) {
      Item opp_item = items_table[team_shopping_lists[2].items[i]];
      float scale = item_height / (float)opp_item.image.height;
      float drawn_w = (float)opp_item.image.width * scale;
      float opp_x = (float)GetScreenWidth() - drawn_w - 20.0f;
      DrawTextureEx(opp_item.image, (Vector2){opp_x, top_y + (item_height + spacing) * i}, 0.0f, scale, WHITE);
    }

    // Top center banner: Team scores & live lead
    float badge_w = 400.0f;
    float badge_h = 44.0f;
    float badge_x = ((float)GetScreenWidth() - badge_w) * 0.5f;
    DrawRectangleRounded((Rectangle){badge_x, 8.0f, badge_w, badge_h}, 0.25f, 4, (Color){255, 255, 255, 230});
    DrawRectangleRoundedLinesEx((Rectangle){badge_x, 8.0f, badge_w, badge_h}, 0.25f, 4, 2.0f, DARKGRAY);

    const char *time_str = TextFormat("TIME: %.3f s", ela_time);
    DrawText(time_str, (int)(badge_x + (badge_w - MeasureText(time_str, 18)) * 0.5f), 12, 18, BLACK);

    if (winner == 0) {
      int my_team = g_players[g_local_player_id - 1].team_id;
      int my_rem = team_shopping_lists[my_team].len;
      int opp_rem = team_shopping_lists[my_team == 1 ? 2 : 1].len;
      if (my_rem < opp_rem) {
        int lead = opp_rem - my_rem;
        const char *lead_str = TextFormat("YOUR TEAM LEADS by %d item%s!", lead, lead > 1 ? "s" : "");
        DrawText(lead_str, (int)(badge_x + (badge_w - MeasureText(lead_str, 13)) * 0.5f), 32, 13, DARKGREEN);
      } else if (my_rem > opp_rem) {
        int behind = my_rem - opp_rem;
        const char *behind_str = TextFormat("RIVAL TEAM LEADS by %d item%s!", behind, behind > 1 ? "s" : "");
        DrawText(behind_str, (int)(badge_x + (badge_w - MeasureText(behind_str, 13)) * 0.5f), 32, 13, MAROON);
      } else {
        const char *tied_str = TextFormat("TIED 2v2 RACE! (%d items each)", my_rem);
        DrawText(tied_str, (int)(badge_x + (badge_w - MeasureText(tied_str, 13)) * 0.5f), 32, 13, DARKBLUE);
      }
    } else {
      int my_team = g_players[g_local_player_id - 1].team_id;
      const char *fin_str = (winner == my_team) ? "RACE FINISHED - TEAM VICTORY!" : "RACE FINISHED - RIVAL TEAM WON!";
      DrawText(fin_str, (int)(badge_x + (badge_w - MeasureText(fin_str, 13)) * 0.5f), 32, 13, (winner == my_team) ? DARKGREEN : MAROON);
    }

    const char *ping_str = TextFormat("Ping: %.0f ms", g_net.rtt_ms);
    DrawText(ping_str, (int)(badge_x + badge_w + 14.0f), 20, 14, DARKGRAY);

  } else if (g_net.mode != NET_MODE_SINGLE) {
    // FFA Multiplayer Mode HUD
    ShoppingList *my_slist = get_player_shopping_list(g_local_player_id);
    DrawText(TextFormat("YOU (%d)", my_slist->len), 20, 12, 16, PLAYER_OUTLINE_COLORS[g_local_player_id - 1]);
    for (int i = 0; i < my_slist->len; i++) {
      Item item = items_table[my_slist->items[i]];
      float scale = item_height / (float)item.image.height;
      DrawTextureEx(item.image, (Vector2){20, top_y + (item_height + spacing) * i}, 0.0f, scale, WHITE);
    }

    int best_opp_id = -1;
    int min_opp_rem = 999;
    for (int i = 0; i < MAX_PLAYERS; i++) {
      if (i != g_local_player_id - 1 && g_players[i].active) {
        if (g_players[i].shopping_list.len < min_opp_rem) {
          min_opp_rem = g_players[i].shopping_list.len;
          best_opp_id = i + 1;
        }
      }
    }

    if (best_opp_id != -1) {
      ShoppingList *opp_slist = &g_players[best_opp_id - 1].shopping_list;
      const char *opp_hdr = TextFormat("P%d (%d items)", best_opp_id, opp_slist->len);
      int opp_hdr_w = MeasureText(opp_hdr, 16);
      DrawText(opp_hdr, GetScreenWidth() - opp_hdr_w - 20, 12, 16, PLAYER_OUTLINE_COLORS[best_opp_id - 1]);
      for (int i = 0; i < opp_slist->len; i++) {
        Item opp_item = items_table[opp_slist->items[i]];
        float scale = item_height / (float)opp_item.image.height;
        float drawn_w = (float)opp_item.image.width * scale;
        float opp_x = (float)GetScreenWidth() - drawn_w - 20.0f;
        DrawTextureEx(opp_item.image, (Vector2){opp_x, top_y + (item_height + spacing) * i}, 0.0f, scale, WHITE);
      }
    }

    float badge_w = 380.0f;
    float badge_h = 44.0f;
    float badge_x = ((float)GetScreenWidth() - badge_w) * 0.5f;
    DrawRectangleRounded((Rectangle){badge_x, 8.0f, badge_w, badge_h}, 0.25f, 4, (Color){255, 255, 255, 230});
    DrawRectangleRoundedLinesEx((Rectangle){badge_x, 8.0f, badge_w, badge_h}, 0.25f, 4, 2.0f, DARKGRAY);

    const char *time_str = TextFormat("TIME: %.3f s", ela_time);
    DrawText(time_str, (int)(badge_x + (badge_w - MeasureText(time_str, 18)) * 0.5f), 12, 18, BLACK);

    if (winner == 0) {
      if (best_opp_id != -1) {
        if (my_slist->len < min_opp_rem) {
          int lead = min_opp_rem - my_slist->len;
          const char *lead_str = TextFormat("YOU'RE LEADING by %d item%s!", lead, lead > 1 ? "s" : "");
          DrawText(lead_str, (int)(badge_x + (badge_w - MeasureText(lead_str, 13)) * 0.5f), 32, 13, DARKGREEN);
        } else if (my_slist->len > min_opp_rem) {
          int behind = my_slist->len - min_opp_rem;
          const char *behind_str = TextFormat("P%d LEADING by %d item%s!", best_opp_id, behind, behind > 1 ? "s" : "");
          DrawText(behind_str, (int)(badge_x + (badge_w - MeasureText(behind_str, 13)) * 0.5f), 32, 13, MAROON);
        } else {
          const char *tied_str = TextFormat("TIED RACE! (%d items each)", my_slist->len);
          DrawText(tied_str, (int)(badge_x + (badge_w - MeasureText(tied_str, 13)) * 0.5f), 32, 13, DARKBLUE);
        }
      }
    } else {
      const char *fin_str = (winner == g_local_player_id) ? "RACE FINISHED - YOU WON!"
                                                          : TextFormat("RACE FINISHED - P%d WON!", winner);
      DrawText(fin_str, (int)(badge_x + (badge_w - MeasureText(fin_str, 13)) * 0.5f), 32, 13, (winner == g_local_player_id) ? DARKGREEN : MAROON);
    }

    const char *ping_str = TextFormat("Ping: %.0f ms", g_net.rtt_ms);
    DrawText(ping_str, (int)(badge_x + badge_w + 14.0f), 20, 14, DARKGRAY);

  } else {
    // Singleplayer HUD (original layout)
    ShoppingList *my_slist = get_player_shopping_list(1);
    for (int i = 0; i < my_slist->len; i++) {
      Item item = items_table[my_slist->items[i]];
      float scale = item_height / (float)item.image.height;
      DrawTextureEx(item.image, (Vector2){20, top_y + (item_height + spacing) * i}, 0.0f, scale, WHITE);
    }

    const char *string = TextFormat("%.3f", ela_time);
    const char *string_zeros = TextFormat("%.3f", floorf(ela_time));
    int string_width_zeros = MeasureText(string_zeros, 30);
    DrawText(string, GetScreenWidth() - string_width_zeros - 10,
             GetScreenHeight() - 40, 30, BLACK);
  }

  // Crash toast notification
  if (now - crash_toast_time < 2.5f && crash_toast_text[0] != '\0') {
    float toast_w = 400.0f;
    float toast_h = 28.0f;
    float toast_x = ((float)GetScreenWidth() - toast_w) * 0.5f;
    float toast_y = 56.0f;
    DrawRectangleRounded((Rectangle){toast_x, toast_y, toast_w, toast_h},
                         0.25f, 4, (Color){255, 245, 230, 240});
    DrawRectangleRoundedLinesEx(
        (Rectangle){toast_x, toast_y, toast_w, toast_h}, 0.25f, 4, 1.5f,
        ORANGE);
    DrawText(
        crash_toast_text,
        (int)(toast_x + (toast_w - MeasureText(crash_toast_text, 14)) * 0.5f),
        (int)(toast_y + 7.0f), 14, DARKGRAY);
  }

  // Host waiting banner
  if (g_net.mode == NET_MODE_HOST && !has_remote_cart) {
    float badge_w = 460.0f;
    float badge_x = ((float)GetScreenWidth() - badge_w) * 0.5f;
    DrawRectangleRounded((Rectangle){badge_x, 8.0f, badge_w, 36.0f}, 0.25f, 4,
                         (Color){255, 255, 255, 230});
    DrawRectangleRoundedLinesEx((Rectangle){badge_x, 8.0f, badge_w, 36.0f},
                                0.25f, 4, 1.5f, DARKBLUE);
#if defined(__EMSCRIPTEN__)
    const char *wait_str =
        TextFormat("ROOM %s - Waiting for players to join...",
                   g_net.remote_ip);
#else
    const char *wait_str =
        TextFormat("HOSTING (port %d) - Waiting for players to join...",
                   g_net.remote_port);
#endif
    DrawText(wait_str,
             (int)(badge_x + (badge_w - MeasureText(wait_str, 15)) * 0.5f), 18,
             15, DARKBLUE);
  }

  // --- GAME OVER / VICTORY / DEFEAT MODAL ---
  if (winner != 0) {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){0, 0, 0, 130});
    float mw = 520.0f;
    float mh = 240.0f;
    float mx = ((float)GetScreenWidth() - mw) * 0.5f;
    float my = ((float)GetScreenHeight() - mh) * 0.5f;

    DrawRectangleRounded((Rectangle){mx, my, mw, mh}, 0.10f, 4, RAYWHITE);

    if (g_net.mode == NET_MODE_SINGLE) {
      DrawRectangleRoundedLinesEx((Rectangle){mx, my, mw, mh}, 0.10f, 4, 4.0f,
                                  GOLD);
      const char *title = "SHOPPING COMPLETE!";
      DrawText(title, (int)(mx + (mw - MeasureText(title, 32)) * 0.5f),
               (int)(my + 28), 32, GOLD);
      const char *sub = "All groceries successfully collected!";
      DrawText(sub, (int)(mx + (mw - MeasureText(sub, 17)) * 0.5f),
               (int)(my + 74), 17, DARKGRAY);
      const char *t_str = TextFormat("Final Time: %.3f seconds", winning_time);
      DrawText(t_str, (int)(mx + (mw - MeasureText(t_str, 20)) * 0.5f),
               (int)(my + 115), 20, BLACK);
      const char *rem_str = "Press [R] or [SPACE] to Play Again  |  [ESC] Lobby";
      DrawText(rem_str, (int)(mx + (mw - MeasureText(rem_str, 18)) * 0.5f),
               (int)(my + 172), 18, DARKBLUE);
    } else if (g_config.game_mode == GAME_MODE_2V2) {
      int my_team = g_players[g_local_player_id - 1].team_id;
      bool won = (winner == my_team);
      Color border_col = won ? GOLD : MAROON;
      DrawRectangleRoundedLinesEx((Rectangle){mx, my, mw, mh}, 0.10f, 4, 4.0f,
                                  border_col);

      const char *title = won ? "TEAM VICTORY!" : "DEFEAT!";
      Color title_col = won ? GOLD : RED;
      DrawText(title, (int)(mx + (mw - MeasureText(title, 38)) * 0.5f),
               (int)(my + 24), 38, title_col);

      const char *team_str = (winner == 1) ? "Team Blue" : "Team Red";
      const char *sub = won ? TextFormat("Your team (%s) completed the shopping list first!", team_str)
                            : TextFormat("Rival %s completed their shopping list first!", team_str);
      Color sub_col = won ? DARKGREEN : DARKGRAY;
      DrawText(sub, (int)(mx + (mw - MeasureText(sub, 17)) * 0.5f),
               (int)(my + 74), 17, sub_col);

      const char *t_str =
          TextFormat("Winning Time: %.3f seconds", winning_time);
      DrawText(t_str, (int)(mx + (mw - MeasureText(t_str, 20)) * 0.5f),
               (int)(my + 116), 20, BLACK);

      const char *rem_str = "";
      if (g_net.mode == NET_MODE_HOST) {
        rem_str = "Press [R] to Start Rematch  |  [ESC] Lobby";
      } else {
        rem_str = rematch_requested ? "Rematch Requested! Waiting for Host...  |  [ESC] Lobby"
                                    : "Press [R] to Request Rematch  |  [ESC] Lobby";
      }
      DrawText(rem_str, (int)(mx + (mw - MeasureText(rem_str, 18)) * 0.5f),
               (int)(my + 172), 18, DARKBLUE);
    } else {
      bool won = (winner == g_local_player_id);
      Color border_col = won ? GOLD : MAROON;
      DrawRectangleRoundedLinesEx((Rectangle){mx, my, mw, mh}, 0.10f, 4, 4.0f,
                                  border_col);

      const char *title = won ? "VICTORY!" : "DEFEAT!";
      Color title_col = won ? GOLD : RED;
      DrawText(title, (int)(mx + (mw - MeasureText(title, 38)) * 0.5f),
               (int)(my + 24), 38, title_col);

      const char *sub = won ? "You finished your shopping list first!"
                            : TextFormat("Player %d finished their shopping list first!", winner);
      Color sub_col = won ? DARKGREEN : DARKGRAY;
      DrawText(sub, (int)(mx + (mw - MeasureText(sub, 17)) * 0.5f),
               (int)(my + 74), 17, sub_col);

      const char *t_str =
          TextFormat("Winning Time: %.3f seconds", winning_time);
      DrawText(t_str, (int)(mx + (mw - MeasureText(t_str, 20)) * 0.5f),
               (int)(my + 116), 20, BLACK);

      const char *rem_str = "";
      if (g_net.mode == NET_MODE_HOST) {
        rem_str = "Press [R] to Start Rematch  |  [ESC] Lobby";
      } else {
        rem_str = rematch_requested ? "Rematch Requested! Waiting for Host...  |  [ESC] Lobby"
                                    : "Press [R] to Request Rematch  |  [ESC] Lobby";
      }
      DrawText(rem_str, (int)(mx + (mw - MeasureText(rem_str, 18)) * 0.5f),
               (int)(my + 172), 18, DARKBLUE);
    }
  }

  EndDrawing();

  if (screenshot_path) {
    if (screenshot_countdown > 0) {
      screenshot_countdown--;
    } else if (screenshot_countdown == 0) {
      TakeScreenshot(screenshot_path);
      screenshot_countdown = -2;
    }
  }
}

int main(int argc, char **argv) {
  g_config = room_config_default();
#if defined(__EMSCRIPTEN__)
  (void)argc;
  (void)argv;
  SetTraceLogLevel(LOG_INFO);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "gloBUS");
  SetTargetFPS(60);

  cam = (Camera2D){
      (Vector2){(float)GetScreenWidth() / 2.0f,
                (float)GetScreenHeight() / 2.0f},
      (Vector2){0.0f, 0.0f},
      0.0f,
      120.0f,
  };

  g_app_state = APP_STATE_LOBBY;
  emscripten_set_main_loop(DrawTickFrame, 0, 1);
  return 0;
#else
  int port = DEFAULT_NET_PORT;
  const char *join_ip = NULL;
  NetMode mode = NET_MODE_SINGLE;
  int cli_player_id = 0;
  int cli_team_id = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--host") == 0 || strcmp(argv[i], "-h") == 0) {
      mode = NET_MODE_HOST;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        port = atoi(argv[++i]);
      }
    } else if (strcmp(argv[i], "--join") == 0 ||
               strcmp(argv[i], "--connect") == 0 ||
               strcmp(argv[i], "-j") == 0) {
      mode = NET_MODE_CLIENT;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        join_ip = argv[++i];
      }
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        port = atoi(argv[++i]);
      }
    } else if (strcmp(argv[i], "--2v2") == 0 || strcmp(argv[i], "--teams") == 0) {
      g_config.game_mode = GAME_MODE_2V2;
    } else if (strcmp(argv[i], "--ffa") == 0) {
      g_config.game_mode = GAME_MODE_FFA;
    } else if (strcmp(argv[i], "--players") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        int p = atoi(argv[++i]);
        if (p < 2) p = 2;
        if (p > MAX_PLAYERS) p = MAX_PLAYERS;
        g_config.max_players = (uint8_t)p;
      }
    } else if (strcmp(argv[i], "--player") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        cli_player_id = atoi(argv[++i]);
      }
    } else if (strcmp(argv[i], "--team") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        cli_team_id = atoi(argv[++i]);
      }
    } else if (strcmp(argv[i], "--size") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        int sz = atoi(argv[++i]);
        if (sz < MALL_MIN_DIM) sz = MALL_MIN_DIM;
        if (sz > MALL_MAX_WIDTH) sz = MALL_MAX_WIDTH;
        g_config.mall_width = (uint8_t)sz;
        g_config.mall_height = (uint8_t)sz;
      }
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        int sz_h = atoi(argv[++i]);
        if (sz_h < MALL_MIN_DIM) sz_h = MALL_MIN_DIM;
        if (sz_h > MALL_MAX_HEIGHT) sz_h = MALL_MAX_HEIGHT;
        g_config.mall_height = (uint8_t)sz_h;
      }
    } else if (strcmp(argv[i], "--items") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        int cnt = atoi(argv[++i]);
        if (cnt < 1) cnt = 1;
        if (cnt > SHOPPING_LIST_MAX_LEN) cnt = SHOPPING_LIST_MAX_LEN;
        g_config.shopping_list_count = (uint8_t)cnt;
      }
    } else if (strcmp(argv[i], "--wfc") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        const char *w_arg = argv[++i];
        if (strcmp(w_arg, "classic") == 0 || strcmp(w_arg, "0") == 0) g_config.wfc_preset = WFC_PRESET_ID_CLASSIC;
        else if (strcmp(w_arg, "aisles") == 0 || strcmp(w_arg, "1") == 0) g_config.wfc_preset = WFC_PRESET_ID_AISLES;
        else if (strcmp(w_arg, "warehouse") == 0 || strcmp(w_arg, "2") == 0) g_config.wfc_preset = WFC_PRESET_ID_WAREHOUSE;
        else if (strcmp(w_arg, "maze") == 0 || strcmp(w_arg, "3") == 0) g_config.wfc_preset = WFC_PRESET_ID_MAZE;
        else if (strcmp(w_arg, "plaza") == 0 || strcmp(w_arg, "4") == 0) g_config.wfc_preset = WFC_PRESET_ID_PLAZA;
        else g_config.wfc_preset = (uint8_t)atoi(w_arg);
      }
    } else if (strcmp(argv[i], "--theme") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.floor_theme = (uint8_t)atoi(argv[++i]);
      }
    } else if (strcmp(argv[i], "--force") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.push_force = (float)atof(argv[++i]);
      }
    } else if (strcmp(argv[i], "--friction") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.roll_friction = (float)atof(argv[++i]);
      }
    } else if (strcmp(argv[i], "--grip") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.lateral_grip = (float)atof(argv[++i]);
      }
    } else if (strcmp(argv[i], "--mass") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.cart_mass = (float)atof(argv[++i]);
      }
    } else if (strcmp(argv[i], "--drift") == 0) {
      g_config.push_force = 62.0f;
      g_config.lateral_grip = 0.35f;
      g_config.roll_friction = 0.025f;
      g_config.angular_friction = 1.2f;
    } else if (strcmp(argv[i], "--demolition") == 0) {
      g_config.cart_mass = 65.0f;
      g_config.crash_impulse_mult = 2.0f;
      g_config.floor_drop_count = 3;
      g_config.wall_restitution = 0.65f;
    } else if (strcmp(argv[i], "--sprint") == 0) {
      g_config.mall_width = 18;
      g_config.mall_height = 18;
      g_config.shopping_list_count = 3;
      g_config.push_force = 55.0f;
    } else if (strcmp(argv[i], "--no-theft") == 0) {
      g_config.theft_mode = 0;
    } else if (strcmp(argv[i], "--drops") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.floor_drop_count = (uint8_t)atoi(argv[++i]);
      }
    } else if (strcmp(argv[i], "--restitution") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        g_config.wall_restitution = (float)atof(argv[++i]);
      }
    } else if (strcmp(argv[i], "--screenshot") == 0) {
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        screenshot_path = argv[++i];
        screenshot_countdown = 5;
      }
    } else if (strcmp(argv[i], "--help") == 0) {
      printf("gloBUS - Shopping Cart Simulator\n");
      printf("Usage:\n");
      printf("  ./main                      Singleplayer\n");
      printf(
          "  ./main --host [port]        Host multiplayer game (default: %d)\n",
          DEFAULT_NET_PORT);
      printf("  ./main --join <IP> [port]   Join game over Tailscale / LAN\n");
      printf("Game Mode Options:\n");
      printf("  --2v2, --teams              2v2 Team mode (cooperate on shared shopping list)\n");
      printf("  --ffa                       Free-for-all mode (individual shopping lists)\n");
      printf("  --players <2..4>            Max players in room (default: 4)\n");
      printf("  --player <1..4>             Set local player slot ID (default: 1 host/solo, 2 client)\n");
      printf("  --team <1..2>               Set local team (1=Blue, 2=Red)\n");
      printf("Room Customization Options (for host & singleplayer):\n");
      printf("  --size <W> [H]              Supermarket grid dimensions 16..36 (default: 24)\n");
      printf("  --items <N>                 Shopping list count 1..16 (default: 5)\n");
      printf("  --wfc <preset>              WFC preset: classic, aisles, warehouse, maze, plaza (0..4)\n");
      printf("  --theme <0..3>              Floor theme: 0=Classic, 1=Terrazzo, 2=Midnight, 3=Mint\n");
      printf("  --force <float>             Cart push force (default: 45.0)\n");
      printf("  --friction <float>          Roll friction (default: 0.05)\n");
      printf("  --grip <float>              Wheel lateral grip (default: 1.0, lower=driftier)\n");
      printf("  --mass <float>              Cart mass in kg (default: 35.0)\n");
      printf("  --drift                     Preset: Tokyo Drift handling\n");
      printf("  --demolition                Preset: Heavy carts, max bump impulse, drop all\n");
      printf("  --sprint                    Preset: Compact 18x18 mall, 3 items, fast pace\n");
      printf("  --no-theft                  Disable item stealing on collision\n");
      printf("  --drops <0..3>              Floor drop items per crash (0=none, 1=one, 2=two, 3=all)\n");
      printf("  --restitution <float>       Wall bounciness (default: 0.20)\n");
      printf("  --screenshot <file>         Save screenshot after a few frames and exit\n");
      return 0;
    }
  }

  if (mode == NET_MODE_CLIENT && !join_ip) {
    fprintf(stderr, "Error: --join requires host IP address (e.g. ./main "
                    "--join 100.x.y.z)\n");
    return 1;
  }

  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "gloBUS");
  SetTargetFPS(60);

  cam = (Camera2D){
      (Vector2){(float)GetScreenWidth() / 2.0f,
                (float)GetScreenHeight() / 2.0f},
      (Vector2){0.0f, 0.0f},
      0.0f,
      120.0f,
  };

  if (cli_player_id >= 1 && cli_player_id <= MAX_PLAYERS) {
    g_local_player_id = cli_player_id;
  }

  if (mode == NET_MODE_HOST) {
    start_host(NULL, NULL, port);
  } else if (mode == NET_MODE_CLIENT) {
    start_client(join_ip, NULL, port);
  } else {
    start_singleplayer();
  }

  if (cli_team_id == 1 || cli_team_id == 2) {
    g_players[g_local_player_id - 1].team_id = (uint8_t)cli_team_id;
  }

  while (!WindowShouldClose()) {
    DrawTickFrame();
    if (screenshot_path && screenshot_countdown == -2) {
      break;
    }
  }

  cleanup_game_world();
  free_carts();
  net_close();
  for (int i = 0; i < NUM_ITEMS; i++) {
    item_free(items_table[i]);
  }

  CloseWindow();
  return 0;
#endif
}
