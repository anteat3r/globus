const http = require('http');
const fs = require('fs');
const path = require('path');
const { WebSocketServer, WebSocket } = require('ws');

const PORT = parseInt(process.env.PORT, 10) || 8080;
const HOST = process.env.HOST || '0.0.0.0';

// Project directories
const WEB_DIR = path.resolve(__dirname, '..', 'web');
const ROOT_DIR = path.resolve(__dirname, '..');

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.wasm': 'application/wasm',
  '.data': 'application/octet-stream',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.ico': 'image/x-icon',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json',
};

// Rooms registry: roomId -> Room
const rooms = new Map();

function generateRoomId() {
  const chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
  let id = '';
  for (let i = 0; i < 4; i++) {
    id += chars.charAt(Math.floor(Math.random() * chars.length));
  }
  return id;
}

// HTTP Server
const server = http.createServer((req, res) => {
  const parsedUrl = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
  let pathname = parsedUrl.pathname;

  // API endpoints
  if (pathname === '/api/status') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    let totalPlayers = 0;
    const roomSummaries = [];
    for (const [id, room] of rooms.entries()) {
      let count = room.players ? room.players.length : (room.host ? 1 : 0);
      const maxP = (room.config && room.config.maxPlayers) ? room.config.maxPlayers : 4;
      totalPlayers += count;
      roomSummaries.push({
        id,
        players: count,
        maxPlayers: maxP,
        waiting: count < maxP,
        config: room.config,
        createdAt: room.createdAt,
      });
    }
    return res.end(JSON.stringify({
      status: 'ok',
      activeRooms: rooms.size,
      totalPlayers,
      rooms: roomSummaries,
    }));
  }

  if (pathname === '/api/rooms') {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    const available = [];
    for (const [id, room] of rooms.entries()) {
      const maxP = (room.config && room.config.maxPlayers) ? room.config.maxPlayers : 4;
      const count = room.players ? room.players.length : 1;
      if (room.host && count < maxP) {
        available.push({
          id,
          createdAt: room.createdAt,
          config: room.config,
          players: count,
          maxPlayers: maxP,
        });
      }
    }
    return res.end(JSON.stringify({ rooms: available }));
  }

  // Static file serving
  if (pathname === '/' || pathname === '') {
    pathname = '/index.html';
  }

  // Security check: prevent directory traversal
  const safePath = path.normalize(pathname).replace(/^(\.\.[\/\\])+/, '');
  
  // Try web/ directory first, then root directory (for res/ etc.)
  let filePath = path.join(WEB_DIR, safePath);
  if (!fs.existsSync(filePath)) {
    filePath = path.join(ROOT_DIR, safePath);
  }

  if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || 'application/octet-stream';
    res.writeHead(200, {
      'Content-Type': contentType,
      'Access-Control-Allow-Origin': '*',
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    });
    fs.createReadStream(filePath).pipe(res);
  } else {
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('404 Not Found');
  }
});

// WebSocket Server
const wss = new WebSocketServer({ noServer: true });

server.on('upgrade', (request, socket, head) => {
  const parsedUrl = new URL(request.url, `http://${request.headers.host || 'localhost'}`);
  if (parsedUrl.pathname === '/ws') {
    socket.setNoDelay(true);
    wss.handleUpgrade(request, socket, head, (ws) => {
      wss.emit('connection', ws, request);
    });
  } else {
    socket.destroy();
  }
});

wss.on('connection', (ws) => {
  if (ws._socket) {
    ws._socket.setNoDelay(true);
  }
  ws.room = null;
  ws.role = null; // 'host' or 'client'
  ws.isAlive = true;

  ws.on('pong', () => {
    ws.isAlive = true;
  });

  ws.on('message', (data, isBinary) => {
    if (isBinary) {
      // Forward binary game packet to all other peers in same room
      if (ws.room) {
        const room = rooms.get(ws.room);
        if (room && room.players) {
          for (const peer of room.players) {
            if (peer && peer !== ws && peer.readyState === WebSocket.OPEN) {
              // Buffer bloat mitigation: if peer socket buffer is severely backlogged,
              // drop stale position packets (header type 4 = PKT_CART_STATE) to avoid lag buildup
              if (peer.bufferedAmount > 32768 && data && data.length > 0 && data[0] === 4) {
                continue;
              }
              peer.send(data, { binary: true });
            }
          }
        }
      }
      return;
    }

    // Text JSON control message
    try {
      const msg = JSON.parse(data.toString());
      handleControlMessage(ws, msg);
    } catch (err) {
      console.error('[WS] Invalid JSON received:', err);
    }
  });

  ws.on('close', () => {
    handleLeave(ws, 'Connection closed');
  });

  ws.on('error', (err) => {
    console.error('[WS] Socket error:', err.message);
  });
});

