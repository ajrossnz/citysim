#include "citysim.h"
#include "tile_editor/tiles.h"

/* Tile art: pointer to a 16x16 pixel array (one row = 16 unsigned chars) */
typedef const unsigned char (*TileArt)[16];

/*
 * Mapping from game tile types to editor tile art.
 * NULL entries fall back to the original solid-colour rendering.
 * Update this table as new tiles are created in the tile editor.
 * The index matches the TILE_xxx constants in citysim.h.
 */
#define NUM_TILE_TYPES 15

#if defined(TILE_COUNT) && TILE_COUNT > 0
static TileArt tile_art_map[NUM_TILE_TYPES] = {
    NULL,           /*  0: TILE_GRASS        */
    NULL,           /*  1: TILE_RESIDENTIAL  */
    NULL,           /*  2: TILE_COMMERCIAL   */
    NULL,           /*  3: TILE_INDUSTRIAL   */
    tile_road1,     /*  4: TILE_ROAD         */
    NULL,           /*  5: TILE_POWER_LINE   */
    NULL,           /*  6: TILE_WATER        */
    tile_park1,     /*  7: TILE_PARK         */
    NULL,           /*  8: TILE_POLICE       */
    NULL,           /*  9: TILE_FIRE         */
    NULL,           /* 10: TILE_HOSPITAL     */
    NULL,           /* 11: TILE_SCHOOL       */
    NULL,           /* 12: TILE_POWER_PLANT  */
    NULL,           /* 13: TILE_WATER_PUMP   */
    tile_rail1,     /* 14: TILE_RAIL         */
};
#endif

/* Graphics Functions */

void init_ega(void) {
    union REGS regs;
    regs.x.eax = EGA_MODE;
    int386(0x10, &regs, &regs);
}

void set_text_mode(void) {
    union REGS regs;
    regs.x.eax = TEXT_MODE;
    int386(0x10, &regs, &regs);
}

void set_pixel(int x, int y, unsigned char color) {
    volatile unsigned char *video;
    unsigned int offset;
    unsigned char bit_mask;
    unsigned char old_val;

    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT)
        return;

    video = (volatile unsigned char *)0xA0000;
    offset = (y * 80) + (x / 8);
    bit_mask = 0x80 >> (x % 8);
    
    /* Use write mode 0 - write to planes individually */
    outp(0x3CE, 0x05);  /* Graphics Mode Register */
    outp(0x3CF, 0x00);  /* Write mode 0, read mode 0 */
    
    /* Set bit mask for this pixel */
    outp(0x3CE, 0x08);  /* Bit Mask Register */
    outp(0x3CF, bit_mask);
    
    /* Enable Set/Reset for all planes */
    outp(0x3CE, 0x01);  /* Enable Set/Reset Register */
    outp(0x3CF, 0x0F);  /* Enable all planes */
    
    /* Set the color in Set/Reset register */
    outp(0x3CE, 0x00);  /* Set/Reset Register */
    outp(0x3CF, color);
    
    /* Dummy read to load latches, then write to apply */
    old_val = video[offset];
    video[offset] = 0xFF;  /* Value doesn't matter with Set/Reset enabled */
    
    /* Reset to defaults */
    outp(0x3CE, 0x01);  /* Disable Set/Reset */
    outp(0x3CF, 0x00);
    outp(0x3CE, 0x08);  /* Reset bit mask */
    outp(0x3CF, 0xFF);
}

