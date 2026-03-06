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
    NULL,           /*  4: TILE_ROAD (context-aware) */
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

/*
 * Fast draw_char using Set/Reset mode.  EGA registers are set up once,
 * then each font row is expanded to a 10-bit pattern (2x horizontal
 * scale), placed into byte-aligned positions, and written with a single
 * Bit Mask change per byte.  ~90 port I/O ops vs ~700 for the old
 * per-pixel draw_filled_rect approach.
 */
void draw_char(int x, int y, char c, unsigned char color) {
    volatile unsigned char *video = (volatile unsigned char *)0xA0000;
    int char_index, i, j;
    int base_byte, bit_off;

    if (c < 32 || c > 126)
        return;
    char_index = c - 32;
    base_byte = x / 8;
    bit_off = x % 8;

    /* Write mode 0, Set/Reset enabled for all planes */
    outp(0x3CE, 0x05); outp(0x3CF, 0x00);
    outp(0x3CE, 0x01); outp(0x3CF, 0x0F);
    outp(0x3CE, 0x00); outp(0x3CF, color);

    for (i = 0; i < 7; i++) {
        unsigned char glyph = font_data[char_index][i];
        unsigned short fg_bits = 0;
        unsigned long fg_placed;
        unsigned char fb[3];
        int sy = y + i * 2;
        int off0 = sy * 80 + base_byte;
        int off1 = off0 + 80;
        int b;

        /* Expand 5-bit glyph row to 10-bit pattern (2x scale) */
        for (j = 0; j < 5; j++) {
            if (glyph & (0x10 >> j))
                fg_bits |= (unsigned short)(3 << (8 - j * 2));
        }

        /* Place 10-bit pattern into 3-byte span at bit_off */
        fg_placed = (unsigned long)fg_bits << (14 - bit_off);
        fb[0] = (unsigned char)(fg_placed >> 16);
        fb[1] = (unsigned char)(fg_placed >> 8);
        fb[2] = (unsigned char)(fg_placed);

        for (b = 0; b < 3; b++) {
            if (fb[b] == 0) continue;
            outp(0x3CE, 0x08);
            outp(0x3CF, fb[b]);
            /* Latch read + Set/Reset write for both scanlines (2x vert) */
            video[off0 + b] = video[off0 + b];
            video[off0 + b] = 0xFF;
            video[off1 + b] = video[off1 + b];
            video[off1 + b] = 0xFF;
        }
    }

    /* Restore defaults */
    outp(0x3CE, 0x01); outp(0x3CF, 0x00);
    outp(0x3CE, 0x08); outp(0x3CF, 0xFF);
}

/*
 * Draw a character with a background colour in a single pass per plane.
 * Each 12x14 pixel cell (10px glyph + 2px gap, 7 font rows x 2 vscale)
 * is written without any visible flicker: for each plane, every pixel
 * in the cell is set to either the foreground or background plane bit
 * in one read-modify-write per byte.
 */
static void draw_char_bg(int x, int y, char c,
                          unsigned char fg, unsigned char bg) {
    volatile unsigned char *video = (volatile unsigned char *)0xA0000;
    int char_index, plane, i, j, b;
    int base_byte, bit_off;
    unsigned long cell_placed;
    unsigned char cmask[3]; /* cell-occupancy mask per byte */

    if (c < 32 || c > 126) c = ' ';
    char_index = c - 32;
    base_byte = x / 8;
    bit_off = x % 8;

    /* 12-pixel cell mask placed into byte positions */
    cell_placed = (unsigned long)0xFFF << (12 - bit_off);
    cmask[0] = (unsigned char)(cell_placed >> 16);
    cmask[1] = (unsigned char)(cell_placed >> 8);
    cmask[2] = (unsigned char)(cell_placed);

    /* Direct plane writes — no Set/Reset */
    outp(0x3CE, 0x05); outp(0x3CF, 0x00);  /* Write mode 0 */
    outp(0x3CE, 0x01); outp(0x3CF, 0x00);  /* Disable Set/Reset */
    outp(0x3CE, 0x08); outp(0x3CF, 0xFF);  /* Full bit mask */

    for (plane = 0; plane < 4; plane++) {
        unsigned char fg_bit = (fg >> plane) & 1;
        unsigned char bg_bit = (bg >> plane) & 1;

        outp(0x3CE, 0x04); outp(0x3CF, (unsigned char)plane);  /* Read plane */
        outp(0x3C4, 0x02); outp(0x3C5, (unsigned char)(1 << plane)); /* Write plane */

        for (i = 0; i < 7; i++) {
            unsigned char glyph = font_data[char_index][i];
            unsigned short fg_pattern = 0;
            unsigned long fg_placed_l;
            unsigned char fmask[3], pval[3];
            int sy, off0, off1;

            for (j = 0; j < 5; j++) {
                if (glyph & (0x10 >> j))
                    fg_pattern |= (unsigned short)(3 << (8 - j * 2));
            }

            fg_placed_l = (unsigned long)fg_pattern << (14 - bit_off);
            fmask[0] = (unsigned char)(fg_placed_l >> 16);
            fmask[1] = (unsigned char)(fg_placed_l >> 8);
            fmask[2] = (unsigned char)(fg_placed_l);

            /* Plane value for each byte: fg pixels get fg_bit, bg pixels get bg_bit */
            for (b = 0; b < 3; b++) {
                if (fg_bit && bg_bit)
                    pval[b] = cmask[b];
                else if (!fg_bit && !bg_bit)
                    pval[b] = 0;
                else if (fg_bit)
                    pval[b] = fmask[b];
                else
                    pval[b] = cmask[b] & ~fmask[b];
            }

            sy = y + i * 2;
            off0 = sy * 80 + base_byte;
            off1 = off0 + 80;

            for (b = 0; b < 3; b++) {
                if (cmask[b]) {
                    unsigned char v;
                    v = video[off0 + b];
                    video[off0 + b] = (v & ~cmask[b]) | pval[b];
                    v = video[off1 + b];
                    video[off1 + b] = (v & ~cmask[b]) | pval[b];
                }
            }
        }
    }

    /* Restore: all planes writable */
    outp(0x3C4, 0x02); outp(0x3C5, 0x0F);
}

