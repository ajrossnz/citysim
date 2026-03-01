#ifndef CITYSIM_H
#define CITYSIM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <conio.h>
#include <i86.h>

/* EGA Graphics Mode Constants */
#define EGA_MODE 0x10       /* 640x350 16 colour */
#define TEXT_MODE 0x03
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 350
#define EGA_MEMORY 0xA000

/* City Map Constants */
#define MAP_WIDTH 512
#define MAP_HEIGHT 512
#define TILE_SIZE 16        /* 16x16 pixels per tile in city view */
#define VIEW_TILES_X 40     /* Number of tiles visible horizontally */
#define VIEW_TILES_Y 20     /* Number of tiles visible vertically (320px, leaves 30px for status bar) */

/* Tile Types */
#define TILE_GRASS 0
#define TILE_RESIDENTIAL 1
#define TILE_COMMERCIAL 2
#define TILE_INDUSTRIAL 3
#define TILE_ROAD 4
#define TILE_POWER_LINE 5
#define TILE_WATER 6
#define TILE_PARK 7
#define TILE_POLICE 8
#define TILE_FIRE 9
#define TILE_HOSPITAL 10
#define TILE_SCHOOL 11
#define TILE_POWER_PLANT 12
#define TILE_WATER_PUMP 13
#define TILE_RAIL 14

/* Game States */
#define STATE_CITY_VIEW 0
#define STATE_HUMAN_VIEW 1
#define STATE_MENU 2

/* Human Activities */
#define ACTIVITY_SLEEPING 0
#define ACTIVITY_WORKING 1
#define ACTIVITY_SHOPPING 2
#define ACTIVITY_LEISURE 3
#define ACTIVITY_TRAVELING 4
#define ACTIVITY_EATING 5

/* Colours (EGA Palette) */
#define COLOR_BLACK 0
#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_CYAN 3
#define COLOR_RED 4
#define COLOR_MAGENTA 5
#define COLOR_BROWN 6
#define COLOR_LIGHT_GRAY 7
#define COLOR_DARK_GRAY 8
#define COLOR_LIGHT_BLUE 9
#define COLOR_LIGHT_GREEN 10
#define COLOR_LIGHT_CYAN 11
#define COLOR_LIGHT_RED 12
#define COLOR_LIGHT_MAGENTA 13
#define COLOR_YELLOW 14
#define COLOR_WHITE 15

/* Maximum entities */
#define MAX_HUMANS 256
#define MAX_BUILDINGS 512

/* Time Constants */
#define TICKS_PER_HOUR 18   /* Approximately 1 hour = 1 second */
#define HOURS_PER_DAY 24

/* Structure Definitions */

typedef struct {
    unsigned char type;
    unsigned char population;
    unsigned char power;
    unsigned char water;
    unsigned char development;
    unsigned char pollution;
} Tile;

typedef struct {
    char name[20];
    unsigned short x;
    unsigned short y;
    unsigned char age;
    unsigned char activity;
    unsigned short home_x;
    unsigned short home_y;
    unsigned short work_x;
    unsigned short work_y;
    unsigned char happiness;
    unsigned char health;
    unsigned char wealth;
    unsigned char education;
} Human;

typedef struct {
    unsigned char type;
    unsigned short x;
    unsigned short y;
    unsigned char width;
    unsigned char height;
    unsigned char capacity;
    unsigned char occupancy;
    unsigned char condition;
} Building;

typedef struct {
    Tile map[MAP_HEIGHT][MAP_WIDTH];
    Human humans[MAX_HUMANS];
    Building buildings[MAX_BUILDINGS];
    unsigned int population;
    unsigned int num_buildings;
    unsigned int num_humans;
    long funds;
    unsigned int game_time;      /* In hours */
    unsigned int game_day;
    unsigned short scroll_x;
    unsigned short scroll_y;
    unsigned short cursor_x;
    unsigned short cursor_y;
    unsigned char current_tool;
    unsigned char game_state;
    unsigned char selected_human;
    unsigned char zoom_level;
} GameState;

/* Road/rail tile selection table entry */
typedef struct { unsigned char art; unsigned char rot; } RoadTableEntry;
extern const RoadTableEntry road_table[16];
unsigned char neighbor_mask(GameState*, unsigned short, unsigned short, unsigned char);

/* Function Prototypes */

/* Graphics Functions */
void init_ega(void);
void set_text_mode(void);
void set_pixel(int x, int y, unsigned char color);
void draw_tile(int screen_x, int screen_y, unsigned char tile_type);
void draw_map(GameState* game);
void draw_single_tile(GameState* game, unsigned short map_x, unsigned short map_y);
void draw_tile_in_context(int screen_x, int screen_y, GameState* game, unsigned short map_x, unsigned short map_y);
void draw_cursor(GameState* game);
void draw_ui(GameState* game);
void draw_human_view(GameState* game);
void clear_screen(unsigned char color);
void draw_rect(int x, int y, int width, int height, unsigned char color);
void draw_filled_rect(int x, int y, int width, int height, unsigned char color);
void draw_text(int x, int y, const char* text, unsigned char color);
void draw_number(int x, int y, long num, unsigned char color);
void draw_char(int x, int y, char c, unsigned char color);
void draw_help_screen(void);
unsigned char get_tile_color(unsigned char tile_type);
void draw_map_zoomed(GameState* game);

/* Game Logic Functions */
void init_game(GameState* game);
void update_game(GameState* game);
void update_humans(GameState* game);
void update_buildings(GameState* game);
void update_tile_services(GameState* game);
void spawn_human(GameState* game, unsigned short x, unsigned short y);
void handle_input(GameState* game);
void place_tile(GameState* game, unsigned short x, unsigned short y, unsigned char type);
int get_tile_cost(unsigned char type);
void calculate_human_activity(GameState* game, Human* human);
const char* get_activity_name(unsigned char activity);
const char* get_tile_name(unsigned char type);

/* Utility Functions */
unsigned int rand_range(unsigned int min, unsigned int max);
void generate_name(char* name);

#endif /* CITYSIM_H */
