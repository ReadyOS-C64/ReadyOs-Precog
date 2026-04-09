/*
 * quicknotes.c - Ready OS Quicknotes
 *
 * Split-pane note editor:
 * - left pane lists note titles
 * - right pane edits the active note
 * - note bodies live in REU, only the active note is mirrored in C64 RAM
 */

#include "../../lib/tui.h"
#include <c64.h>
#include <cbm.h>
#include <cbm_filetype.h>
#include <conio.h>
#include <string.h>

#include "../../lib/clipboard.h"
#include "../../lib/file_dialog.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"
#include "../../lib/storage_device.h"

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define HEADER_Y      0
#define EDIT_START_Y  2
#define EDIT_HEIGHT   20
#define STATUS_Y      23
#define HELP_Y        24

#define LIST_X        0
#define LIST_WIDTH    10
#define DIVIDER_X     10
#define EDIT_X        11
#define EDIT_WIDTH    29

#define PANE_LIST     0
#define PANE_EDIT     1

#define MAX_NOTES        50
#define MAX_NOTE_LINES   50
#define MAX_LINE_LEN     30
#define NOTE_TITLE_LEN   20
#define TITLE_VISIBLE    9
#define NOTE_BODY_SIZE   (MAX_NOTE_LINES * MAX_LINE_LEN)
#define NOTES_PER_BANK   25
#define NOTE_BANK_COUNT  2

#define MAX_DIR_ENTRIES  18
#define IO_BUF_SIZE      64
#define FIND_LEN         20
#define CLIP_TEXT_BUF_SIZE ((MAX_NOTE_LINES * MAX_LINE_LEN) + MAX_NOTE_LINES)

#define KEY_COPY    TUI_KEY_F1
#define KEY_PASTE   TUI_KEY_F3
#define KEY_SAVE    TUI_KEY_F5
#define KEY_SELECT  TUI_KEY_F6
#define KEY_OPEN    TUI_KEY_F7
#define KEY_HELP    TUI_KEY_F8

#define KEY_CTRL_A  1
#define KEY_CTRL_B  2
#define KEY_CTRL_D  4
#define KEY_CTRL_E  5
#define KEY_CTRL_F  6
#define KEY_CTRL_G  7
#define KEY_CTRL_K  11
#define KEY_CTRL_L  12
#define KEY_CTRL_N  14
#define KEY_CTRL_O  15
#define KEY_CTRL_P  16
#define KEY_CTRL_R  18
#define KEY_CTRL_U  21
#define KEY_CTRL_W  23

#define LFN_DIR      1
#define LFN_FILE     2
#define LFN_CMD      15
#define CR           0x0D

#define FILE_MAGIC0  'Q'
#define FILE_MAGIC1  'N'
#define FILE_MAGIC2  'T'
#define FILE_MAGIC3  'S'
#define FILE_VERSION 1u
#define FILE_HDR_LEN 8u

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Static state
 *---------------------------------------------------------------------------*/

static char current_note_lines[MAX_NOTE_LINES][MAX_LINE_LEN];
static char swap_note_lines[MAX_NOTE_LINES][MAX_LINE_LEN];
static char note_titles[MAX_NOTES][NOTE_TITLE_LEN + 1];
static unsigned char note_line_counts[MAX_NOTES];
static unsigned char note_cursor_x[MAX_NOTES];
static unsigned char note_cursor_y[MAX_NOTES];
static unsigned char note_scroll_y[MAX_NOTES];

static unsigned char note_count;
static unsigned char current_note_idx;
static unsigned char current_line_count;
static unsigned char cursor_x;
static unsigned char cursor_y;
static unsigned char scroll_y;
static unsigned char list_scroll;
static unsigned char focus_pane;
static unsigned char running;
static unsigned char modified;
static unsigned char current_dirty;
static unsigned char has_selection;
static unsigned char sel_anchor_x;
static unsigned char sel_anchor_y;
static unsigned char resume_ready;

static unsigned char note_banks[NOTE_BANK_COUNT];

static char filename[16];
static union {
    char clip_text_buf[CLIP_TEXT_BUF_SIZE + 1];
    FileDialogState dialog;
    char save_buf[17];
} file_scratch;
static char title_buf[NOTE_TITLE_LEN + 1];
static char find_buf[FIND_LEN + 1];
static unsigned char file_header[FILE_HDR_LEN];
static unsigned char title_raw[NOTE_TITLE_LEN];

static ResumeWriteSegment quicknotes_resume_write_segments[] = {
    { current_note_lines, sizeof(current_note_lines) },
    { note_titles, sizeof(note_titles) },
    { note_line_counts, sizeof(note_line_counts) },
    { note_cursor_x, sizeof(note_cursor_x) },
    { note_cursor_y, sizeof(note_cursor_y) },
    { note_scroll_y, sizeof(note_scroll_y) },
    { &note_count, sizeof(note_count) },
    { &current_note_idx, sizeof(current_note_idx) },
    { &current_line_count, sizeof(current_line_count) },
    { &cursor_x, sizeof(cursor_x) },
    { &cursor_y, sizeof(cursor_y) },
    { &scroll_y, sizeof(scroll_y) },
    { &list_scroll, sizeof(list_scroll) },
    { &focus_pane, sizeof(focus_pane) },
    { &modified, sizeof(modified) },
    { &current_dirty, sizeof(current_dirty) },
    { &has_selection, sizeof(has_selection) },
    { &sel_anchor_x, sizeof(sel_anchor_x) },
    { &sel_anchor_y, sizeof(sel_anchor_y) },
    { filename, sizeof(filename) },
    { find_buf, sizeof(find_buf) },
    { note_banks, sizeof(note_banks) },
};

static ResumeReadSegment quicknotes_resume_read_segments[] = {
    { current_note_lines, sizeof(current_note_lines) },
    { note_titles, sizeof(note_titles) },
    { note_line_counts, sizeof(note_line_counts) },
    { note_cursor_x, sizeof(note_cursor_x) },
    { note_cursor_y, sizeof(note_cursor_y) },
    { note_scroll_y, sizeof(note_scroll_y) },
    { &note_count, sizeof(note_count) },
    { &current_note_idx, sizeof(current_note_idx) },
    { &current_line_count, sizeof(current_line_count) },
    { &cursor_x, sizeof(cursor_x) },
    { &cursor_y, sizeof(cursor_y) },
    { &scroll_y, sizeof(scroll_y) },
    { &list_scroll, sizeof(list_scroll) },
    { &focus_pane, sizeof(focus_pane) },
    { &modified, sizeof(modified) },
    { &current_dirty, sizeof(current_dirty) },
    { &has_selection, sizeof(has_selection) },
    { &sel_anchor_x, sizeof(sel_anchor_x) },
    { &sel_anchor_y, sizeof(sel_anchor_y) },
    { filename, sizeof(filename) },
    { find_buf, sizeof(find_buf) },
    { note_banks, sizeof(note_banks) },
};

#define QUICKNOTES_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(quicknotes_resume_read_segments) / sizeof(quicknotes_resume_read_segments[0])))

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/

static void quicknotes_init(void);
static void quicknotes_draw(void);
static void quicknotes_loop(void);
static void draw_header_static(void);
static void draw_header_dynamic(void);
static void draw_list(void);
static void draw_divider(void);
static void draw_editor_text(void);
static void draw_editor_line(unsigned char line_idx);
static void draw_status(void);
static void draw_help(void);
static void draw_cursor(void);
static void undraw_cursor(void);
static void quicknotes_refresh(void);
static unsigned char line_length(unsigned char line_idx);
static unsigned char max_scroll_y(void);
static unsigned char list_max_scroll(void);
static void clamp_view_state(void);
static void ensure_current_note_visible(void);
static void clear_selection(void);
static void begin_selection(void);
static unsigned char selection_has_text(void);
static void selection_bounds(unsigned char *start_y, unsigned char *start_x,
                             unsigned char *end_y, unsigned char *end_x);
static unsigned char selection_line_span_for(unsigned char line_idx,
                                             unsigned char cursor_x_pos,
                                             unsigned char cursor_y_pos,
                                             unsigned char anchor_x,
                                             unsigned char anchor_y,
                                             unsigned char *start_col,
                                             unsigned char *end_col);
static unsigned char selection_line_span(unsigned char line_idx,
                                         unsigned char *start_col,
                                         unsigned char *end_col);
static void selection_visual_range_for(unsigned char cursor_x_pos,
                                       unsigned char cursor_y_pos,
                                       unsigned char *start_y,
                                       unsigned char *end_y);
static void redraw_selection_transition(unsigned char old_cursor_x,
                                        unsigned char old_cursor_y);
static void move_page_down(void);
static void move_page_up(void);
static void list_page_down(void);
static void list_page_up(void);
static void handle_char(unsigned char ch);
static void handle_return(void);
static unsigned char handle_delete(void);
static void handle_cursor(unsigned char key);
static void handle_cursor_selection(unsigned char key);
static void copy_to_clipboard(void);
static void paste_from_clipboard(void);
static unsigned char show_find_dialog(void);
static unsigned char find_next_match(void);
static unsigned char show_open_dialog(DirPageEntry *out_entry);
static unsigned char show_save_dialog(void);
static unsigned char show_title_dialog(void);
static unsigned char show_confirm(const char *msg);
static void show_message(const char *msg, unsigned char color);
static void show_help_popup(void);
static unsigned char file_load(const char *name);
static unsigned char file_save(const char *name);
static void resume_save_state(void);
static unsigned char resume_restore_state(void);
static unsigned char handle_global_nav(unsigned char key);

