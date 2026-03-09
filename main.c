#include "citysim.h"

int main(void) {
    static GameState game;
    static MouseState mouse;
    int running = 1;
    int first_render = 1;
    unsigned int last_game_time = 0;
    long last_funds = 0;
    unsigned char last_tool = 0;
    unsigned char last_density = 0;
    unsigned char old_zoom_level = 0;
    unsigned int flash_timer = 0;
    int has_mouse = 0;
    unsigned char last_buttons = 0;
    unsigned short last_mouse_x = 0;
    unsigned short last_mouse_y = 0;
    unsigned char last_menu_active = 0;
    unsigned char last_menu_selected = 0;
    signed char last_menu_drop_sel = -1;
    unsigned char last_overlay_flags = 0;
    unsigned char pop_snap[VIEW_TILES_Y][VIEW_TILES_X];
    int snap_x, snap_y, pop_changed;
    /* Enter graphics mode */
    init_ega();

    /* Try to init mouse; hide hardware cursor (we draw our own) */
    has_mouse = mouse_init();
    if (has_mouse)
        mouse_hide();

    /* Splash screen — count wait loops for random seed entropy */
    draw_splash_screen();
    while (1) {
        flash_timer++;  /* reuse as entropy counter */
        if (kbhit()) {
            int sk = getch();
            if (sk == '1') {
                /* New city — pick difficulty */
                init_game(&game);
                draw_difficulty_screen();
                while (1) {
                    flash_timer++;
                    if (kbhit()) {
                        int dk = getch();
                        if (dk == '1') { game.difficulty = DIFFICULTY_EASY;   break; }
                        if (dk == '2') { game.difficulty = DIFFICULTY_MEDIUM; break; }
                        if (dk == '3') { game.difficulty = DIFFICULTY_HARD;   break; }
                    }
                    delay(50);
                }
                /* Generate terrain — flash_timer varies based on user wait time */
                generate_terrain(&game, flash_timer);
                break;
            } else if (sk == '2') {
                init_game(&game);
                if (!load_game(&game, "CITYSIM.SAV")) {
                    /* Load failed — start new game */
                    init_game(&game);
                }
                break;
            } else if (sk == '3') {
                set_text_mode();
                return 0;
            }
        }
        delay(50);
    }
    last_funds = game.funds;
    last_tool = game.current_tool;

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

        /* Year-end popups: newspaper first, then budget (if not auto) */
        if (game.game_state == STATE_CITY_VIEW && !game.menu_active) {
            if (game.pending_newspaper) {
                game.pending_newspaper = 0;
                game.game_state = STATE_NEWSPAPER;
            } else if (game.pending_budget) {
                game.pending_budget = 0;
                game.game_state = STATE_BUDGET;
            }
        }

        /* Update game logic (paused while menu open, popup showing, or speed==0) */
        if (!game.menu_active && !game.query_popup && game.game_speed > 0) {
            /* speed 1=slow(1x), 2=normal(2x), 3=fast(5x), 4=fastest(20x) */
            static const int updates_per_frame[] = { 0, 1, 2, 5, 20 };
            int updates = updates_per_frame[game.game_speed];
            int u;
            for (u = 0; u < updates; u++)
                update_game(&game);
        }

        /* Track real playtime */
        game.play_ticks++;

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
                                game.menu_drop_sel = menu_first_selectable(mi, game.menu_expand);
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
                } else if (!(mouse.buttons & 1) &&
                           (mouse.pixel_x != last_mouse_x ||
                            mouse.pixel_y != last_mouse_y)) {
                    /* Track mouse hover in dropdown (only when mouse moves) */
                    int drop_x = menu_label_x[game.menu_selected];
                    int drop_w = 180;
                    int drop_h = menu_counts[game.menu_selected] * 16 + 2;
                    if ((int)mouse.pixel_y < MENU_BAR_HEIGHT) {
                        /* Hovering over menu bar: switch menus */
                        int mi;
                        for (mi = NUM_MENUS - 1; mi >= 0; mi--) {
                            if ((int)mouse.pixel_x >= menu_label_x[mi]) {
                                game.menu_selected = (unsigned char)mi;
                                game.menu_drop_sel = menu_first_selectable(mi, game.menu_expand);
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
                                game.menu_drop_sel = menu_first_selectable(mi, game.menu_expand);
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
            game.current_density != last_density ||
            cursor_changed ||
            zoom_changed) {
            needs_ui_update = 1;
        }

        if (game.game_state == STATE_CITY_VIEW) {
            if (game.needs_redraw) {
                first_render = 1;
                game.needs_redraw = 0;
            }

            /* Query popup: draw once then freeze rendering until dismissed */
            {
                static unsigned char popup_was_up = 0;
                if (game.query_popup) {
                    if (!popup_was_up) {
                        draw_query_popup(&game);
                        popup_was_up = 1;
                    }
                    delay(50);
                    continue;
                }
                if (popup_was_up) {
                    popup_was_up = 0;
                    first_render = 1; /* redraw map after popup dismissed */
                }
            }
            if (first_render || scroll_changed || zoom_changed) {
                /* Full map redraw */
                if (game.zoom_level > 0) {
                    draw_map_zoomed(&game);
                } else {
                    draw_map(&game);
                }
                draw_overlays(&game);
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
                    if (pop_changed && !game.menu_active)
                        draw_cursor(&game);
                }

                /* Flash: 8-frame cycle. Cursor on for 0-5, off for 6-7.
                   Redraw tiles at transitions (frame 0 = on, frame 6 = off).
                   Skip entirely while menu is open to avoid obscuring it. */
                if (!game.menu_active) {
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
            }

            if (needs_ui_update) {
                draw_ui(&game);
                draw_menu_bar(&game);
            }

            /* Menu state changes */
            if (game.menu_active != last_menu_active ||
                game.menu_selected != last_menu_selected ||
                game.menu_drop_sel != last_menu_drop_sel ||
                game.overlay_flags != last_overlay_flags) {
                /* Menu switched or closed: full redraw to clear old dropdown */
                if ((last_menu_active && !game.menu_active) ||
                    (last_menu_active && game.menu_active &&
                     game.menu_selected != last_menu_selected) ||
                    game.overlay_flags != last_overlay_flags) {
                    if (game.zoom_level > 0)
                        draw_map_zoomed(&game);
                    else
                        draw_map(&game);
                    draw_overlays(&game);
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
                last_overlay_flags = game.overlay_flags;
            }

            last_game_time = game.game_time;
            last_funds = game.funds;
            last_tool = game.current_tool;
            last_density = game.current_density;
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
        } else if (game.game_state == STATE_ABOUT) {
            draw_about_screen(&game);
            /* Wait for any key to dismiss */
            while (kbhit()) getch();
            while (1) {
                if (kbhit()) {
                    int k = getch();
                    if (k == 0 || k == 0xE0) getch(); /* consume extended */
                    break;
                }
                delay(50);
            }
            game.game_state = STATE_CITY_VIEW;
            first_render = 1;
        } else if (game.game_state == STATE_BUDGET) {
            /* Budget screen with department funding controls */
            {
                unsigned char budget_dept = 0; /* 0=tax, 1=police, 2=fire, 3=health, 4=education */
                draw_budget_screen(&game);
                while (kbhit()) getch();
                while (1) {
                    if (kbhit()) {
                        int k = getch();
                        unsigned char *fund_ptr = NULL;
                        if (k == 0 || k == 0xE0) {
                            getch(); /* consume extended */
                        } else if (k == 27 || k == 13) {
                            break;
                        } else if (k == 't' || k == 'T') {
                            budget_dept = 0;
                        } else if (k == '1') {
                            budget_dept = 1;
                        } else if (k == '2') {
                            budget_dept = 2;
                        } else if (k == '3') {
                            budget_dept = 3;
                        } else if (k == '4') {
                            budget_dept = 4;
                        } else if (k == '+' || k == '=') {
                            if (budget_dept == 0) {
                                if (game.city_tax < 20) game.city_tax++;
                            } else {
                                switch (budget_dept) {
                                    case 1: fund_ptr = &game.fund_police; break;
                                    case 2: fund_ptr = &game.fund_fire; break;
                                    case 3: fund_ptr = &game.fund_health; break;
                                    case 4: fund_ptr = &game.fund_education; break;
                                }
                                if (fund_ptr && *fund_ptr < 100)
                                    *fund_ptr = (*fund_ptr <= 90) ? *fund_ptr + 10 : 100;
                            }
                            draw_budget_screen(&game);
                        } else if (k == '-' || k == '_') {
                            if (budget_dept == 0) {
                                if (game.city_tax > 0) game.city_tax--;
                            } else {
                                switch (budget_dept) {
                                    case 1: fund_ptr = &game.fund_police; break;
                                    case 2: fund_ptr = &game.fund_fire; break;
                                    case 3: fund_ptr = &game.fund_health; break;
                                    case 4: fund_ptr = &game.fund_education; break;
                                }
                                if (fund_ptr && *fund_ptr >= 10)
                                    *fund_ptr -= 10;
                                else if (fund_ptr)
                                    *fund_ptr = 0;
                            }
                            draw_budget_screen(&game);
                        }
                    }
                    delay(50);
                }
            }
            /* If newspaper pending, show it next; otherwise return to city */
            if (game.pending_newspaper) {
                game.pending_newspaper = 0;
                game.game_state = STATE_NEWSPAPER;
            } else {
                game.game_state = STATE_CITY_VIEW;
            }
            first_render = 1;
        } else if (game.game_state == STATE_NEWSPAPER) {
            draw_newspaper(&game);
            while (kbhit()) getch();
            while (1) {
                if (kbhit()) {
                    int k = getch();
                    if (k == 0 || k == 0xE0) getch();
                    break;
                }
                delay(50);
            }
            /* If budget pending, show it next */
            if (game.pending_budget) {
                game.pending_budget = 0;
                game.game_state = STATE_BUDGET;
            } else {
                game.game_state = STATE_CITY_VIEW;
            }
            first_render = 1;
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
