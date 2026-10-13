CC ?= gcc
CFLAGS ?= -Wall -Wextra -O3 -g -fsanitize=address -DWFC_TOOL
LDFLAGS ?= -lraylib -lm -fsanitize=address

# Emscripten compiler detection
EMCC ?= $(shell which emcc 2>/dev/null || if [ -x $(HOME)/.emsdk/upstream/emscripten/emcc ]; then echo $(HOME)/.emsdk/upstream/emscripten/emcc; else echo emcc; fi)
RAYLIB_WEB_DIR ?= lib/raylib-6.0_webassembly

TARGET = main
SRCS = wfc.c geometry.c wall.c item.c cart.c mall.c net.c main.c
OBJS = wfc.o geometry.o wall.o item.o cart.o mall.o net.o main.o

WEB_OUT_DIR = web
WEB_TARGET = $(WEB_OUT_DIR)/main.js

.PHONY: all native web serve clean run

all: $(TARGET)

native: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

# Header dependencies
geometry.o: geometry.c geometry.h
wall.o: wall.c wall.h geometry.h
item.o: item.c item.h geometry.h
cart.o: cart.c cart.h geometry.h item.h wall.h
mall.o: mall.c mall.h geometry.h item.h wall.h wfc.h
net.o: net.c net.h item.h mall.h
main.o: main.c geometry.h wall.h item.h cart.h mall.h net.h
wfc.o: wfc.c wfc.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Web target using Emscripten
web: $(WEB_TARGET)

$(WEB_TARGET): $(SRCS)
	@mkdir -p $(WEB_OUT_DIR)
	$(EMCC) -O3 -Wall -DWFC_TOOL -DPLATFORM_WEB $(SRCS) \
		-I$(RAYLIB_WEB_DIR)/include \
		$(RAYLIB_WEB_DIR)/lib/libraylib.web.a \
		-sUSE_GLFW=3 \
		-sMIN_WEBGL_VERSION=1 \
		-sMAX_WEBGL_VERSION=2 \
		-sGL_ENABLE_GET_PROC_ADDRESS=1 \
		-sSTACK_SIZE=4MB \
		-sINITIAL_MEMORY=256MB \
		-sALLOW_MEMORY_GROWTH=1 \
		-sMAXIMUM_MEMORY=512MB \
		-sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString"]' \
		-sEXPORTED_FUNCTIONS='["_main","_web_start_singleplayer","_web_start_host","_web_start_join","_web_return_to_lobby","_web_request_rematch","_web_resize","_web_zoom_in","_web_zoom_out","_web_peer_joined","_web_tab_resumed","_web_set_room_config_json","_web_set_player_info"]' \
		--preload-file res \
		-o $(WEB_TARGET)
	@echo "=============================================="
	@echo " WebAssembly build complete in $(WEB_OUT_DIR)/"
	@echo " Run 'make serve' to start the lobby server!"
	@echo "=============================================="

# Run native game
run: $(TARGET)
	./$(TARGET)

# Run lobby relay server
serve: web
	@cd server && npm install --silent 2>/dev/null || true
	@node server/server.js

clean:
	rm -f $(OBJS) $(TARGET)
	rm -f $(WEB_OUT_DIR)/main.js $(WEB_OUT_DIR)/main.wasm $(WEB_OUT_DIR)/main.data
