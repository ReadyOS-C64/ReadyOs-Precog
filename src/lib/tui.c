/*
 * tui.c - TUI Library Implementation for Ready OS
 * Text User Interface with PETSCII box drawing
 *
 * For Commodore 64, compiled with CC65
 */

#include "tui.h"
#include <c64.h>
#include <conio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Static variables (faster than stack on 6502)
 *---------------------------------------------------------------------------*/
static unsigned int screen_offset;
static unsigned char i, j;

/*---------------------------------------------------------------------------
 * Core Functions
 *---------------------------------------------------------------------------*/

void tui_init(void) {
    /* Set default colors */
    VIC.bordercolor = TUI_THEME_BG;
    VIC.bgcolor0 = TUI_THEME_BG;
    textcolor(TUI_THEME_FG);
    clrscr();
}

void tui_clear(unsigned char bg_color) {
    VIC.bgcolor0 = bg_color;

    /* Clear screen memory */
    memset(TUI_SCREEN, 32, 1000);  /* Space character */

    /* Set all color RAM to default foreground */
    memset(TUI_COLOR_RAM, TUI_THEME_FG, 1000);
}

void tui_gotoxy(unsigned char x, unsigned char y) {
    gotoxy(x, y);
}

/*---------------------------------------------------------------------------
 * Drawing Functions
 *---------------------------------------------------------------------------*/

void tui_putc(unsigned char x, unsigned char y, unsigned char ch, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;
    TUI_SCREEN[screen_offset] = ch;
    TUI_COLOR_RAM[screen_offset] = color;
}

void tui_puts(unsigned char x, unsigned char y, const char *str, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;

    for (i = 0; str[i] != 0 && (x + i) < TUI_SCREEN_WIDTH; ++i) {
        TUI_SCREEN[screen_offset + i] = tui_ascii_to_screen(str[i]);
        TUI_COLOR_RAM[screen_offset + i] = color;
    }
}

void tui_puts_n(unsigned char x, unsigned char y, const char *str,
                unsigned char maxlen, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;

    for (i = 0; str[i] != 0 && i < maxlen && (x + i) < TUI_SCREEN_WIDTH; ++i) {
        TUI_SCREEN[screen_offset + i] = tui_ascii_to_screen(str[i]);
        TUI_COLOR_RAM[screen_offset + i] = color;
    }

    /* Pad with spaces if string is shorter than maxlen */
    for (; i < maxlen && (x + i) < TUI_SCREEN_WIDTH; ++i) {
        TUI_SCREEN[screen_offset + i] = 32;  /* Space */
        TUI_COLOR_RAM[screen_offset + i] = color;
    }
}

void tui_hline(unsigned char x, unsigned char y, unsigned char len, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;

    for (i = 0; i < len && (x + i) < TUI_SCREEN_WIDTH; ++i) {
        TUI_SCREEN[screen_offset + i] = TUI_HLINE;
        TUI_COLOR_RAM[screen_offset + i] = color;
    }
}

void tui_vline(unsigned char x, unsigned char y, unsigned char len, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;

    for (i = 0; i < len && (y + i) < TUI_SCREEN_HEIGHT; ++i) {
        TUI_SCREEN[screen_offset] = TUI_VLINE;
        TUI_COLOR_RAM[screen_offset] = color;
        screen_offset += 40;
    }
}

void tui_fill_rect(const TuiRect *rect, unsigned char ch, unsigned char color) {
    for (j = 0; j < rect->h && (rect->y + j) < TUI_SCREEN_HEIGHT; ++j) {
        screen_offset = (unsigned int)(rect->y + j) * 40 + rect->x;
        for (i = 0; i < rect->w && (rect->x + i) < TUI_SCREEN_WIDTH; ++i) {
            TUI_SCREEN[screen_offset + i] = ch;
            TUI_COLOR_RAM[screen_offset + i] = color;
        }
    }
}

