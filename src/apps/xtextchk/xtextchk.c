/*
 * xtextchk.c - Standalone text-file render harness
 *
 * Proves the plain SEQ -> cbm_read -> editor-style screen write path
 * independently of ReadyShell.
 *
 * XTEXTCHK_CASE:
 *   0 = run all samples
 *   1 = bang sample ("A!B")
 *   2 = ASCII pipe sample ("A|B")
 *   3 = raw PETSCII vline sample ("A<221>B")
 */

#include <cbm.h>
#include <conio.h>
#include <string.h>
#include "textfile_screen.h"
#include "tui.h"

#ifndef XTEXTCHK_CASE
#define XTEXTCHK_CASE 0
#endif

#define DRIVE8 8

#define LFN_FILE 2
#define LFN_CMD  15

#define RC_OK 0u
#define RC_IO 1u

#define STEP_CASE  1u
#define STEP_LOAD  2u
#define STEP_RENDER 3u
#define STEP_DUMP  4u

#define DBG_LEN    0x100u
#define SLOT_BASE  0x20u
#define SLOT_SIZE  0x20u
#define SLOT_COUNT 3u

#define LINE_CAP   32u
#define IO_BUF_CAP 16u
#define DRAW_X     2u
#define DRAW_Y     6u

typedef struct {
    const char* name;
    unsigned char expected_delim;
    unsigned char row;
} ProbeCase;

static unsigned char g_dbg[DBG_LEN];
static unsigned char g_line[LINE_CAP];
static unsigned char g_io_buf[IO_BUF_CAP];

static const ProbeCase g_cases[SLOT_COUNT] = {
    { "bang",  33u,  DRAW_Y + 0u },
    { "apipe", 124u, DRAW_Y + 2u },
    { "vline", 221u, DRAW_Y + 4u }
};

static void cleanup_io(void) {
    cbm_k_clrch();
    cbm_k_clall();
}

static void dbg_clear(void) {
    memset(g_dbg, 0, sizeof(g_dbg));
    g_dbg[0] = 0x54u;
    g_dbg[1] = 0x01u;
    g_dbg[2] = 0u;
    g_dbg[3] = (unsigned char)XTEXTCHK_CASE;
    g_dbg[8] = SLOT_COUNT;
}

static void dbg_set_fail(unsigned char step, unsigned char detail) {
    if (g_dbg[4] == 0u) {
        g_dbg[4] = step;
        g_dbg[5] = detail;
    }
    g_dbg[2] = 0u;
}

static unsigned char* slot_ptr(unsigned char index) {
    return g_dbg + SLOT_BASE + ((unsigned int)index * (unsigned int)SLOT_SIZE);
}

static void slot_store_name(unsigned char* slot, const char* name) {
    unsigned char i;

    for (i = 0u; i < 8u; ++i) {
        slot[i] = (name && name[i] != '\0') ? (unsigned char)name[i] : 0u;
    }
}

static void clear_screen_buffers(void) {
    memset(TUI_SCREEN, 32, 1000u);
    memset(TUI_COLOR_RAM, TUI_COLOR_WHITE, 1000u);
}

static unsigned char read_status_code(unsigned char* code_out) {
    char line[40];
    int n;
    unsigned int code;
    const char* p;

    n = cbm_read(LFN_CMD, line, sizeof(line) - 1u);
    if (n < 0) {
        n = 0;
    }
    line[n] = '\0';
    code = 0u;
    p = line;
    while (*p >= '0' && *p <= '9') {
        code = (unsigned int)(code * 10u + (unsigned int)(*p - '0'));
        ++p;
    }
    if (code_out) {
        *code_out = (unsigned char)((code > 255u) ? 255u : code);
    }
    return (unsigned char)n;
}

static unsigned char run_command(unsigned char device, const char* cmd) {
    unsigned char code;
    char* open_name;

    if (cmd) {
        open_name = (char*)cmd;
    } else {
        open_name = "";
    }
    cleanup_io();
    if (cbm_open(LFN_CMD, device, 15, open_name) != 0) {
        cleanup_io();
        return RC_IO;
    }
    code = 255u;
    (void)read_status_code(&code);
    cbm_close(LFN_CMD);
    cleanup_io();
    if (code <= 1u || code == 62u) {
        return RC_OK;
    }
    return RC_IO;
}

static unsigned char scratch_name(const char* name) {
    char cmd[24];

    strcpy(cmd, "s:");
    strcat(cmd, name);
    return run_command(DRIVE8, cmd);
}

static unsigned char write_named_dump_file(const char* name) {
    char open_name[24];
    int n;

    if (scratch_name(name) != RC_OK) {
        dbg_set_fail(STEP_DUMP, 1u);
        return 1u;
    }

    strcpy(open_name, name);
    strcat(open_name, ",s,w");
    cleanup_io();
    if (cbm_open(7u, DRIVE8, 2, open_name) != 0) {
        cleanup_io();
        dbg_set_fail(STEP_DUMP, 2u);
        return 1u;
    }
    n = cbm_write(7u, g_dbg, DBG_LEN);
    cbm_close(7u);
    cleanup_io();
    if (n != DBG_LEN) {
        dbg_set_fail(STEP_DUMP, 3u);
        return 1u;
    }
    return 0u;
}

