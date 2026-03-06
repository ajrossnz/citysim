#include "citysim.h"

#define IS_ZONE(t) ((t) == TILE_RESIDENTIAL || (t) == TILE_COMMERCIAL || (t) == TILE_INDUSTRIAL)

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
        case TILE_SCHOOL: return "School";
        case TILE_POWER_PLANT: return "Power Plant";
        case TILE_WATER_PUMP: return "Water Pump";
        case TILE_RAIL: return "Rail";
        case TILE_BULLDOZER: return "Bulldozer";
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
        case TILE_SCHOOL: return 800;
        case TILE_POWER_PLANT: return 5000;
        case TILE_WATER_PUMP: return 2000;
        case TILE_RAIL: return 20;
        default: return 0;
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
    
    /* Starting funds */
    game->funds = 50000;
    game->game_time = 8;  /* Start at 8 AM */
    game->game_day = 1;
    game->current_tool = TILE_RESIDENTIAL;
    game->game_state = STATE_CITY_VIEW;
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

    /* Reset all services */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            game->map[y][x].power = 0;
            game->map[y][x].water = 0;
        }
    }

    /* --- Power: BFS through power lines from power plants --- */
    head = 0;
    tail = 0;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
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
                        } else if (IS_ZONE(t) && !game->map[ny][nx].power) {
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

    /* BFS through connected power lines */
    while (head < tail) {
        qy = (unsigned short)(queue[head] / MAP_WIDTH);
        qx = (unsigned short)(queue[head] % MAP_WIDTH);
        head++;

        /* Check 4 neighbors */
        for (dx = -1; dx <= 1; dx++) {
            for (dy = -1; dy <= 1; dy++) {
                if ((dx == 0) == (dy == 0)) continue;  /* only cardinal */
                nx = qx + dx;
                ny = qy + dy;
                if (nx < MAP_WIDTH && ny < MAP_HEIGHT &&
                    game->map[ny][nx].type == TILE_POWER_LINE &&
                    !game->map[ny][nx].power &&
                    tail < 4096) {
                    game->map[ny][nx].power = 1;
                    queue[tail++] = (unsigned int)ny * MAP_WIDTH + nx;
                }
            }
        }
    }

    /* Post-BFS: powered power lines energise adjacent buildings */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type == TILE_POWER_LINE &&
                game->map[y][x].power) {
                /* Check 4 cardinal neighbors */
                for (dx = -1; dx <= 1; dx++) {
                    for (dy = -1; dy <= 1; dy++) {
                        if ((dx == 0) == (dy == 0)) continue;
                        nx = x + dx;
                        ny = y + dy;
                        if (nx >= MAP_WIDTH || ny >= MAP_HEIGHT) continue;
                        t = game->map[ny][nx].type;
                        if (t == TILE_POWER_LINE || t == TILE_POWER_PLANT ||
                            t == TILE_GRASS || t == TILE_ROAD || t == TILE_RAIL ||
                            t == TILE_WATER)
                            continue;
                        /* It's a building/zone tile - power it */
                        if (t == TILE_RESIDENTIAL || t == TILE_COMMERCIAL ||
                            t == TILE_INDUSTRIAL) {
                            /* 3x3 zone: find anchor and power all 9 */
                            if (find_zone_anchor(game, nx, ny, &ax, &ay)) {
                                for (r = 0; r < 3; r++)
                                    for (c = 0; c < 3; c++)
                                        if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                                            game->map[ay + r][ax + c].power = 1;
                            }
                        } else {
                            /* 1x1 building */
                            game->map[ny][nx].power = 1;
                        }
                    }
                }
            }
        }
    }

    /* --- Phase 4: Zone-to-zone power transfer BFS --- */
    {
        static unsigned int zqueue[4096];
        int zhead = 0, ztail = 0;
        int pr, pc;

        /* Seed: enqueue every powered zone anchor */
        for (y = 0; y < MAP_HEIGHT; y++) {
            for (x = 0; x < MAP_WIDTH; x++) {
                if (IS_ZONE(game->map[y][x].type) &&
                    game->map[y][x].development == 0 &&
                    game->map[y][x].power &&
                    ztail < 4096) {
                    zqueue[ztail++] = (unsigned int)y * MAP_WIDTH + x;
                }
            }
        }

        /* BFS: for each anchor, probe 12 cardinal-adjacent perimeter cells */
        while (zhead < ztail) {
            unsigned short zy = (unsigned short)(zqueue[zhead] / MAP_WIDTH);
            unsigned short zx = (unsigned short)(zqueue[zhead] % MAP_WIDTH);
            zhead++;

            /* North edge: row=-1, col=0..2 */
            for (pc = 0; pc < 3; pc++) {
                pr = -1;
                nx = zx + pc; ny = zy + pr;
                if (nx < MAP_WIDTH && ny < MAP_HEIGHT &&
                    IS_ZONE(game->map[ny][nx].type) &&
                    !game->map[ny][nx].power) {
                    if (find_zone_anchor(game, nx, ny, &ax, &ay)) {
                        for (r = 0; r < 3; r++)
                            for (c = 0; c < 3; c++)
                                if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                                    game->map[ay + r][ax + c].power = 1;
                        if (ztail < 4096)
                            zqueue[ztail++] = (unsigned int)ay * MAP_WIDTH + ax;
                    }
                }
            }
            /* South edge: row=3, col=0..2 */
            for (pc = 0; pc < 3; pc++) {
                pr = 3;
                nx = zx + pc; ny = zy + pr;
                if (nx < MAP_WIDTH && ny < MAP_HEIGHT &&
                    IS_ZONE(game->map[ny][nx].type) &&
                    !game->map[ny][nx].power) {
                    if (find_zone_anchor(game, nx, ny, &ax, &ay)) {
                        for (r = 0; r < 3; r++)
                            for (c = 0; c < 3; c++)
                                if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                                    game->map[ay + r][ax + c].power = 1;
                        if (ztail < 4096)
                            zqueue[ztail++] = (unsigned int)ay * MAP_WIDTH + ax;
                    }
                }
            }
            /* West edge: col=-1, row=0..2 */
            for (pr = 0; pr < 3; pr++) {
                pc = -1;
                nx = zx + pc; ny = zy + pr;
                if (nx < MAP_WIDTH && ny < MAP_HEIGHT &&
                    IS_ZONE(game->map[ny][nx].type) &&
                    !game->map[ny][nx].power) {
                    if (find_zone_anchor(game, nx, ny, &ax, &ay)) {
                        for (r = 0; r < 3; r++)
                            for (c = 0; c < 3; c++)
                                if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                                    game->map[ay + r][ax + c].power = 1;
                        if (ztail < 4096)
                            zqueue[ztail++] = (unsigned int)ay * MAP_WIDTH + ax;
                    }
                }
            }
            /* East edge: col=3, row=0..2 */
            for (pr = 0; pr < 3; pr++) {
                pc = 3;
                nx = zx + pc; ny = zy + pr;
                if (nx < MAP_WIDTH && ny < MAP_HEIGHT &&
                    IS_ZONE(game->map[ny][nx].type) &&
                    !game->map[ny][nx].power) {
                    if (find_zone_anchor(game, nx, ny, &ax, &ay)) {
                        for (r = 0; r < 3; r++)
                            for (c = 0; c < 3; c++)
                                if (ay + r < MAP_HEIGHT && ax + c < MAP_WIDTH)
                                    game->map[ay + r][ax + c].power = 1;
                        if (ztail < 4096)
                            zqueue[ztail++] = (unsigned int)ay * MAP_WIDTH + ax;
                    }
                }
            }
        }
    }

    /* --- Water: radius-based from water pumps --- */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
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

    /* --- Pollution: reset and accumulate from sources --- */
    for (y = 0; y < MAP_HEIGHT; y++)
        for (x = 0; x < MAP_WIDTH; x++)
            game->map[y][x].pollution = 0;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            int base = 0, radius = 0;
            t = game->map[y][x].type;

            if (t == TILE_INDUSTRIAL && game->map[y][x].development == 0) {
                base = 80; radius = 8;
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
                        int ny = y + dy, nx = x + dx;
                        int dist, contrib;
                        unsigned int val;
                        if (ny < 0 || ny >= MAP_HEIGHT || nx < 0 || nx >= MAP_WIDTH)
                            continue;
                        dist = abs(dx) + abs(dy);
                        if (dist > radius) continue;
                        contrib = base - (base * dist / radius);
                        val = (unsigned int)game->map[ny][nx].pollution + contrib;
                        game->map[ny][nx].pollution = (val > 255) ? 255 : (unsigned char)val;
                    }
                }
            }
        }
    }

    /* Parks reduce pollution */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type == TILE_PARK) {
                for (dy = -4; dy <= 4; dy++) {
                    for (dx = -4; dx <= 4; dx++) {
                        int ny = y + dy, nx = x + dx;
                        int dist, reduction;
                        if (ny < 0 || ny >= MAP_HEIGHT || nx < 0 || nx >= MAP_WIDTH)
                            continue;
                        dist = abs(dx) + abs(dy);
                        if (dist > 4) continue;
                        reduction = 20 - (20 * dist / 4);
                        if (game->map[ny][nx].pollution > reduction)
                            game->map[ny][nx].pollution -= (unsigned char)reduction;
                        else
                            game->map[ny][nx].pollution = 0;
                    }
                }
            }
        }
    }
}

