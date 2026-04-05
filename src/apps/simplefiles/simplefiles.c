/*
 * simplefiles.c - ReadyOS compact dual-pane file manager
 *
 * V1 goals:
 * - dual-pane browser for IEC drives 8 and 9
 * - delete / rename / copy / duplicate
 * - paged SEQ viewer with low RAM usage
 * - ReadyOS resume/app-switch behavior
 */

#include "../../lib/tui.h"
#include "../../lib/file_browser.h"
#include "../../lib/resume_state.h"
#include "../../lib/storage_device.h"

#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

#define MODE_BROWSER 0
#define MODE_VIEWER  1

#define KEY_CTRL_B   2
#define KEY_CTRL_N   14
#define KEY_CTRL_P   16
#define KEY_HELP     TUI_KEY_F8

#define PANE_LEFT    0
#define PANE_RIGHT   1
#define PANE_COUNT   2

#define HEADER_Y     0
#define PANE_TITLE_Y 3
#define LIST_START_Y 4
#define LIST_HEIGHT  16
#define DETAIL_Y     20
#define STATUS_Y     21
#define INFO_Y       22
#define HELP_Y1      23
#define HELP_Y2      24

#define LEFT_X       0
#define LEFT_W       19
#define DIVIDER_X    19
#define RIGHT_X      20
#define RIGHT_W      20

#define MAX_PANE_ENTRIES 32
#define VIEW_COLS        38
#define VIEW_ROWS        16
#define VIEW_PAGE_BYTES  (VIEW_COLS * VIEW_ROWS)
#define COPY_BUF_SIZE    128
#define SIMPLEFILES_DRIVE_MASK 0x03u

typedef struct {
    unsigned char drive;
    unsigned char available;
    unsigned char load_error;
    unsigned char count;
    unsigned char selected;
    unsigned char scroll;
    unsigned int free_blocks;
    FileBrowserEntry entries[MAX_PANE_ENTRIES];
} FilePane;

typedef struct {
    unsigned char pane_drive[PANE_COUNT];
    unsigned char pane_selected[PANE_COUNT];
    unsigned char pane_scroll[PANE_COUNT];
    unsigned char active_pane;
    unsigned char mode;
    unsigned char viewer_drive;
    unsigned char viewer_type;
    unsigned char reserved;
    unsigned char discovered_mask;
    unsigned int viewer_offset;
    char viewer_name[FILE_BROWSER_NAME_LEN];
} SimpleFilesResumeV1;

static FilePane panes[PANE_COUNT];
static unsigned char active_pane;
static unsigned char running;
static unsigned char discovered_mask;
static unsigned char app_mode;
static unsigned char resume_ready;

static FileBrowserEntry viewer_entry;
static FileBrowserEntry stage_entry;
static unsigned char viewer_drive;
static unsigned int viewer_offset;
static unsigned int viewer_len;
static unsigned char viewer_buf[VIEW_PAGE_BYTES];
static unsigned char copy_buf[COPY_BUF_SIZE];
static char drive_menu_items[4][10];
static const char *drive_menu_ptrs[4];
static unsigned char drive_menu_values[4];

static char status_msg[40];
static unsigned char status_color;
static SimpleFilesResumeV1 resume_blob;

static void set_status(const char *msg, unsigned char color);
static unsigned char normalize_drive_8_9(unsigned char drive);
static void format_drive_mask(char *out);
static void format_block_value(unsigned int value, char *out);
static unsigned char drive_in_mask(unsigned char mask, unsigned char drive);
static unsigned char first_drive_in_mask(unsigned char mask);
static unsigned char next_drive_in_mask(unsigned char mask, unsigned char current);
static void seed_default_drives(void);
static void probe_drives(void);
static void pane_clamp(FilePane *pane);
static FilePane *active_file_pane(void);
static FilePane *passive_file_pane(void);
static FileBrowserEntry *pane_selected_entry(FilePane *pane);
static unsigned char pane_find_name(FilePane *pane, const char *name);
static void pane_load(unsigned char pane_index, const char *prefer_name);
static void reload_matching_drives(unsigned char drive, const char *prefer_name);
static void draw_browser(void);
static void draw_header(void);
static void draw_pane_title(unsigned char pane_index);
static void draw_pane_row(unsigned char pane_index, unsigned char row);
static void draw_pane(unsigned char pane_index);
static void draw_detail(void);
static void draw_status(void);
static void draw_help(void);
static void draw_divider(void);
static void draw_viewer(void);
static unsigned char show_confirm(const char *msg);
static unsigned char show_name_dialog(const char *title,
                                      const char *prompt,
                                      char *buffer,
                                      const char *initial_value);
static void browser_switch_active(unsigned char next_active);
static void browser_move_vertical(unsigned char move_down);
static void browser_page_move(unsigned char move_down);
static void browser_cycle_drive(void);
static void browser_swap_drives(void);
static void browser_refresh_all(void);
static unsigned char browser_target_name(const char *title,
                                         const char *initial_value,
                                         char *out_name);
static unsigned char browser_choose_copy_drive(unsigned char src_drive,
                                               unsigned char preferred_drive,
                                               unsigned char *out_drive);
static unsigned char browser_duplicate_via_stage(FilePane *src_pane,
                                                 FilePane *dst_pane,
                                                 FileBrowserEntry *entry,
                                                 const char *dst_name,
                                                 unsigned char *code_out,
                                                 char *msg_out,
                                                 unsigned char msg_cap);
static void browser_normalize_visible_drives(void);
static unsigned char browser_name_exists_on_loaded_drive(unsigned char drive,
                                                         const char *name,
                                                         FilePane *src_pane,
                                                         FilePane *dst_pane,
                                                         const char *src_name);
static FileBrowserEntry *browser_find_loaded_entry(unsigned char drive,
                                                   const char *name);
static unsigned char browser_verify_copy_result(const FileBrowserEntry *src_entry,
                                                unsigned char dst_drive,
                                                const char *dst_name);
static void browser_set_copy_status(unsigned char same_drive,
                                    unsigned char src_drive,
                                    const char *src_name,
                                    unsigned char dst_drive,
                                    const char *dst_name);
static void browser_delete_entry(void);
static void browser_rename_entry(void);
static void browser_copy_entry(unsigned char prompt_name,
                               unsigned char force_same_drive);
static void browser_open_entry(void);
static unsigned char viewer_load_page(void);
static void viewer_page_down(void);
static void viewer_page_up(void);
static void enter_viewer(FilePane *pane, const FileBrowserEntry *entry);
static void leave_viewer(void);
static void show_help_popup(void);
static void resume_save_state(void);
static unsigned char restore_resume_state(void);

static void set_status(const char *msg, unsigned char color) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1u);
    status_msg[sizeof(status_msg) - 1u] = 0;
    status_color = color;
}

static unsigned char normalize_drive_8_9(unsigned char drive) {
    if (drive == 9u) {
        return 9u;
    }
    return 8u;
}

static void format_drive_mask(char *out) {
    unsigned char drive;
    unsigned char pos;

    pos = 0;
    for (drive = 8; drive <= 9; ++drive) {
        if ((discovered_mask & (unsigned char)(1u << (drive - 8u))) == 0u) {
            continue;
        }
        if (pos != 0u) {
            out[pos++] = ' ';
        }
        out[pos++] = '0' + (drive / 10u);
        out[pos++] = '0' + (drive % 10u);
    }
    if (pos == 0u) {
        strcpy(out, "NONE");
    } else {
        out[pos] = 0;
    }
}

