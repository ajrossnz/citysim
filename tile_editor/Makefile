CC = gcc
CFLAGS = -Wall -Wextra -O2 $(shell sdl2-config --cflags)
LDFLAGS = $(shell sdl2-config --libs)

tile_editor: tile_editor.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f tile_editor

.PHONY: clean
