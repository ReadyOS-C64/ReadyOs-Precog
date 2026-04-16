/*
 * sidetris.c - ReadyOS Sidetris game (PETSCII text mode)
 *
 * Controls:
 * - W/UP: move piece up
 * - S/DOWN: move piece down
 * - A/LEFT: rotate counter-clockwise
 * - D/RIGHT: rotate clockwise
 * - SPACE: hard-drop to the lock column
 * - P: pause/resume
 * - R: restart run
 * - M: return to main menu
 * - CTRL+B: return to launcher
 * - F2/F4: switch apps
 */

#include "../../lib/tui.h"
#include "../../lib/resume_state.h"
#include <c64.h>
#include <conio.h>
#include <string.h>

#define BOARD_W 20
#define BOARD_H 10
#define CELL_COUNT (BOARD_W * BOARD_H)

#define HEADER_Y 0
#define HEADER_H 3
#define INFO_Y 3
#define BOARD_X ((40 - BOARD_FRAME_W) / 2)
#define BOARD_Y 6
#define BOARD_FRAME_W (BOARD_W + 2)
#define BOARD_FRAME_H (BOARD_H + 2)
#define PREVIEW_X (BOARD_X + BOARD_FRAME_W + 1)
#define PREVIEW_Y 8
#define PREVIEW_W 8
#define PREVIEW_H 6
#define STATUS_Y 20
#define GLOBAL_HELP_Y 21
#define HELP_Y 23
#define HELP2_Y 24

#define PAUSE_X 8
#define PAUSE_Y 9
#define PAUSE_W 24
#define PAUSE_H 7

#define CHAR_BLOCK 0xA0
#define CHAR_EMPTY 46
#define CHAR_SPACE 32

#define MODE_MENU 0
#define MODE_PLAY 1
#define MODE_PAUSE 2
#define MODE_OVER 3

#define SPEED_COUNT 10
#define SPEED_DEFAULT 4

#define PIECE_I 0
#define PIECE_O 1
#define PIECE_T 2
#define PIECE_J 3
#define PIECE_L 4
#define PIECE_S 5
#define PIECE_Z 6
#define PIECE_COUNT 7
#define ROTATION_COUNT 4
#define BLOCK_COORD_COUNT 8

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)
#define RASTER_LINE (*(volatile unsigned char*)0xD012)
#define JIFFY_LO    (*(volatile unsigned char*)0xA2)

#define APP_BANK_MIN 1
#define APP_BANK_MAX 23

static unsigned char board_cells[CELL_COUNT];
static unsigned char rebuild_cells[CELL_COUNT];
static unsigned char rendered_cells[CELL_COUNT];
static unsigned char piece_bag[PIECE_COUNT];

static unsigned char mode;
static unsigned char running;
static unsigned char resume_ready;
static unsigned char speed_id;
static unsigned char level;
static unsigned char piece_type;
static unsigned char piece_rotation;
static unsigned char next_piece;
static unsigned char piece_x;
static unsigned char piece_y;
static unsigned char bag_count;
static unsigned char drop_tick;

static unsigned int lines_cleared;
static unsigned int rng_state;
static unsigned long score;
static unsigned long session_best_score;
static unsigned long last_score;

typedef struct {
    unsigned char mode;
    unsigned char speed_id;
    unsigned char level;
    unsigned char piece_type;
    unsigned char piece_rotation;
    unsigned char next_piece;
    unsigned char piece_x;
    unsigned char piece_y;
    unsigned char bag_count;
    unsigned char piece_bag[PIECE_COUNT];
    unsigned int lines_cleared;
    unsigned int rng_state;
    unsigned long score;
    unsigned long session_best_score;
    unsigned long last_score;
    unsigned char board_cells[CELL_COUNT];
} SidetrisResumeV1;

static SidetrisResumeV1 resume_blob;

static const char *speed_names[SPEED_COUNT] = {
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "10"
};

static const unsigned char speed_base_ticks[SPEED_COUNT] = {
    30,
    27,
    24,
    21,
    18,
    15,
    12,
    10,
    8,
    6
};

static const signed char piece_cells[PIECE_COUNT][ROTATION_COUNT][BLOCK_COORD_COUNT] = {
    {
        {0, 0, 1, 0, 2, 0, 3, 0},
        {0, 0, 0, 1, 0, 2, 0, 3},
        {0, 0, 1, 0, 2, 0, 3, 0},
        {0, 0, 0, 1, 0, 2, 0, 3}
    },
    {
        {0, 0, 1, 0, 0, 1, 1, 1},
        {0, 0, 1, 0, 0, 1, 1, 1},
        {0, 0, 1, 0, 0, 1, 1, 1},
        {0, 0, 1, 0, 0, 1, 1, 1}
    },
    {
        {0, 0, 1, 0, 2, 0, 1, 1},
        {0, 1, 1, 0, 1, 1, 1, 2},
        {1, 0, 0, 1, 1, 1, 2, 1},
        {0, 0, 0, 1, 1, 1, 0, 2}
    },
    {
        {0, 0, 0, 1, 1, 1, 2, 1},
        {0, 0, 1, 0, 0, 1, 0, 2},
        {0, 0, 1, 0, 2, 0, 2, 1},
        {1, 0, 1, 1, 0, 2, 1, 2}
    },
    {
        {2, 0, 0, 1, 1, 1, 2, 1},
        {0, 0, 0, 1, 0, 2, 1, 2},
        {0, 0, 1, 0, 2, 0, 0, 1},
        {0, 0, 1, 0, 1, 1, 1, 2}
    },
    {
        {1, 0, 2, 0, 0, 1, 1, 1},
        {0, 0, 0, 1, 1, 1, 1, 2},
        {1, 0, 2, 0, 0, 1, 1, 1},
        {0, 0, 0, 1, 1, 1, 1, 2}
    },
    {
        {0, 0, 1, 0, 1, 1, 2, 1},
        {1, 0, 0, 1, 1, 1, 0, 2},
        {0, 0, 1, 0, 1, 1, 2, 1},
        {1, 0, 0, 1, 1, 1, 0, 2}
    }
};

