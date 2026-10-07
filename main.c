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

static Cart main_cart;
static Cart remote_cart;
static bool has_remote_cart = false;
static int remote_pick_side = -1;
static int remote_pick_item_idx = -1;
static Vector2 remote_target_pos;
static Vector2 remote_target_vel;
static float remote_target_rot = 0.0f;
static float remote_target_ang_vel = 0.0f;

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
static bool world_initialized = false;
static uint32_t net_seed = 0;

static float LerpAngle(float a, float b, float t) {
  float diff = fmodf(b - a + PI, 2.0f * PI) - PI;
  if (diff < -PI)
    diff += 2.0f * PI;
  return a + diff * t;
}

static void init_game_world(uint32_t seed) {
  if (world_initialized)
    return;
  net_seed = seed;
  srand(seed);

  fill_items_table();

  items = itemarr_new();
  walls = wallarr_new();
  posters = island_posterarr_new();
  shelves = island_shelfarr_new();
  mall_generation("res/wfc.png", &walls, &items, &posters, &shelves);

  printf("%d items loaded, %d shelves, %d posters generated (seed: %u)\n",
         items.len, shelves.len, posters.len, seed);
  fflush(stdout);

  fill_shopping_list();
  world_initialized = true;
}

static void apply_remote_shopping_list(const PktInit *init) {
  shopping_list_len = init->shopping_list_count;
  for (int i = 0; i < shopping_list_len && i < SHOPPING_LIST_MAX_LEN; i++) {
    shopping_list[i] = init->shopping_list[i];
  }
}

