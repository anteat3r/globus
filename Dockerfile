FROM node:22-alpine

WORKDIR /app

# Install server dependencies
COPY server/package*.json ./server/
RUN cd server && npm install --production

# Copy server, web client, and game assets
COPY server/ ./server/
COPY web/ ./web/
COPY res/ ./res/

ENV PORT=8080
ENV HOST=0.0.0.0

EXPOSE 8080

CMD ["node", "server/server.js"]
