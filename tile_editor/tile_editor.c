/*
 * tile_editor.c - EGA Tile Editor (SDL2, Linux)
 *
 * A game-agnostic tile editor for creating pixel art using the 16-colour
 * EGA palette. Tiles are NxM tile units (each unit 16x16 pixels).
 * Saves to a text-based project file and exports C headers.
 *
 * Build: gcc -o tile_editor tile_editor.c $(sdl2-config --cflags --libs)
 */

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define TILE_UNIT        16
#define MAX_TILES        256
#define MAX_TILE_NAME    64
#define MAX_TILE_UNITS   8       /* max 8x8 tile units = 128x128 px */
#define MAX_TILE_PX      (MAX_TILE_UNITS * TILE_UNIT)

#define PANEL_LEFT_W     160
#define PANEL_RIGHT_W    140
#define STATUS_BAR_H     72
#define TILE_LIST_ITEM_H 24

#define INITIAL_W        1024
#define INITIAL_H        768

/* Transparency */
#define COLOR_TRANSPARENT 255

/* Text input modes */
enum {
    INPUT_NONE = 0,
    INPUT_TILE_NAME,
    INPUT_TILE_WIDTH,
    INPUT_TILE_HEIGHT,
    INPUT_RENAME,
    INPUT_CONFIRM_CLEAR,
    INPUT_CONFIRM_DELETE,
    INPUT_CONFIRM_QUIT,
    INPUT_XPM_PATH
};

/* Tools */
enum {
    TOOL_DRAW = 0,
    TOOL_FILL,
    TOOL_SELECT
};

/* ------------------------------------------------------------------ */
/* EGA palette                                                        */
/* ------------------------------------------------------------------ */

static const SDL_Color ega_palette[16] = {
    {   0,   0,   0, 255 },  /* 0  Black        */
    {   0,   0, 170, 255 },  /* 1  Blue         */
    {   0, 170,   0, 255 },  /* 2  Green        */
    {   0, 170, 170, 255 },  /* 3  Cyan         */
    { 170,   0,   0, 255 },  /* 4  Red          */
    { 170,   0, 170, 255 },  /* 5  Magenta      */
    { 170,  85,   0, 255 },  /* 6  Brown        */
    { 170, 170, 170, 255 },  /* 7  Light Gray   */
    {  85,  85,  85, 255 },  /* 8  Dark Gray    */
    {  85,  85, 255, 255 },  /* 9  Light Blue   */
    {  85, 255,  85, 255 },  /* 10 Light Green  */
    {  85, 255, 255, 255 },  /* 11 Light Cyan   */
    { 255,  85,  85, 255 },  /* 12 Light Red    */
    { 255,  85, 255, 255 },  /* 13 Light Magenta*/
    { 255, 255,  85, 255 },  /* 14 Yellow       */
    { 255, 255, 255, 255 },  /* 15 White        */
};

static const char *ega_names[16] = {
    "Black", "Blue", "Green", "Cyan",
    "Red", "Magenta", "Brown", "LtGray",
    "DkGray", "LtBlue", "LtGreen", "LtCyan",
    "LtRed", "LtMagenta", "Yellow", "White"
};

/* ------------------------------------------------------------------ */
/* Tile data                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char name[MAX_TILE_NAME];
    int  units_w;   /* width in tile units  */
    int  units_h;   /* height in tile units */
    int  px_w;      /* units_w * TILE_UNIT  */
    int  px_h;      /* units_h * TILE_UNIT  */
    unsigned char pixels[MAX_TILE_PX][MAX_TILE_PX];
} Tile;

/* ------------------------------------------------------------------ */
/* Editor state                                                       */
/* ------------------------------------------------------------------ */

static Tile  tiles[MAX_TILES];
static int   tile_count = 0;
static int   selected_tile = -1;
static int   current_colour = 2;  /* start on green */
static int   current_tool = TOOL_DRAW;
static int   unsaved = 0;

/* Undo: single-level snapshot of current tile's pixels */
static unsigned char undo_buf[MAX_TILE_PX][MAX_TILE_PX];
static int undo_valid = 0;

/* Selection state */
static int sel_active = 0;      /* 1 = marquee defined */
static int sel_dragging = 0;    /* 1 = currently dragging out marquee */
static int sel_x1, sel_y1;      /* start corner (pixel coords in tile) */
static int sel_x2, sel_y2;      /* end corner */
/* Floating selection */
static int float_active = 0;    /* 1 = floating selection exists */
static int float_w, float_h;
static int float_mx, float_my;  /* mouse pixel pos where float follows */
static unsigned char float_buf[MAX_TILE_PX][MAX_TILE_PX];
static int float_is_move = 0;   /* 1 = move (erased source), 0 = copy */

/* Text input state */
static int  input_mode = INPUT_NONE;
static char input_buf[MAX_TILE_NAME];
static int  input_len = 0;
/* For new-tile flow, stash the name and width */
static char new_tile_name[MAX_TILE_NAME];
static int  new_tile_width = 0;

/* Tile list scroll offset */
static int tile_list_scroll = 0;

/* Window / layout */
static SDL_Window   *window;
static SDL_Renderer *renderer;
static int win_w = INITIAL_W;
static int win_h = INITIAL_H;

/* File paths */
static const char *project_file = "tiles.dat";
static const char *header_file  = "tiles.h";

/* ------------------------------------------------------------------ */
/* Forward declarations                                               */
/* ------------------------------------------------------------------ */

static void save_project(void);
static void load_project(void);
static void export_header(void);
static void snapshot_undo(void);
static void flood_fill(Tile *t, int x, int y, int old_c, int new_c);

/* ------------------------------------------------------------------ */
/* Layout helpers                                                     */
/* ------------------------------------------------------------------ */

static void get_canvas_rect(SDL_Rect *r)
{
    r->x = PANEL_LEFT_W;
    r->y = 0;
    r->w = win_w - PANEL_LEFT_W - PANEL_RIGHT_W;
    r->h = win_h - STATUS_BAR_H;
}

static int get_zoom(void)
{
    SDL_Rect cr;
    int zx, zy;
    get_canvas_rect(&cr);
    if (selected_tile < 0 || selected_tile >= tile_count) return 1;
    zx = (cr.w - 2) / tiles[selected_tile].px_w;
    zy = (cr.h - 2) / tiles[selected_tile].px_h;
    if (zx < zy) return zx > 1 ? zx : 1;
    return zy > 1 ? zy : 1;
}