/*
 * Draw a fixed-width text field with a background colour.
 * Draws exactly field_chars characters, padding short strings with spaces.
 * Each character cell is written atomically (no flicker).
 */
static void draw_text_bg(int x, int y, const char *text,
                          int field_chars,
                          unsigned char fg, unsigned char bg) {
    int i;
    int len;

    for (len = 0; text[len]; len++) {}

    for (i = 0; i < field_chars; i++) {
        draw_char_bg(x + i * 12, y, i < len ? text[i] : ' ', fg, bg);
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

/*
 * Rotate a 16x16 tile: rotation 0=0deg, 1=90CW, 2=180, 3=270CW.
 * 90CW: dst[y][x] = src[15-x][y]
 */
static void rotate_tile(const unsigned char src[16][16],
                        unsigned char dst[16][16], int rotation) {
    int x, y;
    if (rotation == 0) {
        memcpy(dst, src, 256);
    } else if (rotation == 1) {
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                dst[y][x] = src[15 - x][y];
    } else if (rotation == 2) {
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                dst[y][x] = src[15 - y][15 - x];
    } else {
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                dst[y][x] = src[x][15 - y];
    }
}

/*
 * Compute a 4-bit neighbor bitmask for same-type tiles.
 * N=1, S=2, E=4, W=8
 */
unsigned char neighbor_mask(GameState *game,
                           unsigned short mx, unsigned short my,
                           unsigned char tile_type) {
    unsigned char mask = 0;
    unsigned char t;
    if (my > 0) {
        t = game->map[my - 1][mx].type;
        if (t == tile_type ||
            (t == TILE_POWER_LINE && game->map[my - 1][mx].development == tile_type))
            mask |= 1;  /* N */
    }
    if (my + 1 < MAP_HEIGHT) {
        t = game->map[my + 1][mx].type;
        if (t == tile_type ||
            (t == TILE_POWER_LINE && game->map[my + 1][mx].development == tile_type))
            mask |= 2;  /* S */
    }
    if (mx + 1 < MAP_WIDTH) {
        t = game->map[my][mx + 1].type;
        if (t == tile_type ||
            (t == TILE_POWER_LINE && game->map[my][mx + 1].development == tile_type))
            mask |= 4;  /* E */
    }
    if (mx > 0) {
        t = game->map[my][mx - 1].type;
        if (t == tile_type ||
            (t == TILE_POWER_LINE && game->map[my][mx - 1].development == tile_type))
            mask |= 8;  /* W */
    }
    return mask;
}

/*
 * Road tile selection table, indexed by neighbor bitmask.
 * art: 0=horiz, 1=vert, 2=turn.  rot: rotation (0-3).
 */
const RoadTableEntry road_table[16] = {
    {0, 0}, /* 0:  isolated  -> horiz */
    {1, 0}, /* 1:  N         -> vert */
    {1, 0}, /* 2:  S         -> vert */
    {1, 0}, /* 3:  N+S       -> vert */
    {0, 0}, /* 4:  E         -> horiz */
    {2, 1}, /* 5:  N+E       -> turn 90CW */
    {2, 2}, /* 6:  S+E       -> turn 180 */
    {1, 0}, /* 7:  N+S+E     -> vert (no T) */
    {0, 0}, /* 8:  W         -> horiz */
    {2, 0}, /* 9:  N+W       -> turn 0 */
    {2, 3}, /* 10: S+W       -> turn 270CW */
    {1, 0}, /* 11: N+S+W     -> vert (no T) */
    {0, 0}, /* 12: E+W       -> horiz */
    {0, 0}, /* 13: N+E+W     -> horiz (no T) */
    {0, 0}, /* 14: S+E+W     -> horiz (no T) */
    {0, 0}, /* 15: all       -> horiz (no cross) */
};

/*
 * Extended road neighbor mask: treats TILE_ROAD and power-line-on-road
 * as road neighbors, so roads visually connect to power-line overlays.
 */
static unsigned char road_neighbor_mask_ext(GameState *game,
                                            unsigned short mx,
                                            unsigned short my) {
    unsigned char mask = 0;
    unsigned char t;
    if (my > 0) {
        t = game->map[my - 1][mx].type;
        if (t == TILE_ROAD ||
            (t == TILE_POWER_LINE && game->map[my - 1][mx].development == TILE_ROAD))
            mask |= 1;
    }
    if (my + 1 < MAP_HEIGHT) {
        t = game->map[my + 1][mx].type;
        if (t == TILE_ROAD ||
            (t == TILE_POWER_LINE && game->map[my + 1][mx].development == TILE_ROAD))
            mask |= 2;
    }
    if (mx + 1 < MAP_WIDTH) {
        t = game->map[my][mx + 1].type;
        if (t == TILE_ROAD ||
            (t == TILE_POWER_LINE && game->map[my][mx + 1].development == TILE_ROAD))
            mask |= 4;
    }
    if (mx > 0) {
        t = game->map[my][mx - 1].type;
        if (t == TILE_ROAD ||
            (t == TILE_POWER_LINE && game->map[my][mx - 1].development == TILE_ROAD))
            mask |= 8;
    }
    return mask;
}

/*
 * Composite two 16x16 tiles: overlay non-zero pixels over base.
 */
static void composite_tiles(const unsigned char base[16][16],
                            const unsigned char overlay[16][16],
                            unsigned char out[16][16]) {
    int y, x;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            out[y][x] = (overlay[y][x] != COLOR_TRANSPARENT) ? overlay[y][x] : base[y][x];
}

/*
 * Neighbor mask for power lines: counts adjacent TILE_POWER_LINE tiles
 * and TILE_POWER_PLANT tiles (any sub-position) as neighbors.
 */
static unsigned char powerline_neighbor_mask(GameState *game,
                                             unsigned short mx,
                                             unsigned short my) {
    unsigned char mask = 0;
    unsigned char t;
    if (my > 0) {
        t = game->map[my - 1][mx].type;
        if (t == TILE_POWER_LINE || t == TILE_POWER_PLANT) mask |= 1;
    }
    if (my + 1 < MAP_HEIGHT) {
        t = game->map[my + 1][mx].type;
        if (t == TILE_POWER_LINE || t == TILE_POWER_PLANT) mask |= 2;
    }
    if (mx + 1 < MAP_WIDTH) {
        t = game->map[my][mx + 1].type;
        if (t == TILE_POWER_LINE || t == TILE_POWER_PLANT) mask |= 4;
    }
    if (mx > 0) {
        t = game->map[my][mx - 1].type;
        if (t == TILE_POWER_LINE || t == TILE_POWER_PLANT) mask |= 8;
    }
    return mask;
}

/*
 * Extract a 16x16 sub-tile from a larger tile art array and draw it.
 * sub_row/sub_col select which 16x16 cell within the multi-tile art.
 * art_width is the pixel width of the full art (e.g. 48 for 3x3).
 */
static void draw_multitile_sub(int sx, int sy,
                               const unsigned char *art,
                               int art_width,
                               int sub_row, int sub_col) {
    unsigned char subtile[16][16];
    int y, x;
    int base_y = sub_row * 16;
    int base_x = sub_col * 16;

    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            subtile[y][x] = art[(base_y + y) * art_width + (base_x + x)];

    draw_tile_pixels(sx, sy, (const unsigned char (*)[16])subtile);
}

/*
 * Draw a tile using context-aware art selection for roads, rail, and
 * multi-tile zones. For other tile types, falls back to draw_tile().
 */
void draw_tile_in_context(int screen_x, int screen_y,
                          GameState *game,
                          unsigned short map_x, unsigned short map_y) {
#if defined(TILE_COUNT) && TILE_COUNT > 0
    unsigned char tile_type = game->map[map_y][map_x].type;
    unsigned char mask;

    /* 3x3 power plant: draw from powercoal art */
    if (tile_type == TILE_POWER_PLANT) {
        unsigned char subpos = game->map[map_y][map_x].development;
        int sub_row = subpos / 3;
        int sub_col = subpos % 3;
        draw_multitile_sub(screen_x, screen_y,
                           &tile_powercoal[0][0], 48,
                           sub_row, sub_col);
        return;
    }

    /* 3x3 residential zone: select art based on population state */
    if (tile_type == TILE_RESIDENTIAL) {
        unsigned char subpos = game->map[map_y][map_x].development;
        int sub_row = subpos / 3;
        int sub_col = subpos % 3;

        if (game->map[map_y][map_x].population == 0) {
            /* Unpopulated: draw from resunpop 48x48 art */
            draw_multitile_sub(screen_x, screen_y,
                               &tile_resunpop[0][0], 48,
                               sub_row, sub_col);
        } else {
            /* Populated: select art by density */
            const unsigned char *art;
            switch (game->map[map_y][map_x].density) {
                case 0:  art = &tile_reslowden1[0][0]; break;
                case 1:  art = &tile_resmedden1[0][0]; break;
                default: art = &tile_reshiden1[0][0];  break;
            }
            draw_multitile_sub(screen_x, screen_y, art, 48,
                               sub_row, sub_col);
        }
        return;
    }

    /* 3x3 commercial zone: select art based on population state */
    if (tile_type == TILE_COMMERCIAL) {
        unsigned char subpos = game->map[map_y][map_x].development;
        int sub_row = subpos / 3;
        int sub_col = subpos % 3;

        if (game->map[map_y][map_x].population == 0) {
            draw_multitile_sub(screen_x, screen_y,
                               &tile_comunpop[0][0], 48,
                               sub_row, sub_col);
        } else {
            const unsigned char *art;
            switch (game->map[map_y][map_x].density) {
                case 0:  art = &tile_comlight1[0][0]; break;
                case 1:  art = &tile_commed1[0][0];   break;
                default: art = &tile_comcorp1[0][0];  break;
            }
            draw_multitile_sub(screen_x, screen_y, art, 48,
                               sub_row, sub_col);
        }
        return;
    }

    /* 3x3 industrial zone: select art based on population state */
    if (tile_type == TILE_INDUSTRIAL) {
        unsigned char subpos = game->map[map_y][map_x].development;
        int sub_row = subpos / 3;
        int sub_col = subpos % 3;

        if (game->map[map_y][map_x].population == 0) {
            draw_multitile_sub(screen_x, screen_y,
                               &tile_indunpop[0][0], 48,
                               sub_row, sub_col);
        } else {
            const unsigned char *art;
            switch (game->map[map_y][map_x].density) {
                case 0:  art = &tile_indagri1[0][0];  break;
                case 1:  art = &tile_indmed1[0][0];   break;
                default: art = &tile_indheavy1[0][0]; break;
            }
            draw_multitile_sub(screen_x, screen_y, art, 48,
                               sub_row, sub_col);
        }
        return;
    }

    if (tile_type == TILE_ROAD) {
        TileArt art;
        unsigned char rot;
        unsigned char entry_art;

        mask = road_neighbor_mask_ext(game, map_x, map_y);
        entry_art = road_table[mask].art;
        rot = road_table[mask].rot;

        if (entry_art == 0) art = tile_roadhoriz;
        else if (entry_art == 1) art = tile_roadvert;
        else art = tile_roadturn;

        if (rot == 0) {
            draw_tile_pixels(screen_x, screen_y, art);
        } else {
            unsigned char buf[16][16];
            rotate_tile((const unsigned char (*)[16])art, buf, rot);
            draw_tile_pixels(screen_x, screen_y,
                             (const unsigned char (*)[16])buf);
        }
        return;
    }

    if (tile_type == TILE_POWER_LINE) {
        unsigned char dev = game->map[map_y][map_x].development;
        unsigned char pl_mask = powerline_neighbor_mask(game, map_x, map_y);
        unsigned char pl_rot = 0;

        /* Determine orientation: vertical if N/S neighbors only */
        if ((pl_mask & 3) && !(pl_mask & 12))
            pl_rot = 1;  /* vertical: rotate 90 */

        if (dev == TILE_ROAD || dev == TILE_RAIL) {
            /* Overlay on road or rail: draw base first, composite */
            unsigned char base[16][16];
            unsigned char overlay[16][16];
            unsigned char comp[16][16];

            if (dev == TILE_ROAD) {
                unsigned char rd_mask = road_neighbor_mask_ext(game, map_x, map_y);
                unsigned char rd_art = road_table[rd_mask].art;
                unsigned char rd_rot = road_table[rd_mask].rot;
                TileArt rd_src;

                if (rd_art == 0) rd_src = tile_roadhoriz;
                else if (rd_art == 1) rd_src = tile_roadvert;
                else rd_src = tile_roadturn;

                if (rd_rot == 0)
                    memcpy(base, rd_src, 256);
                else
                    rotate_tile((const unsigned char (*)[16])rd_src, base, rd_rot);
            } else {
                /* Rail */
                unsigned char rl_mask = neighbor_mask(game, map_x, map_y, TILE_RAIL);
                if ((rl_mask & 12) && !(rl_mask & 3))
                    rotate_tile(tile_rail1, base, 1);
                else
                    memcpy(base, tile_rail1, 256);
            }

            if (pl_rot == 0)
                memcpy(overlay, tile_powerline, 256);
            else
                rotate_tile(tile_powerline, overlay, pl_rot);

            composite_tiles(base, overlay, comp);
            draw_tile_pixels(screen_x, screen_y, (const unsigned char (*)[16])comp);
        } else {
            /* Standalone power line on grass: composite onto grass base */
            unsigned char grass_base[16][16];
            unsigned char overlay[16][16];
            unsigned char comp[16][16];
            memset(grass_base, COLOR_GREEN, 256);

            if (pl_rot == 0)
                memcpy(overlay, tile_powerline, 256);
            else
                rotate_tile(tile_powerline, overlay, pl_rot);

            composite_tiles(grass_base, overlay, comp);
            draw_tile_pixels(screen_x, screen_y,
                             (const unsigned char (*)[16])comp);
        }
        return;
    }

    if (tile_type == TILE_RAIL) {
        mask = neighbor_mask(game, map_x, map_y, TILE_RAIL);
        /* Has N or S neighbor (and no E/W only) -> vertical (base tile) */
        /* Has E or W neighbor (and no N/S only) -> horizontal (rotate 90) */
        /* Both axes or turns -> fallback to solid colour */
        if ((mask & 3) && !(mask & 12)) {
            /* vertical */
            draw_tile_pixels(screen_x, screen_y, tile_rail1);
        } else if ((mask & 12) && !(mask & 3)) {
            /* horizontal: rotate rail1 90CW */
            unsigned char buf[16][16];
            rotate_tile(tile_rail1, buf, 1);
            draw_tile_pixels(screen_x, screen_y,
                             (const unsigned char (*)[16])buf);
        } else if (mask == 0) {
            /* isolated rail: show vertical */
            draw_tile_pixels(screen_x, screen_y, tile_rail1);
        } else {
            /* T/cross: solid colour fallback */
            draw_filled_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE,
                             get_tile_color(TILE_RAIL));
        }
        return;
    }
#endif

    draw_tile(screen_x, screen_y, game->map[map_y][map_x].type);
}

unsigned char get_tile_color(unsigned char tile_type) {
    switch (tile_type) {
        case TILE_GRASS:        return COLOR_GREEN;
        case TILE_RESIDENTIAL:  return COLOR_YELLOW;
        case TILE_COMMERCIAL:   return COLOR_LIGHT_BLUE;
        case TILE_INDUSTRIAL:   return COLOR_BROWN;
        case TILE_ROAD:         return COLOR_DARK_GRAY;
        case TILE_POWER_LINE:   return COLOR_LIGHT_GRAY;
        case TILE_WATER:        return COLOR_BLUE;
        case TILE_PARK:         return COLOR_LIGHT_GREEN;
        case TILE_POLICE:       return COLOR_CYAN;
        case TILE_FIRE:         return COLOR_RED;
        case TILE_HOSPITAL:     return COLOR_WHITE;
        case TILE_SCHOOL:       return COLOR_MAGENTA;
        case TILE_POWER_PLANT:  return COLOR_LIGHT_RED;
        case TILE_WATER_PUMP:   return COLOR_LIGHT_CYAN;
        case TILE_RAIL:         return COLOR_BROWN;
        default:                return COLOR_BLACK;
    }
}

/* Draw a tile based on its type */
void draw_tile(int screen_x, int screen_y, unsigned char tile_type) {
    /* Use pixel art if available for this tile type */
#if defined(TILE_COUNT) && TILE_COUNT > 0
    if (tile_type < NUM_TILE_TYPES && tile_art_map[tile_type] != NULL) {
        draw_tile_pixels(screen_x, screen_y, tile_art_map[tile_type]);
        return;
    }
#endif

    /* Fallback: solid colour fill */
    draw_filled_rect(screen_x, screen_y, TILE_SIZE, TILE_SIZE,
                     get_tile_color(tile_type));
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
            screen_y = tile_y * TILE_SIZE + MAP_Y_OFFSET;

            draw_tile_in_context(screen_x, screen_y, game, map_x, map_y);
        }
    }
}

