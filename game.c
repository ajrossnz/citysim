#include "citysim.h"

#define IS_ZONE(t) ((t) == TILE_RESIDENTIAL || (t) == TILE_COMMERCIAL || (t) == TILE_INDUSTRIAL)
#define IS_3X3(t) (IS_ZONE(t) || (t) == TILE_POWER_PLANT || (t) == TILE_POLICE || (t) == TILE_FIRE || (t) == TILE_HOSPITAL)

/* ---- Mouse driver (INT 33h) ---- */

int mouse_init(void) {
    union REGS regs;
    regs.x.eax = 0x0000;  /* Function 0: init/reset */
    int386(0x33, &regs, &regs);
    return (regs.x.eax == 0xFFFF) ? 1 : 0;
}

void mouse_read(MouseState* ms) {
    union REGS regs;
    regs.x.eax = 0x0003;  /* Function 3: get position and buttons */
    int386(0x33, &regs, &regs);
    ms->buttons = (unsigned char)(regs.x.ebx & 0x07);
    ms->pixel_x = (unsigned short)(regs.x.ecx);
    ms->pixel_y = (unsigned short)(regs.x.edx);
}

void mouse_show(void) {
    union REGS regs;
    regs.x.eax = 0x0001;
    int386(0x33, &regs, &regs);
}

void mouse_hide(void) {
    union REGS regs;
    regs.x.eax = 0x0002;
    int386(0x33, &regs, &regs);
}

static unsigned int seed = 12345;

unsigned int rand_range(unsigned int min, unsigned int max) {
    seed = seed * 1103515245 + 12345;
    return min + (seed % (max - min + 1));
}

const char* name_prefixes[] = {
    "John", "Mary", "James", "Patricia", "Robert", "Jennifer", "Michael", "Linda",
    "William", "Barbara", "David", "Elizabeth", "Richard", "Susan", "Joseph", "Jessica"
};

const char* name_suffixes[] = {
    "Smith", "Johnson", "Williams", "Brown", "Jones", "Garcia", "Miller", "Davis",
    "Rodriguez", "Martinez", "Hernandez", "Lopez", "Gonzalez", "Wilson", "Anderson", "Thomas"
};

void generate_name(char* name) {
    sprintf(name, "%s %s",
            name_prefixes[rand_range(0, 15)],
            name_suffixes[rand_range(0, 15)]);
}

/*
 * Convert game_day (0-based) to a calendar date string.
 * Day 0 = Monday, 1st January 1900.
 * Output: "Mon 1 Jan 1900" (max ~15 chars).
 */
void format_date(unsigned int game_day, char *buf) {
    static const char *day_names[] = {
        "Mon","Tue","Wed","Thu","Fri","Sat","Sun"
    };
    static const char *mon_names[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    static const int mon_days[] = {
        31,28,31,30,31,30,31,31,30,31,30,31
    };
    unsigned int year = 1900;
    unsigned int days_left = game_day;
    int dow = (int)(game_day % 7);  /* 0=Mon since 1 Jan 1900 was Monday */
    int leap, yd, m, md;

    /* Find year */
    while (1) {
        leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        yd = 365 + leap;
        if (days_left < (unsigned int)yd) break;
        days_left -= yd;
        year++;
    }

    /* Find month */
    leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    for (m = 0; m < 12; m++) {
        md = mon_days[m] + (m == 1 && leap ? 1 : 0);
        if (days_left < (unsigned int)md) break;
        days_left -= md;
    }
    if (m >= 12) m = 11;

    sprintf(buf, "%s %u %s %u", day_names[dow],
            (unsigned)(days_left + 1), mon_names[m], year);
}

const char* get_activity_name(unsigned char activity) {
    switch (activity) {
        case ACTIVITY_SLEEPING: return "Sleeping";
        case ACTIVITY_WORKING: return "Working";
        case ACTIVITY_SHOPPING: return "Shopping";
        case ACTIVITY_LEISURE: return "Leisure";
        case ACTIVITY_TRAVELING: return "Traveling";
        case ACTIVITY_EATING: return "Eating";
        default: return "Unknown";
    }
}

const char* get_tile_name(unsigned char type) {
    switch (type) {
        case TILE_GRASS: return "Grass";
        case TILE_RESIDENTIAL: return "Residential";
        case TILE_COMMERCIAL: return "Commercial";
        case TILE_INDUSTRIAL: return "Industrial";
        case TILE_ROAD: return "Road";
        case TILE_POWER_LINE: return "Power";
        case TILE_WATER: return "Water";
        case TILE_PARK: return "Park";
        case TILE_POLICE: return "Police";
        case TILE_FIRE: return "Fire Sta";
        case TILE_HOSPITAL: return "Hospital";
        case TILE_AIRPORT: return "Airport";
        case TILE_SCHOOL: return "School";
        case TILE_POWER_PLANT: return "Power Plant";
        case TILE_WATER_PUMP: return "Water Pump";
        case TILE_RAIL: return "Rail";
        case TILE_BULLDOZER: return "Bulldozer";
        case TILE_WOODS: return "Woods";
        case TILE_RIVER: return "River";
        case TILE_DIRT: return "Dirt";
        default: return "Unknown";
    }
}

int get_tile_cost(unsigned char type) {
    switch (type) {
        case TILE_GRASS: return 0;
        case TILE_BULLDOZER: return 1;
        case TILE_RESIDENTIAL: return 100;
        case TILE_COMMERCIAL: return 150;
        case TILE_INDUSTRIAL: return 200;
        case TILE_ROAD: return 10;
        case TILE_POWER_LINE: return 5;
        case TILE_PARK: return 50;
        case TILE_POLICE: return 500;
        case TILE_FIRE: return 500;
        case TILE_HOSPITAL: return 1000;
        case TILE_AIRPORT: return 10000;
        case TILE_SCHOOL: return 800;
        case TILE_POWER_PLANT: return 5000;
        case TILE_WATER_PUMP: return 2000;
        case TILE_RAIL: return 20;
        case TILE_WOODS: return 0;
        case TILE_RIVER: return 0;
        case TILE_DIRT: return 0;
        default: return 0;
    }
}

/*
 * Micropolis-style river: biased random walk stamping diamond-shaped
 * water blobs. Uses 8 directions (N,NE,E,SE,S,SW,W,NW) with periodic
 * snapping back to the global river direction for natural meander.
 *
 * dir: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
 */
static const int dir_dx[] = { 0, 1, 1, 1, 0,-1,-1,-1 };
static const int dir_dy[] = {-1,-1, 0, 1, 1, 1, 0,-1 };

static void stamp_river_diamond(GameState *game, int cx, int cy, int r) {
    int dy, dx;
    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            int ny = cy + dy, nx = cx + dx;
            if (abs(dx) + abs(dy) <= r &&
                nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT)
                game->map[ny][nx].type = TILE_RIVER;
        }
    }
}

static void do_river(GameState *game, int sx, int sy,
                     int river_dir, int blob_radius) {
    int cur_dir = river_dir;
    int x = sx, y = sy;

    while (x >= blob_radius && x < MAP_WIDTH - blob_radius &&
           y >= blob_radius && y < MAP_HEIGHT - blob_radius) {
        stamp_river_diamond(game, x, y, blob_radius);

        /* ~10% chance: snap back to global river direction */
        if (rand_range(0, 99) < 10) {
            cur_dir = river_dir;
        } else {
            /* ~45% chance each of turning ±45 degrees */
            if (rand_range(0, 199) > 90)
                cur_dir = (cur_dir + 1) % 8;
            if (rand_range(0, 199) > 90)
                cur_dir = (cur_dir + 7) % 8;
        }

        x += dir_dx[cur_dir];
        y += dir_dy[cur_dir];
    }
}

void generate_terrain(GameState* game, unsigned int entropy) {
    int i, j;
    int cx, cy, river_dir;

    /* Seed: combine caller entropy with PIT timer for true randomness */
    seed = entropy ^ ((unsigned int)inp(0x40) << 8) ^ (unsigned int)inp(0x40);
    seed = seed * 1103515245 + 12345;

    /* Pick a starting point near the center (Micropolis-style) */
    cx = MAP_WIDTH / 4 + (int)rand_range(0, MAP_WIDTH / 2);
    cy = MAP_HEIGHT / 4 + (int)rand_range(0, MAP_HEIGHT / 2);

    /* Pick a random cardinal direction (N=0, E=2, S=4, W=6) */
    river_dir = (int)rand_range(0, 3) * 2;

    /* Big river: from center outward in chosen direction */
    do_river(game, cx, cy, river_dir, 4);

    /* Big river: from center outward in opposite direction */
    do_river(game, cx, cy, (river_dir + 4) % 8, 4);

    /* Small tributary from center in a different direction */
    river_dir = (int)rand_range(0, 3) * 2;
    do_river(game, cx, cy, river_dir, 2);

    /* Scatter woods clusters; first 8 near center */
    for (i = 0; i < (int)rand_range(40, 80); i++) {
        int cx, cy;
        int radius = (int)rand_range(4, 15);
        if (i < 8) {
            cx = MAP_WIDTH / 2 + (int)rand_range(0, 60) - 30;
            cy = MAP_HEIGHT / 2 + (int)rand_range(0, 60) - 30;
        } else {
            cx = (int)rand_range(10, MAP_WIDTH - 10);
            cy = (int)rand_range(10, MAP_HEIGHT - 10);
        }
        for (j = 0; j < radius * radius; j++) {
            int wx = cx + (int)rand_range(0, radius * 2) - radius;
            int wy = cy + (int)rand_range(0, radius * 2) - radius;
            if (wx >= 0 && wx < MAP_WIDTH && wy >= 0 && wy < MAP_HEIGHT &&
                game->map[wy][wx].type == TILE_GRASS)
                game->map[wy][wx].type = TILE_WOODS;
        }
    }

    /* Scatter dirt patches; first 3 near center */
    for (i = 0; i < (int)rand_range(10, 25); i++) {
        int cx, cy;
        int radius = (int)rand_range(3, 8);
        if (i < 3) {
            cx = MAP_WIDTH / 2 + (int)rand_range(0, 50) - 25;
            cy = MAP_HEIGHT / 2 + (int)rand_range(0, 50) - 25;
        } else {
            cx = (int)rand_range(10, MAP_WIDTH - 10);
            cy = (int)rand_range(10, MAP_HEIGHT - 10);
        }
        for (j = 0; j < radius * radius / 2; j++) {
            int wx = cx + (int)rand_range(0, radius * 2) - radius;
            int wy = cy + (int)rand_range(0, radius * 2) - radius;
            if (wx >= 0 && wx < MAP_WIDTH && wy >= 0 && wy < MAP_HEIGHT &&
                game->map[wy][wx].type == TILE_GRASS)
                game->map[wy][wx].type = TILE_DIRT;
        }
    }
}

