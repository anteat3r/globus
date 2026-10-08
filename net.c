#include "net.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

NetContext g_net = {
    .mode = NET_MODE_SINGLE,
    .state = NET_STATE_DISCONNECTED,
    .sockfd = -1,
    .is_connected = false,
    .seed = 0,
    .rtt_ms = 0.0f,
    .last_recv_time = 0.0f,
    .last_send_time = 0.0f,
    .last_ping_time = 0.0f,
    .remote_ip = {0},
    .remote_port = 0,
    .send_seq = 0,
    .recv_seq = 0,
};

static struct sockaddr_in g_remote_addr;
static bool g_has_remote_addr = false;

static bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool net_init_host(int port) {
  net_close();

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return false;
  }

  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  if (!set_nonblocking(fd)) {
    close(fd);
    return false;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("bind");
    close(fd);
    return false;
  }

  g_net.mode = NET_MODE_HOST;
  g_net.state = NET_STATE_CONNECTING;
  g_net.sockfd = fd;
  g_net.is_connected = false;
  g_net.remote_port = port;
  g_has_remote_addr = false;

  printf("[NET] Hosting server on UDP port %d...\n", port);
  return true;
}

bool net_init_client(const char *host_ip, int port) {
  net_close();

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    perror("socket");
    return false;
  }

  if (!set_nonblocking(fd)) {
    close(fd);
    return false;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host_ip, &addr.sin_addr) <= 0) {
    struct hostent *he = gethostbyname(host_ip);
    if (!he) {
      fprintf(stderr, "[NET] Failed to resolve host: %s\n", host_ip);
      close(fd);
      return false;
    }
    memcpy(&addr.sin_addr, he->h_addr_list[0], sizeof(struct in_addr));
  }

  g_remote_addr = addr;
  g_has_remote_addr = true;

  g_net.mode = NET_MODE_CLIENT;
  g_net.state = NET_STATE_CONNECTING;
  g_net.sockfd = fd;
  g_net.is_connected = false;
  strncpy(g_net.remote_ip, host_ip, sizeof(g_net.remote_ip) - 1);
  g_net.remote_port = port;

  printf("[NET] Client initialized, connecting to %s:%d...\n", host_ip, port);
  return true;
}

void net_close(void) {
  if (g_net.sockfd >= 0) {
    close(g_net.sockfd);
    g_net.sockfd = -1;
  }
  g_net.state = NET_STATE_DISCONNECTED;
  g_net.is_connected = false;
  g_has_remote_addr = false;
}

bool net_send(const void *data, size_t size) {
  if (g_net.sockfd < 0 || !g_has_remote_addr)
    return false;

  ssize_t sent = sendto(g_net.sockfd, data, size, 0,
                        (struct sockaddr *)&g_remote_addr, sizeof(g_remote_addr));
  return sent == (ssize_t)size;
}

int net_poll(NetPacket *pkt) {
  if (g_net.sockfd < 0)
    return 0;

  struct sockaddr_in from;
  socklen_t from_len = sizeof(from);

  ssize_t len = recvfrom(g_net.sockfd, pkt->raw, sizeof(pkt->raw), 0,
                         (struct sockaddr *)&from, &from_len);

  if (len < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return 0;
    return -1;
  }

  if (len < (ssize_t)sizeof(PktHeader))
    return 0;

  // On host side, if connecting or if sender changed, update remote address
  if (g_net.mode == NET_MODE_HOST) {
    if (!g_has_remote_addr || pkt->header.type == PKT_HELLO) {
      g_remote_addr = from;
      g_has_remote_addr = true;
      inet_ntop(AF_INET, &from.sin_addr, g_net.remote_ip,
                sizeof(g_net.remote_ip));
      g_net.remote_port = ntohs(from.sin_port);
    }
  }

  g_net.last_recv_time = (float)GetTime();
  return (int)len;
}

void net_send_hello(void) {
  PktHello p = {
      .header = {.type = PKT_HELLO},
      .player_name = "Player 2",
  };
  net_send(&p, sizeof(p));
}

void net_send_init(uint32_t seed, const int *slist, int slist_len,
                   const uint8_t *grid_mask) {
  PktInit p;
  memset(&p, 0, sizeof(p));
  p.header.type = PKT_INIT;
  p.seed = seed;
  p.shopping_list_count = (uint8_t)slist_len;
  for (int i = 0; i < slist_len && i < SHOPPING_LIST_MAX_LEN; i++) {
    p.shopping_list[i] = (int16_t)slist[i];
  }
  if (grid_mask) {
    memcpy(p.grid_mask, grid_mask, sizeof(p.grid_mask));
  }
  net_send(&p, sizeof(p));
}

void net_send_ready(void) {
  PktReady p = {
      .header = {.type = PKT_READY},
  };
  net_send(&p, sizeof(p));
}

void net_send_cart_state(Vector2 pos, Vector2 vel, float rot, float ang_vel,
                         int pick_side, Vector2 pick_pos, float mass) {
  PktCartState p = {
      .header = {.type = PKT_CART_STATE},
      .seq = ++g_net.send_seq,
      .pos_x = pos.x,
      .pos_y = pos.y,
      .vel_x = vel.x,
      .vel_y = vel.y,
      .rot = rot,
      .ang_vel = ang_vel,
      .pick_side = (int8_t)pick_side,
      .pick_pos_x = pick_pos.x,
      .pick_pos_y = pick_pos.y,
      .mass = mass,
  };
  net_send(&p, sizeof(p));
}

void net_send_item_pick(int texture_id, Vector2 pos, int player_id) {
  PktItemPick p = {
      .header = {.type = PKT_ITEM_PICK},
      .texture_id = texture_id,
      .pos_x = pos.x,
      .pos_y = pos.y,
      .player_id = (uint8_t)player_id,
  };
  net_send(&p, sizeof(p));
}

void net_send_ping(void) {
  PktPingPong p = {
      .header = {.type = PKT_PING},
      .timestamp_ms = (uint32_t)(GetTime() * 1000.0),
  };
  net_send(&p, sizeof(p));
}

void net_send_bump(Vector2 impulse, float ang_impulse) {
  PktBump p = {
      .header = {.type = PKT_BUMP},
      .impulse_x = impulse.x,
      .impulse_y = impulse.y,
      .ang_impulse = ang_impulse,
  };
  net_send(&p, sizeof(p));
}

void net_send_game_over(uint8_t winner_id, float final_time) {
  PktGameOver p = {
      .header = {.type = PKT_GAME_OVER},
      .winner_id = winner_id,
      .final_time = final_time,
  };
  net_send(&p, sizeof(p));
}