void draw_filled_rect(int x, int y, int width, int height, unsigned char color) {
    volatile unsigned char *video;
    int row, col;
    int start_byte, end_byte;
    unsigned char left_mask, right_mask;
    int offset;

    video = (volatile unsigned char *)0xA0000;
    
    /* Reset EGA registers to known state first */
    outp(0x3CE, 0x01);  /* Disable Set/Reset */
    outp(0x3CF, 0x00);
    outp(0x3CE, 0x08);  /* Reset bit mask */
    outp(0x3CF, 0xFF);
    outp(0x3CE, 0x05);  /* Graphics Mode Register */
    outp(0x3CF, 0x00);  /* Write mode 0 */
    
    /* Set up EGA for block write */
    outp(0x3CE, 0x01);  /* Enable Set/Reset */
    outp(0x3CF, 0x0F);  /* All planes */
    
    outp(0x3CE, 0x00);  /* Set/Reset value */
    outp(0x3CF, color);
    
    for (row = 0; row < height; row++) {
        if (y + row < 0 || y + row >= SCREEN_HEIGHT)
            continue;
            
        start_byte = x / 8;
        end_byte = (x + width - 1) / 8;
        
        /* Calculate masks for partial bytes */
        left_mask = 0xFF >> (x % 8);
        right_mask = 0xFF << (7 - ((x + width - 1) % 8));
        
        offset = ((y + row) * 80);
        
        if (start_byte == end_byte) {
            /* Rectangle fits in one byte */
            outp(0x3CE, 0x08);
            outp(0x3CF, left_mask & right_mask);
            video[offset + start_byte] = video[offset + start_byte];
            video[offset + start_byte] = 0xFF;
        } else {
            /* Left edge */
            outp(0x3CE, 0x08);
            outp(0x3CF, left_mask);
            video[offset + start_byte] = video[offset + start_byte];
            video[offset + start_byte] = 0xFF;
            
            /* Middle bytes */
            outp(0x3CE, 0x08);
            outp(0x3CF, 0xFF);
            for (col = start_byte + 1; col < end_byte; col++) {
                video[offset + col] = video[offset + col];
                video[offset + col] = 0xFF;
            }
            
            /* Right edge */
            outp(0x3CE, 0x08);
            outp(0x3CF, right_mask);
            video[offset + end_byte] = video[offset + end_byte];
            video[offset + end_byte] = 0xFF;
        }
    }
    
    /* Reset all registers to safe defaults */
    outp(0x3CE, 0x00);  /* Set/Reset Register */
    outp(0x3CF, 0x00);
    outp(0x3CE, 0x01);  /* Disable Set/Reset */
    outp(0x3CF, 0x00);
    outp(0x3CE, 0x08);  /* Reset bit mask */
    outp(0x3CF, 0xFF);
    outp(0x3CE, 0x05);  /* Write mode 0 */
    outp(0x3CF, 0x00);
}

void draw_rect(int x, int y, int width, int height, unsigned char color) {
    int i;
    for (i = 0; i < width; i++) {
        set_pixel(x + i, y, color);
        set_pixel(x + i, y + height - 1, color);
    }
    for (i = 0; i < height; i++) {
        set_pixel(x, y + i, color);
        set_pixel(x + width - 1, y + i, color);
    }
}

void clear_screen(unsigned char color) {
    volatile unsigned char *video;
    unsigned int i;
    unsigned char plane;

    video = (volatile unsigned char *)0xA0000;
    
    /* Clear each plane */
    for (plane = 0; plane < 4; plane++) {
        /* Select plane */
        outp(0x3C4, 0x02);  /* Map Mask register */
        outp(0x3C5, 1 << plane);
        
        /* Fill plane with appropriate value */
        if (color & (1 << plane)) {
            for (i = 0; i < 28000; i++) {
                video[i] = 0xFF;
            }
        } else {
            for (i = 0; i < 28000; i++) {
                video[i] = 0x00;
            }
        }
    }
    
    /* Reset to all planes */
    outp(0x3C4, 0x02);
    outp(0x3C5, 0x0F);
}

/* Simple 5x7 font data for essential characters */
static const unsigned char font_data[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* Space */
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* ! */
    {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, /* & */
    {0x04,0x04,0x04,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, /* . */
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, /* 2 */
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x00}, /* : */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x08}, /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* [ */
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00}, /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* a */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* b */
    {0x00,0x00,0x0E,0x11,0x10,0x11,0x0E}, /* c */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x08,0x08,0x1E,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1E,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* z */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* | */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* } */
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, /* ~ */
};

void draw_char(int x, int y, char c, unsigned char color) {
    int i, j;
    unsigned char row;
    int char_index;

    if (c < 32 || c > 126)
        return;

    char_index = c - 32;

    /* Draw at 2x scale: each font pixel becomes a 2x2 block */
    for (i = 0; i < 7; i++) {
        row = font_data[char_index][i];
        for (j = 0; j < 5; j++) {
            if (row & (0x10 >> j)) {
                draw_filled_rect(x + j * 2, y + i * 2, 2, 2, color);
            }
        }
    }
}

void draw_text(int x, int y, const char* text, unsigned char color) {
    int i;
    int cursor_x = x;

    for (i = 0; text[i] != '\0'; i++) {
        draw_char(cursor_x, y, text[i], color);
        cursor_x += 12;  /* 5*2 pixels wide + 2px gap */
    }
}