void init_game(GameState* game) {
    int x, y;
    
    memset(game, 0, sizeof(GameState));
    
    /* Initialize map with grass */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            game->map[y][x].type = TILE_GRASS;
            game->map[y][x].population = 0;
            game->map[y][x].power = 0;
            game->map[y][x].water = 0;
            game->map[y][x].development = 0;
            game->map[y][x].pollution = 0;
        }
    }
    /* Starting funds — varies by difficulty */
    game->funds = 50000;
    game->game_time = 8;  /* Start at 8 AM */
    game->game_day = 0;   /* Day 0 = Monday 1 Jan 1900 */
    game->current_tool = TILE_RESIDENTIAL;
    game->current_density = 2; /* High density default */
    game->game_state = STATE_CITY_VIEW;
    game->difficulty = DIFFICULTY_EASY;
    game->city_tax = DEFAULT_TAX_RATE;
    game->game_speed = 2; /* Normal */
    game->fund_police = 100;
    game->fund_fire = 100;
    game->fund_health = 100;
    game->fund_education = 100;
    game->auto_budget = 1;
    game->show_newspaper = 1;
    game->last_year_day = 0;
    game->cursor_x = 256;
    game->cursor_y = 256;

    /* Center scroll on cursor */
    game->scroll_x = game->cursor_x - VIEW_TILES_X / 2;
    game->scroll_y = game->cursor_y - VIEW_TILES_Y / 2;
}

void spawn_human(GameState* game, unsigned short x, unsigned short y) {
    Human* human;
    
    if (game->num_humans >= MAX_HUMANS)
        return;
    
    human = &game->humans[game->num_humans];
    generate_name(human->name);
    human->x = x;
    human->y = y;
    human->age = rand_range(18, 65);
    human->activity = ACTIVITY_SLEEPING;
    human->home_x = x;
    human->home_y = y;
    human->work_x = rand_range(0, MAP_WIDTH - 1);
    human->work_y = rand_range(0, MAP_HEIGHT - 1);
    human->happiness = rand_range(50, 80);
    human->health = rand_range(60, 90);
    human->wealth = rand_range(30, 70);
    human->education = rand_range(40, 80);
    
    game->num_humans++;
}

void calculate_human_activity(GameState* game, Human* human) {
    unsigned int hour = game->game_time % 24;
    
    /* Simple activity schedule based on time of day */
    if (hour >= 0 && hour < 6) {
        human->activity = ACTIVITY_SLEEPING;
        human->x = human->home_x;
        human->y = human->home_y;
    } else if (hour >= 6 && hour < 8) {
        human->activity = ACTIVITY_EATING;
        human->x = human->home_x;
        human->y = human->home_y;
    } else if (hour >= 8 && hour < 9) {
        human->activity = ACTIVITY_TRAVELING;
    } else if (hour >= 9 && hour < 17) {
        human->activity = ACTIVITY_WORKING;
        human->x = human->work_x;
        human->y = human->work_y;
    } else if (hour >= 17 && hour < 18) {
        human->activity = ACTIVITY_TRAVELING;
    } else if (hour >= 18 && hour < 19) {
        human->activity = ACTIVITY_EATING;
        human->x = human->home_x;
        human->y = human->home_y;
    } else if (hour >= 19 && hour < 22) {
        if (rand_range(0, 10) < 3) {
            human->activity = ACTIVITY_SHOPPING;
        } else {
            human->activity = ACTIVITY_LEISURE;
        }
    } else {
        human->activity = ACTIVITY_SLEEPING;
        human->x = human->home_x;
        human->y = human->home_y;
    }
    
    /* Random small movement when traveling */
    if (human->activity == ACTIVITY_TRAVELING) {
        if (rand_range(0, 10) < 5) {
            if (human->x < human->work_x && human->x < MAP_WIDTH - 1)
                human->x++;
            else if (human->x > human->work_x && human->x > 0)
                human->x--;
            
            if (human->y < human->work_y && human->y < MAP_HEIGHT - 1)
                human->y++;
            else if (human->y > human->work_y && human->y > 0)
                human->y--;
        }
    }
}

void update_humans(GameState* game) {
    unsigned int i;
    
    for (i = 0; i < game->num_humans; i++) {
        calculate_human_activity(game, &game->humans[i]);
        
        /* Slowly adjust happiness based on city conditions */
        if (rand_range(0, 100) < 5) {
            Tile* home_tile = &game->map[game->humans[i].home_y][game->humans[i].home_x];
            
            if (home_tile->power && home_tile->water) {
                if (game->humans[i].happiness < 100)
                    game->humans[i].happiness++;
            } else {
                if (game->humans[i].happiness > 0)
                    game->humans[i].happiness--;
            }
            
            /* Pollution affects health */
            if (home_tile->pollution > 50) {
                if (game->humans[i].health > 0)
                    game->humans[i].health--;
            } else if (game->humans[i].health < 100) {
                game->humans[i].health++;
            }
        }
    }
}

/*
 * Find the anchor (top-left, development==0) of a 3x3 zone given any
 * tile within it.  Returns 1 and sets ax/ay on success, 0 on failure.
 */
static int find_zone_anchor(GameState *game, unsigned short x, unsigned short y,
                            unsigned short *ax, unsigned short *ay) {
    unsigned char dev = game->map[y][x].development;
    int r = dev / 3;
    int c = dev % 3;
    if (x < c || y < r) return 0;
    *ax = x - c;
    *ay = y - r;
    return 1;
}