static unsigned char allocate_note_banks(void);
static void free_note_banks(void);
static unsigned char validate_note_banks(void);
static void prepare_fresh_storage(void);
static void init_blank_document(void);
static void set_default_title(unsigned char idx, char *dst);
static unsigned char note_bank_for_index(unsigned char idx);
static unsigned int note_offset_for_index(unsigned char idx);
static void fetch_note_to_buffer(unsigned char idx, void *dst);
static void stash_buffer_to_note(unsigned char idx, const void *src);
static void save_current_note_to_slot(void);
static void load_current_note_from_slot(void);
static void switch_to_note(unsigned char idx);
static void create_note_after_current(void);
static void rename_current_note(void);
static void delete_current_note(void);
static void move_current_note_up(void);
static void move_current_note_down(void);

/*---------------------------------------------------------------------------
 * Utilities
 *---------------------------------------------------------------------------*/

static void u8_to_ascii(unsigned char value, char *out) {
    char rev[4];
    unsigned char count;
    unsigned char i;

    if (value == 0u) {
        out[0] = '0';
        out[1] = 0;
        return;
    }

    count = 0u;
    while (value > 0u && count < 3u) {
        rev[count] = (char)('0' + (value % 10u));
        value = (unsigned char)(value / 10u);
        ++count;
    }

    for (i = 0u; i < count; ++i) {
        out[i] = rev[count - 1u - i];
    }
    out[count] = 0;
}

static unsigned char to_lower(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (unsigned char)(ch + 32);
    }
    return ch;
}

static unsigned char line_length(unsigned char line_idx) {
    return (unsigned char)strlen(current_note_lines[line_idx]);
}

static unsigned char max_scroll_y(void) {
    if (current_line_count > EDIT_HEIGHT) {
        return (unsigned char)(current_line_count - EDIT_HEIGHT);
    }
    return 0u;
}

static unsigned char list_max_scroll(void) {
    if (note_count > EDIT_HEIGHT) {
        return (unsigned char)(note_count - EDIT_HEIGHT);
    }
    return 0u;
}

static void clamp_view_state(void) {
    unsigned char max_scroll;

    if (current_line_count == 0u) {
        current_line_count = 1u;
    }
    if (current_line_count > MAX_NOTE_LINES) {
        current_line_count = MAX_NOTE_LINES;
    }
    if (cursor_y >= current_line_count) {
        cursor_y = (unsigned char)(current_line_count - 1u);
    }
    if (cursor_x >= MAX_LINE_LEN - 1u) {
        cursor_x = (unsigned char)(MAX_LINE_LEN - 2u);
    }
    if (cursor_x > line_length(cursor_y)) {
        cursor_x = line_length(cursor_y);
    }

    max_scroll = max_scroll_y();
    if (scroll_y > max_scroll) {
        scroll_y = max_scroll;
    }
    if (cursor_y < scroll_y) {
        scroll_y = cursor_y;
    } else if (cursor_y >= scroll_y + EDIT_HEIGHT) {
        scroll_y = (unsigned char)(cursor_y - EDIT_HEIGHT + 1u);
        if (scroll_y > max_scroll) {
            scroll_y = max_scroll;
        }
    }

    if (note_count == 0u) {
        note_count = 1u;
    }
    if (note_count > MAX_NOTES) {
        note_count = MAX_NOTES;
    }
    if (current_note_idx >= note_count) {
        current_note_idx = (unsigned char)(note_count - 1u);
    }
    if (focus_pane > PANE_EDIT) {
        focus_pane = PANE_EDIT;
    }
    if (list_scroll > list_max_scroll()) {
        list_scroll = list_max_scroll();
    }
    ensure_current_note_visible();
}

static void ensure_current_note_visible(void) {
    unsigned char max_scroll;

    max_scroll = list_max_scroll();
    if (current_note_idx < list_scroll) {
        list_scroll = current_note_idx;
    } else if (current_note_idx >= list_scroll + EDIT_HEIGHT) {
        list_scroll = (unsigned char)(current_note_idx - EDIT_HEIGHT + 1u);
        if (list_scroll > max_scroll) {
            list_scroll = max_scroll;
        }
    }
}

static void clear_selection(void) {
    has_selection = 0u;
    sel_anchor_x = cursor_x;
    sel_anchor_y = cursor_y;
}

static void begin_selection(void) {
    sel_anchor_x = cursor_x;
    sel_anchor_y = cursor_y;
    has_selection = 1u;
}

static unsigned char selection_has_text(void) {
    if (!has_selection) {
        return 0u;
    }
    if (sel_anchor_y != cursor_y) {
        return 1u;
    }
    return (unsigned char)(sel_anchor_x != cursor_x);
}

static void selection_bounds(unsigned char *start_y, unsigned char *start_x,
                             unsigned char *end_y, unsigned char *end_x) {
    if (sel_anchor_y < cursor_y ||
        (sel_anchor_y == cursor_y && sel_anchor_x <= cursor_x)) {
        *start_y = sel_anchor_y;
        *start_x = sel_anchor_x;
        *end_y = cursor_y;
        *end_x = cursor_x;
    } else {
        *start_y = cursor_y;
        *start_x = cursor_x;
        *end_y = sel_anchor_y;
        *end_x = sel_anchor_x;
    }
}

static unsigned char selection_line_span_for(unsigned char line_idx,
                                             unsigned char cursor_x_pos,
                                             unsigned char cursor_y_pos,
                                             unsigned char anchor_x,
                                             unsigned char anchor_y,
                                             unsigned char *start_col,
                                             unsigned char *end_col) {
    unsigned char start_y;
    unsigned char start_x;
    unsigned char end_y;
    unsigned char end_x;
    unsigned char len;

    if (anchor_x == cursor_x_pos && anchor_y == cursor_y_pos) {
        return 0u;
    }

    if (anchor_y < cursor_y_pos ||
        (anchor_y == cursor_y_pos && anchor_x <= cursor_x_pos)) {
        start_y = anchor_y;
        start_x = anchor_x;
        end_y = cursor_y_pos;
        end_x = cursor_x_pos;
    } else {
        start_y = cursor_y_pos;
        start_x = cursor_x_pos;
        end_y = anchor_y;
        end_x = anchor_x;
    }

    if (line_idx < start_y || line_idx > end_y) {
        return 0u;
    }

    if (start_y == end_y) {
        *start_col = start_x;
        *end_col = end_x;
        return (unsigned char)(*end_col > *start_col);
    }

    len = line_length(line_idx);
    if (line_idx == start_y) {
        *start_col = start_x;
        *end_col = len;
        return (unsigned char)(*end_col > *start_col);
    }

    if (line_idx == end_y) {
        *start_col = 0u;
        *end_col = end_x;
        return (unsigned char)(*end_col > *start_col);
    }

    *start_col = 0u;
    *end_col = len;
    return (unsigned char)(*end_col > *start_col);
}

static unsigned char selection_line_span(unsigned char line_idx,
                                         unsigned char *start_col,
                                         unsigned char *end_col) {
    if (!has_selection) {
        return 0u;
    }
    return selection_line_span_for(line_idx, cursor_x, cursor_y,
                                   sel_anchor_x, sel_anchor_y,
                                   start_col, end_col);
}

static void selection_visual_range_for(unsigned char cursor_x_pos,
                                       unsigned char cursor_y_pos,
                                       unsigned char *start_y,
                                       unsigned char *end_y) {
    if (sel_anchor_x == cursor_x_pos && sel_anchor_y == cursor_y_pos) {
        *start_y = cursor_y_pos;
        *end_y = cursor_y_pos;
    } else if (sel_anchor_y < cursor_y_pos ||
               (sel_anchor_y == cursor_y_pos && sel_anchor_x <= cursor_x_pos)) {
        *start_y = sel_anchor_y;
        *end_y = cursor_y_pos;
    } else {
        *start_y = cursor_y_pos;
        *end_y = sel_anchor_y;
    }
}

static void redraw_selection_transition(unsigned char old_cursor_x,
                                        unsigned char old_cursor_y) {
    unsigned char old_start_y;
    unsigned char old_end_y;
    unsigned char new_start_y;
    unsigned char new_end_y;
    unsigned char redraw_start;
    unsigned char redraw_end;
    unsigned char idx;

    selection_visual_range_for(old_cursor_x, old_cursor_y, &old_start_y, &old_end_y);
    selection_visual_range_for(cursor_x, cursor_y, &new_start_y, &new_end_y);
    redraw_start = (old_start_y < new_start_y) ? old_start_y : new_start_y;
    redraw_end = (old_end_y > new_end_y) ? old_end_y : new_end_y;

    if (redraw_end < redraw_start) {
        return;
    }

    for (idx = redraw_start; idx <= redraw_end; ++idx) {
        draw_editor_line(idx);
        if (idx == redraw_end) {
            break;
        }
    }
}

static void clear_note_buffer(void *dst) {
    memset(dst, 0, NOTE_BODY_SIZE);
}

static void set_default_title(unsigned char idx, char *dst) {
    char num_buf[4];

    strcpy(dst, "NOTE ");
    u8_to_ascii((unsigned char)(idx + 1u), num_buf);
    strcat(dst, num_buf);
}

static unsigned char note_bank_for_index(unsigned char idx) {
    return note_banks[idx / NOTES_PER_BANK];
}

static unsigned int note_offset_for_index(unsigned char idx) {
    return (unsigned int)(idx % NOTES_PER_BANK) * (unsigned int)NOTE_BODY_SIZE;
}

static void fetch_note_to_buffer(unsigned char idx, void *dst) {
    reu_dma_fetch((unsigned int)dst,
                  note_bank_for_index(idx),
                  note_offset_for_index(idx),
                  NOTE_BODY_SIZE);
}

static void stash_buffer_to_note(unsigned char idx, const void *src) {
    reu_dma_stash((unsigned int)src,
                  note_bank_for_index(idx),
                  note_offset_for_index(idx),
                  NOTE_BODY_SIZE);
}

