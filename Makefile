CC ?= gcc
CFLAGS ?= -Wall -Wextra -O3 -g -fsanitize=address -DWFC_TOOL
LDFLAGS ?= -lraylib -lm -fsanitize=address

TARGET = main
OBJS = wfc.o geometry.o wall.o item.o cart.o mall.o net.o main.o

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

# Header dependencies
geometry.o: geometry.c geometry.h
wall.o: wall.c wall.h geometry.h
item.o: item.c item.h geometry.h
cart.o: cart.c cart.h geometry.h item.h wall.h
mall.o: mall.c mall.h geometry.h item.h wall.h wfc.h
net.o: net.c net.h item.h
main.o: main.c geometry.h wall.h item.h cart.h mall.h net.h
wfc.o: wfc.c wfc.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