void update_tile_services(GameState* game) {
    int x, y, dx, dy, r, c;
    static unsigned int queue[4096];
    int head, tail;
    unsigned short qx, qy, nx, ny;
    unsigned short ax, ay;
    unsigned char t;
    static unsigned int pollution_timer = 0;
    int do_pollution;

    pollution_timer++;
    do_pollution = (pollution_timer >= 6);
    if (do_pollution) pollution_timer = 0;

    /* Reset services in a single pass */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            game->map[y][x].power = 0;
            game->map[y][x].water = 0;
            if (do_pollution) {
                game->map[y][x].pollution = 0;
                game->map[y][x].crime = 0;
            }
        }
    }

    /* --- Power: BFS through power lines from power plants --- */
    head = 0;
    tail = 0;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type == TILE_GRASS) continue;
            if (game->map[y][x].type == TILE_POWER_PLANT &&
                game->map[y][x].development == 0) {
                /* Mark all 9 plant tiles powered */
                for (r = 0; r < 3; r++) {
                    for (c = 0; c < 3; c++) {
                        if (y + r < MAP_HEIGHT && x + c < MAP_WIDTH)
                            game->map[y + r][x + c].power = 1;
                    }
                }
                /* Energise adjacent tiles on the perimeter */
                for (r = -1; r <= 3; r++) {
                    for (c = -1; c <= 3; c++) {
                        if (r >= 0 && r <= 2 && c >= 0 && c <= 2)
                            continue;  /* skip interior */
                        ny = y + r;
                        nx = x + c;
                        if (ny >= MAP_HEIGHT || nx >= MAP_WIDTH)
                            continue;
                        t = game->map[ny][nx].type;
                        if (t == TILE_POWER_LINE && !game->map[ny][nx].power &&
                            tail < 4096) {
                            game->map[ny][nx].power = 1;
                            queue[tail++] = (unsigned int)ny * MAP_WIDTH + nx;
                        } else if (IS_3X3(t) && !game->map[ny][nx].power) {
                            if (find_zone_anchor(game, nx, ny, &ax, &ay)) {
                                int zr, zc;
                                for (zr = 0; zr < 3; zr++)
                                    for (zc = 0; zc < 3; zc++)
                                        if (ay + zr < MAP_HEIGHT && ax + zc < MAP_WIDTH)
                                            game->map[ay + zr][ax + zc].power = 1;
                            }
                        } else if (t != TILE_POWER_PLANT && t != TILE_GRASS &&
                                   t != TILE_ROAD && t != TILE_RAIL &&
                                   t != TILE_WATER && !game->map[ny][nx].power) {
                            game->map[ny][nx].power = 1;
                        }
                    }
                }
            }
        }
    }

    /* BFS through power lines AND conducting zones/buildings.
     * When we reach a power line, enqueue it.
     * When we reach a zone tile, power the whole 3x3 zone and enqueue
     * the anchor so the BFS continues from the zone's perimeter. */
    while (head < tail) {
        unsigned char qt;
        qy = (unsigned short)(queue[head] / MAP_WIDTH);
        qx = (unsigned short)(queue[head] % MAP_WIDTH);
        head++;
        qt = game->map[qy][qx].type;

        if (qt == TILE_POWER_LINE) {
            /* Power line: check 4 cardinal neighbors */
            for (dx = -1; dx <= 1; dx++) {
                for (dy = -1; dy <= 1; dy++) {
                    if ((dx == 0) == (dy == 0)) continue;
                    nx = qx + dx;
                    ny = qy + dy;
                    if (nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
                    if (game->map[ny][nx].power) continue;
                    if (tail >= 4096) continue;
                    t = game->map[ny][nx].type;
                    if (t == TILE_POWER_LINE) {
                        game->map[ny][nx].power = 1;
                        queue[tail++] = (unsigned int)ny * MAP_WIDTH + nx;
                    } else if (IS_3X3(t)) {
                        if (find_zone_anchor(game, nx, ny, &ax, &ay) &&
                            !game->map[ay][ax].power) {
                            for (r = 0; r < 3; r++)
                                for (c = 0; c < 3; c++)
                                    if (ay+r < MAP_HEIGHT && ax+c < MAP_WIDTH)
                                        game->map[ay+r][ax+c].power = 1;
                            /* Enqueue anchor — BFS will probe zone perimeter */
                            queue[tail++] = (unsigned int)ay * MAP_WIDTH + ax;
                        }
                    } else if (t != TILE_GRASS && t != TILE_ROAD &&
                               t != TILE_RAIL && t != TILE_WATER &&
                               t != TILE_POWER_PLANT) {
                        game->map[ny][nx].power = 1;
                    }
                }
            }
        } else if (IS_3X3(qt)) {
            /* Zone anchor: check the 12 perimeter cells around 3x3 */
            int pr, pc;
            for (pr = -1; pr <= 3; pr++) {
                for (pc = -1; pc <= 3; pc++) {
                    if (pr >= 0 && pr <= 2 && pc >= 0 && pc <= 2) continue;
                    nx = qx + pc;
                    ny = qy + pr;
                    if (nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
                    if (game->map[ny][nx].power) continue;
                    if (tail >= 4096) continue;
                    t = game->map[ny][nx].type;
                    if (t == TILE_POWER_LINE) {
                        game->map[ny][nx].power = 1;
                        queue[tail++] = (unsigned int)ny * MAP_WIDTH + nx;
                    } else if (IS_3X3(t)) {
                        if (find_zone_anchor(game, nx, ny, &ax, &ay) &&
                            !game->map[ay][ax].power) {
                            for (r = 0; r < 3; r++)
                                for (c = 0; c < 3; c++)
                                    if (ay+r < MAP_HEIGHT && ax+c < MAP_WIDTH)
                                        game->map[ay+r][ax+c].power = 1;
                            queue[tail++] = (unsigned int)ay * MAP_WIDTH + ax;
                        }
                    } else if (t != TILE_GRASS && t != TILE_ROAD &&
                               t != TILE_RAIL && t != TILE_WATER &&
                               t != TILE_POWER_PLANT) {
                        game->map[ny][nx].power = 1;
                    }
                }
            }
        }
    }

    /* --- Water: radius-based from water pumps --- */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type != TILE_WATER_PUMP) continue;
            if (game->map[y][x].type == TILE_WATER_PUMP) {
                for (dy = -5; dy <= 5; dy++) {
                    for (dx = -5; dx <= 5; dx++) {
                        if (y + dy >= 0 && y + dy < MAP_HEIGHT &&
                            x + dx >= 0 && x + dx < MAP_WIDTH) {
                            game->map[y + dy][x + dx].water = 1;
                        }
                    }
                }
            }
        }
    }

    /* --- Pollution: only recalculate every 6 hours --- */
    if (do_pollution) {
        /* Pollution was already reset to 0 in the combined reset loop above */
        for (y = 0; y < MAP_HEIGHT; y++) {
            for (x = 0; x < MAP_WIDTH; x++) {
                int base = 0, radius = 0;
                t = game->map[y][x].type;
                if (t == TILE_GRASS) continue;

                if (t == TILE_INDUSTRIAL && game->map[y][x].development == 0 &&
                    game->map[y][x].population > 0) {
                    /* Scale by density: low=40, med=60, high=80 */
                    base = 40 + game->map[y][x].density * 20;
                    radius = 4 + game->map[y][x].density * 2;
                } else if (t == TILE_POWER_PLANT && game->map[y][x].development == 0) {
                    base = 60; radius = 10;
                } else if (t == TILE_ROAD) {
                    base = 10; radius = 2;
                } else if ((t == TILE_RESIDENTIAL || t == TILE_COMMERCIAL) &&
                           game->map[y][x].development == 0 &&
                           game->map[y][x].population > 0) {
                    base = 3; radius = 1;
                }

                if (base > 0) {
                    for (dy = -radius; dy <= radius; dy++) {
                        for (dx = -radius; dx <= radius; dx++) {
                            int ny2 = y + dy, nx2 = x + dx;
                            int dist, contrib;
                            unsigned int val;
                            if (ny2 < 0 || ny2 >= MAP_HEIGHT || nx2 < 0 || nx2 >= MAP_WIDTH)
                                continue;
                            dist = abs(dx) + abs(dy);
                            if (dist > radius) continue;
                            contrib = base - (base * dist / radius);
                            val = (unsigned int)game->map[ny2][nx2].pollution + contrib;
                            game->map[ny2][nx2].pollution = (val > 255) ? 255 : (unsigned char)val;
                        }
                    }
                }
            }
        }

        /* Crime: base crime from population density, reduced by police */
        for (y = 0; y < MAP_HEIGHT; y++) {
            for (x = 0; x < MAP_WIDTH; x++) {
                int base_crime = 0;
                t = game->map[y][x].type;
                if ((t == TILE_RESIDENTIAL || t == TILE_COMMERCIAL) &&
                    game->map[y][x].development == 0 &&
                    game->map[y][x].population > 0) {
                    base_crime = 30 + game->map[y][x].density * 20;
                } else if (t == TILE_INDUSTRIAL &&
                           game->map[y][x].development == 0) {
                    base_crime = 50;
                }
                if (base_crime > 0) {
                    for (dy = -2; dy <= 2; dy++) {
                        for (dx = -2; dx <= 2; dx++) {
                            int ny2 = y + dy, nx2 = x + dx;
                            int dist, contrib;
                            unsigned int val;
                            if (ny2 < 0 || ny2 >= MAP_HEIGHT || nx2 < 0 || nx2 >= MAP_WIDTH)
                                continue;
                            dist = abs(dx) + abs(dy);
                            if (dist > 2) continue;
                            contrib = base_crime - (base_crime * dist / 2);
                            val = (unsigned int)game->map[ny2][nx2].crime + contrib;
                            game->map[ny2][nx2].crime = (val > 255) ? 255 : (unsigned char)val;
                        }
                    }
                }
            }
        }

        /* Police stations reduce crime in radius 12 */
        for (y = 0; y < MAP_HEIGHT; y++) {
            for (x = 0; x < MAP_WIDTH; x++) {
                if (game->map[y][x].type != TILE_POLICE ||
                    game->map[y][x].development != 0) continue;
                if (!game->map[y][x].power) continue; /* must be powered */
                for (dy = -12; dy <= 12; dy++) {
                    for (dx = -12; dx <= 12; dx++) {
                        int ny2 = y + dy, nx2 = x + dx;
                        int dist, reduction;
                        if (ny2 < 0 || ny2 >= MAP_HEIGHT || nx2 < 0 || nx2 >= MAP_WIDTH)
                            continue;
                        dist = abs(dx) + abs(dy);
                        if (dist > 12) continue;
                        reduction = 40 - (40 * dist / 12);
                        if (game->map[ny2][nx2].crime > reduction)
                            game->map[ny2][nx2].crime -= (unsigned char)reduction;
                        else
                            game->map[ny2][nx2].crime = 0;
                    }
                }
            }
        }

        /* Parks and woods reduce pollution */
        for (y = 0; y < MAP_HEIGHT; y++) {
            for (x = 0; x < MAP_WIDTH; x++) {
                if (game->map[y][x].type != TILE_PARK &&
                    game->map[y][x].type != TILE_WOODS) continue;
                for (dy = -4; dy <= 4; dy++) {
                    for (dx = -4; dx <= 4; dx++) {
                        int ny2 = y + dy, nx2 = x + dx;
                        int dist, reduction;
                        if (ny2 < 0 || ny2 >= MAP_HEIGHT || nx2 < 0 || nx2 >= MAP_WIDTH)
                            continue;
                        dist = abs(dx) + abs(dy);
                        if (dist > 4) continue;
                        reduction = 20 - (20 * dist / 4);
                        if (game->map[ny2][nx2].pollution > reduction)
                            game->map[ny2][nx2].pollution -= (unsigned char)reduction;
                        else
                            game->map[ny2][nx2].pollution = 0;
                    }
                }
            }
        }
    }
}

/*
 * Micropolis-inspired tax table: index = taxEffect + gameLevel.
 * At 7% tax (default), taxEffect=7; easy=0, so index=7 → 0 (neutral).
 * Higher taxes push demand negative; lower taxes give a bonus.
 */
static const int tax_table[] = {
    200, 150, 120, 100, 80, 50, 30, 0, -10, -40, -100,
    -150, -200, -250, -300, -350, -400, -450, -500, -550, -600
};

/*
 * Check if a zone at (x,y) has road access within 3 tiles of its
 * perimeter.  Returns 1 if road found, 0 otherwise.  Acts as a
 * simplified traffic check (Micropolis uses full pathfinding).
 */
static int zone_has_road_access(GameState *game, int x, int y) {
    int r, c;
    /* Check perimeter one tile out from 3x3 zone */
    for (r = -1; r <= 3; r++) {
        for (c = -1; c <= 3; c++) {
            int ny, nx;
            if (r >= 0 && r <= 2 && c >= 0 && c <= 2)
                continue;  /* skip interior */
            ny = y + r; nx = x + c;
            if (ny >= 0 && ny < MAP_HEIGHT && nx >= 0 && nx < MAP_WIDTH) {
                if (game->map[ny][nx].type == TILE_ROAD)
                    return 1;
            }
        }
    }
    return 0;
}

/*
 * Remove one populated sub-tile from a 3x3 zone at anchor (x,y).
 * Decrements game->population for residential zones.
 * Removes associated humans if residential.
 */
static void zone_depop_one(GameState *game, int x, int y) {
    int r, c;
    unsigned char ztype = game->map[y][x].type;
    for (r = 2; r >= 0; r--) {
        for (c = 2; c >= 0; c--) {
            if (y + r < MAP_HEIGHT && x + c < MAP_WIDTH &&
                game->map[y+r][x+c].population) {
                game->map[y+r][x+c].population = 0;
                if (ztype == TILE_RESIDENTIAL) {
                    if (game->population >= 111)
                        game->population -= 111;
                    else
                        game->population = 0;
                }
                return;
            }
        }
    }
}

/*
 * Add one populated sub-tile to a 3x3 zone at anchor (x,y).
 * Increments game->population for residential zones.
 * Spawns a human if first pop in zone.
 */
