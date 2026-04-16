/*
 * game2048.c - Ready OS 2048 game (PETASCII text mode)
 *
 * Controls:
 * - Cursor keys or WASD: move
 * - SPACE: pause/resume
 * - R: restart (from pause menu or after game over)
 * - CTRL+B: return to launcher
 * - F2/F4: switch apps
 */

#include "../../lib/tui.h"
#include "../../lib/resume_state.h"
#include <c64.h>
#include <conio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define GRID_SIZE 4
#define CELL_COUNT 16

#define HEADER_Y 0
#define HEADER_H 3
#define SUBTITLE_Y 1
#define HELP_Y 24

#define BOARD_X 1
#define BOARD_Y 3
#define CELL_W 8
#define CELL_H 4
#define BOARD_W 37
#define BOARD_H 21

#define PAUSE_X 4
#define PAUSE_Y 4
#define PAUSE_W 32
#define PAUSE_H 17
#define PAUSE_AREA (PAUSE_W * PAUSE_H)

#define WIN_EXPONENT 11 /* 2^11 = 2048 */

/* Shim data address used by existing apps */
#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/* Entropy taps */
#define RASTER_LINE (*(volatile unsigned char*)0xD012)
#define JIFFY_LO    (*(volatile unsigned char*)0xA2)

/*---------------------------------------------------------------------------
 * Static State
 *---------------------------------------------------------------------------*/

static unsigned char board[CELL_COUNT];
static unsigned char draw_cache[CELL_COUNT];
static unsigned char pause_saved_screen[PAUSE_AREA];
static unsigned char pause_saved_color[PAUSE_AREA];

static unsigned long score;
static unsigned long best_score;
static unsigned int rng_state;

static unsigned char running;
static unsigned char paused;
static unsigned char game_over;
static unsigned char won_2048;
static unsigned char dirty_board_full;
static unsigned char resume_ready;

typedef struct {
    unsigned char board[CELL_COUNT];
    unsigned long score;
    unsigned long best_score;
    unsigned int rng_state;
    unsigned char game_over;
    unsigned char won_2048;
} Game2048ResumeV1;

static Game2048ResumeV1 resume_blob;

/*---------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

static void draw_field(unsigned char x, unsigned char y, unsigned char width,
                       const char *text, unsigned char color) {
    tui_puts_n(x, y, text, width, color);
}

static void draw_centered_field(unsigned char x, unsigned char y, unsigned char width,
                                const char *text, unsigned char color) {
    unsigned char text_len;
    unsigned char draw_x;

    text_len = (unsigned char)strlen(text);
    tui_clear_line(y, x, width, color);

    if (text_len >= width) {
        draw_x = x;
        text_len = width;
    } else {
        draw_x = (unsigned char)(x + ((width - text_len) / 2));
    }

    draw_field(draw_x, y, text_len, text, color);
}

static void draw_right_field(unsigned char x, unsigned char y, unsigned char width,
                             const char *text, unsigned char color) {
    unsigned char text_len;
    unsigned char draw_x;

    text_len = (unsigned char)strlen(text);
    tui_clear_line(y, x, width, color);

    if (text_len >= width) {
        draw_x = x;
        text_len = width;
    } else {
        draw_x = (unsigned char)(x + width - text_len);
    }

    draw_field(draw_x, y, text_len, text, color);
}

static unsigned char cell_x(unsigned char col) {
    return (unsigned char)(BOARD_X + 1 + col * (CELL_W + 1));
}

static unsigned char cell_y(unsigned char row) {
    return (unsigned char)(BOARD_Y + 1 + row * (CELL_H + 1));
}

static unsigned long pow2_from_exp(unsigned char exp) {
    unsigned long value = 1UL;
    while (exp > 0) {
        value <<= 1;
        --exp;
    }
    return value;
}

static void u32_to_ascii(unsigned long value, char *out) {
    char rev[11];
    unsigned char count, i;

    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }

    count = 0;
    while (value > 0 && count < 10) {
        rev[count] = (char)('0' + (value % 10));
        value /= 10;
        ++count;
    }

    for (i = 0; i < count; ++i) {
        out[i] = rev[count - 1 - i];
    }
    out[count] = 0;
}

static unsigned char random8(void) {
    unsigned int x = rng_state;

    x ^= (unsigned int)RASTER_LINE;
    x ^= (unsigned int)JIFFY_LO << 8;
    x ^= (x << 7);
    x ^= (x >> 9);
    x ^= (x << 8);

    rng_state = x;
    return (unsigned char)x;
}

static unsigned char tile_color(unsigned char exp) {
    switch (exp) {
        case 0:  return TUI_COLOR_GRAY1;
        case 1:  return TUI_COLOR_LIGHTGREEN; /* 2 */
        case 2:  return TUI_COLOR_YELLOW;     /* 4 */
        case 3:  return TUI_COLOR_ORANGE;     /* 8 */
        case 4:  return TUI_COLOR_LIGHTRED;   /* 16 */
        case 5:  return TUI_COLOR_RED;        /* 32 */
        case 6:  return TUI_COLOR_PURPLE;     /* 64 */
        case 7:  return TUI_COLOR_CYAN;       /* 128 */
        case 8:  return TUI_COLOR_LIGHTBLUE;  /* 256 */
        case 9:  return TUI_COLOR_WHITE;      /* 512 */
        case 10: return TUI_COLOR_BROWN;      /* 1024 */
        default: return TUI_COLOR_GRAY3;      /* 2048+ */
    }
}