static unsigned char read_first_line(const char* name,
                                     unsigned char* out,
                                     unsigned char cap,
                                     unsigned char* out_len) {
    char open_name[24];
    unsigned char total;
    unsigned char i;
    int n;

    if (!name || !out || cap == 0u) {
        return RC_IO;
    }

    strcpy(open_name, name);
    strcat(open_name, ",s,r");

    cleanup_io();
    if (cbm_open(LFN_FILE, DRIVE8, 2, open_name) != 0) {
        cleanup_io();
        return RC_IO;
    }

    total = 0u;
    while (1) {
        n = cbm_read(LFN_FILE, g_io_buf, IO_BUF_CAP);
        if (n <= 0) {
            break;
        }
        for (i = 0u; i < (unsigned char)n; ++i) {
            if (g_io_buf[i] == 13u) {
                cbm_close(LFN_FILE);
                cleanup_io();
                if (out_len) {
                    *out_len = total;
                }
                return RC_OK;
            }
            if (total + 1u >= cap) {
                cbm_close(LFN_FILE);
                cleanup_io();
                return RC_IO;
            }
            out[total++] = g_io_buf[i];
        }
    }

    cbm_close(LFN_FILE);
    cleanup_io();
    if (out_len) {
        *out_len = total;
    }
    return RC_OK;
}

static unsigned char find_delim(const unsigned char* text,
                                unsigned char len,
                                unsigned char expected) {
    unsigned char i;

    for (i = 0u; i < len; ++i) {
        if (text[i] == expected) {
            return i;
        }
    }
    return 0xFFu;
}

static void render_line_to_screen(const unsigned char* text,
                                  unsigned char len,
                                  unsigned char row) {
    unsigned int offset;
    unsigned char i;

    offset = (unsigned int)row * 40u + DRAW_X;
    for (i = 0u; i < len; ++i) {
        TUI_SCREEN[offset + i] = textfile_byte_to_screen(text[i]);
        TUI_COLOR_RAM[offset + i] = TUI_COLOR_WHITE;
    }
}

static unsigned char run_probe(unsigned char slot_index,
                               const ProbeCase* probe) {
    unsigned char* slot;
    unsigned char len;
    unsigned char delim_index;
    unsigned int screen_offset;
    unsigned char i;
    unsigned char rc;

    slot = slot_ptr(slot_index);
    slot_store_name(slot, probe->name);
    slot[11] = probe->expected_delim;

    memset(g_line, 0, sizeof(g_line));
    rc = read_first_line(probe->name, g_line, sizeof(g_line), &len);
    slot[8] = rc;
    slot[9] = len;
    if (rc != RC_OK) {
        dbg_set_fail(STEP_LOAD, slot_index + 1u);
        return 1u;
    }

    for (i = 0u; i < 8u; ++i) {
        slot[18u + i] = (i < len) ? g_line[i] : 0u;
    }

    delim_index = find_delim(g_line, len, probe->expected_delim);
    slot[10] = delim_index;
    if (delim_index == 0xFFu) {
        dbg_set_fail(STEP_LOAD, (unsigned char)(0x10u + slot_index));
        return 1u;
    }

    render_line_to_screen(g_line, len, probe->row);
    screen_offset = (unsigned int)probe->row * 40u + DRAW_X;

    slot[12] = g_line[delim_index];
    slot[13] = textfile_byte_to_screen(g_line[delim_index]);
    slot[14] = TUI_SCREEN[screen_offset + delim_index];
    slot[15] = (delim_index > 0u) ? TUI_SCREEN[screen_offset + delim_index - 1u] : 0u;
    slot[16] = (delim_index + 1u < len) ? TUI_SCREEN[screen_offset + delim_index + 1u] : 0u;

    if (slot[14] == 32u) {
        dbg_set_fail(STEP_RENDER, slot_index + 1u);
    }
    return 0u;
}

static unsigned char should_run_case(unsigned char index) {
#if XTEXTCHK_CASE == 0
    (void)index;
    return 1u;
#else
    if (XTEXTCHK_CASE == 0) {
        return 1u;
    }
    return (unsigned char)(XTEXTCHK_CASE == (unsigned char)(index + 1u));
#endif
}

int main(void) {
    unsigned char i;

    clrscr();
    clear_screen_buffers();
    dbg_clear();

    for (i = 0u; i < SLOT_COUNT; ++i) {
        if (!should_run_case(i)) {
            continue;
        }
        if (run_probe(i, &g_cases[i]) != 0u) {
            (void)write_named_dump_file("xtextstat");
            cprintf("XTEXT FAIL %u\r\n", (unsigned int)(i + 1u));
            return 1;
        }
    }

    if (g_dbg[4] == 0u) {
        g_dbg[2] = 1u;
    }
    (void)write_named_dump_file("xtextstat");
    cprintf("XTEXT OK\r\n");
    return (g_dbg[2] != 0u) ? 0 : 1;
}