static unsigned char allocate_note_banks(void) {
    unsigned char i;

    for (i = 0u; i < NOTE_BANK_COUNT; ++i) {
        note_banks[i] = 0xFFu;
    }

    for (i = 0u; i < NOTE_BANK_COUNT; ++i) {
        note_banks[i] = reu_alloc_bank(REU_APP_ALLOC);
        if (note_banks[i] == 0xFFu) {
            free_note_banks();
            return 0u;
        }
    }

    return 1u;
}

static void free_note_banks(void) {
    unsigned char i;

    for (i = 0u; i < NOTE_BANK_COUNT; ++i) {
        if (note_banks[i] != 0xFFu) {
            reu_free_bank(note_banks[i]);
            note_banks[i] = 0xFFu;
        }
    }
}

static unsigned char validate_note_banks(void) {
    unsigned char i;

    for (i = 0u; i < NOTE_BANK_COUNT; ++i) {
        if (note_banks[i] == 0xFFu) {
            return 0u;
        }
        if (reu_bank_type(note_banks[i]) != REU_APP_ALLOC) {
            return 0u;
        }
    }
    return 1u;
}

static void prepare_fresh_storage(void) {
    unsigned char idx;

    clear_note_buffer(swap_note_lines);
    for (idx = 0u; idx < MAX_NOTES; ++idx) {
        stash_buffer_to_note(idx, swap_note_lines);
    }
}

static void init_blank_document(void) {
    unsigned char i;

    memset(note_titles, 0, sizeof(note_titles));
    memset(note_line_counts, 0, sizeof(note_line_counts));
    memset(note_cursor_x, 0, sizeof(note_cursor_x));
    memset(note_cursor_y, 0, sizeof(note_cursor_y));
    memset(note_scroll_y, 0, sizeof(note_scroll_y));
    clear_note_buffer(current_note_lines);
    clear_note_buffer(swap_note_lines);
    prepare_fresh_storage();

    note_count = 1u;
    current_note_idx = 0u;
    current_line_count = 1u;
    cursor_x = 0u;
    cursor_y = 0u;
    scroll_y = 0u;
    list_scroll = 0u;
    focus_pane = PANE_EDIT;
    modified = 0u;
    current_dirty = 0u;
    clear_selection();
    find_buf[0] = 0;

    set_default_title(0u, note_titles[0]);
    note_line_counts[0] = 1u;
    stash_buffer_to_note(0u, current_note_lines);

    for (i = 1u; i < MAX_NOTES; ++i) {
        note_titles[i][0] = 0;
    }

    strcpy(filename, "UNTITLED");
}

static void save_current_note_to_slot(void) {
    if (current_note_idx >= note_count) {
        return;
    }

    if (current_line_count == 0u) {
        current_line_count = 1u;
    }
    note_line_counts[current_note_idx] = current_line_count;
    note_cursor_x[current_note_idx] = cursor_x;
    note_cursor_y[current_note_idx] = cursor_y;
    note_scroll_y[current_note_idx] = scroll_y;
    stash_buffer_to_note(current_note_idx, current_note_lines);
    current_dirty = 0u;
}

static void load_current_note_from_slot(void) {
    if (current_note_idx >= note_count) {
        current_note_idx = 0u;
    }

    fetch_note_to_buffer(current_note_idx, current_note_lines);
    current_line_count = note_line_counts[current_note_idx];
    if (current_line_count == 0u || current_line_count > MAX_NOTE_LINES) {
        current_line_count = 1u;
    }

    cursor_x = note_cursor_x[current_note_idx];
    cursor_y = note_cursor_y[current_note_idx];
    scroll_y = note_scroll_y[current_note_idx];
    clear_selection();
    clamp_view_state();
    current_dirty = 0u;
}

static void switch_to_note(unsigned char idx) {
    if (idx >= note_count || idx == current_note_idx) {
        return;
    }

    save_current_note_to_slot();
    current_note_idx = idx;
    load_current_note_from_slot();
    ensure_current_note_visible();
}

/*---------------------------------------------------------------------------
 * Drawing
 *---------------------------------------------------------------------------*/

static void draw_header_static(void) {
    TuiRect header;

    header.x = 0u;
    header.y = HEADER_Y;
    header.w = 40u;
    header.h = 2u;

    tui_window(&header, TUI_COLOR_LIGHTBLUE);
}

static void draw_header_dynamic(void) {
    unsigned char filename_len;
    unsigned char title_end;
    unsigned int current_num;

    tui_hline(1u, HEADER_Y, 38u, TUI_COLOR_LIGHTBLUE);
    tui_putc(0u, HEADER_Y, TUI_CORNER_TL, TUI_COLOR_LIGHTBLUE);
    tui_putc(39u, HEADER_Y, TUI_CORNER_TR, TUI_COLOR_LIGHTBLUE);
    tui_puts(1u, HEADER_Y, "QUICKNOTES:", TUI_COLOR_WHITE);
    filename_len = 0u;
    while (filename[filename_len] != 0 && filename_len < 18u) {
        tui_putc((unsigned char)(12u + filename_len), HEADER_Y,
                 tui_ascii_to_screen(filename[filename_len]), TUI_COLOR_YELLOW);
        ++filename_len;
    }
    title_end = (unsigned char)(12u + filename_len);
    if (title_end > 30u) {
        title_end = 30u;
    }
    if (title_end < 31u) {
        tui_hline(title_end, HEADER_Y, (unsigned char)(31u - title_end), TUI_COLOR_LIGHTBLUE);
    }

    tui_puts(31u, HEADER_Y, "N:", TUI_COLOR_WHITE);
    current_num = (unsigned int)current_note_idx + 1u;
    tui_print_uint(33u, HEADER_Y, current_num, TUI_COLOR_CYAN);
    if (current_num >= 10u) {
        tui_puts(35u, HEADER_Y, "/", TUI_COLOR_WHITE);
        tui_print_uint(36u, HEADER_Y, note_count, TUI_COLOR_CYAN);
    } else {
        tui_puts(34u, HEADER_Y, "/", TUI_COLOR_WHITE);
        tui_print_uint(35u, HEADER_Y, note_count, TUI_COLOR_CYAN);
    }
}

static void draw_divider(void) {
    unsigned char y;

    for (y = EDIT_START_Y; y < EDIT_START_Y + EDIT_HEIGHT; ++y) {
        tui_putc(DIVIDER_X, y, TUI_VLINE, TUI_COLOR_GRAY2);
    }
}

static void draw_list(void) {
    unsigned char row;
    unsigned char idx;
    unsigned char y;
    unsigned char color;

    for (row = 0u; row < EDIT_HEIGHT; ++row) {
        y = (unsigned char)(EDIT_START_Y + row);
        tui_clear_line(y, LIST_X, LIST_WIDTH, TUI_COLOR_BLUE);
        idx = (unsigned char)(list_scroll + row);
        if (idx >= note_count) {
            continue;
        }

        if (idx == current_note_idx) {
            color = (focus_pane == PANE_LIST) ? TUI_COLOR_CYAN : TUI_COLOR_YELLOW;
            tui_putc(0, y, tui_ascii_to_screen('>'), color);
        } else {
            color = TUI_COLOR_WHITE;
            tui_putc(0, y, 32, TUI_COLOR_WHITE);
        }

        tui_puts_n(1, y, note_titles[idx], TITLE_VISIBLE, color);
    }
}

static void draw_editor_line(unsigned char line_idx) {
    unsigned char screen_y;
    unsigned char len;
    unsigned char col;
    unsigned int text_offset;
    unsigned char ch;
    unsigned char highlight_start;
    unsigned char highlight_end;
    unsigned char has_highlight;

    if (line_idx < scroll_y || line_idx >= scroll_y + EDIT_HEIGHT) {
        return;
    }

    screen_y = (unsigned char)(EDIT_START_Y + (line_idx - scroll_y));
    tui_clear_line(screen_y, EDIT_X, EDIT_WIDTH, TUI_COLOR_WHITE);

    if (line_idx >= current_line_count) {
        return;
    }

    len = line_length(line_idx);
    if (len > EDIT_WIDTH) {
        len = EDIT_WIDTH;
    }

    has_highlight = selection_line_span(line_idx, &highlight_start, &highlight_end);
    if (highlight_start > EDIT_WIDTH) {
        highlight_start = EDIT_WIDTH;
    }
    if (highlight_end > EDIT_WIDTH) {
        highlight_end = EDIT_WIDTH;
    }

    text_offset = (unsigned int)screen_y * 40u + EDIT_X;
    for (col = 0u; col < len; ++col) {
        ch = tui_ascii_to_screen((unsigned char)current_note_lines[line_idx][col]);
        if (has_highlight && col >= highlight_start && col < highlight_end) {
            ch |= 0x80u;
        }
        TUI_SCREEN[text_offset + col] = ch;
        TUI_COLOR_RAM[text_offset + col] = TUI_COLOR_WHITE;
    }
}

static void draw_editor_text(void) {
    unsigned char idx;

    for (idx = scroll_y; idx < scroll_y + EDIT_HEIGHT; ++idx) {
        draw_editor_line(idx);
    }
}

static void draw_status(void) {
    tui_clear_line(STATUS_Y, 0, 40, TUI_COLOR_WHITE);

    if (modified) {
        tui_puts(0, STATUS_Y, "*MOD*", TUI_COLOR_LIGHTRED);
    }

    tui_puts(6, STATUS_Y, (focus_pane == PANE_LIST) ? "LIST" : "EDIT", TUI_COLOR_CYAN);
    tui_puts(11, STATUS_Y, "N:", TUI_COLOR_GRAY3);
    tui_print_uint(13, STATUS_Y, (unsigned int)current_note_idx + 1u, TUI_COLOR_WHITE);
    tui_puts(15, STATUS_Y, "/", TUI_COLOR_GRAY3);
    tui_print_uint(16, STATUS_Y, note_count, TUI_COLOR_WHITE);

    tui_puts(20, STATUS_Y, "L:", TUI_COLOR_GRAY3);
    tui_print_uint(22, STATUS_Y, (unsigned int)cursor_y + 1u, TUI_COLOR_WHITE);
    tui_puts(24, STATUS_Y, "/", TUI_COLOR_GRAY3);
    tui_print_uint(25, STATUS_Y, current_line_count, TUI_COLOR_WHITE);

    tui_puts(29, STATUS_Y, "C:", TUI_COLOR_GRAY3);
    tui_print_uint(31, STATUS_Y, (unsigned int)cursor_x + 1u, TUI_COLOR_WHITE);

    tui_puts(35, STATUS_Y, "D:", TUI_COLOR_GRAY3);
    tui_print_uint(37, STATUS_Y, storage_device_get_default(), TUI_COLOR_CYAN);
}