void draw_map_zoomed(GameState* game) {
    int tile_px, vtx, vty;
    int tile_x, tile_y, map_x, map_y;
    int screen_x, screen_y;
    unsigned char color;

    tile_px = 16 >> game->zoom_level;
    vtx = 640 / tile_px;
    vty = (VIEW_TILES_Y * TILE_SIZE) / tile_px;
    if (vtx > MAP_WIDTH) vtx = MAP_WIDTH;
    if (vty > MAP_HEIGHT) vty = MAP_HEIGHT;

    /* Clear map area — zoomed view may not fill the screen */
    draw_filled_rect(0, MAP_Y_OFFSET, 640, VIEW_TILES_Y * TILE_SIZE, COLOR_BLACK);

    for (tile_y = 0; tile_y < vty && tile_y + game->scroll_y < MAP_HEIGHT; tile_y++) {
        for (tile_x = 0; tile_x < vtx && tile_x + game->scroll_x < MAP_WIDTH; tile_x++) {
            map_x = tile_x + game->scroll_x;
            map_y = tile_y + game->scroll_y;
            screen_x = tile_x * tile_px;
            screen_y = tile_y * tile_px + MAP_Y_OFFSET;
            color = get_tile_color(game->map[map_y][map_x].type);

            if (tile_px == 1) {
                set_pixel(screen_x, screen_y, color);
            } else {
                draw_filled_rect(screen_x, screen_y, tile_px, tile_px, color);
            }
        }
    }
}

