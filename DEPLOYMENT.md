# gloBUS - Web & VPS Deployment Guide

This guide walks you through compiling the game with **Emscripten** (WebAssembly), running the **lobby & relay server**, and deploying the game to your **VPS**.

---

## 🚀 Quick Start (Local Testing)

### 1. Build the Web Game
Ensure Emscripten is installed/active, then run:
```bash
make web
```
This compiles the C raylib game and bundles supermarket assets into `web/`:
- `web/main.js` (Web loader & JavaScript glue)
- `web/main.wasm` (Compiled WebAssembly game engine)
- `web/main.data` (Preloaded textures and grocery assets)

### 2. Start the Lobby & Relay Server
```bash
make serve
```
Open **[http://localhost:8080](http://localhost:8080)** in your browser!

---

## 🌐 Deploying to your VPS

Choose whichever deployment method fits your VPS setup:

---

### Option A: Docker / Docker Compose (Recommended)

Docker packages Node.js, static files, and the relay server into an isolated container with zero host dependencies.

1. **Build the web files on your machine** (or on the VPS):
   ```bash
   make web
   ```
2. **Transfer the repository to your VPS**:
   ```bash
   rsync -avz --exclude 'node_modules' --exclude '*.o' . user@your-vps:/opt/globus
   ```
3. **Run with Docker Compose on your VPS**:
   ```bash
   cd /opt/globus
   docker compose up -d --build
   ```
The game is now live on `http://<vps-ip>:8080`!

---

### Option B: Node.js & Systemd (Native Linux Service)

If your VPS has Node.js (v18+) installed:

1. **Copy the files to the VPS**:
   ```bash
   rsync -avz --exclude 'node_modules' --exclude '*.o' . user@your-vps:/opt/globus
   ```
2. **Install server dependencies on the VPS**:
   ```bash
   cd /opt/globus/server
   npm install --production
   ```
3. **Install and start the systemd service**:
   ```bash
   sudo cp /opt/globus/deploy/globus.service /etc/systemd/system/
   sudo systemctl daemon-reload
   sudo systemctl enable --now globus
   ```
4. **Check service status**:
   ```bash
   sudo systemctl status globus
   sudo journalctl -u globus -f
   ```

---

### Option C: Reverse Proxy with Nginx & HTTPS / SSL

To serve the game on a custom domain (e.g., `https://globus.yourdomain.com`) with automated SSL:

1. **Copy the Nginx configuration**:
   ```bash
   sudo cp /opt/globus/deploy/nginx.conf /etc/nginx/sites-available/globus.conf
   sudo ln -s /etc/nginx/sites-available/globus.conf /etc/nginx/sites-enabled/
   ```
2. **Edit the domain name**:
   ```bash
   sudo nano /etc/nginx/sites-available/globus.conf
   # Replace your-domain.com with your actual domain name
   ```
3. **Test and reload Nginx**:
   ```bash
   sudo nginx -t
   sudo systemctl reload nginx
   ```
4. **Enable HTTPS with Let's Encrypt Certbot**:
   ```bash
   sudo certbot --nginx -d globus.yourdomain.com
   ```
Certbot automatically enables SSL and keeps WebSockets (`wss://`) working!

---

## 🎮 How the Lobby & Relay Architecture Works

```
                                  +------------------------------+
                                  |         Lobby Server         |
                                  |        (server/server.js)    |
                                  |                              |
                                  |  - Static Web Host (port 80) |
                                  |  - Room Management & Match   |
                                  +--------------+---------------+
                                                 |
                         +-----------------------+-----------------------+
                         | WebSocket (JSON Lobby / Raw Binary Relay)    |
                         v                                               v
        +--------------------------------+              +--------------------------------+
        |        Player 1 (Host)         |              |       Player 2 (Client)        |
        |      WebAssembly / Raylib      |              |      WebAssembly / Raylib      |
        |           (Blue Cart)          |              |           (Red Cart)           |
        +--------------------------------+              +--------------------------------+
```

1. **Lobby & Matchmaking**:
   - Players can choose **Solo Practice**, **Quick Match**, **Host Room**, or **Join Room**.
   - Room codes (e.g. `CART42`) can be shared via direct link:
     `http://<vps-ip>:8080/?room=CART42`
   - When opened, Player 2 automatically connects and joins the room.

2. **Binary Datagram Relay**:
   - Control messages (room creation, joining, player leave) are exchanged as lightweight JSON text frames.
   - High-frequency game packets (cart physics, bumps, grocery pickups, shopping lists, game over) are exchanged directly as raw binary frames.
   - The relay server forwards binary packets between the two room peers with zero decoding overhead, minimizing VPS CPU and latency.

3. **Fallback & Native Game Support**:
   - The native desktop game (`./main`, `./main --host`, `./main --join`) remains fully supported via standard UDP.