static void draw_help(void) {
    tui_clear_line(HELP_Y, 0, 40, TUI_COLOR_GRAY3);
    if (focus_pane == PANE_LIST) {
        tui_puts(0, HELP_Y, "RET/^L/^O:PANE ^W:+ ^R:TTL ^D:DEL", TUI_COLOR_GRAY3);
    } else {
        tui_puts(0, HELP_Y, "^L/^O:PANE F1:CPY F3:PST F5:SAV", TUI_COLOR_GRAY3);
    }
}

static void undraw_cursor(void) {
    unsigned char screen_x;
    unsigned char screen_y;
    unsigned int offset;
    unsigned char highlight_start;
    unsigned char highlight_end;

    if (focus_pane != PANE_EDIT) {
        return;
    }

    screen_x = (unsigned char)(EDIT_X + cursor_x);
    screen_y = (unsigned char)(EDIT_START_Y + (cursor_y - scroll_y));
    if (screen_x >= 40u) {
        screen_x = 39u;
    }
    if (screen_y < EDIT_START_Y || screen_y >= EDIT_START_Y + EDIT_HEIGHT) {
        return;
    }

    offset = (unsigned int)screen_y * 40u + screen_x;
    TUI_SCREEN[offset] &= 0x7Fu;
    if (selection_line_span(cursor_y, &highlight_start, &highlight_end) &&
        cursor_x >= highlight_start && cursor_x < highlight_end) {
        TUI_SCREEN[offset] |= 0x80u;
    }
}

static void draw_cursor(void) {
    unsigned char screen_x;
    unsigned char screen_y;
    unsigned int offset;

    if (focus_pane != PANE_EDIT) {
        return;
    }

    screen_x = (unsigned char)(EDIT_X + cursor_x);
    screen_y = (unsigned char)(EDIT_START_Y + (cursor_y - scroll_y));
    if (screen_x >= 40u) {
        screen_x = 39u;
    }
    if (screen_y >= EDIT_START_Y + EDIT_HEIGHT) {
        return;
    }

    offset = (unsigned int)screen_y * 40u + screen_x;
    TUI_SCREEN[offset] |= 0x80u;
}

static void quicknotes_draw(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header_static();
    draw_header_dynamic();
    draw_list();
    draw_divider();
    draw_editor_text();
    draw_status();
    draw_help();
    draw_cursor();
}

static void quicknotes_refresh(void) {
    draw_header_dynamic();
    draw_list();
    draw_status();
    draw_help();
    draw_cursor();
}

/*---------------------------------------------------------------------------
 * Note paging / editing
 *---------------------------------------------------------------------------*/

static void move_page_down(void) {
    unsigned char jump;
    unsigned char max_scroll;

    if (current_line_count <= 1u) {
        return;
    }

    jump = (unsigned char)(EDIT_HEIGHT - 1u);
    max_scroll = max_scroll_y();

    if (cursor_y + jump >= current_line_count) {
        cursor_y = (unsigned char)(current_line_count - 1u);
    } else {
        cursor_y = (unsigned char)(cursor_y + jump);
    }

    if (scroll_y + jump >= max_scroll) {
        scroll_y = max_scroll;
    } else {
        scroll_y = (unsigned char)(scroll_y + jump);
    }

    clamp_view_state();
}

static void move_page_up(void) {
    unsigned char jump;

    jump = (unsigned char)(EDIT_HEIGHT - 1u);
    if (cursor_y > jump) {
        cursor_y = (unsigned char)(cursor_y - jump);
    } else {
        cursor_y = 0u;
    }

    if (scroll_y > jump) {
        scroll_y = (unsigned char)(scroll_y - jump);
    } else {
        scroll_y = 0u;
    }

    clamp_view_state();
}

static void list_page_down(void) {
    unsigned char jump;
    unsigned char idx;

    jump = (unsigned char)(EDIT_HEIGHT - 1u);
    idx = current_note_idx;
    if (idx + jump >= note_count) {
        idx = (unsigned char)(note_count - 1u);
    } else {
        idx = (unsigned char)(idx + jump);
    }
    switch_to_note(idx);
}

static void list_page_up(void) {
    unsigned char jump;
    unsigned char idx;

    jump = (unsigned char)(EDIT_HEIGHT - 1u);
    idx = current_note_idx;
    if (idx > jump) {
        idx = (unsigned char)(idx - jump);
    } else {
        idx = 0u;
    }
    switch_to_note(idx);
}

static void handle_char(unsigned char ch) {
    unsigned char len;
    unsigned char i;

    len = (unsigned char)strlen(current_note_lines[cursor_y]);
    if (len >= MAX_LINE_LEN - 1u) {
        return;
    }

    for (i = (unsigned char)(len + 1u); i > cursor_x; --i) {
        current_note_lines[cursor_y][i] = current_note_lines[cursor_y][i - 1u];
    }

    current_note_lines[cursor_y][cursor_x] = (char)ch;
    ++cursor_x;
    modified = 1u;
    current_dirty = 1u;
}

static void handle_return(void) {
    unsigned char i;
    unsigned char rest_len;

    if (current_line_count >= MAX_NOTE_LINES) {
        return;
    }

    for (i = current_line_count; i > cursor_y + 1u; --i) {
        strcpy(current_note_lines[i], current_note_lines[i - 1u]);
    }

    rest_len = (unsigned char)(strlen(current_note_lines[cursor_y]) - cursor_x);
    if (rest_len > 0u) {
        strcpy(current_note_lines[cursor_y + 1u], &current_note_lines[cursor_y][cursor_x]);
    } else {
        current_note_lines[cursor_y + 1u][0] = 0;
    }

    current_note_lines[cursor_y][cursor_x] = 0;
    ++cursor_y;
    ++current_line_count;
    cursor_x = 0u;
    modified = 1u;
    current_dirty = 1u;
    clamp_view_state();
}

static unsigned char handle_delete(void) {
    unsigned char len;
    unsigned char prev_len;
    unsigned char i;

    if (cursor_x > 0u) {
        len = (unsigned char)strlen(current_note_lines[cursor_y]);
        for (i = (unsigned char)(cursor_x - 1u); i < len; ++i) {
            current_note_lines[cursor_y][i] = current_note_lines[cursor_y][i + 1u];
        }
        --cursor_x;
        modified = 1u;
        current_dirty = 1u;
        return 0u;
    }

    if (cursor_y > 0u) {
        prev_len = (unsigned char)strlen(current_note_lines[cursor_y - 1u]);
        if (prev_len + strlen(current_note_lines[cursor_y]) < MAX_LINE_LEN) {
            strcat(current_note_lines[cursor_y - 1u], current_note_lines[cursor_y]);
            for (i = cursor_y; i < current_line_count - 1u; ++i) {
                strcpy(current_note_lines[i], current_note_lines[i + 1u]);
            }
            current_note_lines[current_line_count - 1u][0] = 0;
            --current_line_count;
            --cursor_y;
            cursor_x = prev_len;
            modified = 1u;
            current_dirty = 1u;
            clamp_view_state();
            return 1u;
        }
    }

    return 0u;
}

static void handle_cursor(unsigned char key) {
    unsigned char len;

    switch (key) {
        case TUI_KEY_UP:
            if (cursor_y > 0u) {
                --cursor_y;
                len = line_length(cursor_y);
                if (cursor_x > len) {
                    cursor_x = len;
                }
                clamp_view_state();
            }
            break;

        case TUI_KEY_DOWN:
            if (cursor_y < current_line_count - 1u) {
                ++cursor_y;
                len = line_length(cursor_y);
                if (cursor_x > len) {
                    cursor_x = len;
                }
                clamp_view_state();
            }
            break;

        case TUI_KEY_LEFT:
            if (cursor_x > 0u) {
                --cursor_x;
            } else if (cursor_y > 0u) {
                --cursor_y;
                cursor_x = line_length(cursor_y);
                clamp_view_state();
            }
            break;

        case TUI_KEY_RIGHT:
            len = line_length(cursor_y);
            if (cursor_x < len) {
                ++cursor_x;
            } else if (cursor_y < current_line_count - 1u) {
                ++cursor_y;
                cursor_x = 0u;
                clamp_view_state();
            }
            break;

        case TUI_KEY_HOME:
            cursor_x = 0u;
            break;
    }
}

static void handle_cursor_selection(unsigned char key) {
    unsigned char len;
    unsigned char min_y;
    unsigned char max_y;

    min_y = scroll_y;
    max_y = (unsigned char)(scroll_y + EDIT_HEIGHT - 1u);
    if (max_y >= current_line_count) {
        max_y = (unsigned char)(current_line_count - 1u);
    }

    switch (key) {
        case TUI_KEY_UP:
            if (cursor_y > min_y) {
                --cursor_y;
                len = line_length(cursor_y);
                if (cursor_x > len) {
                    cursor_x = len;
                }
            }
            break;

        case TUI_KEY_DOWN:
            if (cursor_y < max_y) {
                ++cursor_y;
                len = line_length(cursor_y);
                if (cursor_x > len) {
                    cursor_x = len;
                }
            }
            break;

        case TUI_KEY_LEFT:
            if (cursor_x > 0u) {
                --cursor_x;
            } else if (cursor_y > min_y) {
                --cursor_y;
                cursor_x = line_length(cursor_y);
            }
            break;

        case TUI_KEY_RIGHT:
            len = line_length(cursor_y);
            if (cursor_x < len) {
                ++cursor_x;
            } else if (cursor_y < max_y) {
                ++cursor_y;
                cursor_x = 0u;
            }
            break;

        case TUI_KEY_HOME:
            cursor_x = 0u;
            break;
    }
}