function handleControlMessage(ws, msg) {
  switch (msg.type) {
    case 'create_room': {
      let roomId = (msg.room || '').trim().toUpperCase();
      if (!roomId) {
        roomId = generateRoomId();
        while (rooms.has(roomId)) {
          roomId = generateRoomId();
        }
      }

      // If this socket is already host of this room, nothing to do
      if (rooms.has(roomId)) {
        const existing = rooms.get(roomId);
        if (existing.host === ws) {
          ws.room = roomId;
          ws.role = 'host';
          return;
        }
        if (existing.host && existing.host.readyState === WebSocket.OPEN) {
          return sendJson(ws, { type: 'error', message: `Room ${roomId} already exists` });
        }
      }

      handleLeave(ws);

      const room = {
        id: roomId,
        host: ws,
        players: [ws],
        config: msg.config || null,
        createdAt: Date.now(),
      };
      rooms.set(roomId, room);
      ws.room = roomId;
      ws.role = 'host';
      ws.playerId = 1;
      ws.teamId = 1;

      console.log(`[LOBBY] Room ${roomId} created by host (Player 1, Team 1)`);
      sendJson(ws, { type: 'room_created', room: roomId, role: 'host', playerId: 1, teamId: 1, config: room.config });
      broadcastRoomList();
      break;
    }

    case 'join_room': {
      const roomId = (msg.room || '').trim().toUpperCase();
      if (!roomId || !rooms.has(roomId)) {
        return sendJson(ws, { type: 'error', message: `Room ${roomId} does not exist` });
      }
      const room = rooms.get(roomId);
      if (!room.players) room.players = [room.host];
      if (room.players.includes(ws)) {
        return;
      }
      const maxPlayers = (room.config && room.config.maxPlayers) ? room.config.maxPlayers : 4;
      if (room.players.length >= maxPlayers) {
        return sendJson(ws, { type: 'error', message: `Room ${roomId} is already full (${room.players.length}/${maxPlayers} players)` });
      }
      if (!room.host || room.host.readyState !== WebSocket.OPEN) {
        return sendJson(ws, { type: 'error', message: `Room ${roomId} host is disconnected` });
      }

      handleLeave(ws);
      room.players.push(ws);
      const playerId = room.players.length; // 2, 3, or 4
      ws.room = roomId;
      ws.role = 'client';
      ws.playerId = playerId;

      // Determine team in 2v2:
      let teamId = msg.teamId;
      if (!teamId || (teamId !== 1 && teamId !== 2)) {
        if (room.config && room.config.gameMode === 1) { // 2v2 mode
          let t1 = 0, t2 = 0;
          for (const p of room.players) {
            if (p !== ws) {
              if (p.teamId === 2) t2++; else t1++;
            }
          }
          teamId = (t1 <= t2) ? 1 : 2;
        } else {
          teamId = playerId;
        }
      }
      ws.teamId = teamId;

      console.log(`[LOBBY] Player ${playerId} (Team ${teamId}) joined room ${roomId} (${room.players.length}/${maxPlayers})`);
      sendJson(ws, {
        type: 'room_joined',
        room: roomId,
        role: 'client',
        playerId: playerId,
        teamId: teamId,
        config: room.config,
        totalPlayers: room.players.length,
      });

      // Notify all other players in the room
      for (const peer of room.players) {
        if (peer !== ws && peer.readyState === WebSocket.OPEN) {
          sendJson(peer, {
            type: 'peer_joined',
            role: 'client',
            playerId: playerId,
            teamId: teamId,
            totalPlayers: room.players.length,
          });
        }
      }
      broadcastRoomList();
      break;
    }

    case 'quick_match': {
      handleLeave(ws);
      // Find open room waiting for players
      let targetRoom = null;
      for (const [id, r] of rooms.entries()) {
        const maxP = (r.config && r.config.maxPlayers) ? r.config.maxPlayers : 4;
        const count = r.players ? r.players.length : 1;
        if (r.host && r.host.readyState === WebSocket.OPEN && count < maxP) {
          targetRoom = r;
          break;
        }
      }

      if (targetRoom) {
        if (!targetRoom.players) targetRoom.players = [targetRoom.host];
        targetRoom.players.push(ws);
        const playerId = targetRoom.players.length;
        ws.room = targetRoom.id;
        ws.role = 'client';
        ws.playerId = playerId;

        let teamId = 1;
        if (targetRoom.config && targetRoom.config.gameMode === 1) {
          let t1 = 0, t2 = 0;
          for (const p of targetRoom.players) {
            if (p !== ws) {
              if (p.teamId === 2) t2++; else t1++;
            }
          }
          teamId = (t1 <= t2) ? 1 : 2;
        } else {
          teamId = playerId;
        }
        ws.teamId = teamId;

        console.log(`[LOBBY] Quick match: Player ${playerId} (Team ${teamId}) joined room ${targetRoom.id}`);
        sendJson(ws, {
          type: 'room_joined',
          room: targetRoom.id,
          role: 'client',
          playerId: playerId,
          teamId: teamId,
          config: targetRoom.config,
          totalPlayers: targetRoom.players.length,
        });

        for (const peer of targetRoom.players) {
          if (peer !== ws && peer.readyState === WebSocket.OPEN) {
            sendJson(peer, {
              type: 'peer_joined',
              role: 'client',
              playerId: playerId,
              teamId: teamId,
              totalPlayers: targetRoom.players.length,
            });
          }
        }
      } else {
        // Create new room as host
        let roomId = generateRoomId();
        while (rooms.has(roomId)) {
          roomId = generateRoomId();
        }
        const room = {
          id: roomId,
          host: ws,
          players: [ws],
          config: msg.config || null,
          createdAt: Date.now(),
        };
        rooms.set(roomId, room);
        ws.room = roomId;
        ws.role = 'host';
        ws.playerId = 1;
        ws.teamId = 1;
        console.log(`[LOBBY] Quick match: created room ${roomId}`);
        sendJson(ws, { type: 'room_created', room: roomId, role: 'host', playerId: 1, teamId: 1, config: room.config });
      }
      broadcastRoomList();
      break;
    }

    case 'list_rooms': {
      sendRoomList(ws);
      break;
    }

    case 'leave_room': {
      handleLeave(ws, 'Player left room');
      sendJson(ws, { type: 'left_room' });
      broadcastRoomList();
      break;
    }

    case 'ping': {
      sendJson(ws, { type: 'pong', timestamp: msg.timestamp });
      break;
    }

    default:
      console.warn('[WS] Unknown control message type:', msg.type);
  }
}

