/*
 * readyshellpoc.c - ReadyShell POC for ReadyOS
 *
 * For Commodore 64, compiled with CC65
 */

#include "core/rs_config.h"
#include "core/rs_errors.h"
#include "core/rs_token.h"
#include "core/rs_ui_state.h"
#include "core/rs_vm.h"
#include "platform/rs_overlay.h"
#include "platform/rs_platform.h"
#include "../../lib/resume_state.h"
#include "../../lib/tui.h"
#include <cbm.h>
#include <c64.h>
#include <conio.h>
#include <_heap.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 40
#define SCREEN_HEIGHT 25
#define SCREEN_MEM ((unsigned char*)0x0400)
#define COLOR_MEM ((unsigned char*)0xD800)

#define TITLE_Y 0
#define BODY_TOP 3
#define BODY_BOTTOM 23
#define HELP_Y 24

#define PROMPT_TEXT ">"
#define PROMPT_LEN 1
#define LOGICAL_MAX 160
#define INPUT_COLS (SCREEN_WIDTH - PROMPT_LEN)
#define PHYSICAL_MAX INPUT_COLS
#define FOOTER_NORMAL_TEXT "RET RUN F2/F4 APPS CTRL+B"
#define FOOTER_PAUSED_TEXT "PAUSED - PRESS ANY KEY"

/* Key codes */
#define KEY_RETURN 13
#define KEY_LEFT 157
#define KEY_RIGHT 29
#define KEY_DEL 20
#define KEY_NEXT_APP TUI_KEY_NEXT_APP
#define KEY_PREV_APP TUI_KEY_PREV_APP
#define KEY_RUNSTOP TUI_KEY_RUNSTOP

/* Shim ABI */
#define SHIM_CURRENT_BANK (*(unsigned char*)0xC834)

/* KERNAL keyboard state */
#define RS_KBD_BUFFER_COUNT (*(volatile unsigned char*)0x00C6)
#define RS_KBD_CURRENT_KEY (*(volatile unsigned char*)0x00CB)
#define RS_KBD_MODIFIERS (*(volatile unsigned char*)0x028D)
#define RS_KBD_NO_KEY 0x40u
#define RS_KBD_SPACE_KEY 0x3Cu

/* PETSCII box chars (screen codes) */
#define BOX_TL 0x70
#define BOX_TR 0x6E
#define BOX_BL 0x6D
#define BOX_BR 0x7D
#define BOX_H 0x40
#define BOX_V 0x5D

/* C64 colors */
#define C_BLACK 0
#define C_WHITE 1
#define C_LIGHTRED 10
#define C_LIGHTBLUE 14
#define C_CYAN 3
#define C_YELLOW 7
#define C_GRAY3 15
#define C_BLUE 6

/* Keep runtime VM state in fixed RAM outside the overlay execution window. */
typedef struct {
    RSVM vm;
    RSVMPlatform platform;
    RSError err;
    unsigned char resume_ready;
    unsigned char cursor_y;
    unsigned char cursor_x;
    unsigned int off;
    unsigned char i;
} ReadyShellRuntimeState;

#define RS_RUNTIME_ADDR 0xCA00u
#define RS_RUNTIME_LIMIT_ADDR 0xD000u
#define RS_REU_HEAP_SESSION_FLAG (*(unsigned char*)0xCFF0)

#define RS_RUNTIME ((ReadyShellRuntimeState*)RS_RUNTIME_ADDR)
#define g_vm (RS_RUNTIME->vm)
#define g_platform (RS_RUNTIME->platform)
#define g_err (RS_RUNTIME->err)
#define resume_ready (RS_RUNTIME->resume_ready)
#define g_cursor_y (RS_RUNTIME->cursor_y)
#define g_cursor_x (RS_RUNTIME->cursor_x)
#define g_off (RS_RUNTIME->off)
#define g_i (RS_RUNTIME->i)