static unsigned char find_in_line_from(unsigned char line_idx,
                                       unsigned char start_col,
                                       const char *needle) {
    unsigned char text_len;
    unsigned char needle_len;
    unsigned char col;
    unsigned char ni;

    text_len = line_length(line_idx);
    needle_len = (unsigned char)strlen(needle);

    if (needle_len == 0u || start_col > text_len || needle_len > text_len) {
        return 255u;
    }

    for (col = start_col; col + needle_len <= text_len; ++col) {
        for (ni = 0u; ni < needle_len; ++ni) {
            if (to_lower((unsigned char)current_note_lines[line_idx][col + ni]) !=
                to_lower((unsigned char)needle[ni])) {
                break;
            }
        }
        if (ni == needle_len) {
            return col;
        }
    }

    return 255u;
}

static unsigned char find_next_match(void) {
    unsigned char line_idx;
    unsigned char start_col;
    unsigned char match_col;

    if (find_buf[0] == 0) {
        return 0u;
    }

    line_idx = cursor_y;
    start_col = (unsigned char)(cursor_x + 1u);
    if (start_col > line_length(cursor_y)) {
        start_col = line_length(cursor_y);
    }

    while (line_idx < current_line_count) {
        match_col = find_in_line_from(line_idx, start_col, find_buf);
        if (match_col != 255u) {
            cursor_y = line_idx;
            cursor_x = match_col;
            clamp_view_state();
            return 1u;
        }
        ++line_idx;
        start_col = 0u;
    }

    line_idx = 0u;
    while (line_idx <= cursor_y && line_idx < current_line_count) {
        match_col = find_in_line_from(line_idx, 0u, find_buf);
        if (match_col != 255u) {
            if (line_idx == cursor_y && match_col <= cursor_x) {
                ++line_idx;
                continue;
            }
            cursor_y = line_idx;
            cursor_x = match_col;
            clamp_view_state();
            return 1u;
        }
        ++line_idx;
    }

    return 0u;
}

/*---------------------------------------------------------------------------
 * Clipboard
 *---------------------------------------------------------------------------*/

static void copy_to_clipboard(void) {
    unsigned char len;
    unsigned int out_len;
    unsigned char start_y;
    unsigned char start_x;
    unsigned char end_y;
    unsigned char end_x;
    unsigned char line_idx;
    unsigned char from_col;
    unsigned char to_col;
    unsigned char col;

    if (selection_has_text()) {
        out_len = 0u;
        selection_bounds(&start_y, &start_x, &end_y, &end_x);

        for (line_idx = start_y; line_idx <= end_y; ++line_idx) {
            from_col = 0u;
            to_col = line_length(line_idx);

            if (line_idx == start_y) {
                from_col = start_x;
            }
            if (line_idx == end_y) {
                to_col = end_x;
            }

            for (col = from_col; col < to_col && out_len < CLIP_TEXT_BUF_SIZE; ++col) {
                file_scratch.clip_text_buf[out_len] = current_note_lines[line_idx][col];
                ++out_len;
            }

            if (line_idx != end_y && out_len < CLIP_TEXT_BUF_SIZE) {
                file_scratch.clip_text_buf[out_len] = CR;
                ++out_len;
            }

            if (line_idx == end_y) {
                break;
            }
        }

        if (out_len > 0u) {
            clip_copy(CLIP_TYPE_TEXT, file_scratch.clip_text_buf, out_len);
        }
        return;
    }

    len = line_length(cursor_y);
    if (len > 0u) {
        clip_copy(CLIP_TYPE_TEXT, current_note_lines[cursor_y], len);
    }
}

static void paste_from_clipboard(void) {
    unsigned int len;
    unsigned int i;

    if (clip_item_count() == 0u) {
        return;
    }

    len = clip_paste(0u, file_scratch.clip_text_buf, CLIP_TEXT_BUF_SIZE);
    if (len == 0u) {
        return;
    }

    file_scratch.clip_text_buf[len] = 0;
    clear_selection();

    for (i = 0u; i < len; ++i) {
        if ((unsigned char)file_scratch.clip_text_buf[i] == CR) {
            if (current_line_count >= MAX_NOTE_LINES) {
                break;
            }
            handle_return();
        } else if ((unsigned char)file_scratch.clip_text_buf[i] >= 32u &&
                   (unsigned char)file_scratch.clip_text_buf[i] < 128u) {
            handle_char((unsigned char)file_scratch.clip_text_buf[i]);
        }
    }
}

/*---------------------------------------------------------------------------
 * File I/O helpers
 *---------------------------------------------------------------------------*/

static unsigned char file_write_exact(unsigned char lfn, const void *data, unsigned int len) {
    const unsigned char *ptr;
    unsigned int total;
    int nw;

    ptr = (const unsigned char*)data;
    total = 0u;
    while (total < len) {
        nw = cbm_write(lfn, ptr + total, len - total);
        if (nw <= 0) {
            return 0u;
        }
        total += (unsigned int)nw;
    }
    return 1u;
}

static unsigned char file_read_exact(unsigned char lfn, void *data, unsigned int len) {
    unsigned char *ptr;
    unsigned int total;
    int nr;

    ptr = (unsigned char*)data;
    total = 0u;
    while (total < len) {
        nr = cbm_read(lfn, ptr + total, len - total);
        if (nr <= 0) {
            return 0u;
        }
        total += (unsigned int)nr;
    }
    return 1u;
}

static unsigned char file_load(const char *name) {
    static char open_str[24];
    unsigned char idx;
    unsigned char line_idx;
    unsigned char line_len;

    strcpy(open_str, name);
    strcat(open_str, ",s,r");

    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return 1u;
    }

    if (!file_read_exact(LFN_FILE, file_header, FILE_HDR_LEN)) {
        cbm_close(LFN_FILE);
        return 1u;
    }

    if (file_header[0] != FILE_MAGIC0 || file_header[1] != FILE_MAGIC1 ||
        file_header[2] != FILE_MAGIC2 || file_header[3] != FILE_MAGIC3 ||
        file_header[4] != FILE_VERSION) {
        cbm_close(LFN_FILE);
        return 1u;
    }

    if (file_header[5] == 0u || file_header[5] > MAX_NOTES ||
        file_header[6] >= file_header[5]) {
        cbm_close(LFN_FILE);
        return 1u;
    }

    if (!validate_note_banks()) {
        free_note_banks();
        if (!allocate_note_banks()) {
            cbm_close(LFN_FILE);
            return 1u;
        }
    }

    memset(note_titles, 0, sizeof(note_titles));
    memset(note_line_counts, 0, sizeof(note_line_counts));
    memset(note_cursor_x, 0, sizeof(note_cursor_x));
    memset(note_cursor_y, 0, sizeof(note_cursor_y));
    memset(note_scroll_y, 0, sizeof(note_scroll_y));
    clear_note_buffer(current_note_lines);
    clear_note_buffer(swap_note_lines);
    prepare_fresh_storage();

    note_count = file_header[5];
    current_note_idx = file_header[6];
    list_scroll = 0u;
    focus_pane = PANE_EDIT;
    clear_selection();

    for (idx = 0u; idx < note_count; ++idx) {
        clear_note_buffer(current_note_lines);

        if (!file_read_exact(LFN_FILE, title_raw, NOTE_TITLE_LEN)) {
            cbm_close(LFN_FILE);
            return 1u;
        }
        memcpy(note_titles[idx], title_raw, NOTE_TITLE_LEN);
        note_titles[idx][NOTE_TITLE_LEN] = 0;
        if (note_titles[idx][0] == 0) {
            set_default_title(idx, note_titles[idx]);
        }

        if (!file_read_exact(LFN_FILE, &line_len, 1u)) {
            cbm_close(LFN_FILE);
            return 1u;
        }
        if (line_len == 0u || line_len > MAX_NOTE_LINES) {
            cbm_close(LFN_FILE);
            return 1u;
        }
        note_line_counts[idx] = line_len;

        for (line_idx = 0u; line_idx < line_len; ++line_idx) {
            if (!file_read_exact(LFN_FILE, &title_raw[0], 1u)) {
                cbm_close(LFN_FILE);
                return 1u;
            }
            if (title_raw[0] >= MAX_LINE_LEN) {
                cbm_close(LFN_FILE);
                return 1u;
            }
            if (title_raw[0] > 0u) {
                if (!file_read_exact(LFN_FILE, current_note_lines[line_idx], title_raw[0])) {
                    cbm_close(LFN_FILE);
                    return 1u;
                }
            }
            current_note_lines[line_idx][title_raw[0]] = 0;
        }

        stash_buffer_to_note(idx, current_note_lines);
    }

    cbm_close(LFN_FILE);

    load_current_note_from_slot();
    strncpy(filename, name, 15);
    filename[15] = 0;
    modified = 0u;
    current_dirty = 0u;
    return 0u;
}