void tui_clear_rect(const TuiRect *rect, unsigned char color) {
    tui_fill_rect(rect, 32, color);  /* Space character */
}

void tui_clear_line(unsigned char y, unsigned char x, unsigned char len, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;
    for (i = 0; i < len && (x + i) < TUI_SCREEN_WIDTH; ++i) {
        TUI_SCREEN[screen_offset + i] = 32;
        TUI_COLOR_RAM[screen_offset + i] = color;
    }
}

/*---------------------------------------------------------------------------
 * Window/Panel Functions
 *---------------------------------------------------------------------------*/

void tui_window(const TuiRect *rect, unsigned char border_color) {
    unsigned char x2, y2;

    x2 = rect->x + rect->w - 1;
    y2 = rect->y + rect->h - 1;

    /* Draw corners */
    tui_putc(rect->x, rect->y, TUI_CORNER_TL, border_color);
    tui_putc(x2, rect->y, TUI_CORNER_TR, border_color);
    tui_putc(rect->x, y2, TUI_CORNER_BL, border_color);
    tui_putc(x2, y2, TUI_CORNER_BR, border_color);

    /* Draw top and bottom lines */
    tui_hline(rect->x + 1, rect->y, rect->w - 2, border_color);
    tui_hline(rect->x + 1, y2, rect->w - 2, border_color);

    /* Draw left and right lines */
    tui_vline(rect->x, rect->y + 1, rect->h - 2, border_color);
    tui_vline(x2, rect->y + 1, rect->h - 2, border_color);

    /* Clear interior */
    if (rect->w > 2 && rect->h > 2) {
        TuiRect interior;
        interior.x = rect->x + 1;
        interior.y = rect->y + 1;
        interior.w = rect->w - 2;
        interior.h = rect->h - 2;
        tui_clear_rect(&interior, TUI_THEME_FG);
    }
}

void tui_window_title(const TuiRect *rect, const char *title,
                      unsigned char border_color, unsigned char title_color) {
    unsigned char title_len;
    unsigned char title_x;

    /* Draw the basic window */
    tui_window(rect, border_color);

    /* Calculate centered title position */
    title_len = strlen(title);
    if (title_len > rect->w - 4) {
        title_len = rect->w - 4;
    }
    title_x = rect->x + (rect->w - title_len) / 2;

    /* Draw title */
    tui_puts_n(title_x, rect->y, title, title_len, title_color);
}

void tui_panel(const TuiRect *rect, const char *title, unsigned char bg_color) {
    /* Clear the panel area */
    tui_clear_rect(rect, bg_color);

    /* Draw title if provided */
    if (title != 0 && title[0] != 0) {
        tui_puts(rect->x, rect->y, title, TUI_THEME_TITLE);
    }
}

/*---------------------------------------------------------------------------
 * Status Bar
 *---------------------------------------------------------------------------*/

void tui_status_bar(const char *text, unsigned char color) {
    TuiRect bar;
    bar.x = 0;
    bar.y = 24;
    bar.w = 40;
    bar.h = 1;

    tui_clear_rect(&bar, color);
    tui_puts(0, 24, text, color);
}

void tui_status_bar_multi(const char **items, unsigned char count, unsigned char color) {
    unsigned char x;
    unsigned char spacing;

    /* Clear status bar */
    TuiRect bar;
    bar.x = 0;
    bar.y = 24;
    bar.w = 40;
    bar.h = 1;
    tui_clear_rect(&bar, color);

    /* Distribute items across the bar */
    if (count == 0) return;

    spacing = 40 / count;
    x = 0;

    for (i = 0; i < count; ++i) {
        tui_puts(x, 24, items[i], color);
        x += spacing;
    }
}

/*---------------------------------------------------------------------------
 * Menu/List Functions
 *---------------------------------------------------------------------------*/