void draw_single_tile(GameState* game, unsigned short map_x, unsigned short map_y) {
    int screen_x, screen_y;
    int tile_px, vtx, vty;

    tile_px = 16 >> game->zoom_level;
    vtx = 640 / tile_px;
    vty = (VIEW_TILES_Y * TILE_SIZE) / tile_px;
    if (vtx > MAP_WIDTH) vtx = MAP_WIDTH;
    if (vty > MAP_HEIGHT) vty = MAP_HEIGHT;

    /* Only draw if visible in viewport */
    if (map_x < game->scroll_x || map_x >= game->scroll_x + vtx ||
        map_y < game->scroll_y || map_y >= game->scroll_y + vty)
        return;

    screen_x = (map_x - game->scroll_x) * tile_px;
    screen_y = (map_y - game->scroll_y) * tile_px + MAP_Y_OFFSET;

    if (game->zoom_level == 0) {
        draw_tile_in_context(screen_x, screen_y, game, map_x, map_y);
    } else {
        unsigned char color = get_tile_color(game->map[map_y][map_x].type);
        if (tile_px == 1) {
            set_pixel(screen_x, screen_y, color);
        } else {
            draw_filled_rect(screen_x, screen_y, tile_px, tile_px, color);
        }
    }
}

int get_tool_size(unsigned char tool) {
    if (tool == TILE_RESIDENTIAL || tool == TILE_COMMERCIAL ||
        tool == TILE_INDUSTRIAL || tool == TILE_POWER_PLANT)
        return 3;
    return 1;
}

