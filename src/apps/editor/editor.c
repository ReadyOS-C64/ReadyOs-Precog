/*
 * editor.c - Ready OS Text Editor
 * Simple text editor with clipboard support
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include <c64.h>
#include <cbm.h>
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

/* Layout */
#define HEADER_Y     0
#define EDIT_START_Y 2
#define EDIT_HEIGHT  20
#define STATUS_Y     23
#define HELP_Y       24
#define TEXT_X       5
#define LINE_NUM_X   3

/* Buffer sizes */
#define MAX_LINES    100
#define MAX_LINE_LEN 80
#define LINE_DISPLAY (40 - TEXT_X)
#define FIND_LEN     20
#define CLIP_TEXT_BUF_SIZE ((EDIT_HEIGHT * MAX_LINE_LEN) + EDIT_HEIGHT)

/* Function keys for editor commands */
#define KEY_COPY  TUI_KEY_F1   /* F1 = Copy */
#define KEY_PASTE TUI_KEY_F3   /* F3 = Paste */
#define KEY_SAVE   TUI_KEY_F5   /* F5 = Save */
#define KEY_SELECT TUI_KEY_F6   /* Shift+F5 = Selection mode */
#define KEY_OPEN   TUI_KEY_F7   /* F7 = Open */
#define KEY_HELP   TUI_KEY_F8   /* Shift+F7 = Help */

/* Control-key shortcuts (cgetc() returns ASCII control codes) */
#define KEY_CTRL_A 1
#define KEY_CTRL_B 2
#define KEY_CTRL_E 5
#define KEY_CTRL_F 6
#define KEY_CTRL_G 7
#define KEY_CTRL_N 14
#define KEY_CTRL_P 16

/* File I/O */
#define IO_BUF_SIZE     64
#define LFN_DIR         1
#define LFN_FILE        2
#define LFN_CMD         15
#define CR              0x0D

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Static variables
 *---------------------------------------------------------------------------*/

/* Text buffer - array of line pointers */
static char text_buffer[MAX_LINES][MAX_LINE_LEN];
static unsigned char line_count;

/* Cursor position */
static unsigned char cursor_x;
static unsigned char cursor_y;

/* Scroll position */
static unsigned char scroll_y;

/* Editor state */
static unsigned char running;
static unsigned char modified;
static char filename[16];

/* Selection for copy/paste */
static unsigned char sel_anchor_x;
static unsigned char sel_anchor_y;
static unsigned char has_selection;
static union {
    char clip_text_buf[CLIP_TEXT_BUF_SIZE + 1];
    FileDialogState dialog;
    char save_buf[17];
} file_scratch;
static char find_buf[FIND_LEN + 1];
static unsigned char resume_ready;

static ResumeWriteSegment editor_resume_write_segments[] = {
    { text_buffer, sizeof(text_buffer) },
    { &line_count, sizeof(line_count) },
    { &cursor_x, sizeof(cursor_x) },
    { &cursor_y, sizeof(cursor_y) },
    { &scroll_y, sizeof(scroll_y) },
    { &modified, sizeof(modified) },
    { filename, sizeof(filename) },
    { find_buf, sizeof(find_buf) },
    { &sel_anchor_x, sizeof(sel_anchor_x) },
    { &sel_anchor_y, sizeof(sel_anchor_y) },
    { &has_selection, sizeof(has_selection) },
};

static ResumeReadSegment editor_resume_read_segments[] = {
    { text_buffer, sizeof(text_buffer) },
    { &line_count, sizeof(line_count) },
    { &cursor_x, sizeof(cursor_x) },
    { &cursor_y, sizeof(cursor_y) },
    { &scroll_y, sizeof(scroll_y) },
    { &modified, sizeof(modified) },
    { filename, sizeof(filename) },
    { find_buf, sizeof(find_buf) },
    { &sel_anchor_x, sizeof(sel_anchor_x) },
    { &sel_anchor_y, sizeof(sel_anchor_y) },
    { &has_selection, sizeof(has_selection) },
};

#define EDITOR_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(editor_resume_read_segments) / sizeof(editor_resume_read_segments[0])))

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/
static void editor_init(void);
static void editor_draw(void);
static void editor_loop(void);
static void draw_header_static(void);
static void draw_header_dynamic(void);
static void draw_text(void);
static void draw_status(void);
static void draw_drive_status(unsigned char x, unsigned char y);
static void draw_cursor(void);
static void undraw_cursor(void);
static void draw_line(unsigned char line_idx);
static void draw_lines_from(unsigned char start_line);
static void redraw_line_range(unsigned char start_line, unsigned char end_line);
static void clamp_view_state(void);
static unsigned char max_scroll_y(void);
static unsigned char line_length(unsigned char line_idx);
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
static void handle_char(unsigned char ch);
static void handle_return(void);
static unsigned char handle_delete(void);
static void handle_cursor(unsigned char key);
static void handle_cursor_selection(unsigned char key);
static void copy_to_clipboard(void);
static void paste_from_clipboard(void);
static unsigned char show_find_dialog(void);
static unsigned char find_next_match(void);
static void new_file(void);
static unsigned char show_open_dialog(DirPageEntry *out_entry);
static unsigned char file_load(const char *name);
static unsigned char file_save(const char *name);
static unsigned char show_save_dialog(void);
static unsigned char show_confirm(const char *msg);
static void show_message(const char *msg, unsigned char color);
static void show_help_popup(void);
static void resume_save_state(void);
static unsigned char resume_restore_state(void);