static void zone_grow_one(GameState *game, int x, int y, int pop_count) {
    int r, c;
    int target;
    unsigned char ztype = game->map[y][x].type;

    if (pop_count >= 9) return;
    target = (int)rand_range(0, 8 - pop_count);

    for (r = 0; r < 3; r++) {
        for (c = 0; c < 3; c++) {
            if (y + r < MAP_HEIGHT && x + c < MAP_WIDTH &&
                !game->map[y+r][x+c].population) {
                if (target == 0) {
                    game->map[y+r][x+c].population = 1;
                    if (ztype == TILE_RESIDENTIAL) {
                        game->population += (pop_count == 8) ? 112 : 111;
                        if (pop_count == 0 && game->num_humans < MAX_HUMANS)
                            spawn_human(game, (unsigned short)(x + 1),
                                        (unsigned short)(y + 1));
                    }
                    return;
                }
                target--;
            }
        }
    }
}

/*
 * Fully depopulate a zone: clear all sub-tiles, adjust population,
 * and remove associated humans.
 */
static void zone_depop_all(GameState *game, int x, int y) {
    int r, c, pop_count = 0;
    unsigned int i;
    unsigned char ztype = game->map[y][x].type;

    if (y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++)
                if (game->map[y+r][x+c].population)
                    pop_count++;
    }
    for (r = 0; r < 3; r++)
        for (c = 0; c < 3; c++)
            if (y + r < MAP_HEIGHT && x + c < MAP_WIDTH)
                game->map[y+r][x+c].population = 0;

    if (ztype == TILE_RESIDENTIAL) {
        unsigned int zone_pop = (unsigned int)pop_count * 111;
        if (pop_count == 9) zone_pop = 1000;
        if (game->population >= zone_pop)
            game->population -= zone_pop;
        else
            game->population = 0;
    }

    /* Remove humans whose home is within this 3x3 block */
    i = 0;
    while (i < game->num_humans) {
        if (game->humans[i].home_x >= (unsigned short)x &&
            game->humans[i].home_x < (unsigned short)(x + 3) &&
            game->humans[i].home_y >= (unsigned short)y &&
            game->humans[i].home_y < (unsigned short)(y + 3)) {
            game->num_humans--;
            if (i < game->num_humans)
                game->humans[i] = game->humans[game->num_humans];
        } else {
            i++;
        }
    }
}

void update_buildings(GameState* game) {
    int x, y;
    int tax_idx;

    /* --- Count zone populations --- */
    game->r_pop = 0;
    game->c_pop = 0;
    game->i_pop = 0;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type == TILE_GRASS) continue;
            if (game->map[y][x].development == 0 &&
                game->map[y][x].population > 0) {
                int r, c;
                unsigned int cnt = 0;
                if (y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
                    for (r = 0; r < 3; r++)
                        for (c = 0; c < 3; c++)
                            if (game->map[y+r][x+c].population)
                                cnt++;
                }
                if (game->map[y][x].type == TILE_RESIDENTIAL)
                    game->r_pop += cnt;
                else if (game->map[y][x].type == TILE_COMMERCIAL)
                    game->c_pop += cnt;
                else if (game->map[y][x].type == TILE_INDUSTRIAL)
                    game->i_pop += cnt;
            }
        }
    }

    /* --- Micropolis-style valve computation ---
     *
     * employment = (c_pop + i_pop) / r_pop — how many jobs per resident
     * migration  = r_pop * (employment - 1) — positive if jobs > residents
     * births     = r_pop * 0.02
     * projectedRes = r_pop + migration + births
     *
     * laborBase  = r_pop / (c_pop + i_pop) — labour supply, clamped 0-1.3
     * internalMarket = (r_pop + c_pop + i_pop) / 3.7
     * projectedCom = internalMarket * laborBase
     * projectedInd = i_pop * laborBase * difficultyMod
     *
     * ratio = projected / actual, clamped to 2.0
     * valveChange = (ratio - 1) * 600 + taxTable[taxEffect + gameLevel]
     * valve += valveChange, clamped to [-2000, +2000] (res) or [-1500, +1500]
     */
    {
        static const int diff_ind_x10[] = { 12, 11, 10 }; /* Easy=1.2, Med=1.1, Hard=1.0 */
        long norm_r = (long)game->r_pop;
        long c = (long)game->c_pop;
        long ind = (long)game->i_pop;
        long total_jobs = c + ind;
        long employment_x100;  /* employment × 100 (fixed-point) */
        long migration_x100;
        long births_x100;
        long proj_r_x100;
        long labor_x100;  /* laborBase × 100 */
        long proj_c_x100;
        long proj_i_x100;
        long res_ratio_x100, com_ratio_x100, ind_ratio_x100;
        int tax_bonus;

        /* Employment ratio × 100 */
        if (norm_r > 0)
            employment_x100 = total_jobs * 100 / norm_r;
        else
            employment_x100 = 100;

        /* Migration and births */
        migration_x100 = norm_r * (employment_x100 - 100) / 100;
        births_x100 = norm_r * 2;  /* 0.02 × 100 = 2 */
        proj_r_x100 = norm_r * 100 + migration_x100 * 100 + births_x100;

        /* Labor base × 100 */
        if (total_jobs > 0)
            labor_x100 = norm_r * 100 / total_jobs;
        else
            labor_x100 = 100;
        if (labor_x100 > 130) labor_x100 = 130;
        if (labor_x100 < 0) labor_x100 = 0;

        /* Commercial demand driven by residential population.
         * Residents need shops: target ~1 commercial per 3 residential.
         * proj_c = r_pop / 3, scaled by labor availability. */
        proj_c_x100 = norm_r * 100 / 3;
        if (labor_x100 < 100)
            proj_c_x100 = proj_c_x100 * labor_x100 / 100;

        /* Projected industrial × 100 */
        proj_i_x100 = ind * labor_x100 * diff_ind_x10[game->difficulty < 3 ? game->difficulty : 1] / 1000;
        if (proj_i_x100 < 500) proj_i_x100 = 500;  /* floor of 5.0 */

        /* Ratios × 100, clamped to 200 */
        if (norm_r > 0)
            res_ratio_x100 = proj_r_x100 / norm_r;
        else
            res_ratio_x100 = 130;
        if (res_ratio_x100 > 200) res_ratio_x100 = 200;

        if (c > 0)
            com_ratio_x100 = proj_c_x100 / c;
        else
            com_ratio_x100 = proj_c_x100;  /* bootstrap */
        if (com_ratio_x100 > 200) com_ratio_x100 = 200;

        if (ind > 0)
            ind_ratio_x100 = proj_i_x100 / ind;
        else
            ind_ratio_x100 = proj_i_x100;
        if (ind_ratio_x100 > 200) ind_ratio_x100 = 200;

        /* Tax effect: index = taxRate + difficulty */
        tax_idx = (int)game->city_tax + (int)game->difficulty;
        if (tax_idx > 20) tax_idx = 20;
        if (tax_idx < 0) tax_idx = 0;
        tax_bonus = tax_table[tax_idx];

        /* Valve: blend toward target rather than accumulating deltas.
         * target = (ratio - 1.0) × 600 + tax_bonus, then move valve
         * 25% of the way toward target each evaluation (smooth, no oscillation). */
        {
            int res_target = (int)((res_ratio_x100 - 100) * 6) + tax_bonus;
            int com_target = (int)((com_ratio_x100 - 100) * 6) + tax_bonus;
            int ind_target = (int)((ind_ratio_x100 - 100) * 6) + tax_bonus;

            /* Clamp targets */
            if (res_target > 2000) res_target = 2000;
            if (res_target < -2000) res_target = -2000;
            if (com_target > 1500) com_target = 1500;
            if (com_target < -1500) com_target = -1500;
            if (ind_target > 1500) ind_target = 1500;
            if (ind_target < -1500) ind_target = -1500;

            game->rci_demand[0] += (res_target - game->rci_demand[0]) / 4;
            game->rci_demand[1] += (com_target - game->rci_demand[1]) / 4;
            game->rci_demand[2] += (ind_target - game->rci_demand[2]) / 4;
        }

        /* Bootstrap: if very few zones, give residential a push */
        if (game->r_pop < 9 && game->c_pop == 0 && game->i_pop == 0) {
            if (game->rci_demand[0] < 300)
                game->rci_demand[0] = 300;
        }
        /* Bootstrap commercial/industrial once residential exists */
        if (game->r_pop > 0 && game->c_pop == 0 && game->rci_demand[1] < 200)
            game->rci_demand[1] = 200;
        if (game->r_pop > 0 && game->i_pop == 0 && game->rci_demand[2] < 200)
            game->rci_demand[2] = 200;
    }

    /* --- Per-zone growth and decay ---
     *
     * Inspired by Micropolis: each zone anchor is processed with a 1-in-8
     * random chance each tick.  A zone score (zscore) combines the global
     * valve with a local evaluation:
     *   Residential: penalty for pollution (landValue - pollution, ×32, cap 6000, -3000)
     *   Commercial:  bonus for low pollution
     *   Industrial:  always 0 (industry doesn't care about land value)
     *
     * No power → zscore = -500
     * No road access → additional -3000 penalty (traffic failure)
     *
     * Growth:  zscore > -350 AND random check proportional to zscore
     * Decay:   zscore < 350 AND random check proportional to -zscore
     */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            unsigned char ztype = game->map[y][x].type;
            int zscore, local_eval, pop_count, has_road;
            int r, c, valve_idx;

            if (ztype == TILE_GRASS) continue;
            if (ztype != TILE_RESIDENTIAL && ztype != TILE_COMMERCIAL &&
                ztype != TILE_INDUSTRIAL) continue;
            if (game->map[y][x].development != 0) continue;
            if (y + 2 >= MAP_HEIGHT || x + 2 >= MAP_WIDTH) continue;

            /* Process 1 in 4 ticks (Micropolis uses 1-in-8 but runs faster) */
            if (rand_range(0, 3) != 0) continue;

            /* Count populated sub-tiles */
            pop_count = 0;
            for (r = 0; r < 3; r++)
                for (c = 0; c < 3; c++)
                    if (game->map[y+r][x+c].population)
                        pop_count++;

            /* Which valve? */
            if (ztype == TILE_RESIDENTIAL) valve_idx = 0;
            else if (ztype == TILE_COMMERCIAL) valve_idx = 1;
            else valve_idx = 2;

            /* Local evaluation */
            has_road = zone_has_road_access(game, x, y);

            if (ztype == TILE_RESIDENTIAL) {
                /* Land value: base 100, +30 per density level, minus pollution.
                 * Scaled ×32 and capped at 6000, then centred around 0.
                 * At density 0 with no pollution: (100)*32=3200 → local_eval=200
                 * This allows early-game growth while rewarding higher density. */
                int lv = 100 + (int)game->map[y][x].density * 30;
                int pv = lv - (int)game->map[y][x].pollution;
                if (pv < 0) pv = 0;
                pv = pv * 32;
                if (pv > 6000) pv = 6000;
                local_eval = pv - 3000;
                if (!has_road) local_eval = -3000;
            } else if (ztype == TILE_COMMERCIAL) {
                /* Commercial benefits from low pollution */
                local_eval = 1500 - (int)game->map[y][x].pollution * 12;
                if (!has_road) local_eval = -3000;
            } else {
                /* Industrial: always 0 unless no road */
                local_eval = 0;
                if (!has_road) local_eval = -1000;
            }

            zscore = game->rci_demand[valve_idx] + local_eval;

            /* No power → strong negative */
            if (!game->map[y][x].power)
                zscore = -500;

            /* Pollution blocks residential growth entirely if very high */
            if (ztype == TILE_RESIDENTIAL &&
                game->map[y][x].pollution > 128 && zscore > 0)
                zscore = 0;

            /* --- Growth or decay (mutually exclusive) --- */
            if (zscore >= 0 && pop_count < 9) {
                /* Positive demand: grow */
                int grow_chance = 5 + zscore / 150;
                if (grow_chance > 60) grow_chance = 60;
                if (rand_range(0, 99) < (unsigned)grow_chance)
                    zone_grow_one(game, x, y, pop_count);
            } else if (zscore < -100 && pop_count > 0) {
                /* Significantly negative: decay */
                int decay_chance = 1 + (-zscore - 100) / 300;
                if (decay_chance > 30) decay_chance = 30;
                if (rand_range(0, 99) < (unsigned)decay_chance)
                    zone_depop_one(game, x, y);
            }

            /* Full depopulation only for severely negative (no power, no road) */
            if (zscore <= -500 && pop_count > 0 && rand_range(0, 30) == 0)
                zone_depop_all(game, x, y);
        }
    }
}