void tui_menu_init(TuiMenu *menu, unsigned char x, unsigned char y,
                   unsigned char w, unsigned char h,
                   const char **items, unsigned char count) {
    menu->x = x;
    menu->y = y;
    menu->w = w;
    menu->h = h;
    menu->items = items;
    menu->count = count;
    menu->selected = 0;
    menu->scroll_offset = 0;
    menu->item_color = TUI_THEME_FG;
    menu->sel_color = TUI_THEME_HIGHLIGHT;
}

void tui_menu_draw(TuiMenu *menu) {
    unsigned char row;       /* Local loop var to avoid collision with i */
    unsigned char item_idx;
    unsigned char col;

    /* Draw each visible item */
    for (row = 0; row < menu->h; ++row) {
        item_idx = menu->scroll_offset + row;
        screen_offset = (unsigned int)(menu->y + row) * 40 + menu->x;

        if (item_idx < menu->count) {
            unsigned char color;
            unsigned char prefix;

            /* Determine color and prefix based on selection */
            if (item_idx == menu->selected) {
                color = menu->sel_color;
                prefix = 0x3E;  /* '>' in screen code */
            } else {
                color = menu->item_color;
                prefix = 32;    /* Space */
            }

            /* Draw prefix and space */
            TUI_SCREEN[screen_offset] = prefix;
            TUI_COLOR_RAM[screen_offset] = color;
            TUI_SCREEN[screen_offset + 1] = 32;  /* Space after prefix */
            TUI_COLOR_RAM[screen_offset + 1] = color;

            /* Draw item text - use direct write to avoid i collision */
            {
                const char *str = menu->items[item_idx];
                unsigned char pos;
                unsigned char maxlen = menu->w - 2;
                unsigned int text_offset = screen_offset + 2;

                for (pos = 0; str[pos] != 0 && pos < maxlen; ++pos) {
                    TUI_SCREEN[text_offset + pos] = tui_ascii_to_screen(str[pos]);
                    TUI_COLOR_RAM[text_offset + pos] = color;
                }
                /* Pad with spaces */
                for (; pos < maxlen; ++pos) {
                    TUI_SCREEN[text_offset + pos] = 32;
                    TUI_COLOR_RAM[text_offset + pos] = color;
                }
            }
        } else {
            /* Clear empty line */
            for (col = 0; col < menu->w; ++col) {
                TUI_SCREEN[screen_offset + col] = 32;
                TUI_COLOR_RAM[screen_offset + col] = menu->item_color;
            }
        }
    }
}

unsigned char tui_menu_input(TuiMenu *menu, unsigned char key) {
    switch (key) {
        case TUI_KEY_UP:
            if (menu->selected > 0) {
                --menu->selected;
                /* Scroll up if needed */
                if (menu->selected < menu->scroll_offset) {
                    menu->scroll_offset = menu->selected;
                }
            }
            break;

        case TUI_KEY_DOWN:
            if (menu->selected < menu->count - 1) {
                ++menu->selected;
                /* Scroll down if needed */
                if (menu->selected >= menu->scroll_offset + menu->h) {
                    menu->scroll_offset = menu->selected - menu->h + 1;
                }
            }
            break;

        case TUI_KEY_RETURN:
            return menu->selected;

        case TUI_KEY_HOME:
            menu->selected = 0;
            menu->scroll_offset = 0;
            break;
    }

    return 255;  /* No selection made */
}

unsigned char tui_menu_selected(TuiMenu *menu) {
    return menu->selected;
}

/*---------------------------------------------------------------------------
 * Text Input Functions
 *---------------------------------------------------------------------------*/

void tui_input_init(TuiInput *input, unsigned char x, unsigned char y,
                    unsigned char width, unsigned char maxlen,
                    char *buffer, unsigned char color) {
    input->x = x;
    input->y = y;
    input->width = width;
    input->maxlen = maxlen;
    input->cursor = 0;
    input->buffer = buffer;
    input->color = color;

    /* Clear buffer */
    buffer[0] = 0;
}