static void format_block_value(unsigned int value, char *out) {
    char digits[6];
    unsigned char pos;
    unsigned char width;

    width = 4;
    out[0] = ' ';
    out[1] = ' ';
    out[2] = ' ';
    out[3] = ' ';
    out[4] = 0;

    if (value > 9999u) {
        out[0] = '+';
        out[1] = '+';
        out[2] = '+';
        out[3] = '+';
        return;
    }

    pos = 0;
    do {
        digits[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && pos < sizeof(digits));

    while (pos != 0u && width != 0u) {
        out[--width] = digits[--pos];
    }
}

static unsigned char drive_in_mask(unsigned char mask, unsigned char drive) {
    if (drive != 8u && drive != 9u) {
        return 0u;
    }
    return (unsigned char)(mask & (unsigned char)(1u << (drive - 8u)));
}

static unsigned char first_drive_in_mask(unsigned char mask) {
    unsigned char drive;

    for (drive = 8; drive <= 9; ++drive) {
        if (mask & (unsigned char)(1u << (drive - 8u))) {
            return drive;
        }
    }
    return 8;
}

static unsigned char next_drive_in_mask(unsigned char mask, unsigned char current) {
    unsigned char drive;

    if (mask == 0u) {
        return normalize_drive_8_9(current);
    }

    current = normalize_drive_8_9(current);
    for (drive = 0; drive < 2; ++drive) {
        ++current;
        if (current > 9u) {
            current = 8u;
        }
        if (mask & (unsigned char)(1u << (current - 8u))) {
            return current;
        }
    }

    return first_drive_in_mask(mask);
}

static void seed_default_drives(void) {
    unsigned char primary;
    unsigned char secondary;

    primary = storage_device_get_default();
    if (discovered_mask != 0u &&
        (discovered_mask & (unsigned char)(1u << (primary - 8u))) == 0u) {
        primary = first_drive_in_mask(discovered_mask);
    }

    secondary = storage_device_toggle_8_9(primary);
    if (discovered_mask != 0u &&
        (discovered_mask & (unsigned char)(1u << (secondary - 8u))) == 0u) {
        secondary = next_drive_in_mask(discovered_mask, primary);
    }

    panes[PANE_LEFT].drive = normalize_drive_8_9(primary);
    panes[PANE_RIGHT].drive = normalize_drive_8_9(secondary);
}

static void probe_drives(void) {
    discovered_mask = SIMPLEFILES_DRIVE_MASK;
}

static void browser_normalize_visible_drives(void) {
    panes[PANE_LEFT].drive = normalize_drive_8_9(panes[PANE_LEFT].drive);
    panes[PANE_RIGHT].drive = normalize_drive_8_9(panes[PANE_RIGHT].drive);
    if (panes[PANE_LEFT].drive == panes[PANE_RIGHT].drive) {
        panes[PANE_RIGHT].drive = storage_device_toggle_8_9(panes[PANE_LEFT].drive);
    }
}

static void pane_clamp(FilePane *pane) {
    if (pane->count == 0u) {
        pane->selected = 0u;
        pane->scroll = 0u;
        return;
    }
    if (pane->selected >= pane->count) {
        pane->selected = (unsigned char)(pane->count - 1u);
    }
    if (pane->scroll > pane->selected) {
        pane->scroll = pane->selected;
    }
    if (pane->selected >= pane->scroll + LIST_HEIGHT) {
        pane->scroll = (unsigned char)(pane->selected - LIST_HEIGHT + 1u);
    }
}

static FilePane *active_file_pane(void) {
    return &panes[active_pane];
}

static FilePane *passive_file_pane(void) {
    return &panes[active_pane == PANE_LEFT ? PANE_RIGHT : PANE_LEFT];
}

static FileBrowserEntry *pane_selected_entry(FilePane *pane) {
    if (pane->count == 0u || pane->selected >= pane->count) {
        return 0;
    }
    return &pane->entries[pane->selected];
}

static unsigned char pane_find_name(FilePane *pane, const char *name) {
    unsigned char i;

    for (i = 0; i < pane->count; ++i) {
        if (strcmp(pane->entries[i].name, name) == 0) {
            return i;
        }
    }
    return 255;
}

static void pane_load(unsigned char pane_index, const char *prefer_name) {
    FilePane *pane;
    unsigned char saved_selected;
    char selected_name[FILE_BROWSER_NAME_LEN];

    pane = &panes[pane_index];
    saved_selected = pane->selected;
    selected_name[0] = 0;

    if (pane->count != 0u && pane->selected < pane->count) {
        strcpy(selected_name, pane->entries[pane->selected].name);
    }

    pane->available = (unsigned char)(pane->drive == 8u || pane->drive == 9u);
    pane->load_error = 0;
    pane->count = 0;
    pane->free_blocks = 0;

    if (!pane->available) {
        pane->selected = 0;
        pane->scroll = 0;
        return;
    }

    if (file_browser_read_directory(pane->drive,
                                    pane->entries,
                                    MAX_PANE_ENTRIES,
                                    &pane->count,
                                    &pane->free_blocks) != FILE_BROWSER_RC_OK) {
        pane->load_error = 1;
        pane->count = 0;
        pane->selected = 0;
        pane->scroll = 0;
        return;
    }

    if (prefer_name != 0) {
        pane->selected = pane_find_name(pane, prefer_name);
        if (pane->selected == 255u) {
            pane->selected = saved_selected;
        }
    } else if (selected_name[0] != 0) {
        pane->selected = pane_find_name(pane, selected_name);
        if (pane->selected == 255u) {
            pane->selected = saved_selected;
        }
    } else {
        pane->selected = saved_selected;
    }

    pane_clamp(pane);
}

static void reload_matching_drives(unsigned char drive, const char *prefer_name) {
    unsigned char pane_index;

    for (pane_index = 0; pane_index < PANE_COUNT; ++pane_index) {
        if (panes[pane_index].drive == drive) {
            pane_load(pane_index, prefer_name);
        }
    }
}

static void format_size_field(const FileBrowserEntry *entry,
                              char *out,
                              unsigned char width) {
    char digits[6];
    unsigned char pos;
    unsigned int value;

    for (pos = 0; pos < width; ++pos) {
        out[pos] = ' ';
    }
    out[width] = 0;

    if (entry->type == CBM_T_DIR) {
        out[0] = 'D';
        out[1] = 'I';
        out[2] = 'R';
        return;
    }
    if (!file_browser_is_mutable(entry) && !file_browser_is_copyable(entry)) {
        out[0] = '?';
        return;
    }

    value = entry->size;
    if (value > 9999u) {
        out[0] = '+';
        out[1] = '+';
        out[2] = '+';
        out[3] = '+';
        return;
    }

    digits[0] = 0;
    pos = 0;
    do {
        digits[pos++] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u && pos < sizeof(digits));

    while (pos != 0u && width != 0u) {
        out[--width] = digits[--pos];
    }
}

static void draw_header(void) {
    TuiRect box = {0, HEADER_Y, 40, 3};
    char mask_text[16];

    tui_window_title(&box, "SIMPLE FILES", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    format_drive_mask(mask_text);
    tui_puts_n(1, HEADER_Y + 1, "DRIVES:", 7, TUI_COLOR_WHITE);
    tui_puts_n(9, HEADER_Y + 1, mask_text, 14, TUI_COLOR_CYAN);
    tui_puts_n(24, HEADER_Y + 1, "RET:SEQ VIEW", 12, TUI_COLOR_GRAY3);
}

static void draw_pane_title(unsigned char pane_index) {
    FilePane *pane;
    unsigned char x;
    unsigned char w;
    char line[21];
    char count_buf[5];
    unsigned char pos;
    unsigned char color;

    pane = &panes[pane_index];
    x = (pane_index == PANE_LEFT) ? LEFT_X : RIGHT_X;
    w = (pane_index == PANE_LEFT) ? LEFT_W : RIGHT_W;
    color = (pane_index == active_pane) ? TUI_COLOR_CYAN : TUI_COLOR_GRAY3;

    for (pos = 0; pos < w; ++pos) {
        line[pos] = ' ';
    }
    line[w] = 0;

    line[0] = (pane_index == active_pane) ? '>' : ' ';
    line[1] = (pane_index == PANE_LEFT) ? 'L' : 'R';
    line[2] = ' ';
    line[3] = 'D';
    line[4] = ':';
    line[5] = '0' + (pane->drive / 10u);
    line[6] = '0' + (pane->drive % 10u);

    if (!pane->available) {
        memcpy(&line[9], "OFF", 3);
    } else if (pane->load_error) {
        memcpy(&line[9], "ERR", 3);
    } else {
        format_block_value(pane->free_blocks, count_buf);
        line[8] = '#';
        if (pane->count >= 10u) {
            line[9] = '0' + (pane->count / 10u);
            line[10] = '0' + (pane->count % 10u);
        } else {
            line[10] = '0' + pane->count;
        }
        memcpy(&line[w - 5u], count_buf, 4);
        line[w - 1u] = 'F';
    }

    tui_puts_n(x, PANE_TITLE_Y, line, w, color);
}

static void draw_pane_row(unsigned char pane_index, unsigned char row) {
    FilePane *pane;
    unsigned char x;
    unsigned char w;
    unsigned char idx;
    unsigned char color;
    unsigned char i;
    unsigned char name_width;
    char line[21];
    char size_buf[5];
    const FileBrowserEntry *entry;
    unsigned int offset;

    pane = &panes[pane_index];
    x = (pane_index == PANE_LEFT) ? LEFT_X : RIGHT_X;
    w = (pane_index == PANE_LEFT) ? LEFT_W : RIGHT_W;
    idx = (unsigned char)(pane->scroll + row);
    color = TUI_COLOR_WHITE;

    for (i = 0; i < w; ++i) {
        line[i] = ' ';
    }
    line[w] = 0;

    if (idx < pane->count) {
        entry = &pane->entries[idx];
        line[0] = (idx == pane->selected) ? '>' : ' ';
        line[1] = (char)file_browser_type_marker(entry->type);
        line[2] = ' ';
        name_width = (unsigned char)(w - 8u);
        for (i = 0; i < name_width && entry->name[i] != 0; ++i) {
            line[3u + i] = entry->name[i];
        }
        format_size_field(entry, size_buf, 4);
        memcpy(&line[w - 4u], size_buf, 4);
        if (idx == pane->selected) {
            color = (pane_index == active_pane) ? TUI_COLOR_CYAN : TUI_COLOR_LIGHTBLUE;
        }
    }

    tui_puts_n(x, LIST_START_Y + row, line, w, color);

    if (idx < pane->count && idx == pane->selected) {
        offset = (unsigned int)(LIST_START_Y + row) * 40u + x;
        for (i = 0; i < w; ++i) {
            TUI_SCREEN[offset + i] |= 0x80;
        }
    }
}

static void draw_pane(unsigned char pane_index) {
    unsigned char row;

    draw_pane_title(pane_index);
    for (row = 0; row < LIST_HEIGHT; ++row) {
        draw_pane_row(pane_index, row);
    }
}

static void draw_divider(void) {
    unsigned char y;

    for (y = PANE_TITLE_Y; y < LIST_START_Y + LIST_HEIGHT; ++y) {
        tui_putc(DIVIDER_X, y, TUI_VLINE, TUI_COLOR_GRAY2);
    }
}

static void draw_detail(void) {
    FilePane *pane;
    FileBrowserEntry *entry;
    char size_buf[5];

    pane = active_file_pane();
    entry = pane_selected_entry(pane);

    tui_clear_line(DETAIL_Y, 0, 40, TUI_COLOR_WHITE);
    tui_clear_line(INFO_Y, 0, 40, TUI_COLOR_WHITE);

    if (entry == 0) {
        tui_puts_n(0, DETAIL_Y, "NO FILE SELECTED", 16, TUI_COLOR_GRAY3);
        return;
    }

    tui_puts_n(0, DETAIL_Y, "D:", 2, TUI_COLOR_GRAY3);
    tui_print_uint(2, DETAIL_Y, pane->drive, TUI_COLOR_WHITE);
    tui_putc(5, DETAIL_Y, tui_ascii_to_screen(file_browser_type_marker(entry->type)), TUI_COLOR_CYAN);
    tui_puts_n(7, DETAIL_Y, entry->name, 24, TUI_COLOR_WHITE);

    format_size_field(entry, size_buf, 4);
    tui_puts_n(0, INFO_Y, "OPS:", 4, TUI_COLOR_GRAY3);
    tui_puts_n(5, INFO_Y,
               file_browser_is_copyable(entry) ? "CPY " : "--- ",
               4,
               file_browser_is_copyable(entry) ? TUI_COLOR_LIGHTGREEN : TUI_COLOR_GRAY2);
    tui_puts_n(10, INFO_Y,
               file_browser_is_viewable(entry) ? "SEQ VIEW" : "NO VIEW ",
               8,
               file_browser_is_viewable(entry) ? TUI_COLOR_CYAN : TUI_COLOR_GRAY2);
    tui_puts_n(22, INFO_Y, "BLKS:", 5, TUI_COLOR_GRAY3);
    tui_puts_n(28, INFO_Y, size_buf, 4, TUI_COLOR_WHITE);
}

static void draw_status(void) {
    tui_clear_line(STATUS_Y, 0, 40, TUI_COLOR_WHITE);
    tui_puts_n(0, STATUS_Y, status_msg, 39, status_color);
}

static void draw_help(void) {
    tui_clear_line(HELP_Y1, 0, 40, TUI_COLOR_GRAY3);
    tui_clear_line(HELP_Y2, 0, 40, TUI_COLOR_GRAY3);
    tui_puts_n(0, HELP_Y1, "ARROWS MOVE F3:DRV F5:REF F8:HELP", 39, TUI_COLOR_GRAY3);
    tui_puts_n(0, HELP_Y2, "C:COPY N:AS D:DUP S:SWAP ^B:HOME", 39, TUI_COLOR_GRAY3);
}

static void draw_browser(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    draw_pane(PANE_LEFT);
    draw_pane(PANE_RIGHT);
    draw_divider();
    draw_detail();
    draw_status();
    draw_help();
}

static unsigned char sanitize_view_char(unsigned char ch) {
    if (ch >= 32u && ch < 128u) {
        return ch;
    }
    return '.';
}

static void draw_viewer(void) {
    TuiRect box = {0, 0, 40, 23};
    unsigned char row;
    unsigned char col;
    unsigned int src;
    unsigned char ch;
    char title[32];

    tui_clear(TUI_COLOR_BLUE);
    strcpy(title, "SEQ D");
    title[5] = '0' + (viewer_drive / 10u);
    title[6] = '0' + (viewer_drive % 10u);
    title[7] = ':';
    title[8] = 0;
    strncat(title, viewer_entry.name, 20);

    tui_window_title(&box, title, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts_n(1, 1, "BYTE:", 5, TUI_COLOR_GRAY3);
    tui_print_uint(7, 1, viewer_offset, TUI_COLOR_WHITE);
    tui_puts_n(16, 1, "READ:", 5, TUI_COLOR_GRAY3);
    tui_print_uint(22, 1, viewer_len, TUI_COLOR_WHITE);

    for (row = 0; row < VIEW_ROWS; ++row) {
        tui_clear_line((unsigned char)(3u + row), 1, VIEW_COLS, TUI_COLOR_WHITE);
    }

    row = 0;
    col = 0;
    src = 0;
    while (src < viewer_len && row < VIEW_ROWS) {
        ch = viewer_buf[src++];
        if (ch == 13u || ch == 10u) {
            if (ch == 13u && src < viewer_len && viewer_buf[src] == 10u) {
                ++src;
            }
            ++row;
            col = 0;
            continue;
        }
        tui_putc((unsigned char)(1u + col),
                 (unsigned char)(3u + row),
                 tui_ascii_to_screen(sanitize_view_char(ch)),
                 TUI_COLOR_WHITE);
        ++col;
        if (col >= VIEW_COLS) {
            ++row;
            col = 0;
        }
    }

    tui_puts_n(0, 23, "CTRL+P/CTRL+N PAGE STOP BACK F8:HELP", 39, TUI_COLOR_GRAY3);
    tui_puts_n(0, 24, "F2/F4 APPS  CTRL+B HOME", 25, TUI_COLOR_GRAY3);
}

static unsigned char show_confirm(const char *msg) {
    TuiRect win = {9, 9, 22, 6};
    unsigned char key;

    tui_window_title(&win, "CONFIRM", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts_n(11, 11, msg, 18, TUI_COLOR_WHITE);
    tui_puts_n(11, 13, "Y:YES  N:NO", 11, TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == 'y' || key == 'Y') {
            return 1;
        }
        if (key == 'n' || key == 'N' ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0;
        }
    }
}

static unsigned char show_name_dialog(const char *title,
                                      const char *prompt,
                                      char *buffer,
                                      const char *initial_value) {
    TuiRect win = {4, 7, 32, 8};
    TuiInput input;
    unsigned char key;

    tui_window_title(&win, title, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts_n(6, 10, prompt, 24, TUI_COLOR_WHITE);
    tui_input_init(&input, 6, 11, 20, 16, buffer, TUI_COLOR_CYAN);
    if (initial_value != 0) {
        strncpy(buffer, initial_value, 16);
        buffer[16] = 0;
        input.cursor = (unsigned char)strlen(buffer);
    }
    tui_input_draw(&input);
    tui_puts_n(6, 13, "RET:OK  STOP:CANCEL", 19, TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0;
        }
        if (tui_input_key(&input, key)) {
            if (buffer[0] != 0) {
                return 1;
            }
        }
        tui_input_draw(&input);
    }
}

static void browser_switch_active(unsigned char next_active) {
    unsigned char old_active;
    unsigned char row;
    FilePane *pane;

    if (next_active >= PANE_COUNT || next_active == active_pane) {
        return;
    }

    old_active = active_pane;
    active_pane = next_active;
    draw_pane_title(old_active);
    draw_pane_title(active_pane);

    pane = &panes[old_active];
    row = (pane->selected >= pane->scroll) ? (unsigned char)(pane->selected - pane->scroll) : 255u;
    if (row < LIST_HEIGHT) {
        draw_pane_row(old_active, row);
    }

    pane = &panes[active_pane];
    row = (pane->selected >= pane->scroll) ? (unsigned char)(pane->selected - pane->scroll) : 255u;
    if (row < LIST_HEIGHT) {
        draw_pane_row(active_pane, row);
    }

    draw_detail();
}

static void browser_move_vertical(unsigned char move_down) {
    FilePane *pane;
    unsigned char old_selected;
    unsigned char old_scroll;
    unsigned char old_row;
    unsigned char new_row;

    pane = active_file_pane();
    if (pane->count == 0u) {
        return;
    }

    old_selected = pane->selected;
    old_scroll = pane->scroll;
    old_row = (unsigned char)(old_selected - old_scroll);

    if (!move_down) {
        if (pane->selected > 0u) {
            --pane->selected;
        }
    } else if (pane->selected + 1u < pane->count) {
        ++pane->selected;
    }

    pane_clamp(pane);
    if (pane->scroll != old_scroll) {
        draw_pane(active_pane);
    } else if (pane->selected != old_selected) {
        draw_pane_row(active_pane, old_row);
        new_row = (unsigned char)(pane->selected - pane->scroll);
        draw_pane_row(active_pane, new_row);
    }

    draw_detail();
}

static void browser_page_move(unsigned char move_down) {
    FilePane *pane;
    unsigned char jump;

    pane = active_file_pane();
    if (pane->count == 0u) {
        return;
    }

    jump = LIST_HEIGHT - 1u;
    if (!move_down) {
        if (pane->selected > jump) {
            pane->selected = (unsigned char)(pane->selected - jump);
        } else {
            pane->selected = 0;
        }
        if (pane->scroll > jump) {
            pane->scroll = (unsigned char)(pane->scroll - jump);
        } else {
            pane->scroll = 0;
        }
    } else {
        if (pane->selected + jump < pane->count) {
            pane->selected = (unsigned char)(pane->selected + jump);
        } else {
            pane->selected = (unsigned char)(pane->count - 1u);
        }
        if (pane->scroll + jump < pane->count) {
            pane->scroll = (unsigned char)(pane->scroll + jump);
        }
    }
    pane_clamp(pane);
    draw_pane(active_pane);
    draw_detail();
}

static void browser_cycle_drive(void) {
    FilePane *pane;

    probe_drives();
    browser_normalize_visible_drives();
    pane = active_file_pane();
    pane->drive = storage_device_toggle_8_9(pane->drive);
    storage_device_set_default(pane->drive);
    pane_load(active_pane, 0);
    set_status(pane->load_error ? "DRIVE ERROR" : "DRIVE CHANGED",
               pane->load_error ? TUI_COLOR_LIGHTRED : TUI_COLOR_LIGHTGREEN);
    draw_pane(PANE_LEFT);
    draw_pane(PANE_RIGHT);
    draw_detail();
    draw_status();
}

static void browser_swap_drives(void) {
    unsigned char tmp;

    tmp = panes[PANE_LEFT].drive;
    panes[PANE_LEFT].drive = panes[PANE_RIGHT].drive;
    panes[PANE_RIGHT].drive = tmp;
    pane_load(PANE_LEFT, 0);
    pane_load(PANE_RIGHT, 0);
    draw_pane(PANE_LEFT);
    draw_pane(PANE_RIGHT);
    draw_detail();
}

static void browser_refresh_all(void) {
    probe_drives();
    browser_normalize_visible_drives();
    pane_load(PANE_LEFT, 0);
    pane_load(PANE_RIGHT, 0);
    set_status("REFRESHED", TUI_COLOR_LIGHTGREEN);
    draw_browser();
}

static unsigned char browser_target_name(const char *title,
                                         const char *initial_value,
                                         char *out_name) {
    unsigned char ok;

    ok = show_name_dialog(title, "FILENAME:", out_name, initial_value);
    draw_browser();
    return ok;
}

static unsigned char browser_choose_copy_drive(unsigned char src_drive,
                                               unsigned char preferred_drive,
                                               unsigned char *out_drive) {
    TuiRect win = {8, 7, 24, 10};
    TuiMenu menu;
    unsigned char key;
    unsigned char count;
    unsigned char drive;
    unsigned char i;

    count = 0u;
    for (drive = 8; drive <= 9; ++drive) {
        if (!drive_in_mask(discovered_mask, drive) || drive == src_drive) {
            continue;
        }
        drive_menu_values[count] = drive;
        strcpy(drive_menu_items[count], "DRIVE 00");
        drive_menu_items[count][6] = '0' + (drive / 10u);
        drive_menu_items[count][7] = '0' + (drive % 10u);
        drive_menu_ptrs[count] = drive_menu_items[count];
        ++count;
    }

    if (count == 0u) {
        draw_browser();
        set_status("NO TARGET DRIVE", TUI_COLOR_LIGHTRED);
        draw_status();
        return 2u;
    }

    tui_window_title(&win, "COPY TO", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts_n(10, 9, "SELECT DRIVE", 12, TUI_COLOR_WHITE);
    tui_puts_n(9, 15, "RET:OK STOP:CANCEL", 18, TUI_COLOR_GRAY3);

    tui_menu_init(&menu, 10, 10, 12, count, drive_menu_ptrs, count);
    menu.item_color = TUI_COLOR_WHITE;
    menu.sel_color = TUI_COLOR_CYAN;
    for (i = 0u; i < count; ++i) {
        if (drive_menu_values[i] == preferred_drive) {
            menu.selected = i;
            break;
        }
    }
    tui_menu_draw(&menu);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            draw_browser();
            return 0u;
        }
        if (key == TUI_KEY_RETURN) {
            *out_drive = drive_menu_values[menu.selected];
            draw_browser();
            return 1u;
        }
        (void)tui_menu_input(&menu, key);
        tui_menu_draw(&menu);
    }
}

static unsigned char browser_duplicate_via_stage(FilePane *src_pane,
                                                 FilePane *dst_pane,
                                                 FileBrowserEntry *entry,
                                                 const char *dst_name,
                                                 unsigned char *code_out,
                                                 char *msg_out,
                                                 unsigned char msg_cap) {
    static const char *temp_names[] = {
        "sfdup0", "sfdup1", "sfdup2", "sfdup3",
        "sfdup4", "sfdup5", "sfdup6", "sfdup7"
    };
    unsigned char stage_drive;
    const char *temp_name;
    unsigned char i;
    unsigned char rc;

    if (dst_pane == 0 ||
        dst_pane->drive == src_pane->drive ||
        !dst_pane->available ||
        dst_pane->load_error) {
        if (code_out != 0) {
            *code_out = 255u;
        }
        if (msg_out != 0 && msg_cap != 0u) {
            strncpy(msg_out, "SET OTHER DRIVE", (unsigned char)(msg_cap - 1u));
            msg_out[msg_cap - 1u] = 0;
        }
        return FILE_BROWSER_RC_UNSUPPORTED;
    }

    stage_drive = dst_pane->drive;
    temp_name = 0;
    for (i = 0u; i < (unsigned char)(sizeof(temp_names) / sizeof(temp_names[0])); ++i) {
        if (strcmp(temp_names[i], dst_name) != 0 &&
            pane_find_name(dst_pane, temp_names[i]) == 255u) {
            temp_name = temp_names[i];
            break;
        }
    }
    if (temp_name == 0) {
        if (code_out != 0) {
            *code_out = 255u;
        }
        if (msg_out != 0 && msg_cap != 0u) {
            strncpy(msg_out, "NO TEMP NAME", (unsigned char)(msg_cap - 1u));
            msg_out[msg_cap - 1u] = 0;
        }
        return FILE_BROWSER_RC_IO;
    }

    (void)file_browser_scratch(stage_drive, temp_name, code_out, msg_out, msg_cap);
    rc = file_browser_copy_stream(src_pane->drive,
                                  entry,
                                  stage_drive,
                                  temp_name,
                                  copy_buf,
                                  sizeof(copy_buf),
                                  code_out,
                                  msg_out,
                                  msg_cap);
    if (rc != FILE_BROWSER_RC_OK) {
        return rc;
    }

    reload_matching_drives(stage_drive, temp_name);
    if (!browser_verify_copy_result(entry, stage_drive, temp_name)) {
        if (code_out != 0) {
            *code_out = 255u;
        }
        if (msg_out != 0 && msg_cap != 0u) {
            strncpy(msg_out, "STAGE VERIFY", (unsigned char)(msg_cap - 1u));
            msg_out[msg_cap - 1u] = 0;
        }
        (void)file_browser_scratch(stage_drive, temp_name, code_out, msg_out, msg_cap);
        reload_matching_drives(stage_drive, 0);
        return FILE_BROWSER_RC_IO;
    }

    strcpy(stage_entry.name, temp_name);
    stage_entry.size = entry->size;
    stage_entry.type = entry->type;
    stage_entry.access = 0u;

    rc = file_browser_copy_stream(stage_drive,
                                  &stage_entry,
                                  src_pane->drive,
                                  dst_name,
                                  copy_buf,
                                  sizeof(copy_buf),
                                  code_out,
                                  msg_out,
                                  msg_cap);
    reload_matching_drives(src_pane->drive, dst_name);
    reload_matching_drives(stage_drive, temp_name);
    if (rc != FILE_BROWSER_RC_OK) {
        (void)file_browser_scratch(stage_drive, temp_name, code_out, msg_out, msg_cap);
        reload_matching_drives(stage_drive, 0);
        return rc;
    }

    (void)file_browser_scratch(stage_drive, temp_name, code_out, msg_out, msg_cap);
    reload_matching_drives(stage_drive, 0);
    return FILE_BROWSER_RC_OK;
}

static unsigned char browser_name_exists_on_loaded_drive(unsigned char drive,
                                                         const char *name,
                                                         FilePane *src_pane,
                                                         FilePane *dst_pane,
                                                         const char *src_name) {
    if (src_pane->drive == drive &&
        pane_find_name(src_pane, name) != 255u &&
        strcmp(src_name, name) != 0) {
        return 1u;
    }
    if (dst_pane->drive == drive && pane_find_name(dst_pane, name) != 255u) {
        return 1u;
    }
    return 0u;
}

static FileBrowserEntry *browser_find_loaded_entry(unsigned char drive,
                                                   const char *name) {
    unsigned char pane_index;
    unsigned char selected_index;

    for (pane_index = 0; pane_index < PANE_COUNT; ++pane_index) {
        if (panes[pane_index].drive != drive) {
            continue;
        }
        selected_index = pane_find_name(&panes[pane_index], name);
        if (selected_index != 255u) {
            return &panes[pane_index].entries[selected_index];
        }
    }

    return 0;
}

static unsigned char browser_verify_copy_result(const FileBrowserEntry *src_entry,
                                                unsigned char dst_drive,
                                                const char *dst_name) {
    FileBrowserEntry *dst_entry;

    dst_entry = browser_find_loaded_entry(dst_drive, dst_name);
    if (dst_entry == 0) {
        return 0u;
    }

    return (unsigned char)(dst_entry->type == src_entry->type &&
                           dst_entry->size == src_entry->size);
}

static void browser_set_copy_status(unsigned char same_drive,
                                    unsigned char src_drive,
                                    const char *src_name,
                                    unsigned char dst_drive,
                                    const char *dst_name) {
    unsigned char pos;
    unsigned char i;
    const char *verb;

    if (same_drive) {
        verb = "DUP ";
    } else {
        verb = "CPY ";
    }

    pos = 0u;
    while (verb[pos] != 0) {
        status_msg[pos] = verb[pos];
        ++pos;
    }

    status_msg[pos++] = '0' + (src_drive / 10u);
    status_msg[pos++] = '0' + (src_drive % 10u);
    status_msg[pos++] = ':';
    for (i = 0u; i < 8u && src_name[i] != 0; ++i) {
        status_msg[pos++] = src_name[i];
    }
    status_msg[pos++] = ' ';
    status_msg[pos++] = '>';
    status_msg[pos++] = ' ';
    status_msg[pos++] = '0' + (dst_drive / 10u);
    status_msg[pos++] = '0' + (dst_drive % 10u);
    status_msg[pos++] = ':';
    for (i = 0u; i < 8u && dst_name[i] != 0 && pos + 1u < sizeof(status_msg); ++i) {
        status_msg[pos++] = dst_name[i];
    }
    status_msg[pos] = 0;
    status_color = TUI_COLOR_YELLOW;
}

static void browser_delete_entry(void) {
    FilePane *pane;
    FileBrowserEntry *entry;
    unsigned char code;
    char msg[24];

    pane = active_file_pane();
    entry = pane_selected_entry(pane);
    if (entry == 0u) {
        return;
    }
    if (!file_browser_is_mutable(entry)) {
        set_status("ENTRY LOCKED OUT", TUI_COLOR_LIGHTRED);
        draw_status();
        return;
    }

    if (!show_confirm("SCRATCH FILE?")) {
        draw_browser();
        set_status("CANCELLED", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }

    if (file_browser_scratch(pane->drive, entry->name, &code, msg, sizeof(msg)) == FILE_BROWSER_RC_OK) {
        reload_matching_drives(pane->drive, 0);
        set_status("SCRATCHED", TUI_COLOR_LIGHTGREEN);
        draw_browser();
        return;
    }

    draw_browser();
    set_status(msg, TUI_COLOR_LIGHTRED);
    draw_status();
}

static void browser_rename_entry(void) {
    FilePane *pane;
    FileBrowserEntry *entry;
    char new_name[17];
    unsigned char code;
    char msg[24];

    pane = active_file_pane();
    entry = pane_selected_entry(pane);
    if (entry == 0u) {
        return;
    }
    if (!file_browser_is_mutable(entry)) {
        set_status("ENTRY LOCKED OUT", TUI_COLOR_LIGHTRED);
        draw_status();
        return;
    }
    if (!browser_target_name("RENAME", entry->name, new_name)) {
        set_status("CANCELLED", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }
    if (strcmp(entry->name, new_name) == 0) {
        set_status("UNCHANGED", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }
    if (pane_find_name(pane, new_name) != 255u) {
        set_status("NAME EXISTS", TUI_COLOR_LIGHTRED);
        draw_status();
        return;
    }

    if (file_browser_rename(pane->drive, entry->name, new_name, &code, msg, sizeof(msg)) == FILE_BROWSER_RC_OK) {
        reload_matching_drives(pane->drive, new_name);
        set_status("RENAMED", TUI_COLOR_LIGHTGREEN);
        draw_browser();
        return;
    }

    draw_browser();
    set_status(msg, TUI_COLOR_LIGHTRED);
    draw_status();
}

static void browser_copy_entry(unsigned char prompt_name,
                               unsigned char force_same_drive) {
    FilePane *src_pane;
    FilePane *dst_pane;
    FileBrowserEntry *entry;
    char dst_name[17];
    unsigned char dst_drive;
    unsigned char code;
    char msg[24];
    unsigned char exists;
    unsigned char copy_rc;
    unsigned char overwrite_done;

    src_pane = active_file_pane();
    dst_pane = passive_file_pane();
    entry = pane_selected_entry(src_pane);
    if (entry == 0u) {
        return;
    }
    if (!file_browser_is_copyable(entry)) {
        set_status("COPY UNSUPPORTED", TUI_COLOR_LIGHTRED);
        draw_status();
        return;
    }

    strcpy(dst_name, entry->name);
    if (force_same_drive) {
        dst_drive = src_pane->drive;
        prompt_name = 1;
    } else {
        exists = browser_choose_copy_drive(src_pane->drive, dst_pane->drive, &dst_drive);
        if (exists == 0u) {
            set_status("CANCELLED", TUI_COLOR_GRAY3);
            draw_status();
            return;
        }
        if (exists == 2u) {
            return;
        }
    }

    if (prompt_name && !browser_target_name(force_same_drive ? "DUPLICATE" : "COPY AS",
                                            entry->name,
                                            dst_name)) {
        set_status("CANCELLED", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }

    if (src_pane->drive == dst_drive && strcmp(entry->name, dst_name) == 0) {
        set_status("NEED NEW NAME", TUI_COLOR_LIGHTRED);
        draw_status();
        return;
    }

    exists = browser_name_exists_on_loaded_drive(dst_drive,
                                                 dst_name,
                                                 src_pane,
                                                 dst_pane,
                                                 entry->name);
    overwrite_done = 0u;

    while (1) {
        if (exists && !overwrite_done) {
            if (!show_confirm("OVERWRITE FILE?")) {
                draw_browser();
                set_status("CANCELLED", TUI_COLOR_GRAY3);
                draw_status();
                return;
            }
            draw_browser();
            if (file_browser_scratch(dst_drive, dst_name, &code, msg, sizeof(msg)) != FILE_BROWSER_RC_OK) {
                set_status(msg, TUI_COLOR_LIGHTRED);
                draw_status();
                return;
            }
            overwrite_done = 1u;
        }

        browser_set_copy_status((unsigned char)(src_pane->drive == dst_drive),
                                src_pane->drive,
                                entry->name,
                                dst_drive,
                                dst_name);
        draw_status();

        if (src_pane->drive == dst_drive) {
            copy_rc = browser_duplicate_via_stage(src_pane,
                                                  dst_pane,
                                                  entry,
                                                  dst_name,
                                                  &code,
                                                  msg,
                                                  sizeof(msg));
        } else {
            copy_rc = file_browser_copy_stream(src_pane->drive,
                                               entry,
                                               dst_drive,
                                               dst_name,
                                               copy_buf,
                                               sizeof(copy_buf),
                                               &code,
                                               msg,
                                               sizeof(msg));
        }

        if (copy_rc == FILE_BROWSER_RC_EXISTS && !overwrite_done) {
            exists = 1u;
            continue;
        }
        break;
    }

    if (copy_rc == FILE_BROWSER_RC_OK) {
        reload_matching_drives(dst_drive, dst_name);
        if (src_pane->drive != dst_drive) {
            reload_matching_drives(src_pane->drive, entry->name);
        }
        if (!browser_verify_copy_result(entry, dst_drive, dst_name)) {
            if (src_pane->drive == dst_drive) {
                (void)file_browser_scratch(dst_drive, dst_name, &code, msg, sizeof(msg));
                reload_matching_drives(dst_drive, entry->name);
                set_status("DUP VERIFY", TUI_COLOR_LIGHTRED);
            } else {
                (void)file_browser_scratch(dst_drive, dst_name, &code, msg, sizeof(msg));
                reload_matching_drives(dst_drive, 0);
                set_status("COPY VERIFY", TUI_COLOR_LIGHTRED);
            }
            draw_browser();
            draw_status();
            return;
        }
        draw_browser();
        set_status(force_same_drive ? "DUPLICATED" : "COPIED", TUI_COLOR_LIGHTGREEN);
        draw_status();
        return;
    }

    draw_browser();
    set_status(msg, TUI_COLOR_LIGHTRED);
    draw_status();
}

static void browser_open_entry(void) {
    FilePane *pane;
    FileBrowserEntry *entry;

    pane = active_file_pane();
    entry = pane_selected_entry(pane);
    if (entry == 0u) {
        return;
    }
    if (!file_browser_is_viewable(entry)) {
        set_status("SEQ VIEW ONLY", TUI_COLOR_LIGHTRED);
        draw_status();
        return;
    }
    enter_viewer(pane, entry);
    draw_viewer();
}

static unsigned char viewer_load_page(void) {
    unsigned char rc;

    rc = file_browser_read_page(viewer_drive,
                                &viewer_entry,
                                viewer_offset,
                                viewer_buf,
                                sizeof(viewer_buf),
                                &viewer_len);
    if (rc != FILE_BROWSER_RC_OK) {
        set_status("VIEW READ FAIL", TUI_COLOR_LIGHTRED);
        return 0;
    }
    return 1;
}

static void viewer_page_down(void) {
    unsigned int old_offset;
    unsigned int old_len;

    if (viewer_len < VIEW_PAGE_BYTES) {
        set_status("END OF FILE", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }

    old_offset = viewer_offset;
    old_len = viewer_len;
    viewer_offset = (unsigned int)(viewer_offset + VIEW_PAGE_BYTES);
    if (!viewer_load_page() || viewer_len == 0u) {
        viewer_offset = old_offset;
        viewer_len = old_len;
        viewer_load_page();
        set_status("END OF FILE", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }
    draw_viewer();
}

static void viewer_page_up(void) {
    if (viewer_offset == 0u) {
        set_status("AT TOP", TUI_COLOR_GRAY3);
        draw_status();
        return;
    }

    if (viewer_offset > VIEW_PAGE_BYTES) {
        viewer_offset = (unsigned int)(viewer_offset - VIEW_PAGE_BYTES);
    } else {
        viewer_offset = 0u;
    }

    if (viewer_load_page()) {
        draw_viewer();
    }
}

static void enter_viewer(FilePane *pane, const FileBrowserEntry *entry) {
    viewer_drive = pane->drive;
    viewer_entry = *entry;
    viewer_offset = 0u;
    app_mode = MODE_VIEWER;
    if (!viewer_load_page()) {
        app_mode = MODE_BROWSER;
        draw_browser();
    }
}

static void leave_viewer(void) {
    app_mode = MODE_BROWSER;
    draw_browser();
}

static void show_help_popup(void) {
    TuiRect win = {1, 4, 38, 16};
    unsigned char key;

    if (app_mode == MODE_VIEWER) {
        tui_window_title(&win, "SIMPLE FILES HELP", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
        tui_puts(3, 6, "SEQ VIEWER", TUI_COLOR_WHITE);
        tui_puts(3, 7, "RET ON SEQ:OPEN VIEW", TUI_COLOR_GRAY3);
        tui_puts(3, 8, "CTRL+N:NEXT PAGE", TUI_COLOR_GRAY3);
        tui_puts(3, 9, "CTRL+P:PREV PAGE", TUI_COLOR_GRAY3);
        tui_puts(3, 10, "STOP/LEFT:BACK TO PANES", TUI_COLOR_GRAY3);
        tui_puts(3, 11, "ONLY CURRENT PAGE IN RAM", TUI_COLOR_GRAY3);
        tui_puts(3, 12, "F2/F4:APPS CTRL+B:LAUNCHER", TUI_COLOR_GRAY3);
        tui_puts(3, 13, "F8:HELP", TUI_COLOR_GRAY3);
        tui_puts(3, 17, "RET/F8/STOP:CLOSE", TUI_COLOR_CYAN);
    } else {
        tui_window_title(&win, "SIMPLE FILES HELP", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
        tui_puts(3, 6, "READYOS SIMPLE FILES", TUI_COLOR_WHITE);
        tui_puts(3, 7, "LT/RT:ACTIVE PANE  UP/DN:MOVE", TUI_COLOR_GRAY3);
        tui_puts(3, 8, "CTRL+N/CTRL+P:PAGE", TUI_COLOR_GRAY3);
        tui_puts(3, 9, "RET:VIEW SEQ  F3:TOGGLE 8/9", TUI_COLOR_GRAY3);
        tui_puts(3, 10, "C:COPY TO DRIVE", TUI_COLOR_GRAY3);
        tui_puts(3, 11, "N:COPY AS  D:DUP VIA OTHER DRIVE", TUI_COLOR_GRAY3);
        tui_puts(3, 12, "R:RENAME  DEL:SCRATCH", TUI_COLOR_GRAY3);
        tui_puts(3, 13, "S:SWAP PANES  F5:REFRESH", TUI_COLOR_GRAY3);
        tui_puts(3, 14, "F2/F4:APPS CTRL+B:LAUNCHER", TUI_COLOR_GRAY3);
        tui_puts(3, 17, "RET/F8/STOP:CLOSE", TUI_COLOR_CYAN);
    }

    while (1) {
        key = tui_getkey();
        if (key == KEY_HELP || key == TUI_KEY_RETURN ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            break;
        }
    }

    if (app_mode == MODE_VIEWER) {
        draw_viewer();
    } else {
        draw_browser();
    }
}

static void resume_save_state(void) {
    unsigned char pane_index;

    if (!resume_ready) {
        return;
    }

    for (pane_index = 0; pane_index < PANE_COUNT; ++pane_index) {
        resume_blob.pane_drive[pane_index] = panes[pane_index].drive;
        resume_blob.pane_selected[pane_index] = panes[pane_index].selected;
        resume_blob.pane_scroll[pane_index] = panes[pane_index].scroll;
    }
    resume_blob.active_pane = active_pane;
    resume_blob.mode = app_mode;
    resume_blob.viewer_drive = viewer_drive;
    resume_blob.viewer_type = viewer_entry.type;
    resume_blob.discovered_mask = discovered_mask;
    resume_blob.viewer_offset = viewer_offset;
    strncpy(resume_blob.viewer_name, viewer_entry.name, FILE_BROWSER_NAME_LEN - 1u);
    resume_blob.viewer_name[FILE_BROWSER_NAME_LEN - 1u] = 0;

    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static unsigned char restore_resume_state(void) {
    unsigned int payload_len;
    unsigned char pane_index;
    unsigned char selected_index;
    unsigned char left_drive;
    unsigned char right_drive;
    unsigned char fallback_drive;
    if (!resume_ready) {
        return 0;
    }
    if (!resume_try_load(&resume_blob, sizeof(resume_blob), &payload_len)) {
        return 0;
    }
    if (payload_len != sizeof(resume_blob)) {
        return 0;
    }

    probe_drives();
    active_pane = (resume_blob.active_pane < PANE_COUNT) ? resume_blob.active_pane : PANE_LEFT;
    fallback_drive = storage_device_get_default();
    left_drive = normalize_drive_8_9(resume_blob.pane_drive[PANE_LEFT]);
    right_drive = normalize_drive_8_9(resume_blob.pane_drive[PANE_RIGHT]);

    if (left_drive == right_drive) {
        if (active_pane == PANE_LEFT) {
            right_drive = storage_device_toggle_8_9(left_drive);
        } else {
            left_drive = storage_device_toggle_8_9(right_drive);
        }
    }

    panes[PANE_LEFT].drive = left_drive;
    panes[PANE_RIGHT].drive = right_drive;
    browser_normalize_visible_drives();
    for (pane_index = 0; pane_index < PANE_COUNT; ++pane_index) {
        panes[pane_index].selected = resume_blob.pane_selected[pane_index];
        panes[pane_index].scroll = resume_blob.pane_scroll[pane_index];
        pane_load(pane_index, 0);
        pane_clamp(&panes[pane_index]);
    }

    app_mode = MODE_BROWSER;
    viewer_entry.name[0] = 0;
    viewer_entry.type = resume_blob.viewer_type;
    viewer_drive = normalize_drive_8_9(resume_blob.viewer_drive);
    viewer_offset = resume_blob.viewer_offset;

    if (resume_blob.mode == MODE_VIEWER && resume_blob.viewer_name[0] != 0 &&
        resume_blob.viewer_type == CBM_T_SEQ) {
        for (pane_index = 0; pane_index < PANE_COUNT; ++pane_index) {
            if (panes[pane_index].drive != viewer_drive) {
                continue;
            }
            selected_index = pane_find_name(&panes[pane_index], resume_blob.viewer_name);
            if (selected_index != 255u) {
                viewer_entry = panes[pane_index].entries[selected_index];
                app_mode = MODE_VIEWER;
                if (!viewer_load_page()) {
                    app_mode = MODE_BROWSER;
                }
                break;
            }
        }
    }

    return 1;
}

static void init_state(void) {
    unsigned char pane_index;

    tui_init();
    running = 1;
    app_mode = MODE_BROWSER;
    active_pane = PANE_LEFT;
    viewer_entry.name[0] = 0;
    viewer_entry.type = CBM_T_SEQ;
    viewer_drive = 8;
    viewer_offset = 0;
    viewer_len = 0;
    set_status("READY", TUI_COLOR_LIGHTGREEN);

    for (pane_index = 0; pane_index < PANE_COUNT; ++pane_index) {
        panes[pane_index].drive = (unsigned char)(8u + pane_index);
        panes[pane_index].available = 0;
        panes[pane_index].load_error = 0;
        panes[pane_index].count = 0;
        panes[pane_index].selected = 0;
        panes[pane_index].scroll = 0;
        panes[pane_index].free_blocks = 0;
    }

    probe_drives();
    seed_default_drives();
    browser_normalize_visible_drives();
    pane_load(PANE_LEFT, 0);
    pane_load(PANE_RIGHT, 0);
}

static void handle_browser_key(unsigned char key) {
    switch (key) {
        case TUI_KEY_LEFT:
            browser_switch_active(PANE_LEFT);
            break;
        case TUI_KEY_RIGHT:
            browser_switch_active(PANE_RIGHT);
            break;
        case TUI_KEY_UP:
            browser_move_vertical(0);
            break;
        case TUI_KEY_DOWN:
            browser_move_vertical(1);
            break;
        case TUI_KEY_HOME:
            active_file_pane()->selected = 0;
            active_file_pane()->scroll = 0;
            draw_pane(active_pane);
            draw_detail();
            break;
        case KEY_CTRL_N:
            browser_page_move(1);
            break;
        case KEY_CTRL_P:
            browser_page_move(0);
            break;
        case TUI_KEY_RETURN:
            browser_open_entry();
            break;
        case TUI_KEY_DEL:
            browser_delete_entry();
            break;
        case TUI_KEY_F3:
            browser_cycle_drive();
            break;
        case TUI_KEY_F5:
            browser_refresh_all();
            break;
        case KEY_HELP:
            show_help_popup();
            break;
        default:
            switch (key) {
                case 'c':
                case 'C':
                    browser_copy_entry(0, 0);
                    break;
                case 'n':
                case 'N':
                    browser_copy_entry(1, 0);
                    break;
                case 'd':
                case 'D':
                    browser_copy_entry(1, 1);
                    break;
                case 'r':
                case 'R':
                    browser_rename_entry();
                    break;
                case 's':
                case 'S':
                    browser_swap_drives();
                    break;
                default:
                    break;
            }
            break;
    }
}

static void handle_viewer_key(unsigned char key) {
    switch (key) {
        case KEY_CTRL_N:
            viewer_page_down();
            break;
        case KEY_CTRL_P:
            viewer_page_up();
            break;
        case KEY_HELP:
            show_help_popup();
            break;
        case TUI_KEY_RUNSTOP:
        case TUI_KEY_LARROW:
            leave_viewer();
            break;
        default:
            break;
    }
}

int main(void) {
    unsigned char bank;
    unsigned char key;
    unsigned char next;
    unsigned char prev;

    init_state();
    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1u && bank <= 15u) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)restore_resume_state();

    if (app_mode == MODE_VIEWER) {
        draw_viewer();
    } else {
        draw_browser();
    }

    while (running) {
        key = tui_getkey();

        if (key == KEY_CTRL_B) {
            resume_save_state();
            tui_return_to_launcher();
        }
        if (key == TUI_KEY_NEXT_APP) {
            next = tui_get_next_app(SHIM_CURRENT_BANK);
            if (next != 0u) {
                resume_save_state();
                tui_switch_to_app(next);
            }
            continue;
        }
        if (key == TUI_KEY_PREV_APP) {
            prev = tui_get_prev_app(SHIM_CURRENT_BANK);
            if (prev != 0u) {
                resume_save_state();
                tui_switch_to_app(prev);
            }
            continue;
        }

        if (app_mode == MODE_VIEWER) {
            handle_viewer_key(key);
        } else {
            handle_browser_key(key);
        }
    }

    return 0;
}