/*
 * Clipped drawing helpers: restrict to the play area
 * (y from MAP_Y_OFFSET to MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE).
 */
static void draw_filled_rect_play(int x, int y, int w, int h,
                                   unsigned char color) {
    int play_top = MAP_Y_OFFSET;
    int play_bot = MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE;
    if (y < play_top) { h -= (play_top - y); y = play_top; }
    if (y + h > play_bot) h = play_bot - y;
    if (h <= 0 || w <= 0) return;
    draw_filled_rect(x, y, w, h, color);
}

static void draw_rect_play(int x, int y, int w, int h, unsigned char color) {
    int play_top = MAP_Y_OFFSET;
    int play_bot = MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE;
    int i;
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    int yt = (y < play_top) ? play_top : y;
    int yb = (y2 >= play_bot) ? play_bot - 1 : y2;

    /* Top edge */
    if (y >= play_top && y < play_bot)
        for (i = x; i <= x2; i++) set_pixel(i, y, color);
    /* Bottom edge */
    if (y2 >= play_top && y2 < play_bot)
        for (i = x; i <= x2; i++) set_pixel(i, y2, color);
    /* Left edge */
    for (i = yt; i <= yb; i++) set_pixel(x, i, color);
    /* Right edge */
    for (i = yt; i <= yb; i++) set_pixel(x2, i, color);
}