void tui_input_draw(TuiInput *input) {
    unsigned char len;
    unsigned char display_start;
    unsigned char display_len;

    len = strlen(input->buffer);
    screen_offset = (unsigned int)input->y * 40 + input->x;

    /* Calculate visible portion */
    if (input->cursor >= input->width) {
        display_start = input->cursor - input->width + 1;
    } else {
        display_start = 0;
    }

    display_len = len - display_start;
    if (display_len > input->width) {
        display_len = input->width;
    }

    /* Draw text */
    for (i = 0; i < input->width; ++i) {
        unsigned char ch;
        if (i < display_len) {
            ch = tui_ascii_to_screen(input->buffer[display_start + i]);
        } else {
            ch = 32;  /* Space */
        }

        /* Show cursor as reverse video */
        if (display_start + i == input->cursor) {
            ch = 0xA0;  /* Reverse space or could use actual cursor char */
        }

        TUI_SCREEN[screen_offset + i] = ch;
        TUI_COLOR_RAM[screen_offset + i] = input->color;
    }
}

unsigned char tui_input_key(TuiInput *input, unsigned char key) {
    unsigned char len;

    len = strlen(input->buffer);

    if (key == TUI_KEY_RETURN) {
        return 1;  /* Enter pressed */
    }

    if (key == TUI_KEY_DEL) {
        /* Delete character before cursor */
        if (input->cursor > 0) {
            --input->cursor;
            /* Shift remaining characters left */
            for (i = input->cursor; i < len; ++i) {
                input->buffer[i] = input->buffer[i + 1];
            }
        }
    } else if (key == TUI_KEY_LEFT) {
        if (input->cursor > 0) {
            --input->cursor;
        }
    } else if (key == TUI_KEY_RIGHT) {
        if (input->cursor < len) {
            ++input->cursor;
        }
    } else if (key == TUI_KEY_HOME) {
        input->cursor = 0;
    } else if (key >= 32 && key < 128) {
        /* Printable character */
        if (len < input->maxlen) {
            /* Insert character at cursor position */
            for (i = len + 1; i > input->cursor; --i) {
                input->buffer[i] = input->buffer[i - 1];
            }
            input->buffer[input->cursor] = key;
            ++input->cursor;
        }
    }

    return 0;
}

void tui_input_clear(TuiInput *input) {
    input->buffer[0] = 0;
    input->cursor = 0;
}

/*---------------------------------------------------------------------------
 * Text Area Functions
 *---------------------------------------------------------------------------*/

void tui_textarea_init(TuiTextArea *area, unsigned char x, unsigned char y,
                       unsigned char w, unsigned char h,
                       const char **lines, unsigned int line_count) {
    area->x = x;
    area->y = y;
    area->w = w;
    area->h = h;
    area->lines = lines;
    area->line_count = line_count;
    area->scroll_pos = 0;
    area->color = TUI_THEME_FG;
}

void tui_textarea_draw(TuiTextArea *area) {
    unsigned int line_idx;

    for (i = 0; i < area->h; ++i) {
        line_idx = area->scroll_pos + i;

        if (line_idx < area->line_count) {
            tui_puts_n(area->x, area->y + i, area->lines[line_idx],
                       area->w, area->color);
        } else {
            /* Clear empty line */
            screen_offset = (unsigned int)(area->y + i) * 40 + area->x;
            for (j = 0; j < area->w; ++j) {
                TUI_SCREEN[screen_offset + j] = 32;
                TUI_COLOR_RAM[screen_offset + j] = area->color;
            }
        }
    }
}

void tui_textarea_scroll(TuiTextArea *area, int delta) {
    int new_pos;

    new_pos = (int)area->scroll_pos + delta;

    if (new_pos < 0) {
        new_pos = 0;
    }

    if ((unsigned int)new_pos > area->line_count - area->h) {
        if (area->line_count > area->h) {
            new_pos = area->line_count - area->h;
        } else {
            new_pos = 0;
        }
    }

    area->scroll_pos = (unsigned int)new_pos;
}