void draw_number(int x, int y, long num, unsigned char color) {
    char buffer[16];
    sprintf(buffer, "%ld", num);
    draw_text(x, y, buffer, color);
}

/*
 * Render a 16x16 pixel-art tile directly to EGA memory.
 * Exploits the fact that tiles are always placed at 16px-aligned X positions,
 * so each row spans exactly 2 bytes (16 pixels / 8 pixels-per-byte).
 * Writes one plane at a time to minimise EGA register switches.
 */
static void draw_tile_pixels(int sx, int sy, TileArt pixels) {
    volatile unsigned char *video;
    int y, x, plane;
    unsigned char plane_bit;

    video = (volatile unsigned char *)0xA0000;

    /* Ensure clean EGA state for direct plane writes */
    outp(0x3CE, 0x05);  /* Write mode 0 */
    outp(0x3CF, 0x00);
    outp(0x3CE, 0x01);  /* Disable Set/Reset */
    outp(0x3CF, 0x00);
    outp(0x3CE, 0x08);  /* Full bit mask */
    outp(0x3CF, 0xFF);

    for (plane = 0; plane < 4; plane++) {
        plane_bit = (unsigned char)(1 << plane);

        /* Select this plane via Map Mask */
        outp(0x3C4, 0x02);
        outp(0x3C5, plane_bit);

        for (y = 0; y < 16; y++) {
            unsigned char byte0 = 0, byte1 = 0;
            int offset;

            if (sy + y < 0 || sy + y >= SCREEN_HEIGHT) continue;
            offset = (sy + y) * 80 + sx / 8;

            /* Pack 8 pixels into each byte for this plane */
            for (x = 0; x < 8; x++) {
                if (pixels[y][x] & plane_bit)
                    byte0 |= (unsigned char)(0x80 >> x);
                if (pixels[y][x + 8] & plane_bit)
                    byte1 |= (unsigned char)(0x80 >> x);
            }

            video[offset]     = byte0;
            video[offset + 1] = byte1;
        }
    }

    /* Restore: all planes enabled */
    outp(0x3C4, 0x02);
    outp(0x3C5, 0x0F);
}

/* Draw a tile based on its type */
void draw_tile(int screen_x, int screen_y, unsigned char tile_type) {
    unsigned char color;

    /* Use pixel art if available for this tile type */
#if defined(TILE_COUNT) && TILE_COUNT > 0
    if (tile_type < NUM_TILE_TYPES && tile_art_map[tile_type] != NULL) {
        draw_tile_pixels(screen_x, screen_y, tile_art_map[tile_type]);
        return;
    }
#endif

    /* Fallback: solid colour fill */
    switch (tile_type) {
        case TILE_GRASS:
            color = COLOR_GREEN;
            break;
        case TILE_RESIDENTIAL:
            color = COLOR_YELLOW;
            break;
        case TILE_COMMERCIAL:
            color = COLOR_LIGHT_BLUE;
            break;
        case TILE_INDUSTRIAL:
            color = COLOR_BROWN;
            break;
        case TILE_ROAD:
            color = COLOR_DARK_GRAY;
            break;
        case TILE_POWER_LINE:
            color = COLOR_LIGHT_GRAY;
            break;
        case TILE_WATER:
            color = COLOR_BLUE;
            break;
        case TILE_PARK:
            color = COLOR_LIGHT_GREEN;
            break;
        case TILE_POLICE:
            color = COLOR_CYAN;
            break;
        case TILE_FIRE:
            color = COLOR_RED;
            break;
        case TILE_HOSPITAL:
            color = COLOR_WHITE;
            break;
        case TILE_SCHOOL:
            color = COLOR_MAGENTA;
            break;
        case TILE_POWER_PLANT:
            color = COLOR_LIGHT_RED;
            break;
        case TILE_WATER_PUMP:
            color = COLOR_LIGHT_CYAN;
            break;
        case TILE_RAIL:
            color = COLOR_BROWN;
            break;
        default:
            color = COLOR_BLACK;
    }
    
    draw_filled_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE, color);
}

void draw_map(GameState* game) {
    int screen_x, screen_y;
    int map_x, map_y;
    int tile_x, tile_y;

    for (tile_y = 0; tile_y < VIEW_TILES_Y && tile_y + game->scroll_y < MAP_HEIGHT; tile_y++) {
        for (tile_x = 0; tile_x < VIEW_TILES_X && tile_x + game->scroll_x < MAP_WIDTH; tile_x++) {
            map_x = tile_x + game->scroll_x;
            map_y = tile_y + game->scroll_y;
            screen_x = tile_x * TILE_SIZE;
            screen_y = tile_y * TILE_SIZE;

            draw_tile(screen_x, screen_y, game->map[map_y][map_x].type);
        }
    }
}