/* Map mouse position to pixel coordinate in tile. Returns 0 if outside. */
static int mouse_to_pixel(int mx, int my, int *px, int *py)
{
    SDL_Rect cr;
    int zoom, ox, oy, lx, ly;
    Tile *t;
    if (selected_tile < 0) return 0;
    t = &tiles[selected_tile];
    get_canvas_rect(&cr);
    zoom = get_zoom();
    ox = cr.x + (cr.w - t->px_w * zoom) / 2;
    oy = cr.y + (cr.h - t->px_h * zoom) / 2;
    lx = mx - ox;
    ly = my - oy;
    if (lx < 0 || ly < 0) return 0;
    lx /= zoom;
    ly /= zoom;
    if (lx >= t->px_w || ly >= t->px_h) return 0;
    *px = lx;
    *py = ly;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Simple text rendering using SDL (built-in rectangles, no TTF)      */
/* ------------------------------------------------------------------ */

/* Tiny 4x6 bitmap font for UI text. Covers ASCII 32-126. */
/* Each glyph is 6 rows of 4-bit wide data (MSB = left). */
static const unsigned char mini_font[95][6] = {
    /* 32 ' ' */ {0x0,0x0,0x0,0x0,0x0,0x0},
    /* 33 '!' */ {0x4,0x4,0x4,0x0,0x4,0x0},
    /* 34 '"' */ {0xA,0xA,0x0,0x0,0x0,0x0},
    /* 35 '#' */ {0xA,0xF,0xA,0xF,0xA,0x0},
    /* 36 '$' */ {0x4,0xE,0xC,0x6,0xE,0x4},
    /* 37 '%' */ {0x9,0x2,0x4,0x9,0x0,0x0},
    /* 38 '&' */ {0x4,0xA,0x4,0xA,0x5,0x0},
    /* 39 '\''*/ {0x4,0x4,0x0,0x0,0x0,0x0},
    /* 40 '(' */ {0x2,0x4,0x4,0x4,0x2,0x0},
    /* 41 ')' */ {0x4,0x2,0x2,0x2,0x4,0x0},
    /* 42 '*' */ {0x0,0xA,0x4,0xA,0x0,0x0},
    /* 43 '+' */ {0x0,0x4,0xE,0x4,0x0,0x0},
    /* 44 ',' */ {0x0,0x0,0x0,0x4,0x4,0x8},
    /* 45 '-' */ {0x0,0x0,0xE,0x0,0x0,0x0},
    /* 46 '.' */ {0x0,0x0,0x0,0x0,0x4,0x0},
    /* 47 '/' */ {0x1,0x2,0x4,0x8,0x0,0x0},
    /* 48 '0' */ {0x6,0x9,0x9,0x9,0x6,0x0},
    /* 49 '1' */ {0x4,0xC,0x4,0x4,0xE,0x0},
    /* 50 '2' */ {0x6,0x9,0x2,0x4,0xF,0x0},
    /* 51 '3' */ {0xE,0x1,0x6,0x1,0xE,0x0},
    /* 52 '4' */ {0x2,0x6,0xA,0xF,0x2,0x0},
    /* 53 '5' */ {0xF,0x8,0xE,0x1,0xE,0x0},
    /* 54 '6' */ {0x6,0x8,0xE,0x9,0x6,0x0},
    /* 55 '7' */ {0xF,0x1,0x2,0x4,0x4,0x0},
    /* 56 '8' */ {0x6,0x9,0x6,0x9,0x6,0x0},
    /* 57 '9' */ {0x6,0x9,0x7,0x1,0x6,0x0},
    /* 58 ':' */ {0x0,0x4,0x0,0x4,0x0,0x0},
    /* 59 ';' */ {0x0,0x4,0x0,0x4,0x8,0x0},
    /* 60 '<' */ {0x2,0x4,0x8,0x4,0x2,0x0},
    /* 61 '=' */ {0x0,0xE,0x0,0xE,0x0,0x0},
    /* 62 '>' */ {0x8,0x4,0x2,0x4,0x8,0x0},
    /* 63 '?' */ {0x6,0x9,0x2,0x0,0x4,0x0},
    /* 64 '@' */ {0x6,0x9,0xB,0x8,0x6,0x0},
    /* 65 'A' */ {0x6,0x9,0xF,0x9,0x9,0x0},
    /* 66 'B' */ {0xE,0x9,0xE,0x9,0xE,0x0},
    /* 67 'C' */ {0x6,0x9,0x8,0x9,0x6,0x0},
    /* 68 'D' */ {0xE,0x9,0x9,0x9,0xE,0x0},
    /* 69 'E' */ {0xF,0x8,0xE,0x8,0xF,0x0},
    /* 70 'F' */ {0xF,0x8,0xE,0x8,0x8,0x0},
    /* 71 'G' */ {0x6,0x8,0xB,0x9,0x6,0x0},
    /* 72 'H' */ {0x9,0x9,0xF,0x9,0x9,0x0},
    /* 73 'I' */ {0xE,0x4,0x4,0x4,0xE,0x0},
    /* 74 'J' */ {0x1,0x1,0x1,0x9,0x6,0x0},
    /* 75 'K' */ {0x9,0xA,0xC,0xA,0x9,0x0},
    /* 76 'L' */ {0x8,0x8,0x8,0x8,0xF,0x0},
    /* 77 'M' */ {0x9,0xF,0xF,0x9,0x9,0x0},
    /* 78 'N' */ {0x9,0xD,0xF,0xB,0x9,0x0},
    /* 79 'O' */ {0x6,0x9,0x9,0x9,0x6,0x0},
    /* 80 'P' */ {0xE,0x9,0xE,0x8,0x8,0x0},
    /* 81 'Q' */ {0x6,0x9,0x9,0xA,0x5,0x0},
    /* 82 'R' */ {0xE,0x9,0xE,0xA,0x9,0x0},
    /* 83 'S' */ {0x6,0x8,0x6,0x1,0xE,0x0},
    /* 84 'T' */ {0xE,0x4,0x4,0x4,0x4,0x0},
    /* 85 'U' */ {0x9,0x9,0x9,0x9,0x6,0x0},
    /* 86 'V' */ {0x9,0x9,0x9,0x6,0x6,0x0},
    /* 87 'W' */ {0x9,0x9,0xF,0xF,0x9,0x0},
    /* 88 'X' */ {0x9,0x9,0x6,0x9,0x9,0x0},
    /* 89 'Y' */ {0x9,0x9,0x6,0x4,0x4,0x0},
    /* 90 'Z' */ {0xF,0x1,0x6,0x8,0xF,0x0},
    /* 91 '[' */ {0x6,0x4,0x4,0x4,0x6,0x0},
    /* 92 '\\'*/ {0x8,0x4,0x2,0x1,0x0,0x0},
    /* 93 ']' */ {0x6,0x2,0x2,0x2,0x6,0x0},
    /* 94 '^' */ {0x4,0xA,0x0,0x0,0x0,0x0},
    /* 95 '_' */ {0x0,0x0,0x0,0x0,0xF,0x0},
    /* 96 '`' */ {0x4,0x2,0x0,0x0,0x0,0x0},
    /* 97 'a' */ {0x0,0x6,0xB,0x9,0x7,0x0},
    /* 98 'b' */ {0x8,0xE,0x9,0x9,0xE,0x0},
    /* 99 'c' */ {0x0,0x6,0x8,0x8,0x6,0x0},
    /*100 'd' */ {0x1,0x7,0x9,0x9,0x7,0x0},
    /*101 'e' */ {0x0,0x6,0xF,0x8,0x6,0x0},
    /*102 'f' */ {0x2,0x4,0xE,0x4,0x4,0x0},
    /*103 'g' */ {0x0,0x7,0x9,0x7,0x1,0x6},
    /*104 'h' */ {0x8,0xE,0x9,0x9,0x9,0x0},
    /*105 'i' */ {0x4,0x0,0x4,0x4,0x4,0x0},
    /*106 'j' */ {0x2,0x0,0x2,0x2,0xA,0x4},
    /*107 'k' */ {0x8,0x9,0xE,0xA,0x9,0x0},
    /*108 'l' */ {0xC,0x4,0x4,0x4,0xE,0x0},
    /*109 'm' */ {0x0,0x9,0xF,0x9,0x9,0x0},
    /*110 'n' */ {0x0,0xE,0x9,0x9,0x9,0x0},
    /*111 'o' */ {0x0,0x6,0x9,0x9,0x6,0x0},
    /*112 'p' */ {0x0,0xE,0x9,0xE,0x8,0x8},
    /*113 'q' */ {0x0,0x7,0x9,0x7,0x1,0x1},
    /*114 'r' */ {0x0,0xA,0xC,0x8,0x8,0x0},
    /*115 's' */ {0x0,0x6,0xC,0x2,0xC,0x0},
    /*116 't' */ {0x4,0xE,0x4,0x4,0x2,0x0},
    /*117 'u' */ {0x0,0x9,0x9,0x9,0x7,0x0},
    /*118 'v' */ {0x0,0x9,0x9,0x6,0x6,0x0},
    /*119 'w' */ {0x0,0x9,0x9,0xF,0x6,0x0},
    /*120 'x' */ {0x0,0x9,0x6,0x6,0x9,0x0},
    /*121 'y' */ {0x0,0x9,0x9,0x7,0x1,0x6},
    /*122 'z' */ {0x0,0xF,0x2,0x4,0xF,0x0},
    /*123 '{' */ {0x2,0x4,0x8,0x4,0x2,0x0},
    /*124 '|' */ {0x4,0x4,0x4,0x4,0x4,0x0},
    /*125 '}' */ {0x8,0x4,0x2,0x4,0x8,0x0},
    /*126 '~' */ {0x0,0x5,0xA,0x0,0x0,0x0},
};

/* Draw a single character at (x,y) with scale factor. */
static void draw_char_ui(int x, int y, char ch, int scale,
                         Uint8 r, Uint8 g, Uint8 b)
{
    int idx, row, col;
    const unsigned char *glyph;
    SDL_Rect rc;

    if (ch < 32 || ch > 126) return;
    idx = ch - 32;
    glyph = mini_font[idx];

    SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    for (row = 0; row < 6; row++) {
        for (col = 0; col < 4; col++) {
            if (glyph[row] & (0x8 >> col)) {
                rc.x = x + col * scale;
                rc.y = y + row * scale;
                rc.w = scale;
                rc.h = scale;
                SDL_RenderFillRect(renderer, &rc);
            }
        }
    }
}

/* Draw a string at (x,y) with scale. Returns width drawn. */
static int draw_text_ui(int x, int y, const char *s, int scale,
                        Uint8 r, Uint8 g, Uint8 b)
{
    int cx = x;
    while (*s) {
        draw_char_ui(cx, y, *s, scale, r, g, b);
        cx += 5 * scale;
        s++;
    }
    return cx - x;
}

/* ------------------------------------------------------------------ */
/* Flood fill                                                         */
/* ------------------------------------------------------------------ */

static void flood_fill(Tile *t, int x, int y, int old_c, int new_c)
{
    /* Stack-based to avoid deep recursion */
    typedef struct { short x, y; } Pt;
    static Pt stack[MAX_TILE_PX * MAX_TILE_PX];
    int sp = 0;

    if (old_c == new_c) return;

    stack[sp].x = (short)x;
    stack[sp].y = (short)y;
    sp++;

    while (sp > 0) {
        sp--;
        x = stack[sp].x;
        y = stack[sp].y;
        if (x < 0 || x >= t->px_w || y < 0 || y >= t->px_h) continue;
        if (t->pixels[y][x] != (unsigned char)old_c) continue;
        t->pixels[y][x] = (unsigned char)new_c;

        if (sp + 4 >= MAX_TILE_PX * MAX_TILE_PX) continue;
        stack[sp].x = (short)(x + 1); stack[sp].y = (short)y; sp++;
        stack[sp].x = (short)(x - 1); stack[sp].y = (short)y; sp++;
        stack[sp].x = (short)x; stack[sp].y = (short)(y + 1); sp++;
        stack[sp].x = (short)x; stack[sp].y = (short)(y - 1); sp++;
    }
}

/* ------------------------------------------------------------------ */
/* Undo                                                               */
/* ------------------------------------------------------------------ */

static void snapshot_undo(void)
{
    Tile *t;
    if (selected_tile < 0) return;
    t = &tiles[selected_tile];
    memcpy(undo_buf, t->pixels, sizeof(undo_buf));
    undo_valid = 1;
}

static void do_undo(void)
{
    Tile *t;
    unsigned char tmp[MAX_TILE_PX][MAX_TILE_PX];
    if (!undo_valid || selected_tile < 0) return;
    t = &tiles[selected_tile];
    memcpy(tmp, t->pixels, sizeof(tmp));
    memcpy(t->pixels, undo_buf, sizeof(undo_buf));
    memcpy(undo_buf, tmp, sizeof(undo_buf));
    unsaved = 1;
}

/* ------------------------------------------------------------------ */
/* Selection helpers                                                   */
/* ------------------------------------------------------------------ */

/* Normalise selection so x1<=x2, y1<=y2 */
static void sel_normalise(int *x1, int *y1, int *x2, int *y2)
{
    int tmp;
    if (*x1 > *x2) { tmp = *x1; *x1 = *x2; *x2 = tmp; }
    if (*y1 > *y2) { tmp = *y1; *y1 = *y2; *y2 = tmp; }
}

static void sel_clear(void)
{
    sel_active = 0;
    sel_dragging = 0;
    float_active = 0;
}

/* Lift selection into float_buf. If is_move, erase source pixels. */
static void sel_lift(int is_move)
{
    Tile *t;
    int nx1, ny1, nx2, ny2, x, y;
    if (!sel_active || selected_tile < 0) return;
    t = &tiles[selected_tile];

    nx1 = sel_x1; ny1 = sel_y1; nx2 = sel_x2; ny2 = sel_y2;
    sel_normalise(&nx1, &ny1, &nx2, &ny2);
    float_w = nx2 - nx1 + 1;
    float_h = ny2 - ny1 + 1;

    snapshot_undo();
    memset(float_buf, COLOR_TRANSPARENT, sizeof(float_buf));
    for (y = 0; y < float_h; y++) {
        for (x = 0; x < float_w; x++) {
            float_buf[y][x] = t->pixels[ny1 + y][nx1 + x];
            if (is_move)
                t->pixels[ny1 + y][nx1 + x] = 0;
        }
    }

    float_active = 1;
    float_is_move = is_move;
    float_mx = -1;
    float_my = -1;
    unsaved = 1;
}

/* Stamp floating selection at pixel position (px, py) in tile */
static void sel_stamp(int px, int py)
{
    Tile *t;
    int x, y, dx, dy;
    if (!float_active || selected_tile < 0) return;
    t = &tiles[selected_tile];

    for (y = 0; y < float_h; y++) {
        for (x = 0; x < float_w; x++) {
            dx = px + x;
            dy = py + y;
            if (dx >= 0 && dx < t->px_w && dy >= 0 && dy < t->px_h) {
                unsigned char c = float_buf[y][x];
                if (c != COLOR_TRANSPARENT)
                    t->pixels[dy][dx] = c;
            }
        }
    }

    float_active = 0;
    sel_active = 0;
    unsaved = 1;
}

/* ------------------------------------------------------------------ */
/* Save / Load / Export                                                */
/* ------------------------------------------------------------------ */

static void save_project(void)
{
    FILE *f;
    int i, x, y;

    f = fopen(project_file, "w");
    if (!f) {
        fprintf(stderr, "Failed to save %s\n", project_file);
        return;
    }

    fprintf(f, "TILESET 1.0\n");
    for (i = 0; i < tile_count; i++) {
        Tile *t = &tiles[i];
        fprintf(f, "TILE %s %d %d\n", t->name, t->units_w, t->units_h);
        for (y = 0; y < t->px_h; y++) {
            for (x = 0; x < t->px_w; x++) {
                if (x > 0) fprintf(f, " ");
                fprintf(f, "%d", t->pixels[y][x]);
            }
            fprintf(f, "\n");
        }
        fprintf(f, "END\n");
    }

    fclose(f);

    export_header();
    unsaved = 0;
    printf("Saved %s and %s (%d tiles)\n", project_file, header_file, tile_count);
}

static void load_project(void)
{
    FILE *f;
    char line[4096];
    char name[MAX_TILE_NAME];
    int uw, uh;

    f = fopen(project_file, "r");
    if (!f) return;

    tile_count = 0;
    selected_tile = -1;

    /* Read header line */
    if (!fgets(line, sizeof(line), f)) { fclose(f); return; }

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "TILE %63s %d %d", name, &uw, &uh) == 3) {
            Tile *t;
            int y, x, val;
            if (tile_count >= MAX_TILES) break;
            if (uw < 1 || uw > MAX_TILE_UNITS || uh < 1 || uh > MAX_TILE_UNITS)
                continue;

            t = &tiles[tile_count];
            memset(t, 0, sizeof(*t));
            snprintf(t->name, MAX_TILE_NAME, "%s", name);
            t->units_w = uw;
            t->units_h = uh;
            t->px_w = uw * TILE_UNIT;
            t->px_h = uh * TILE_UNIT;

            for (y = 0; y < t->px_h; y++) {
                if (!fgets(line, sizeof(line), f)) break;
                if (strncmp(line, "END", 3) == 0) break;
                {
                    char *p = line;
                    for (x = 0; x < t->px_w; x++) {
                        while (*p == ' ' || *p == '\t') p++;
                        val = (int)strtol(p, &p, 10);
                        if (val < 0) val = 0;
                        if (val != COLOR_TRANSPARENT && val > 15) val = 15;
                        t->pixels[y][x] = (unsigned char)val;
                    }
                }
            }
            /* Skip to END if we haven't reached it */
            while (fgets(line, sizeof(line), f)) {
                if (strncmp(line, "END", 3) == 0) break;
            }
            tile_count++;
        }
    }

    fclose(f);

    if (tile_count > 0) selected_tile = 0;
    unsaved = 0;
    printf("Loaded %d tiles from %s\n", tile_count, project_file);
}