static void DrawTickFrame(void) {
  float delta = GetFrameTime();
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
          net_send_init(net_seed, shopping_list, shopping_list_len);
          g_net.state = NET_STATE_CONNECTING;
        }
        break;

      case PKT_INIT:
        if (g_net.mode == NET_MODE_CLIENT) {
          init_game_world(pkt.init.seed);
          apply_remote_shopping_list(&pkt.init);
          has_remote_cart = true;
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

      case PKT_CART_STATE:
        if (has_remote_cart) {
          remote_target_pos = (Vector2){pkt.cart.pos_x, pkt.cart.pos_y};
          remote_target_vel = (Vector2){pkt.cart.vel_x, pkt.cart.vel_y};
          remote_target_rot = pkt.cart.rot;
          remote_target_ang_vel = pkt.cart.ang_vel;
          remote_pick_side = pkt.cart.pick_side;
          remote_pick_item_idx = pkt.cart.pick_item_idx;
          remote_cart.mass = pkt.cart.mass;
        }
        break;

      case PKT_ITEM_PICK:
        if (world_initialized) {
          Vector2 target_pos = (Vector2){pkt.pick.pos_x, pkt.pick.pos_y};
          int found_idx = -1;
          float best_dist = 0.45f;
          for (int i = 0; i < items.len; i++) {
            float d = Vector2Distance(items.items[i].pos, target_pos);
            if (d < best_dist) {
              best_dist = d;
              found_idx = i;
            }
          }
          if (found_idx != -1) {
            cart_consume_item(&remote_cart, &items, found_idx);
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
    const char *status_str = TextFormat("Connecting to Host at %s:%d...",
                                        g_net.remote_ip, g_net.remote_port);
    int sw = MeasureText(status_str, 24);
    DrawText(status_str, GetScreenWidth() / 2 - sw / 2,
             GetScreenHeight() / 2 - 20, 24, DARKBLUE);

    const char *sub_str = "Waiting for game world init from host (Tailscale)...";
    int ssw = MeasureText(sub_str, 16);
    DrawText(sub_str, GetScreenWidth() / 2 - ssw / 2,
             GetScreenHeight() / 2 + 15, 16, GRAY);
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
                               Vector2Scale(left_stick, CART_PUSH_FORCE), delta);
    }
    if (Vector2LengthSqr(right_stick) > 0.04f) {
      cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                               Vector2Scale(right_stick, CART_PUSH_FORCE), delta);
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
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.6f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.2f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.0f, 0.27f},
                             (Vector2){CART_PUSH_FORCE * 0.35f, 0.0f}, delta);
  }

  if (IsKeyDown(KEY_D)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.6f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.2f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.0f, 0.27f},
                             (Vector2){-CART_PUSH_FORCE * 0.35f, 0.0f}, delta);
  }

  if (IsKeyDown(KEY_O))
    cam.zoom /= 1.01f;

  if (IsKeyDown(KEY_P))
    cam.zoom *= 1.01f;

  if (IsKeyPressed(KEY_I))
    rot_follow = !rot_follow;

  Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), cam);

  if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    bool set_idx = false;
    for (int i = 0; i < walls.len; i++) {
      Wall wall = walls.items[i];
      for (int j = 0; j < wall.collider.len; j++) {
        Vector2 vrt = wall.collider.items[j];
        if (Vector2Distance(vrt, mouse) < 0.3f) {
          if (sel_vrt == j && sel_wall == i) {
            dragging = true;
            break;
          }
          sel_vrt = j;
          sel_wall = i;
          set_idx = true;
          break;
        }
      }
      if (set_idx)
        break;
    }
  }

  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && dragging) {
    dragging = false;
  }

  // Local physics simulation
  cart_tick(&main_cart, walls, walls.len, delta);

  // Remote cart smooth interpolation and cart-on-cart collision
  if (has_remote_cart) {
    remote_cart.pos = Vector2Lerp(remote_cart.pos, remote_target_pos, 0.40f);
    remote_cart.vel = remote_target_vel;
    remote_cart.rot = LerpAngle(remote_cart.rot, remote_target_rot, 0.40f);
    remote_cart.ang_vel = remote_target_ang_vel;

    // Cart-on-cart bumping physics
    cart_collide_cart(&main_cart, &remote_cart);
  }

  // Stream local cart state to network
  if (g_net.state == NET_STATE_CONNECTED && world_initialized) {
    net_send_cart_state(main_cart.pos, main_cart.vel, main_cart.rot,
                        main_cart.ang_vel, pick_side, pick_item_idx,
                        main_cart.mass);
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
    float ddist = Vector2Distance(item.pos, driver);
    if (ddist < item.collision_radius + main_cart.driver_radius &&
        ddist < min_dist) {
      min_dist = ddist;
      pick_item_idx = i;
      if (Vector2Distance(item.pos, lhaldle) <
          Vector2Distance(item.pos, rhaldle))
        pick_side = 0;
      else
        pick_side = 1;
    }
  }

  // Local item pickup action
  if ((IsKeyPressed(KEY_ENTER) ||
       IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) &&
      pick_side != -1 && pick_item_idx >= 0 && pick_item_idx < items.len) {
    Item picked = items.items[pick_item_idx];
    cart_consume_item(&main_cart, &items, pick_item_idx);

    if (g_net.mode != NET_MODE_SINGLE) {
      net_send_item_pick(picked.image.id, picked.pos,
                         g_net.mode == NET_MODE_HOST ? 0 : 1);
    }
    pick_item_idx = -1;
    pick_side = -1;
  }

  float wheel = GetMouseWheelMove();
  if (wheel != 0.0f) {
    cam.zoom += wheel * 10.0f;
    if (cam.zoom < 60.0f)
      cam.zoom = 60.0f;
    if (cam.zoom > 160.0f)
      cam.zoom = 160.0f;
  }

  cam.target = main_cart.pos;
  if (rot_follow)
    cam.rotation = -main_cart.rot * RAD2DEG;
  else
    cam.rotation = 0.0f;
  cam.offset =
      (Vector2){(float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f};

  Vector2 topleft = GetScreenToWorld2D((Vector2){0, 0}, cam);
  Vector2 botright =
      GetScreenToWorld2D((Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()}, cam);

  if (shopping_list_len > 0) {
    ela_time = (float)GetTime();
  }

  BeginDrawing();
  ClearBackground(RAYWHITE);
  BeginMode2D(cam);

  // Background checkerboard
  for (int i = -10; i < MALL_WIDTH + 10; i++) {
    for (int j = -10; j < MALL_HEIGHT + 10; j++) {
      int posx = i;
      int posy = j;
      if ((posx + posy) % 2 == 0)
        continue;
      DrawRectangleV((Vector2){(float)posx, (float)posy}, (Vector2){1.0f, 1.0f},
                     LIGHTGRAY);
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

  // Draw remote cart
  if (has_remote_cart) {
    Item *rem_item = (remote_pick_item_idx >= 0 && remote_pick_item_idx < items.len)
                         ? &items.items[remote_pick_item_idx]
                         : NULL;
    cart_draw(&remote_cart, remote_pick_side, rem_item, topleft, botright);
  }

  // Draw local cart
  cart_draw(&main_cart, pick_side,
            pick_item_idx >= 0 ? &items.items[pick_item_idx] : NULL, topleft,
            botright);

  itemarr_draw(items, Vector2Zero(), 0.0f, topleft, botright);

  // Draw hands
  if (has_remote_cart) {
    Item *rem_item = (remote_pick_item_idx >= 0 && remote_pick_item_idx < items.len)
                         ? &items.items[remote_pick_item_idx]
                         : NULL;
    cart_draw_hands(&remote_cart, remote_pick_side, rem_item);
  }
  cart_draw_hands(&main_cart, pick_side,
                  pick_item_idx >= 0 ? &items.items[pick_item_idx] : NULL);

  EndMode2D();

  // Shopping list HUD
  float item_height = (GetScreenHeight() - 20 * (SHOPPING_LIST_MAX_LEN + 1)) /
                      (float)SHOPPING_LIST_MAX_LEN;
  for (int i = 0; i < shopping_list_len; i++) {
    Item item = items_table[shopping_list[i]];
    float scale = item_height / (float)item.image.height;
    DrawTextureEx(item.image, (Vector2){20, 20 + item_height * i + 20 * i}, 0.0f,
                  scale, WHITE);
  }

  // Elapsed timer HUD
  const char *string = TextFormat("%.3f", ela_time);
  const char *string_zeros = TextFormat("%.3f", floorf(ela_time));
  int string_width_zeros = MeasureText(string_zeros, 30);
  DrawText(string, GetScreenWidth() - string_width_zeros - 10,
           GetScreenHeight() - 40, 30, BLACK);

  // Multiplayer Status Banner
  if (g_net.mode == NET_MODE_HOST) {
    if (g_net.state == NET_STATE_CONNECTED) {
      DrawText(TextFormat("MULTIPLAYER: P1 (Blue, You) | P2 (Red, Connected) | Ping: %.0f ms",
                          g_net.rtt_ms),
               20, 10, 18, DARKGREEN);
    } else {
      DrawText(TextFormat("HOSTING on port %d - Waiting for Player 2...",
                          g_net.remote_port),
               20, 10, 18, DARKBLUE);
    }
  } else if (g_net.mode == NET_MODE_CLIENT) {
    if (g_net.state == NET_STATE_CONNECTED) {
      DrawText(TextFormat("MULTIPLAYER: P2 (Red, You) | P1 (Blue, Host) | Ping: %.0f ms",
                          g_net.rtt_ms),
               20, 10, 18, DARKGREEN);
    }
  }

  // Shopping list completion message
  if (shopping_list_len == 0) {
    const char *done_text = "SHOPPING LIST COMPLETE!";
    int tw = MeasureText(done_text, 36);
    DrawText(done_text, GetScreenWidth() / 2 - tw / 2, 60, 36, GOLD);
  }

  EndDrawing();
}

int main(int argc, char **argv) {
  int port = DEFAULT_NET_PORT;
  const char *join_ip = NULL;
  NetMode mode = NET_MODE_SINGLE;

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
    } else if (strcmp(argv[i], "--help") == 0) {
      printf("gloBUS - Shopping Cart Simulator\n");
      printf("Usage:\n");
      printf("  ./main                      Singleplayer\n");
      printf("  ./main --host [port]        Host multiplayer game (default: %d)\n",
             DEFAULT_NET_PORT);
      printf("  ./main --join <IP> [port]   Join game over Tailscale / LAN\n");
      return 0;
    }
  }

  if (mode == NET_MODE_CLIENT && !join_ip) {
    fprintf(stderr,
            "Error: --join requires host IP address (e.g. ./main --join 100.x.y.z)\n");
    return 1;
  }

  SetTraceLogLevel(LOG_WARNING);
  SetConfigFlags(FLAG_WINDOW_RESIZABLE);
  InitWindow(800, 600, "gloBUS");
  SetTargetFPS(60);

  // Initialize carts based on role
  if (mode == NET_MODE_HOST) {
    net_init_host(port);

    // Player 1 (Blue)
    main_cart =
        cart_new_goofy(DARKBLUE, BLUE, BLACK, Vector2Zero(), 4,
                       (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
                       (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
    main_cart.pos = (Vector2){9.5f, 10.0f};

    // Player 2 (Red)
    remote_cart =
        cart_new_goofy(MAROON, RED, DARKGRAY, Vector2Zero(), 4,
                       (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
                       (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
    remote_cart.pos = (Vector2){10.5f, 10.0f};
    remote_target_pos = remote_cart.pos;

    init_game_world((uint32_t)time(NULL));
  } else if (mode == NET_MODE_CLIENT) {
    net_init_client(join_ip, port);

    // Local player is Player 2 (Red)
    main_cart =
        cart_new_goofy(MAROON, RED, DARKGRAY, Vector2Zero(), 4,
                       (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
                       (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
    main_cart.pos = (Vector2){10.5f, 10.0f};

    // Remote player is Player 1 (Blue)
    remote_cart =
        cart_new_goofy(DARKBLUE, BLUE, BLACK, Vector2Zero(), 4,
                       (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
                       (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
    remote_cart.pos = (Vector2){9.5f, 10.0f};
    remote_target_pos = remote_cart.pos;

    // Load base textures now so world init later is instantaneous
    fill_items_table();
  } else {
    // Singleplayer (Blue)
    main_cart =
        cart_new_goofy(DARKBLUE, BLUE, BLACK, Vector2Zero(), 4,
                       (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
                       (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
    main_cart.pos = (Vector2){10.0f, 10.0f};

    init_game_world((uint32_t)time(NULL));
  }

  cam = (Camera2D){
      (Vector2){(float)GetScreenWidth() / 2.0f, (float)GetScreenHeight() / 2.0f},
      (Vector2){0.0f, 0.0f},
      0.0f,
      100.0f,
  };

  while (!WindowShouldClose()) {
    DrawTickFrame();
  }

  if (world_initialized) {
    island_shelfarr_free(&shelves);
    island_posterarr_free(&posters);
    itemarr_free_all(&items);
    wallarr_free_all(&walls);
  }
  cart_free(&main_cart);
  if (mode != NET_MODE_SINGLE) {
    cart_free(&remote_cart);
    net_close();
  }
  for (int i = 0; i < NUM_ITEMS; i++) {
    item_free(items_table[i]);
  }

  CloseWindow();
  return 0;
}
