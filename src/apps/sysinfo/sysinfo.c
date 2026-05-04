/*
 * sysinfo.c - ReadyOS System Info
 *
 * Read-only split-pane hardware summary. The app does not allocate REU banks;
 * it only probes and restores bytes while reporting REU presence/size.
 */

#include "../../lib/tui.h"
#include "sysinfo_uci.h"

#include <c64.h>
#include <string.h>

#define HEADER_Y       0
#define PANE_Y         2
#define PANE_H         20
#define STATUS_Y       23
#define HELP_Y         24

#define LIST_X         0
#define LIST_W         10
#define DIVIDER_X      10
#define INFO_X         11
#define INFO_W         29

#define TAB_SYSTEM     0
#define TAB_ULTIMATE   1
#define TAB_COUNT      2

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

#define KERNAL_REV_BYTE (*(volatile unsigned char*)0xE4AC)
#define BASIC_ROM       ((volatile unsigned char*)0xA000)
#define JIFFY_LO        (*(volatile unsigned char*)0x00A2)
#define JIFFY_MID       (*(volatile unsigned char*)0x00A1)
#define JIFFY_HI        (*(volatile unsigned char*)0x00A0)
#define VIC_CTRL1       (*(volatile unsigned char*)0xD011)
#define VIC_RASTER      (*(volatile unsigned char*)0xD012)

#define REU_COMMAND  (*(volatile unsigned char*)0xDF01)
#define REU_C64_LO   (*(volatile unsigned char*)0xDF02)
#define REU_C64_HI   (*(volatile unsigned char*)0xDF03)
#define REU_REU_LO   (*(volatile unsigned char*)0xDF04)
#define REU_REU_HI   (*(volatile unsigned char*)0xDF05)
#define REU_REU_BANK (*(volatile unsigned char*)0xDF06)
#define REU_LEN_LO   (*(volatile unsigned char*)0xDF07)
#define REU_LEN_HI   (*(volatile unsigned char*)0xDF08)

#define REU_CMD_STASH 0x90
#define REU_CMD_FETCH 0x91
#define REU_TEST_OFF  0xFFF0u

static unsigned char running;
static unsigned char current_tab;
static unsigned char row_y;

static unsigned char uci_data[SYSINFO_UCI_DATA_MAX];
static unsigned char uci_stat[SYSINFO_UCI_STAT_MAX];
static unsigned char uci_data_len;
static unsigned char uci_stat_len;
static char value_buf[32];

static const char *tab_names[TAB_COUNT] = {
    "system",
    "ultimate"
};

static void draw_shell(void);
static void draw_tabs(void);
static void draw_divider(void);
static void refresh_current_tab(void);
static void draw_system_tab(void);
static void draw_ultimate_tab(void);
static void add_row(const char *label, const char *value, unsigned char color);
static void add_row_uint(const char *label, unsigned int value);
static void copy_uci_text(char *dst, unsigned char dst_len);
static void format_uptime(char *dst);
static void format_reu(char *dst);
static void format_video(char *dst);
static void format_mac(char *dst, const unsigned char *src);
static void format_ip(char *dst, const unsigned char *src);
static void format_drive_info(char *dst, const unsigned char *src, unsigned char len);
static void append_char(char *dst, unsigned char dst_len, char ch);
static void append_str(char *dst, unsigned char dst_len, const char *src);
static void append_uint(char *dst, unsigned char dst_len, unsigned int value);
static void append_hex2(char *dst, unsigned char dst_len, unsigned char value);
static unsigned char reu_detect(void);
static unsigned int reu_detect_kb(void);
static void reu_stash_byte(unsigned char bank, unsigned int off, unsigned char value);
static unsigned char reu_fetch_byte(unsigned char bank, unsigned int off);

static void append_char(char *dst, unsigned char dst_len, char ch) {
    unsigned char len;

    len = (unsigned char)strlen(dst);
    if (len + 1u >= dst_len) {
        return;
    }
    dst[len] = ch;
    dst[len + 1u] = 0;
}