static unsigned char file_save(const char *name) {
    static char cmd_str[24];
    static char open_str[24];
    unsigned char idx;
    unsigned char line_idx;
    unsigned char line_len;

    save_current_note_to_slot();

    strcpy(cmd_str, "s:");
    strcat(cmd_str, name);
    cbm_open(LFN_CMD, storage_device_get_default(), 15, cmd_str);
    cbm_close(LFN_CMD);

    strcpy(open_str, name);
    strcat(open_str, ",s,w");
    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return 1u;
    }

    file_header[0] = FILE_MAGIC0;
    file_header[1] = FILE_MAGIC1;
    file_header[2] = FILE_MAGIC2;
    file_header[3] = FILE_MAGIC3;
    file_header[4] = FILE_VERSION;
    file_header[5] = note_count;
    file_header[6] = current_note_idx;
    file_header[7] = 0u;

    if (!file_write_exact(LFN_FILE, file_header, FILE_HDR_LEN)) {
        cbm_close(LFN_FILE);
        return 1u;
    }

    for (idx = 0u; idx < note_count; ++idx) {
        memset(title_raw, 0, sizeof(title_raw));
        memcpy(title_raw, note_titles[idx], NOTE_TITLE_LEN);
        if (!file_write_exact(LFN_FILE, title_raw, NOTE_TITLE_LEN)) {
            cbm_close(LFN_FILE);
            return 1u;
        }

        line_len = note_line_counts[idx];
        if (line_len == 0u) {
            line_len = 1u;
        }
        if (!file_write_exact(LFN_FILE, &line_len, 1u)) {
            cbm_close(LFN_FILE);
            return 1u;
        }

        fetch_note_to_buffer(idx, swap_note_lines);
        for (line_idx = 0u; line_idx < line_len; ++line_idx) {
            title_raw[0] = (unsigned char)strlen(swap_note_lines[line_idx]);
            if (!file_write_exact(LFN_FILE, &title_raw[0], 1u)) {
                cbm_close(LFN_FILE);
                return 1u;
            }
            if (title_raw[0] > 0u) {
                if (!file_write_exact(LFN_FILE, swap_note_lines[line_idx], title_raw[0])) {
                    cbm_close(LFN_FILE);
                    return 1u;
                }
            }
        }
    }

    cbm_close(LFN_FILE);
    strncpy(filename, name, 15);
    filename[15] = 0;
    modified = 0u;
    current_dirty = 0u;
    return 0u;
}

/*---------------------------------------------------------------------------
 * Dialogs
 *---------------------------------------------------------------------------*/

static void show_message(const char *msg, unsigned char color) {
    TuiRect win;
    unsigned char len;

    len = (unsigned char)strlen(msg);
    win.x = 8u;
    win.y = 10u;
    win.w = 24u;
    win.h = 5u;
    tui_window(&win, TUI_COLOR_LIGHTBLUE);

    tui_puts((unsigned char)(20u - len / 2u), 11u, msg, color);
    tui_puts(10, 13, "PRESS ANY KEY", TUI_COLOR_GRAY3);
    tui_getkey();
}

static unsigned char show_confirm(const char *msg) {
    TuiRect win;
    unsigned char key;

    win.x = 8u;
    win.y = 9u;
    win.w = 24u;
    win.h = 6u;
    tui_window_title(&win, "CONFIRM", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(10, 12, msg, TUI_COLOR_WHITE);
    tui_puts(10, 13, "Y:YES    N:NO", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == 'y' || key == 'Y') {
            return 1u;
        }
        if (key == 'n' || key == 'N' || key == TUI_KEY_RUNSTOP) {
            return 0u;
        }
    }
}

static unsigned char show_open_dialog(DirPageEntry *out_entry) {
    static const FileDialogConfig cfg = {
        "OPEN NOTEBOOK",
        "OPEN",
        "NO SEQ FILES FOUND",
        CBM_T_SEQ,
        1u
    };

    return file_dialog_pick(&file_scratch.dialog, &cfg, out_entry);
}

static unsigned char show_save_dialog(void) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    tui_clear(TUI_COLOR_BLUE);
    win.x = 5u;
    win.y = 7u;
    win.w = 30u;
    win.h = 8u;
    tui_window_title(&win, "SAVE NOTEBOOK", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(7, 10, "FILENAME:", TUI_COLOR_WHITE);

    tui_input_init(&input, 7, 11, 20, 16, file_scratch.save_buf, TUI_COLOR_CYAN);
    if (strcmp(filename, "UNTITLED") != 0) {
        strcpy(file_scratch.save_buf, filename);
        input.cursor = (unsigned char)strlen(file_scratch.save_buf);
    }

    tui_input_draw(&input);
    tui_puts(7, 12, "DRIVE:", TUI_COLOR_GRAY3);
    tui_print_uint(14, 12, storage_device_get_default(), TUI_COLOR_CYAN);
    tui_puts(7, 13, "F3:DRV RET:SAVE STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_F3) {
            storage_device_set_default(
                storage_device_toggle_8_9(storage_device_get_default()));
            tui_puts_n(14, 12, "", 2, TUI_COLOR_CYAN);
            tui_print_uint(14, 12, storage_device_get_default(), TUI_COLOR_CYAN);
            continue;
        }
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0u;
        }
        if (tui_input_key(&input, key)) {
            if (file_scratch.save_buf[0] == 0) {
                continue;
            }
            tui_puts(7, 12, "SAVING...", TUI_COLOR_YELLOW);
            if (file_save(file_scratch.save_buf) != 0u) {
                show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                return 0u;
            }
            return 1u;
        }
        tui_input_draw(&input);
    }
}

static unsigned char show_title_dialog(void) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    tui_clear(TUI_COLOR_BLUE);
    win.x = 4u;
    win.y = 8u;
    win.w = 32u;
    win.h = 7u;
    tui_window_title(&win, "RENAME NOTE", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(6, 10, "TITLE:", TUI_COLOR_WHITE);

    tui_input_init(&input, 6, 11, NOTE_TITLE_LEN, NOTE_TITLE_LEN, title_buf, TUI_COLOR_CYAN);
    strcpy(title_buf, note_titles[current_note_idx]);
    input.cursor = (unsigned char)strlen(title_buf);
    tui_input_draw(&input);
    tui_puts(6, 13, "RET:SAVE STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0u;
        }
        if (tui_input_key(&input, key)) {
            if (title_buf[0] == 0) {
                continue;
            }
            return 1u;
        }
        tui_input_draw(&input);
    }
}

static unsigned char show_find_dialog(void) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    tui_clear(TUI_COLOR_BLUE);
    win.x = 6u;
    win.y = 8u;
    win.w = 28u;
    win.h = 7u;
    tui_window_title(&win, "FIND", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(8, 10, "SEARCH FOR:", TUI_COLOR_WHITE);

    tui_input_init(&input, 8, 11, 18, FIND_LEN, find_buf, TUI_COLOR_CYAN);
    input.cursor = (unsigned char)strlen(find_buf);
    tui_input_draw(&input);
    tui_puts(8, 13, "RET:FIND STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0u;
        }
        if (tui_input_key(&input, key)) {
            if (find_buf[0] == 0) {
                continue;
            }
            return 1u;
        }
        tui_input_draw(&input);
    }
}

/*---------------------------------------------------------------------------
 * Note operations
 *---------------------------------------------------------------------------*/

static void create_note_after_current(void) {
    unsigned char idx;
    unsigned char insert_idx;

    if (note_count >= MAX_NOTES) {
        show_message("NOTE LIMIT", TUI_COLOR_LIGHTRED);
        return;
    }

    save_current_note_to_slot();
    insert_idx = (unsigned char)(current_note_idx + 1u);

    for (idx = note_count; idx > insert_idx; --idx) {
        strcpy(note_titles[idx], note_titles[idx - 1u]);
        note_line_counts[idx] = note_line_counts[idx - 1u];
        note_cursor_x[idx] = note_cursor_x[idx - 1u];
        note_cursor_y[idx] = note_cursor_y[idx - 1u];
        note_scroll_y[idx] = note_scroll_y[idx - 1u];
        fetch_note_to_buffer(idx - 1u, swap_note_lines);
        stash_buffer_to_note(idx, swap_note_lines);
    }

    clear_note_buffer(current_note_lines);
    current_line_count = 1u;
    note_line_counts[insert_idx] = 1u;
    note_cursor_x[insert_idx] = 0u;
    note_cursor_y[insert_idx] = 0u;
    note_scroll_y[insert_idx] = 0u;
    set_default_title(insert_idx, note_titles[insert_idx]);
    stash_buffer_to_note(insert_idx, current_note_lines);

    ++note_count;
    current_note_idx = insert_idx;
    cursor_x = 0u;
    cursor_y = 0u;
    scroll_y = 0u;
    clear_selection();
    focus_pane = PANE_EDIT;
    modified = 1u;
    current_dirty = 0u;
    ensure_current_note_visible();
}

static void rename_current_note(void) {
    if (!show_title_dialog()) {
        return;
    }

    strncpy(note_titles[current_note_idx], title_buf, NOTE_TITLE_LEN);
    note_titles[current_note_idx][NOTE_TITLE_LEN] = 0;
    modified = 1u;
}

static void delete_current_note(void) {
    unsigned char idx;

    if (note_count <= 1u) {
        clear_note_buffer(current_note_lines);
        current_line_count = 1u;
        note_line_counts[0] = 1u;
        note_cursor_x[0] = 0u;
        note_cursor_y[0] = 0u;
        note_scroll_y[0] = 0u;
        set_default_title(0u, note_titles[0]);
        stash_buffer_to_note(0u, current_note_lines);
        cursor_x = 0u;
        cursor_y = 0u;
        scroll_y = 0u;
        clear_selection();
        modified = 1u;
        current_dirty = 0u;
        return;
    }

    for (idx = current_note_idx; idx < note_count - 1u; ++idx) {
        strcpy(note_titles[idx], note_titles[idx + 1u]);
        note_line_counts[idx] = note_line_counts[idx + 1u];
        note_cursor_x[idx] = note_cursor_x[idx + 1u];
        note_cursor_y[idx] = note_cursor_y[idx + 1u];
        note_scroll_y[idx] = note_scroll_y[idx + 1u];
        fetch_note_to_buffer(idx + 1u, swap_note_lines);
        stash_buffer_to_note(idx, swap_note_lines);
    }

    note_titles[note_count - 1u][0] = 0;
    note_line_counts[note_count - 1u] = 0u;
    note_cursor_x[note_count - 1u] = 0u;
    note_cursor_y[note_count - 1u] = 0u;
    note_scroll_y[note_count - 1u] = 0u;
    --note_count;
    if (current_note_idx >= note_count) {
        current_note_idx = (unsigned char)(note_count - 1u);
    }
    load_current_note_from_slot();
    modified = 1u;
    ensure_current_note_visible();
}