/*
 * Return the year number for a given game_day (day 0 = 1 Jan 1900).
 */
static unsigned int year_from_day(unsigned int game_day) {
    unsigned int year = 1900;
    unsigned int days_left = game_day;
    int leap, yd;
    while (1) {
        leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        yd = 365 + leap;
        if (days_left < (unsigned int)yd) break;
        days_left -= yd;
        year++;
    }
    return year;
}

/*
 * Count service buildings of a given type on the map.
 */
static unsigned int count_buildings_of_type(GameState *game, unsigned char btype) {
    int sy, sx;
    unsigned int count = 0;
    for (sy = 0; sy < MAP_HEIGHT; sy++) {
        for (sx = 0; sx < MAP_WIDTH; sx++) {
            if (game->map[sy][sx].type == btype &&
                game->map[sy][sx].development == 0)
                count++;
        }
    }
    return count;
}

/*
 * Count road + rail tiles on the map (for maintenance cost).
 */
static unsigned int count_roads(GameState *game) {
    int sy, sx;
    unsigned int count = 0;
    for (sy = 0; sy < MAP_HEIGHT; sy++) {
        for (sx = 0; sx < MAP_WIDTH; sx++) {
            unsigned char t = game->map[sy][sx].type;
            if (t == TILE_ROAD || t == TILE_RAIL)
                count++;
        }
    }
    return count;
}

/*
 * Process year-end: calculate annual costs, deduct from funds,
 * set pending flags for budget/newspaper popups.
 */
static void process_year_end(GameState *game) {
    static const int flevel_x10[] = { 14, 12, 8 };
    unsigned int total_pop = game->population;
    unsigned int zone_count;
    unsigned int lv_avg;
    int fl;
    long annual_tax;
    unsigned int n_police, n_fire, n_hospital, n_school, n_roads;

    /* Compute annual tax revenue (365x daily rate) */
    zone_count = game->r_pop + game->c_pop + game->i_pop;
    if (zone_count > 0) {
        unsigned int density_sum = 0;
        int sy, sx;
        unsigned int zt = 0;
        for (sy = 0; sy < MAP_HEIGHT; sy++) {
            for (sx = 0; sx < MAP_WIDTH; sx++) {
                unsigned char t = game->map[sy][sx].type;
                if ((t == TILE_RESIDENTIAL || t == TILE_COMMERCIAL ||
                     t == TILE_INDUSTRIAL) &&
                    game->map[sy][sx].population > 0) {
                    density_sum += game->map[sy][sx].density;
                    zt++;
                }
            }
        }
        if (zt > 0)
            lv_avg = 5 + (density_sum * 10) / (zt * 2);
        else
            lv_avg = 5;
    } else {
        lv_avg = 5;
    }
    fl = flevel_x10[game->difficulty < 3 ? game->difficulty : 1];
    annual_tax = ((long)total_pop * lv_avg / 120) * game->city_tax * fl / 10;
    annual_tax *= 365;
    if (annual_tax < 0) annual_tax = 0;
    game->year_revenue = annual_tax;

    /* Count service buildings (only anchor tiles, development==0) */
    n_police = count_buildings_of_type(game, TILE_POLICE);
    n_fire = count_buildings_of_type(game, TILE_FIRE);
    n_hospital = count_buildings_of_type(game, TILE_HOSPITAL);
    n_school = count_buildings_of_type(game, TILE_SCHOOL);
    n_roads = count_roads(game);

    /* Annual costs: base cost * count * funding% / 100 */
    game->year_police_cost = (long)n_police * 100L * game->fund_police / 100;
    game->year_fire_cost = (long)n_fire * 100L * game->fund_fire / 100;
    game->year_health_cost = (long)n_hospital * 150L * game->fund_health / 100;
    game->year_education_cost = (long)n_school * 120L * game->fund_education / 100;
    game->year_road_cost = (long)n_roads;  /* $1 per road/rail tile per year */

    /* Apply to funds: revenue already collected daily, only deduct service costs */
    game->funds -= game->year_police_cost;
    game->funds -= game->year_fire_cost;
    game->funds -= game->year_health_cost;
    game->funds -= game->year_education_cost;
    game->funds -= game->year_road_cost;

    /* Set pending flags */
    if (!game->auto_budget)
        game->pending_budget = 1;
    if (game->show_newspaper)
        game->pending_newspaper = 1;
}

void update_game(GameState* game) {
    static unsigned int tick_counter = 0;
    
    tick_counter++;
    
    /* Update time */
    if (tick_counter >= TICKS_PER_HOUR) {
        tick_counter = 0;
        game->game_time++;
        
        if (game->game_time >= 24) {
            game->game_time = 0;
            game->game_day++;

            /* Year-end processing: check if we crossed into a new year */
            {
                unsigned int cur_year = year_from_day(game->game_day);
                unsigned int prev_year = year_from_day(game->game_day - 1);
                if (cur_year != prev_year && game->game_day > 0) {
                    process_year_end(game);
                }
            }
        }

        /* Update game systems every hour */
        update_tile_services(game);
        update_humans(game);
        update_buildings(game);
        
        /* Collect taxes every 24 hours (Micropolis-style formula) */
        if (game->game_time == 0) {
            /* TaxFund = (TotalPop * LVAvg / 120) * CityTax * FLevel
             * LVAvg approximated from average zone density (0-2 mapped to 5-15).
             * FLevel: Easy=1.4, Medium=1.2, Hard=0.8 — use fixed-point x10. */
            static const int flevel_x10[] = { 14, 12, 8 };
            unsigned int total_pop = game->population;
            unsigned int zone_count = game->r_pop + game->c_pop + game->i_pop;
            unsigned int lv_avg;
            long tax_fund;
            int fl;

            if (zone_count > 0) {
                /* Compute average density across populated zone tiles (0-2),
                 * then scale to a land value range roughly 5-15 */
                unsigned int density_sum = 0;
                int sy, sx;
                unsigned int zt = 0;
                for (sy = 0; sy < MAP_HEIGHT; sy++) {
                    for (sx = 0; sx < MAP_WIDTH; sx++) {
                        unsigned char t = game->map[sy][sx].type;
                        if ((t == TILE_RESIDENTIAL || t == TILE_COMMERCIAL ||
                             t == TILE_INDUSTRIAL) &&
                            game->map[sy][sx].population > 0) {
                            density_sum += game->map[sy][sx].density;
                            zt++;
                        }
                    }
                }
                /* avg density 0-2 -> lv_avg 5-15 */
                if (zt > 0)
                    lv_avg = 5 + (density_sum * 10) / (zt * 2);
                else
                    lv_avg = 5;
            } else {
                lv_avg = 5;
            }

            fl = flevel_x10[game->difficulty < 3 ? game->difficulty : 1];
            /* (pop * lv / 120) * tax * flevel/10 */
            tax_fund = ((long)total_pop * lv_avg / 120) * game->city_tax * fl / 10;
            if (tax_fund < 0) tax_fund = 0;
            game->funds += tax_fund;
        }
    }
}

static void clear_tile(GameState *game, unsigned short x, unsigned short y) {
    game->map[y][x].type = TILE_GRASS;
    game->map[y][x].population = 0;
    game->map[y][x].power = 0;
    game->map[y][x].water = 0;
    game->map[y][x].development = 0;
    game->map[y][x].pollution = 0;
    game->map[y][x].crime = 0;
    game->map[y][x].density = 0;
}