/*---------------------------------------------------------------------------
 * Initialization
 *---------------------------------------------------------------------------*/

static void editor_init(void) {
    /* Initialize TUI and REU manager */
    tui_init();
    reu_mgr_init();

    /* Initialize text buffer */
    new_file();
    find_buf[0] = 0;

    /* Default filename */
    strcpy(filename, "UNTITLED");

    running = 1;
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    (void)resume_save_segments(editor_resume_write_segments, EDITOR_RESUME_SEG_COUNT);
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;
    unsigned char i;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_load_segments(editor_resume_read_segments, EDITOR_RESUME_SEG_COUNT, &payload_len)) {
        return 0;
    }

    if (line_count == 0 || line_count > MAX_LINES) {
        return 0;
    }
    filename[sizeof(filename) - 1] = 0;
    for (i = 0; i < line_count; ++i) {
        text_buffer[i][MAX_LINE_LEN - 1] = 0;
    }

    has_selection = 0;
    sel_anchor_x = cursor_x;
    sel_anchor_y = cursor_y;
    modified = modified ? 1 : 0;
    clamp_view_state();
    running = 1;
    return 1;
}

static unsigned char line_length(unsigned char line_idx) {
    return (unsigned char)strlen(text_buffer[line_idx]);
}

static unsigned char max_scroll_y(void) {
    if (line_count > EDIT_HEIGHT) {
        return (unsigned char)(line_count - EDIT_HEIGHT);
    }
    return 0;
}

static void clamp_view_state(void) {
    unsigned char max_scroll;

    if (line_count == 0) {
        line_count = 1;
    }
    if (cursor_y >= line_count) {
        cursor_y = (unsigned char)(line_count - 1);
    }
    if (cursor_x >= MAX_LINE_LEN - 1) {
        cursor_x = (MAX_LINE_LEN - 2);
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
        scroll_y = (unsigned char)(cursor_y - EDIT_HEIGHT + 1);
        if (scroll_y > max_scroll) {
            scroll_y = max_scroll;
        }
    }
}

static void new_file(void) {
    unsigned char i;

    /* Clear all lines */
    for (i = 0; i < MAX_LINES; ++i) {
        text_buffer[i][0] = 0;
    }

    line_count = 1;
    cursor_x = 0;
    cursor_y = 0;
    scroll_y = 0;
    modified = 0;
    has_selection = 0;
    sel_anchor_x = 0;
    sel_anchor_y = 0;
}

static void clear_selection(void) {
    has_selection = 0;
    sel_anchor_x = cursor_x;
    sel_anchor_y = cursor_y;
}

static void begin_selection(void) {
    sel_anchor_x = cursor_x;
    sel_anchor_y = cursor_y;
    has_selection = 1;
}

static unsigned char selection_has_text(void) {
    if (!has_selection) {
        return 0;
    }
    if (sel_anchor_y != cursor_y) {
        return 1;
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
        return 0;
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
        return 0;
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
        *start_col = 0;
        *end_col = end_x;
        return (unsigned char)(*end_col > *start_col);
    }

    *start_col = 0;
    *end_col = len;
    return (unsigned char)(*end_col > *start_col);
}

