/*
 * reuviewer.c - Ready OS REU Memory Map Viewer
 * Visual 16x16 grid showing all 256 REU banks
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"
#include <c64.h>
#include <conio.h>

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define TITLE_Y    0
#define GRID_Y     4
#define GRID_ROWS  16
#define GRID_COLS  16
#define DETAIL_Y   20
#define HELP_Y     22
#define STATUS_Y   24

/* Screen codes for bank type display */
#define CHAR_FREE   0x2E  /* '.' screen code */
#define CHAR_APP    0x01  /* 'A' screen code */
#define CHAR_CLIP   0x03  /* 'C' screen code */
#define CHAR_ALLOC  0x15  /* 'U' screen code (user/alloc) */
#define CHAR_RSVD   0x12  /* 'R' screen code (reserved app slot) */
#define CHAR_RS1    49    /* '1' screen code */
#define CHAR_RS2    50    /* '2' screen code */
#define CHAR_RSD    0x04  /* 'D' screen code */

#define SHIM_CURRENT_BANK ((unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Static variables
 *---------------------------------------------------------------------------*/

static unsigned char running;
static unsigned char cursor_x;  /* 0-15 in grid */
static unsigned char cursor_y_pos;  /* 0-15 in grid */

typedef struct {
    unsigned char cursor_x;
    unsigned char cursor_y_pos;
} ReuViewerResumeV1;

static ReuViewerResumeV1 resume_blob;
static unsigned char resume_ready;

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    resume_blob.cursor_x = cursor_x;
    resume_blob.cursor_y_pos = cursor_y_pos;
    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_try_load(&resume_blob, sizeof(resume_blob), &payload_len)) {
        return 0;
    }
    if (payload_len != sizeof(resume_blob)) {
        return 0;
    }
    if (resume_blob.cursor_x >= GRID_COLS || resume_blob.cursor_y_pos >= GRID_ROWS) {
        return 0;
    }

    cursor_x = resume_blob.cursor_x;
    cursor_y_pos = resume_blob.cursor_y_pos;
    return 1;
}

/*---------------------------------------------------------------------------
 * Drawing
 *---------------------------------------------------------------------------*/