void update_buildings(GameState* game) {
    int x, y;

    /* --- RCI demand computation --- */
    game->r_pop = 0;
    game->c_pop = 0;
    game->i_pop = 0;

    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
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

    /* R demand: bootstrap floor of 10 when no industry/commerce */
    if (game->i_pop == 0 && game->c_pop == 0)
        game->rci_demand[0] = 10;
    else
        game->rci_demand[0] = (int)(game->i_pop + game->c_pop) * 3 / 2 - (int)game->r_pop;
    game->rci_demand[1] = (int)game->r_pop / 2 - (int)game->c_pop;
    game->rci_demand[2] = (int)game->r_pop * 3 / 10 - (int)game->i_pop;

    /* Update zones - only check top-left tile of each 3x3 block */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            /* --- Residential growth (gated on R demand) --- */
            if (game->map[y][x].type == TILE_RESIDENTIAL &&
                game->map[y][x].development == 0) {
                if (game->map[y][x].power && game->rci_demand[0] > 0 &&
                    y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
                    int r, c, pop_count = 0;
                    for (r = 0; r < 3; r++)
                        for (c = 0; c < 3; c++)
                            if (game->map[y+r][x+c].population)
                                pop_count++;

                    {
                        int growth_chance = 10;
                        unsigned char poll = game->map[y][x].pollution;
                        unsigned char dens = game->map[y][x].density;

                        if (dens == 2 && poll > 60)
                            growth_chance -= 3;
                        else if (dens == 1 && poll > 40)
                            growth_chance -= 4;
                        else if (dens == 0 && poll > 30)
                            growth_chance -= 5;
                        if (growth_chance < 1) growth_chance = 1;

                        if (pop_count < 9 && rand_range(0, 100) < (unsigned)growth_chance) {
                            int target = (int)rand_range(0, 8 - pop_count);
                            int found = 0;
                            for (r = 0; r < 3 && !found; r++) {
                                for (c = 0; c < 3 && !found; c++) {
                                    if (!game->map[y+r][x+c].population) {
                                        if (target == 0) {
                                            game->map[y+r][x+c].population = 1;
                                            game->population += (pop_count == 8) ? 112 : 111;
                                            found = 1;
                                        } else {
                                            target--;
                                        }
                                    }
                                }
                            }
                            if (pop_count == 0 && game->num_humans < MAX_HUMANS)
                                spawn_human(game, x + 1, y + 1);
                        }
                    }
                }
            }

            /* --- Commercial growth (gated on C demand) --- */
            if (game->map[y][x].type == TILE_COMMERCIAL &&
                game->map[y][x].development == 0) {
                if (game->map[y][x].power && game->rci_demand[1] > 0 &&
                    y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
                    int r, c, pop_count = 0;
                    for (r = 0; r < 3; r++)
                        for (c = 0; c < 3; c++)
                            if (game->map[y+r][x+c].population)
                                pop_count++;

                    if (pop_count < 9 && rand_range(0, 100) < 10) {
                        int target = (int)rand_range(0, 8 - pop_count);
                        int found = 0;
                        for (r = 0; r < 3 && !found; r++) {
                            for (c = 0; c < 3 && !found; c++) {
                                if (!game->map[y+r][x+c].population) {
                                    if (target == 0) {
                                        game->map[y+r][x+c].population = 1;
                                        found = 1;
                                    } else {
                                        target--;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* --- Industrial growth (gated on I demand) --- */
            if (game->map[y][x].type == TILE_INDUSTRIAL &&
                game->map[y][x].development == 0) {
                if (game->map[y][x].power && game->rci_demand[2] > 0 &&
                    y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
                    int r, c, pop_count = 0;
                    for (r = 0; r < 3; r++)
                        for (c = 0; c < 3; c++)
                            if (game->map[y+r][x+c].population)
                                pop_count++;

                    if (pop_count < 9 && rand_range(0, 100) < 10) {
                        int target = (int)rand_range(0, 8 - pop_count);
                        int found = 0;
                        for (r = 0; r < 3 && !found; r++) {
                            for (c = 0; c < 3 && !found; c++) {
                                if (!game->map[y+r][x+c].population) {
                                    if (target == 0) {
                                        game->map[y+r][x+c].population = 1;
                                        found = 1;
                                    } else {
                                        target--;
                                    }
                                }
                            }
                        }
                    }
                }
            }

            /* Population decay for unpowered zones */
            if ((game->map[y][x].type == TILE_RESIDENTIAL ||
                 game->map[y][x].type == TILE_COMMERCIAL ||
                 game->map[y][x].type == TILE_INDUSTRIAL) &&
                game->map[y][x].development == 0 &&
                game->map[y][x].population > 0 &&
                !game->map[y][x].power) {
                if (rand_range(0, 2880) == 0) {
                    int r, c, pop_count = 0;
                    unsigned int i;
                    /* Count populated sub-tiles before clearing */
                    if (y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
                        for (r = 0; r < 3; r++)
                            for (c = 0; c < 3; c++)
                                if (game->map[y+r][x+c].population)
                                    pop_count++;
                    }
                    /* Clear population on all 9 tiles */
                    for (r = 0; r < 3; r++)
                        for (c = 0; c < 3; c++)
                            if (y + r < MAP_HEIGHT && x + c < MAP_WIDTH)
                                game->map[y+r][x+c].population = 0;
                    /* Subtract zone population based on how full it was */
                    if (game->map[y][x].type == TILE_RESIDENTIAL) {
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
                        if (game->humans[i].home_x >= x &&
                            game->humans[i].home_x < x + 3 &&
                            game->humans[i].home_y >= y &&
                            game->humans[i].home_y < y + 3) {
                            /* Swap-remove */
                            game->num_humans--;
                            if (i < game->num_humans)
                                game->humans[i] = game->humans[game->num_humans];
                        } else {
                            i++;
                        }
                    }
                }
            }
        }
    }
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
        }
        
        /* Update game systems every hour */
        update_tile_services(game);
        update_humans(game);
        update_buildings(game);
        
        /* Collect taxes every 24 hours */
        if (game->game_time == 0) {
            game->funds += game->population * 5;
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
    game->map[y][x].density = 0;
}

void bulldoze_tile(GameState* game, unsigned short x, unsigned short y) {
    unsigned char type;
    int cost = 1;  /* base bulldoze cost per tile */

    if (x >= MAP_WIDTH || y >= MAP_HEIGHT)
        return;

    type = game->map[y][x].type;
    if (type == TILE_GRASS)
        return;  /* nothing to bulldoze */

    /* 3x3 zone or building: find anchor and clear all 9 tiles */
    if (type == TILE_RESIDENTIAL || type == TILE_COMMERCIAL ||
        type == TILE_INDUSTRIAL || type == TILE_POWER_PLANT) {
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

    /* 3x3 zone/building types */
    if (type == TILE_RESIDENTIAL || type == TILE_COMMERCIAL ||
        type == TILE_INDUSTRIAL || type == TILE_POWER_PLANT) {
        int r, c;
        unsigned short bx, by;

        /* Block is centered on cursor: top-left = (x-1, y-1) */
        if (x < 1 || y < 1 || x + 1 >= MAP_WIDTH || y + 1 >= MAP_HEIGHT)
            return;

        /* Check all 9 cells are empty grass */
        for (r = -1; r <= 1; r++) {
            for (c = -1; c <= 1; c++) {
                by = y + r;
                bx = x + c;
                if (game->map[by][bx].type != TILE_GRASS)
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
        } else if (existing != TILE_GRASS && existing != TILE_RAIL) {
            return;  /* reject everything else */
        }

        cost = get_tile_cost(type);
        if (game->funds < cost)
            return;
        game->funds -= cost;
        game->map[y][x].type = TILE_POWER_LINE;
        game->map[y][x].development = (existing == TILE_GRASS) ? 0 : existing;
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

    /* Only build on empty grass */
    if (game->map[y][x].type != TILE_GRASS)
        return;

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
                game->menu_drop_sel = menu_first_selectable(game->menu_selected);
                break;
            case 77: /* Right arrow */
                if (game->menu_selected < NUM_MENUS - 1)
                    game->menu_selected++;
                else
                    game->menu_selected = 0;
                game->menu_drop_sel = menu_first_selectable(game->menu_selected);
                break;
            case 72: /* Up arrow */
                game->menu_drop_sel = menu_next_selectable(
                    game->menu_selected, game->menu_drop_sel, -1);
                break;
            case 80: /* Down arrow */
                game->menu_drop_sel = menu_next_selectable(
                    game->menu_selected, game->menu_drop_sel, 1);
                break;
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
                    case 3: /* About - no-op for now */
                        game->menu_active = 0;
                        break;
                    case 4: /* Noop / placeholder */
                        break;
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
        }
    } else {
        /* Regular key */
        switch (key) {
            case ' ': /* Space - place tile or bulldoze */
                if (game->game_state == STATE_CITY_VIEW && game->zoom_level == 0) {
                    if (game->current_tool == TILE_BULLDOZER)
                        bulldoze_tile(game, game->cursor_x, game->cursor_y);
                    else
                        place_tile(game, game->cursor_x, game->cursor_y, game->current_tool);
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
                    game->menu_drop_sel = menu_first_selectable(0);
                }
                break;
        }
    }
}