void bulldoze_tile(GameState* game, unsigned short x, unsigned short y) {
    unsigned char type;
    int cost = 1;  /* base bulldoze cost per tile */

    if (x >= MAP_WIDTH || y >= MAP_HEIGHT)
        return;

    type = game->map[y][x].type;
    if (type == TILE_GRASS || type == TILE_DIRT)
        return;  /* nothing to bulldoze */
    if (type == TILE_RIVER)
        return;  /* rivers can't be bulldozed (bridges later) */

    /* 6x6 airport: find top-left from development index and clear all 36 tiles */
    if (type == TILE_AIRPORT) {
        unsigned char dev = game->map[y][x].development;
        unsigned short ax = x - (dev % 6);
        unsigned short ay = y - (dev / 6);
        int r, c;
        if (game->funds < cost) return;
        game->funds -= cost;
        for (r = 0; r < 6; r++)
            for (c = 0; c < 6; c++)
                if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                    clear_tile(game, ax + c, ay + r);
        return;
    }

    /* 3x3 zone or building: find anchor and clear all 9 tiles */
    if (type == TILE_RESIDENTIAL || type == TILE_COMMERCIAL ||
        type == TILE_INDUSTRIAL || type == TILE_POWER_PLANT ||
        type == TILE_POLICE || type == TILE_FIRE || type == TILE_HOSPITAL) {
        unsigned short ax, ay;
        int r, c;
        if (!find_zone_anchor(game, x, y, &ax, &ay))
            return;
        if (game->funds < cost)
            return;
        game->funds -= cost;
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++)
                if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                    clear_tile(game, ax + c, ay + r);
        return;
    }

    /* Power line with road/rail underneath: restore the underlying tile */
    if (type == TILE_POWER_LINE) {
        unsigned char under = game->map[y][x].development;
        if (game->funds < cost)
            return;
        game->funds -= cost;
        if (under == TILE_ROAD || under == TILE_RAIL) {
            game->map[y][x].type = under;
            game->map[y][x].development = 0;
            game->map[y][x].power = 0;
        } else {
            clear_tile(game, x, y);
        }
        return;
    }

    /* 1x1 tile (road, rail, park, etc.) */
    if (game->funds < cost)
        return;
    game->funds -= cost;
    clear_tile(game, x, y);
}

void place_tile(GameState* game, unsigned short x, unsigned short y, unsigned char type) {
    int cost;

    if (x >= MAP_WIDTH || y >= MAP_HEIGHT)
        return;

    /* 6x6 airport */
    if (type == TILE_AIRPORT) {
        int r, c, half = 3;
        unsigned short bx, by;
        if (x < half || y < half || x + half >= MAP_WIDTH || y + half >= MAP_HEIGHT)
            return;
        for (r = -half; r < half; r++) {
            for (c = -half; c < half; c++) {
                unsigned char ct;
                by = y + r; bx = x + c;
                ct = game->map[by][bx].type;
                if (ct != TILE_GRASS && ct != TILE_WOODS && ct != TILE_DIRT)
                    return;
            }
        }
        cost = get_tile_cost(type);
        if (game->funds < cost) return;
        game->funds -= cost;
        for (r = 0; r < 6; r++) {
            for (c = 0; c < 6; c++) {
                by = y - half + r; bx = x - half + c;
                game->map[by][bx].type = type;
                game->map[by][bx].development = (unsigned char)(r * 6 + c);
            }
        }
        return;
    }

    /* 3x3 zone/building types */
    if (type == TILE_RESIDENTIAL || type == TILE_COMMERCIAL ||
        type == TILE_INDUSTRIAL || type == TILE_POWER_PLANT ||
        type == TILE_POLICE || type == TILE_FIRE || type == TILE_HOSPITAL) {
        int r, c;
        unsigned short bx, by;

        /* Block is centered on cursor: top-left = (x-1, y-1) */
        if (x < 1 || y < 1 || x + 1 >= MAP_WIDTH || y + 1 >= MAP_HEIGHT)
            return;

        /* Check all 9 cells are empty terrain (grass, woods, dirt) */
        for (r = -1; r <= 1; r++) {
            for (c = -1; c <= 1; c++) {
                unsigned char ct;
                by = y + r;
                bx = x + c;
                ct = game->map[by][bx].type;
                if (ct != TILE_GRASS && ct != TILE_WOODS && ct != TILE_DIRT)
                    return;
            }
        }

        cost = get_tile_cost(type);
        if (game->funds < cost)
            return;
        game->funds -= cost;

        /* Place all 9 tiles; development = sub-position (row*3 + col) */
        for (r = 0; r < 3; r++) {
            for (c = 0; c < 3; c++) {
                by = y - 1 + r;
                bx = x - 1 + c;
                game->map[by][bx].type = type;
                game->map[by][bx].development = (unsigned char)(r * 3 + c);
                game->map[by][bx].density = game->current_density;
            }
        }
        return;
    }

    /* Power line: can overlay grass, straight roads, and rail */
    if (type == TILE_POWER_LINE) {
        unsigned char existing = game->map[y][x].type;

        if (existing == TILE_ROAD) {
            unsigned char mask = neighbor_mask(game, x, y, TILE_ROAD);
            if (road_table[mask].art == 2)
                return;  /* reject turns */
        } else if (existing != TILE_GRASS && existing != TILE_RAIL &&
                   existing != TILE_WOODS && existing != TILE_DIRT) {
            return;  /* reject everything else */
        }

        cost = get_tile_cost(type);
        if (game->funds < cost)
            return;
        game->funds -= cost;
        game->map[y][x].type = TILE_POWER_LINE;
        game->map[y][x].development = (existing == TILE_GRASS ||
            existing == TILE_WOODS || existing == TILE_DIRT) ? 0 : existing;
        return;
    }

    /* Road or rail on existing power line: composite (power line stays) */
    if ((type == TILE_ROAD || type == TILE_RAIL) &&
        game->map[y][x].type == TILE_POWER_LINE &&
        game->map[y][x].development == 0) {
        cost = get_tile_cost(type);
        if (game->funds < cost)
            return;
        game->funds -= cost;
        game->map[y][x].development = type;
        return;
    }

    /* Only build on empty terrain */
    {
        unsigned char existing = game->map[y][x].type;
        if (existing != TILE_GRASS && existing != TILE_WOODS &&
            existing != TILE_DIRT)
            return;
    }

    cost = get_tile_cost(type);

    if (game->funds < cost)
        return;

    game->funds -= cost;
    game->map[y][x].type = type;

    if (type >= TILE_POLICE && type <= TILE_WATER_PUMP) {
        if (game->num_buildings < MAX_BUILDINGS) {
            game->buildings[game->num_buildings].type = type;
            game->buildings[game->num_buildings].x = x;
            game->buildings[game->num_buildings].y = y;
            game->buildings[game->num_buildings].width = 1;
            game->buildings[game->num_buildings].height = 1;
            game->buildings[game->num_buildings].capacity = 100;
            game->buildings[game->num_buildings].occupancy = 0;
            game->buildings[game->num_buildings].condition = 100;
            game->num_buildings++;
        }
    }
}

/* ---- Save / Load ---- */

#define SAVE_MAGIC 0x4353  /* "CS" */
#define SAVE_VERSION 2

int save_game(GameState* game, const char* filename) {
    FILE *f = fopen(filename, "wb");
    unsigned short magic = SAVE_MAGIC;
    unsigned short ver = SAVE_VERSION;
    if (!f) return 0;
    fwrite(&magic, sizeof(magic), 1, f);
    fwrite(&ver, sizeof(ver), 1, f);
    fwrite(&game->population, sizeof(game->population), 1, f);
    fwrite(&game->num_buildings, sizeof(game->num_buildings), 1, f);
    fwrite(&game->num_humans, sizeof(game->num_humans), 1, f);
    fwrite(&game->funds, sizeof(game->funds), 1, f);
    fwrite(&game->game_time, sizeof(game->game_time), 1, f);
    fwrite(&game->game_day, sizeof(game->game_day), 1, f);
    fwrite(&game->cursor_x, sizeof(game->cursor_x), 1, f);
    fwrite(&game->cursor_y, sizeof(game->cursor_y), 1, f);
    fwrite(&game->scroll_x, sizeof(game->scroll_x), 1, f);
    fwrite(&game->scroll_y, sizeof(game->scroll_y), 1, f);
    fwrite(&game->current_tool, sizeof(game->current_tool), 1, f);
    fwrite(&game->current_density, sizeof(game->current_density), 1, f);
    fwrite(&game->difficulty, sizeof(game->difficulty), 1, f);
    fwrite(&game->city_tax, sizeof(game->city_tax), 1, f);
    fwrite(&game->fund_police, sizeof(game->fund_police), 1, f);
    fwrite(&game->fund_fire, sizeof(game->fund_fire), 1, f);
    fwrite(&game->fund_health, sizeof(game->fund_health), 1, f);
    fwrite(&game->fund_education, sizeof(game->fund_education), 1, f);
    fwrite(&game->auto_budget, sizeof(game->auto_budget), 1, f);
    fwrite(&game->show_newspaper, sizeof(game->show_newspaper), 1, f);
    fwrite(&game->last_year_day, sizeof(game->last_year_day), 1, f);
    fwrite(&game->play_ticks, sizeof(game->play_ticks), 1, f);
    fwrite(&game->rci_demand, sizeof(game->rci_demand), 1, f);
    fwrite(&game->r_pop, sizeof(game->r_pop), 1, f);
    fwrite(&game->c_pop, sizeof(game->c_pop), 1, f);
    fwrite(&game->i_pop, sizeof(game->i_pop), 1, f);
    fwrite(game->map, sizeof(game->map), 1, f);
    fwrite(game->humans, sizeof(Human) * game->num_humans, 1, f);
    fwrite(game->buildings, sizeof(Building) * game->num_buildings, 1, f);
    fclose(f);
    return 1;
}

