#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(int, em_ws_connect, (const char *url_ptr, const char *room_ptr, int is_host), {
  let url = (url_ptr && UTF8ToString(url_ptr)) || "";
  const room = (room_ptr && UTF8ToString(room_ptr)) || "";

  if (!url || url.length === 0) {
    const loc = window.location;
    const proto = (loc.protocol === 'https:') ? 'wss:' : 'ws:';
    url = proto + '//' + loc.host + '/ws';
  }

  if (!window._globus_net) {
    window._globus_net = {
      ws: null,
      queue: [],
      connected: false,
      connecting: false,
      role: null,
      room: null,
      serverRoom: null,
    };
  }

  window._globus_net.role = is_host ? 'host' : 'client';
  window._globus_net.room = room;

  // Reuse existing open WebSocket if available!
  if (window._globus_net.ws && window._globus_net.ws.readyState === WebSocket.OPEN) {
    console.log('[WS] Reusing open WebSocket for room:', room);
    window._globus_net.connected = true;
    if (window._globus_net.serverRoom !== room && room && room.length > 0) {
      window._globus_net.serverRoom = room;
      const msg = {
        type: is_host ? 'create_room' : 'join_room',
        room: room
      };
      window._globus_net.ws.send(JSON.stringify(msg));
    }
    return 1;
  }

  if (window._globus_net.ws && window._globus_net.ws.readyState === WebSocket.CONNECTING) {
    console.log('[WS] WebSocket is already connecting...');
    return 1;
  }

  try {
    const ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';
    window._globus_net.ws = ws;
    window._globus_net.connecting = true;

    ws.onopen = function() {
      console.log('[WS] Connected to relay server:', url);
      window._globus_net.connecting = false;
      window._globus_net.connected = true;
      if (window._globus_net.room && window._globus_net.room.length > 0) {
        const msg = {
          type: (window._globus_net.role === 'host') ? 'create_room' : 'join_room',
          room: window._globus_net.room
        };
        ws.send(JSON.stringify(msg));
      }
      if (window.onNetStatus) window.onNetStatus('Connected to relay, room ' + window._globus_net.room);
    };

    ws.onmessage = function(event) {
      if (typeof event.data === 'string') {
        try {
          const ctrl = JSON.parse(event.data);
          console.log('[WS] Control message:', ctrl);
          if (ctrl.type === 'peer_joined') {
            if (window.onPeerJoined) window.onPeerJoined(ctrl);
          } else if (ctrl.type === 'peer_left') {
            if (window.onPeerLeft) window.onPeerLeft(ctrl);
          } else if (ctrl.type === 'room_created') {
            if (window.onRoomCreated) window.onRoomCreated(ctrl);
          } else if (ctrl.type === 'room_joined') {
            if (window.onRoomJoined) window.onRoomJoined(ctrl);
          } else if (ctrl.type === 'room_list') {
            if (window.onRoomList) window.onRoomList(ctrl.rooms);
          } else if (ctrl.type === 'error') {
            if (window.onNetError) window.onNetError(ctrl.message);
          }
        } catch(e) {}
      } else if (event.data instanceof ArrayBuffer) {
        const u8 = new Uint8Array(event.data);
        if (u8.length > 0 && u8[0] === 4) { // PKT_CART_STATE
          const q = window._globus_net.queue;
          if (q.length > 3) {
            let lastCart = -1;
            for (let i = q.length - 1; i >= 0; i--) {
              if (q[i][0] === 4) {
                lastCart = i;
                break;
              }
            }
            if (lastCart !== -1) {
              window._globus_net.queue = q.filter((p, idx) => p[0] !== 4 || idx === lastCart);
            }
          }
        }
        window._globus_net.queue.push(u8);
      }
    };

    ws.onerror = function(err) {
      console.error('[WS] Connection error:', err);
      window._globus_net.connecting = false;
      if (window.onNetError) window.onNetError('WebSocket connection error');
    };

    ws.onclose = function() {
      console.log('[WS] Connection closed');
      window._globus_net.connected = false;
      window._globus_net.connecting = false;
      if (window.onNetStatus) window.onNetStatus('Disconnected from relay');
    };

    return 1;
  } catch(e) {
    console.error('[WS] Exception connecting to WebSocket:', e);
    window._globus_net.connecting = false;
    return 0;
  }
});