static void append_str(char *dst, unsigned char dst_len, const char *src) {
    while (*src != 0) {
        append_char(dst, dst_len, *src);
        ++src;
    }
}

static void append_uint(char *dst, unsigned char dst_len, unsigned int value) {
    char rev[6];
    unsigned char count;

    count = 0u;
    do {
        rev[count] = (char)('0' + (value % 10u));
        value = (unsigned int)(value / 10u);
        ++count;
    } while (value != 0u && count < sizeof(rev));

    while (count > 0u) {
        --count;
        append_char(dst, dst_len, rev[count]);
    }
}

static void append_hex2(char *dst, unsigned char dst_len, unsigned char value) {
    static const char hex[] = "0123456789ABCDEF";

    append_char(dst, dst_len, hex[(value >> 4) & 0x0Fu]);
    append_char(dst, dst_len, hex[value & 0x0Fu]);
}

static void append_2digit(char *dst, unsigned char dst_len, unsigned int value) {
    append_char(dst, dst_len, (char)('0' + ((value / 10u) % 10u)));
    append_char(dst, dst_len, (char)('0' + (value % 10u)));
}

static void add_row(const char *label, const char *value, unsigned char color) {
    if (row_y >= PANE_Y + PANE_H) {
        return;
    }
    tui_clear_line(row_y, INFO_X, INFO_W, TUI_COLOR_WHITE);
    tui_puts_n(INFO_X, row_y, label, 9u, TUI_COLOR_GRAY3);
    tui_puts_n(INFO_X + 9u, row_y, value, (unsigned char)(INFO_W - 9u), color);
    ++row_y;
}

static void add_row_uint(const char *label, unsigned int value) {
    value_buf[0] = 0;
    append_uint(value_buf, sizeof(value_buf), value);
    add_row(label, value_buf, TUI_COLOR_WHITE);
}

static void draw_header(void) {
    TuiRect header;

    header.x = 0u;
    header.y = HEADER_Y;
    header.w = 40u;
    header.h = 2u;
    tui_window(&header, TUI_COLOR_LIGHTBLUE);
    tui_puts(1u, HEADER_Y, "SYSTEM INFO", TUI_COLOR_YELLOW);
}

static void draw_tabs(void) {
    unsigned char row;
    unsigned char y;
    unsigned char color;

    for (row = 0u; row < PANE_H; ++row) {
        y = (unsigned char)(PANE_Y + row);
        tui_clear_line(y, LIST_X, LIST_W, TUI_COLOR_BLUE);
        if (row >= TAB_COUNT) {
            continue;
        }
        color = (row == current_tab) ? TUI_COLOR_CYAN : TUI_COLOR_WHITE;
        tui_putc(0u, y, (row == current_tab) ? tui_ascii_to_screen('>') : 32u, color);
        tui_puts_n(1u, y, tab_names[row], 9u, color);
    }
}

static void draw_divider(void) {
    unsigned char y;

    for (y = PANE_Y; y < PANE_Y + PANE_H; ++y) {
        tui_putc(DIVIDER_X, y, TUI_VLINE, TUI_COLOR_GRAY2);
    }
}

static void draw_help(void) {
    tui_clear_line(HELP_Y, 0u, 40u, TUI_COLOR_GRAY3);
    tui_puts(0u, HELP_Y, "UP/DN:TABS F2/F4:APPS CTRL+B:HOME", TUI_COLOR_GRAY3);
}

static void draw_status(void) {
    tui_clear_line(STATUS_Y, 0u, 40u, TUI_COLOR_WHITE);
    tui_puts(0u, STATUS_Y, "READ-ONLY INFO  REFRESH ON TAB ENTRY", TUI_COLOR_GRAY3);
}

