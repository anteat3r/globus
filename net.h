#ifndef NET_H
#define NET_H

#include "item.h"
#include "mall.h"
#include <raylib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DEFAULT_NET_PORT 7777

typedef enum {
  NET_MODE_SINGLE = 0,
  NET_MODE_HOST,
  NET_MODE_CLIENT,
} NetMode;

typedef enum {
  NET_STATE_DISCONNECTED = 0,
  NET_STATE_CONNECTING,
  NET_STATE_CONNECTED,
} NetState;

typedef enum {
  PKT_HELLO = 1,
  PKT_INIT = 2,
  PKT_READY = 3,
  PKT_CART_STATE = 4,
  PKT_ITEM_PICK = 5,
  PKT_PING = 6,
  PKT_PONG = 7,
  PKT_BUMP = 8,
  PKT_GAME_OVER = 9,
  PKT_COLLISION_EVENT = 10,
} PacketType;

#pragma pack(push, 1)

typedef struct {
  uint8_t type;
} PktHeader;

#define MAX_NET_PLAYERS 4

typedef struct {
  PktHeader header;
  char player_name[16];
  uint8_t player_id;
  uint8_t team_id;
} PktHello;

#define NET_GRID_MASK_SIZE MALL_GRID_MASK_SIZE

typedef struct {
  PktHeader header;
  uint32_t seed;
  RoomConfig config;
  uint8_t shopping_list_count;
  int16_t shopping_list[SHOPPING_LIST_MAX_LEN];
  uint8_t grid_mask[NET_GRID_MASK_SIZE];
  uint8_t active_players_mask; // Bitmask of active players (bit 0 = P1, bit 1 = P2, etc.)
  uint8_t player_teams[MAX_NET_PLAYERS]; // Team ID for each player (1 or 2)
} PktInit;

typedef struct {
  PktHeader header;
} PktReady;

typedef struct {
  PktHeader header;
  uint32_t seq;
  float pos_x;
  float pos_y;
  float vel_x;
  float vel_y;
  float rot;
  float ang_vel;
  int8_t pick_side;
  float pick_pos_x;
  float pick_pos_y;
  float mass;
  uint8_t player_id; // 1..4
} PktCartState;

typedef struct {
  PktHeader header;
  int32_t texture_id;
  float pos_x;
  float pos_y;
  uint8_t player_id;
} PktItemPick;

typedef struct {
  PktHeader header;
  uint32_t timestamp_ms;
} PktPingPong;

typedef struct {
  PktHeader header;
  float impulse_x;
  float impulse_y;
  float ang_impulse;
} PktBump;

typedef struct {
  PktHeader header;
  uint8_t winner_id;
  float final_time;
  uint8_t winning_team; // 0=FFA, 1=Team 1, 2=Team 2
} PktGameOver;

typedef struct {
  PktHeader header;
  float impulse_x;
  float impulse_y;
  float ang_impulse;
  uint8_t faster_player_id;
  int32_t transferred_texture_id;
  int32_t p1_dropped_texture_id;
  float p1_drop_x;
  float p1_drop_y;
  int32_t p2_dropped_texture_id;
  float p2_drop_x;
  float p2_drop_y;
  uint8_t p1_id; // 1..4
  uint8_t p2_id; // 1..4
} PktCollisionEvent;

#pragma pack(pop)

typedef union {
  PktHeader header;
  PktHello hello;
  PktInit init;
  PktReady ready;
  PktCartState cart;
  PktItemPick pick;
  PktPingPong ping;
  PktBump bump;
  PktGameOver game_over;
  PktCollisionEvent collision;
  uint8_t raw[1024];
} NetPacket;

typedef struct {
  NetMode mode;
  NetState state;
  int sockfd;
  bool is_connected;
  uint32_t seed;
  float rtt_ms;
  float last_recv_time;
  float last_send_time;
  float last_ping_time;
  char remote_ip[64];
  int remote_port;
  uint32_t send_seq;
  uint32_t recv_seq;
} NetContext;

extern NetContext g_net;

bool net_init_host(int port);
bool net_init_client(const char *host_ip, int port);
bool net_init_web(const char *ws_url, const char *room_code, NetMode mode);
void net_close(void);

bool net_send(const void *data, size_t size);
int net_poll(NetPacket *pkt);

void net_send_hello(void);
void net_send_hello_multi(const char *name, uint8_t player_id, uint8_t team_id);
void net_send_init(uint32_t seed, const RoomConfig *config, const int *slist,
                   int slist_len, const uint8_t *grid_mask);
void net_send_init_multi(uint32_t seed, const RoomConfig *config, const int *slist,
                         int slist_len, const uint8_t *grid_mask,
                         uint8_t active_players_mask, const uint8_t *player_teams);
void net_send_ready(void);
void net_send_cart_state(Vector2 pos, Vector2 vel, float rot, float ang_vel,
                         int pick_side, Vector2 pick_pos, float mass);
void net_send_cart_state_multi(Vector2 pos, Vector2 vel, float rot, float ang_vel,
                               int pick_side, Vector2 pick_pos, float mass,
                               uint8_t player_id);
void net_send_item_pick(int texture_id, Vector2 pos, int player_id);
void net_send_ping(void);
void net_send_bump(Vector2 impulse, float ang_impulse);
void net_send_game_over(uint8_t winner_id, float final_time);
void net_send_game_over_multi(uint8_t winner_id, float final_time, uint8_t winning_team);
void net_send_collision_event(const PktCollisionEvent *event);

#endif // NET_H