static void export_header(void)
{
    FILE *f;
    int i, x, y;

    f = fopen(header_file, "w");
    if (!f) {
        fprintf(stderr, "Failed to write %s\n", header_file);
        return;
    }

    fprintf(f, "/* Auto-generated by tile_editor - do not edit by hand */\n");
    fprintf(f, "#ifndef TILES_H\n#define TILES_H\n\n");
    fprintf(f, "#define COLOR_TRANSPARENT 255\n\n");

    for (i = 0; i < tile_count; i++) {
        Tile *t = &tiles[i];
        fprintf(f, "/* %s: %dx%d (%dx%d pixels) */\n",
                t->name, t->units_w, t->units_h, t->px_w, t->px_h);
        fprintf(f, "static const unsigned char tile_%s[%d][%d] = {\n",
                t->name, t->px_h, t->px_w);
        for (y = 0; y < t->px_h; y++) {
            fprintf(f, "    {");
            for (x = 0; x < t->px_w; x++) {
                if (x > 0) fprintf(f, ",");
                fprintf(f, "%3d", t->pixels[y][x]);
            }
            fprintf(f, "}%s\n", (y < t->px_h - 1) ? "," : "");
        }
        fprintf(f, "};\n\n");
    }

    if (tile_count > 0) {
        fprintf(f, "#define TILE_COUNT %d\n\n", tile_count);
        fprintf(f, "static const char *tile_names[TILE_COUNT] = {");
        for (i = 0; i < tile_count; i++) {
            if (i > 0) fprintf(f, ",");
            fprintf(f, "\"%s\"", tiles[i].name);
        }
        fprintf(f, "};\n");
        fprintf(f, "static const int tile_widths[TILE_COUNT] = {");
        for (i = 0; i < tile_count; i++) {
            if (i > 0) fprintf(f, ",");
            fprintf(f, "%d", tiles[i].px_w);
        }
        fprintf(f, "};\n");
        fprintf(f, "static const int tile_heights[TILE_COUNT] = {");
        for (i = 0; i < tile_count; i++) {
            if (i > 0) fprintf(f, ",");
            fprintf(f, "%d", tiles[i].px_h);
        }
        fprintf(f, "};\n");
    }

    fprintf(f, "\n#endif\n");
    fclose(f);
}