static void draw_shell(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    draw_tabs();
    draw_divider();
    draw_status();
    draw_help();
}

static void clear_info_pane(void) {
    unsigned char y;

    for (y = PANE_Y; y < PANE_Y + PANE_H; ++y) {
        tui_clear_line(y, INFO_X, INFO_W, TUI_COLOR_WHITE);
    }
    row_y = PANE_Y;
}

static const char *kernal_name(void) {
    switch (KERNAL_REV_BYTE) {
        case 0x2Bu: return "901227-01";
        case 0x5Cu: return "901227-02";
        case 0x81u: return "901227-03";
        case 0x63u: return "901246-01 sx";
        case 0x00u: return "406145-02 jp";
        case 0xB3u: return "251104-04 sx";
        default:    return "custom/unknown";
    }
}

static const char *basic_name(void) {
    if (BASIC_ROM[0] == 0x94u && BASIC_ROM[1] == 0xE3u &&
        BASIC_ROM[2] == 0x7Bu && BASIC_ROM[3] == 0xE3u &&
        BASIC_ROM[4] == 'C' && BASIC_ROM[5] == 'B') {
        return "basic 2.0";
    }
    return "custom/unknown";
}

static unsigned int read_raster_line(void) {
    unsigned char hi;
    unsigned char lo;
    unsigned char hi2;

    hi = (unsigned char)(VIC_CTRL1 & 0x80u);
    lo = VIC_RASTER;
    hi2 = (unsigned char)(VIC_CTRL1 & 0x80u);
    if (hi != hi2) {
        lo = VIC_RASTER;
        hi = hi2;
    }
    return (unsigned int)lo + (hi ? 256u : 0u);
}

static unsigned int detect_raster_lines(void) {
    unsigned int guard;
    unsigned int line;
    unsigned int prev;
    unsigned int max_line;

    guard = 60000u;
    do {
        line = read_raster_line();
        --guard;
    } while (line < 250u && guard != 0u);

    guard = 60000u;
    do {
        line = read_raster_line();
        --guard;
    } while (line >= 20u && guard != 0u);

    prev = line;
    max_line = line;
    guard = 60000u;
    do {
        line = read_raster_line();
        if (line != prev) {
            if (line > max_line) {
                max_line = line;
            }
            if (prev > 200u && line < 20u) {
                break;
            }
            prev = line;
        }
        --guard;
    } while (guard != 0u);

    return (unsigned int)(max_line + 1u);
}

static void format_video(char *dst) {
    unsigned int lines;

    lines = detect_raster_lines();
    dst[0] = 0;
    if (lines >= 311u && lines <= 313u) {
        append_str(dst, 32u, "pal ");
    } else if (lines == 263u) {
        append_str(dst, 32u, "ntsc ");
    } else if (lines == 262u) {
        append_str(dst, 32u, "old ntsc ");
    } else {
        append_str(dst, 32u, "unknown ");
    }
    append_uint(dst, 32u, lines);
    append_str(dst, 32u, " lines");
}

static void format_uptime(char *dst) {
    unsigned long ticks;
    unsigned long total_seconds;
    unsigned int hours;
    unsigned int minutes;
    unsigned int seconds;

    ticks = ((unsigned long)JIFFY_HI << 16) |
            ((unsigned long)JIFFY_MID << 8) |
            (unsigned long)JIFFY_LO;
    total_seconds = ticks / 60UL;
    hours = (unsigned int)(total_seconds / 3600UL);
    minutes = (unsigned int)((total_seconds / 60UL) % 60UL);
    seconds = (unsigned int)(total_seconds % 60UL);

    dst[0] = 0;
    append_uint(dst, 32u, hours);
    append_char(dst, 32u, ':');
    append_2digit(dst, 32u, minutes);
    append_char(dst, 32u, ':');
    append_2digit(dst, 32u, seconds);
}