function handleLeave(ws, reason = 'Player left') {
  if (!ws.room) return;
  const room = rooms.get(ws.room);
  if (!room) {
    ws.room = null;
    ws.role = null;
    return;
  }

  console.log(`[LOBBY] Player ${ws.playerId || ws.role} left room ${ws.room}: ${reason}`);

  if (room.players) {
    const idx = room.players.indexOf(ws);
    if (idx !== -1) {
      room.players.splice(idx, 1);
    }
  }

  if (ws.role === 'host') {
    if (room.players && room.players.length > 0) {
      // Promote next player to host
      room.host = room.players[0];
      room.host.role = 'host';
      sendJson(room.host, { type: 'promoted_to_host', playerId: room.host.playerId || 1 });
      for (const peer of room.players) {
        if (peer.readyState === WebSocket.OPEN) {
          sendJson(peer, {
            type: 'peer_left',
            playerId: ws.playerId || 1,
            reason: 'Host left, new host promoted',
            totalPlayers: room.players.length,
          });
        }
      }
    } else {
      rooms.delete(ws.room);
      console.log(`[LOBBY] Room ${ws.room} deleted (empty)`);
    }
  } else {
    if (room.players) {
      for (const peer of room.players) {
        if (peer.readyState === WebSocket.OPEN) {
          sendJson(peer, {
            type: 'peer_left',
            playerId: ws.playerId || 2,
            reason: reason || 'Player disconnected',
            totalPlayers: room.players.length,
          });
        }
      }
    }
  }

  ws.room = null;
  ws.role = null;
}

function sendJson(ws, obj) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(obj));
  }
}

function sendRoomList(targetWs) {
  const available = [];
  for (const [id, room] of rooms.entries()) {
    const maxP = (room.config && room.config.maxPlayers) ? room.config.maxPlayers : 4;
    const count = room.players ? room.players.length : 1;
    if (room.host && count < maxP) {
      available.push({
        id,
        createdAt: room.createdAt,
        config: room.config,
        players: count,
        maxPlayers: maxP,
      });
    }
  }
  sendJson(targetWs, { type: 'room_list', rooms: available });
}

function broadcastRoomList() {
  const available = [];
  for (const [id, room] of rooms.entries()) {
    const maxP = (room.config && room.config.maxPlayers) ? room.config.maxPlayers : 4;
    const count = room.players ? room.players.length : 1;
    if (room.host && count < maxP) {
      available.push({
        id,
        createdAt: room.createdAt,
        config: room.config,
        players: count,
        maxPlayers: maxP,
      });
    }
  }
  const payload = JSON.stringify({ type: 'room_list', rooms: available });
  for (const client of wss.clients) {
    // Only send to clients who are in lobby (not in a running match)
    if (!client.room && client.readyState === WebSocket.OPEN) {
      client.send(payload);
    }
  }
}

// 30s heartbeat interval to clean up dead sockets and keep NAT/firewalls alive
const heartbeatInterval = setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) {
      handleLeave(ws, 'Heartbeat timeout');
      ws.terminate();
      continue;
    }
    ws.isAlive = false;
    ws.ping();
  }
}, 30000);

server.on('close', () => {
  clearInterval(heartbeatInterval);
});

server.listen(PORT, HOST, () => {
  console.log(`===============================================`);
  console.log(` gloBUS Lobby & Relay Server`);
  console.log(` Running on: http://${HOST}:${PORT}`);
  console.log(` WebSocket:  ws://${HOST}:${PORT}/ws`);
  console.log(` Web Root:   ${WEB_DIR}`);
  console.log(` Assets:     ${ROOT_DIR}/res`);
  console.log(`===============================================`);
});