void draw_cursor(GameState* game) {
    int screen_x, screen_y;
    int tile_px, vtx, vty, corner;
    int size, half, ox, oy, total_px;

    tile_px = 16 >> game->zoom_level;
    vtx = 640 / tile_px;
    vty = (VIEW_TILES_Y * TILE_SIZE) / tile_px;
    if (vtx > MAP_WIDTH) vtx = MAP_WIDTH;
    if (vty > MAP_HEIGHT) vty = MAP_HEIGHT;

    if (game->cursor_x < game->scroll_x || game->cursor_x >= game->scroll_x + vtx ||
        game->cursor_y < game->scroll_y || game->cursor_y >= game->scroll_y + vty)
        return;

    screen_x = (game->cursor_x - game->scroll_x) * tile_px;
    screen_y = (game->cursor_y - game->scroll_y) * tile_px + MAP_Y_OFFSET;

    size = get_tool_size(game->current_tool);
    half = size / 2;  /* 0 for 1x1, 1 for 3x3 */
    total_px = size * tile_px;

    /* Offset to top-left of footprint (3x3 is centered on cursor) */
    ox = screen_x - half * tile_px;
    oy = screen_y - half * tile_px;

    if (game->zoom_level == 0) {
        if (size > 1) {
            /* Draw outline rectangle around the full footprint, clipped */
            draw_rect_play(ox, oy, total_px, total_px, COLOR_WHITE);
            draw_rect_play(ox + 1, oy + 1, total_px - 2, total_px - 2, COLOR_WHITE);
        } else {
            /* 1x1 cursor: 4px white corners */
            draw_filled_rect_play(screen_x, screen_y, 4, 4, COLOR_WHITE);
            draw_filled_rect_play(screen_x + 12, screen_y, 4, 4, COLOR_WHITE);
            draw_filled_rect_play(screen_x, screen_y + 12, 4, 4, COLOR_WHITE);
            draw_filled_rect_play(screen_x + 12, screen_y + 12, 4, 4, COLOR_WHITE);
        }
    } else if (tile_px >= 4) {
        /* Zoomed: scaled corners on full footprint */
        corner = tile_px / 4;
        if (corner < 1) corner = 1;
        draw_filled_rect_play(ox, oy, corner, corner, COLOR_WHITE);
        draw_filled_rect_play(ox + total_px - corner, oy, corner, corner, COLOR_WHITE);
        draw_filled_rect_play(ox, oy + total_px - corner, corner, corner, COLOR_WHITE);
        draw_filled_rect_play(ox + total_px - corner, oy + total_px - corner, corner, corner, COLOR_WHITE);
    } else {
        /* Tiny tiles: solid white block covering footprint */
        draw_filled_rect_play(ox, oy, total_px, total_px, COLOR_WHITE);
    }
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
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_BROWN);
    draw_text(38, y, "T - Rail        $20", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_LIGHT_GRAY);
    draw_text(38, y, "L - Power Line  $5", COLOR_WHITE);
    y += 20;

    draw_filled_rect(20, y + 2, 12, 12, COLOR_RED);
    draw_text(38, y, "B - Bulldozer   $1", COLOR_WHITE);

    /* Infrastructure - right column */
    y = 30;
    draw_text(340, y, "SERVICES:", COLOR_WHITE);
    y += 20;

    draw_filled_rect(340, y + 2, 12, 12, COLOR_LIGHT_RED);
    draw_text(358, y, "G - Power Plant $5000", COLOR_WHITE);
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
    draw_text(20, y, "ESC - Menu / Back", COLOR_WHITE);
    y += 18;
    draw_text(20, y, "Ctrl+Q - Quit", COLOR_WHITE);

    /* Bottom prompt */
    draw_text(160, 330, "Press ESC or F1 to return", COLOR_YELLOW);
}


