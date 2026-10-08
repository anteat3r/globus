#ifndef NET_H
#define NET_H

#include "item.h"
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
} PacketType;

#pragma pack(push, 1)

typedef struct {
  uint8_t type;
} PktHeader;

typedef struct {
  PktHeader header;
  char player_name[16];
} PktHello;

#define NET_GRID_MASK_SIZE 512

typedef struct {
  PktHeader header;
  uint32_t seed;
  uint8_t shopping_list_count;
  int16_t shopping_list[SHOPPING_LIST_MAX_LEN];
  uint8_t grid_mask[NET_GRID_MASK_SIZE];
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
} PktGameOver;

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
void net_close(void);

bool net_send(const void *data, size_t size);
int net_poll(NetPacket *pkt);

void net_send_hello(void);
void net_send_init(uint32_t seed, const int *slist, int slist_len,
                   const uint8_t *grid_mask);
void net_send_ready(void);
void net_send_cart_state(Vector2 pos, Vector2 vel, float rot, float ang_vel,
                         int pick_side, Vector2 pick_pos, float mass);
void net_send_item_pick(int texture_id, Vector2 pos, int player_id);
void net_send_ping(void);
void net_send_bump(Vector2 impulse, float ang_impulse);
void net_send_game_over(uint8_t winner_id, float final_time);

#endif // NET_H