/* ------------------------------------------------------------------ */
/* Create / delete tile helpers                                       */
/* ------------------------------------------------------------------ */

static int create_tile(const char *name, int uw, int uh)
{
    Tile *t;
    if (tile_count >= MAX_TILES) return -1;
    if (uw < 1 || uw > MAX_TILE_UNITS || uh < 1 || uh > MAX_TILE_UNITS)
        return -1;

    t = &tiles[tile_count];
    memset(t, 0, sizeof(*t));
    snprintf(t->name, MAX_TILE_NAME, "%s", name);
    t->units_w = uw;
    t->units_h = uh;
    t->px_w = uw * TILE_UNIT;
    t->px_h = uh * TILE_UNIT;
    /* pixels already zeroed by memset */

    selected_tile = tile_count;
    tile_count++;
    unsaved = 1;
    undo_valid = 0;
    return selected_tile;
}

static void delete_tile(int idx)
{
    int i;
    if (idx < 0 || idx >= tile_count) return;
    for (i = idx; i < tile_count - 1; i++)
        tiles[i] = tiles[i + 1];
    tile_count--;
    if (selected_tile >= tile_count)
        selected_tile = tile_count - 1;
    unsaved = 1;
    undo_valid = 0;
}

/* ------------------------------------------------------------------ */
/* Input mode helpers                                                 */
/* ------------------------------------------------------------------ */

static void begin_input(int mode)
{
    input_mode = mode;
    input_buf[0] = '\0';
    input_len = 0;
    SDL_StartTextInput();
}

static void end_input(void)
{
    input_mode = INPUT_NONE;
    input_buf[0] = '\0';
    input_len = 0;
    SDL_StopTextInput();
}

/* ------------------------------------------------------------------ */
/* XPM Import                                                         */
/* ------------------------------------------------------------------ */

static int nearest_ega_color(int r, int g, int b)
{
    int best = 0;
    int best_dist = 999999;
    int i;
    for (i = 0; i < 16; i++) {
        int dr = r - ega_palette[i].r;
        int dg = g - ega_palette[i].g;
        int db = b - ega_palette[i].b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }
    return best;
}

/* Parse a hex digit */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static int import_xpm(const char *path)
{
    FILE *f;
    char line[4096];
    int w = 0, h = 0, ncolors = 0, cpp = 0;
    int found_dims = 0;
    int i, y;
    Tile *t;
    int target_tile;

    /* Color table: map char key(s) to EGA index */
    /* For cpp=1, index by char directly; for cpp=2, linear search */
    typedef struct { char key[4]; unsigned char ega; } ColorEntry;
    ColorEntry ctable[256];
    int ctable_count = 0;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Cannot open XPM file: %s\n", path);
        return 0;
    }

    /* Parse XPM: skip to first quoted string with dimensions */
    while (fgets(line, sizeof(line), f)) {
        char *q;
        if (found_dims && ctable_count < ncolors) {
            /* Parse color entry */
            q = strchr(line, '"');
            if (q) {
                char key[4] = {0};
                char *p;
                int ki;
                q++;
                for (ki = 0; ki < cpp && *q && *q != '"'; ki++)
                    key[ki] = *q++;

                /* Find "c " in remainder */
                p = strstr(q, "\tc ");
                if (!p) p = strstr(q, " c ");
                if (p) {
                    p += 3;
                    while (*p == ' ' || *p == '\t') p++;

                    if (strncasecmp(p, "None", 4) == 0 ||
                        strncasecmp(p, "none", 4) == 0) {
                        ctable[ctable_count].ega = COLOR_TRANSPARENT;
                    } else if (*p == '#') {
                        int r = 0, g = 0, b = 0;
                        int hlen = 0;
                        char *hp = p + 1;
                        while (isxdigit((unsigned char)hp[hlen])) hlen++;

                        if (hlen == 6) {
                            r = hexval(hp[0]) * 16 + hexval(hp[1]);
                            g = hexval(hp[2]) * 16 + hexval(hp[3]);
                            b = hexval(hp[4]) * 16 + hexval(hp[5]);
                        } else if (hlen == 12) {
                            /* #RRRRGGGGBBBB - take high byte */
                            r = hexval(hp[0]) * 16 + hexval(hp[1]);
                            g = hexval(hp[4]) * 16 + hexval(hp[5]);
                            b = hexval(hp[8]) * 16 + hexval(hp[9]);
                        }
                        ctable[ctable_count].ega =
                            (unsigned char)nearest_ega_color(r, g, b);
                    } else {
                        /* Named colors - common ones */
                        int r = 0, g = 0, b = 0;
                        if (strncasecmp(p, "black", 5) == 0)
                            { r = 0; g = 0; b = 0; }
                        else if (strncasecmp(p, "white", 5) == 0)
                            { r = 255; g = 255; b = 255; }
                        else if (strncasecmp(p, "red", 3) == 0)
                            { r = 255; g = 0; b = 0; }
                        else if (strncasecmp(p, "green", 5) == 0)
                            { r = 0; g = 128; b = 0; }
                        else if (strncasecmp(p, "blue", 4) == 0)
                            { r = 0; g = 0; b = 255; }
                        else if (strncasecmp(p, "yellow", 6) == 0)
                            { r = 255; g = 255; b = 0; }
                        else if (strncasecmp(p, "cyan", 4) == 0)
                            { r = 0; g = 255; b = 255; }
                        else if (strncasecmp(p, "magenta", 7) == 0)
                            { r = 255; g = 0; b = 255; }
                        else if (strncasecmp(p, "gray", 4) == 0 ||
                                 strncasecmp(p, "grey", 4) == 0)
                            { r = 128; g = 128; b = 128; }
                        ctable[ctable_count].ega =
                            (unsigned char)nearest_ega_color(r, g, b);
                    }
                    memcpy(ctable[ctable_count].key, key, 4);
                    ctable_count++;
                }
            }
            continue;
        }

        if (!found_dims) {
            q = strchr(line, '"');
            if (q) {
                if (sscanf(q + 1, "%d %d %d %d", &w, &h, &ncolors, &cpp) == 4) {
                    if (w < 1 || h < 1 || w > MAX_TILE_PX || h > MAX_TILE_PX ||
                        (w % TILE_UNIT) != 0 || (h % TILE_UNIT) != 0 ||
                        cpp < 1 || cpp > 3 || ncolors > 256) {
                        fprintf(stderr, "XPM: bad dimensions %dx%d (must be multiples of 16, max %d)\n",
                                w, h, MAX_TILE_PX);
                        fclose(f);
                        return 0;
                    }
                    found_dims = 1;
                }
            }
            continue;
        }
    }

    if (!found_dims || ctable_count == 0) {
        fprintf(stderr, "XPM: failed to parse header/colors\n");
        fclose(f);
        return 0;
    }

    /* Determine target tile: if current tile matches dimensions, import into it */
    if (selected_tile >= 0 &&
        tiles[selected_tile].px_w == w &&
        tiles[selected_tile].px_h == h) {
        target_tile = selected_tile;
        snapshot_undo();
    } else {
        /* Create new tile from filename */
        char tname[MAX_TILE_NAME];
        const char *base = strrchr(path, '/');
        char *dot;
        if (base) base++; else base = path;
        snprintf(tname, MAX_TILE_NAME, "%s", base);
        /* Remove extension */
        dot = strchr(tname, '.');
        if (dot) *dot = '\0';
        /* Sanitize: only alphanum and underscore */
        for (i = 0; tname[i]; i++) {
            if (!isalnum((unsigned char)tname[i]) && tname[i] != '_')
                tname[i] = '_';
        }
        if (tname[0] == '\0') snprintf(tname, MAX_TILE_NAME, "imported");

        target_tile = create_tile(tname, w / TILE_UNIT, h / TILE_UNIT);
        if (target_tile < 0) {
            fprintf(stderr, "XPM: could not create tile\n");
            fclose(f);
            return 0;
        }
    }

    t = &tiles[target_tile];

    /* Rewind and skip to pixel data */
    rewind(f);
    {
        int skip = 1 + ncolors; /* dimensions line + color lines */
        int skipped = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strchr(line, '"')) {
                skipped++;
                if (skipped > skip) break;
            }
        }
    }

    /* We're positioned after reading line that has first pixel row.
     * Re-parse from that line. Actually let's re-read more carefully. */
    rewind(f);
    {
        int lines_with_quotes = 0;
        int pixel_start = 1 + ncolors; /* 0-indexed quote line to start pixels */
        y = 0;
        while (fgets(line, sizeof(line), f) && y < h) {
            char *q = strchr(line, '"');
            if (q) {
                if (lines_with_quotes > pixel_start - 1) {
                    /* This is a pixel row */
                    char *p = q + 1;
                    int x;
                    for (x = 0; x < w && *p && *p != '"'; x++) {
                        char key[4] = {0};
                        int ki;
                        for (ki = 0; ki < cpp && *p && *p != '"'; ki++)
                            key[ki] = *p++;
                        /* Look up in color table */
                        for (i = 0; i < ctable_count; i++) {
                            if (memcmp(ctable[i].key, key, cpp) == 0) {
                                t->pixels[y][x] = ctable[i].ega;
                                break;
                            }
                        }
                    }
                    y++;
                }
                lines_with_quotes++;
            }
        }
    }

    fclose(f);
    unsaved = 1;
    printf("Imported XPM: %s (%dx%d) into tile '%s'\n", path, w, h, t->name);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Drawing                                                            */
