/*
 * readme.c - Ready OS README viewer (generated markdown-lite pages)
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include "../../lib/textfile_screen.h"
#include "../../lib/resume_state.h"
#include "../../generated/readme_pages.h"
#include <c64.h>
#include <conio.h>

/* Layout */
#define TITLE_Y      0
#define CONTENT_X    1
#define CONTENT_Y    3
#define STATUS_Y     22
#define HELP_Y       23

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

static unsigned char running;
static unsigned char page_index;
static unsigned char resume_ready;

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    (void)resume_save(&page_index, sizeof(page_index));
}


static unsigned char style_to_color(unsigned char style) {
    switch (style) {
        case README_STYLE_H1:
            return TUI_COLOR_YELLOW;
        case README_STYLE_H2:
            return TUI_COLOR_LIGHTBLUE;
        case README_STYLE_H3:
            return TUI_COLOR_LIGHTGREEN;
        case README_STYLE_BOLD:
            return TUI_COLOR_LIGHTGREEN;
        case README_STYLE_ITALIC:
            return TUI_COLOR_CYAN;
        default:
            return TUI_COLOR_WHITE;
    }
}


static void draw_frame(void) {
    TuiRect header = {0, TITLE_Y, 40, 3};

    tui_clear(TUI_COLOR_BLUE);
    tui_window_title(&header, "READYOS README", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(2, 1, "PRECOG: ARCHITECTURE + APP GUIDE", TUI_COLOR_CYAN);

    tui_puts(0, HELP_Y, "CURSOR:PAGE HOME:FIRST F2/F4:APPS", TUI_COLOR_GRAY3);
    tui_puts(0, HELP_Y + 1, "CTRL+B:HOME  STOP:QUIT", TUI_COLOR_GRAY3);
}


static void draw_page(void) {
    unsigned char row;
    unsigned char x;
    unsigned char i;
    unsigned char mode;
    unsigned int page_num;
    unsigned int page_total;

    page_num = (unsigned int)page_index + 1;
    page_total = (unsigned int)README_PAGE_COUNT;

    for (row = 0; row < README_LINES_PER_PAGE; ++row) {
        const char *line = readme_page_line_text[page_index][row];
        unsigned char base_style = readme_page_line_style[page_index][row];
        unsigned char base_color = style_to_color(base_style);

        x = 0;
        i = 0;
        mode = 0;

        while (line[i] != 0 && x < README_LINE_WIDTH) {
            unsigned char ch = (unsigned char)line[i++];
            unsigned char color;

            if (ch == README_MARK_PREFIX) {
                unsigned char next = (unsigned char)line[i];
                if (next == README_MARK_PREFIX) {
                    ++i;
                    ch = README_MARK_PREFIX;
                } else if (next == README_MARK_BOLD) {
                    ++i;
                    mode = (mode == README_STYLE_BOLD) ? 0 : README_STYLE_BOLD;
                    continue;
                } else if (next == README_MARK_ITALIC) {
                    ++i;
                    mode = (mode == README_STYLE_ITALIC) ? 0 : README_STYLE_ITALIC;
                    continue;
                }
            }

            if (mode == README_STYLE_BOLD || mode == README_STYLE_ITALIC) {
                color = style_to_color(mode);
            } else {
                color = base_color;
            }

            tui_putc((unsigned char)(CONTENT_X + x),
                     (unsigned char)(CONTENT_Y + row),
                     textfile_byte_to_screen(ch),
                     color);
            ++x;
        }

        while (x < README_LINE_WIDTH) {
            tui_putc((unsigned char)(CONTENT_X + x),
                     (unsigned char)(CONTENT_Y + row),
                     tui_ascii_to_screen(' '),
                     base_color);
            ++x;
        }
    }

    tui_clear_line(STATUS_Y, 0, 40, TUI_COLOR_WHITE);
    tui_puts(1, STATUS_Y, "PAGE", TUI_COLOR_GRAY3);
    tui_print_uint(6, STATUS_Y, page_num, TUI_COLOR_WHITE);
    tui_puts(8, STATUS_Y, "/", TUI_COLOR_GRAY3);
    tui_print_uint(9, STATUS_Y, page_total, TUI_COLOR_WHITE);
}


static void readme_loop(void) {
    unsigned char key;
    unsigned char nav_action;

    draw_frame();
    draw_page();

    while (running) {
        key = tui_getkey();
        nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
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
            case TUI_KEY_LEFT:
            case TUI_KEY_UP:
                if (page_index > 0) {
                    --page_index;
                    draw_page();
                }
                break;

            case TUI_KEY_RIGHT:
            case TUI_KEY_DOWN:
                if ((unsigned int)page_index + 1 < README_PAGE_COUNT) {
                    ++page_index;
                    draw_page();
                }
                break;

            case TUI_KEY_HOME:
                page_index = 0;
                draw_page();
                break;

            case TUI_KEY_RUNSTOP:
                running = 0;
                break;
        }
    }

    __asm__("jmp $FCE2");
}


int main(void) {
    unsigned char bank;
    unsigned int payload_len = 0;

    tui_init();

    page_index = 0;
    resume_ready = 0;

    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
        if (resume_try_load(&page_index, sizeof(page_index), &payload_len) &&
            payload_len == sizeof(page_index) &&
            page_index < README_PAGE_COUNT) {
            /* restored */
        } else {
            page_index = 0;
        }
    }

    running = 1;

    readme_loop();
    return 0;
}