static void draw_header(void) {
    TuiRect header = {0, HEADER_Y, 40, HEADER_H};

    tui_window_title(&header, "2048", TUI_COLOR_GRAY3, TUI_COLOR_CYAN);
}

static void draw_subheader(void) {
    char score_buf[11];
    char best_buf[11];

    u32_to_ascii(score, score_buf);
    u32_to_ascii(best_score, best_buf);

    tui_clear_line(SUBTITLE_Y, 1, 38, TUI_COLOR_WHITE);
    draw_field(2, SUBTITLE_Y, 5, "score", TUI_COLOR_GRAY3);
    draw_right_field(8, SUBTITLE_Y, 5, score_buf, TUI_COLOR_WHITE);
    draw_field(21, SUBTITLE_Y, 10, "high score", TUI_COLOR_GRAY3);
    draw_right_field(31, SUBTITLE_Y, 7, best_buf,
                     won_2048 ? TUI_COLOR_LIGHTGREEN : TUI_COLOR_WHITE);
}

static void draw_help_line(void) {
    if (game_over) {
        draw_centered_field(0, HELP_Y, 40,
                            "GAME OVER  R RESTART  F2/F4 APPS",
                            TUI_COLOR_LIGHTRED);
    } else if (won_2048) {
        draw_centered_field(0, HELP_Y, 40,
                            "2048!  KEEP GOING  F2/F4 APPS",
                            TUI_COLOR_LIGHTGREEN);
    } else {
        draw_centered_field(0, HELP_Y, 40,
                            "ARROWS/WASD MOVE  SPACE PAUSE  F2/F4 APPS",
                            TUI_COLOR_GRAY3);
    }
}

static void draw_board_frame(void) {
    unsigned char i, j;
    unsigned char x, y;
    TuiRect frame = {BOARD_X, BOARD_Y, BOARD_W, BOARD_H};

    tui_window(&frame, TUI_COLOR_GRAY3);

    for (i = 1; i < GRID_SIZE; ++i) {
        x = (unsigned char)(BOARD_X + i * CELL_W + i);
        tui_vline(x, BOARD_Y + 1, BOARD_H - 2, TUI_COLOR_GRAY3);
    }

    for (j = 1; j < GRID_SIZE; ++j) {
        y = (unsigned char)(BOARD_Y + j * CELL_H + j);
        tui_hline(BOARD_X + 1, y, BOARD_W - 2, TUI_COLOR_GRAY3);
    }

    for (j = 1; j < GRID_SIZE; ++j) {
        y = (unsigned char)(BOARD_Y + j * CELL_H + j);
        for (i = 1; i < GRID_SIZE; ++i) {
            x = (unsigned char)(BOARD_X + i * CELL_W + i);
            tui_putc(x, y, TUI_CROSS, TUI_COLOR_GRAY3);
        }
    }
}