static char g_line[LOGICAL_MAX];
typedef struct {
    char last_line[LOGICAL_MAX];
} ReadyShellResumeV1;
static ReadyShellResumeV1 resume_blob;

static void clear_line(unsigned char y, unsigned char color);
static void draw_text(unsigned char x, unsigned char y, const char *s, unsigned char color);
static void resume_save_state(void);
static void shell_overlay_progress(unsigned char stage, void *user);
static void shell_draw_footer_normal(void);
void rs_set_c_stack_top(void);

extern unsigned char rs_heap_bss_run[];
extern unsigned char rs_heap_bss_size[];
extern unsigned char rs_heap_overlay_loadaddr[];

static void shell_init_runtime_regions(void) {
    unsigned int heap_start;
    unsigned int heap_end;
    unsigned int heap_size;

    /* High RAM at $CA00 is scratch and is not part of the app REU snapshot. */
    memset(RS_RUNTIME, 0, sizeof(*RS_RUNTIME));
    RS_REU_HEAP_SESSION_FLAG = 0u;
    rs_set_c_stack_top();
    heap_start = (unsigned int)rs_heap_bss_run + (unsigned int)rs_heap_bss_size;
    if (heap_start & 1u) {
        ++heap_start;
    }
    heap_end = (unsigned int)rs_heap_overlay_loadaddr;
    heap_size = (heap_end > heap_start) ? (heap_end - heap_start) : 0u;
    _heaporg = (unsigned*)heap_start;
    _heapptr = (unsigned*)heap_start;
    _heapend = (unsigned*)heap_end;
    _heapfirst = 0;
    _heaplast = 0;
    if (heap_size != 0u) {
        _heapadd((void*)heap_start, heap_size);
    }
}

static unsigned char ascii_to_screen(unsigned char ascii) {
    if (ascii >= 'A' && ascii <= 'Z') return (unsigned char)(ascii - 'A' + 1);
    if (ascii >= 'a' && ascii <= 'z') return (unsigned char)(ascii - 'a' + 1);
    if (ascii >= '0' && ascii <= '9') return (unsigned char)(ascii - '0' + 48);

    switch (ascii) {
        case ' ': return 32;
        case '!': return 33;
        case '"': return 34;
        case '#': return 35;
        case '$': return 36;
        case '%': return 37;
        case '&': return 38;
        case '\'': return 39;
        case '(': return 40;
        case ')': return 41;
        case '*': return 42;
        case '+': return 43;
        case ',': return 44;
        case '-': return 45;
        case '.': return 46;
        case '/': return 47;
        case ':': return 58;
        case ';': return 59;
        case '<': return 60;
        case '=': return 61;
        case '>': return 62;
        case '?': return 63;
        case '@': return 0;
        case '[': return 27;
        case ']': return 29;
        /* Use the known-visible vertical line glyph in ReadyOS screen mode. */
        case '|': return BOX_V;
        case '_': return 100;
        default: return 32;
    }
}

static unsigned char shell_normalize_input_key(unsigned char key) {
    /*
     * Normalize C64/PETSCII variants that users expect to behave as pipeline
     * input. Keep this as a switch table to avoid optimizer surprises on
     * chained comparisons with high-bit PETSCII values.
     */
    switch (key) {
        case '!':
        case '^':
        case CH_LIRA:
        case CH_PI:
        case CH_VLINE:
        case CH_CURS_UP:
        case 0x01: /* seen on some host-keyboard layouts for Shift+1 */
        case 0x81:
        case 0xA1:
        case 0xC1:
        case 0xD1:
            return '|';
        default:
            break;
    }
    return key;
}

/*
 * Reference-style input acceptance:
 * - Decide insertability from raw key first.
 * - Then apply substitutions (! -> | and C64 variants).
 *
 * This avoids rejecting normalized C64 pipe byte ($DD), which is outside
 * ASCII 32..126 but is still the correct target-char representation.
 */