static unsigned int board_index(unsigned char x, unsigned char y);
static unsigned char random8(void);
static void u8_to_ascii(unsigned char value, char *out);
static void u16_to_ascii(unsigned int value, char *out);
static void u32_to_ascii(unsigned long value, char *out);
static void draw_field(unsigned char x, unsigned char y, unsigned char width,
                       const char *text, unsigned char color);
static void draw_centered_field(unsigned char x, unsigned char y, unsigned char width,
                                const char *text, unsigned char color);
static void clear_rect_area(unsigned char x, unsigned char y, unsigned char width,
                            unsigned char height, unsigned char color);
static void clear_board(void);
static unsigned char piece_color(unsigned char type_id);
static void piece_bounds(unsigned char type_id, unsigned char rotation,
                         unsigned char *min_x, unsigned char *max_x,
                         unsigned char *min_y, unsigned char *max_y);
static unsigned char can_place_piece(unsigned char type_id, unsigned char rotation,
                                     unsigned char base_x, unsigned char base_y);
static void refill_bag(void);
static unsigned char pull_piece(void);
static unsigned char current_drop_ticks(void);
static void update_level_from_lines(void);
static void update_session_best(void);
static void draw_header(void);
static void draw_info_lines(void);
static void draw_board_frame(void);
static void draw_preview_frame(void);
static void draw_preview_piece(void);
static void draw_board_cells(void);
static void draw_status_lines(void);
static void draw_pause_box(void);
static void draw_game_over_box(void);
static void draw_menu_box(void);
static void draw_game_screen(void);
static void invalidate_board_cache(void);
static void restore_play_area_after_popup(void);
static void reset_run_state(void);
static unsigned char spawn_piece(void);
static void start_new_game(void);
static void enter_menu(void);
static unsigned char clear_full_columns(void);
static void lock_active_piece(void);
static void step_gravity(void);
static void hard_drop_piece(void);
static void move_piece_vertical(signed char delta);
static void rotate_piece(signed char dir);
static void prepare_suspend_state(void);
static void resume_save_state(void);
static unsigned char resume_restore_state(void);
static unsigned char handle_app_switch(unsigned char key);
static void handle_menu_key(unsigned char key);
static void handle_play_key(unsigned char key);
static void handle_pause_key(unsigned char key);
static void handle_over_key(unsigned char key);
static void sidetris_loop(void);

static unsigned int board_index(unsigned char x, unsigned char y) {
    return (unsigned int)y * (unsigned int)BOARD_W + (unsigned int)x;
}

static unsigned char random8(void) {
    unsigned int x;

    x = rng_state;
    x ^= (unsigned int)RASTER_LINE;
    x ^= (unsigned int)JIFFY_LO << 8;
    x ^= (x << 7);
    x ^= (x >> 9);
    x ^= (x << 8);
    rng_state = x;
    return (unsigned char)x;
}

static void u8_to_ascii(unsigned char value, char *out) {
    char rev[4];
    unsigned char count;
    unsigned char i;

    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }

    count = 0;
    while (value > 0 && count < 3) {
        rev[count] = (char)('0' + (value % 10));
        value = (unsigned char)(value / 10);
        ++count;
    }

    for (i = 0; i < count; ++i) {
        out[i] = rev[count - 1 - i];
    }
    out[count] = 0;
}

static void u16_to_ascii(unsigned int value, char *out) {
    char rev[6];
    unsigned char count;
    unsigned char i;

    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }

    count = 0;
    while (value > 0 && count < 5) {
        rev[count] = (char)('0' + (value % 10));
        value /= 10;
        ++count;
    }

    for (i = 0; i < count; ++i) {
        out[i] = rev[count - 1 - i];
    }
    out[count] = 0;
}

