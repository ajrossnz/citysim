#include "citysim.h"

int main(void) {
    static GameState game;
    static MouseState mouse;
    int running = 1;
    int first_render = 1;
    unsigned int last_game_time = 0;
    long last_funds = 0;
    unsigned char last_tool = 0;
    unsigned char old_zoom_level = 0;
    unsigned int flash_timer = 0;
    int has_mouse = 0;
    unsigned char last_buttons = 0;
    unsigned short last_mouse_x = 0;
    unsigned short last_mouse_y = 0;
    unsigned char last_menu_active = 0;
    unsigned char last_menu_selected = 0;
    signed char last_menu_drop_sel = -1;
    unsigned char pop_snap[VIEW_TILES_Y][VIEW_TILES_X];
    int snap_x, snap_y, pop_changed;
    /* Initialize game */
    init_game(&game);
    last_funds = game.funds;
    last_tool = game.current_tool;

    /* Enter graphics mode */
    init_ega();

    /* Try to init mouse; hide hardware cursor (we draw our own) */
    has_mouse = mouse_init();
    if (has_mouse)
        mouse_hide();

    /* Main game loop */
    while (running) {
        int needs_ui_update = 0;
        int scroll_changed = 0;
        int cursor_changed = 0;
        int zoom_changed = 0;
        unsigned short old_cursor_x = game.cursor_x;
        unsigned short old_cursor_y = game.cursor_y;
        unsigned short old_scroll_x = game.scroll_x;
        unsigned short old_scroll_y = game.scroll_y;
        unsigned char old_tile;

        /* Snapshot visible tile population before update (zoom 0 only) */
        pop_changed = 0;
        if (game.game_state == STATE_CITY_VIEW && game.zoom_level == 0) {
            for (snap_y = 0; snap_y < VIEW_TILES_Y; snap_y++) {
                for (snap_x = 0; snap_x < VIEW_TILES_X; snap_x++) {
                    unsigned short mx = game.scroll_x + snap_x;
                    unsigned short my = game.scroll_y + snap_y;
                    if (mx < MAP_WIDTH && my < MAP_HEIGHT)
                        pop_snap[snap_y][snap_x] = game.map[my][mx].population;
                    else
                        pop_snap[snap_y][snap_x] = 0;
                }
            }
        }

        /* Update game logic (paused while menu is open) */
        if (!game.menu_active)
            update_game(&game);

        /* Remember tile under cursor before input */
        old_tile = game.map[game.cursor_y][game.cursor_x].type;

        /* Handle input */
        handle_input(&game);

        /* Poll mouse */
        if (has_mouse) {
            mouse_read(&mouse);

            if (game.menu_active) {
                /* Mouse in menu bar area */
                if ((mouse.buttons & 1) && !(last_buttons & 1)) {
                    if (mouse.pixel_y < MENU_BAR_HEIGHT) {
                        /* Click on menu bar: determine which menu */
                        int mi;
                        for (mi = NUM_MENUS - 1; mi >= 0; mi--) {
                            if ((int)mouse.pixel_x >= menu_label_x[mi]) {
                                game.menu_selected = (unsigned char)mi;
                                game.menu_drop_sel = menu_first_selectable(mi);
                                break;
                            }
                        }
                    } else {
                        /* Click in dropdown area? */
                        int drop_x = menu_label_x[game.menu_selected];
                        int drop_w = 180;
                        int drop_h = menu_counts[game.menu_selected] * 16 + 2;
                        if ((int)mouse.pixel_x >= drop_x &&
                            (int)mouse.pixel_x < drop_x + drop_w &&
                            (int)mouse.pixel_y >= MENU_BAR_HEIGHT &&
                            (int)mouse.pixel_y < MENU_BAR_HEIGHT + drop_h) {
                            int item_idx = ((int)mouse.pixel_y - MENU_BAR_HEIGHT - 1) / 16;
                            if (item_idx >= 0 && item_idx < menu_counts[game.menu_selected]) {
                                game.menu_drop_sel = (signed char)item_idx;
                                /* Execute selection via Enter */
                                handle_menu_input(&game, 13, 0);
                            }
                        } else {
                            /* Click outside: close menu */
                            game.menu_active = 0;
                        }
                    }
                } else if (!(mouse.buttons & 1)) {
                    /* Track mouse hover in dropdown */
                    int drop_x = menu_label_x[game.menu_selected];
                    int drop_w = 180;
                    int drop_h = menu_counts[game.menu_selected] * 16 + 2;
                    if ((int)mouse.pixel_y < MENU_BAR_HEIGHT) {
                        /* Hovering over menu bar: switch menus */
                        int mi;
                        for (mi = NUM_MENUS - 1; mi >= 0; mi--) {
                            if ((int)mouse.pixel_x >= menu_label_x[mi]) {
                                game.menu_selected = (unsigned char)mi;
                                game.menu_drop_sel = menu_first_selectable(mi);
                                break;
                            }
                        }
                    } else if ((int)mouse.pixel_x >= drop_x &&
                               (int)mouse.pixel_x < drop_x + drop_w &&
                               (int)mouse.pixel_y >= MENU_BAR_HEIGHT &&
                               (int)mouse.pixel_y < MENU_BAR_HEIGHT + drop_h) {
                        int item_idx = ((int)mouse.pixel_y - MENU_BAR_HEIGHT - 1) / 16;
                        if (item_idx >= 0 && item_idx < menu_counts[game.menu_selected])
                            game.menu_drop_sel = (signed char)item_idx;
                    }
                }
            } else if (game.game_state == STATE_CITY_VIEW && game.zoom_level == 0) {
                /* Normal map mouse handling - only update cursor if mouse moved */
                if ((mouse.pixel_x != last_mouse_x || mouse.pixel_y != last_mouse_y) &&
                    mouse.pixel_y >= MAP_Y_OFFSET &&
                    mouse.pixel_y < MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE) {
                    unsigned short mx = game.scroll_x + mouse.pixel_x / TILE_SIZE;
                    unsigned short my = game.scroll_y + (mouse.pixel_y - MAP_Y_OFFSET) / TILE_SIZE;
                    if (mx < MAP_WIDTH && my < MAP_HEIGHT) {
                        game.cursor_x = mx;
                        game.cursor_y = my;
                    }
                }
                /* Left click = place tile (edge-triggered) */
                if ((mouse.buttons & 1) && !(last_buttons & 1)) {
                    if (mouse.pixel_y < MENU_BAR_HEIGHT) {
                        /* Click on menu bar: open menu */
                        int mi;
                        for (mi = NUM_MENUS - 1; mi >= 0; mi--) {
                            if ((int)mouse.pixel_x >= menu_label_x[mi]) {
                                game.menu_active = 1;
                                game.menu_selected = (unsigned char)mi;
                                game.menu_drop_sel = menu_first_selectable(mi);
                                break;
                            }
                        }
                    } else if (mouse.pixel_y >= MAP_Y_OFFSET &&
                               mouse.pixel_y < MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE) {
                        if (game.current_tool == TILE_BULLDOZER)
                            bulldoze_tile(&game, game.cursor_x, game.cursor_y);
                        else
                            place_tile(&game, game.cursor_x, game.cursor_y, game.current_tool);
                    }
                }
            }
            last_mouse_x = mouse.pixel_x;
            last_mouse_y = mouse.pixel_y;
            last_buttons = mouse.buttons;
        }

        /* Detect what changed */
        scroll_changed = (game.scroll_x != old_scroll_x ||
                          game.scroll_y != old_scroll_y);
        cursor_changed = (game.cursor_x != old_cursor_x ||
                          game.cursor_y != old_cursor_y);
        zoom_changed = (game.zoom_level != old_zoom_level);

        /* Check if UI needs updating */
        if (game.game_time != last_game_time ||
            game.funds != last_funds ||
            game.current_tool != last_tool ||
            cursor_changed ||
            zoom_changed) {
            needs_ui_update = 1;
        }

        if (game.game_state == STATE_CITY_VIEW) {
            if (first_render || scroll_changed || zoom_changed) {
                /* Full map redraw */
                if (game.zoom_level > 0) {
                    draw_map_zoomed(&game);
                } else {
                    draw_map(&game);
                }
                draw_cursor(&game);
                /* Clear status bar area on full redraw (returning from
                   help/human view leaves stale pixels) */
                draw_filled_rect(0, MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE,
                                 SCREEN_WIDTH, 30, COLOR_BLACK);
                draw_menu_bar(&game);
                draw_ui(&game);
                first_render = 0;
                needs_ui_update = 0;
            } else if (cursor_changed || game.current_tool != last_tool) {
                /* Erase old cursor by redrawing all tiles in old footprint */
                {
                    int sz = get_tool_size(last_tool);
                    int half = sz / 2;
                    int dy, dx;
                    for (dy = -half; dy <= half; dy++) {
                        for (dx = -half; dx <= half; dx++) {
                            int ry = (int)old_cursor_y + dy;
                            int rx = (int)old_cursor_x + dx;
                            if (ry >= 0 && ry < MAP_HEIGHT &&
                                rx >= 0 && rx < MAP_WIDTH)
                                draw_single_tile(&game, (unsigned short)rx,
                                                 (unsigned short)ry);
                        }
                    }
                }
                /* Draw new cursor */
                draw_cursor(&game);
            } else if (game.map[game.cursor_y][game.cursor_x].type != old_tile) {
                /* Tile was placed — redraw 5x5 area to cover 3x3 zones + neighbors */
                {
                    int dy, dx;
                    for (dy = -2; dy <= 2; dy++) {
                        for (dx = -2; dx <= 2; dx++) {
                            int ry = (int)game.cursor_y + dy;
                            int rx = (int)game.cursor_x + dx;
                            if (ry >= 0 && ry < MAP_HEIGHT &&
                                rx >= 0 && rx < MAP_WIDTH)
                                draw_single_tile(&game, (unsigned short)rx,
                                                 (unsigned short)ry);
                        }
                    }
                }
                draw_cursor(&game);
            } else {
                /* Auto-redraw tiles whose population changed */
                if (game.zoom_level == 0) {
                    for (snap_y = 0; snap_y < VIEW_TILES_Y; snap_y++) {
                        for (snap_x = 0; snap_x < VIEW_TILES_X; snap_x++) {
                            unsigned short mx = game.scroll_x + snap_x;
                            unsigned short my = game.scroll_y + snap_y;
                            if (mx < MAP_WIDTH && my < MAP_HEIGHT &&
                                game.map[my][mx].population != pop_snap[snap_y][snap_x]) {
                                draw_single_tile(&game, mx, my);
                                pop_changed = 1;
                            }
                        }
                    }
                    if (pop_changed)
                        draw_cursor(&game);
                }

                /* Flash: 8-frame cycle. Cursor on for 0-5, off for 6-7.
                   Redraw tiles at transitions (frame 0 = on, frame 6 = off). */
                flash_timer++;
                if (flash_timer >= 8) flash_timer = 0;
                if (flash_timer == 0 || flash_timer == 6) {
                    int sz, half, dy, dx;
                    sz = get_tool_size(game.current_tool);
                    half = sz / 2;
                    for (dy = -half; dy <= half; dy++) {
                        for (dx = -half; dx <= half; dx++) {
                            int ry = (int)game.cursor_y + dy;
                            int rx = (int)game.cursor_x + dx;
                            if (ry >= 0 && ry < MAP_HEIGHT &&
                                rx >= 0 && rx < MAP_WIDTH)
                                draw_single_tile(&game, (unsigned short)rx,
                                                 (unsigned short)ry);
                        }
                    }
                    if (flash_timer == 0)
                        draw_cursor(&game);
                }
            }

            if (needs_ui_update) {
                draw_ui(&game);
                draw_menu_bar(&game);
            }

            /* Menu state changes */
            if (game.menu_active != last_menu_active ||
                game.menu_selected != last_menu_selected ||
                game.menu_drop_sel != last_menu_drop_sel) {
                /* Menu switched or closed: full redraw to clear old dropdown */
                if ((last_menu_active && !game.menu_active) ||
                    (last_menu_active && game.menu_active &&
                     game.menu_selected != last_menu_selected)) {
                    if (game.zoom_level > 0)
                        draw_map_zoomed(&game);
                    else
                        draw_map(&game);
                    draw_cursor(&game);
                    draw_filled_rect(0, MAP_Y_OFFSET + VIEW_TILES_Y * TILE_SIZE,
                                     SCREEN_WIDTH, 30, COLOR_BLACK);
                    draw_ui(&game);
                }
                draw_menu_bar(&game);
                if (game.menu_active) {
                    draw_menu_dropdown(&game);
                }
                last_menu_active = game.menu_active;
                last_menu_selected = game.menu_selected;
                last_menu_drop_sel = game.menu_drop_sel;
            }

            last_game_time = game.game_time;
            last_funds = game.funds;
            last_tool = game.current_tool;
            old_zoom_level = game.zoom_level;
        } else if (game.game_state == STATE_HUMAN_VIEW) {
            draw_human_view(&game);
        } else if (game.game_state == STATE_MENU) {
            draw_help_screen();
            /* Wait for F1 or ESC to exit help */
            while (kbhit()) getch();
            while (1) {
                if (kbhit()) {
                    int k = getch();
                    if (k == 0 || k == 0xE0) {
                        k = getch();
                        if (k == 59) break; /* F1 */
                    } else if (k == 27) {
                        break; /* ESC */
                    }
                }
                delay(50);
            }
            game.game_state = STATE_CITY_VIEW;
            first_render = 1; /* Force full redraw when returning */
        }

        /* Small delay to control frame rate */
        delay(50);
    }

    /* Return to text mode */
    set_text_mode();

    printf("Thanks for playing CitySim!\n");
    printf("Final Statistics:\n");
    printf("  Population: %u\n", game.population);
    printf("  Funds: $%ld\n", game.funds);
    printf("  Days Played: %u\n", game.game_day);

    return 0;
}
