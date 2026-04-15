/*
 * deminer.c - Ready OS Deminer game (PETSCII text mode)
 *
 * Controls:
 * - Cursor keys or WASD: move
 * - RETURN or J: reveal
 * - SPACE or K: flag/unflag
 * - P: pause/resume
 * - R: restart current level
 * - M: return to level menu
 * - CTRL+B: return to launcher
 * - F2/F4: switch apps
 */

#include "../../lib/tui.h"
#include "../../lib/resume_state.h"
#include <c64.h>
#include <conio.h>
#include <string.h>

#define MAX_BOARD_W 16
#define MAX_BOARD_H 16
#define MAX_CELLS   256

#define HEADER_Y  0
#define HEADER_H  3
#define STATUS_Y 23
#define HELP_Y   24
#define PLAY_TOP HEADER_H
#define PLAY_H   (STATUS_Y - PLAY_TOP)

#define LEVEL_BEGINNER     0
#define LEVEL_INTERMEDIATE 1
#define LEVEL_COUNT        2

#define MODE_MENU  0
#define MODE_PLAY  1
#define MODE_PAUSE 2
#define MODE_WON   3
#define MODE_LOST  4

#define PAUSE_X  8
#define PAUSE_Y  8
#define PAUSE_W  24
#define PAUSE_H  9
#define PAUSE_AREA (PAUSE_W * PAUSE_H)

#define HELP_POPUP_X 1
#define HELP_POPUP_Y 4
#define HELP_POPUP_W 38
#define HELP_POPUP_H 16
#define HELP_POPUP_AREA (HELP_POPUP_W * HELP_POPUP_H)

#define CHAR_BLANK    32
#define CHAR_BLOCK    0xA0
#define CHAR_CHECKER  0x66
#define CHAR_DOT      46
#define CHAR_STAR     42
#define CHAR_FLAG     30
#define CHAR_X        24

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)
#define RASTER_LINE (*(volatile unsigned char*)0xD012)
#define JIFFY_LO    (*(volatile unsigned char*)0xA2)

static unsigned char cell_mine[MAX_CELLS];
static unsigned char cell_adj[MAX_CELLS];
static unsigned char cell_visible[MAX_CELLS];
static unsigned char cell_flagged[MAX_CELLS];
static unsigned char draw_cache[MAX_CELLS];
static unsigned char reveal_queue[MAX_CELLS];

static unsigned char pause_saved_screen[PAUSE_AREA];
static unsigned char pause_saved_color[PAUSE_AREA];
static unsigned char help_saved_screen[HELP_POPUP_AREA];
static unsigned char help_saved_color[HELP_POPUP_AREA];

static unsigned char board_w;
static unsigned char board_h;
static unsigned char mine_total;
static unsigned char total_safe;
static unsigned char revealed_safe;
static unsigned char flags_used;
static unsigned char cursor_x;
static unsigned char cursor_y;
static unsigned char level_id;
static unsigned char menu_level;
static unsigned char mode;
static unsigned char mines_placed;
static unsigned char blast_index;
static unsigned char running;
static unsigned char dirty_full;
static unsigned char resume_ready;
static unsigned int rng_state;

typedef struct {
    unsigned char level_id;
    unsigned char menu_level;
    unsigned char mode;
    unsigned char cursor_x;
    unsigned char cursor_y;
    unsigned char revealed_safe;
    unsigned char flags_used;
    unsigned char mines_placed;
    unsigned char blast_index;
    unsigned int rng_state;
    unsigned char cell_mine[MAX_CELLS];
    unsigned char cell_adj[MAX_CELLS];
    unsigned char cell_visible[MAX_CELLS];
    unsigned char cell_flagged[MAX_CELLS];
} DeminerResumeV1;

static DeminerResumeV1 resume_blob;

static void configure_level(unsigned char level);
static void invalidate_draw_cache(void);
static void draw_header(void);
static void draw_menu(void);
static void draw_game_screen(void);
static void draw_status_line(void);
static void draw_help_line(void);
static void draw_pause_overlay(void);
static void show_help_popup(void);
static void restore_pause_background(void);
static void update_keyrepeat_mode(void);
static void enter_menu(void);
static void start_game(unsigned char level);
static void draw_changed_cells(void);
static void resume_save_state(void);
static unsigned char resume_restore_state(void);