static void move_current_note_up(void) {
    unsigned char other;
    char title_tmp[NOTE_TITLE_LEN + 1];
    unsigned char line_tmp;
    unsigned char view_tmp;

    if (current_note_idx == 0u) {
        return;
    }

    save_current_note_to_slot();
    other = (unsigned char)(current_note_idx - 1u);
    fetch_note_to_buffer(other, swap_note_lines);
    stash_buffer_to_note(other, current_note_lines);
    stash_buffer_to_note(current_note_idx, swap_note_lines);

    strcpy(title_tmp, note_titles[other]);
    strcpy(note_titles[other], note_titles[current_note_idx]);
    strcpy(note_titles[current_note_idx], title_tmp);

    line_tmp = note_line_counts[other];
    note_line_counts[other] = note_line_counts[current_note_idx];
    note_line_counts[current_note_idx] = line_tmp;

    view_tmp = note_cursor_x[other];
    note_cursor_x[other] = note_cursor_x[current_note_idx];
    note_cursor_x[current_note_idx] = view_tmp;

    view_tmp = note_cursor_y[other];
    note_cursor_y[other] = note_cursor_y[current_note_idx];
    note_cursor_y[current_note_idx] = view_tmp;

    view_tmp = note_scroll_y[other];
    note_scroll_y[other] = note_scroll_y[current_note_idx];
    note_scroll_y[current_note_idx] = view_tmp;

    current_note_idx = other;
    modified = 1u;
    current_dirty = 1u;
    ensure_current_note_visible();
}

static void move_current_note_down(void) {
    unsigned char other;
    char title_tmp[NOTE_TITLE_LEN + 1];
    unsigned char line_tmp;
    unsigned char view_tmp;

    if (current_note_idx + 1u >= note_count) {
        return;
    }

    save_current_note_to_slot();
    other = (unsigned char)(current_note_idx + 1u);
    fetch_note_to_buffer(other, swap_note_lines);
    stash_buffer_to_note(other, current_note_lines);
    stash_buffer_to_note(current_note_idx, swap_note_lines);

    strcpy(title_tmp, note_titles[other]);
    strcpy(note_titles[other], note_titles[current_note_idx]);
    strcpy(note_titles[current_note_idx], title_tmp);

    line_tmp = note_line_counts[other];
    note_line_counts[other] = note_line_counts[current_note_idx];
    note_line_counts[current_note_idx] = line_tmp;

    view_tmp = note_cursor_x[other];
    note_cursor_x[other] = note_cursor_x[current_note_idx];
    note_cursor_x[current_note_idx] = view_tmp;

    view_tmp = note_cursor_y[other];
    note_cursor_y[other] = note_cursor_y[current_note_idx];
    note_cursor_y[current_note_idx] = view_tmp;

    view_tmp = note_scroll_y[other];
    note_scroll_y[other] = note_scroll_y[current_note_idx];
    note_scroll_y[current_note_idx] = view_tmp;

    current_note_idx = other;
    modified = 1u;
    current_dirty = 1u;
    ensure_current_note_visible();
}

/*---------------------------------------------------------------------------
 * Help / resume / navigation
 *---------------------------------------------------------------------------*/

static void show_help_popup(void) {
    TuiRect win;
    unsigned char key;

    win.x = 1u;
    win.y = 4u;
    win.w = 38u;
    win.h = 16u;
    tui_window_title(&win, "QUICKNOTES HELP", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(3, 6, "READYOS QUICKNOTES", TUI_COLOR_WHITE);
    tui_puts(3, 7, "LEFT@COL0/^L/^O: TITLES RIGHT", TUI_COLOR_GRAY3);
    tui_puts(3, 8, "RET:EDIT HOME:TOP UP/DN:NOTE", TUI_COLOR_GRAY3);
    tui_puts(3, 9, "CTRL+N/CTRL+P: PAGE", TUI_COLOR_GRAY3);
    tui_puts(3, 10, "CTRL+W:+NOTE CTRL+R:TITLE", TUI_COLOR_GRAY3);
    tui_puts(3, 11, "CTRL+D:DELETE CTRL+U/K:MOVE", TUI_COLOR_GRAY3);
    tui_puts(3, 12, "RET:NEW LINE DEL:BACKSP", TUI_COLOR_GRAY3);
    tui_puts(3, 13, "F1:COPY F3:PASTE F6:SELECT", TUI_COLOR_GRAY3);
    tui_puts(3, 14, "CTRL+L/O:PANE F5:SAVE CTRL+A:AS", TUI_COLOR_GRAY3);
    tui_puts(3, 15, "F7:OPEN CTRL+F/G:FIND", TUI_COLOR_GRAY3);
    tui_puts(3, 16, "FILE DLG: F3 TOGGLE D8/D9", TUI_COLOR_GRAY3);
    tui_puts(3, 17, "F2/F4:APPS CTRL+B:LAUNCHER", TUI_COLOR_GRAY3);
    tui_puts(3, 18, "RET/F8/STOP: CLOSE", TUI_COLOR_CYAN);

    while (1) {
        key = tui_getkey();
        if (key == KEY_HELP || key == TUI_KEY_RETURN ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return;
        }
    }
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    save_current_note_to_slot();
    (void)resume_save_segments(quicknotes_resume_write_segments, QUICKNOTES_RESUME_SEG_COUNT);
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len;
    unsigned char idx;

    if (!resume_ready) {
        return 0u;
    }
    if (!resume_load_segments(quicknotes_resume_read_segments, QUICKNOTES_RESUME_SEG_COUNT, &payload_len)) {
        return 0u;
    }
    if (!validate_note_banks()) {
        return 0u;
    }
    if (note_count == 0u || note_count > MAX_NOTES) {
        return 0u;
    }
    if (current_note_idx >= note_count) {
        return 0u;
    }
    if (current_line_count == 0u || current_line_count > MAX_NOTE_LINES) {
        return 0u;
    }

    filename[sizeof(filename) - 1u] = 0;
    find_buf[sizeof(find_buf) - 1u] = 0;
    modified = modified ? 1u : 0u;
    current_dirty = current_dirty ? 1u : 0u;
    has_selection = has_selection ? 1u : 0u;

    for (idx = 0u; idx < note_count; ++idx) {
        note_titles[idx][NOTE_TITLE_LEN] = 0;
        if (note_titles[idx][0] == 0) {
            set_default_title(idx, note_titles[idx]);
        }
        if (note_line_counts[idx] == 0u || note_line_counts[idx] > MAX_NOTE_LINES) {
            return 0u;
        }
    }

    for (idx = 0u; idx < current_line_count; ++idx) {
        current_note_lines[idx][MAX_LINE_LEN - 1u] = 0;
    }

    running = 1u;
    clamp_view_state();
    return 1u;
}

static unsigned char handle_global_nav(unsigned char key) {
    unsigned char nav_action;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        resume_save_state();
        tui_return_to_launcher();
        return 1u;
    }
    if (nav_action >= 1u && nav_action <= 23u) {
        resume_save_state();
        tui_switch_to_app(nav_action);
        return 1u;
    }
    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return 1u;
    }
    return 0u;
}

/*---------------------------------------------------------------------------
 * Initialization / main loop
 *---------------------------------------------------------------------------*/

static void quicknotes_init(void) {
    unsigned char bank;

    tui_init();
    reu_mgr_init();

    running = 1u;
    resume_ready = 0u;
    note_banks[0] = 0xFFu;
    note_banks[1] = 0xFFu;
    filename[0] = 0;
    find_buf[0] = 0;

    bank = SHIM_CURRENT_BANK;
    if (bank >= 1u && bank <= 23u) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1u;
    }

    if (!resume_restore_state()) {
        if (!allocate_note_banks()) {
            show_message("NO REU SPACE", TUI_COLOR_LIGHTRED);
            running = 0u;
            return;
        }
        init_blank_document();
    }
}