/*---------------------------------------------------------------------------
 * Input Functions
 *---------------------------------------------------------------------------*/

unsigned char tui_getkey(void) {
    return cgetc();
}

unsigned char tui_kbhit(void) {
    return kbhit();
}

/* SHFLAG at $028D holds modifier key state */
#define SHFLAG (*(unsigned char*)0x028D)

unsigned char tui_get_modifiers(void) {
    return SHFLAG;
}

unsigned char tui_is_back_pressed(void) {
    /* Check if CTRL key is held */
    /* This should be called after getting a key with tui_getkey */
    return (SHFLAG & TUI_MOD_CTRL) ? 1 : 0;
}

void tui_return_to_launcher(void) {
    /* Call shim's return_to_launcher routine via jump table at $C80C */
    __asm__("jmp $C80C");
}

/* Shim data addresses for app switching (shim at $C800) */
#define SHIM_TARGET_BANK ((unsigned char*)0xC820)
#define SHIM_REU_BITMAP_LO ((unsigned char*)0xC836)
#define SHIM_REU_BITMAP_HI ((unsigned char*)0xC837)
#define SHIM_REU_BITMAP_XHI ((unsigned char*)0xC838)

/* App bank definitions (must match launcher.c) */
#define APP_BANK_LAUNCHER 0
#define APP_BANK_EDITOR   1
#define APP_BANK_HEXCALC  2
#define APP_BANK_MAX      23  /* Highest valid app bank */

static unsigned char tui_bank_loaded(unsigned char bank) {
    if (bank < 8) {
        return (unsigned char)((*SHIM_REU_BITMAP_LO & (unsigned char)(1U << bank)) != 0);
    }
    if (bank < 16) {
        return (unsigned char)((*SHIM_REU_BITMAP_HI & (unsigned char)(1U << (bank - 8))) != 0);
    }
    if (bank < 24) {
        return (unsigned char)((*SHIM_REU_BITMAP_XHI & (unsigned char)(1U << (bank - 16))) != 0);
    }
    return 0;
}

void tui_switch_to_app(unsigned char bank) {
    /* Set target bank for switch */
    *SHIM_TARGET_BANK = bank;

    /* Call shim's switch_to_app routine via jump table at $C80F */
    __asm__("jmp $C80F");
}

unsigned char tui_get_next_app(unsigned char current_bank) {
    unsigned char next = current_bank;
    unsigned char tries = 0;

    if (current_bank < 1 || current_bank > APP_BANK_MAX) {
        current_bank = APP_BANK_EDITOR;
        next = current_bank;
    }

    while (tries < APP_BANK_MAX) {
        ++next;
        if (next > APP_BANK_MAX) {
            next = APP_BANK_EDITOR;
        }
        if (next != current_bank && tui_bank_loaded(next)) {
            return next;
        }
        ++tries;
    }

    return 0;
}

unsigned char tui_get_prev_app(unsigned char current_bank) {
    unsigned char prev = current_bank;
    unsigned char tries = 0;

    if (current_bank < 1 || current_bank > APP_BANK_MAX) {
        current_bank = APP_BANK_EDITOR;
        prev = current_bank;
    }

    while (tries < APP_BANK_MAX) {
        if (prev <= APP_BANK_EDITOR) {
            prev = APP_BANK_MAX;
        } else {
            --prev;
        }
        if (prev != current_bank && tui_bank_loaded(prev)) {
            return prev;
        }
        ++tries;
    }

    return 0;
}

/*---------------------------------------------------------------------------
 * Progress Bar
 *---------------------------------------------------------------------------*/

