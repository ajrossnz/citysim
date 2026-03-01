#include "citysim.h"

int main(void) {
    static GameState game;
    int running = 1;
    int first_render = 1;
    unsigned int last_game_time = 0;
    long last_funds = 0;
    unsigned char last_tool = 0;
    /* Initialize game */
    init_game(&game);
    last_funds = game.funds;
    last_tool = game.current_tool;

    /* Enter graphics mode */
    init_ega();

    /* Main game loop */
    while (running) {
        int needs_ui_update = 0;
        int scroll_changed = 0;
        int cursor_changed = 0;
        unsigned short old_cursor_x = game.cursor_x;
        unsigned short old_cursor_y = game.cursor_y;
        unsigned short old_scroll_x = game.scroll_x;
        unsigned short old_scroll_y = game.scroll_y;
        unsigned char old_tile;

        /* Update game logic */
        update_game(&game);

        /* Remember tile under cursor before input */
        old_tile = game.map[game.cursor_y][game.cursor_x].type;

        /* Handle input */
        handle_input(&game);

        /* Detect what changed */
        scroll_changed = (game.scroll_x != old_scroll_x ||
                          game.scroll_y != old_scroll_y);
        cursor_changed = (game.cursor_x != old_cursor_x ||
                          game.cursor_y != old_cursor_y);

        /* Check if UI needs updating */
        if (game.game_time != last_game_time ||
            game.funds != last_funds ||
            game.current_tool != last_tool ||
            cursor_changed) {
            needs_ui_update = 1;
        }

        if (game.game_state == STATE_CITY_VIEW) {
            if (first_render || scroll_changed) {
                /* Full map redraw without clear_screen */
                draw_map(&game);
                draw_cursor(&game);
                draw_ui(&game);
                first_render = 0;
                needs_ui_update = 0;
            } else if (cursor_changed) {
                /* Erase old cursor by redrawing the old tile */
                draw_single_tile(&game, old_cursor_x, old_cursor_y);
                /* Draw new cursor */
                draw_cursor(&game);
            } else if (game.map[game.cursor_y][game.cursor_x].type != old_tile) {
                /* Tile was placed at cursor position */
                draw_single_tile(&game, game.cursor_x, game.cursor_y);
                draw_cursor(&game);
            }

            if (needs_ui_update) {
                draw_ui(&game);
            }

            last_game_time = game.game_time;
            last_funds = game.funds;
            last_tool = game.current_tool;
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