static void draw_tile(unsigned char idx) {
    unsigned char row, col;
    unsigned char x, y;
    unsigned char exp;
    unsigned char color;
    unsigned char fill_ch;
    unsigned char dx, dy;
    unsigned int offset;
    unsigned long value;
    char value_buf[11];
    unsigned char len, tx, ty, i;

    row = idx >> 2;
    col = idx & 0x03;
    exp = board[idx];

    x = cell_x(col);
    y = cell_y(row);
    color = tile_color(exp);
    fill_ch = (exp == 0) ? 32 : 0xA0;

    for (dy = 0; dy < CELL_H; ++dy) {
        offset = (unsigned int)(y + dy) * 40 + x;
        for (dx = 0; dx < CELL_W; ++dx) {
            TUI_SCREEN[offset + dx] = fill_ch;
            TUI_COLOR_RAM[offset + dx] = color;
        }
    }

    if (exp == 0) {
        return;
    }

    value = pow2_from_exp(exp);
    u32_to_ascii(value, value_buf);
    len = (unsigned char)strlen(value_buf);
    if (len > CELL_W) {
        len = CELL_W;
    }

    tx = (unsigned char)(x + (CELL_W - len) / 2);
    ty = (unsigned char)(y + (CELL_H / 2));
    offset = (unsigned int)ty * 40 + tx;

    for (i = 0; i < len; ++i) {
        TUI_SCREEN[offset + i] = (unsigned char)(tui_ascii_to_screen(value_buf[i]) | 0x80);
        TUI_COLOR_RAM[offset + i] = color;
    }
}

static void draw_changed_tiles(void) {
    unsigned char i;

    for (i = 0; i < CELL_COUNT; ++i) {
        if (dirty_board_full || draw_cache[i] != board[i]) {
            draw_tile(i);
            draw_cache[i] = board[i];
        }
    }

    dirty_board_full = 0;
}

static void save_pause_background(void) {
    unsigned char x, y;
    unsigned int src;
    unsigned int dst = 0;

    for (y = 0; y < PAUSE_H; ++y) {
        src = (unsigned int)(PAUSE_Y + y) * 40 + PAUSE_X;
        for (x = 0; x < PAUSE_W; ++x) {
            pause_saved_screen[dst] = TUI_SCREEN[src + x];
            pause_saved_color[dst] = TUI_COLOR_RAM[src + x];
            ++dst;
        }
    }
}

static void restore_pause_background(void) {
    unsigned char x, y;
    unsigned int dst;
    unsigned int src = 0;

    for (y = 0; y < PAUSE_H; ++y) {
        dst = (unsigned int)(PAUSE_Y + y) * 40 + PAUSE_X;
        for (x = 0; x < PAUSE_W; ++x) {
            TUI_SCREEN[dst + x] = pause_saved_screen[src];
            TUI_COLOR_RAM[dst + x] = pause_saved_color[src];
            ++src;
        }
    }
}