void draw_single_tile(GameState* game, unsigned short map_x, unsigned short map_y) {
    int screen_x, screen_y;

    /* Only draw if visible in viewport */
    if (map_x < game->scroll_x || map_x >= game->scroll_x + VIEW_TILES_X ||
        map_y < game->scroll_y || map_y >= game->scroll_y + VIEW_TILES_Y)
        return;

    screen_x = (map_x - game->scroll_x) * TILE_SIZE;
    screen_y = (map_y - game->scroll_y) * TILE_SIZE;
    draw_tile(screen_x, screen_y, game->map[map_y][map_x].type);
}

void draw_cursor(GameState* game) {
    int screen_x, screen_y;

    if (game->cursor_x < game->scroll_x || game->cursor_x >= game->scroll_x + VIEW_TILES_X ||
        game->cursor_y < game->scroll_y || game->cursor_y >= game->scroll_y + VIEW_TILES_Y)
        return;

    screen_x = (game->cursor_x - game->scroll_x) * TILE_SIZE;
    screen_y = (game->cursor_y - game->scroll_y) * TILE_SIZE;

    /* Draw white corners */
    draw_filled_rect(screen_x, screen_y, 4, 4, COLOR_WHITE);
    draw_filled_rect(screen_x + TILE_SIZE - 4, screen_y, 4, 4, COLOR_WHITE);
    draw_filled_rect(screen_x, screen_y + TILE_SIZE - 4, 4, 4, COLOR_WHITE);
    draw_filled_rect(screen_x + TILE_SIZE - 4, screen_y + TILE_SIZE - 4, 4, 4, COLOR_WHITE);
}

void draw_help_screen(void) {
    int y;

    /* 2x font: 12px per char, 14px tall, ~18px line spacing */
    clear_screen(COLOR_BLUE);

    /* Title */
    draw_text(220, 6, "CITYSIM HELP", COLOR_YELLOW);

    /* Building legend - left column */
    y = 30;
    draw_text(20, y, "BUILDINGS:", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_YELLOW);
    draw_text(38, y, "R - Residential $100", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_LIGHT_BLUE);
    draw_text(38, y, "C - Commercial  $150", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_BROWN);
    draw_text(38, y, "I - Industrial  $200", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_DARK_GRAY);
    draw_text(38, y, "D - Road        $10", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_LIGHT_GREEN);
    draw_text(38, y, "P - Park        $50", COLOR_WHITE);

    /* Infrastructure - right column */
    y = 30;
    draw_text(340, y, "SERVICES:", COLOR_WHITE);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_LIGHT_RED);
    draw_text(358, y, "Power Plant $5000", COLOR_WHITE);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_LIGHT_CYAN);
    draw_text(358, y, "Water Pump  $2000", COLOR_WHITE);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_CYAN);
    draw_text(358, y, "Police      $500", COLOR_WHITE);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_RED);
    draw_text(358, y, "Fire Stn    $500", COLOR_WHITE);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_WHITE);
    draw_text(358, y, "Hospital    $1000", COLOR_LIGHT_GRAY);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_MAGENTA);
    draw_text(358, y, "School      $800", COLOR_WHITE);

    /* Controls */
    y = 195;
    draw_text(20, y, "CONTROLS:", COLOR_YELLOW);
    y += 20;
    draw_text(20, y, "Arrows - Move cursor", COLOR_WHITE);
    y += 18;
    draw_text(20, y, "Space  - Place tile", COLOR_WHITE);
    y += 18;
    draw_text(20, y, "H - View nearest citizen", COLOR_WHITE);
    y += 18;
    draw_text(20, y, "Z - View random citizen", COLOR_WHITE);
    y += 18;
    draw_text(20, y, "F1 - This help screen", COLOR_WHITE);
    y += 18;
    draw_text(20, y, "ESC - Quit / Back", COLOR_WHITE);

    /* Bottom prompt */
    draw_text(160, 330, "Press ESC or F1 to return", COLOR_YELLOW);
}


