# Makefile for CitySim using Open Watcom
# For 386 DOS target with EGA graphics

CC = wcc386
LINK = wlink
CFLAGS = -bt=dos -6r -fp6 -ox -s -w4 -e25 -fo=$^@
LDFLAGS = system dos4g

OBJS = main.obj graphics.obj game.obj

citysim.exe: $(OBJS)
	$(LINK) $(LDFLAGS) file {$(OBJS)} name citysim

main.obj: main.c citysim.h
	$(CC) $(CFLAGS) main.c

graphics.obj: graphics.c citysim.h tile_editor/tiles.h
	$(CC) $(CFLAGS) graphics.c

game.obj: game.c citysim.h
	$(CC) $(CFLAGS) game.c

clean: .SYMBOLIC
	del *.obj *.err citysim.exe