static void reu_stash_byte(unsigned char bank, unsigned int off, unsigned char value) {
    static unsigned char byte_value;

    byte_value = value;
    REU_C64_LO = (unsigned char)((unsigned int)&byte_value & 0xFFu);
    REU_C64_HI = (unsigned char)((unsigned int)&byte_value >> 8);
    REU_REU_LO = (unsigned char)(off & 0xFFu);
    REU_REU_HI = (unsigned char)(off >> 8);
    REU_REU_BANK = bank;
    REU_LEN_LO = 1u;
    REU_LEN_HI = 0u;
    REU_COMMAND = REU_CMD_STASH;
}

static unsigned char reu_fetch_byte(unsigned char bank, unsigned int off) {
    static unsigned char byte_value;

    byte_value = 0u;
    REU_C64_LO = (unsigned char)((unsigned int)&byte_value & 0xFFu);
    REU_C64_HI = (unsigned char)((unsigned int)&byte_value >> 8);
    REU_REU_LO = (unsigned char)(off & 0xFFu);
    REU_REU_HI = (unsigned char)(off >> 8);
    REU_REU_BANK = bank;
    REU_LEN_LO = 1u;
    REU_LEN_HI = 0u;
    REU_COMMAND = REU_CMD_FETCH;
    return byte_value;
}

static unsigned char reu_detect(void) {
    unsigned char orig;
    unsigned char got;

    orig = reu_fetch_byte(0u, REU_TEST_OFF);
    reu_stash_byte(0u, REU_TEST_OFF, 0xA5u);
    got = reu_fetch_byte(0u, REU_TEST_OFF);
    reu_stash_byte(0u, REU_TEST_OFF, orig);
    return (unsigned char)(got == 0xA5u);
}

static unsigned int reu_detect_kb(void) {
    static const unsigned char candidates[] = {
        1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u
    };
    unsigned char base_orig;
    unsigned char cand_orig;
    unsigned char got;
    unsigned char i;

    if (!reu_detect()) {
        return 0u;
    }

    base_orig = reu_fetch_byte(0u, REU_TEST_OFF);
    reu_stash_byte(0u, REU_TEST_OFF, 0x5Au);

    for (i = 0u; i < sizeof(candidates); ++i) {
        cand_orig = reu_fetch_byte(candidates[i], REU_TEST_OFF);
        reu_stash_byte(candidates[i], REU_TEST_OFF, 0xC3u);
        got = reu_fetch_byte(0u, REU_TEST_OFF);
        reu_stash_byte(candidates[i], REU_TEST_OFF, cand_orig);
        if (got == 0xC3u) {
            reu_stash_byte(0u, REU_TEST_OFF, base_orig);
            return (unsigned int)candidates[i] * 64u;
        }
    }

    reu_stash_byte(0u, REU_TEST_OFF, base_orig);
    return 16384u;
}

static void format_reu(char *dst) {
    unsigned int kb;

    kb = reu_detect_kb();
    dst[0] = 0;
    if (kb == 0u) {
        append_str(dst, 32u, "absent");
        return;
    }
    append_uint(dst, 32u, kb);
    append_str(dst, 32u, " kb");
    if (kb >= 1024u) {
        append_str(dst, 32u, " / ");
        append_uint(dst, 32u, (unsigned int)(kb / 1024u));
        append_str(dst, 32u, " mb");
    }
}

static void draw_system_tab(void) {
    clear_info_pane();
    add_row("kernal:", kernal_name(), TUI_COLOR_WHITE);

    value_buf[0] = '$';
    value_buf[1] = 0;
    append_hex2(value_buf, sizeof(value_buf), KERNAL_REV_BYTE);
    add_row("k byte:", value_buf, TUI_COLOR_GRAY3);

    add_row("basic:", basic_name(), TUI_COLOR_WHITE);
    add_row("sku:", "unknown", TUI_COLOR_GRAY3);
    format_video(value_buf);
    add_row("video:", value_buf, TUI_COLOR_CYAN);
    add_row("emulator:", "not reliable", TUI_COLOR_GRAY3);

    format_reu(value_buf);
    add_row("reu:", value_buf, TUI_COLOR_YELLOW);

    format_uptime(value_buf);
    add_row("up since:", value_buf, TUI_COLOR_LIGHTGREEN);
    add_row("timer:", "24-bit jiffy wraps", TUI_COLOR_GRAY3);
}