static unsigned char selection_line_span(unsigned char line_idx,
                                         unsigned char *start_col,
                                         unsigned char *end_col) {
    if (!has_selection) {
        return 0;
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

    selection_visual_range_for(old_cursor_x, old_cursor_y, &old_start_y, &old_end_y);
    selection_visual_range_for(cursor_x, cursor_y, &new_start_y, &new_end_y);

    redraw_start = (old_start_y < new_start_y) ? old_start_y : new_start_y;
    redraw_end = (old_end_y > new_end_y) ? old_end_y : new_end_y;
    redraw_line_range(redraw_start, redraw_end);
}

/*---------------------------------------------------------------------------
 * Drawing
 *---------------------------------------------------------------------------*/

static void draw_header_static(void) {
    TuiRect header;

    header.x = 0;
    header.y = HEADER_Y;
    header.w = 40;
    header.h = 2;

    tui_window(&header, TUI_COLOR_LIGHTBLUE);

    /* Static labels */
    tui_puts(1, HEADER_Y, "EDITOR:", TUI_COLOR_WHITE);
    tui_puts(9, HEADER_Y, filename, TUI_COLOR_YELLOW);
    tui_puts(28, HEADER_Y, "L:", TUI_COLOR_WHITE);
    tui_puts(33, HEADER_Y, "/", TUI_COLOR_WHITE);
}

static void draw_header_dynamic(void) {
    /* Overwrite just the line numbers */
    tui_puts_n(30, HEADER_Y, "", 3, TUI_COLOR_CYAN);
    tui_print_uint(30, HEADER_Y, cursor_y + 1, TUI_COLOR_CYAN);
    tui_puts_n(34, HEADER_Y, "", 4, TUI_COLOR_CYAN);
    tui_print_uint(34, HEADER_Y, line_count, TUI_COLOR_CYAN);
}

static void draw_drive_status(unsigned char x, unsigned char y) {
    tui_puts(x, y, "D:", TUI_COLOR_GRAY3);
    tui_puts_n((unsigned char)(x + 2), y, "", 2, TUI_COLOR_CYAN);
    tui_print_uint((unsigned char)(x + 2), y, storage_device_get_default(), TUI_COLOR_CYAN);
}

static void draw_line(unsigned char line_idx) {
    unsigned char screen_y;
    unsigned int line_no;
    unsigned char line_x;
    unsigned char len;
    unsigned char col;
    unsigned int text_offset;
    unsigned char ch;
    unsigned char highlight_start;
    unsigned char highlight_end;
    unsigned char has_highlight;

    if (line_idx < scroll_y || line_idx >= scroll_y + EDIT_HEIGHT) return;
    screen_y = EDIT_START_Y + (line_idx - scroll_y);
    tui_puts_n(0, screen_y, "", TEXT_X, TUI_COLOR_GRAY2);

    if (line_idx < line_count) {
        tui_clear_line(screen_y, TEXT_X, LINE_DISPLAY, TUI_COLOR_WHITE);
        line_no = (unsigned int)line_idx + 1;
        if (line_no >= 100) {
            line_x = 0;
        } else if (line_no >= 10) {
            line_x = 1;
        } else {
            line_x = 2;
        }
        tui_print_uint(line_x, screen_y, line_no, TUI_COLOR_GRAY2);
        tui_putc(LINE_NUM_X, screen_y, tui_ascii_to_screen(':'), TUI_COLOR_GRAY2);
        len = line_length(line_idx);
        if (len > LINE_DISPLAY) {
            len = LINE_DISPLAY;
        }
        has_highlight = selection_line_span(line_idx, &highlight_start, &highlight_end);
        if (highlight_start > LINE_DISPLAY) {
            highlight_start = LINE_DISPLAY;
        }
        if (highlight_end > LINE_DISPLAY) {
            highlight_end = LINE_DISPLAY;
        }
        text_offset = (unsigned int)screen_y * 40 + TEXT_X;
        for (col = 0; col < len; ++col) {
            ch = tui_ascii_to_screen((unsigned char)text_buffer[line_idx][col]);
            if (has_highlight && col >= highlight_start && col < highlight_end) {
                ch |= 0x80;
            }
            TUI_SCREEN[text_offset + col] = ch;
            TUI_COLOR_RAM[text_offset + col] = TUI_COLOR_WHITE;
        }
    } else {
        tui_clear_line(screen_y, 0, 40, TUI_COLOR_WHITE);
    }
}

static void draw_lines_from(unsigned char start_line) {
    unsigned char idx;
    for (idx = start_line; idx < scroll_y + EDIT_HEIGHT; ++idx) {
        draw_line(idx);
    }
}

static void redraw_line_range(unsigned char start_line, unsigned char end_line) {
    unsigned char idx;

    if (end_line < start_line) {
        return;
    }

    for (idx = start_line; idx <= end_line; ++idx) {
        draw_line(idx);
        if (idx == end_line) {
            break;
        }
    }
}

static void draw_text(void) {
    unsigned char line_idx;

    /* Draw all visible lines (overwrite in-place, no clear needed) */
    for (line_idx = scroll_y; line_idx < scroll_y + EDIT_HEIGHT; ++line_idx) {
        draw_line(line_idx);
    }
}

static void draw_status(void) {
    tui_puts_n(1, STATUS_Y, "", 30, TUI_COLOR_WHITE);
    if (modified) {
        tui_puts_n(1, STATUS_Y, "*MODIFIED*", 12, TUI_COLOR_LIGHTRED);
    }
    if (has_selection) {
        tui_puts(24, STATUS_Y, "SELECT", TUI_COLOR_CYAN);
    }

    tui_puts(14, STATUS_Y, "COL:", TUI_COLOR_GRAY3);
    tui_puts_n(18, STATUS_Y, "", 4, TUI_COLOR_GRAY3);
    tui_print_uint(18, STATUS_Y, cursor_x + 1, TUI_COLOR_GRAY3);
    draw_drive_status(30, STATUS_Y);
}

static void draw_help(void) {
    tui_puts(0, HELP_Y, "F1:CPY F3:PST F5:SAV F6:SEL F7:OPN F8:H",
             TUI_COLOR_GRAY3);
}

static void undraw_cursor(void) {
    unsigned char screen_x, screen_y;
    unsigned int offset;
    unsigned char highlight_start;
    unsigned char highlight_end;
    screen_x = TEXT_X + cursor_x;
    screen_y = EDIT_START_Y + (cursor_y - scroll_y);
    if (screen_x >= 40) screen_x = 39;
    if (screen_y < EDIT_START_Y || screen_y >= EDIT_START_Y + EDIT_HEIGHT) return;
    offset = (unsigned int)screen_y * 40 + screen_x;
    TUI_SCREEN[offset] &= 0x7F;  /* Clear cursor reverse bit */
    if (selection_line_span(cursor_y, &highlight_start, &highlight_end) &&
        cursor_x >= highlight_start && cursor_x < highlight_end) {
        TUI_SCREEN[offset] |= 0x80;
    }
}

static void draw_cursor(void) {
    unsigned char screen_x, screen_y;
    unsigned int offset;

    /* Calculate screen position */
    screen_x = TEXT_X + cursor_x;
    screen_y = EDIT_START_Y + (cursor_y - scroll_y);

    if (screen_x >= 40) screen_x = 39;
    if (screen_y >= EDIT_START_Y + EDIT_HEIGHT) return;

    /* Draw cursor as reverse block */
    offset = (unsigned int)screen_y * 40 + screen_x;
    TUI_SCREEN[offset] |= 0x80;  /* Reverse bit */
}

static void editor_draw(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header_static();
    draw_header_dynamic();
    draw_text();
    draw_status();
    draw_help();
    draw_cursor();
}

/* Refresh only dynamic parts without full screen clear */
static void editor_refresh(void) {
    draw_header_dynamic();
    draw_status();
    draw_cursor();
}

static void move_page_down(void) {
    unsigned char jump;
    unsigned char max_scroll;

    if (line_count <= 1) {
        return;
    }

    jump = EDIT_HEIGHT - 1;
    max_scroll = max_scroll_y();

    if (cursor_y + jump >= line_count) {
        cursor_y = (unsigned char)(line_count - 1);
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

    jump = EDIT_HEIGHT - 1;

    if (cursor_y > jump) {
        cursor_y = (unsigned char)(cursor_y - jump);
    } else {
        cursor_y = 0;
    }

    if (scroll_y > jump) {
        scroll_y = (unsigned char)(scroll_y - jump);
    } else {
        scroll_y = 0;
    }

    clamp_view_state();
}

static unsigned char to_lower(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return (unsigned char)(ch + 32);
    }
    return ch;
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

    if (needle_len == 0 || start_col > text_len || needle_len > text_len) {
        return 255;
    }

    for (col = start_col; col + needle_len <= text_len; ++col) {
        for (ni = 0; ni < needle_len; ++ni) {
            if (to_lower((unsigned char)text_buffer[line_idx][col + ni]) !=
                to_lower((unsigned char)needle[ni])) {
                break;
            }
        }
        if (ni == needle_len) {
            return col;
        }
    }

    return 255;
}

static unsigned char find_next_match(void) {
    unsigned char line_idx;
    unsigned char start_col;
    unsigned char match_col;

    if (find_buf[0] == 0) {
        return 0;
    }

    line_idx = cursor_y;
    start_col = (unsigned char)(cursor_x + 1);
    if (start_col > line_length(cursor_y)) {
        start_col = line_length(cursor_y);
    }

    while (line_idx < line_count) {
        match_col = find_in_line_from(line_idx, start_col, find_buf);
        if (match_col != 255) {
            cursor_y = line_idx;
            cursor_x = match_col;
            clamp_view_state();
            return 1;
        }
        ++line_idx;
        start_col = 0;
    }

    line_idx = 0;
    while (line_idx <= cursor_y && line_idx < line_count) {
        match_col = find_in_line_from(line_idx, 0, find_buf);
        if (match_col != 255) {
            if (line_idx == cursor_y && match_col <= cursor_x) {
                ++line_idx;
                continue;
            }
            cursor_y = line_idx;
            cursor_x = match_col;
            clamp_view_state();
            return 1;
        }
        ++line_idx;
    }

    return 0;
}

/*---------------------------------------------------------------------------
 * Text Editing
 *---------------------------------------------------------------------------*/

static void handle_char(unsigned char ch) {
    unsigned char len;
    unsigned char i;

    len = strlen(text_buffer[cursor_y]);

    /* Check if line has room */
    if (len >= MAX_LINE_LEN - 1) {
        return;
    }

    /* Insert character at cursor position */
    /* Shift characters right */
    for (i = len + 1; i > cursor_x; --i) {
        text_buffer[cursor_y][i] = text_buffer[cursor_y][i - 1];
    }

    /* Insert new character */
    text_buffer[cursor_y][cursor_x] = ch;
    ++cursor_x;
    modified = 1;
}

static void handle_return(void) {
    unsigned char i;
    unsigned char rest_len;

    /* Check if we have room for more lines */
    if (line_count >= MAX_LINES) {
        return;
    }

    /* Shift all lines below down */
    for (i = line_count; i > cursor_y + 1; --i) {
        strcpy(text_buffer[i], text_buffer[i - 1]);
    }

    /* Calculate length of text after cursor */
    rest_len = strlen(text_buffer[cursor_y]) - cursor_x;

    /* Copy rest of current line to new line */
    if (rest_len > 0) {
        strcpy(text_buffer[cursor_y + 1], &text_buffer[cursor_y][cursor_x]);
    } else {
        text_buffer[cursor_y + 1][0] = 0;
    }

    /* Terminate current line at cursor */
    text_buffer[cursor_y][cursor_x] = 0;

    /* Move cursor to start of new line */
    ++cursor_y;
    ++line_count;
    cursor_x = 0;
    modified = 1;
    clamp_view_state();
}

static unsigned char handle_delete(void) {
    unsigned char len;
    unsigned char prev_len;
    unsigned char i;

    if (cursor_x > 0) {
        /* Delete character before cursor */
        len = strlen(text_buffer[cursor_y]);

        /* Shift characters left */
        for (i = cursor_x - 1; i < len; ++i) {
            text_buffer[cursor_y][i] = text_buffer[cursor_y][i + 1];
        }

        --cursor_x;
        modified = 1;
        return 0;
    } else if (cursor_y > 0) {
        /* Join with previous line */
        prev_len = strlen(text_buffer[cursor_y - 1]);

        /* Check if there's room */
        if (prev_len + strlen(text_buffer[cursor_y]) < MAX_LINE_LEN) {
            /* Append current line to previous */
            strcat(text_buffer[cursor_y - 1], text_buffer[cursor_y]);

            /* Shift all lines below up */
            for (i = cursor_y; i < line_count - 1; ++i) {
                strcpy(text_buffer[i], text_buffer[i + 1]);
            }

            /* Clear last line */
            text_buffer[line_count - 1][0] = 0;

            --line_count;
            --cursor_y;
            cursor_x = prev_len;
            modified = 1;
            clamp_view_state();
            return 1;
        }
    }

    return 0;
}

static void handle_cursor(unsigned char key) {
    unsigned char len;

    switch (key) {
        case TUI_KEY_UP:
            if (cursor_y > 0) {
                --cursor_y;
                /* Adjust cursor_x if new line is shorter */
                len = line_length(cursor_y);
                if (cursor_x > len) {
                    cursor_x = len;
                }
                clamp_view_state();
            }
            break;

        case TUI_KEY_DOWN:
            if (cursor_y < line_count - 1) {
                ++cursor_y;
                len = line_length(cursor_y);
                if (cursor_x > len) {
                    cursor_x = len;
                }
                clamp_view_state();
            }
            break;

        case TUI_KEY_LEFT:
            if (cursor_x > 0) {
                --cursor_x;
            } else if (cursor_y > 0) {
                /* Wrap to end of previous line */
                --cursor_y;
                cursor_x = line_length(cursor_y);
                clamp_view_state();
            }
            break;

        case TUI_KEY_RIGHT:
            len = line_length(cursor_y);
            if (cursor_x < len) {
                ++cursor_x;
            } else if (cursor_y < line_count - 1) {
                /* Wrap to start of next line */
                ++cursor_y;
                cursor_x = 0;
                clamp_view_state();
            }
            break;

        case TUI_KEY_HOME:
            cursor_x = 0;
            break;
    }
}

static void handle_cursor_selection(unsigned char key) {
    unsigned char len;
    unsigned char min_y;
    unsigned char max_y;

    min_y = scroll_y;
    max_y = (unsigned char)(scroll_y + EDIT_HEIGHT - 1);
    if (max_y >= line_count) {
        max_y = (unsigned char)(line_count - 1);
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
            if (cursor_x > 0) {
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
                cursor_x = 0;
            }
            break;

        case TUI_KEY_HOME:
            cursor_x = 0;
            break;
    }
}

/*---------------------------------------------------------------------------
 * Clipboard Operations
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
        out_len = 0;
        selection_bounds(&start_y, &start_x, &end_y, &end_x);

        for (line_idx = start_y; line_idx <= end_y; ++line_idx) {
            from_col = 0;
            to_col = line_length(line_idx);

            if (line_idx == start_y) {
                from_col = start_x;
            }
            if (line_idx == end_y) {
                to_col = end_x;
            }

            for (col = from_col; col < to_col && out_len < CLIP_TEXT_BUF_SIZE; ++col) {
                file_scratch.clip_text_buf[out_len] = text_buffer[line_idx][col];
                ++out_len;
            }

            if (line_idx != end_y && out_len < CLIP_TEXT_BUF_SIZE) {
                file_scratch.clip_text_buf[out_len] = CR;
                ++out_len;
            }
        }

        if (out_len > 0) {
            clip_copy(CLIP_TYPE_TEXT, file_scratch.clip_text_buf, out_len);
        }
        return;
    }

    len = line_length(cursor_y);
    if (len > 0) {
        clip_copy(CLIP_TYPE_TEXT, text_buffer[cursor_y], len);
    }
}

static void paste_from_clipboard(void) {
    unsigned int len;
    unsigned int i;

    if (clip_item_count() == 0) return;
    len = clip_paste(0, file_scratch.clip_text_buf, CLIP_TEXT_BUF_SIZE);
    if (len > 0) {
        file_scratch.clip_text_buf[len] = 0;
        clear_selection();

        /* Insert pasted text at cursor */
        for (i = 0; i < len; ++i) {
            if ((unsigned char)file_scratch.clip_text_buf[i] == CR) {
                if (line_count >= MAX_LINES) {
                    break;
                }
                handle_return();
            } else if ((unsigned char)file_scratch.clip_text_buf[i] >= 32 &&
                       (unsigned char)file_scratch.clip_text_buf[i] < 128) {
                handle_char((unsigned char)file_scratch.clip_text_buf[i]);
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * File I/O
 *---------------------------------------------------------------------------*/

static unsigned char show_open_dialog(DirPageEntry *out_entry) {
    static const FileDialogConfig cfg = {
        "OPEN FILE",
        "OPEN",
        "NO FILES FOUND ON DISK",
        DIR_PAGE_TYPE_ANY,
        1u
    };

    return file_dialog_pick(&file_scratch.dialog, &cfg, out_entry);
}

static unsigned char file_load(const char *name) {
    static char open_str[24];
    static char io_buf[IO_BUF_SIZE];
    unsigned char line;
    unsigned char col;
    unsigned char truncated;
    int n;
    unsigned char i;

    /* Build open string: "name,s,r" */
    strcpy(open_str, name);
    strcat(open_str, ",s,r");

    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return 1;
    }

    new_file();
    line = 0;
    col = 0;
    truncated = 0;

    while (1) {
        n = cbm_read(LFN_FILE, io_buf, IO_BUF_SIZE);
        if (n <= 0) break;

        for (i = 0; i < (unsigned char)n; ++i) {
            if (io_buf[i] == CR) {
                /* End current line */
                text_buffer[line][col] = 0;
                ++line;
                col = 0;
                if (line >= MAX_LINES) {
                    truncated = 1;
                    goto done;
                }
            } else {
                if (col < MAX_LINE_LEN - 1) {
                    text_buffer[line][col] = io_buf[i];
                    ++col;
                }
            }
        }
    }

done:
    /* Terminate the last line if it didn't end with CR */
    if (!truncated) {
        text_buffer[line][col] = 0;
    }

    cbm_close(LFN_FILE);

    line_count = truncated ? MAX_LINES : (unsigned char)(line + 1);
    if (line_count > MAX_LINES) line_count = MAX_LINES;

    strncpy(filename, name, 15);
    filename[15] = 0;
    modified = 0;
    cursor_x = 0;
    cursor_y = 0;
    scroll_y = 0;

    return 0;
}

static unsigned char file_save(const char *name) {
    static char cmd_str[24];
    static char open_str[24];
    unsigned char cr_byte;
    unsigned char i;
    unsigned char len;

    cr_byte = CR;

    /* Scratch existing file first */
    strcpy(cmd_str, "s:");
    strcat(cmd_str, name);
    cbm_open(LFN_CMD, storage_device_get_default(), 15, cmd_str);
    cbm_close(LFN_CMD);

    /* Open for write */
    strcpy(open_str, name);
    strcat(open_str, ",s,w");

    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return 1;
    }

    for (i = 0; i < line_count; ++i) {
        len = strlen(text_buffer[i]);
        if (len > 0) {
            cbm_write(LFN_FILE, text_buffer[i], len);
        }
        /* Write CR after each line (except maybe last empty) */
        if (i < line_count - 1 || len > 0) {
            cbm_write(LFN_FILE, &cr_byte, 1);
        }
    }

    cbm_close(LFN_FILE);

    strncpy(filename, name, 15);
    filename[15] = 0;
    modified = 0;

    return 0;
}

static void show_message(const char *msg, unsigned char color) {
    TuiRect win;
    unsigned char len;

    len = strlen(msg);

    win.x = 8;
    win.y = 10;
    win.w = 24;
    win.h = 5;
    tui_window(&win, TUI_COLOR_LIGHTBLUE);

    tui_puts(20 - len / 2, 11, msg, color);
    tui_puts(10, 13, "PRESS ANY KEY", TUI_COLOR_GRAY3);

    tui_getkey();
}

static unsigned char show_confirm(const char *msg) {
    TuiRect win;
    unsigned char key;

    win.x = 8;
    win.y = 9;
    win.w = 24;
    win.h = 6;
    tui_window_title(&win, "CONFIRM", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(10, 12, msg, TUI_COLOR_WHITE);
    tui_puts(10, 13, "Y:YES    N:NO", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == 'y' || key == 'Y') return 1;
        if (key == 'n' || key == 'N' || key == TUI_KEY_RUNSTOP) return 0;
    }
}

static unsigned char show_save_dialog(void) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    tui_clear(TUI_COLOR_BLUE);

    win.x = 5;
    win.y = 7;
    win.w = 30;
    win.h = 8;
    tui_window_title(&win, "SAVE FILE", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(7, 10, "FILENAME:", TUI_COLOR_WHITE);

    /* Init first (this clears the buffer) */
    tui_input_init(&input, 7, 11, 20, 16, file_scratch.save_buf, TUI_COLOR_CYAN);

    /* THEN pre-fill with current filename (after init cleared it) */
    if (strcmp(filename, "UNTITLED") != 0) {
        strcpy(file_scratch.save_buf, filename);
        input.cursor = strlen(file_scratch.save_buf);
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
            return 0;
        }

        if (tui_input_key(&input, key)) {
            /* RETURN was pressed */
            if (file_scratch.save_buf[0] == 0) {
                continue;  /* Empty name, ignore */
            }

            /* Show saving status on a clean save dialog */
            tui_puts(7, 12, "SAVING...", TUI_COLOR_YELLOW);
            if (file_save(file_scratch.save_buf) != 0) {
                show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                return 0;
            }
            return 1;
        }

        /* Redraw input field after every keypress */
        tui_input_draw(&input);
    }
}

