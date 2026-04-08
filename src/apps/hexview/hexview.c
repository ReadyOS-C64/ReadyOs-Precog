/*
 * hexview.c - Ready OS Hex Memory Viewer
 * Browse C64 memory in hex format
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include <c64.h>
#include <conio.h>
#include <string.h>

#include "../../lib/clipboard.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

/* Layout */
#define TITLE_Y    0
#define HEX_START_Y 4
#define STATUS_Y   22
#define HELP_Y     23

/* Hex viewer */
#define HEX_BYTES_PER_LINE 8
#define HEX_LINES 14

/* Right-column interpretation mode */
#define CHAR_MODE_PETSCII 0
#define CHAR_MODE_SCREEN  1

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Static variables
 *---------------------------------------------------------------------------*/

/* Hex viewer state */
static unsigned int hex_address;

/* App state */
static unsigned char running;
static unsigned char char_mode;

typedef struct {
    unsigned int hex_address;
    unsigned char char_mode;
} HexViewResumeV1;

static HexViewResumeV1 resume_blob;
static unsigned char resume_ready;

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    resume_blob.hex_address = hex_address;
    resume_blob.char_mode = char_mode;
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
    if (resume_blob.hex_address > 0xFF80) {
        return 0;
    }
    if (resume_blob.char_mode > CHAR_MODE_SCREEN) {
        return 0;
    }

    hex_address = resume_blob.hex_address;
    char_mode = resume_blob.char_mode;
    return 1;
}

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/
static void hexview_init(void);
static void hexview_loop(void);
static void draw_hex_static(void);
static void draw_hex_data(void);
static void draw_hex_line(unsigned char y, unsigned int addr);
static void handle_hex_key(unsigned char key);
static void show_help_screen(void);
static unsigned char petscii_to_screen_safe(unsigned char b);
static unsigned char byte_to_visual_screen(unsigned char b);
static void draw_char_mode_label(void);

/*---------------------------------------------------------------------------
 * Initialization
 *---------------------------------------------------------------------------*/

static void hexview_init(void) {
    tui_init();
    reu_mgr_init();

    hex_address = 0x0400;  /* Default to screen memory */
    char_mode = CHAR_MODE_PETSCII;
    running = 1;
}

/*---------------------------------------------------------------------------
 * Hex Viewer Drawing
 *---------------------------------------------------------------------------*/

static void draw_hex_line(unsigned char y, unsigned int addr) {
    unsigned char *ptr;
    unsigned char i, b;

    ptr = (unsigned char *)addr;

    /* Address */
    tui_print_hex16(0, y, addr, TUI_COLOR_CYAN);
    tui_puts(5, y, ":", TUI_COLOR_GRAY3);

    /* Hex bytes */
    for (i = 0; i < HEX_BYTES_PER_LINE; ++i) {
        b = ptr[i];
        tui_print_hex8(7 + i * 3, y, b, TUI_COLOR_WHITE);

        /* Visual column: PETSCII or raw screen-code view */
        tui_putc((unsigned char)(32 + i), y, byte_to_visual_screen(b),
                 TUI_COLOR_LIGHTGREEN);
    }
}

static void draw_hex_static(void) {
    TuiRect box;

    /* Clear screen */
    tui_clear(TUI_COLOR_BLUE);

    /* Title frame */
    box.x = 0;
    box.y = TITLE_Y;
    box.w = 40;
    box.h = 2;
    tui_window(&box, TUI_COLOR_LIGHTBLUE);

    tui_puts(1, TITLE_Y, "HEX VIEW:", TUI_COLOR_WHITE);

    /* Column headers */
    tui_puts(7, TITLE_Y + 1, "00 01 02 03 04 05 06 07  VIEW",
             TUI_COLOR_GRAY3);
    draw_char_mode_label();

    /* Help */
    tui_puts(1, HELP_Y, "UP/DN:SCROLL C:COPY F6:MODE F8:HELP",
             TUI_COLOR_GRAY3);
    tui_puts(1, HELP_Y + 1, "F2/F4:APPS ^B:HOME STOP:QUIT",
             TUI_COLOR_GRAY3);
}

static void draw_hex_data(void) {
    unsigned char i;
    unsigned int addr;

    /* Title address (overwrite in-place) */
    tui_print_hex16(11, TITLE_Y, hex_address, TUI_COLOR_YELLOW);

    /* Hex dump lines */
    addr = hex_address;
    for (i = 0; i < HEX_LINES; ++i) {
        draw_hex_line(HEX_START_Y + i, addr);
        addr += HEX_BYTES_PER_LINE;
    }

    /* Status line (overwrite in-place) */
    tui_puts_n(1, STATUS_Y, "VIEWING:", 8, TUI_COLOR_WHITE);
    tui_print_hex16(10, STATUS_Y, hex_address, TUI_COLOR_CYAN);
    tui_puts(16, STATUS_Y, "-", TUI_COLOR_WHITE);
    tui_print_hex16(17, STATUS_Y, hex_address + (HEX_LINES * HEX_BYTES_PER_LINE) - 1,
                    TUI_COLOR_CYAN);
    /* Pad rest of status line */
    tui_puts_n(23, STATUS_Y, "", 17, TUI_COLOR_WHITE);
    draw_char_mode_label();
}

/*---------------------------------------------------------------------------
 * Hex Viewer Input
 *---------------------------------------------------------------------------*/