static void draw_header(void) {
    TuiRect box = {0, TITLE_Y, 40, 3};
    tui_window_title(&box, "REU MEMORY MAP",
                     TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
}

static void draw_summary(void) {
    unsigned char free_count;
    unsigned char app_count;
    unsigned char clip_count;
    unsigned char alloc_count;
    unsigned char rsv_count;
    unsigned char rs1_count;
    unsigned char rs2_count;
    unsigned char rsd_count;
    unsigned char rs_count;

    free_count = reu_count_free();
    app_count = reu_count_type(REU_APP_STATE);
    clip_count = reu_count_type(REU_CLIPBOARD);
    alloc_count = reu_count_type(REU_APP_ALLOC);
    rsv_count = reu_count_type(REU_RESERVED);
    rs1_count = reu_count_type(REU_RS_OVL1);
    rs2_count = reu_count_type(REU_RS_OVL2);
    rsd_count = reu_count_type(REU_RS_DEBUG);
    rs_count = (unsigned char)(rs1_count + rs2_count + rsd_count);

    tui_puts(1, TITLE_Y + 1, "256 BANKS", TUI_COLOR_WHITE);

    tui_puts(12, TITLE_Y + 1, "F:", TUI_COLOR_GRAY2);
    tui_print_uint(14, TITLE_Y + 1, free_count, TUI_COLOR_GRAY2);

    tui_puts(19, TITLE_Y + 1, "A:", TUI_COLOR_CYAN);
    tui_print_uint(21, TITLE_Y + 1, app_count, TUI_COLOR_CYAN);

    tui_puts(25, TITLE_Y + 1, "C:", TUI_COLOR_YELLOW);
    tui_print_uint(27, TITLE_Y + 1, clip_count, TUI_COLOR_YELLOW);

    tui_puts(31, TITLE_Y + 1, "U:", TUI_COLOR_LIGHTGREEN);
    tui_print_uint(33, TITLE_Y + 1, alloc_count, TUI_COLOR_LIGHTGREEN);

    tui_puts(36, TITLE_Y + 1, "R:", TUI_COLOR_LIGHTRED);
    tui_print_uint(38, TITLE_Y + 1, rsv_count, TUI_COLOR_LIGHTRED);

    tui_clear_line(TITLE_Y + 2, 0, 40, TUI_COLOR_WHITE);
    tui_puts(1, TITLE_Y + 2, "RS:", TUI_COLOR_GRAY2);
    tui_print_uint(4, TITLE_Y + 2, rs_count, TUI_COLOR_WHITE);
    tui_puts(8, TITLE_Y + 2, "1:", TUI_COLOR_LIGHTBLUE);
    tui_print_uint(10, TITLE_Y + 2, rs1_count, TUI_COLOR_LIGHTBLUE);
    tui_puts(14, TITLE_Y + 2, "2:", TUI_COLOR_GREEN);
    tui_print_uint(16, TITLE_Y + 2, rs2_count, TUI_COLOR_GREEN);
    tui_puts(22, TITLE_Y + 2, "D:", TUI_COLOR_ORANGE);
    tui_print_uint(24, TITLE_Y + 2, rsd_count, TUI_COLOR_ORANGE);
}

static void draw_legend(void) {
    tui_clear_line(3, 0, 40, TUI_COLOR_GRAY3);

    tui_putc(0, 3, CHAR_FREE, TUI_COLOR_GRAY2);
    tui_puts(1, 3, "F", TUI_COLOR_GRAY3);
    tui_putc(4, 3, CHAR_APP, TUI_COLOR_CYAN);
    tui_puts(5, 3, "A", TUI_COLOR_GRAY3);
    tui_putc(8, 3, CHAR_CLIP, TUI_COLOR_YELLOW);
    tui_puts(9, 3, "C", TUI_COLOR_GRAY3);
    tui_putc(12, 3, CHAR_ALLOC, TUI_COLOR_LIGHTGREEN);
    tui_puts(13, 3, "U", TUI_COLOR_GRAY3);
    tui_putc(16, 3, CHAR_RSVD, TUI_COLOR_LIGHTRED);
    tui_puts(17, 3, "R", TUI_COLOR_GRAY3);
    tui_putc(20, 3, CHAR_RS1, TUI_COLOR_LIGHTBLUE);
    tui_puts(21, 3, "1", TUI_COLOR_GRAY3);
    tui_putc(24, 3, CHAR_RS2, TUI_COLOR_GREEN);
    tui_puts(25, 3, "2", TUI_COLOR_GRAY3);
    tui_putc(28, 3, CHAR_RSD, TUI_COLOR_ORANGE);
    tui_puts(29, 3, "D", TUI_COLOR_GRAY3);
    tui_puts(36, 3, "RS", TUI_COLOR_GRAY3);
}

static void draw_grid(void) {
    unsigned char row, col;
    unsigned char bank;
    unsigned char type;
    unsigned char ch;
    unsigned char color;
    unsigned char screen_x, screen_y;
    unsigned int offset;

    /* Column headers: 0-F */
    for (col = 0; col < GRID_COLS; ++col) {
        tui_putc(4 + col * 2, GRID_Y - 1,
                 tui_ascii_to_screen("0123456789ABCDEF"[col]),
                 TUI_COLOR_GRAY3);
    }

    for (row = 0; row < GRID_ROWS; ++row) {
        screen_y = GRID_Y + row;

        /* Row header: 0x-Fx */
        tui_putc(1, screen_y,
                 tui_ascii_to_screen("0123456789ABCDEF"[row]),
                 TUI_COLOR_GRAY3);
        tui_putc(2, screen_y, tui_ascii_to_screen('x'), TUI_COLOR_GRAY3);

        for (col = 0; col < GRID_COLS; ++col) {
            bank = row * 16 + col;
            type = reu_bank_type(bank);
            screen_x = 4 + col * 2;

            switch (type) {
                case REU_APP_STATE:
                    ch = CHAR_APP;
                    color = TUI_COLOR_CYAN;
                    break;
                case REU_CLIPBOARD:
                    ch = CHAR_CLIP;
                    color = TUI_COLOR_YELLOW;
                    break;
                case REU_APP_ALLOC:
                    ch = CHAR_ALLOC;
                    color = TUI_COLOR_LIGHTGREEN;
                    break;
                case REU_RESERVED:
                    ch = CHAR_RSVD;
                    color = TUI_COLOR_LIGHTRED;
                    break;
                case REU_RS_OVL1:
                    ch = CHAR_RS1;
                    color = TUI_COLOR_LIGHTBLUE;
                    break;
                case REU_RS_OVL2:
                    ch = CHAR_RS2;
                    color = TUI_COLOR_GREEN;
                    break;
                case REU_RS_DEBUG:
                    ch = CHAR_RSD;
                    color = TUI_COLOR_ORANGE;
                    break;
                default:
                    ch = CHAR_FREE;
                    color = TUI_COLOR_GRAY2;
                    break;
            }

            offset = (unsigned int)screen_y * 40 + screen_x;

            /* If this is the cursor position, show reversed */
            if (row == cursor_y_pos && col == cursor_x) {
                TUI_SCREEN[offset] = ch | 0x80;  /* Reverse */
                TUI_COLOR_RAM[offset] = TUI_COLOR_WHITE;
            } else {
                TUI_SCREEN[offset] = ch;
                TUI_COLOR_RAM[offset] = color;
            }
        }
    }
}

static void draw_detail(void) {
    unsigned char bank;
    unsigned char type;
    const char *type_str;

    bank = cursor_y_pos * 16 + cursor_x;
    type = reu_bank_type(bank);

    tui_clear_line(DETAIL_Y, 0, 40, TUI_COLOR_WHITE);
    tui_clear_line(DETAIL_Y + 1, 0, 40, TUI_COLOR_WHITE);

    tui_puts(1, DETAIL_Y, "BANK ", TUI_COLOR_WHITE);
    tui_print_hex8(6, DETAIL_Y, bank, TUI_COLOR_CYAN);

    tui_puts(12, DETAIL_Y, "TYPE: ", TUI_COLOR_WHITE);

    switch (type) {
        case REU_FREE:      type_str = "FREE"; break;
        case REU_APP_STATE: type_str = "APP STATE"; break;
        case REU_CLIPBOARD: type_str = "CLIPBOARD"; break;
        case REU_APP_ALLOC: type_str = "APP ALLOC"; break;
        case REU_RESERVED:  type_str = "APP SLOT RSV"; break;
        case REU_RS_OVL1:   type_str = "RS OVL1 CACHE"; break;
        case REU_RS_OVL2:   type_str = "RS OVL2 CACHE"; break;
        case REU_RS_DEBUG:  type_str = "RS DEBUG/PROBE"; break;
        default:            type_str = "UNKNOWN"; break;
    }
    tui_puts(18, DETAIL_Y, type_str, TUI_COLOR_YELLOW);

    /* Show bank number in decimal too */
    tui_puts(1, DETAIL_Y + 1, "DECIMAL: ", TUI_COLOR_GRAY3);
    tui_print_uint(10, DETAIL_Y + 1, bank, TUI_COLOR_WHITE);
}

static void draw_help(void) {
    tui_puts(0, HELP_Y, "CURSORS:NAVIGATE  READ-ONLY VIEW", TUI_COLOR_GRAY3);
    tui_puts(0, HELP_Y + 1, "1/2/D:RS  F2/F4:APPS  CTRL+B", TUI_COLOR_GRAY3);
}

static void draw_status(void) {
    unsigned char free_count = reu_count_free();
    tui_puts_n(0, STATUS_Y, "FREE: ", 6, TUI_COLOR_GRAY3);
    tui_print_uint(6, STATUS_Y, free_count, TUI_COLOR_WHITE);
    tui_puts(10, STATUS_Y, "/ 256 BANKS", TUI_COLOR_GRAY3);
}

static void reuviewer_draw(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    draw_summary();
    draw_legend();
    draw_grid();
    draw_detail();
    draw_help();
    draw_status();
}

/*---------------------------------------------------------------------------
 * Main loop
 *---------------------------------------------------------------------------*/

static void reuviewer_loop(void) {
    unsigned char key;
    unsigned char nav_action;

    reuviewer_draw();

    while (running) {
        key = tui_getkey();
        nav_action = tui_handle_global_hotkey(key, *SHIM_CURRENT_BANK, 1);
        if (nav_action == TUI_HOTKEY_LAUNCHER) {
            resume_save_state();
            tui_return_to_launcher();
        }
        if (nav_action >= 1 && nav_action <= 15) {
            resume_save_state();
            tui_switch_to_app(nav_action);
            continue;
        }
        if (nav_action == TUI_HOTKEY_BIND_ONLY) {
            continue;
        }

        switch (key) {
            case TUI_KEY_UP:
                if (cursor_y_pos > 0) {
                    --cursor_y_pos;
                    draw_grid();
                    draw_detail();
                }
                break;

            case TUI_KEY_DOWN:
                if (cursor_y_pos < GRID_ROWS - 1) {
                    ++cursor_y_pos;
                    draw_grid();
                    draw_detail();
                }
                break;

            case TUI_KEY_LEFT:
                if (cursor_x > 0) {
                    --cursor_x;
                    draw_grid();
                    draw_detail();
                }
                break;

            case TUI_KEY_RIGHT:
                if (cursor_x < GRID_COLS - 1) {
                    ++cursor_x;
                    draw_grid();
                    draw_detail();
                }
                break;

            case TUI_KEY_RUNSTOP:
                running = 0;
                break;
        }
    }

    __asm__("jmp $FCE2");
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

int main(void) {
    unsigned char bank;

    tui_init();
    reu_mgr_init();

    resume_ready = 0;
    bank = *SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }

    if (!resume_restore_state()) {
        cursor_x = 0;
        cursor_y_pos = 0;
    }
    running = 1;

    reuviewer_loop();
    return 0;
}
