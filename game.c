#include "citysim.h"

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
        default: return "Unknown";
    }
}

int get_tile_cost(unsigned char type) {
    switch (type) {
        case TILE_GRASS: return 0;
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
    game->population++;
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

void update_tile_services(GameState* game) {
    int x, y, dx, dy;
    int has_power, has_water;
    
    /* Reset all services */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            game->map[y][x].power = 0;
            game->map[y][x].water = 0;
        }
    }
    
    /* Propagate power and water from sources */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type == TILE_POWER_PLANT) {
                /* Power spreads to adjacent tiles */
                for (dy = -5; dy <= 5; dy++) {
                    for (dx = -5; dx <= 5; dx++) {
                        if (y + dy >= 0 && y + dy < MAP_HEIGHT &&
                            x + dx >= 0 && x + dx < MAP_WIDTH) {
                            game->map[y + dy][x + dx].power = 1;
                        }
                    }
                }
            }
            
            if (game->map[y][x].type == TILE_WATER_PUMP) {
                /* Water spreads to adjacent tiles */
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
}

void update_buildings(GameState* game) {
    int x, y;
    
    /* Update residential zones - only check top-left tile of each 3x3 block */
    for (y = 0; y < MAP_HEIGHT; y++) {
        for (x = 0; x < MAP_WIDTH; x++) {
            if (game->map[y][x].type == TILE_RESIDENTIAL &&
                game->map[y][x].development == 0) {
                /* Spawn humans in residential zones with power and water */
                if (game->map[y][x].power && game->map[y][x].water) {
                    if (rand_range(0, 1000) < 2 && game->num_humans < MAX_HUMANS) {
                        spawn_human(game, x + 1, y + 1);
                        /* Mark all 9 tiles as populated */
                        if (y + 2 < MAP_HEIGHT && x + 2 < MAP_WIDTH) {
                            int r, c;
                            for (r = 0; r < 3; r++)
                                for (c = 0; c < 3; c++)
                                    game->map[y+r][x+c].population = 1;
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

void place_tile(GameState* game, unsigned short x, unsigned short y, unsigned char type) {
    int cost;

    if (x >= MAP_WIDTH || y >= MAP_HEIGHT)
        return;

    /* 3x3 zone types: residential, commercial, industrial */
    if (type == TILE_RESIDENTIAL || type == TILE_COMMERCIAL ||
        type == TILE_INDUSTRIAL) {
        int r, c;
        unsigned short bx, by;

        /* Block is centered on cursor: top-left = (x-1, y-1) */
        if (x < 1 || y < 1 || x + 1 >= MAP_WIDTH || y + 1 >= MAP_HEIGHT)
            return;

        /* Check all 9 cells are buildable (not water, not already zoned) */
        for (r = -1; r <= 1; r++) {
            for (c = -1; c <= 1; c++) {
                by = y + r;
                bx = x + c;
                if (game->map[by][bx].type == TILE_WATER)
                    return;
                if (game->map[by][bx].type == TILE_RESIDENTIAL ||
                    game->map[by][bx].type == TILE_COMMERCIAL ||
                    game->map[by][bx].type == TILE_INDUSTRIAL)
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
            }
        }
        return;
    }

    /* Don't build on water */
    if (game->map[y][x].type == TILE_WATER && type != TILE_WATER)
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
    vty = 320 / tile_px;
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

void handle_input(GameState* game) {
    int key;
    int tile_px, vtx, vty;

    if (!kbhit())
        return;

    /* Compute zoom-dependent viewport size */
    tile_px = 16 >> game->zoom_level;
    vtx = 640 / tile_px;
    vty = 320 / tile_px;
    if (vtx > MAP_WIDTH) vtx = MAP_WIDTH;
    if (vty > MAP_HEIGHT) vty = MAP_HEIGHT;

    key = getch();

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
            case 59: /* F1 - Help */
                game->game_state = STATE_MENU;
                break;
        }
    } else {
        /* Regular key */
        switch (key) {
            case ' ': /* Space - place tile */
                if (game->game_state == STATE_CITY_VIEW && game->zoom_level == 0) {
                    place_tile(game, game->cursor_x, game->cursor_y, game->current_tool);
                }
                break;
            case 'r':
            case 'R':
                game->current_tool = TILE_RESIDENTIAL;
                break;
            case 'c':
            case 'C':
                game->current_tool = TILE_COMMERCIAL;
                break;
            case 'i':
            case 'I':
                game->current_tool = TILE_INDUSTRIAL;
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
            case 27: /* ESC */
                if (game->game_state == STATE_HUMAN_VIEW || game->game_state == STATE_MENU) {
                    game->game_state = STATE_CITY_VIEW;
                } else if (game->game_state == STATE_CITY_VIEW) {
                    /* Quit the game - set a flag or exit */
                    /* For now, just return to text mode and the main loop will end */
                    set_text_mode();
                    exit(0);
                }
                break;
        }
    }
}