static void handle_hex_key(unsigned char key) {
    switch (key) {
        case TUI_KEY_UP:
            if (hex_address >= HEX_BYTES_PER_LINE) {
                hex_address -= HEX_BYTES_PER_LINE;
            }
            break;

        case TUI_KEY_DOWN:
            if (hex_address < 0xFF80) {
                hex_address += HEX_BYTES_PER_LINE;
            }
            break;

        case TUI_KEY_LEFT:
            if (hex_address >= HEX_BYTES_PER_LINE * HEX_LINES) {
                hex_address -= HEX_BYTES_PER_LINE * HEX_LINES;
            } else {
                hex_address = 0;
            }
            break;

        case TUI_KEY_RIGHT:
            if (hex_address < 0xFF00 - HEX_BYTES_PER_LINE * HEX_LINES) {
                hex_address += HEX_BYTES_PER_LINE * HEX_LINES;
            }
            break;

        case 'c':
        case 'C':
            /* Copy current line to clipboard */
            {
                static char hex_buf[48];
                unsigned char i, b;
                unsigned char *ptr = (unsigned char *)hex_address;

                for (i = 0; i < 16; ++i) {
                    b = ptr[i];
                    hex_buf[i * 3] = "0123456789ABCDEF"[(b >> 4) & 0x0F];
                    hex_buf[i * 3 + 1] = "0123456789ABCDEF"[b & 0x0F];
                    hex_buf[i * 3 + 2] = ' ';
                }
                hex_buf[47] = 0;
                clip_copy(CLIP_TYPE_TEXT, hex_buf, 47);
            }
            break;

        case TUI_KEY_F8:
            show_help_screen();
            break;

        case TUI_KEY_F6:
            if (char_mode == CHAR_MODE_PETSCII) {
                char_mode = CHAR_MODE_SCREEN;
            } else {
                char_mode = CHAR_MODE_PETSCII;
            }
            break;

        case TUI_KEY_RUNSTOP:
            running = 0;
            break;
    }
}

static unsigned char petscii_to_screen_safe(unsigned char b) {
    /* PETSCII controls: leave blank. */
    if (b < 0x20 || (b >= 0x80 && b < 0xA0)) {
        return 32;
    }

    /* DEL in both sets: show placeholder to mark a non-printable byte. */
    if (b == 0x7F || b == 0xFF) {
        return 46; /* '.' */
    }

    /* Fold shifted/reverse printable sets into base printable ranges. */
    if (b >= 0xA0) {
        b = (unsigned char)(b - 0x80);
    }

    if (b >= 0x20 && b <= 0x3F) {
        return b;
    }
    if (b >= 0x40 && b <= 0x5F) {
        return (unsigned char)(b - 0x40);
    }
    if (b >= 0x60 && b <= 0x7E) {
        return (unsigned char)(b - 0x20);
    }

    return 32;
}

static unsigned char byte_to_visual_screen(unsigned char b) {
    if (char_mode == CHAR_MODE_SCREEN) {
        /* Raw screen-code byte; includes graphics and reverse-video bit. */
        return b;
    }
    return petscii_to_screen_safe(b);
}

static void draw_char_mode_label(void) {
    if (char_mode == CHAR_MODE_SCREEN) {
        tui_puts_n(36, TITLE_Y + 1, "SCR", 3, TUI_COLOR_LIGHTGREEN);
    } else {
        tui_puts_n(36, TITLE_Y + 1, "PET", 3, TUI_COLOR_LIGHTGREEN);
    }
}

static void show_help_screen(void) {
    tui_clear(TUI_COLOR_BLUE);
    tui_puts_n(1, 1, "HEXVIEW PETSCII HELP", 38, TUI_COLOR_YELLOW);
    tui_puts_n(1, 3, "F6 TOGGLES RIGHT-COLUMN MODE:", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 4, "PET = BYTE AS PETSCII", 38, TUI_COLOR_LIGHTGREEN);
    tui_puts_n(1, 5, "SCR = BYTE AS RAW SCREEN CODE", 38, TUI_COLOR_LIGHTGREEN);
    tui_puts_n(1, 7, "PET SPACE-RULE:", 38, TUI_COLOR_CYAN);
    tui_puts_n(1, 8, "00-1F, 80-9F -> SPACE (CONTROLS)", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 9, "PET SUBSTITUTE-RULE:", 38, TUI_COLOR_CYAN);
    tui_puts_n(1, 10, "7F, FF -> '.' (DEL PLACEHOLDER)", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 11, "A0-FF -> FOLDED TO BASE GLYPHS", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 13, "SCR MODE: NO SUBSTITUTION,", 38, TUI_COLOR_CYAN);
    tui_puts_n(1, 14, "BYTE WRITTEN DIRECTLY AS SCREEN CODE.", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 16, "F8 OR ANY KEY: RETURN", 38, TUI_COLOR_YELLOW);
    (void)tui_getkey();

    draw_hex_static();
    draw_hex_data();
}

/*---------------------------------------------------------------------------
 * Main Loop
 *---------------------------------------------------------------------------*/

static void hexview_loop(void) {
    unsigned char key;
    unsigned char nav_action;

    draw_hex_static();
    draw_hex_data();

    while (running) {
        /* Get input */
        key = tui_getkey();
        nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
        if (nav_action == TUI_HOTKEY_LAUNCHER) {
            resume_save_state();
            tui_return_to_launcher();
        }
        if (nav_action >= 1 && nav_action <= 15) {
            resume_save_state();
            tui_switch_to_app(nav_action);
        }
        if (nav_action == TUI_HOTKEY_BIND_ONLY) {
            continue;
        }

        handle_hex_key(key);
        draw_hex_data();
    }

    /* Reset to BASIC */
    __asm__("jmp $FCE2");
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

int main(void) {
    unsigned char bank;

    hexview_init();
    resume_ready = 0;

    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();

    hexview_loop();
    return 0;
}