static void draw_pause_overlay(void) {
    TuiRect popup = {PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H};

    save_pause_background();
    tui_window_title(&popup, "PAUSED", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(7, PAUSE_Y + 3, "SPACE  RESUME", TUI_COLOR_WHITE);
    tui_puts(7, PAUSE_Y + 5, "R      RESTART", TUI_COLOR_WHITE);
    tui_puts(7, PAUSE_Y + 8, "ARROWS/WASD  MOVE", TUI_COLOR_GRAY3);
    tui_puts(7, PAUSE_Y + 10, "F2/F4        SWITCH APPS", TUI_COLOR_GRAY3);
    tui_puts(7, PAUSE_Y + 11, "CTRL+B       LAUNCHER", TUI_COLOR_GRAY3);
    tui_puts(7, PAUSE_Y + 13, "GOAL: MAKE 2048", TUI_COLOR_LIGHTGREEN);
}

static void spawn_tile(void) {
    unsigned char empties[CELL_COUNT];
    unsigned char empty_count;
    unsigned char i;
    unsigned char pick;
    unsigned char exp;

    empty_count = 0;
    for (i = 0; i < CELL_COUNT; ++i) {
        if (board[i] == 0) {
            empties[empty_count] = i;
            ++empty_count;
        }
    }

    if (empty_count == 0) {
        return;
    }

    pick = (unsigned char)(random8() % empty_count);
    exp = ((random8() % 10) == 0) ? 2 : 1;
    board[empties[pick]] = exp;
}

static unsigned char can_move(void) {
    unsigned char r, c;
    unsigned char idx;

    for (idx = 0; idx < CELL_COUNT; ++idx) {
        if (board[idx] == 0) {
            return 1;
        }
    }

    for (r = 0; r < GRID_SIZE; ++r) {
        for (c = 0; c + 1 < GRID_SIZE; ++c) {
            idx = (unsigned char)(r * 4 + c);
            if (board[idx] == board[idx + 1]) {
                return 1;
            }
        }
    }

    for (c = 0; c < GRID_SIZE; ++c) {
        for (r = 0; r + 1 < GRID_SIZE; ++r) {
            idx = (unsigned char)(r * 4 + c);
            if (board[idx] == board[idx + 4]) {
                return 1;
            }
        }
    }

    return 0;
}

static unsigned char slide_line(const unsigned char *in, unsigned char *out, unsigned long *gain) {
    unsigned char tmp[4];
    unsigned char count;
    unsigned char i, j;
    unsigned char changed;

    count = 0;
    for (i = 0; i < 4; ++i) {
        if (in[i] != 0) {
            tmp[count] = in[i];
            ++count;
        }
    }
    while (count < 4) {
        tmp[count] = 0;
        ++count;
    }

    for (i = 0; i < 3; ++i) {
        if (tmp[i] != 0 && tmp[i] == tmp[i + 1]) {
            ++tmp[i];
            *gain += pow2_from_exp(tmp[i]);
            for (j = i + 1; j < 3; ++j) {
                tmp[j] = tmp[j + 1];
            }
            tmp[3] = 0;
        }
    }

    changed = 0;
    for (i = 0; i < 4; ++i) {
        out[i] = tmp[i];
        if (out[i] != in[i]) {
            changed = 1;
        }
    }

    return changed;
}

static unsigned char apply_move(unsigned char dir) {
    unsigned char line;
    unsigned char i;
    unsigned char in[4];
    unsigned char out[4];
    unsigned char changed;
    unsigned char move_changed;
    unsigned char r, c;
    unsigned char idx;
    unsigned long gain;

    move_changed = 0;
    gain = 0;

    for (line = 0; line < 4; ++line) {
        if (dir == 0 || dir == 1) {
            c = line;
            for (i = 0; i < 4; ++i) {
                r = (dir == 0) ? i : (unsigned char)(3 - i);
                in[i] = board[(unsigned char)(r * 4 + c)];
            }
            changed = slide_line(in, out, &gain);
            if (changed) {
                for (i = 0; i < 4; ++i) {
                    r = (dir == 0) ? i : (unsigned char)(3 - i);
                    board[(unsigned char)(r * 4 + c)] = out[i];
                }
                move_changed = 1;
            }
        } else {
            r = line;
            for (i = 0; i < 4; ++i) {
                c = (dir == 2) ? i : (unsigned char)(3 - i);
                in[i] = board[(unsigned char)(r * 4 + c)];
            }
            changed = slide_line(in, out, &gain);
            if (changed) {
                for (i = 0; i < 4; ++i) {
                    c = (dir == 2) ? i : (unsigned char)(3 - i);
                    board[(unsigned char)(r * 4 + c)] = out[i];
                }
                move_changed = 1;
            }
        }
    }

    if (!move_changed) {
        return 0;
    }

    score += gain;
    if (score > best_score) {
        best_score = score;
    }

    spawn_tile();

    if (!won_2048) {
        for (idx = 0; idx < CELL_COUNT; ++idx) {
            if (board[idx] >= WIN_EXPONENT) {
                won_2048 = 1;
                break;
            }
        }
    }

    if (!can_move()) {
        game_over = 1;
    }

    return 1;
}

static void reset_game(void) {
    unsigned char i;

    for (i = 0; i < CELL_COUNT; ++i) {
        board[i] = 0;
        draw_cache[i] = 0xFF;
    }

    score = 0;
    paused = 0;
    game_over = 0;
    won_2048 = 0;
    dirty_board_full = 1;

    spawn_tile();
    spawn_tile();
}

static signed char key_to_direction(unsigned char key) {
    switch (key) {
        case TUI_KEY_UP:
        case 'w':
        case 'W':
            return 0;
        case TUI_KEY_DOWN:
        case 's':
        case 'S':
            return 1;
        case TUI_KEY_LEFT:
        case 'a':
        case 'A':
            return 2;
        case TUI_KEY_RIGHT:
        case 'd':
        case 'D':
            return 3;
        default:
            return -1;
    }
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    memcpy(resume_blob.board, board, sizeof(board));
    resume_blob.score = score;
    resume_blob.best_score = best_score;
    resume_blob.rng_state = rng_state;
    resume_blob.game_over = game_over;
    resume_blob.won_2048 = won_2048;
    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;
    unsigned char i;
    unsigned char has_tile = 0;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_try_load(&resume_blob, sizeof(resume_blob), &payload_len)) {
        return 0;
    }
    if (payload_len != sizeof(resume_blob)) {
        return 0;
    }

    for (i = 0; i < CELL_COUNT; ++i) {
        if (resume_blob.board[i] > 15) {
            return 0;
        }
        if (resume_blob.board[i] != 0) {
            has_tile = 1;
        }
    }
    if (!has_tile) {
        return 0;
    }

    memcpy(board, resume_blob.board, sizeof(board));
    score = resume_blob.score;
    best_score = resume_blob.best_score;
    if (best_score < score) {
        best_score = score;
    }
    rng_state = resume_blob.rng_state;
    if (rng_state == 0) {
        rng_state = (unsigned int)0xACE1 ^ ((unsigned int)RASTER_LINE << 8) ^ JIFFY_LO;
    }
    game_over = (resume_blob.game_over != 0);
    won_2048 = (resume_blob.won_2048 != 0);
    paused = 0;

    for (i = 0; i < CELL_COUNT; ++i) {
        draw_cache[i] = 0xFF;
    }
    dirty_board_full = 1;

    if (!can_move()) {
        game_over = 1;
    }
    return 1;
}