static unsigned char shell_key_to_insert_char(unsigned char raw,
                                              unsigned char *out_char,
                                              unsigned char *out_norm) {
    unsigned char accepted;
    unsigned char normalized;

    accepted = 0;
    if (raw >= 32 && raw <= 126) {
        accepted = 1;
    } else {
        switch (raw) {
            case CH_CURS_UP:
            case CH_PI:
            case CH_VLINE:
            case 0x81:
            case 0xA1:
            case 0xC1:
            case 0xD1:
                accepted = 1;
                break;
            default:
                break;
        }
    }

    normalized = shell_normalize_input_key(raw);
    if (out_norm) *out_norm = normalized;
    if (!accepted) return 0;

    if (out_char) *out_char = normalized;
    return 1;
}

static void put_char(unsigned char x, unsigned char y, unsigned char ch, unsigned char color) {
    g_off = (unsigned int)y * SCREEN_WIDTH + x;
    SCREEN_MEM[g_off] = ch;
    COLOR_MEM[g_off] = color;
}

static void put_ascii(unsigned char x, unsigned char y, unsigned char ch, unsigned char color) {
    put_char(x, y, ascii_to_screen(ch), color);
}

static void clear_line(unsigned char y, unsigned char color) {
    g_off = (unsigned int)y * SCREEN_WIDTH;
    for (g_i = 0; g_i < SCREEN_WIDTH; ++g_i) {
        SCREEN_MEM[g_off + g_i] = 32;
        COLOR_MEM[g_off + g_i] = color;
    }
}

static void shell_draw_footer_text(const char *text, unsigned char color) {
    clear_line(HELP_Y, color);
    draw_text(0, HELP_Y, text, color);
}

static unsigned char shell_pause_flags(void) {
    unsigned char flags;
    flags = 0u;
    (void)rs_reu_read(RS_REU_UI_FLAGS_OFF, &flags, 1u);
    return flags;
}

static void shell_pause_set_flags(unsigned char flags) {
    (void)rs_reu_write(RS_REU_UI_FLAGS_OFF, &flags, 1u);
}

static void shell_draw_footer_normal(void) {
    shell_draw_footer_text(FOOTER_NORMAL_TEXT, C_GRAY3);
}

static void shell_draw_footer_paused(void) {
    shell_draw_footer_text(FOOTER_PAUSED_TEXT, C_YELLOW);
}

static void shell_pause_clear_buffer(void) {
    RS_KBD_BUFFER_COUNT = 0u;
}

static unsigned char shell_pause_key_down(void) {
    cbm_k_scnkey();
    return (unsigned char)(
        RS_KBD_CURRENT_KEY != RS_KBD_NO_KEY ||
        (RS_KBD_MODIFIERS & 0x07u) != 0u
    );
}

static void shell_pause_arm_poll(void) {
    unsigned char flags;
    cbm_k_scnkey();
    if (RS_KBD_CURRENT_KEY != RS_KBD_SPACE_KEY) {
        return;
    }

    shell_pause_clear_buffer();
    flags = (unsigned char)(shell_pause_flags() | RS_UI_FLAG_PAUSED);
    shell_pause_set_flags(flags);
}

static void shell_wait_if_paused(void) {
    unsigned char flags;

    flags = shell_pause_flags();
    if ((flags & RS_UI_FLAG_PAUSED) == 0u) {
        return;
    }

    /* Make the keyboard the active input stream before we block here. */
    cbm_k_clrch();
    shell_draw_footer_paused();
    shell_pause_clear_buffer();

    while (shell_pause_key_down()) {
        waitvsync();
    }
    shell_pause_clear_buffer();

    while (!shell_pause_key_down()) {
        waitvsync();
    }
    while (shell_pause_key_down()) {
        waitvsync();
    }

    flags = (unsigned char)(shell_pause_flags() & (unsigned char)~RS_UI_FLAG_PAUSED);
    shell_pause_set_flags(flags);
    cbm_k_clrch();
    shell_pause_clear_buffer();
    shell_draw_footer_normal();
}

