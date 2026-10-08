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
static Vector2 remote_pick_pos = {0};
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
static float race_start_time = 0.0f;
static bool world_initialized = false;
static uint32_t net_seed = 0;
static uint8_t host_grid_mask[MALL_GRID_MASK_SIZE] = {0};

static ShoppingList local_shopping_list = {0};
static ShoppingList remote_shopping_list = {0};
static int winner =
    0; // 0 = Ongoing race, 1 = Player 1 (Blue), 2 = Player 2 (Red)
static float winning_time = 0.0f;
static bool rematch_requested = false;
static const char *screenshot_path = NULL;
static int screenshot_countdown = -1;

static float LerpAngle(float a, float b, float t) {
  float diff = fmodf(b - a + PI, 2.0f * PI) - PI;
  if (diff < -PI)
    diff += 2.0f * PI;
  return a + diff * t;
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
  mall_generation("res/wfc.png", &walls, &items, &posters, &shelves, seed,
                  in_grid_mask, host_grid_mask);

  printf("%d items loaded, %d shelves, %d posters generated (seed: %u)\n",
         items.len, shelves.len, posters.len, seed);
  fflush(stdout);

  if (in_grid_mask == NULL) {
    fill_shopping_list();
    local_shopping_list.len = shopping_list_len;
    local_shopping_list.initial_len = shopping_list_len;
    for (int i = 0; i < shopping_list_len; i++) {
      local_shopping_list.items[i] = shopping_list[i];
      remote_shopping_list.items[i] = shopping_list[i];
    }
    remote_shopping_list.len = shopping_list_len;
    remote_shopping_list.initial_len = shopping_list_len;
  }
  winner = 0;
  winning_time = 0.0f;
  race_start_time = 0.0f;
  ela_time = 0.0f;
  rematch_requested = false;
  world_initialized = true;
}

static void apply_remote_shopping_list(const PktInit *init) {
  shopping_list_len = init->shopping_list_count;
  for (int i = 0; i < shopping_list_len && i < SHOPPING_LIST_MAX_LEN; i++) {
    shopping_list[i] = init->shopping_list[i];
    local_shopping_list.items[i] = init->shopping_list[i];
    remote_shopping_list.items[i] = init->shopping_list[i];
  }
  local_shopping_list.len = shopping_list_len;
  local_shopping_list.initial_len = shopping_list_len;
  remote_shopping_list.len = shopping_list_len;
  remote_shopping_list.initial_len = shopping_list_len;
  winner = 0;
  winning_time = 0.0f;
  race_start_time = 0.0f;
  ela_time = 0.0f;
  rematch_requested = false;
}

static void cleanup_game_world(void) {
  if (world_initialized) {
    island_shelfarr_free(&shelves);
    island_posterarr_free(&posters);
    itemarr_free_all(&items);
    wallarr_free_all(&walls);
    itemarr_free_all(&main_cart.items);
    main_cart.mass = main_cart.base_mass;
    main_cart.mom_inertia = main_cart.base_mom_inertia;
    if (g_net.mode != NET_MODE_SINGLE) {
      itemarr_free_all(&remote_cart.items);
      remote_cart.mass = remote_cart.base_mass;
      remote_cart.mom_inertia = remote_cart.base_mom_inertia;
    }
    world_initialized = false;
  }
}