static unsigned char run_uci(const unsigned char *cmd, unsigned char len) {
    uci_data_len = 0u;
    uci_stat_len = 0u;
    return sysinfo_uci_command(cmd, len,
                               uci_data, sizeof(uci_data), &uci_data_len,
                               uci_stat, sizeof(uci_stat), &uci_stat_len);
}

static void copy_uci_text(char *dst, unsigned char dst_len) {
    unsigned char i;
    unsigned char ch;

    dst[0] = 0;
    for (i = 0u; i < uci_data_len && i + 1u < dst_len; ++i) {
        ch = uci_data[i];
        if (ch < 32u || ch > 126u) {
            ch = ' ';
        }
        append_char(dst, dst_len, (char)ch);
    }
    if (dst[0] == 0) {
        append_str(dst, dst_len, "no data");
    }
}

static void format_mac(char *dst, const unsigned char *src) {
    unsigned char i;

    dst[0] = 0;
    for (i = 0u; i < 6u; ++i) {
        if (i != 0u) {
            append_char(dst, 32u, ':');
        }
        append_hex2(dst, 32u, src[i]);
    }
}

static void format_ip(char *dst, const unsigned char *src) {
    unsigned char i;

    dst[0] = 0;
    for (i = 0u; i < 4u; ++i) {
        if (i != 0u) {
            append_char(dst, 32u, '.');
        }
        append_uint(dst, 32u, src[i]);
    }
}

static const char *drive_type_name(unsigned char type) {
    switch (type) {
        case 0x00u: return "1541";
        case 0x01u: return "1571";
        case 0x02u: return "1581";
        case 0x03u: return "undec";
        case 0x0Fu: return "softiec";
        case 0x50u: return "printer";
        default:    return "other";
    }
}

static void format_drive_info(char *dst, const unsigned char *src, unsigned char len) {
    unsigned char count;

    dst[0] = 0;
    if (len == 0u) {
        append_str(dst, 32u, "no data");
        return;
    }
    count = src[0];
    append_uint(dst, 32u, count);
    append_str(dst, 32u, " drive");
    if (count != 1u) {
        append_char(dst, 32u, 's');
    }
    if (len >= 4u && count > 0u) {
        append_str(dst, 32u, " d");
        append_uint(dst, 32u, src[2]);
        append_char(dst, 32u, ' ');
        append_str(dst, 32u, drive_type_name(src[1]));
        append_char(dst, 32u, ' ');
        append_str(dst, 32u, src[3] ? "on" : "off");
    }
}