void tui_progress_bar(unsigned char x, unsigned char y, unsigned char width,
                      unsigned char filled, unsigned char total,
                      unsigned char fill_color, unsigned char empty_color) {
    unsigned char filled_width;
    unsigned char pos;

    if (total == 0) {
        filled_width = 0;
    } else {
        filled_width = (unsigned char)((unsigned int)filled * width / total);
    }

    screen_offset = (unsigned int)y * 40 + x;

    for (pos = 0; pos < width && (x + pos) < TUI_SCREEN_WIDTH; ++pos) {
        if (pos < filled_width) {
            TUI_SCREEN[screen_offset + pos] = 0xA0;  /* Solid block */
            TUI_COLOR_RAM[screen_offset + pos] = fill_color;
        } else {
            TUI_SCREEN[screen_offset + pos] = 0x66;  /* Checker pattern */
            TUI_COLOR_RAM[screen_offset + pos] = empty_color;
        }
    }
}

/*---------------------------------------------------------------------------
 * Utility Functions
 *---------------------------------------------------------------------------*/

unsigned char tui_ascii_to_screen(unsigned char ascii) {
    if (ascii >= 'A' && ascii <= 'Z') {
        return ascii - 'A' + 1;    /* A-Z -> 1-26 */
    }
    if (ascii >= 'a' && ascii <= 'z') {
        return ascii - 'a' + 1;    /* a-z -> 1-26 (same as uppercase) */
    }
    if (ascii >= '0' && ascii <= '9') {
        return ascii - '0' + 48;   /* 0-9 -> 48-57 */
    }

    /* Common punctuation */
    switch (ascii) {
        case ' ':  return 32;
        case '!':  return 33;
        case '"':  return 34;
        case '#':  return 35;
        case '$':  return 36;
        case '%':  return 37;
        case '&':  return 38;
        case '\'': return 39;
        case '(':  return 40;
        case ')':  return 41;
        case '*':  return 42;
        case '+':  return 43;
        case ',':  return 44;
        case '-':  return 45;
        case '.':  return 46;
        case '/':  return 47;
        case ':':  return 58;
        case ';':  return 59;
        case '<':  return 60;
        case '=':  return 61;
        case '>':  return 62;
        case '?':  return 63;
        case '@':  return 0;
        case '[':  return 27;
        case ']':  return 29;
        case '_':  return 100;
        default:   return 32;  /* Default to space */
    }
}

void tui_str_to_screen(char *str) {
    while (*str) {
        *str = tui_ascii_to_screen(*str);
        ++str;
    }
}

void tui_print_uint(unsigned char x, unsigned char y, unsigned int value,
                    unsigned char color) {
    static char buf[6];
    unsigned char pos;

    /* Convert to string (right to left) */
    pos = 5;
    buf[5] = 0;

    do {
        --pos;
        buf[pos] = '0' + (value % 10);
        value /= 10;
    } while (value > 0 && pos > 0);

    /* Print */
    tui_puts(x, y, &buf[pos], color);
}

void tui_print_hex8(unsigned char x, unsigned char y, unsigned char value,
                    unsigned char color) {
    static const char hex[] = "0123456789ABCDEF";

    tui_putc(x, y, tui_ascii_to_screen('$'), color);
    tui_putc(x + 1, y, tui_ascii_to_screen(hex[(value >> 4) & 0x0F]), color);
    tui_putc(x + 2, y, tui_ascii_to_screen(hex[value & 0x0F]), color);
}

void tui_print_hex16(unsigned char x, unsigned char y, unsigned int value,
                     unsigned char color) {
    static const char hex[] = "0123456789ABCDEF";

    tui_putc(x, y, tui_ascii_to_screen('$'), color);
    tui_putc(x + 1, y, tui_ascii_to_screen(hex[(value >> 12) & 0x0F]), color);
    tui_putc(x + 2, y, tui_ascii_to_screen(hex[(value >> 8) & 0x0F]), color);
    tui_putc(x + 3, y, tui_ascii_to_screen(hex[(value >> 4) & 0x0F]), color);
    tui_putc(x + 4, y, tui_ascii_to_screen(hex[value & 0x0F]), color);
}