int load_game(GameState* game, const char* filename) {
    FILE *f = fopen(filename, "rb");
    unsigned short magic, ver;
    if (!f) return 0;
    fread(&magic, sizeof(magic), 1, f);
    fread(&ver, sizeof(ver), 1, f);
    if (magic != SAVE_MAGIC || ver != SAVE_VERSION) {
        fclose(f);
        return 0;
    }
    fread(&game->population, sizeof(game->population), 1, f);
    fread(&game->num_buildings, sizeof(game->num_buildings), 1, f);
    fread(&game->num_humans, sizeof(game->num_humans), 1, f);
    fread(&game->funds, sizeof(game->funds), 1, f);
    fread(&game->game_time, sizeof(game->game_time), 1, f);
    fread(&game->game_day, sizeof(game->game_day), 1, f);
    fread(&game->cursor_x, sizeof(game->cursor_x), 1, f);
    fread(&game->cursor_y, sizeof(game->cursor_y), 1, f);
    fread(&game->scroll_x, sizeof(game->scroll_x), 1, f);
    fread(&game->scroll_y, sizeof(game->scroll_y), 1, f);
    fread(&game->current_tool, sizeof(game->current_tool), 1, f);
    fread(&game->current_density, sizeof(game->current_density), 1, f);
    fread(&game->difficulty, sizeof(game->difficulty), 1, f);
    fread(&game->city_tax, sizeof(game->city_tax), 1, f);
    fread(&game->fund_police, sizeof(game->fund_police), 1, f);
    fread(&game->fund_fire, sizeof(game->fund_fire), 1, f);
    fread(&game->fund_health, sizeof(game->fund_health), 1, f);
    fread(&game->fund_education, sizeof(game->fund_education), 1, f);
    fread(&game->auto_budget, sizeof(game->auto_budget), 1, f);
    fread(&game->show_newspaper, sizeof(game->show_newspaper), 1, f);
    fread(&game->last_year_day, sizeof(game->last_year_day), 1, f);
    fread(&game->play_ticks, sizeof(game->play_ticks), 1, f);
    fread(&game->rci_demand, sizeof(game->rci_demand), 1, f);
    fread(&game->r_pop, sizeof(game->r_pop), 1, f);
    fread(&game->c_pop, sizeof(game->c_pop), 1, f);
    fread(&game->i_pop, sizeof(game->i_pop), 1, f);
    fread(game->map, sizeof(game->map), 1, f);
    fread(game->humans, sizeof(Human) * game->num_humans, 1, f);
    fread(game->buildings, sizeof(Building) * game->num_buildings, 1, f);
    fclose(f);
    game->game_state = STATE_CITY_VIEW;
    game->menu_active = 0;
    game->zoom_level = 0;
    return 1;
}

/* Recentre scroll around cursor for the current zoom level */
static void recentre_scroll(GameState* game) {
    int tile_px, vtx, vty;

    tile_px = 16 >> game->zoom_level;
    vtx = 640 / tile_px;
    vty = (VIEW_TILES_Y * TILE_SIZE) / tile_px;
    if (vtx > MAP_WIDTH) vtx = MAP_WIDTH;
    if (vty > MAP_HEIGHT) vty = MAP_HEIGHT;

    game->scroll_x = (game->cursor_x > (unsigned)(vtx / 2))
                      ? game->cursor_x - vtx / 2 : 0;
    game->scroll_y = (game->cursor_y > (unsigned)(vty / 2))
                      ? game->cursor_y - vty / 2 : 0;
    if (game->scroll_x + vtx > MAP_WIDTH)
        game->scroll_x = MAP_WIDTH - vtx;
    if (game->scroll_y + vty > MAP_HEIGHT)
        game->scroll_y = MAP_HEIGHT - vty;
}

void handle_menu_input(GameState* game, int key, int extended) {
    const MenuItem *items;

    if (extended) {
        switch (key) {
            case 75: /* Left arrow */
                if (game->menu_selected > 0)
                    game->menu_selected--;
                else
                    game->menu_selected = NUM_MENUS - 1;
                game->menu_drop_sel = menu_first_selectable(game->menu_selected, game->menu_expand);
                break;
            case 77: /* Right arrow */
                if (game->menu_selected < NUM_MENUS - 1)
                    game->menu_selected++;
                else
                    game->menu_selected = 0;
                game->menu_drop_sel = menu_first_selectable(game->menu_selected, game->menu_expand);
                break;
            case 72: /* Up arrow */
                game->menu_drop_sel = menu_next_selectable(
                    game->menu_selected, game->menu_drop_sel, -1, game->menu_expand);
                break;
            case 80: /* Down arrow */
                game->menu_drop_sel = menu_next_selectable(
                    game->menu_selected, game->menu_drop_sel, 1, game->menu_expand);
                break;
            /* Alt+letter switches menu while open */
            case 33: game->menu_selected = 0; game->menu_drop_sel = menu_first_selectable(0, game->menu_expand); break;
            case 48: game->menu_selected = 1; game->menu_drop_sel = menu_first_selectable(1, game->menu_expand); break;
            case 31: game->menu_selected = 2; game->menu_drop_sel = menu_first_selectable(2, game->menu_expand); break;
            case 47: game->menu_selected = 3; game->menu_drop_sel = menu_first_selectable(3, game->menu_expand); break;
            case 24: game->menu_selected = 4; game->menu_drop_sel = menu_first_selectable(4, game->menu_expand); break;
        }
        return;
    }

    /* Regular keys */
    switch (key) {
        case 27: /* ESC - close menu */
            game->menu_active = 0;
            break;
        case 17: /* Ctrl+Q */
            set_text_mode();
            exit(0);
            break;
        case 13: /* Enter - execute selection */
            if (game->menu_drop_sel >= 0 &&
                game->menu_drop_sel < menu_counts[game->menu_selected]) {
                items = menu_items[game->menu_selected];
                if (items[game->menu_drop_sel].flags & (MF_HEADING | MF_DISABLED))
                    break;
                switch (items[game->menu_drop_sel].action) {
                    case 0: /* Set tool */
                        game->current_tool = items[game->menu_drop_sel].tile_type & 0x1F;
                        game->current_density = items[game->menu_drop_sel].tile_type >> 5;
                        game->menu_active = 0;
                        break;
                    case 1: /* Help */
                        game->menu_active = 0;
                        game->game_state = STATE_MENU;
                        break;
                    case 2: /* Quit */
                        set_text_mode();
                        exit(0);
                        break;
                    case 3: /* About */
                        game->menu_active = 0;
                        game->game_state = STATE_ABOUT;
                        break;
                    case 4: /* Noop / placeholder */
                        break;
                    case 5: /* Save */
                        game->menu_active = 0;
                        save_game(game, "CITYSIM.SAV");
                        game->needs_redraw = 1;
                        break;
                    case 6: /* Load */
                        game->menu_active = 0;
                        if (load_game(game, "CITYSIM.SAV"))
                            game->needs_redraw = 1;
                        break;
                    case 7: /* Toggle overlay */
                        game->overlay_flags ^= items[game->menu_drop_sel].tile_type;
                        break;
                    case 8: /* Budget screen */
                        game->menu_active = 0;
                        game->game_state = STATE_BUDGET;
                        break;
                    case 9: /* Set game speed */
                        game->game_speed = items[game->menu_drop_sel].tile_type;
                        game->menu_active = 0;
                        break;
                    case 10: /* Query mode */
                        game->menu_active = 0;
                        game->query_mode = 1;
                        break;
                    case 11: /* Toggle auto budget */
                        game->auto_budget = !game->auto_budget;
                        break;
                    case 12: /* Toggle newspaper */
                        game->show_newspaper = !game->show_newspaper;
                        break;
                    case 13: { /* Toggle collapsible section */
                        unsigned char tt = items[game->menu_drop_sel].tile_type;
                        if (tt == TILE_RESIDENTIAL)
                            game->menu_expand ^= 1;
                        else if (tt == TILE_COMMERCIAL)
                            game->menu_expand ^= 2;
                        else if (tt == TILE_INDUSTRIAL)
                            game->menu_expand ^= 4;
                    } break;
                }
            }
            break;
    }
}