static unsigned char show_find_dialog(void) {
    TuiRect win;
    TuiInput input;
    unsigned char key;
    static char prior_find[FIND_LEN + 1];

    tui_clear(TUI_COLOR_BLUE);

    win.x = 5;
    win.y = 7;
    win.w = 30;
    win.h = 8;
    tui_window_title(&win, "FIND TEXT", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(7, 10, "SEARCH:", TUI_COLOR_WHITE);
    strncpy(prior_find, find_buf, FIND_LEN);
    prior_find[FIND_LEN] = 0;
    tui_input_init(&input, 7, 11, 20, FIND_LEN, find_buf, TUI_COLOR_CYAN);
    if (prior_find[0] != 0) {
        strcpy(find_buf, prior_find);
        input.cursor = (unsigned char)strlen(find_buf);
    }

    tui_input_draw(&input);
    tui_puts(7, 13, "RET:FIND  STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();

        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0;
        }

        if (tui_input_key(&input, key)) {
            if (find_buf[0] == 0) {
                continue;
            }
            return 1;
        }

        tui_input_draw(&input);
    }
}

static void show_help_popup(void) {
    TuiRect win;
    unsigned char key;

    win.x = 1;
    win.y = 4;
    win.w = 38;
    win.h = 16;
    tui_window_title(&win, "EDITOR HELP", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(3, 6, "READYOS EDITOR", TUI_COLOR_WHITE);
    tui_puts(3, 7, "RET:NEW LINE DEL:BACKSP", TUI_COLOR_GRAY3);
    tui_puts(3, 8, "HOME:START CTRL+E:LINE END", TUI_COLOR_GRAY3);
    tui_puts(3, 9, "UP/DN/LT/RT:MOVE", TUI_COLOR_GRAY3);
    tui_puts(3, 10, "CTRL+N:PG DN CTRL+P:PG UP", TUI_COLOR_GRAY3);
    tui_puts(3, 11, "CTRL+F:FIND CTRL+G:FIND NEXT", TUI_COLOR_GRAY3);
    tui_puts(3, 12, "F1:COPY/LINE  F3:PASTE", TUI_COLOR_GRAY3);
    tui_puts(3, 13, "F5:SAVE   CTRL+A:SAVE AS", TUI_COLOR_GRAY3);
    tui_puts(3, 14, "F6:SELECT F7:OPEN F8:HELP", TUI_COLOR_GRAY3);
    tui_puts(3, 15, "FILE DLG: F3 TOGGLE D8/D9", TUI_COLOR_GRAY3);
    tui_puts(3, 16, "SELECT:ARROWS/HOME F1/F6", TUI_COLOR_GRAY3);
    tui_puts(3, 17, "F2/F4:APPS CTRL+B:LAUNCHER", TUI_COLOR_GRAY3);
    tui_puts(3, 18, "RET/F8/STOP:CLOSE", TUI_COLOR_CYAN);

    while (1) {
        key = tui_getkey();
        if (key == KEY_HELP || key == TUI_KEY_RETURN ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return;
        }
    }
}

/*---------------------------------------------------------------------------
 * Main Loop
 *---------------------------------------------------------------------------*/

static unsigned char handle_editor_nav(unsigned char key) {
    unsigned char nav_action;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        clear_selection();
        resume_save_state();
        tui_return_to_launcher();
        return 1;
    }
    if (nav_action >= 1 && nav_action <= 15) {
        clear_selection();
        resume_save_state();
        tui_switch_to_app(nav_action);
        return 1;
    }
    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return 1;
    }
    return 0;
}