/* ------------------------------------------------------------------ */

static void render(void)
{
    SDL_Rect r;
    int i, x, y, zoom, ox, oy;
    Tile *t;
    char status[256];

    /* Background */
    SDL_SetRenderDrawColor(renderer, 40, 40, 40, 255);
    SDL_RenderClear(renderer);

    /* --- Left panel: tile list --- */
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    r.x = 0; r.y = 0; r.w = PANEL_LEFT_W; r.h = win_h - STATUS_BAR_H;
    SDL_RenderFillRect(renderer, &r);

    /* Panel header */
    draw_text_ui(8, 6, "TILES", 2, 200, 200, 200);

    /* Tile entries */
    {
        int visible = (win_h - STATUS_BAR_H - 30) / TILE_LIST_ITEM_H;
        int start = tile_list_scroll;
        int end = start + visible;
        if (end > tile_count) end = tile_count;
        for (i = start; i < end; i++) {
            int iy = 30 + (i - start) * TILE_LIST_ITEM_H;
            if (i == selected_tile) {
                SDL_SetRenderDrawColor(renderer, 60, 60, 100, 255);
                r.x = 2; r.y = iy; r.w = PANEL_LEFT_W - 4;
                r.h = TILE_LIST_ITEM_H - 2;
                SDL_RenderFillRect(renderer, &r);
            }
            draw_text_ui(8, iy + 4, tiles[i].name, 2, 220, 220, 220);
            /* Show tile size */
            {
                char sz[16];
                sprintf(sz, "%dx%d", tiles[i].units_w, tiles[i].units_h);
                draw_text_ui(PANEL_LEFT_W - 40, iy + 4, sz, 1,
                             150, 150, 150);
            }
        }
        /* [+New] button */
        {
            int by = 30 + (end - start) * TILE_LIST_ITEM_H + 4;
            draw_text_ui(8, by, "[N]ew tile", 2, 100, 200, 100);
        }
    }

    /* --- Right panel: palette + tools --- */
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    r.x = win_w - PANEL_RIGHT_W; r.y = 0;
    r.w = PANEL_RIGHT_W; r.h = win_h - STATUS_BAR_H;
    SDL_RenderFillRect(renderer, &r);

    /* Palette swatches: 2 columns, 8 rows */
    draw_text_ui(win_w - PANEL_RIGHT_W + 8, 6, "PALETTE", 2, 200, 200, 200);
    for (i = 0; i < 16; i++) {
        int col = i % 2;
        int row = i / 2;
        int sx = win_w - PANEL_RIGHT_W + 10 + col * 62;
        int sy = 28 + row * 34;

        SDL_SetRenderDrawColor(renderer, ega_palette[i].r,
                               ega_palette[i].g, ega_palette[i].b, 255);
        r.x = sx; r.y = sy; r.w = 56; r.h = 28;
        SDL_RenderFillRect(renderer, &r);

        /* Highlight selected */
        if (i == current_colour) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(renderer, &r);
            r.x++; r.y++; r.w -= 2; r.h -= 2;
            SDL_RenderDrawRect(renderer, &r);
        }

        /* Colour index label */
        {
            char idx_s[4];
            sprintf(idx_s, "%d", i);
            /* Use white text on dark colours, black on light */
            if (i == 0 || i == 1 || i == 4 || i == 5 || i == 8)
                draw_text_ui(sx + 2, sy + 2, idx_s, 1, 200, 200, 200);
            else
                draw_text_ui(sx + 2, sy + 2, idx_s, 1, 0, 0, 0);
        }
    }

    /* 17th swatch: transparent (checkerboard pattern) */
    {
        int sx = win_w - PANEL_RIGHT_W + 10;
        int sy = 28 + 8 * 34;
        int cx, cy;
        for (cy = 0; cy < 28; cy++) {
            for (cx = 0; cx < 56; cx++) {
                if ((cx + cy) % 2 == 0)
                    SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
                else
                    SDL_SetRenderDrawColor(renderer, 85, 85, 85, 255);
                SDL_RenderDrawPoint(renderer, sx + cx, sy + cy);
            }
        }
        draw_text_ui(sx + 2, sy + 2, "T", 1, 255, 255, 255);

        if (current_colour == COLOR_TRANSPARENT) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            r.x = sx; r.y = sy; r.w = 56; r.h = 28;
            SDL_RenderDrawRect(renderer, &r);
            r.x++; r.y++; r.w -= 2; r.h -= 2;
            SDL_RenderDrawRect(renderer, &r);
        }
    }

    /* Tool info */
    {
        int ty = 28 + 9 * 34 + 10;
        draw_text_ui(win_w - PANEL_RIGHT_W + 8, ty, "TOOL:", 2,
                     200, 200, 200);
        draw_text_ui(win_w - PANEL_RIGHT_W + 8, ty + 18,
                     current_tool == TOOL_DRAW ? "[D]raw" :
                     current_tool == TOOL_FILL ? "[F]ill" : "[S]elect",
                     2, 100, 200, 255);

        if (selected_tile >= 0) {
            ty += 44;
            draw_text_ui(win_w - PANEL_RIGHT_W + 8, ty, "TILE:", 2,
                         200, 200, 200);
            draw_text_ui(win_w - PANEL_RIGHT_W + 8, ty + 18,
                         tiles[selected_tile].name, 2, 255, 255, 100);
            {
                char sz[16];
                sprintf(sz, "%dx%d units", tiles[selected_tile].units_w,
                        tiles[selected_tile].units_h);
                draw_text_ui(win_w - PANEL_RIGHT_W + 8, ty + 36,
                             sz, 1, 150, 150, 150);
            }

            /* 1:1 Preview */
            {
                Tile *pt = &tiles[selected_tile];
                int pscale = (pt->px_w <= 32 && pt->px_h <= 32) ? 2 : 1;
                int pw = pt->px_w * pscale;
                int ph = pt->px_h * pscale;
                int px_start = win_w - PANEL_RIGHT_W + 8;
                int py_start = ty + 52;
                int px, py;

                draw_text_ui(px_start, py_start, "PREVIEW:", 1,
                             150, 150, 150);
                py_start += 10;

                /* Gray border */
                SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
                r.x = px_start - 1; r.y = py_start - 1;
                r.w = pw + 2; r.h = ph + 2;
                SDL_RenderDrawRect(renderer, &r);

                /* Pixel-for-pixel rendering */
                for (py = 0; py < pt->px_h; py++) {
                    for (px = 0; px < pt->px_w; px++) {
                        int c = pt->pixels[py][px];
                        if (c == COLOR_TRANSPARENT) {
                            int bx, by;
                            for (by = 0; by < pscale; by++) {
                                for (bx = 0; bx < pscale; bx++) {
                                    if ((px + py + bx + by) % 2 == 0)
                                        SDL_SetRenderDrawColor(renderer,
                                            170, 170, 170, 255);
                                    else
                                        SDL_SetRenderDrawColor(renderer,
                                            85, 85, 85, 255);
                                    SDL_RenderDrawPoint(renderer,
                                        px_start + px * pscale + bx,
                                        py_start + py * pscale + by);
                                }
                            }
                        } else {
                            SDL_SetRenderDrawColor(renderer,
                                ega_palette[c].r, ega_palette[c].g,
                                ega_palette[c].b, 255);
                            r.x = px_start + px * pscale;
                            r.y = py_start + py * pscale;
                            r.w = pscale;
                            r.h = pscale;
                            SDL_RenderFillRect(renderer, &r);
                        }
                    }
                }
            }
        }
    }

    /* --- Canvas --- */
    if (selected_tile >= 0 && selected_tile < tile_count) {
        SDL_Rect cr;
        get_canvas_rect(&cr);
        t = &tiles[selected_tile];
        zoom = get_zoom();
        ox = cr.x + (cr.w - t->px_w * zoom) / 2;
        oy = cr.y + (cr.h - t->px_h * zoom) / 2;

        /* Draw pixels */
        for (y = 0; y < t->px_h; y++) {
            for (x = 0; x < t->px_w; x++) {
                int c = t->pixels[y][x];
                if (c == COLOR_TRANSPARENT) {
                    /* Checkerboard for transparent */
                    int bx, by;
                    for (by = 0; by < zoom; by++) {
                        for (bx = 0; bx < zoom; bx++) {
                            if (((x + y) + (bx + by) / (zoom > 4 ? zoom/4 : 1)) % 2 == 0)
                                SDL_SetRenderDrawColor(renderer, 170, 170, 170, 255);
                            else
                                SDL_SetRenderDrawColor(renderer, 85, 85, 85, 255);
                            SDL_RenderDrawPoint(renderer,
                                ox + x * zoom + bx, oy + y * zoom + by);
                        }
                    }
                } else {
                    SDL_SetRenderDrawColor(renderer, ega_palette[c].r,
                                           ega_palette[c].g, ega_palette[c].b,
                                           255);
                    r.x = ox + x * zoom;
                    r.y = oy + y * zoom;
                    r.w = zoom;
                    r.h = zoom;
                    SDL_RenderFillRect(renderer, &r);
                }
            }
        }

        /* Grid lines (pixel boundaries) */
        SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
        for (x = 0; x <= t->px_w; x++)
            SDL_RenderDrawLine(renderer, ox + x * zoom, oy,
                               ox + x * zoom, oy + t->px_h * zoom);
        for (y = 0; y <= t->px_h; y++)
            SDL_RenderDrawLine(renderer, ox, oy + y * zoom,
                               ox + t->px_w * zoom, oy + y * zoom);

        /* Tile-unit boundaries (thicker, brighter) */
        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
        for (x = 0; x <= t->units_w; x++) {
            int lx = ox + x * TILE_UNIT * zoom;
            SDL_RenderDrawLine(renderer, lx, oy, lx, oy + t->px_h * zoom);
            SDL_RenderDrawLine(renderer, lx + 1, oy,
                               lx + 1, oy + t->px_h * zoom);
        }
        for (y = 0; y <= t->units_h; y++) {
            int ly = oy + y * TILE_UNIT * zoom;
            SDL_RenderDrawLine(renderer, ox, ly, ox + t->px_w * zoom, ly);
            SDL_RenderDrawLine(renderer, ox, ly + 1,
                               ox + t->px_w * zoom, ly + 1);
        }

        /* Selection marquee (dashed white/black) */
        if (sel_active && !float_active) {
            int nx1 = sel_x1, ny1 = sel_y1, nx2 = sel_x2, ny2 = sel_y2;
            int sx1, sy1, sx2, sy2, si;
            sel_normalise(&nx1, &ny1, &nx2, &ny2);
            sx1 = ox + nx1 * zoom;
            sy1 = oy + ny1 * zoom;
            sx2 = ox + (nx2 + 1) * zoom;
            sy2 = oy + (ny2 + 1) * zoom;
            /* Draw marching ants style border */
            for (si = sx1; si < sx2; si++) {
                SDL_SetRenderDrawColor(renderer,
                    ((si + SDL_GetTicks()/100) % 4 < 2) ? 255 : 0,
                    ((si + SDL_GetTicks()/100) % 4 < 2) ? 255 : 0,
                    ((si + SDL_GetTicks()/100) % 4 < 2) ? 255 : 0, 255);
                SDL_RenderDrawPoint(renderer, si, sy1);
                SDL_RenderDrawPoint(renderer, si, sy2 - 1);
            }
            for (si = sy1; si < sy2; si++) {
                SDL_SetRenderDrawColor(renderer,
                    ((si + SDL_GetTicks()/100) % 4 < 2) ? 255 : 0,
                    ((si + SDL_GetTicks()/100) % 4 < 2) ? 255 : 0,
                    ((si + SDL_GetTicks()/100) % 4 < 2) ? 255 : 0, 255);
                SDL_RenderDrawPoint(renderer, sx1, si);
                SDL_RenderDrawPoint(renderer, sx2 - 1, si);
            }
        }

        /* Floating selection: draw at mouse position */
        if (float_active) {
            int fmx, fmy, fpx, fpy;
            /* Get mouse position on canvas */
            SDL_GetMouseState(&fmx, &fmy);
            if (mouse_to_pixel(fmx, fmy, &fpx, &fpy)) {
                int fx, fy;
                for (fy = 0; fy < float_h; fy++) {
                    for (fx = 0; fx < float_w; fx++) {
                        int dx = fpx + fx;
                        int dy = fpy + fy;
                        unsigned char c = float_buf[fy][fx];
                        if (c == COLOR_TRANSPARENT) continue;
                        if (dx >= 0 && dx < t->px_w &&
                            dy >= 0 && dy < t->px_h) {
                            SDL_SetRenderDrawColor(renderer,
                                ega_palette[c].r, ega_palette[c].g,
                                ega_palette[c].b, 255);
                            r.x = ox + dx * zoom;
                            r.y = oy + dy * zoom;
                            r.w = zoom;
                            r.h = zoom;
                            SDL_RenderFillRect(renderer, &r);
                        }
                    }
                }
                /* Border around floating selection */
                SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
                r.x = ox + fpx * zoom;
                r.y = oy + fpy * zoom;
                r.w = float_w * zoom;
                r.h = float_h * zoom;
                SDL_RenderDrawRect(renderer, &r);
            }
        }
    } else {
        /* No tile selected — show instructions */
        int cx = (PANEL_LEFT_W + win_w - PANEL_RIGHT_W) / 2;
        int cy = (win_h - STATUS_BAR_H) / 2;
        draw_text_ui(cx - 100, cy - 10, "Press N to create a tile", 2,
                     150, 150, 150);
    }

    /* --- Status bar --- */
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    r.x = 0; r.y = win_h - STATUS_BAR_H; r.w = win_w; r.h = STATUS_BAR_H;
    SDL_RenderFillRect(renderer, &r);

    /* Separator line */
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderDrawLine(renderer, 0, win_h - STATUS_BAR_H,
                       win_w, win_h - STATUS_BAR_H);

    if (input_mode != INPUT_NONE) {
        const char *prompt = "";
        switch (input_mode) {
        case INPUT_TILE_NAME:   prompt = "Tile name: "; break;
        case INPUT_TILE_WIDTH:  prompt = "Width in tiles: "; break;
        case INPUT_TILE_HEIGHT: prompt = "Height in tiles: "; break;
        case INPUT_RENAME:      prompt = "New name: "; break;
        case INPUT_CONFIRM_CLEAR:  prompt = "Clear canvas? (Y/N) "; break;
        case INPUT_CONFIRM_DELETE: prompt = "Delete tile? (Y/N) "; break;
        case INPUT_CONFIRM_QUIT:   prompt = "Unsaved changes. Quit? (Y/N) ";
            break;
        case INPUT_XPM_PATH:       prompt = "XPM file path: "; break;
        }
        sprintf(status, "%s%s_", prompt, input_buf);
        draw_text_ui(8, win_h - STATUS_BAR_H + 8, status, 2,
                     255, 255, 100);
    } else {
        sprintf(status, "col=%s(%d)  tool=%s  tiles=%d%s",
                current_colour == COLOR_TRANSPARENT ? "Transp" :
                    ega_names[current_colour],
                current_colour,
                current_tool == TOOL_DRAW ? "Draw" :
                current_tool == TOOL_FILL ? "Fill" : "Select",
                tile_count,
                unsaved ? "  [unsaved]" : "");
        draw_text_ui(8, win_h - STATUS_BAR_H + 8, status, 2,
                     180, 180, 180);

        /* Keyboard shortcuts hint */
        draw_text_ui(8, win_h - STATUS_BAR_H + 26,
                     "N:new D:draw F:fill S:sel U:dup ^D:copy Ent:move",
                     3, 100, 100, 100);
        draw_text_ui(8, win_h - STATUS_BAR_H + 48,
                     "Z:undo M:xpm Sh+T:transp r:rename ^S:save Q:quit",
                     3, 100, 100, 100);
    }

    SDL_RenderPresent(renderer);
}