void draw_ui(GameState* game) {
    /* 2x font: chars are 10x14px, 12px spacing. Bar is y=320-349 (30px). */
    int bar_y = 328;  /* Center 14px text in 30px bar */
    char buf[20];

    /* Separator line — cheap single-row rect, always redrawn */
    draw_filled_rect(0, 320, SCREEN_WIDTH, 1, COLOR_DARK_GRAY);

    /* Each field uses draw_text_bg with a fixed width, so stale digits
       are overwritten with background — no blanket clear, no flicker. */

    /* Money: "$50000" — 9 chars max */
    sprintf(buf, "$%ld", game->funds);
    draw_text_bg(2, bar_y, buf, 9, COLOR_YELLOW, COLOR_BLACK);

    /* Population — 9 chars max */
    sprintf(buf, "Pop:%u", (unsigned)game->population);
    draw_text_bg(120, bar_y, buf, 9, COLOR_WHITE, COLOR_BLACK);

    /* Day — 8 chars max */
    sprintf(buf, "Day:%u", (unsigned)game->game_day);
    draw_text_bg(230, bar_y, buf, 8, COLOR_LIGHT_CYAN, COLOR_BLACK);

    /* Time (HH:00) — 5 chars */
    sprintf(buf, "%02u:00", game->game_time % 24);
    draw_text_bg(340, bar_y, buf, 5, COLOR_LIGHT_CYAN, COLOR_BLACK);

    /* Current tool name or zoom indicator — 11 chars max */
    if (game->zoom_level > 0) {
        sprintf(buf, "ZOOM %dx", 1 << game->zoom_level);
        draw_text_bg(420, bar_y, buf, 11, COLOR_LIGHT_MAGENTA, COLOR_BLACK);
    } else if (game->current_tool == TILE_RESIDENTIAL) {
        static const char *rsuf[] = { " Low", " Med", " Hi" };
        sprintf(buf, "Res%s", rsuf[game->current_density < 3 ? game->current_density : 0]);
        draw_text_bg(420, bar_y, buf, 11, COLOR_LIGHT_GREEN, COLOR_BLACK);
    } else if (game->current_tool == TILE_COMMERCIAL) {
        static const char *csuf[] = { " Light", " Med", " Corp" };
        sprintf(buf, "Com%s", csuf[game->current_density < 3 ? game->current_density : 0]);
        draw_text_bg(420, bar_y, buf, 11, COLOR_LIGHT_GREEN, COLOR_BLACK);
    } else if (game->current_tool == TILE_INDUSTRIAL) {
        static const char *isuf[] = { " Agri", " Med", " Heavy" };
        sprintf(buf, "Ind%s", isuf[game->current_density < 3 ? game->current_density : 0]);
        draw_text_bg(420, bar_y, buf, 11, COLOR_LIGHT_GREEN, COLOR_BLACK);
    } else {
        draw_text_bg(420, bar_y, get_tile_name(game->current_tool),
                     11, COLOR_LIGHT_GREEN, COLOR_BLACK);
    }
}

/* ---- Menu bar definitions ---- */

static const char *menu_labels[] = { "File", "Build", "Speed", "Options" };
const int menu_label_x[] = { 4, 68, 144, 220 };
static const int menu_label_w[] = { 4, 5, 5, 7 };

/* CS menu */
static const MenuItem menu_cs[] = {
    { "About CitySim",  0, 0, 3 },
    { "Help        F1", 0, 0, 1 },
    { "Quit       ^Q",  0, 0, 2 }
};
#define MENU_CS_COUNT 3

/* Build menu */
static const MenuItem menu_build[] = {
    { "Residential",     MF_HEADING, 0, 4 },
    { "  Low Density",   0, TILE_RESIDENTIAL,              0 },
    { "  Med Density",   0, TILE_RESIDENTIAL | (1 << 5),   0 },
    { "  High Density",  0, TILE_RESIDENTIAL | (2 << 5),   0 },
    { "Commercial",      MF_HEADING, 0, 4 },
    { "  Light",         0, TILE_COMMERCIAL,               0 },
    { "  Medium",        0, TILE_COMMERCIAL  | (1 << 5),   0 },
    { "  Corporate",     0, TILE_COMMERCIAL  | (2 << 5),   0 },
    { "Industrial",      MF_HEADING, 0, 4 },
    { "  Agriculture",   0, TILE_INDUSTRIAL,               0 },
    { "  Medium",        0, TILE_INDUSTRIAL  | (1 << 5),   0 },
    { "  Heavy",         0, TILE_INDUSTRIAL  | (2 << 5),   0 },
    { "Bulldozer     B", 0, TILE_BULLDOZER, 0 },
    { "Road",            0, TILE_ROAD, 0 },
    { "Rail",            0, TILE_RAIL, 0 },
    { "Power Line",      0, TILE_POWER_LINE, 0 },
    { "Park",            0, TILE_PARK, 0 },
    { "Power Plant",     0, TILE_POWER_PLANT, 0 }
};
#define MENU_BUILD_COUNT 18