static void editor_loop(void) {
    unsigned char key;
    unsigned char old_scroll;
    unsigned char joined_line;
    unsigned char old_cursor_x;
    unsigned char old_cursor_y;
    unsigned char old_start_y;
    unsigned char old_end_y;

    editor_draw();

    while (running) {
        key = tui_getkey();
        if (handle_editor_nav(key)) {
            continue;
        }

        if (has_selection) {
            switch (key) {
                case TUI_KEY_RUNSTOP:
                    running = 0;
                    break;

                case KEY_COPY:
                    selection_visual_range_for(cursor_x, cursor_y, &old_start_y, &old_end_y);
                    copy_to_clipboard();
                    clear_selection();
                    redraw_line_range(old_start_y, old_end_y);
                    editor_refresh();
                    break;

                case KEY_SELECT:
                    selection_visual_range_for(cursor_x, cursor_y, &old_start_y, &old_end_y);
                    clear_selection();
                    redraw_line_range(old_start_y, old_end_y);
                    editor_refresh();
                    break;

                case KEY_HELP:
                    show_help_popup();
                    editor_draw();
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
                    editor_refresh();
                    break;

                default:
                    break;
            }
            continue;
        }

        /* Handle control keys */
        switch (key) {
            case TUI_KEY_RUNSTOP:
                running = 0;
                break;

            case KEY_COPY:
                copy_to_clipboard();
                editor_refresh();
                break;

            case KEY_PASTE:
                undraw_cursor();
                paste_from_clipboard();
                draw_text();
                editor_refresh();
                break;

            case KEY_SAVE:
                if (strcmp(filename, "UNTITLED") == 0) {
                    show_save_dialog();
                } else {
                    tui_puts(1, STATUS_Y, "SAVING...", TUI_COLOR_YELLOW);
                    if (file_save(filename) != 0) {
                        show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                    }
                }
                editor_draw();
                break;

            case KEY_CTRL_A:
                show_save_dialog();
                editor_draw();
                break;

            case KEY_SELECT:
                begin_selection();
                editor_refresh();
                break;

            case KEY_OPEN:
                {
                    DirPageEntry selected_entry;

                    if (show_open_dialog(&selected_entry) == FILE_DIALOG_RC_OK) {
                        if (modified) {
                            if (!show_confirm("DISCARD CHANGES?")) {
                                editor_draw();
                                break;
                            }
                        }
                        tui_clear(TUI_COLOR_BLUE);
                        tui_puts(14, 12, "LOADING...", TUI_COLOR_YELLOW);
                        if (file_load(selected_entry.name) != 0) {
                            show_message("LOAD ERROR!", TUI_COLOR_LIGHTRED);
                        }
                    }
                }
                editor_draw();
                break;

            case KEY_HELP:
                show_help_popup();
                editor_draw();
                break;

            case KEY_CTRL_N:
                undraw_cursor();
                old_scroll = scroll_y;
                move_page_down();
                if (scroll_y != old_scroll) {
                    draw_text();
                }
                editor_refresh();
                break;

            case KEY_CTRL_P:
                undraw_cursor();
                old_scroll = scroll_y;
                move_page_up();
                if (scroll_y != old_scroll) {
                    draw_text();
                }
                editor_refresh();
                break;

            case KEY_CTRL_E:
                undraw_cursor();
                cursor_x = line_length(cursor_y);
                editor_refresh();
                break;

            case KEY_CTRL_F:
                if (show_find_dialog()) {
                    if (!find_next_match()) {
                        show_message("NOT FOUND", TUI_COLOR_LIGHTRED);
                    }
                }
                editor_draw();
                break;

            case KEY_CTRL_G:
                if (find_buf[0] == 0) {
                    if (show_find_dialog()) {
                        if (!find_next_match()) {
                            show_message("NOT FOUND", TUI_COLOR_LIGHTRED);
                        }
                    }
                    editor_draw();
                    break;
                }
                undraw_cursor();
                old_scroll = scroll_y;
                if (find_next_match()) {
                    if (scroll_y != old_scroll) {
                        draw_text();
                    }
                    editor_refresh();
                } else {
                    show_message("NOT FOUND", TUI_COLOR_LIGHTRED);
                    editor_draw();
                }
                break;

            case TUI_KEY_UP:
            case TUI_KEY_DOWN:
            case TUI_KEY_LEFT:
            case TUI_KEY_RIGHT:
            case TUI_KEY_HOME:
                undraw_cursor();
                old_scroll = scroll_y;
                handle_cursor(key);
                if (scroll_y != old_scroll) {
                    draw_text();
                }
                editor_refresh();
                break;

            case TUI_KEY_RETURN:
                undraw_cursor();
                old_scroll = scroll_y;
                handle_return();
                if (scroll_y != old_scroll) {
                    draw_text();
                } else {
                    draw_lines_from((cursor_y > 0) ? (unsigned char)(cursor_y - 1) : 0);
                }
                editor_refresh();
                break;

            case TUI_KEY_DEL:
                undraw_cursor();
                old_scroll = scroll_y;
                joined_line = handle_delete();
                if (scroll_y != old_scroll) {
                    draw_text();
                } else if (joined_line) {
                    draw_lines_from(cursor_y);
                } else {
                    draw_line(cursor_y);
                }
                editor_refresh();
                break;

            default:
                /* Regular character input */
                if (key >= 32 && key < 128) {
                    undraw_cursor();
                    handle_char(key);
                    draw_line(cursor_y);
                    editor_refresh();
                }
                break;
        }
    }

    /* Reset to BASIC (no shim available for proper return) */
    __asm__("jmp $FCE2");  /* KERNAL cold start */
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

int main(void) {
    unsigned char bank;

    editor_init();
    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();
    editor_loop();
    return 0;
}
