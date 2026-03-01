# CLAUDE.md - CitySim Development Guide

## Build

```bash
wmake          # Build with Open Watcom (produces citysim.exe)
wmake clean    # Remove build artifacts
```

Requires Open Watcom C/C++ (`wcc386`, `wlink`) and DOS4GW extender. Test with `dosbox citysim.exe`.

## Project Structure

- `citysim.h` - All structs, constants, and prototypes. Single header for the whole project.
- `main.c` - Game loop. Manages render/UI refresh logic and game state transitions.
- `graphics.c` - EGA mode 0x10 (640x350 16-colour) graphics. Contains `set_pixel`, `draw_filled_rect`, 5x7 bitmap font (`draw_char`/`draw_text`/`draw_number`), tile rendering, status bar (`draw_ui`), help screen, and human detail view.
- `game.c` - Game logic: map init, simulation tick, human AI, building placement, input handling, economy.

## Key Conventions

- **C89 compatible** - No C99 features. Variables declared at top of blocks. Open Watcom targeting DOS.
- **No dynamic allocation** - All arrays are fixed-size in the `GameState` struct.
- **EGA register programming** - Direct port I/O via `outp()`. Write mode 0 with Set/Reset for pixel and rect operations. Always reset EGA registers to defaults after drawing.
- **Coordinate system** - Map is 64x64 tiles. Screen is 640x350 pixels. Tiles are 16x16 pixels. Viewport shows 40x21 tiles. Status bar occupies y=330-349 (bottom 20 pixels).
- **Render strategy** - Full redraw on scroll/cursor changes (clear + map + UI). Incremental UI-only refresh on time/funds/tool changes. Minimise `draw_ui` calls since it clears the status bar area each time.

## Common Tasks

### Adding a new tile type
1. Add `#define TILE_xxx` constant in `citysim.h`
2. Add colour mapping in `draw_tile()` switch in `graphics.c`
3. Add cost in `get_tile_cost()` in `game.c`
4. Add name in `get_tile_name()` in `game.c`
5. Add key binding in `handle_input()` in `game.c`

### Adding status bar info
Edit `draw_ui()` in `graphics.c`. Use `draw_text()` for labels, `draw_number()` for values. Status bar y=336 for vertically centered text. Keep total width under 640px.

### Modifying the font
The `font_data[96][7]` array in `graphics.c` contains 5x7 bitmap glyphs for ASCII 32-127. Each byte row has bits 4-0 mapping to pixels left-to-right.

## Gotchas

- `draw_filled_rect` manipulates EGA registers directly for speed. Calling `set_pixel` immediately after without resetting registers will produce wrong colours. Both functions reset registers on exit, but be careful if adding new drawing code.
- `unsigned char` for map coordinates means 0-255 range. Don't subtract below 0 without checking.
- `delay()` is Watcom's built-in millisecond delay. No need to include extra headers.
- The game calls `exit(0)` directly on ESC from city view (in `handle_input`), bypassing the main loop's return path.