void handle_input(GameState* game) {
    int key;
    int tile_px, vtx, vty;

    if (!kbhit())
        return;

    /* Compute zoom-dependent viewport size */
    tile_px = 16 >> game->zoom_level;
    vtx = 640 / tile_px;
    vty = (VIEW_TILES_Y * TILE_SIZE) / tile_px;
    if (vtx > MAP_WIDTH) vtx = MAP_WIDTH;
    if (vty > MAP_HEIGHT) vty = MAP_HEIGHT;

    key = getch();

    /* When menu is active, route all input to menu handler */
    if (game->menu_active) {
        if (key == 0 || key == 0xE0) {
            key = getch();
            handle_menu_input(game, key, 1);
        } else {
            handle_menu_input(game, key, 0);
        }
        return;
    }

    if (key == 0 || key == 0xE0) {
        /* Extended key */
        key = getch();

        switch (key) {
            case 72: /* Up arrow */
                if (game->cursor_y > 0) {
                    game->cursor_y--;
                    if (game->cursor_y < game->scroll_y)
                        game->scroll_y = game->cursor_y;
                }
                break;
            case 80: /* Down arrow */
                if (game->cursor_y < MAP_HEIGHT - 1) {
                    game->cursor_y++;
                    if (game->cursor_y >= game->scroll_y + vty)
                        game->scroll_y = game->cursor_y - vty + 1;
                }
                break;
            case 75: /* Left arrow */
                if (game->cursor_x > 0) {
                    game->cursor_x--;
                    if (game->cursor_x < game->scroll_x)
                        game->scroll_x = game->cursor_x;
                }
                break;
            case 77: /* Right arrow */
                if (game->cursor_x < MAP_WIDTH - 1) {
                    game->cursor_x++;
                    if (game->cursor_x >= game->scroll_x + vtx)
                        game->scroll_x = game->cursor_x - vtx + 1;
                }
                break;
            case 71: /* Home (numpad 7) - up-left diagonal */
                if (game->cursor_y >= 10) game->cursor_y -= 10;
                else game->cursor_y = 0;
                if (game->cursor_x >= 10) game->cursor_x -= 10;
                else game->cursor_x = 0;
                recentre_scroll(game);
                break;
            case 73: /* PgUp (numpad 9) - up-right diagonal */
                if (game->cursor_y >= 10) game->cursor_y -= 10;
                else game->cursor_y = 0;
                if (game->cursor_x + 10 < MAP_WIDTH) game->cursor_x += 10;
                else game->cursor_x = MAP_WIDTH - 1;
                recentre_scroll(game);
                break;
            case 79: /* End (numpad 1) - down-left diagonal */
                if (game->cursor_y + 10 < MAP_HEIGHT) game->cursor_y += 10;
                else game->cursor_y = MAP_HEIGHT - 1;
                if (game->cursor_x >= 10) game->cursor_x -= 10;
                else game->cursor_x = 0;
                recentre_scroll(game);
                break;
            case 81: /* PgDn (numpad 3) - down-right diagonal */
                if (game->cursor_y + 10 < MAP_HEIGHT) game->cursor_y += 10;
                else game->cursor_y = MAP_HEIGHT - 1;
                if (game->cursor_x + 10 < MAP_WIDTH) game->cursor_x += 10;
                else game->cursor_x = MAP_WIDTH - 1;
                recentre_scroll(game);
                break;
            case 59: /* F1 - Help */
                game->game_state = STATE_MENU;
                break;
            case 63: /* F5 - Budget */
                game->game_state = STATE_BUDGET;
                break;
            /* Alt+letter opens menus: F=33, B=48, S=31, V=47, O=24 */
            case 33: /* Alt+F - File */
                game->menu_active = 1;
                game->menu_selected = 0;
                game->menu_drop_sel = menu_first_selectable(0, game->menu_expand);
                break;
            case 48: /* Alt+B - Build */
                game->menu_active = 1;
                game->menu_selected = 1;
                game->menu_drop_sel = menu_first_selectable(1, game->menu_expand);
                break;
            case 31: /* Alt+S - Speed */
                game->menu_active = 1;
                game->menu_selected = 2;
                game->menu_drop_sel = menu_first_selectable(2, game->menu_expand);
                break;
            case 47: /* Alt+V - View */
                game->menu_active = 1;
                game->menu_selected = 3;
                game->menu_drop_sel = menu_first_selectable(3, game->menu_expand);
                break;
            case 24: /* Alt+O - Options */
                game->menu_active = 1;
                game->menu_selected = 4;
                game->menu_drop_sel = menu_first_selectable(4, game->menu_expand);
                break;
        }
    } else {
        /* Regular key */

        /* Clear query mode when a tool hotkey is pressed */
        switch (key) {
            case 'r': case 'R': case 'c': case 'C': case 'i': case 'I':
            case 'b': case 'B': case 'd': case 'D': case 'p': case 'P':
            case 't': case 'T': case 'g': case 'G': case 'l': case 'L':
            case 'f': case 'F': case 'e': case 'E': case 'o': case 'O':
            case 'a': case 'A':
                if (game->query_mode || game->query_popup) {
                    game->query_mode = 0;
                    game->query_popup = 0;
                    game->needs_redraw = 1;
                }
                break;
        }

        switch (key) {
            case ' ': /* Space - place tile, bulldoze, or query */
                if (game->query_popup) {
                    /* Dismiss popup */
                    game->query_popup = 0;
                    game->needs_redraw = 1;
                } else if (game->query_mode) {
                    /* Show query popup */
                    game->query_popup = 1;
                } else if (game->game_state == STATE_CITY_VIEW && game->zoom_level == 0) {
                    if (game->current_tool == TILE_BULLDOZER)
                        bulldoze_tile(game, game->cursor_x, game->cursor_y);
                    else
                        place_tile(game, game->cursor_x, game->cursor_y, game->current_tool);
                }
                break;
            case 13: /* Enter - also dismisses query popup */
                if (game->query_popup) {
                    game->query_popup = 0;
                    game->needs_redraw = 1;
                }
                break;
            case '?': /* Toggle query mode */
                if (game->query_popup) {
                    game->query_popup = 0;
                    game->query_mode = 0;
                    game->needs_redraw = 1;
                } else {
                    game->query_mode = !game->query_mode;
                }
                break;
            case 'r':
            case 'R':
                if (game->current_tool == TILE_RESIDENTIAL)
                    game->current_density = (game->current_density + 1) % 3;
                else {
                    game->current_tool = TILE_RESIDENTIAL;
                    game->current_density = 0;
                }
                break;
            case 'c':
            case 'C':
                if (game->current_tool == TILE_COMMERCIAL)
                    game->current_density = (game->current_density + 1) % 3;
                else {
                    game->current_tool = TILE_COMMERCIAL;
                    game->current_density = 0;
                }
                break;
            case 'i':
            case 'I':
                if (game->current_tool == TILE_INDUSTRIAL)
                    game->current_density = (game->current_density + 1) % 3;
                else {
                    game->current_tool = TILE_INDUSTRIAL;
                    game->current_density = 0;
                }
                break;
            case 'b':
            case 'B':
                game->current_tool = TILE_BULLDOZER;
                break;
            case 'd':
            case 'D':
                game->current_tool = TILE_ROAD;
                break;
            case 'p':
            case 'P':
                game->current_tool = TILE_PARK;
                break;
            case 't':
            case 'T':
                game->current_tool = TILE_RAIL;
                break;
            case 'g':
            case 'G':
                game->current_tool = TILE_POWER_PLANT;
                break;
            case 'l':
            case 'L':
                game->current_tool = TILE_POWER_LINE;
                break;
            case 'f':
            case 'F':
                game->current_tool = TILE_POLICE;
                break;
            case 'e':
            case 'E':
                game->current_tool = TILE_FIRE;
                break;
            case 'o':
            case 'O':
                game->current_tool = TILE_HOSPITAL;
                break;
            case 'a':
            case 'A':
                game->current_tool = TILE_AIRPORT;
                break;
            case 'h':
            case 'H':
                /* Find nearest human to cursor */
                if (game->game_state == STATE_CITY_VIEW && game->num_humans > 0) {
                    unsigned int i;
                    unsigned int min_dist = 999999;
                    unsigned int dist;
                    
                    for (i = 0; i < game->num_humans; i++) {
                        dist = abs((int)game->humans[i].x - (int)game->cursor_x) +
                               abs((int)game->humans[i].y - (int)game->cursor_y);
                        if (dist < min_dist) {
                            min_dist = dist;
                            game->selected_human = i;
                        }
                    }
                    game->game_state = STATE_HUMAN_VIEW;
                }
                break;
            case 'z':
            case 'Z':
                /* Toggle between city and human view */
                if (game->game_state == STATE_HUMAN_VIEW) {
                    game->game_state = STATE_CITY_VIEW;
                } else if (game->num_humans > 0) {
                    game->selected_human = rand_range(0, game->num_humans - 1);
                    game->game_state = STATE_HUMAN_VIEW;
                }
                break;
            case '-': /* Zoom out */
                if (game->zoom_level < 4) {
                    game->zoom_level++;
                    recentre_scroll(game);
                }
                break;
            case '+': /* Zoom in */
            case '=':
                if (game->zoom_level > 0) {
                    game->zoom_level--;
                    recentre_scroll(game);
                }
                break;
            case '8': /* Numpad 8 (NumLock on) - up */
                if (game->cursor_y > 0) {
                    game->cursor_y--;
                    if (game->cursor_y < game->scroll_y)
                        game->scroll_y = game->cursor_y;
                }
                break;
            case '2': /* Numpad 2 (NumLock on) - down */
                if (game->cursor_y < MAP_HEIGHT - 1) {
                    game->cursor_y++;
                    if (game->cursor_y >= game->scroll_y + vty)
                        game->scroll_y = game->cursor_y - vty + 1;
                }
                break;
            case '4': /* Numpad 4 (NumLock on) - left */
                if (game->cursor_x > 0) {
                    game->cursor_x--;
                    if (game->cursor_x < game->scroll_x)
                        game->scroll_x = game->cursor_x;
                }
                break;
            case '6': /* Numpad 6 (NumLock on) - right */
                if (game->cursor_x < MAP_WIDTH - 1) {
                    game->cursor_x++;
                    if (game->cursor_x >= game->scroll_x + vtx)
                        game->scroll_x = game->cursor_x - vtx + 1;
                }
                break;
            case '5': /* Numpad 5 (NumLock on) - place tile or query */
                if (game->query_popup) {
                    game->query_popup = 0;
                    game->needs_redraw = 1;
                } else if (game->query_mode) {
                    game->query_popup = 1;
                } else if (game->game_state == STATE_CITY_VIEW && game->zoom_level == 0) {
                    if (game->current_tool == TILE_BULLDOZER)
                        bulldoze_tile(game, game->cursor_x, game->cursor_y);
                    else
                        place_tile(game, game->cursor_x, game->cursor_y, game->current_tool);
                }
                break;
            case '7': /* Numpad 7 (NumLock on) - up-left diagonal */
                if (game->cursor_y >= 10) game->cursor_y -= 10;
                else game->cursor_y = 0;
                if (game->cursor_x >= 10) game->cursor_x -= 10;
                else game->cursor_x = 0;
                recentre_scroll(game);
                break;
            case '9': /* Numpad 9 (NumLock on) - up-right diagonal */
                if (game->cursor_y >= 10) game->cursor_y -= 10;
                else game->cursor_y = 0;
                if (game->cursor_x + 10 < MAP_WIDTH) game->cursor_x += 10;
                else game->cursor_x = MAP_WIDTH - 1;
                recentre_scroll(game);
                break;
            case '1': /* Numpad 1 (NumLock on) - down-left diagonal */
                if (game->cursor_y + 10 < MAP_HEIGHT) game->cursor_y += 10;
                else game->cursor_y = MAP_HEIGHT - 1;
                if (game->cursor_x >= 10) game->cursor_x -= 10;
                else game->cursor_x = 0;
                recentre_scroll(game);
                break;
            case '3': /* Numpad 3 (NumLock on) - down-right diagonal */
                if (game->cursor_y + 10 < MAP_HEIGHT) game->cursor_y += 10;
                else game->cursor_y = MAP_HEIGHT - 1;
                if (game->cursor_x + 10 < MAP_WIDTH) game->cursor_x += 10;
                else game->cursor_x = MAP_WIDTH - 1;
                recentre_scroll(game);
                break;
            case 17: /* Ctrl+Q - Quit */
                set_text_mode();
                exit(0);
                break;
            case 27: /* ESC */
                if (game->game_state == STATE_HUMAN_VIEW || game->game_state == STATE_MENU) {
                    game->game_state = STATE_CITY_VIEW;
                } else if (game->game_state == STATE_CITY_VIEW) {
                    /* Open menu */
                    game->menu_active = 1;
                    game->menu_selected = 0;
                    game->menu_drop_sel = menu_first_selectable(0, game->menu_expand);
                }
                break;
        }
    }
}