void draw_ui(GameState* game) {
    /* 2x font: chars are 10x14px, 12px spacing. Bar is y=320-349 (30px). */
    int bar_y = 328;  /* Center 14px text in 30px bar */

    /* Clear entire status bar area */
    draw_filled_rect(0, 320, SCREEN_WIDTH, 30, COLOR_BLACK);

    /* Separator line */
    draw_filled_rect(0, 320, SCREEN_WIDTH, 1, COLOR_DARK_GRAY);

    /* Money: "$50000" */
    draw_text(2, bar_y, "$", COLOR_YELLOW);
    draw_number(14, bar_y, game->funds, COLOR_YELLOW);

    /* Population: "Pop:0" */
    draw_text(120, bar_y, "Pop:", COLOR_WHITE);
    draw_number(168, bar_y, (long)game->population, COLOR_WHITE);

    /* Day */
    draw_text(230, bar_y, "Day:", COLOR_LIGHT_CYAN);
    draw_number(278, bar_y, (long)game->game_day, COLOR_LIGHT_CYAN);

    /* Time (HH:00) */
    {
        char time_buf[8];
        sprintf(time_buf, "%02u:00", game->game_time % 24);
        draw_text(340, bar_y, time_buf, COLOR_LIGHT_CYAN);
    }

    /* Current tool name */
    draw_text(420, bar_y, get_tile_name(game->current_tool), COLOR_LIGHT_GREEN);

    /* Coordinates */
    {
        char coord_buf[12];
        sprintf(coord_buf, "%u,%u", (unsigned)game->cursor_x, (unsigned)game->cursor_y);
        draw_text(548, bar_y, coord_buf, COLOR_LIGHT_GRAY);
    }
}

void draw_human_view(GameState* game) {
    Human* human;
    char buffer[64];
    int y_pos = 30;

    if (game->selected_human >= game->num_humans) {
        game->game_state = STATE_CITY_VIEW;
        return;
    }

    human = &game->humans[game->selected_human];

    clear_screen(COLOR_DARK_GRAY);

    /* Title bar */
    draw_filled_rect(10, 5, 400, 20, COLOR_BLUE);
    draw_text(14, 8, "CITIZEN DETAILS", COLOR_WHITE);

    /* Info panel background */
    draw_filled_rect(10, y_pos, 400, 280, COLOR_BLACK);
    draw_rect(10, y_pos, 400, 280, COLOR_LIGHT_GRAY);

    y_pos += 10;

    /* Name */
    draw_text(20, y_pos, "Name:", COLOR_LIGHT_CYAN);
    draw_text(80, y_pos, human->name, COLOR_WHITE);
    y_pos += 22;

    /* Age */
    sprintf(buffer, "Age: %u", (unsigned)human->age);
    draw_text(20, y_pos, buffer, COLOR_LIGHT_CYAN);
    y_pos += 22;

    /* Activity */
    draw_text(20, y_pos, "Activity:", COLOR_LIGHT_CYAN);
    draw_text(128, y_pos, get_activity_name(human->activity), COLOR_YELLOW);
    y_pos += 22;

    /* Location */
    sprintf(buffer, "Location: %u,%u", (unsigned)human->x, (unsigned)human->y);
    draw_text(20, y_pos, buffer, COLOR_LIGHT_CYAN);
    y_pos += 22;

    /* Home */
    sprintf(buffer, "Home: %u,%u", (unsigned)human->home_x, (unsigned)human->home_y);
    draw_text(20, y_pos, buffer, COLOR_LIGHT_CYAN);
    y_pos += 22;

    /* Work */
    sprintf(buffer, "Work: %u,%u", (unsigned)human->work_x, (unsigned)human->work_y);
    draw_text(20, y_pos, buffer, COLOR_LIGHT_CYAN);
    y_pos += 22;

    /* Stats */
    sprintf(buffer, "Happiness: %u%%", (unsigned)human->happiness);
    draw_text(20, y_pos, buffer, COLOR_GREEN);
    y_pos += 20;

    sprintf(buffer, "Health: %u%%", (unsigned)human->health);
    draw_text(20, y_pos, buffer, COLOR_GREEN);
    y_pos += 20;

    sprintf(buffer, "Wealth: %u%%", (unsigned)human->wealth);
    draw_text(20, y_pos, buffer, COLOR_GREEN);
    y_pos += 20;

    sprintf(buffer, "Education: %u%%", (unsigned)human->education);
    draw_text(20, y_pos, buffer, COLOR_GREEN);

    /* Instructions */
    draw_text(10, 326, "Press ESC to return", COLOR_LIGHT_GRAY);
}