static void u32_to_ascii(unsigned long value, char *out) {
    char rev[11];
    unsigned char count;
    unsigned char i;

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

static void clear_rect_area(unsigned char x, unsigned char y, unsigned char width,
                            unsigned char height, unsigned char color) {
    unsigned char row;

    for (row = 0; row < height; ++row) {
        tui_clear_line((unsigned char)(y + row), x, width, color);
    }
}

static void clear_board(void) {
    memset(board_cells, 0, sizeof(board_cells));
}

static unsigned char piece_color(unsigned char type_id) {
    switch (type_id) {
        case 1: return TUI_COLOR_CYAN;
        case 2: return TUI_COLOR_YELLOW;
        case 3: return TUI_COLOR_PURPLE;
        case 4: return TUI_COLOR_LIGHTBLUE;
        case 5: return TUI_COLOR_ORANGE;
        case 6: return TUI_COLOR_LIGHTGREEN;
        case 7: return TUI_COLOR_LIGHTRED;
        default: return TUI_COLOR_WHITE;
    }
}

static void piece_bounds(unsigned char type_id, unsigned char rotation,
                         unsigned char *min_x, unsigned char *max_x,
                         unsigned char *min_y, unsigned char *max_y) {
    unsigned char i;
    unsigned char x;
    unsigned char y;

    *min_x = 3;
    *max_x = 0;
    *min_y = 3;
    *max_y = 0;

    for (i = 0; i < BLOCK_COORD_COUNT; i += 2) {
        x = (unsigned char)piece_cells[type_id][rotation][i];
        y = (unsigned char)piece_cells[type_id][rotation][(unsigned char)(i + 1)];
        if (x < *min_x) {
            *min_x = x;
        }
        if (x > *max_x) {
            *max_x = x;
        }
        if (y < *min_y) {
            *min_y = y;
        }
        if (y > *max_y) {
            *max_y = y;
        }
    }
}

static unsigned char can_place_piece(unsigned char type_id, unsigned char rotation,
                                     unsigned char base_x, unsigned char base_y) {
    unsigned char i;
    int board_x;
    int board_y;

    for (i = 0; i < BLOCK_COORD_COUNT; i += 2) {
        board_x = (int)base_x + (int)piece_cells[type_id][rotation][i];
        board_y = (int)base_y + (int)piece_cells[type_id][rotation][(unsigned char)(i + 1)];
        if (board_x < 0 || board_x >= BOARD_W || board_y < 0 || board_y >= BOARD_H) {
            return 0;
        }
        if (board_cells[board_index((unsigned char)board_x, (unsigned char)board_y)] != 0) {
            return 0;
        }
    }

    return 1;
}

static void refill_bag(void) {
    unsigned char i;
    unsigned char j;
    unsigned char tmp;

    for (i = 0; i < PIECE_COUNT; ++i) {
        piece_bag[i] = i;
    }

    for (i = 0; i < PIECE_COUNT; ++i) {
        j = (unsigned char)(random8() % PIECE_COUNT);
        tmp = piece_bag[i];
        piece_bag[i] = piece_bag[j];
        piece_bag[j] = tmp;
    }

    bag_count = PIECE_COUNT;
}

static unsigned char pull_piece(void) {
    if (bag_count == 0) {
        refill_bag();
    }

    --bag_count;
    return piece_bag[bag_count];
}

static unsigned char current_drop_ticks(void) {
    unsigned char ticks;
    unsigned char reduction;

    ticks = speed_base_ticks[speed_id];
    if (level <= 1) {
        return ticks;
    }

    reduction = (unsigned char)(level - 1);
    if (reduction >= (unsigned char)(ticks - 2)) {
        return 2;
    }

    ticks = (unsigned char)(ticks - reduction);
    if (ticks < 2) {
        ticks = 2;
    }
    return ticks;
}

static void update_level_from_lines(void) {
    level = (unsigned char)(1 + (lines_cleared / 10));
    if (level == 0) {
        level = 1;
    }
}

static void update_session_best(void) {
    if (score > session_best_score) {
        session_best_score = score;
    }
}

static void draw_header(void) {
    TuiRect header;
    const char *subtitle;
    unsigned char color;

    header.x = 0;
    header.y = HEADER_Y;
    header.w = 40;
    header.h = HEADER_H;

    subtitle = "SIDEWAYS STACKER";
    color = TUI_COLOR_CYAN;

    if (mode == MODE_PLAY) {
        subtitle = "DROP TO THE RIGHT";
        color = TUI_COLOR_WHITE;
    } else if (mode == MODE_PAUSE) {
        subtitle = "PAUSED";
        color = TUI_COLOR_YELLOW;
    } else if (mode == MODE_OVER) {
        subtitle = "STACK JAMMED";
        color = TUI_COLOR_LIGHTRED;
    }

    tui_window_title(&header, "SIDETRIS", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_centered_field(2, HEADER_Y + 1, 36, subtitle, color);
}

static void draw_info_lines(void) {
    char score_buf[11];
    char best_buf[11];
    char lines_buf[6];
    char level_buf[4];

    u32_to_ascii(score, score_buf);
    u32_to_ascii(session_best_score, best_buf);
    u16_to_ascii(lines_cleared, lines_buf);
    u8_to_ascii(level, level_buf);

    tui_clear_line(INFO_Y, 0, 40, TUI_COLOR_WHITE);
    tui_clear_line((unsigned char)(INFO_Y + 1), 0, 40, TUI_COLOR_WHITE);

    draw_field(0, INFO_Y, 5, "SCORE", TUI_COLOR_GRAY3);
    draw_field(6, INFO_Y, 10, score_buf, TUI_COLOR_WHITE);
    draw_field(18, INFO_Y, 4, "BEST", TUI_COLOR_GRAY3);
    draw_field(23, INFO_Y, 10, best_buf, TUI_COLOR_LIGHTGREEN);

    draw_field(0, (unsigned char)(INFO_Y + 1), 5, "LINES", TUI_COLOR_GRAY3);
    draw_field(6, (unsigned char)(INFO_Y + 1), 5, lines_buf, TUI_COLOR_WHITE);
    draw_field(13, (unsigned char)(INFO_Y + 1), 3, "LVL", TUI_COLOR_GRAY3);
    draw_field(17, (unsigned char)(INFO_Y + 1), 3, level_buf, TUI_COLOR_WHITE);
    draw_field(22, (unsigned char)(INFO_Y + 1), 5, "SPEED", TUI_COLOR_GRAY3);
    draw_field(28, (unsigned char)(INFO_Y + 1), 8, speed_names[speed_id], TUI_COLOR_CYAN);
}

static void draw_board_frame(void) {
    TuiRect frame;

    frame.x = BOARD_X;
    frame.y = BOARD_Y;
    frame.w = BOARD_FRAME_W;
    frame.h = BOARD_FRAME_H;

    tui_window_title(&frame, "WELL", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
}

static void draw_preview_frame(void) {
    TuiRect frame;

    frame.x = PREVIEW_X;
    frame.y = PREVIEW_Y;
    frame.w = PREVIEW_W;
    frame.h = PREVIEW_H;

    tui_window_title(&frame, "NEXT", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
}

static void draw_preview_piece(void) {
    unsigned char min_x;
    unsigned char max_x;
    unsigned char min_y;
    unsigned char max_y;
    unsigned char width;
    unsigned char height;
    unsigned char origin_x;
    unsigned char origin_y;
    unsigned char x;
    unsigned char y;
    unsigned char i;
    unsigned int offset;
    unsigned char color;

    for (y = (unsigned char)(PREVIEW_Y + 1); y < (unsigned char)(PREVIEW_Y + PREVIEW_H - 1); ++y) {
        for (x = (unsigned char)(PREVIEW_X + 1); x < (unsigned char)(PREVIEW_X + PREVIEW_W - 1); ++x) {
            offset = (unsigned int)y * 40 + x;
            TUI_SCREEN[offset] = CHAR_SPACE;
            TUI_COLOR_RAM[offset] = TUI_COLOR_GRAY1;
        }
    }

    piece_bounds(next_piece, 0, &min_x, &max_x, &min_y, &max_y);
    width = (unsigned char)(max_x - min_x + 1);
    height = (unsigned char)(max_y - min_y + 1);
    origin_x = (unsigned char)(PREVIEW_X + 1 + ((PREVIEW_W - 2 - width) / 2));
    origin_y = (unsigned char)(PREVIEW_Y + 1 + ((PREVIEW_H - 2 - height) / 2));
    color = piece_color((unsigned char)(next_piece + 1));

    for (i = 0; i < BLOCK_COORD_COUNT; i += 2) {
        x = (unsigned char)(origin_x + piece_cells[next_piece][0][i] - min_x);
        y = (unsigned char)(origin_y + piece_cells[next_piece][0][(unsigned char)(i + 1)] - min_y);
        offset = (unsigned int)y * 40 + x;
        TUI_SCREEN[offset] = CHAR_BLOCK;
        TUI_COLOR_RAM[offset] = color;
    }
}

static void draw_board_cells(void) {
    unsigned char x;
    unsigned char y;
    unsigned char i;
    unsigned char draw_x;
    unsigned char draw_y;
    unsigned int idx;
    unsigned int offset;
    unsigned char cell;
    unsigned char color;

    memcpy(rebuild_cells, board_cells, sizeof(board_cells));

    if (mode == MODE_PLAY || mode == MODE_PAUSE) {
        color = piece_color((unsigned char)(piece_type + 1));
        for (i = 0; i < BLOCK_COORD_COUNT; i += 2) {
            draw_x = (unsigned char)(piece_x + piece_cells[piece_type][piece_rotation][i]);
            draw_y = (unsigned char)(piece_y + piece_cells[piece_type][piece_rotation][(unsigned char)(i + 1)]);
            rebuild_cells[board_index(draw_x, draw_y)] = (unsigned char)(piece_type + 1);
        }
    }

    for (y = 0; y < BOARD_H; ++y) {
        draw_y = (unsigned char)(BOARD_Y + 1 + y);
        for (x = 0; x < BOARD_W; ++x) {
            idx = board_index(x, y);
            cell = rebuild_cells[idx];
            if (rendered_cells[idx] == cell) {
                continue;
            }
            rendered_cells[idx] = cell;
            draw_x = (unsigned char)(BOARD_X + 1 + x);
            offset = (unsigned int)draw_y * 40 + draw_x;
            if (cell == 0) {
                TUI_SCREEN[offset] = CHAR_EMPTY;
                TUI_COLOR_RAM[offset] = TUI_COLOR_GRAY1;
            } else {
                TUI_SCREEN[offset] = CHAR_BLOCK;
                if (mode == MODE_PLAY || mode == MODE_PAUSE) {
                    color = piece_color(cell);
                } else {
                    color = piece_color(cell);
                }
                TUI_COLOR_RAM[offset] = color;
            }
        }
    }
}

static void draw_status_lines(void) {
    char last_buf[11];
    char tick_buf[4];

    tui_clear_line(STATUS_Y, 0, 40, TUI_COLOR_WHITE);
    tui_clear_line(GLOBAL_HELP_Y, 0, 40, TUI_COLOR_GRAY3);
    tui_clear_line(HELP_Y, 0, 40, TUI_COLOR_GRAY3);
    tui_clear_line(HELP2_Y, 0, 40, TUI_COLOR_GRAY3);

    if (mode == MODE_MENU) {
        u32_to_ascii(last_score, last_buf);
        draw_field(0, STATUS_Y, 15, "RETURN OR SPACE", TUI_COLOR_WHITE);
        draw_field(16, STATUS_Y, 5, "LAST", TUI_COLOR_GRAY3);
        draw_field(22, STATUS_Y, 10, last_buf, TUI_COLOR_WHITE);
        draw_field(0, GLOBAL_HELP_Y, 37, "F2/F4 APPS  CTRL+B HOME  RUN/STOP QUIT", TUI_COLOR_GRAY3);
        draw_field(0, HELP_Y, 39, "W/S OR UP/DN CYCLE SPEED  1-9/0 PICK", TUI_COLOR_GRAY3);
        draw_field(0, HELP2_Y, 31, "RETURN/SPACE START", TUI_COLOR_GRAY3);
        return;
    }

    u8_to_ascii(current_drop_ticks(), tick_buf);

    if (mode == MODE_PAUSE) {
        draw_field(0, STATUS_Y, 16, "PAUSED", TUI_COLOR_YELLOW);
    } else if (mode == MODE_OVER) {
        draw_field(0, STATUS_Y, 16, "GAME OVER", TUI_COLOR_LIGHTRED);
    } else {
        draw_field(0, STATUS_Y, 18, "CLEAR FULL COLUMNS", TUI_COLOR_WHITE);
    }

    draw_field(22, STATUS_Y, 4, "TICK", TUI_COLOR_GRAY3);
    draw_field(27, STATUS_Y, 3, tick_buf, TUI_COLOR_WHITE);
    draw_field(31, STATUS_Y, 4, "DROP", TUI_COLOR_CYAN);

    draw_field(0, GLOBAL_HELP_Y, 37, "F2/F4 APPS  CTRL+B HOME  RUN/STOP QUIT", TUI_COLOR_GRAY3);

    if (mode == MODE_PAUSE) {
        draw_field(0, HELP_Y, 32, "P/RET/SPACE RESUME  R RESTART", TUI_COLOR_GRAY3);
        draw_field(0, HELP2_Y, 23, "M MENU", TUI_COLOR_GRAY3);
    } else if (mode == MODE_OVER) {
        draw_field(0, HELP_Y, 36, "R/RET/SPACE RESTART  M MENU", TUI_COLOR_GRAY3);
    } else {
        draw_field(0, HELP_Y, 39, "W/UP UP  S/DN DOWN  A/L CCW  D/R CW", TUI_COLOR_GRAY3);
        draw_field(0, HELP2_Y, 39, "SPACE DROP  P PAUSE  R RESET  M MENU", TUI_COLOR_GRAY3);
    }
}

static void draw_pause_box(void) {
    TuiRect popup;

    popup.x = PAUSE_X;
    popup.y = PAUSE_Y;
    popup.w = PAUSE_W;
    popup.h = PAUSE_H;

    tui_window_title(&popup, "PAUSED", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_centered_field((unsigned char)(PAUSE_X + 2), (unsigned char)(PAUSE_Y + 2),
                        (unsigned char)(PAUSE_W - 4), "RUN IS SAFE IN REU", TUI_COLOR_WHITE);
    draw_centered_field((unsigned char)(PAUSE_X + 2), (unsigned char)(PAUSE_Y + 3),
                        (unsigned char)(PAUSE_W - 4), "P / RETURN / SPACE = RESUME", TUI_COLOR_GRAY3);
    draw_centered_field((unsigned char)(PAUSE_X + 2), (unsigned char)(PAUSE_Y + 4),
                        (unsigned char)(PAUSE_W - 4), "R = RESTART   M = MENU", TUI_COLOR_GRAY3);
}

static void draw_game_over_box(void) {
    TuiRect popup;
    char score_buf[11];

    popup.x = PAUSE_X;
    popup.y = PAUSE_Y;
    popup.w = PAUSE_W;
    popup.h = PAUSE_H;

    u32_to_ascii(score, score_buf);

    tui_window_title(&popup, "STACK JAMMED", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_centered_field((unsigned char)(PAUSE_X + 2), (unsigned char)(PAUSE_Y + 2),
                        (unsigned char)(PAUSE_W - 4), "FINAL SCORE", TUI_COLOR_WHITE);
    draw_centered_field((unsigned char)(PAUSE_X + 2), (unsigned char)(PAUSE_Y + 3),
                        (unsigned char)(PAUSE_W - 4), score_buf, TUI_COLOR_LIGHTGREEN);
    draw_centered_field((unsigned char)(PAUSE_X + 2), (unsigned char)(PAUSE_Y + 4),
                        (unsigned char)(PAUSE_W - 4), "R / RETURN = AGAIN   M = MENU", TUI_COLOR_GRAY3);
}

static void draw_menu_box(void) {
    TuiRect box;
    char best_buf[11];
    char last_buf[11];

    box.x = 4;
    box.y = 8;
    box.w = 32;
    box.h = 9;

    u32_to_ascii(session_best_score, best_buf);
    u32_to_ascii(last_score, last_buf);

    tui_window_title(&box, "START", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_centered_field(6, 10, 28, "ROTATED TETRIS FOR READYOS", TUI_COLOR_WHITE);
    draw_field(8, 12, 7, "SPEED", TUI_COLOR_GRAY3);
    draw_field(16, 12, 3, speed_names[speed_id], TUI_COLOR_CYAN);
    draw_field(8, 13, 6, "BEST", TUI_COLOR_GRAY3);
    draw_field(15, 13, 10, best_buf, TUI_COLOR_LIGHTGREEN);
    draw_field(8, 14, 6, "LAST", TUI_COLOR_GRAY3);
    draw_field(15, 14, 10, last_buf, TUI_COLOR_WHITE);
    draw_centered_field(6, 15, 28, "RETURN OR SPACE TO PLAY", TUI_COLOR_WHITE);
}

static void draw_game_screen(void) {
    tui_clear(TUI_THEME_BG);
    VIC.bordercolor = TUI_THEME_BG;
    draw_header();
    draw_info_lines();

    if (mode == MODE_MENU) {
        draw_menu_box();
    } else {
        draw_board_frame();
        draw_preview_frame();
        invalidate_board_cache();
        draw_preview_piece();
        draw_board_cells();
        if (mode == MODE_PAUSE) {
            draw_pause_box();
        } else if (mode == MODE_OVER) {
            draw_game_over_box();
        }
    }

    draw_status_lines();
}

static void invalidate_board_cache(void) {
    memset(rendered_cells, 0xFF, sizeof(rendered_cells));
}

static void restore_play_area_after_popup(void) {
    clear_rect_area(PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H, TUI_THEME_BG);
    draw_board_frame();
    invalidate_board_cache();
    draw_board_cells();
}

static void reset_run_state(void) {
    clear_board();
    score = 0;
    lines_cleared = 0;
    level = 1;
    piece_type = PIECE_I;
    piece_rotation = 0;
    piece_x = 0;
    piece_y = 0;
    bag_count = 0;
    invalidate_board_cache();
}

static unsigned char spawn_piece(void) {
    unsigned char min_x;
    unsigned char max_x;
    unsigned char min_y;
    unsigned char max_y;
    unsigned char height;
    unsigned char spawn_y;
    unsigned char try_y;

    piece_type = next_piece;
    next_piece = pull_piece();
    piece_rotation = 0;
    piece_x = 0;

    piece_bounds(piece_type, piece_rotation, &min_x, &max_x, &min_y, &max_y);
    height = (unsigned char)(max_y - min_y + 1);
    spawn_y = (unsigned char)((BOARD_H - height) / 2);

    if (can_place_piece(piece_type, piece_rotation, piece_x, spawn_y)) {
        piece_y = spawn_y;
        return 1;
    }
    if (spawn_y > 0) {
        try_y = (unsigned char)(spawn_y - 1);
        if (can_place_piece(piece_type, piece_rotation, piece_x, try_y)) {
            piece_y = try_y;
            return 1;
        }
    }
    if (spawn_y + 1 < BOARD_H) {
        try_y = (unsigned char)(spawn_y + 1);
        if (can_place_piece(piece_type, piece_rotation, piece_x, try_y)) {
            piece_y = try_y;
            return 1;
        }
    }

    return 0;
}

static void start_new_game(void) {
    reset_run_state();
    mode = MODE_PLAY;
    refill_bag();
    next_piece = pull_piece();
    if (!spawn_piece()) {
        last_score = score;
        mode = MODE_OVER;
    }
    drop_tick = JIFFY_LO;
    draw_game_screen();
}

static void enter_menu(void) {
    reset_run_state();
    mode = MODE_MENU;
    draw_game_screen();
}

static unsigned char clear_full_columns(void) {
    unsigned char src_x;
    unsigned char dst_x;
    unsigned char y;
    unsigned char full;
    unsigned char cleared;

    memset(rebuild_cells, 0, sizeof(rebuild_cells));
    dst_x = BOARD_W;
    cleared = 0;

    src_x = BOARD_W;
    while (src_x > 0) {
        --src_x;
        full = 1;
        for (y = 0; y < BOARD_H; ++y) {
            if (board_cells[board_index(src_x, y)] == 0) {
                full = 0;
                break;
            }
        }
        if (full) {
            ++cleared;
            continue;
        }
        --dst_x;
        for (y = 0; y < BOARD_H; ++y) {
            rebuild_cells[board_index(dst_x, y)] = board_cells[board_index(src_x, y)];
        }
    }

    memcpy(board_cells, rebuild_cells, sizeof(board_cells));
    return cleared;
}

static void lock_active_piece(void) {
    unsigned char i;
    unsigned char board_x;
    unsigned char board_y;
    unsigned char cleared;

    for (i = 0; i < BLOCK_COORD_COUNT; i += 2) {
        board_x = (unsigned char)(piece_x + piece_cells[piece_type][piece_rotation][i]);
        board_y = (unsigned char)(piece_y + piece_cells[piece_type][piece_rotation][(unsigned char)(i + 1)]);
        board_cells[board_index(board_x, board_y)] = (unsigned char)(piece_type + 1);
    }

    cleared = clear_full_columns();
    if (cleared != 0) {
        switch (cleared) {
            case 1: score += (unsigned long)100 * (unsigned long)level; break;
            case 2: score += (unsigned long)300 * (unsigned long)level; break;
            case 3: score += (unsigned long)500 * (unsigned long)level; break;
            default: score += (unsigned long)800 * (unsigned long)level; break;
        }
        lines_cleared = (unsigned int)(lines_cleared + cleared);
        update_level_from_lines();
        update_session_best();
    }

    if (!spawn_piece()) {
        update_session_best();
        last_score = score;
        mode = MODE_OVER;
    }
    drop_tick = JIFFY_LO;
    draw_board_cells();
    draw_preview_piece();
    draw_info_lines();
    draw_status_lines();
    if (mode == MODE_OVER) {
        draw_header();
        draw_game_over_box();
    }
}

static void step_gravity(void) {
    if (mode != MODE_PLAY) {
        return;
    }

    if (piece_x + 1 < BOARD_W && can_place_piece(piece_type, piece_rotation,
                                                 (unsigned char)(piece_x + 1), piece_y)) {
        ++piece_x;
        draw_board_cells();
        return;
    }

    lock_active_piece();
}

static void hard_drop_piece(void) {
    if (mode != MODE_PLAY) {
        return;
    }

    while (piece_x + 1 < BOARD_W && can_place_piece(piece_type, piece_rotation,
                                                    (unsigned char)(piece_x + 1), piece_y)) {
        ++piece_x;
    }

    lock_active_piece();
}

static void move_piece_vertical(signed char delta) {
    unsigned char next_y;

    if (mode != MODE_PLAY) {
        return;
    }

    if (delta < 0) {
        if (piece_y == 0) {
            return;
        }
        next_y = (unsigned char)(piece_y - 1);
    } else {
        next_y = (unsigned char)(piece_y + 1);
        if (next_y >= BOARD_H) {
            return;
        }
    }

    if (can_place_piece(piece_type, piece_rotation, piece_x, next_y)) {
        piece_y = next_y;
        draw_board_cells();
    }
}

static void rotate_piece(signed char dir) {
    unsigned char new_rotation;
    unsigned char try_y;

    if (mode != MODE_PLAY) {
        return;
    }

    if (dir < 0) {
        new_rotation = (unsigned char)((piece_rotation + 3) & 0x03);
    } else {
        new_rotation = (unsigned char)((piece_rotation + 1) & 0x03);
    }

    if (can_place_piece(piece_type, new_rotation, piece_x, piece_y)) {
        piece_rotation = new_rotation;
        draw_board_cells();
        return;
    }

    if (piece_y > 0) {
        try_y = (unsigned char)(piece_y - 1);
        if (can_place_piece(piece_type, new_rotation, piece_x, try_y)) {
            piece_rotation = new_rotation;
            piece_y = try_y;
            draw_board_cells();
            return;
        }
    }

    if (piece_y + 1 < BOARD_H) {
        try_y = (unsigned char)(piece_y + 1);
        if (can_place_piece(piece_type, new_rotation, piece_x, try_y)) {
            piece_rotation = new_rotation;
            piece_y = try_y;
            draw_board_cells();
            return;
        }
    }

    if (piece_y > 1) {
        try_y = (unsigned char)(piece_y - 2);
        if (can_place_piece(piece_type, new_rotation, piece_x, try_y)) {
            piece_rotation = new_rotation;
            piece_y = try_y;
            draw_board_cells();
            return;
        }
    }

    if (piece_x > 0 && can_place_piece(piece_type, new_rotation, (unsigned char)(piece_x - 1), piece_y)) {
        piece_rotation = new_rotation;
        --piece_x;
        draw_board_cells();
    }
}

static void prepare_suspend_state(void) {
    if (mode == MODE_PLAY) {
        mode = MODE_PAUSE;
    }
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }

    resume_blob.mode = mode;
    resume_blob.speed_id = speed_id;
    resume_blob.level = level;
    resume_blob.piece_type = piece_type;
    resume_blob.piece_rotation = piece_rotation;
    resume_blob.next_piece = next_piece;
    resume_blob.piece_x = piece_x;
    resume_blob.piece_y = piece_y;
    resume_blob.bag_count = bag_count;
    memcpy(resume_blob.piece_bag, piece_bag, sizeof(piece_bag));
    resume_blob.lines_cleared = lines_cleared;
    resume_blob.rng_state = rng_state;
    resume_blob.score = score;
    resume_blob.session_best_score = session_best_score;
    resume_blob.last_score = last_score;
    memcpy(resume_blob.board_cells, board_cells, sizeof(board_cells));

    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len;
    unsigned int i;

    payload_len = 0;
    if (!resume_ready) {
        return 0;
    }
    if (!resume_try_load(&resume_blob, sizeof(resume_blob), &payload_len)) {
        return 0;
    }
    if (payload_len != sizeof(resume_blob)) {
        return 0;
    }
    if (resume_blob.mode > MODE_OVER) {
        return 0;
    }
    if (resume_blob.speed_id >= SPEED_COUNT) {
        return 0;
    }
    if (resume_blob.level == 0) {
        return 0;
    }
    if (resume_blob.next_piece >= PIECE_COUNT || resume_blob.piece_type >= PIECE_COUNT) {
        return 0;
    }
    if (resume_blob.piece_rotation >= ROTATION_COUNT || resume_blob.bag_count > PIECE_COUNT) {
        return 0;
    }
    if (resume_blob.piece_x >= BOARD_W || resume_blob.piece_y >= BOARD_H) {
        return 0;
    }
    for (i = 0; i < CELL_COUNT; ++i) {
        if (resume_blob.board_cells[i] > PIECE_COUNT) {
            return 0;
        }
    }
    for (i = 0; i < PIECE_COUNT; ++i) {
        if (resume_blob.piece_bag[i] >= PIECE_COUNT) {
            return 0;
        }
    }

    mode = resume_blob.mode;
    if (mode == MODE_PLAY) {
        mode = MODE_PAUSE;
    }
    speed_id = resume_blob.speed_id;
    level = resume_blob.level;
    piece_type = resume_blob.piece_type;
    piece_rotation = resume_blob.piece_rotation;
    next_piece = resume_blob.next_piece;
    piece_x = resume_blob.piece_x;
    piece_y = resume_blob.piece_y;
    bag_count = resume_blob.bag_count;
    memcpy(piece_bag, resume_blob.piece_bag, sizeof(piece_bag));
    lines_cleared = resume_blob.lines_cleared;
    rng_state = resume_blob.rng_state;
    if (rng_state == 0) {
        rng_state = (unsigned int)0x51D3 ^ ((unsigned int)RASTER_LINE << 8) ^ JIFFY_LO;
    }
    score = resume_blob.score;
    session_best_score = resume_blob.session_best_score;
    last_score = resume_blob.last_score;
    memcpy(board_cells, resume_blob.board_cells, sizeof(board_cells));
    update_session_best();
    drop_tick = JIFFY_LO;
    return 1;
}

static unsigned char handle_app_switch(unsigned char key) {
    unsigned char nav_action;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        prepare_suspend_state();
        resume_save_state();
        tui_return_to_launcher();
        return 1;
    }
    if (nav_action >= APP_BANK_MIN && nav_action <= APP_BANK_MAX) {
        prepare_suspend_state();
        resume_save_state();
        tui_switch_to_app(nav_action);
        return 1;
    }
    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return 1;
    }
    return 0;
}

static void handle_menu_key(unsigned char key) {
    if (key == TUI_KEY_UP || key == 'w' || key == 'W' || key == TUI_KEY_LEFT) {
        if (speed_id == 0) {
            speed_id = (unsigned char)(SPEED_COUNT - 1);
        } else {
            --speed_id;
        }
        draw_menu_box();
        draw_status_lines();
        return;
    }

    if (key == TUI_KEY_DOWN || key == 's' || key == 'S' || key == TUI_KEY_RIGHT) {
        speed_id = (unsigned char)((speed_id + 1) % SPEED_COUNT);
        draw_menu_box();
        draw_status_lines();
        return;
    }

    if (key >= '1' && key <= '9') {
        speed_id = (unsigned char)(key - '1');
        draw_menu_box();
        draw_status_lines();
        return;
    }
    if (key == '0') {
        speed_id = (unsigned char)(SPEED_COUNT - 1);
        draw_menu_box();
        draw_status_lines();
        return;
    }

    if (key == TUI_KEY_RETURN || key == ' ') {
        start_new_game();
    }
}

static void handle_play_key(unsigned char key) {
    if (key == 'p' || key == 'P') {
        mode = MODE_PAUSE;
        draw_header();
        draw_status_lines();
        draw_pause_box();
        return;
    }
    if (key == 'r' || key == 'R') {
        start_new_game();
        return;
    }
    if (key == 'm' || key == 'M') {
        enter_menu();
        return;
    }
    if (key == 'w' || key == 'W') {
        move_piece_vertical(-1);
        return;
    }
    if (key == TUI_KEY_UP) {
        move_piece_vertical(-1);
        return;
    }
    if (key == 's' || key == 'S') {
        move_piece_vertical(1);
        return;
    }
    if (key == TUI_KEY_DOWN) {
        move_piece_vertical(1);
        return;
    }
    if (key == 'a' || key == 'A' || key == TUI_KEY_LEFT) {
        rotate_piece(-1);
        return;
    }
    if (key == 'd' || key == 'D' || key == TUI_KEY_RIGHT) {
        rotate_piece(1);
        return;
    }
    if (key == ' ') {
        hard_drop_piece();
    }
}

static void handle_pause_key(unsigned char key) {
    if (key == 'p' || key == 'P' || key == TUI_KEY_RETURN || key == ' ') {
        mode = MODE_PLAY;
        drop_tick = JIFFY_LO;
        draw_header();
        draw_status_lines();
        restore_play_area_after_popup();
        return;
    }
    if (key == 'r' || key == 'R') {
        start_new_game();
        return;
    }
    if (key == 'm' || key == 'M') {
        enter_menu();
    }
}

static void handle_over_key(unsigned char key) {
    if (key == 'r' || key == 'R' || key == TUI_KEY_RETURN || key == ' ') {
        start_new_game();
        return;
    }
    if (key == 'm' || key == 'M') {
        enter_menu();
    }
}

static void sidetris_loop(void) {
    unsigned char key;
    unsigned char now_tick;

    while (running) {
        while (tui_kbhit()) {
            key = tui_getkey();
            if (handle_app_switch(key)) {
                continue;
            }

            if (key == TUI_KEY_RUNSTOP) {
                running = 0;
                break;
            }

            if (mode == MODE_MENU) {
                handle_menu_key(key);
            } else if (mode == MODE_PLAY) {
                handle_play_key(key);
            } else if (mode == MODE_PAUSE) {
                handle_pause_key(key);
            } else {
                handle_over_key(key);
            }
        }

        if (!running) {
            break;
        }

        if (mode == MODE_PLAY) {
            now_tick = JIFFY_LO;
            if ((unsigned char)(now_tick - drop_tick) >= current_drop_ticks()) {
                drop_tick = now_tick;
                step_gravity();
            }
        }

        waitvsync();
    }
}

int main(void) {
    unsigned char bank;

    rng_state = (unsigned int)0x51D3 ^ ((unsigned int)RASTER_LINE << 8) ^ JIFFY_LO;
    running = 1;
    resume_ready = 0;
    speed_id = SPEED_DEFAULT;
    level = 1;
    score = 0;
    session_best_score = 0;
    last_score = 0;
    lines_cleared = 0;
    mode = MODE_MENU;

    tui_init();

    bank = SHIM_CURRENT_BANK;
    if (bank >= APP_BANK_MIN && bank <= APP_BANK_MAX) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }

    if (resume_restore_state()) {
        draw_game_screen();
    } else {
        enter_menu();
    }

    sidetris_loop();

    __asm__("jmp $FCE2");
    return 0;
}
