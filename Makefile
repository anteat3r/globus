default:
	gcc -o main main.c -O3 -lraylib -lm -g -fsanitize=address -DWFC_TOOL && ./main