static void draw_text(unsigned char x, unsigned char y, const char *s, unsigned char color) {
    unsigned char cx;

    cx = x;
    while (s != 0 && *s != 0 && cx < SCREEN_WIDTH) {
        put_ascii(cx, y, (unsigned char)*s, color);
        ++s;
        ++cx;
    }
}

static void draw_window(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char color) {
    unsigned char x2;
    unsigned char y2;
    unsigned char i;
    unsigned char j;

    x2 = (unsigned char)(x + w - 1);
    y2 = (unsigned char)(y + h - 1);

    put_char(x, y, BOX_TL, color);
    put_char(x2, y, BOX_TR, color);
    put_char(x, y2, BOX_BL, color);
    put_char(x2, y2, BOX_BR, color);

    for (i = (unsigned char)(x + 1); i < x2; ++i) {
        put_char(i, y, BOX_H, color);
        put_char(i, y2, BOX_H, color);
    }
    for (j = (unsigned char)(y + 1); j < y2; ++j) {
        put_char(x, j, BOX_V, color);
        put_char(x2, j, BOX_V, color);
    }
}

static void clear_screen_color(unsigned char bg, unsigned char fg) {
    unsigned int i;

    VIC.bordercolor = bg;
    VIC.bgcolor0 = bg;
    textcolor(fg);
    clrscr();

    for (i = 0; i < 1000; ++i) {
        SCREEN_MEM[i] = 32;
        COLOR_MEM[i] = fg;
    }
}

static void scroll_body_up(void) {
    unsigned char row;
    unsigned int src;
    unsigned int dst;

    for (row = (unsigned char)(BODY_TOP + 1); row <= BODY_BOTTOM; ++row) {
        src = (unsigned int)row * SCREEN_WIDTH;
        dst = (unsigned int)(row - 1) * SCREEN_WIDTH;
        memcpy(SCREEN_MEM + dst, SCREEN_MEM + src, SCREEN_WIDTH);
        memcpy(COLOR_MEM + dst, COLOR_MEM + src, SCREEN_WIDTH);
    }

    clear_line(BODY_BOTTOM, C_WHITE);
}

static void shell_newline(void) {
    if (g_cursor_y >= BODY_BOTTOM) {
        scroll_body_up();
    } else {
        ++g_cursor_y;
    }
    g_cursor_x = 0;
}

static void shell_write_text_color(const char *s, unsigned char color) {
    unsigned char c;

    while (s != 0 && *s != 0) {
        c = (unsigned char)*s++;
        if (c == '\n') {
            shell_newline();
            continue;
        }
        put_ascii(g_cursor_x, g_cursor_y, c, color);
        ++g_cursor_x;
        if (g_cursor_x >= SCREEN_WIDTH) {
            shell_newline();
        }
    }
}

static void shell_write_line_color(const char *s, unsigned char color) {
    clear_line(g_cursor_y, color);
    g_cursor_x = 0;
    if (s != 0) {
        shell_write_text_color(s, color);
    }
    shell_newline();
}

static void shell_write_line(const char *s) {
    shell_write_line_color(s, C_WHITE);
}

static int shell_writer(void *user, const char *line) {
    unsigned char color;
    (void)user;
    color = (rs_vm_current_output_kind() == RS_VM_OUTPUT_PRT) ? C_CYAN : C_WHITE;
    shell_write_line_color(line, color);

    shell_pause_arm_poll();
    shell_wait_if_paused();
    return 0;
}

static void shell_trim(char *s) {
    unsigned int n;
    unsigned int i;
    unsigned int j;

    if (!s) return;

    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[n - 1] = 0;
        --n;
    }

    i = 0;
    while (s[i] == ' ' || s[i] == '\t') ++i;
    if (i == 0) return;

    j = 0;
    while (s[i] != 0) s[j++] = s[i++];
    s[j] = 0;
}