static const char *level_name(void) {
    if (level_id == LEVEL_INTERMEDIATE) {
        return "INTERMEDIATE";
    }
    return "BEGINNER";
}

static unsigned int active_cell_count(void) {
    return (unsigned int)board_w * (unsigned int)board_h;
}

static unsigned int board_index(unsigned char x, unsigned char y) {
    return (unsigned int)y * (unsigned int)board_w + (unsigned int)x;
}

static unsigned char board_frame_width(void) {
    return (unsigned char)(board_w * 2 + 7);
}

static unsigned char board_frame_height(void) {
    return (unsigned char)(board_h + 4);
}

static unsigned char board_frame_x(void) {
    return (unsigned char)((40 - board_frame_width()) / 2);
}

static unsigned char board_frame_y(void) {
    return (unsigned char)(PLAY_TOP + ((PLAY_H - board_frame_height()) / 2));
}

static unsigned char board_origin_x(void) {
    return (unsigned char)(board_frame_x() + 4);
}

static unsigned char board_origin_y(void) {
    return (unsigned char)(board_frame_y() + 2);
}

static unsigned char board_screen_x(unsigned char x) {
    return (unsigned char)(board_origin_x() + x * 2);
}

static unsigned char board_screen_y(unsigned char y) {
    return (unsigned char)(board_origin_y() + y);
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

static void save_popup_background(unsigned char x, unsigned char y,
                                  unsigned char w, unsigned char h,
                                  unsigned char *screen_buf,
                                  unsigned char *color_buf) {
    unsigned char px;
    unsigned char py;
    unsigned int src;
    unsigned int dst;

    dst = 0;
    for (py = 0; py < h; ++py) {
        src = (unsigned int)(y + py) * 40 + x;
        for (px = 0; px < w; ++px) {
            screen_buf[dst] = TUI_SCREEN[src + px];
            color_buf[dst] = TUI_COLOR_RAM[src + px];
            ++dst;
        }
    }
}

static void restore_popup_background(unsigned char x, unsigned char y,
                                     unsigned char w, unsigned char h,
                                     const unsigned char *screen_buf,
                                     const unsigned char *color_buf) {
    unsigned char px;
    unsigned char py;
    unsigned int src;
    unsigned int dst;

    src = 0;
    for (py = 0; py < h; ++py) {
        dst = (unsigned int)(y + py) * 40 + x;
        for (px = 0; px < w; ++px) {
            TUI_SCREEN[dst + px] = screen_buf[src];
            TUI_COLOR_RAM[dst + px] = color_buf[src];
            ++src;
        }
    }
}

static void clear_board_state(void) {
    memset(cell_mine, 0, sizeof(cell_mine));
    memset(cell_adj, 0, sizeof(cell_adj));
    memset(cell_visible, 0, sizeof(cell_visible));
    memset(cell_flagged, 0, sizeof(cell_flagged));
}

static void configure_level(unsigned char level) {
    level_id = level;
    if (level_id == LEVEL_INTERMEDIATE) {
        board_w = 16;
        board_h = 16;
        mine_total = 40;
    } else {
        level_id = LEVEL_BEGINNER;
        board_w = 9;
        board_h = 9;
        mine_total = 10;
    }
    total_safe = (unsigned char)(board_w * board_h - mine_total);
}

static void invalidate_draw_cache(void) {
    memset(draw_cache, 0xFF, sizeof(draw_cache));
    dirty_full = 1;
}

static void draw_header(void) {
    TuiRect header = {0, HEADER_Y, 40, HEADER_H};
    const char *subtitle;
    unsigned char color;

    subtitle = "MINEFIELD DEMINING GAME";
    color = TUI_COLOR_CYAN;

    if (mode == MODE_PAUSE) {
        subtitle = "PAUSED";
        color = TUI_COLOR_YELLOW;
    } else if (mode == MODE_WON) {
        subtitle = "FIELD CLEARED";
        color = TUI_COLOR_LIGHTGREEN;
    } else if (mode == MODE_LOST) {
        subtitle = "MINE TRIGGERED";
        color = TUI_COLOR_LIGHTRED;
    } else if (mode == MODE_PLAY) {
        subtitle = "CLEAR ALL SAFE CELLS";
        color = TUI_COLOR_WHITE;
    }

    tui_window_title(&header, "DEMINER", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_centered_field(2, HEADER_Y + 1, 36, subtitle, color);
}

static unsigned char number_color(unsigned char value) {
    switch (value) {
        case 1: return TUI_COLOR_LIGHTBLUE;
        case 2: return TUI_COLOR_LIGHTGREEN;
        case 3: return TUI_COLOR_YELLOW;
        case 4: return TUI_COLOR_ORANGE;
        case 5: return TUI_COLOR_LIGHTRED;
        case 6: return TUI_COLOR_RED;
        case 7: return TUI_COLOR_CYAN;
        case 8: return TUI_COLOR_WHITE;
        default: return TUI_COLOR_GRAY2;
    }
}

static unsigned char cursor_index(void) {
    return (unsigned char)board_index(cursor_x, cursor_y);
}

static unsigned char render_signature(unsigned int idx) {
    unsigned char sig;

    sig = cell_adj[idx];
    if (cell_visible[idx]) {
        sig |= 0x10;
    }
    if (cell_flagged[idx]) {
        sig |= 0x20;
    }
    if (cell_mine[idx]) {
        sig |= 0x40;
    }
    if ((mode == MODE_PLAY || mode == MODE_WON || mode == MODE_LOST) &&
        idx == (unsigned int)cursor_index()) {
        sig |= 0x80;
    }
    if (mode == MODE_LOST) {
        sig ^= 0x08;
    }
    if (mode == MODE_WON) {
        sig ^= 0x04;
    }
    if (idx == blast_index) {
        sig ^= 0x02;
    }
    return sig;
}

static void render_cell(unsigned int idx) {
    unsigned char x;
    unsigned char y;
    unsigned char screen_x;
    unsigned char screen_y;
    unsigned int offset;
    unsigned char ch0;
    unsigned char ch1;
    unsigned char color;
    unsigned char selected;
    unsigned char revealed;
    unsigned char show_mine;
    unsigned char wrong_flag;

    x = (unsigned char)(idx % board_w);
    y = (unsigned char)(idx / board_w);
    screen_x = board_screen_x(x);
    screen_y = board_screen_y(y);
    offset = (unsigned int)screen_y * 40 + screen_x;
    selected = (unsigned char)((mode == MODE_PLAY || mode == MODE_WON || mode == MODE_LOST) &&
                               idx == (unsigned int)cursor_index());
    revealed = cell_visible[idx];
    show_mine = (unsigned char)((revealed && cell_mine[idx]) ||
                                (mode == MODE_LOST && cell_mine[idx]));
    wrong_flag = (unsigned char)(mode == MODE_LOST &&
                                 cell_flagged[idx] &&
                                 !cell_mine[idx] &&
                                 !cell_visible[idx]);

    ch0 = CHAR_BLANK;
    ch1 = CHAR_BLANK;
    color = TUI_COLOR_WHITE;

    if (show_mine) {
        ch0 = CHAR_STAR;
        ch1 = CHAR_BLANK;
        if (idx == blast_index) {
            color = TUI_COLOR_YELLOW;
        } else if (selected) {
            color = TUI_COLOR_WHITE;
        } else {
            color = TUI_COLOR_LIGHTRED;
        }
    } else if (wrong_flag) {
        ch0 = CHAR_X;
        ch1 = CHAR_BLANK;
        color = selected ? TUI_COLOR_WHITE : TUI_COLOR_LIGHTRED;
    } else if (revealed) {
        if (cell_adj[idx] == 0) {
            ch0 = CHAR_DOT;
            ch1 = CHAR_DOT;
            color = selected ? TUI_COLOR_GRAY3 : TUI_COLOR_GRAY1;
        } else {
            ch0 = tui_ascii_to_screen((unsigned char)('0' + cell_adj[idx]));
            ch1 = CHAR_BLANK;
            color = selected ? TUI_COLOR_WHITE : number_color(cell_adj[idx]);
        }
    } else if (cell_flagged[idx]) {
        ch0 = CHAR_FLAG;
        ch1 = CHAR_BLANK;
        color = selected ? TUI_COLOR_LIGHTGREEN : TUI_COLOR_YELLOW;
    } else {
        if (selected) {
            ch0 = CHAR_CHECKER;
            ch1 = CHAR_CHECKER;
            color = TUI_COLOR_WHITE;
        } else {
            ch0 = CHAR_BLOCK;
            ch1 = CHAR_BLOCK;
            color = TUI_COLOR_GRAY2;
        }
    }

    TUI_SCREEN[offset] = ch0;
    TUI_SCREEN[offset + 1] = ch1;
    TUI_COLOR_RAM[offset] = color;
    TUI_COLOR_RAM[offset + 1] = color;
}

static void draw_changed_cells(void) {
    unsigned int idx;
    unsigned int count;
    unsigned char sig;

    count = active_cell_count();
    for (idx = 0; idx < count; ++idx) {
        sig = render_signature(idx);
        if (dirty_full || draw_cache[idx] != sig) {
            render_cell(idx);
            draw_cache[idx] = sig;
        }
    }
    dirty_full = 0;
}

static void draw_board_frame(void) {
    TuiRect frame;
    unsigned char col;
    unsigned char row;
    unsigned char frame_x;
    unsigned char frame_y;
    unsigned char board_x;
    unsigned char board_y;
    static const char hex_chars[] = "0123456789ABCDEF";

    frame_x = board_frame_x();
    frame_y = board_frame_y();
    board_x = board_origin_x();
    board_y = board_origin_y();

    frame.x = frame_x;
    frame.y = frame_y;
    frame.w = board_frame_width();
    frame.h = board_frame_height();

    tui_window_title(&frame, level_name(), TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    for (col = 0; col < board_w; ++col) {
        tui_putc(board_screen_x(col), (unsigned char)(board_y - 1),
                 tui_ascii_to_screen((unsigned char)hex_chars[col]), TUI_COLOR_GRAY3);
        tui_putc((unsigned char)(board_screen_x(col) + 1), (unsigned char)(board_y - 1),
                 CHAR_BLANK, TUI_COLOR_GRAY3);
    }

    for (row = 0; row < board_h; ++row) {
        tui_putc((unsigned char)(board_x - 3), board_screen_y(row),
                 tui_ascii_to_screen((unsigned char)hex_chars[row]), TUI_COLOR_GRAY3);
        tui_putc((unsigned char)(board_x - 2), board_screen_y(row),
                 CHAR_BLANK, TUI_COLOR_GRAY3);
    }
}

static void draw_status_line(void) {
    unsigned char mines_left;
    unsigned char safe_left;
    char num_buf[5];

    if (flags_used >= mine_total) {
        mines_left = 0;
    } else {
        mines_left = (unsigned char)(mine_total - flags_used);
    }
    safe_left = (unsigned char)(total_safe - revealed_safe);

    tui_clear_line(STATUS_Y, 0, 40, TUI_COLOR_WHITE);
    draw_field(0, STATUS_Y, 13, level_name(),
               (level_id == LEVEL_INTERMEDIATE) ? TUI_COLOR_CYAN : TUI_COLOR_LIGHTGREEN);

    draw_field(13, STATUS_Y, 5, "LEFT", TUI_COLOR_GRAY3);
    u8_to_ascii(mines_left, num_buf);
    draw_field(18, STATUS_Y, 4, num_buf, TUI_COLOR_WHITE);

    draw_field(23, STATUS_Y, 5, "SAFE", TUI_COLOR_GRAY3);
    u8_to_ascii(safe_left, num_buf);
    draw_field(28, STATUS_Y, 3, num_buf, TUI_COLOR_WHITE);

    if (mode == MODE_PAUSE) {
        draw_field(32, STATUS_Y, 8, "PAUSED", TUI_COLOR_YELLOW);
    } else if (mode == MODE_WON) {
        draw_field(33, STATUS_Y, 7, "CLEAR", TUI_COLOR_LIGHTGREEN);
    } else if (mode == MODE_LOST) {
        draw_field(34, STATUS_Y, 6, "BOOM", TUI_COLOR_LIGHTRED);
    }
}

static void draw_help_line(void) {
    tui_clear_line(HELP_Y, 0, 40, TUI_COLOR_GRAY3);

    if (mode == MODE_MENU) {
        draw_field(0, HELP_Y, 40,
                   "RET/J:START  B/I:LEVEL  F8:HELP", TUI_COLOR_GRAY3);
    } else if (mode == MODE_PAUSE) {
        draw_field(0, HELP_Y, 40,
                   "RET/P:RESUME  R:RESTART  F8:HELP", TUI_COLOR_GRAY3);
    } else if (mode == MODE_WON) {
        draw_field(0, HELP_Y, 40,
                   "R:RESTART  M:MENU  F8:HELP", TUI_COLOR_GRAY3);
    } else if (mode == MODE_LOST) {
        draw_field(0, HELP_Y, 40,
                   "R:RETRY  M:MENU  F8:HELP", TUI_COLOR_GRAY3);
    } else {
        draw_field(0, HELP_Y, 40,
                   "RET/J:OPEN  SPC/K:FLAG  F8:HELP", TUI_COLOR_GRAY3);
    }
}

static void restore_pause_background(void) {
    restore_popup_background(PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H,
                             pause_saved_screen, pause_saved_color);
}

static void draw_pause_overlay(void) {
    TuiRect popup;

    popup.x = PAUSE_X;
    popup.y = PAUSE_Y;
    popup.w = PAUSE_W;
    popup.h = PAUSE_H;

    save_popup_background(PAUSE_X, PAUSE_Y, PAUSE_W, PAUSE_H,
                          pause_saved_screen, pause_saved_color);
    tui_window_title(&popup, "PAUSED", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_field(10, PAUSE_Y + 2, 12, "RET/P RESUME", TUI_COLOR_WHITE);
    draw_field(10, PAUSE_Y + 4, 12, "R     RESTART", TUI_COLOR_WHITE);
    draw_field(10, PAUSE_Y + 5, 12, "M     MENU", TUI_COLOR_WHITE);
    draw_field(10, PAUSE_Y + 6, 12, "F8    HELP", TUI_COLOR_WHITE);
}

static void update_keyrepeat_mode(void) {
    tui_keyrepeat_default();
}

static void draw_menu_option(unsigned char y, const char *text, unsigned char selected) {
    unsigned char color;
    unsigned char marker_x;
    unsigned char text_x;

    color = selected ? TUI_COLOR_WHITE : TUI_COLOR_GRAY3;
    marker_x = 5;
    text_x = 7;
    tui_putc(marker_x, y, selected ? tui_ascii_to_screen('>') : CHAR_BLANK,
             selected ? TUI_COLOR_LIGHTGREEN : color);
    draw_field(text_x, y, 28, text, color);
}

static void show_help_popup(void) {
    TuiRect win = {HELP_POPUP_X, HELP_POPUP_Y, HELP_POPUP_W, HELP_POPUP_H};
    unsigned char key;

    save_popup_background(HELP_POPUP_X, HELP_POPUP_Y, HELP_POPUP_W, HELP_POPUP_H,
                          help_saved_screen, help_saved_color);

    tui_window_title(&win, "DEMINER HELP", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_field(3, 6, 32, "READYOS DEMINER", TUI_COLOR_WHITE);
    draw_field(3, 7, 32, "ARROWS/WASD: MOVE", TUI_COLOR_GRAY3);
    draw_field(3, 8, 32, "RETURN/J: REVEAL CELL", TUI_COLOR_GRAY3);
    draw_field(3, 9, 32, "SPACE/K: FLAG OR UNFLAG", TUI_COLOR_GRAY3);
    draw_field(3, 10, 32, "P: PAUSE  R: RESTART  M: MENU", TUI_COLOR_GRAY3);
    draw_field(3, 11, 32, "B/I: PICK LEVEL FROM MENU", TUI_COLOR_GRAY3);
    draw_field(3, 12, 32, "LEFT SHOWS FLAGS REMAINING", TUI_COLOR_GRAY3);
    draw_field(3, 13, 32, "FIRST REVEAL IS ALWAYS SAFE", TUI_COLOR_GRAY3);
    draw_field(3, 14, 32, "OPEN EVERY SAFE CELL TO WIN", TUI_COLOR_GRAY3);
    draw_field(3, 15, 32, "F2/F4: APPS  CTRL+B: LAUNCHER", TUI_COLOR_GRAY3);
    draw_field(3, 16, 32, "RUN/STOP: QUIT APP", TUI_COLOR_GRAY3);
    draw_field(3, 17, 32, "F8/RET/STOP/LEFT: CLOSE", TUI_COLOR_CYAN);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_F8 || key == TUI_KEY_RETURN ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            restore_popup_background(HELP_POPUP_X, HELP_POPUP_Y,
                                     HELP_POPUP_W, HELP_POPUP_H,
                                     help_saved_screen, help_saved_color);
            return;
        }
    }
}

static void draw_menu(void) {
    TuiRect box;

    tui_clear(TUI_THEME_BG);
    VIC.bordercolor = TUI_THEME_BG;
    draw_header();
    draw_status_line();
    draw_help_line();

    box.x = 2;
    box.y = 7;
    box.w = 36;
    box.h = 11;
    tui_window_title(&box, "LEVEL", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    draw_field(8, 10, 24, "CLEAR EVERY SAFE CELL.", TUI_COLOR_WHITE);
    draw_field(9, 11, 22, "FLAG MINES YOU FIND.", TUI_COLOR_WHITE);
    draw_menu_option(13, "BEGINNER 9X9 10 MINES", menu_level == LEVEL_BEGINNER);
    draw_menu_option(15, "INTERMEDIATE 16X16 40 MINES", menu_level == LEVEL_INTERMEDIATE);
}

static void draw_game_screen(void) {
    tui_clear(TUI_THEME_BG);
    VIC.bordercolor = TUI_THEME_BG;
    draw_header();
    draw_status_line();
    draw_help_line();
    draw_board_frame();
    invalidate_draw_cache();
    draw_changed_cells();
}

static void enter_menu(void) {
    mode = MODE_MENU;
    configure_level(menu_level);
    revealed_safe = 0;
    flags_used = 0;
    mines_placed = 0;
    blast_index = 0xFF;
    update_keyrepeat_mode();
    draw_menu();
}

static void start_game(unsigned char level) {
    configure_level(level);
    menu_level = level_id;
    clear_board_state();
    cursor_x = (unsigned char)(board_w / 2);
    cursor_y = (unsigned char)(board_h / 2);
    revealed_safe = 0;
    flags_used = 0;
    mines_placed = 0;
    blast_index = 0xFF;
    mode = MODE_PLAY;
    update_keyrepeat_mode();
    draw_game_screen();
}

static void place_mines(unsigned int exclude_idx) {
    unsigned int placed;
    unsigned int active;
    unsigned int idx;
    unsigned char x;
    unsigned char y;
    int dx;
    int dy;
    int nx;
    int ny;
    unsigned char count;

    active = active_cell_count();
    placed = 0;
    while (placed < mine_total) {
        if (active == 256U) {
            idx = random8();
        } else {
            idx = (unsigned int)(random8() % active);
        }
        if (idx == exclude_idx || cell_mine[idx]) {
            continue;
        }
        cell_mine[idx] = 1;
        ++placed;
    }

    for (y = 0; y < board_h; ++y) {
        for (x = 0; x < board_w; ++x) {
            idx = board_index(x, y);
            if (cell_mine[idx]) {
                continue;
            }
            count = 0;
            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    nx = (int)x + dx;
                    ny = (int)y + dy;
                    if (nx < 0 || ny < 0 || nx >= (int)board_w || ny >= (int)board_h) {
                        continue;
                    }
                    if (cell_mine[board_index((unsigned char)nx, (unsigned char)ny)]) {
                        ++count;
                    }
                }
            }
            cell_adj[idx] = count;
        }
    }

    mines_placed = 1;
}

static void check_for_win(void) {
    if (revealed_safe >= total_safe) {
        mode = MODE_WON;
        dirty_full = 1;
        draw_changed_cells();
        draw_status_line();
        draw_help_line();
        draw_header();
        update_keyrepeat_mode();
    }
}

static void reveal_current_cell(void) {
    unsigned int idx;
    unsigned int head;
    unsigned int tail;
    unsigned int nidx;
    unsigned char qx;
    unsigned char qy;
    int dx;
    int dy;
    int nx;
    int ny;

    if (mode != MODE_PLAY) {
        return;
    }

    idx = board_index(cursor_x, cursor_y);
    if (cell_visible[idx] || cell_flagged[idx]) {
        return;
    }

    if (!mines_placed) {
        place_mines(idx);
    }

    if (cell_mine[idx]) {
        blast_index = (unsigned char)idx;
        mode = MODE_LOST;
        dirty_full = 1;
        draw_changed_cells();
        draw_status_line();
        draw_help_line();
        draw_header();
        update_keyrepeat_mode();
        return;
    }

    cell_visible[idx] = 1;
    ++revealed_safe;

    if (cell_adj[idx] == 0) {
        head = 0;
        tail = 0;
        reveal_queue[tail++] = (unsigned char)idx;

        while (head < tail) {
            nidx = reveal_queue[head++];
            qx = (unsigned char)(nidx % board_w);
            qy = (unsigned char)(nidx / board_w);

            for (dy = -1; dy <= 1; ++dy) {
                for (dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    nx = (int)qx + dx;
                    ny = (int)qy + dy;
                    if (nx < 0 || ny < 0 || nx >= (int)board_w || ny >= (int)board_h) {
                        continue;
                    }
                    idx = board_index((unsigned char)nx, (unsigned char)ny);
                    if (cell_visible[idx] || cell_flagged[idx] || cell_mine[idx]) {
                        continue;
                    }
                    cell_visible[idx] = 1;
                    ++revealed_safe;
                    if (cell_adj[idx] == 0 && tail < MAX_CELLS) {
                        reveal_queue[tail++] = (unsigned char)idx;
                    }
                }
            }
        }
    }

    draw_changed_cells();
    draw_status_line();
    check_for_win();
}

static void toggle_flag_current(void) {
    unsigned int idx;

    if (mode != MODE_PLAY) {
        return;
    }

    idx = board_index(cursor_x, cursor_y);
    if (cell_visible[idx]) {
        return;
    }

    if (cell_flagged[idx]) {
        cell_flagged[idx] = 0;
        if (flags_used > 0) {
            --flags_used;
        }
    } else {
        if (flags_used >= mine_total) {
            draw_status_line();
            return;
        }
        cell_flagged[idx] = 1;
        ++flags_used;
    }

    draw_changed_cells();
    draw_status_line();
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

static void move_cursor(signed char dir) {
    unsigned char old_x;
    unsigned char old_y;

    old_x = cursor_x;
    old_y = cursor_y;

    if (dir == 0 && cursor_y > 0) {
        --cursor_y;
    } else if (dir == 1 && cursor_y + 1 < board_h) {
        ++cursor_y;
    } else if (dir == 2 && cursor_x > 0) {
        --cursor_x;
    } else if (dir == 3 && cursor_x + 1 < board_w) {
        ++cursor_x;
    }

    if (old_x != cursor_x || old_y != cursor_y) {
        draw_changed_cells();
    }
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

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }

    resume_blob.level_id = level_id;
    resume_blob.menu_level = menu_level;
    resume_blob.mode = mode;
    resume_blob.cursor_x = cursor_x;
    resume_blob.cursor_y = cursor_y;
    resume_blob.revealed_safe = revealed_safe;
    resume_blob.flags_used = flags_used;
    resume_blob.mines_placed = mines_placed;
    resume_blob.blast_index = blast_index;
    resume_blob.rng_state = rng_state;
    memcpy(resume_blob.cell_mine, cell_mine, sizeof(cell_mine));
    memcpy(resume_blob.cell_adj, cell_adj, sizeof(cell_adj));
    memcpy(resume_blob.cell_visible, cell_visible, sizeof(cell_visible));
    memcpy(resume_blob.cell_flagged, cell_flagged, sizeof(cell_flagged));

    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len;
    unsigned int idx;
    unsigned int count;

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
    if (resume_blob.level_id >= LEVEL_COUNT || resume_blob.menu_level >= LEVEL_COUNT) {
        return 0;
    }
    if (resume_blob.mode > MODE_LOST) {
        return 0;
    }

    configure_level(resume_blob.level_id);
    if (resume_blob.cursor_x >= board_w || resume_blob.cursor_y >= board_h) {
        return 0;
    }
    if (resume_blob.flags_used > active_cell_count()) {
        return 0;
    }
    if (resume_blob.revealed_safe > total_safe) {
        return 0;
    }
    if (resume_blob.mines_placed > 1) {
        return 0;
    }
    if (resume_blob.blast_index != 0xFF &&
        resume_blob.blast_index >= active_cell_count()) {
        return 0;
    }

    count = active_cell_count();
    for (idx = 0; idx < count; ++idx) {
        if (resume_blob.cell_mine[idx] > 1 ||
            resume_blob.cell_visible[idx] > 1 ||
            resume_blob.cell_flagged[idx] > 1 ||
            resume_blob.cell_adj[idx] > 8) {
            return 0;
        }
    }

    level_id = resume_blob.level_id;
    menu_level = resume_blob.menu_level;
    mode = resume_blob.mode;
    cursor_x = resume_blob.cursor_x;
    cursor_y = resume_blob.cursor_y;
    revealed_safe = resume_blob.revealed_safe;
    flags_used = resume_blob.flags_used;
    mines_placed = resume_blob.mines_placed;
    blast_index = resume_blob.blast_index;
    rng_state = resume_blob.rng_state;
    if (rng_state == 0) {
        rng_state = (unsigned int)0xBEEF ^ ((unsigned int)RASTER_LINE << 8) ^ JIFFY_LO;
    }

    clear_board_state();
    memcpy(cell_mine, resume_blob.cell_mine, sizeof(cell_mine));
    memcpy(cell_adj, resume_blob.cell_adj, sizeof(cell_adj));
    memcpy(cell_visible, resume_blob.cell_visible, sizeof(cell_visible));
    memcpy(cell_flagged, resume_blob.cell_flagged, sizeof(cell_flagged));

    update_keyrepeat_mode();
    return 1;
}

static void deminer_loop(void) {
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

        dir = key_to_direction(key);

        if (mode == MODE_MENU) {
            if (dir >= 0) {
                menu_level = (unsigned char)(menu_level ^ 1);
                draw_menu();
                continue;
            }
            if (key == 'b' || key == 'B') {
                menu_level = LEVEL_BEGINNER;
                start_game(menu_level);
                continue;
            }
            if (key == TUI_KEY_F8) {
                show_help_popup();
                continue;
            }
            if (key == 'i' || key == 'I') {
                menu_level = LEVEL_INTERMEDIATE;
                start_game(menu_level);
                continue;
            }
            if (key == TUI_KEY_RETURN || key == 'j' || key == 'J') {
                start_game(menu_level);
            }
            continue;
        }

        if (mode == MODE_PAUSE) {
            if (key == 'p' || key == 'P' || key == TUI_KEY_RETURN) {
                mode = MODE_PLAY;
                update_keyrepeat_mode();
                restore_pause_background();
                draw_header();
                draw_status_line();
                draw_help_line();
                continue;
            }
            if (key == TUI_KEY_F8) {
                show_help_popup();
                continue;
            }
            if (key == 'r' || key == 'R') {
                restore_pause_background();
                start_game(level_id);
                continue;
            }
            if (key == 'm' || key == 'M') {
                restore_pause_background();
                enter_menu();
                continue;
            }
            continue;
        }

        if (dir >= 0) {
            move_cursor(dir);
            continue;
        }

        if (mode == MODE_WON || mode == MODE_LOST) {
            if (key == TUI_KEY_F8) {
                show_help_popup();
                continue;
            }
            if (key == 'r' || key == 'R') {
                start_game(level_id);
                continue;
            }
            if (key == 'm' || key == 'M' || key == TUI_KEY_RETURN) {
                enter_menu();
                continue;
            }
            if (key == 'b' || key == 'B') {
                start_game(LEVEL_BEGINNER);
                continue;
            }
            if (key == 'i' || key == 'I') {
                start_game(LEVEL_INTERMEDIATE);
            }
            continue;
        }

        if (key == 'p' || key == 'P') {
            mode = MODE_PAUSE;
            update_keyrepeat_mode();
            draw_header();
            draw_status_line();
            draw_help_line();
            draw_pause_overlay();
            continue;
        }

        if (key == TUI_KEY_F8) {
            show_help_popup();
            continue;
        }

        if (key == 'r' || key == 'R') {
            start_game(level_id);
            continue;
        }

        if (key == 'm' || key == 'M') {
            enter_menu();
            continue;
        }

        if (key == TUI_KEY_RETURN || key == 'j' || key == 'J') {
            reveal_current_cell();
            continue;
        }

        if (key == ' ' || key == 'k' || key == 'K') {
            toggle_flag_current();
        }
    }
}

int main(void) {
    unsigned char bank;

    rng_state = (unsigned int)0xBEEF ^ ((unsigned int)RASTER_LINE << 8) ^ JIFFY_LO;
    running = 1;
    resume_ready = 0;
    menu_level = LEVEL_BEGINNER;
    level_id = LEVEL_BEGINNER;
    mode = MODE_MENU;
    blast_index = 0xFF;

    tui_init();

    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 23) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }

    if (resume_restore_state()) {
        if (mode == MODE_MENU) {
            draw_menu();
        } else {
            draw_game_screen();
            if (mode == MODE_PAUSE) {
                draw_pause_overlay();
            }
        }
    } else {
        enter_menu();
    }

    deminer_loop();

    __asm__("jmp $FCE2");
    return 0;
}