static void quicknotes_loop(void) {
    unsigned char key;
    unsigned char old_scroll;
    unsigned char joined_line;
    unsigned char old_cursor_x;
    unsigned char old_cursor_y;
    unsigned char old_start_y;
    unsigned char old_end_y;

    if (!running) {
        return;
    }

    quicknotes_draw();

    while (running) {
        key = tui_getkey();
        if (handle_global_nav(key)) {
            continue;
        }
        if (key == KEY_CTRL_L || key == KEY_CTRL_O) {
            if (focus_pane == PANE_EDIT) {
                undraw_cursor();
                focus_pane = PANE_LIST;
            } else {
                focus_pane = PANE_EDIT;
            }
            quicknotes_refresh();
            continue;
        }

        if (focus_pane == PANE_LIST) {
            switch (key) {
                case TUI_KEY_RUNSTOP:
                    running = 0u;
                    break;

                case TUI_KEY_RETURN:
                case TUI_KEY_RIGHT:
                    focus_pane = PANE_EDIT;
                    quicknotes_refresh();
                    break;

                case TUI_KEY_UP:
                    if (current_note_idx > 0u) {
                        switch_to_note((unsigned char)(current_note_idx - 1u));
                        quicknotes_draw();
                    }
                    break;

                case TUI_KEY_DOWN:
                    if (current_note_idx + 1u < note_count) {
                        switch_to_note((unsigned char)(current_note_idx + 1u));
                        quicknotes_draw();
                    }
                    break;

                case TUI_KEY_HOME:
                    switch_to_note(0u);
                    quicknotes_draw();
                    break;

                case KEY_CTRL_N:
                    list_page_down();
                    quicknotes_draw();
                    break;

                case KEY_CTRL_P:
                    list_page_up();
                    quicknotes_draw();
                    break;

                case KEY_CTRL_W:
                    create_note_after_current();
                    quicknotes_draw();
                    break;

                case KEY_CTRL_R:
                    rename_current_note();
                    quicknotes_draw();
                    break;

                case KEY_CTRL_D:
                    if (show_confirm("DELETE NOTE?")) {
                        delete_current_note();
                    }
                    quicknotes_draw();
                    break;

                case KEY_CTRL_U:
                    move_current_note_up();
                    quicknotes_draw();
                    break;

                case KEY_CTRL_K:
                    move_current_note_down();
                    quicknotes_draw();
                    break;

                case KEY_SAVE:
                    if (strcmp(filename, "UNTITLED") == 0) {
                        (void)show_save_dialog();
                    } else {
                        tui_puts(1, STATUS_Y, "SAVING...", TUI_COLOR_YELLOW);
                        if (file_save(filename) != 0u) {
                            show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                        }
                    }
                    quicknotes_draw();
                    break;

                case KEY_CTRL_A:
                    (void)show_save_dialog();
                    quicknotes_draw();
                    break;

                case KEY_OPEN:
                    {
                        DirPageEntry selected_entry;

                        if (show_open_dialog(&selected_entry) == FILE_DIALOG_RC_OK) {
                            if (modified && !show_confirm("DISCARD CHANGES?")) {
                                quicknotes_draw();
                                break;
                            }
                            tui_clear(TUI_COLOR_BLUE);
                            tui_puts(13, 12, "LOADING...", TUI_COLOR_YELLOW);
                            if (file_load(selected_entry.name) != 0u) {
                                show_message("LOAD ERROR!", TUI_COLOR_LIGHTRED);
                            }
                        }
                    }
                    quicknotes_draw();
                    break;

                case KEY_HELP:
                    show_help_popup();
                    quicknotes_draw();
                    break;

                default:
                    if (key == KEY_COPY || key == KEY_PASTE || key == KEY_SELECT ||
                        key == KEY_CTRL_F || key == KEY_CTRL_G ||
                        (key >= 32u && key < 128u)) {
                        focus_pane = PANE_EDIT;
                        quicknotes_refresh();
                        continue;
                    }
                    break;
            }
            continue;
        }

        if (has_selection) {
            switch (key) {
                case TUI_KEY_RUNSTOP:
                    running = 0u;
                    break;

                case KEY_COPY:
                    selection_visual_range_for(cursor_x, cursor_y, &old_start_y, &old_end_y);
                    copy_to_clipboard();
                    clear_selection();
                    for (old_cursor_x = old_start_y; old_cursor_x <= old_end_y; ++old_cursor_x) {
                        draw_editor_line(old_cursor_x);
                        if (old_cursor_x == old_end_y) {
                            break;
                        }
                    }
                    quicknotes_refresh();
                    break;

                case KEY_SELECT:
                    selection_visual_range_for(cursor_x, cursor_y, &old_start_y, &old_end_y);
                    clear_selection();
                    for (old_cursor_x = old_start_y; old_cursor_x <= old_end_y; ++old_cursor_x) {
                        draw_editor_line(old_cursor_x);
                        if (old_cursor_x == old_end_y) {
                            break;
                        }
                    }
                    quicknotes_refresh();
                    break;

                case KEY_HELP:
                    show_help_popup();
                    quicknotes_draw();
                    break;

                case TUI_KEY_UP:
                case TUI_KEY_DOWN:
                case TUI_KEY_LEFT:
                case TUI_KEY_RIGHT:
                case TUI_KEY_HOME:
                    old_cursor_x = cursor_x;
                    old_cursor_y = cursor_y;
                    undraw_cursor();
                    handle_cursor_selection(key);
                    redraw_selection_transition(old_cursor_x, old_cursor_y);
                    quicknotes_refresh();
                    break;

                default:
                    break;
            }
            continue;
        }

        switch (key) {
            case TUI_KEY_RUNSTOP:
                running = 0u;
                break;

            case KEY_COPY:
                copy_to_clipboard();
                quicknotes_refresh();
                break;

            case KEY_PASTE:
                undraw_cursor();
                paste_from_clipboard();
                draw_editor_text();
                quicknotes_refresh();
                break;

            case KEY_SAVE:
                if (strcmp(filename, "UNTITLED") == 0) {
                    (void)show_save_dialog();
                } else {
                    tui_puts(1, STATUS_Y, "SAVING...", TUI_COLOR_YELLOW);
                    if (file_save(filename) != 0u) {
                        show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                    }
                }
                quicknotes_draw();
                break;

            case KEY_CTRL_A:
                (void)show_save_dialog();
                quicknotes_draw();
                break;

            case KEY_SELECT:
                begin_selection();
                quicknotes_refresh();
                break;

            case KEY_OPEN:
                {
                    DirPageEntry selected_entry;

                    if (show_open_dialog(&selected_entry) == FILE_DIALOG_RC_OK) {
                        if (modified && !show_confirm("DISCARD CHANGES?")) {
                            quicknotes_draw();
                            break;
                        }
                        tui_clear(TUI_COLOR_BLUE);
                        tui_puts(13, 12, "LOADING...", TUI_COLOR_YELLOW);
                        if (file_load(selected_entry.name) != 0u) {
                            show_message("LOAD ERROR!", TUI_COLOR_LIGHTRED);
                        }
                    }
                }
                quicknotes_draw();
                break;

            case KEY_HELP:
                show_help_popup();
                quicknotes_draw();
                break;

            case KEY_CTRL_W:
                create_note_after_current();
                quicknotes_draw();
                break;

            case KEY_CTRL_R:
                rename_current_note();
                quicknotes_draw();
                break;

            case KEY_CTRL_D:
                if (show_confirm("DELETE NOTE?")) {
                    delete_current_note();
                }
                quicknotes_draw();
                break;

            case KEY_CTRL_U:
                move_current_note_up();
                quicknotes_draw();
                break;

            case KEY_CTRL_K:
                move_current_note_down();
                quicknotes_draw();
                break;

            case KEY_CTRL_N:
                undraw_cursor();
                old_scroll = scroll_y;
                move_page_down();
                if (scroll_y != old_scroll) {
                    draw_editor_text();
                }
                quicknotes_refresh();
                break;

            case KEY_CTRL_P:
                undraw_cursor();
                old_scroll = scroll_y;
                move_page_up();
                if (scroll_y != old_scroll) {
                    draw_editor_text();
                }
                quicknotes_refresh();
                break;

            case KEY_CTRL_E:
                undraw_cursor();
                cursor_x = line_length(cursor_y);
                quicknotes_refresh();
                break;

            case KEY_CTRL_F:
                if (show_find_dialog()) {
                    if (!find_next_match()) {
                        show_message("NOT FOUND", TUI_COLOR_LIGHTRED);
                    }
                }
                quicknotes_draw();
                break;

            case KEY_CTRL_G:
                if (find_buf[0] == 0) {
                    if (show_find_dialog()) {
                        if (!find_next_match()) {
                            show_message("NOT FOUND", TUI_COLOR_LIGHTRED);
                        }
                    }
                    quicknotes_draw();
                    break;
                }
                undraw_cursor();
                old_scroll = scroll_y;
                if (find_next_match()) {
                    if (scroll_y != old_scroll) {
                        draw_editor_text();
                    }
                    quicknotes_refresh();
                } else {
                    show_message("NOT FOUND", TUI_COLOR_LIGHTRED);
                    quicknotes_draw();
                }
                break;

            case TUI_KEY_UP:
            case TUI_KEY_DOWN:
            case TUI_KEY_RIGHT:
            case TUI_KEY_HOME:
                undraw_cursor();
                old_scroll = scroll_y;
                handle_cursor(key);
                if (scroll_y != old_scroll) {
                    draw_editor_text();
                }
                quicknotes_refresh();
                break;

            case TUI_KEY_LEFT:
                if (cursor_x == 0u) {
                    focus_pane = PANE_LIST;
                    quicknotes_refresh();
                    break;
                }
                undraw_cursor();
                old_scroll = scroll_y;
                handle_cursor(key);
                if (scroll_y != old_scroll) {
                    draw_editor_text();
                }
                quicknotes_refresh();
                break;

            case TUI_KEY_RETURN:
                undraw_cursor();
                old_scroll = scroll_y;
                handle_return();
                if (scroll_y != old_scroll) {
                    draw_editor_text();
                } else {
                    draw_editor_line((cursor_y > 0u) ? (unsigned char)(cursor_y - 1u) : 0u);
                    draw_editor_line(cursor_y);
                }
                quicknotes_refresh();
                break;

            case TUI_KEY_DEL:
                undraw_cursor();
                old_scroll = scroll_y;
                joined_line = handle_delete();
                if (scroll_y != old_scroll) {
                    draw_editor_text();
                } else if (joined_line) {
                    for (old_cursor_x = cursor_y; old_cursor_x < scroll_y + EDIT_HEIGHT; ++old_cursor_x) {
                        draw_editor_line(old_cursor_x);
                    }
                } else {
                    draw_editor_line(cursor_y);
                }
                quicknotes_refresh();
                break;

            default:
                if (key >= 32u && key < 128u) {
                    undraw_cursor();
                    handle_char(key);
                    draw_editor_line(cursor_y);
                    quicknotes_refresh();
                }
                break;
        }
    }

    if (resume_ready) {
        resume_invalidate();
    }
    free_note_banks();
    __asm__("jmp $FCE2");
}

int main(void) {
    quicknotes_init();
    quicknotes_loop();
    return 0;
}