static void shell_print_u16(unsigned short n) {
    char rev[8];
    char out[8];
    unsigned char i;
    unsigned char j;

    if (n == 0) {
        shell_write_text_color("0", C_WHITE);
        return;
    }

    i = 0;
    while (n > 0 && i < sizeof(rev)) {
        rev[i++] = (char)('0' + (n % 10u));
        n = (unsigned short)(n / 10u);
    }

    j = 0;
    while (i > 0) out[j++] = rev[--i];
    out[j] = 0;
    shell_write_text_color(out, C_WHITE);
}

static void shell_overlay_progress(unsigned char stage, void *user) {
    (void)user;
    clear_line(g_cursor_y, C_WHITE);
    g_cursor_x = 0;
    shell_write_text_color("Loading", C_GRAY3);
    switch (stage) {
        case 1:
            shell_write_text_color(".", C_WHITE);
            break;
        case 2:
            shell_write_text_color("..", C_WHITE);
            break;
        case 3:
            shell_write_text_color("...", C_WHITE);
            break;
        case 4:
            shell_write_text_color("+", C_WHITE);
            break;
        case 5:
            shell_write_text_color("++", C_WHITE);
            break;
        default:
            shell_write_text_color(" done", C_LIGHTBLUE);
            break;
    }
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    memcpy(resume_blob.last_line, g_line, sizeof(g_line));
    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static void shell_nav_to_launcher(void) {
    resume_save_state();
    tui_return_to_launcher();
}

static void shell_nav_switch(unsigned char bank) {
    if (bank == 0) {
        return;
    }
    resume_save_state();
    tui_switch_to_app(bank);
}

static void shell_draw_chrome(void) {
    unsigned char row;

    clear_screen_color(C_BLUE, C_WHITE);
    draw_window(0, TITLE_Y, 40, 3, C_LIGHTBLUE);
    draw_text(11, 0, "READYOS READYSHELL", C_YELLOW);
    draw_text(8, 1, "readyshell v0.2 (beta)", C_CYAN);

    for (row = BODY_TOP; row <= BODY_BOTTOM; ++row) {
        clear_line(row, C_WHITE);
    }

    shell_draw_footer_normal();

    g_cursor_y = BODY_TOP;
    g_cursor_x = 0;
}

static void shell_draw_prompt(const char *buf, unsigned char len, unsigned char pos) {
    unsigned char col;
    unsigned char c;

    (void)pos;

    clear_line(g_cursor_y, C_WHITE);
    draw_text(0, g_cursor_y, PROMPT_TEXT, C_YELLOW);

    for (col = 0; col < INPUT_COLS; ++col) {
        if (col < len) c = (unsigned char)buf[col];
        else c = ' ';
        put_ascii((unsigned char)(PROMPT_LEN + col), g_cursor_y, c, C_WHITE);
    }
}

static void shell_redraw_input_tail(const char *buf,
                                    unsigned char len,
                                    unsigned char from,
                                    unsigned char erase_one) {
    unsigned char to;
    unsigned char col;
    unsigned char c;

    if (from >= INPUT_COLS) return;

    to = len;
    if (erase_one && to < INPUT_COLS) {
        ++to;
    }
    if (to > INPUT_COLS) {
        to = INPUT_COLS;
    }

    for (col = from; col < to; ++col) {
        if (col < len) c = (unsigned char)buf[col];
        else c = ' ';
        put_ascii((unsigned char)(PROMPT_LEN + col), g_cursor_y, c, C_WHITE);
    }
}

static void shell_draw_cursor_cell(const char *buf, unsigned char len, unsigned char pos, unsigned char on) {
    unsigned char c;
    unsigned char screen;

    if (pos >= INPUT_COLS) return;

    if (pos < len) c = (unsigned char)buf[pos];
    else c = ' ';

    screen = ascii_to_screen(c);
    if (on) screen |= 0x80;
    put_char((unsigned char)(PROMPT_LEN + pos), g_cursor_y, screen, C_WHITE);
}

static int shell_read_physical_line(char *out, unsigned char maxlen) {
    unsigned char len;
    unsigned char pos;
    unsigned char key;
    unsigned char raw_key;
    unsigned char i;
    unsigned char bank;
    unsigned char insert_mode;
    unsigned char insert_ch;
    unsigned char accepted;

    len = 0;
    pos = 0;
    insert_mode = 1;
    out[0] = 0;
    (void)kbrepeat(KBREPEAT_NONE);
    /* Ensure keyboard is the active input stream before we block in cgetc(). */
    cbm_k_clrch();
    shell_draw_prompt(out, len, pos);
    shell_draw_cursor_cell(out, len, pos, 1);

    for (;;) {
        key = (unsigned char)cgetc();
        if (key == 0) continue;
        raw_key = key;
        shell_draw_cursor_cell(out, len, pos, 0);

        if (key == 2) {
            shell_nav_to_launcher();
        }
        if (key == KEY_NEXT_APP) {
            bank = tui_get_next_app(SHIM_CURRENT_BANK);
            shell_nav_switch(bank);
        }
        if (key == KEY_PREV_APP) {
            bank = tui_get_prev_app(SHIM_CURRENT_BANK);
            shell_nav_switch(bank);
        }
        if (key == KEY_RUNSTOP) {
            shell_nav_to_launcher();
        }

        if (key == KEY_RETURN) {
            out[len] = 0;
            shell_newline();
            return len;
        }

        if (key == KEY_LEFT || key == CH_CURS_LEFT) {
            if (pos > 0) {
                --pos;
            }
            shell_draw_cursor_cell(out, len, pos, 1);
            continue;
        }
        if (key == KEY_RIGHT || key == CH_CURS_RIGHT) {
            if (pos < len) {
                ++pos;
            }
            shell_draw_cursor_cell(out, len, pos, 1);
            continue;
        }
        if (key == CH_INS) {
            insert_mode = (unsigned char)!insert_mode;
            shell_draw_cursor_cell(out, len, pos, 1);
            continue;
        }

        if (key == KEY_DEL || key == 8 || key == 127) {
            if (pos > 0 && len > 0) {
                --pos;
                i = pos;
                while (i < len) {
                    out[i] = out[i + 1];
                    ++i;
                }
                --len;
                shell_redraw_input_tail(out, len, pos, 1);
            }
            shell_draw_cursor_cell(out, len, pos, 1);
            continue;
        }

        insert_ch = 0;
        accepted = shell_key_to_insert_char(raw_key, &insert_ch, 0);

        if (accepted) {
            if (len < maxlen) {
                if (insert_mode && pos < len) {
                    i = len;
                    while (i > pos) {
                        out[i] = out[i - 1];
                        --i;
                    }
                    out[pos] = (char)insert_ch;
                    ++len;
                    ++pos;
                } else {
                    out[pos] = (char)insert_ch;
                    if (pos == len) {
                        ++len;
                    }
                    ++pos;
                }
                out[len] = 0;
                shell_redraw_input_tail(out, len, (unsigned char)(pos == 0 ? 0 : (pos - 1)), 0);
            }
            shell_draw_cursor_cell(out, len, pos, 1);
            continue;
        }

        shell_draw_cursor_cell(out, len, pos, 1);
    }
}

static int shell_append(char *dst, unsigned short max, const char *src) {
    unsigned short cur;
    unsigned short len;

    cur = (unsigned short)strlen(dst);
    len = (unsigned short)strlen(src);
    if ((unsigned long)cur + (unsigned long)len + 1ul > (unsigned long)max) return -1;
    memcpy(dst + cur, src, len + 1u);
    return 0;
}

static int shell_read_logical_line(char *out, unsigned short max) {
    char phys[PHYSICAL_MAX + 1];
    int n;
    unsigned short len;

    out[0] = 0;
    for (;;) {
        n = shell_read_physical_line(phys, PHYSICAL_MAX);
        if (n < 0) return -1;

        len = (unsigned short)strlen(phys);
        if (len > 0 && phys[len - 1u] == '`') {
            phys[len - 1u] = 0;
            if (shell_append(out, max, phys) != 0) {
                shell_write_line("ERR: COMMAND TOO LONG");
                return -1;
            }
            continue;
        }
        if (shell_append(out, max, phys) != 0) {
            shell_write_line("ERR: COMMAND TOO LONG");
            return -1;
        }
        break;
    }
    return (int)strlen(out);
}

static void shell_show_help(void) {
    shell_write_line("PRT MORE TOP SEL GEN TAP DRVI LST LDV STV");
}

static void shell_print_error(const RSError *err) {
    clear_line(g_cursor_y, C_WHITE);
    g_cursor_x = 0;
    shell_write_text_color("ERR[", C_LIGHTRED);
    shell_print_u16((unsigned short)err->code);
    shell_write_text_color("] line=", C_LIGHTRED);
    shell_print_u16(err->line);
    shell_write_text_color(" col=", C_LIGHTRED);
    shell_print_u16(err->column);
    shell_write_text_color(": ", C_LIGHTRED);
    if (err->message) shell_write_text_color(err->message, C_LIGHTRED);
    if (err->message && strncmp(err->message, "overlay ", 8u) == 0) {
        shell_write_text_color(" rc=", C_LIGHTRED);
        shell_print_u16((unsigned short)rs_overlay_last_rc());
    }
    shell_newline();
}

int main(void) {
    unsigned char bank;
    unsigned int payload_len = 0;

    shell_init_runtime_regions();
    (void)kbrepeat(KBREPEAT_NONE);

    g_line[0] = 0;
    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
        if (resume_try_load(&resume_blob, sizeof(resume_blob), &payload_len) &&
            payload_len == sizeof(resume_blob)) {
            memcpy(g_line, resume_blob.last_line, sizeof(g_line));
            g_line[LOGICAL_MAX - 1] = 0;
        } else {
            g_line[0] = 0;
        }
    }

    rs_vm_init(&g_vm);
    shell_pause_set_flags(0u);
    shell_pause_clear_buffer();

    g_platform.user = 0;
    g_platform.file_read = 0;
    g_platform.file_write = 0;
    g_platform.list_dir = 0;
    g_platform.drive_info = 0;
    rs_vm_set_platform(&g_vm, &g_platform);
    rs_vm_set_writer(&g_vm, shell_writer, 0);

    shell_draw_chrome();
    rs_overlay_debug_mark('M');

    if (rs_overlay_active()) {
        rs_overlay_debug_mark('J');
    } else if (rs_overlay_boot_with_progress(shell_overlay_progress, 0) == 0) {
        rs_overlay_debug_mark('K');
        shell_newline();
        shell_write_line("see readme help or website");
    } else {
        rs_overlay_debug_mark('k');
        shell_write_line("overlay failed");
    }

    for (;;) {
        if (shell_read_logical_line(g_line, sizeof(g_line)) < 0) break;

        shell_trim(g_line);
        if (g_line[0] == 0) continue;
        rs_overlay_debug_mark('L');

        if (rs_ci_equal(g_line, "HELP")) {
            shell_show_help();
            continue;
        }
        if (rs_ci_equal(g_line, "VER")) {
            clear_line(g_cursor_y, C_WHITE);
            g_cursor_x = 0;
            shell_write_text_color("version ", C_WHITE);
            shell_write_text_color(RS_VERSION, C_WHITE);
            shell_newline();
            continue;
        }
        if (rs_ci_equal(g_line, "CLEAR")) {
            shell_draw_chrome();
            continue;
        }

        rs_error_init(&g_err);
        rs_overlay_debug_mark('V');
        if (rs_vm_exec_source(&g_vm, g_line, &g_err) != 0) {
            rs_overlay_debug_mark('X');
            shell_print_error(&g_err);
        } else {
            rs_overlay_debug_mark('v');
        }
    }

    rs_vm_free(&g_vm);
    return 0;
}