static unsigned char handle_app_switch(unsigned char key) {
    unsigned char nav_action;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        resume_save_state();
        tui_return_to_launcher();
        return 1;
    }
    if (nav_action >= 1 && nav_action <= 23) {
        resume_save_state();
        tui_switch_to_app(nav_action);
        return 1;
    }
    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * Main loop
 *---------------------------------------------------------------------------*/

static void game_draw_initial(void) {
    tui_clear(TUI_COLOR_BLACK);
    VIC.bordercolor = TUI_COLOR_BLACK;
    draw_header();
    draw_subheader();
    draw_board_frame();
    draw_help_line();
    draw_changed_tiles();
}

static void update_keyrepeat_mode(void) {
    if (!paused && !game_over) {
        (void)tui_keyrepeat_set(TUI_KEYREPEAT_CURSOR);
    } else {
        tui_keyrepeat_default();
    }
}

static void game_loop(void) {
    unsigned char key;
    signed char dir;

    while (running) {
        key = tui_getkey();
        if (handle_app_switch(key)) {
            continue;
        }

        if (key == TUI_KEY_RUNSTOP) {
            running = 0;
            break;
        }

        if (paused) {
            if (key == ' ') {
                paused = 0;
                update_keyrepeat_mode();
                restore_pause_background();
                draw_subheader();
            } else if (key == 'r' || key == 'R') {
                paused = 0;
                restore_pause_background();
                reset_game();
                update_keyrepeat_mode();
                draw_subheader();
                draw_help_line();
                draw_changed_tiles();
            }
            continue;
        }

        if (key == ' ') {
            paused = 1;
            update_keyrepeat_mode();
            draw_subheader();
            draw_pause_overlay();
            continue;
        }

        if (game_over && (key == 'r' || key == 'R')) {
            reset_game();
            update_keyrepeat_mode();
            draw_subheader();
            draw_help_line();
            draw_changed_tiles();
            continue;
        }

        if (game_over) {
            continue;
        }

        dir = key_to_direction(key);
        if (dir >= 0) {
            if (apply_move((unsigned char)dir)) {
                update_keyrepeat_mode();
                draw_subheader();
                draw_help_line();
                draw_changed_tiles();
            }
        }
    }
}

int main(void) {
    unsigned char bank;

    rng_state = (unsigned int)0xACE1 ^ ((unsigned int)RASTER_LINE << 8) ^ JIFFY_LO;
    running = 1;
    resume_ready = 0;

    tui_init();
    reset_game();

    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 23) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();
    update_keyrepeat_mode();

    game_draw_initial();
    game_loop();

    __asm__("jmp $FCE2");
    return 0;
}