/* Speed menu */
static const MenuItem menu_speed[] = {
    { "Coming Soon", MF_DISABLED, 0, 4 }
};
#define MENU_SPEED_COUNT 1

/* Options menu */
static const MenuItem menu_options[] = {
    { "Coming Soon", MF_DISABLED, 0, 4 }
};
#define MENU_OPTIONS_COUNT 1

const MenuItem *menu_items[] = { menu_cs, menu_build, menu_speed, menu_options };
const int menu_counts[] = { MENU_CS_COUNT, MENU_BUILD_COUNT, MENU_SPEED_COUNT, MENU_OPTIONS_COUNT };

/* Find the first selectable item in a menu */
signed char menu_first_selectable(int menu_idx) {
    int i;
    const MenuItem *items = menu_items[menu_idx];
    int count = menu_counts[menu_idx];
    for (i = 0; i < count; i++) {
        if (!(items[i].flags & (MF_HEADING | MF_DISABLED)))
            return (signed char)i;
    }
    return 0;
}

/* Find next selectable item (direction: +1 or -1), wrapping */
signed char menu_next_selectable(int menu_idx, int cur, int dir) {
    const MenuItem *items = menu_items[menu_idx];
    int count = menu_counts[menu_idx];
    int i = cur;
    int tries = 0;
    while (tries < count) {
        i += dir;
        if (i < 0) i = count - 1;
        if (i >= count) i = 0;
        if (!(items[i].flags & (MF_HEADING | MF_DISABLED)))
            return (signed char)i;
        tries++;
    }
    return (signed char)cur;
}

void draw_menu_bar(GameState* game) {
    int i;
    char buf[8];
    /* Bar background */
    draw_filled_rect(0, 0, 640, MENU_BAR_HEIGHT - 1, COLOR_LIGHT_GRAY);
    /* Separator line at bottom of menu bar */
    draw_filled_rect(0, MENU_BAR_HEIGHT - 1, 640, 1, COLOR_DARK_GRAY);

    for (i = 0; i < NUM_MENUS; i++) {
        unsigned char fg, bg;
        if (game->menu_active && game->menu_selected == i) {
            fg = COLOR_WHITE;
            bg = COLOR_BLUE;
        } else {
            fg = COLOR_BLACK;
            bg = COLOR_LIGHT_GRAY;
        }
        draw_text_bg(menu_label_x[i], 1, menu_labels[i],
                     menu_label_w[i], fg, bg);
    }

    /* RCI demand indicators, right-aligned in menu bar */
    sprintf(buf, "R%+d", game->rci_demand[0]);
    draw_text_bg(490, 1, buf, 4,
                 game->rci_demand[0] > 0 ? COLOR_GREEN : COLOR_RED,
                 COLOR_LIGHT_GRAY);

    sprintf(buf, "C%+d", game->rci_demand[1]);
    draw_text_bg(542, 1, buf, 4,
                 game->rci_demand[1] > 0 ? COLOR_BLUE : COLOR_RED,
                 COLOR_LIGHT_GRAY);

    sprintf(buf, "I%+d", game->rci_demand[2]);
    draw_text_bg(594, 1, buf, 4,
                 game->rci_demand[2] > 0 ? COLOR_BROWN : COLOR_RED,
                 COLOR_LIGHT_GRAY);
}

void draw_menu_dropdown(GameState* game) {
    const MenuItem *items;
    int count, i;
    int drop_x, drop_w, drop_h;
    int item_y;

    if (!game->menu_active)
        return;

    items = menu_items[game->menu_selected];
    count = menu_counts[game->menu_selected];

    /* Dropdown position */
    drop_x = menu_label_x[game->menu_selected];
    drop_w = 180;  /* fixed width for all dropdowns */
    drop_h = count * 16 + 2;  /* 16px per row + 2px border */

    /* White background with black border */
    draw_filled_rect(drop_x, MENU_BAR_HEIGHT, drop_w, drop_h, COLOR_WHITE);
    draw_rect(drop_x, MENU_BAR_HEIGHT, drop_w, drop_h, COLOR_BLACK);

    for (i = 0; i < count; i++) {
        unsigned char fg, bg;
        item_y = MENU_BAR_HEIGHT + 1 + i * 16;

        if (items[i].flags & MF_HEADING) {
            fg = COLOR_DARK_GRAY;
            bg = COLOR_WHITE;
        } else if (items[i].flags & MF_DISABLED) {
            fg = COLOR_DARK_GRAY;
            bg = COLOR_WHITE;
        } else if (game->menu_drop_sel == i) {
            fg = COLOR_WHITE;
            bg = COLOR_BLUE;
            draw_filled_rect(drop_x + 1, item_y, drop_w - 2, 16, COLOR_BLUE);
        } else {
            fg = COLOR_BLACK;
            bg = COLOR_WHITE;
        }

        draw_text_bg(drop_x + 4, item_y + 1, items[i].label, 14, fg, bg);
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