static void draw_ultimate_tab(void) {
    static const unsigned char cmd_ctrl_ident[] = {0x04u, 0x01u};
    static const unsigned char cmd_hwinfo[] = {0x04u, 0x28u, 0x00u};
    static const unsigned char cmd_drvinfo[] = {0x04u, 0x29u, 0x00u};
    static const unsigned char cmd_net_ident[] = {0x03u, 0x01u};
    static const unsigned char cmd_net_count[] = {0x03u, 0x02u};
    static const unsigned char cmd_net_mac0[] = {0x03u, 0x04u, 0x00u};
    static const unsigned char cmd_net_ip0[] = {0x03u, 0x05u, 0x00u};

    clear_info_pane();
    if (!sysinfo_uci_detect()) {
        add_row("uci:", "not detected", TUI_COLOR_LIGHTRED);
        add_row("hint:", "enable the uci", TUI_COLOR_YELLOW);
        add_row("", "to detect ultimate", TUI_COLOR_YELLOW);
        return;
    }

    add_row("uci:", "detected", TUI_COLOR_LIGHTGREEN);
    if (run_uci(cmd_ctrl_ident, sizeof(cmd_ctrl_ident))) {
        copy_uci_text(value_buf, sizeof(value_buf));
        add_row("ctrl:", value_buf, TUI_COLOR_WHITE);
    }
    if (run_uci(cmd_hwinfo, sizeof(cmd_hwinfo))) {
        copy_uci_text(value_buf, sizeof(value_buf));
        add_row("model:", value_buf, TUI_COLOR_CYAN);
    } else {
        add_row("model:", "not exposed", TUI_COLOR_GRAY3);
    }
    if (run_uci(cmd_drvinfo, sizeof(cmd_drvinfo))) {
        format_drive_info(value_buf, uci_data, uci_data_len);
        add_row("drives:", value_buf, TUI_COLOR_WHITE);
    }
    if (run_uci(cmd_net_ident, sizeof(cmd_net_ident))) {
        copy_uci_text(value_buf, sizeof(value_buf));
        add_row("net:", value_buf, TUI_COLOR_WHITE);
    }
    if (run_uci(cmd_net_count, sizeof(cmd_net_count)) && uci_data_len > 0u) {
        add_row_uint("ifaces:", uci_data[0]);
        if (uci_data[0] > 0u) {
            if (run_uci(cmd_net_mac0, sizeof(cmd_net_mac0)) && uci_data_len >= 6u) {
                format_mac(value_buf, uci_data);
                add_row("mac0:", value_buf, TUI_COLOR_WHITE);
            }
            if (run_uci(cmd_net_ip0, sizeof(cmd_net_ip0)) && uci_data_len >= 12u) {
                format_ip(value_buf, uci_data);
                add_row("ip0:", value_buf, TUI_COLOR_LIGHTGREEN);
                format_ip(value_buf, uci_data + 4u);
                add_row("mask:", value_buf, TUI_COLOR_WHITE);
                format_ip(value_buf, uci_data + 8u);
                add_row("gw:", value_buf, TUI_COLOR_WHITE);
            }
        }
    }
    add_row("firmware:", "not exposed by uci", TUI_COLOR_GRAY3);
    add_row("hostname:", "not exposed by uci", TUI_COLOR_GRAY3);
}

static void refresh_current_tab(void) {
    draw_tabs();
    if (current_tab == TAB_SYSTEM) {
        draw_system_tab();
    } else {
        draw_ultimate_tab();
    }
}

static void sysinfo_loop(void) {
    unsigned char key;
    unsigned char nav_action;

    draw_shell();
    refresh_current_tab();

    while (running) {
        key = tui_getkey();
        nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1u);
        if (nav_action == TUI_HOTKEY_LAUNCHER) {
            tui_return_to_launcher();
        }
        if (nav_action >= 1u && nav_action <= 23u) {
            tui_switch_to_app(nav_action);
            continue;
        }
        if (nav_action == TUI_HOTKEY_BIND_ONLY) {
            continue;
        }

        switch (key) {
            case TUI_KEY_UP:
                if (current_tab > 0u) {
                    --current_tab;
                    refresh_current_tab();
                }
                break;
            case TUI_KEY_DOWN:
                if (current_tab + 1u < TAB_COUNT) {
                    ++current_tab;
                    refresh_current_tab();
                }
                break;
            case TUI_KEY_LEFT:
            case TUI_KEY_RIGHT:
            case TUI_KEY_RETURN:
                refresh_current_tab();
                break;
            case TUI_KEY_RUNSTOP:
                running = 0u;
                break;
        }
    }
    __asm__("jmp $FCE2");
}

int main(void) {
    tui_init();
    current_tab = TAB_SYSTEM;
    running = 1u;
    sysinfo_loop();
    return 0;
}