/* ------------------------------------------------------------------ */
/* Event handling                                                     */
/* ------------------------------------------------------------------ */

static int handle_events(void)
{
    SDL_Event ev;
    int running = 1;

    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            if (unsaved) {
                begin_input(INPUT_CONFIRM_QUIT);
            } else {
                running = 0;
            }
            break;

        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = ev.window.data1;
                win_h = ev.window.data2;
            }
            break;

        case SDL_TEXTINPUT:
            if (input_mode == INPUT_TILE_NAME || input_mode == INPUT_RENAME ||
                input_mode == INPUT_TILE_WIDTH || input_mode == INPUT_TILE_HEIGHT ||
                input_mode == INPUT_XPM_PATH) {
                int slen = (int)strlen(ev.text.text);
                if (input_len + slen < MAX_TILE_NAME - 1) {
                    strcat(input_buf, ev.text.text);
                    input_len += slen;
                }
            }
            break;

        case SDL_KEYDOWN:
            /* Handle input modes first */
            if (input_mode != INPUT_NONE) {
                if (input_mode == INPUT_CONFIRM_CLEAR ||
                    input_mode == INPUT_CONFIRM_DELETE ||
                    input_mode == INPUT_CONFIRM_QUIT) {
                    if (ev.key.keysym.sym == SDLK_y) {
                        if (input_mode == INPUT_CONFIRM_CLEAR) {
                            if (selected_tile >= 0) {
                                snapshot_undo();
                                memset(tiles[selected_tile].pixels, 0,
                                       sizeof(tiles[selected_tile].pixels));
                                unsaved = 1;
                            }
                        } else if (input_mode == INPUT_CONFIRM_DELETE) {
                            delete_tile(selected_tile);
                        } else if (input_mode == INPUT_CONFIRM_QUIT) {
                            running = 0;
                        }
                        end_input();
                    } else if (ev.key.keysym.sym == SDLK_n ||
                               ev.key.keysym.sym == SDLK_ESCAPE) {
                        end_input();
                    }
                    break;
                }

                if (ev.key.keysym.sym == SDLK_RETURN) {
                    if (input_mode == INPUT_TILE_NAME) {
                        if (input_len > 0) {
                            strncpy(new_tile_name, input_buf,
                                    MAX_TILE_NAME - 1);
                            new_tile_name[MAX_TILE_NAME - 1] = '\0';
                            begin_input(INPUT_TILE_WIDTH);
                        } else {
                            end_input();
                        }
                    } else if (input_mode == INPUT_TILE_WIDTH) {
                        new_tile_width = atoi(input_buf);
                        if (new_tile_width >= 1 &&
                            new_tile_width <= MAX_TILE_UNITS) {
                            begin_input(INPUT_TILE_HEIGHT);
                        } else {
                            end_input();
                        }
                    } else if (input_mode == INPUT_TILE_HEIGHT) {
                        int h = atoi(input_buf);
                        if (h >= 1 && h <= MAX_TILE_UNITS) {
                            create_tile(new_tile_name, new_tile_width, h);
                        }
                        end_input();
                    } else if (input_mode == INPUT_RENAME) {
                        if (input_len > 0 && selected_tile >= 0) {
                            snprintf(tiles[selected_tile].name,
                                     MAX_TILE_NAME, "%s", input_buf);
                            unsaved = 1;
                        }
                        end_input();
                    } else if (input_mode == INPUT_XPM_PATH) {
                        if (input_len > 0) {
                            import_xpm(input_buf);
                        }
                        end_input();
                    }
                } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    end_input();
                } else if (ev.key.keysym.sym == SDLK_BACKSPACE) {
                    if (input_len > 0) {
                        input_buf[--input_len] = '\0';
                    }
                }
                break;
            }

            /* Normal key handling (no input mode active) */
            {
                SDL_Keymod mod = SDL_GetModState();

                /* Escape: cancel selection/float first, else quit */
                if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    if (float_active || sel_active) {
                        if (float_active && float_is_move)
                            do_undo(); /* restore erased pixels */
                        sel_clear();
                    } else if (unsaved) {
                        begin_input(INPUT_CONFIRM_QUIT);
                    } else {
                        running = 0;
                    }
                }
                else if (ev.key.keysym.sym == SDLK_q) {
                    if (unsaved)
                        begin_input(INPUT_CONFIRM_QUIT);
                    else
                        running = 0;
                }
                /* Enter: stamp floating selection, or lift-move marquee */
                else if (ev.key.keysym.sym == SDLK_RETURN) {
                    if (float_active && selected_tile >= 0) {
                        int fmx, fmy, fpx, fpy;
                        SDL_GetMouseState(&fmx, &fmy);
                        if (mouse_to_pixel(fmx, fmy, &fpx, &fpy))
                            sel_stamp(fpx, fpy);
                    } else if (sel_active && !float_active) {
                        sel_lift(1); /* move */
                    }
                }
                /* Ctrl+D: duplicate (copy) selection as float */
                else if (ev.key.keysym.sym == SDLK_d &&
                         (mod & KMOD_CTRL)) {
                    if (sel_active && !float_active) {
                        sel_lift(0); /* copy */
                    }
                }
                else if (ev.key.keysym.sym == SDLK_s &&
                         (mod & KMOD_CTRL)) {
                    save_project();
                }
                else if (ev.key.keysym.sym == SDLK_n) {
                    begin_input(INPUT_TILE_NAME);
                }
                else if (ev.key.keysym.sym == SDLK_d &&
                         !(mod & KMOD_CTRL)) {
                    current_tool = TOOL_DRAW;
                    sel_clear();
                }
                else if (ev.key.keysym.sym == SDLK_f) {
                    current_tool = TOOL_FILL;
                    sel_clear();
                }
                else if (ev.key.keysym.sym == SDLK_s &&
                         !(mod & KMOD_CTRL)) {
                    current_tool = TOOL_SELECT;
                    sel_clear();
                }
                else if (ev.key.keysym.sym == SDLK_c) {
                    if (selected_tile >= 0)
                        begin_input(INPUT_CONFIRM_CLEAR);
                }
                else if (ev.key.keysym.sym == SDLK_z) {
                    do_undo();
                }
                else if (ev.key.keysym.sym == SDLK_r) {
                    if (selected_tile >= 0)
                        begin_input(INPUT_RENAME);
                }
                else if (ev.key.keysym.sym == SDLK_u) {
                    /* Duplicate current tile */
                    if (selected_tile >= 0 && tile_count < MAX_TILES) {
                        int src_idx = selected_tile;
                        char dup_name[MAX_TILE_NAME];
                        snprintf(dup_name, MAX_TILE_NAME, "%s_copy",
                                 tiles[src_idx].name);
                        if (create_tile(dup_name,
                                        tiles[src_idx].units_w,
                                        tiles[src_idx].units_h) >= 0) {
                            memcpy(tiles[selected_tile].pixels,
                                   tiles[src_idx].pixels,
                                   sizeof(tiles[src_idx].pixels));
                        }
                    }
                }
                else if (ev.key.keysym.sym == SDLK_DELETE) {
                    if (selected_tile >= 0)
                        begin_input(INPUT_CONFIRM_DELETE);
                }
                else if (ev.key.keysym.sym == SDLK_LEFT) {
                    if (selected_tile > 0) {
                        selected_tile--;
                        undo_valid = 0;
                    }
                }
                else if (ev.key.keysym.sym == SDLK_RIGHT) {
                    if (selected_tile < tile_count - 1) {
                        selected_tile++;
                        undo_valid = 0;
                    }
                }
                /* Number keys 0-9 for quick palette selection */
                else if (ev.key.keysym.sym >= SDLK_0 &&
                         ev.key.keysym.sym <= SDLK_9) {
                    int n = ev.key.keysym.sym - SDLK_0;
                    if (mod & KMOD_SHIFT) n += 10;
                    if (n < 16) current_colour = n;
                }
                /* Shift+T for transparent */
                else if (ev.key.keysym.sym == SDLK_t &&
                         (mod & KMOD_SHIFT)) {
                    current_colour = COLOR_TRANSPARENT;
                }
                /* M for XPM import */
                else if (ev.key.keysym.sym == SDLK_m) {
                    begin_input(INPUT_XPM_PATH);
                }
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (input_mode != INPUT_NONE) break;

            /* Check if click is on canvas */
            {
                int px, py;
                if (ev.button.button == SDL_BUTTON_LEFT) {
                    if (mouse_to_pixel(ev.button.x, ev.button.y,
                                       &px, &py)) {
                        Tile *ct = &tiles[selected_tile];
                        if (current_tool == TOOL_SELECT) {
                            if (float_active) {
                                /* Stamp on click */
                                sel_stamp(px, py);
                            } else {
                                /* Begin drag-select */
                                sel_x1 = px; sel_y1 = py;
                                sel_x2 = px; sel_y2 = py;
                                sel_dragging = 1;
                                sel_active = 1;
                            }
                        } else if (current_tool == TOOL_DRAW) {
                            snapshot_undo();
                            ct->pixels[py][px] =
                                (unsigned char)current_colour;
                            unsaved = 1;
                        } else if (current_tool == TOOL_FILL) {
                            int old = ct->pixels[py][px];
                            if (old != current_colour) {
                                snapshot_undo();
                                flood_fill(ct, px, py, old,
                                           current_colour);
                                unsaved = 1;
                            }
                        }
                    }
                    /* Check palette click */
                    else {
                        int pi;
                        for (pi = 0; pi < 16; pi++) {
                            int col = pi % 2;
                            int row = pi / 2;
                            int sx = win_w - PANEL_RIGHT_W + 10 +
                                     col * 62;
                            int sy = 28 + row * 34;
                            if (ev.button.x >= sx &&
                                ev.button.x < sx + 56 &&
                                ev.button.y >= sy &&
                                ev.button.y < sy + 28) {
                                current_colour = pi;
                                break;
                            }
                        }
                        /* Check transparent swatch click */
                        {
                            int tsx = win_w - PANEL_RIGHT_W + 10;
                            int tsy = 28 + 8 * 34;
                            if (ev.button.x >= tsx &&
                                ev.button.x < tsx + 56 &&
                                ev.button.y >= tsy &&
                                ev.button.y < tsy + 28) {
                                current_colour = COLOR_TRANSPARENT;
                            }
                        }
                        /* Check tile list click */
                        if (ev.button.x < PANEL_LEFT_W &&
                            ev.button.y >= 30 &&
                            ev.button.y < win_h - STATUS_BAR_H) {
                            int idx = tile_list_scroll +
                                      (ev.button.y - 30) /
                                      TILE_LIST_ITEM_H;
                            if (idx >= 0 && idx < tile_count) {
                                selected_tile = idx;
                                undo_valid = 0;
                            }
                        }
                    }
                }
                else if (ev.button.button == SDL_BUTTON_RIGHT) {
                    /* Eyedropper */
                    if (mouse_to_pixel(ev.button.x, ev.button.y,
                                       &px, &py)) {
                        current_colour =
                            tiles[selected_tile].pixels[py][px];
                    }
                }
            }
            break;

        case SDL_MOUSEBUTTONUP:
            if (ev.button.button == SDL_BUTTON_LEFT && sel_dragging) {
                sel_dragging = 0;
                /* If zero-size selection, cancel it */
                {
                    int nx1 = sel_x1, ny1 = sel_y1;
                    int nx2 = sel_x2, ny2 = sel_y2;
                    sel_normalise(&nx1, &ny1, &nx2, &ny2);
                    if (nx1 == nx2 && ny1 == ny2)
                        sel_active = 0;
                }
            }
            break;

        case SDL_MOUSEMOTION:
            /* Selection drag */
            if (input_mode == INPUT_NONE && sel_dragging &&
                current_tool == TOOL_SELECT && selected_tile >= 0) {
                int px, py;
                if (mouse_to_pixel(ev.motion.x, ev.motion.y, &px, &py)) {
                    sel_x2 = px;
                    sel_y2 = py;
                }
            }
            /* Drag-drawing */
            else if (input_mode == INPUT_NONE &&
                (ev.motion.state & SDL_BUTTON_LMASK) &&
                current_tool == TOOL_DRAW && selected_tile >= 0) {
                int px, py;
                if (mouse_to_pixel(ev.motion.x, ev.motion.y, &px, &py)) {
                    if (tiles[selected_tile].pixels[py][px] !=
                        (unsigned char)current_colour) {
                        tiles[selected_tile].pixels[py][px] =
                            (unsigned char)current_colour;
                        unsaved = 1;
                    }
                }
            }
            break;

        case SDL_MOUSEWHEEL:
            /* Scroll tile list */
            if (ev.wheel.y > 0 && tile_list_scroll > 0)
                tile_list_scroll--;
            else if (ev.wheel.y < 0 && tile_list_scroll < tile_count - 1)
                tile_list_scroll++;
            break;
        }
    }

    return running;
}

/* ------------------------------------------------------------------ */
/* Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int running = 1;

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    window = SDL_CreateWindow("EGA Tile Editor",
                              SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              INITIAL_W, INITIAL_H,
                              SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1,
                                  SDL_RENDERER_ACCELERATED |
                                  SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    load_project();

    while (running) {
        running = handle_events();
        render();
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