static void restart_match(void) {
  if (g_net.mode == NET_MODE_SINGLE) {
    cleanup_game_world();
    main_cart.pos = (Vector2){10.0f, 10.0f};
    main_cart.vel = Vector2Zero();
    main_cart.rot = 0.0f;
    main_cart.ang_vel = 0.0f;
    init_game_world((uint32_t)time(NULL), NULL);
  } else if (g_net.mode == NET_MODE_HOST) {
    cleanup_game_world();
    main_cart.pos = (Vector2){9.5f, 10.0f};
    main_cart.vel = Vector2Zero();
    main_cart.rot = 0.0f;
    main_cart.ang_vel = 0.0f;
    remote_cart.pos = (Vector2){10.5f, 10.0f};
    remote_cart.vel = Vector2Zero();
    remote_cart.rot = 0.0f;
    remote_cart.ang_vel = 0.0f;
    remote_target_pos = remote_cart.pos;
    remote_target_vel = Vector2Zero();
    remote_target_rot = 0.0f;
    remote_target_ang_vel = 0.0f;
    remote_pick_side = -1;

    uint32_t new_seed = (uint32_t)time(NULL);
    init_game_world(new_seed, NULL);
    if (g_net.state == NET_STATE_CONNECTED) {
      net_send_init(new_seed, shopping_list, shopping_list_len, host_grid_mask);
    }
  } else if (g_net.mode == NET_MODE_CLIENT) {
    rematch_requested = true;
    net_send_hello();
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
          if (winner != 0) {
            restart_match();
          } else {
            net_send_init(net_seed, shopping_list, shopping_list_len,
                          host_grid_mask);
          }
          g_net.state = NET_STATE_CONNECTING;
        }
        break;

      case PKT_INIT:
        if (g_net.mode == NET_MODE_CLIENT) {
          cleanup_game_world();
          init_game_world(pkt.init.seed, pkt.init.grid_mask);
          apply_remote_shopping_list(&pkt.init);
          main_cart.pos = (Vector2){10.5f, 10.0f};
          main_cart.vel = Vector2Zero();
          main_cart.rot = 0.0f;
          main_cart.ang_vel = 0.0f;
          remote_cart.pos = (Vector2){9.5f, 10.0f};
          remote_cart.vel = Vector2Zero();
          remote_cart.rot = 0.0f;
          remote_cart.ang_vel = 0.0f;
          remote_target_pos = remote_cart.pos;
          remote_target_vel = Vector2Zero();
          remote_target_rot = 0.0f;
          remote_target_ang_vel = 0.0f;
          remote_pick_side = -1;
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
          remote_pick_pos = (Vector2){pkt.cart.pick_pos_x, pkt.cart.pick_pos_y};
          remote_cart.mass = pkt.cart.mass;
        }
        break;

      case PKT_BUMP:
        main_cart.vel = Vector2Add(
            main_cart.vel, (Vector2){pkt.bump.impulse_x, pkt.bump.impulse_y});
        main_cart.ang_vel += pkt.bump.ang_impulse;
        break;

      case PKT_ITEM_PICK:
        if (world_initialized) {
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
          if (found_idx != -1) {
            Item picked = cart_consume_item(&remote_cart, &items, found_idx);
            shopping_list_consume(&remote_shopping_list, picked);
          } else {
            Item dummy = {.image = (Texture2D){.id = pkt.pick.texture_id}};
            shopping_list_consume(&remote_shopping_list, dummy);
          }

          if (remote_shopping_list.len == 0 && winner == 0) {
            winner = (g_net.mode == NET_MODE_CLIENT ? 1 : 2);
            winning_time = ela_time;
          }
        }
        break;

      case PKT_GAME_OVER:
        if (winner == 0 || winning_time == 0.0f ||
            pkt.game_over.final_time > 0.0f) {
          winner = pkt.game_over.winner_id;
          winning_time = pkt.game_over.final_time;
          if (winner == (g_net.mode == NET_MODE_CLIENT ? 1 : 2)) {
            remote_shopping_list.len = 0;
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

    const char *sub_str =
        "Waiting for game world init from host (Tailscale)...";
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
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.9f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.9f}, delta);
    // cart_apply_force_rotated(&main_cart, (Vector2){0.0f, 0.27f},
    // (Vector2){CART_PUSH_FORCE * 0.35f, 0.0f}, delta);
  }

  if (IsKeyDown(KEY_D)) {
    cart_apply_force_rotated(&main_cart, (Vector2){-0.25f, 0.27f},
                             (Vector2){0.0f, -CART_PUSH_FORCE * 0.9f}, delta);
    cart_apply_force_rotated(&main_cart, (Vector2){0.25f, 0.27f},
                             (Vector2){0.0f, CART_PUSH_FORCE * 0.9f}, delta);
    // cart_apply_force_rotated(&main_cart, (Vector2){0.0f, 0.27f},
    //                          (Vector2){-CART_PUSH_FORCE * 0.35f, 0.0f},
    //                          delta);
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

  // Remote cart smooth interpolation and cart-on-cart collision
  if (has_remote_cart) {
    remote_cart.pos = Vector2Lerp(remote_cart.pos, remote_target_pos, 0.40f);
    remote_cart.vel = remote_target_vel;
    remote_cart.rot = LerpAngle(remote_cart.rot, remote_target_rot, 0.40f);
    remote_cart.ang_vel = remote_target_ang_vel;

    // Cart-on-cart bumping physics
    Vector2 bump_impulse = Vector2Zero();
    float bump_ang = 0.0f;
    if (cart_collide_cart(&main_cart, &remote_cart, &bump_impulse, &bump_ang)) {
      if (g_net.state == NET_STATE_CONNECTED) {
        net_send_bump(bump_impulse, bump_ang);
      }
      remote_target_vel = remote_cart.vel;
      remote_target_pos = remote_cart.pos;
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
    float ddist = Vector2Distance(item.pos, driver);
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

    bool consumed = shopping_list_consume(&local_shopping_list, picked);
    if (consumed) {
      shopping_list_len = local_shopping_list.len;
      for (int i = 0; i < local_shopping_list.len; i++) {
        shopping_list[i] = local_shopping_list.items[i];
      }
    }

    if (g_net.mode != NET_MODE_SINGLE) {
      net_send_item_pick(picked.image.id, picked.pos,
                         g_net.mode == NET_MODE_HOST ? 0 : 1);
    }

    if (local_shopping_list.len == 0 && winner == 0) {
      winner = (g_net.mode == NET_MODE_CLIENT ? 2 : 1);
      winning_time = ela_time;
      if (g_net.mode != NET_MODE_SINGLE) {
        net_send_game_over((uint8_t)winner, winning_time);
      }
    }

    pick_item_idx = -1;
    pick_side = -1;
  }

  // Rematch / Reset hotkey (R key or Gamepad Start / Y)
  bool rematch_pressed =
      IsKeyPressed(KEY_R) ||
      IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
      IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_UP);
  if (rematch_pressed) {
    restart_match();
  }

  // Stream local cart state to network
  if (g_net.state == NET_STATE_CONNECTED && world_initialized) {
    Vector2 pick_target =
        (pick_side != -1 && pick_item_idx >= 0 && pick_item_idx < items.len)
            ? items.items[pick_item_idx].pos
            : Vector2Zero();
    net_send_cart_state(main_cart.pos, main_cart.vel, main_cart.rot,
                        main_cart.ang_vel, pick_side, pick_target,
                        main_cart.mass);
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
    Item rem_item = {.pos = remote_pick_pos};
    Item *p_rem_item = (remote_pick_side != -1) ? &rem_item : NULL;
    cart_draw(&remote_cart, remote_pick_side, p_rem_item, topleft, botright);
  }

  // Draw local cart
  cart_draw(&main_cart, pick_side,
            pick_item_idx >= 0 ? &items.items[pick_item_idx] : NULL, topleft,
            botright);

  itemarr_draw(items, Vector2Zero(), 0.0f, topleft, botright);

  // Draw hands
  if (has_remote_cart) {
    Item rem_item = {.pos = remote_pick_pos};
    Item *p_rem_item = (remote_pick_side != -1) ? &rem_item : NULL;
    cart_draw_hands(&remote_cart, remote_pick_side, p_rem_item);
  }
  cart_draw_hands(&main_cart, pick_side,
                  pick_item_idx >= 0 ? &items.items[pick_item_idx] : NULL);

  EndMode2D();

  // --- SHOPPING LIST HUD ---
  int my_id = (g_net.mode == NET_MODE_CLIENT ? 2 : 1);
  int opp_id = (my_id == 1 ? 2 : 1);
  Color my_dark_color = (my_id == 1) ? DARKBLUE : MAROON;
  Color opp_dark_color = (opp_id == 1) ? DARKBLUE : MAROON;

  float top_y = (g_net.mode == NET_MODE_SINGLE) ? 20.0f : 34.0f;
  float spacing = 8.0f;
  float avail_h = (float)GetScreenHeight() - top_y - 20.0f;
  float item_height = (avail_h - spacing * (SHOPPING_LIST_MAX_LEN - 1)) /
                      (float)SHOPPING_LIST_MAX_LEN;
  if (item_height > 65.0f)
    item_height = 65.0f;
  if (item_height < 38.0f)
    item_height = 38.0f;

  // Local player checklist (Left side, unboxed and big like before)
  if (g_net.mode != NET_MODE_SINGLE) {
    DrawText(TextFormat("YOU (%d)", local_shopping_list.len), 20, 12, 16,
             my_dark_color);
  }
  for (int i = 0; i < local_shopping_list.len; i++) {
    Item item = items_table[local_shopping_list.items[i]];
    float scale = item_height / (float)item.image.height;
    DrawTextureEx(item.image,
                  (Vector2){20, top_y + (item_height + spacing) * i}, 0.0f,
                  scale, WHITE);
  }

  // Opponent checklist (Right side, multiplayer only)
  if (g_net.mode != NET_MODE_SINGLE && has_remote_cart) {
    const char *opp_hdr = TextFormat("OPPONENT (%d)", remote_shopping_list.len);
    int opp_hdr_w = MeasureText(opp_hdr, 16);
    DrawText(opp_hdr, GetScreenWidth() - opp_hdr_w - 20, 12, 16,
             opp_dark_color);

    for (int i = 0; i < remote_shopping_list.len; i++) {
      Item opp_item = items_table[remote_shopping_list.items[i]];
      float scale = item_height / (float)opp_item.image.height;
      float drawn_w = (float)opp_item.image.width * scale;
      float opp_x = (float)GetScreenWidth() - drawn_w - 20.0f;
      DrawTextureEx(opp_item.image,
                    (Vector2){opp_x, top_y + (item_height + spacing) * i}, 0.0f,
                    scale, WHITE);
    }
  }

  // --- TOP CENTER: RACE TIMER & LIVE COMPETITIVE STANDINGS ---
  if (g_net.mode != NET_MODE_SINGLE && has_remote_cart) {
    float badge_w = 360.0f;
    float badge_h = 44.0f;
    float badge_x = ((float)GetScreenWidth() - badge_w) * 0.5f;
    DrawRectangleRounded((Rectangle){badge_x, 8.0f, badge_w, badge_h}, 0.25f, 4,
                         (Color){255, 255, 255, 230});
    DrawRectangleRoundedLinesEx((Rectangle){badge_x, 8.0f, badge_w, badge_h},
                                0.25f, 4, 2.0f, DARKGRAY);

    const char *time_str = TextFormat("TIME: %.3f s", ela_time);
    DrawText(time_str,
             (int)(badge_x + (badge_w - MeasureText(time_str, 18)) * 0.5f), 12,
             18, BLACK);

    if (winner == 0) {
      if (local_shopping_list.len < remote_shopping_list.len) {
        int lead = remote_shopping_list.len - local_shopping_list.len;
        const char *lead_str = TextFormat("YOU'RE LEADING by %d item%s!", lead,
                                          lead > 1 ? "s" : "");
        DrawText(lead_str,
                 (int)(badge_x + (badge_w - MeasureText(lead_str, 13)) * 0.5f),
                 32, 13, DARKGREEN);
      } else if (local_shopping_list.len > remote_shopping_list.len) {
        int behind = local_shopping_list.len - remote_shopping_list.len;
        const char *behind_str = TextFormat("OPPONENT LEADING by %d item%s!",
                                            behind, behind > 1 ? "s" : "");
        DrawText(
            behind_str,
            (int)(badge_x + (badge_w - MeasureText(behind_str, 13)) * 0.5f), 32,
            13, MAROON);
      } else {
        const char *tied_str =
            TextFormat("TIED RACE! (%d items each)", local_shopping_list.len);
        DrawText(tied_str,
                 (int)(badge_x + (badge_w - MeasureText(tied_str, 13)) * 0.5f),
                 32, 13, DARKBLUE);
      }
    } else {
      const char *fin_str = (winner == my_id) ? "RACE FINISHED - YOU WON!"
                                              : "RACE FINISHED - OPPONENT WON!";
      DrawText(fin_str,
               (int)(badge_x + (badge_w - MeasureText(fin_str, 13)) * 0.5f), 32,
               13, (winner == my_id) ? DARKGREEN : MAROON);
    }

    // Ping
    const char *ping_str = TextFormat("Ping: %.0f ms", g_net.rtt_ms);
    DrawText(ping_str, (int)(badge_x + badge_w + 14.0f), 20, 14, DARKGRAY);
  } else if (g_net.mode == NET_MODE_HOST && !has_remote_cart) {
    float badge_w = 460.0f;
    float badge_x = ((float)GetScreenWidth() - badge_w) * 0.5f;
    DrawRectangleRounded((Rectangle){badge_x, 8.0f, badge_w, 36.0f}, 0.25f, 4,
                         (Color){255, 255, 255, 230});
    DrawRectangleRoundedLinesEx((Rectangle){badge_x, 8.0f, badge_w, 36.0f},
                                0.25f, 4, 1.5f, DARKBLUE);
    const char *wait_str =
        TextFormat("HOSTING (port %d) - Waiting for Player 2 to join...",
                   g_net.remote_port);
    DrawText(wait_str,
             (int)(badge_x + (badge_w - MeasureText(wait_str, 15)) * 0.5f), 18,
             15, DARKBLUE);
  } else {
    // Singleplayer timer (original bottom-right position and style)
    const char *string = TextFormat("%.3f", ela_time);
    const char *string_zeros = TextFormat("%.3f", floorf(ela_time));
    int string_width_zeros = MeasureText(string_zeros, 30);
    DrawText(string, GetScreenWidth() - string_width_zeros - 10,
             GetScreenHeight() - 40, 30, BLACK);
  }

  // --- GAME OVER / VICTORY / DEFEAT MODAL ---
  if (winner != 0) {
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){0, 0, 0, 130});
    float mw = 480.0f;
    float mh = 230.0f;
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
      const char *rem_str = "Press [R] to Play Again";
      DrawText(rem_str, (int)(mx + (mw - MeasureText(rem_str, 18)) * 0.5f),
               (int)(my + 165), 18, DARKBLUE);
    } else {
      bool won = (winner == my_id);
      Color border_col = won ? GOLD : MAROON;
      DrawRectangleRoundedLinesEx((Rectangle){mx, my, mw, mh}, 0.10f, 4, 4.0f,
                                  border_col);

      const char *title = won ? "VICTORY!" : "DEFEAT!";
      Color title_col = won ? GOLD : RED;
      DrawText(title, (int)(mx + (mw - MeasureText(title, 38)) * 0.5f),
               (int)(my + 24), 38, title_col);

      const char *sub = won ? "You finished your shopping list first!"
                            : "Opponent finished their shopping list first!";
      Color sub_col = won ? DARKGREEN : DARKGRAY;
      DrawText(sub, (int)(mx + (mw - MeasureText(sub, 17)) * 0.5f),
               (int)(my + 72), 17, sub_col);

      const char *t_str =
          TextFormat("Winning Time: %.3f seconds", winning_time);
      DrawText(t_str, (int)(mx + (mw - MeasureText(t_str, 20)) * 0.5f),
               (int)(my + 112), 20, BLACK);

      const char *rem_str = "";
      if (g_net.mode == NET_MODE_HOST) {
        rem_str = "Press [R] to Start Rematch";
      } else {
        rem_str = rematch_requested ? "Rematch Requested! Waiting for Host..."
                                    : "Press [R] to Request Rematch";
      }
      DrawText(rem_str, (int)(mx + (mw - MeasureText(rem_str, 18)) * 0.5f),
               (int)(my + 162), 18, DARKBLUE);
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
      printf("  ./main --screenshot <file>  Save screenshot after a few frames "
             "and exit\n");
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

    init_game_world((uint32_t)time(NULL), NULL);
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
  } else {
    // Singleplayer (Blue)
    main_cart =
        cart_new_goofy(DARKBLUE, BLUE, BLACK, Vector2Zero(), 4,
                       (Vector2){-0.15f, -0.27f}, (Vector2){-0.25f, 0.27f},
                       (Vector2){0.25f, 0.27f}, (Vector2){0.15f, -0.27f});
    main_cart.pos = (Vector2){10.0f, 10.0f};

    init_game_world((uint32_t)time(NULL), NULL);
  }

  cam = (Camera2D){
      (Vector2){(float)GetScreenWidth() / 2.0f,
                (float)GetScreenHeight() / 2.0f},
      (Vector2){0.0f, 0.0f},
      0.0f,
      100.0f,
  };

  while (!WindowShouldClose()) {
    DrawTickFrame();
    if (screenshot_path && screenshot_countdown == -2) {
      break;
    }
  }

  cleanup_game_world();
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