EM_JS(int, em_ws_send, (const uint8_t *data, int len), {
  if (!window._globus_net || !window._globus_net.ws || window._globus_net.ws.readyState !== 1) {
    return 0;
  }
  const slice = HEAPU8.subarray(data, data + len);
  window._globus_net.ws.send(slice);
  return 1;
});

EM_JS(int, em_ws_poll, (uint8_t *out_buf, int max_len), {
  if (!window._globus_net || !window._globus_net.queue || window._globus_net.queue.length === 0) {
    return 0;
  }
  const pkt = window._globus_net.queue.shift();
  if (pkt.length > max_len) {
    return 0;
  }
  HEAPU8.set(pkt, out_buf);
  return pkt.length;
});

EM_JS(void, em_ws_close, (void), {
  if (window._globus_net) {
    window._globus_net.queue = [];
    if (window._globus_net.ws && window._globus_net.ws.readyState === 1) {
      try {
        window._globus_net.ws.send(JSON.stringify({ type: 'leave_room' }));
      } catch(e) {}
    }
    window._globus_net.room = null;
    window._globus_net.role = null;
  }
});

bool net_init_web(const char *ws_url, const char *room_code, NetMode mode) {
  g_net.mode = mode;
  g_net.state = NET_STATE_CONNECTING;
  g_net.is_connected = false;
  if (room_code) {
    strncpy(g_net.remote_ip, room_code, sizeof(g_net.remote_ip) - 1);
  } else {
    strncpy(g_net.remote_ip, "LOBBY", sizeof(g_net.remote_ip) - 1);
  }
  int is_host = (mode == NET_MODE_HOST) ? 1 : 0;
  return em_ws_connect(ws_url, room_code, is_host) == 1;
}

bool net_init_host(int port) {
  (void)port;
  return net_init_web(NULL, NULL, NET_MODE_HOST);
}

bool net_init_client(const char *host_ip, int port) {
  (void)port;
  return net_init_web(NULL, host_ip, NET_MODE_CLIENT);
}

void net_close(void) {
  em_ws_close();
  g_net.state = NET_STATE_DISCONNECTED;
  g_net.is_connected = false;
}

bool net_send(const void *data, size_t size) {
  return em_ws_send((const uint8_t *)data, (int)size) == 1;
}

int net_poll(NetPacket *pkt) {
  int len = em_ws_poll(pkt->raw, (int)sizeof(pkt->raw));
  if (len <= 0)
    return 0;
  g_net.last_recv_time = (float)GetTime();
  return len;
}

#else // !__EMSCRIPTEN__ (POSIX sockets)

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static struct sockaddr_in g_remote_addr;
static bool g_has_remote_addr = false;

static bool set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

bool net_init_web(const char *ws_url, const char *room_code, NetMode mode) {
  (void)ws_url;
  (void)room_code;
  (void)mode;
  return false;
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

#endif // __EMSCRIPTEN__

void net_send_hello(void) {
  PktHello p = {
      .header = {.type = PKT_HELLO},
      .player_name = "Player 2",
  };
  net_send(&p, sizeof(p));
}

void net_send_init(uint32_t seed, const RoomConfig *config, const int *slist,
                   int slist_len, const uint8_t *grid_mask) {
  PktInit p;
  memset(&p, 0, sizeof(p));
  p.header.type = PKT_INIT;
  p.seed = seed;
  if (config) {
    p.config = *config;
  } else {
    p.config = room_config_default();
  }
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

void net_send_collision_event(const PktCollisionEvent *event) {
  net_send(event, sizeof(PktCollisionEvent));
}
