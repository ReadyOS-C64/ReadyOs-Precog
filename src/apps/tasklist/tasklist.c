/*
 * tasklist.c - Ready OS Task List / Outliner
 * Hierarchical task manager with notes, search, and file persistence
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

#include "../../lib/clipboard.h"
#include "../../lib/dir_page.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"
#include "../../lib/storage_device.h"

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

/* Layout */
#define HEADER_Y       0
#define LIST_START_Y   2
#define LIST_HEIGHT    19
#define SEARCH_Y       21
#define STATUS_Y       22
#define HELP_Y1        23
#define HELP_Y2        24

/* Task storage */
#define MAX_TASKS        64
#define MAX_TASK_LEN     40
#define MAX_INDENT        7

/* Task flags */
#define TASK_FLAG_DONE     0x01
#define TASK_FLAG_HAS_NOTE 0x02
#define TASK_INDENT_SHIFT  4
#define TASK_INDENT_MASK   0x70

/* Note storage */
#define MAX_NOTE_LINES   16
#define MAX_NOTE_LINE_LEN 40
#define NOTE_POOL_SIZE   4096

/* Search */
#define MAX_SEARCH_LEN   30
#define MAX_SEARCH_WORDS  4

/* File I/O */
#define MAX_DIR_ENTRIES  18
#define DIR_NAME_LEN     17
#define IO_BUF_SIZE      64
#define LFN_DIR          1
#define LFN_FILE         2
#define LFN_CMD          15
#define CR               0x0D
#define OPEN_DIALOG_RC_OK     0u
#define OPEN_DIALOG_RC_CANCEL 1u

/* Tree drawing characters */
#define TREE_BRANCH  0x6B  /* TUI_T_RIGHT */
#define TREE_LAST    0x6D  /* TUI_CORNER_BL */
#define TREE_VERT    0x5D  /* TUI_VLINE */
#define TREE_HORIZ   0x40  /* TUI_HLINE */

/* Note indicator */
#define NOTE_STAR    42    /* '*' screen code */

/* Checkbox characters */
#define CHECK_DONE   24    /* 'X' screen code */
#define CHECK_ACTIVE 32    /* space */

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Data Structures
 *---------------------------------------------------------------------------*/

/* Task storage */
static char task_text[MAX_TASKS][MAX_TASK_LEN + 1];
static unsigned char task_flags[MAX_TASKS];
static unsigned char task_count;

/* Note storage */
static unsigned int  note_offset[MAX_TASKS];
static unsigned int  note_length[MAX_TASKS];
static char note_pool[NOTE_POOL_SIZE];
static unsigned int note_pool_used;

/* View/filter state */
static unsigned char view_list[MAX_TASKS];
static unsigned char view_flags[MAX_TASKS];  /* 0=direct match, 1=parent-only */
static unsigned char view_count;
static unsigned char view_is_filtered;

/* Search */
static char search_buf[MAX_SEARCH_LEN + 1];
static unsigned char search_active;

/* Cursor and scroll */
static unsigned char cursor_pos;   /* index into view_list */
static unsigned char scroll_y;     /* first visible row in view */

/* Edit buffer */
static char edit_buf[MAX_TASK_LEN + 1];

/* File state */
static char filename[16];
static unsigned char modified;
static unsigned char running;

static unsigned char resume_ready;

static ResumeWriteSegment tasklist_resume_write_segments[] = {
    { &task_count, sizeof(task_count) },
    { task_text, sizeof(task_text) },
    { task_flags, sizeof(task_flags) },
    { note_offset, sizeof(note_offset) },
    { note_length, sizeof(note_length) },
    { note_pool, sizeof(note_pool) },
    { &note_pool_used, sizeof(note_pool_used) },
    { search_buf, sizeof(search_buf) },
    { &search_active, sizeof(search_active) },
    { &cursor_pos, sizeof(cursor_pos) },
    { &scroll_y, sizeof(scroll_y) },
    { filename, sizeof(filename) },
    { &modified, sizeof(modified) },
};

static ResumeReadSegment tasklist_resume_read_segments[] = {
    { &task_count, sizeof(task_count) },
    { task_text, sizeof(task_text) },
    { task_flags, sizeof(task_flags) },
    { note_offset, sizeof(note_offset) },
    { note_length, sizeof(note_length) },
    { note_pool, sizeof(note_pool) },
    { &note_pool_used, sizeof(note_pool_used) },
    { search_buf, sizeof(search_buf) },
    { &search_active, sizeof(search_active) },
    { &cursor_pos, sizeof(cursor_pos) },
    { &scroll_y, sizeof(scroll_y) },
    { filename, sizeof(filename) },
    { &modified, sizeof(modified) },
};

#define TASKLIST_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(tasklist_resume_read_segments) / sizeof(tasklist_resume_read_segments[0])))

/* Indent colors (cycle through 8 levels) */
static const unsigned char indent_colors[] = {
    TUI_COLOR_WHITE, TUI_COLOR_CYAN, TUI_COLOR_LIGHTGREEN, TUI_COLOR_YELLOW,
    TUI_COLOR_LIGHTBLUE, TUI_COLOR_LIGHTRED, TUI_COLOR_PURPLE, TUI_COLOR_ORANGE
};

/* File browser data - shared with note edit via union to save memory */
union {
    union {
        struct {
            DirPageEntry dir_entries[MAX_DIR_ENTRIES];
            char dir_display[MAX_DIR_ENTRIES][21];
            const char *dir_ptrs[MAX_DIR_ENTRIES];
            unsigned char dir_count;
        } browser;
        char save_buf[17];
    } file;
    struct {
        char lines[MAX_NOTE_LINES][MAX_NOTE_LINE_LEN + 1];
        unsigned char line_count;
        unsigned char cx, cy;
        unsigned char scroll;
    } note;
} shared;

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/
static void tasklist_init(void);
static void tasklist_draw(void);
static void tasklist_loop(void);
static void rebuild_view(void);
static void draw_header(void);
static void draw_task_line(unsigned char view_idx);
static void draw_visible_tasks(void);
static void draw_status(void);
static void draw_help(void);
static void draw_search_bar(void);
static void insert_task(unsigned char after_view_pos);
static void delete_task(unsigned char view_pos);
static void toggle_done(unsigned char view_pos);
static void indent_task(unsigned char view_pos);
static void outdent_task(unsigned char view_pos);
static void edit_task_popup(unsigned char view_pos);
static void note_edit_popup(unsigned char view_pos);
static void note_compact(void);
static void apply_filter(void);
static void clear_filter(void);
static void copy_task_to_clipboard(unsigned char view_pos);
static void paste_from_clipboard(unsigned char view_pos);
static unsigned char read_directory(unsigned char start_index,
                                    unsigned char *out_total);
static unsigned char show_open_dialog(DirPageEntry *out_entry);
static unsigned char show_save_dialog(void);
static unsigned char file_load(const char *name);
static unsigned char file_save(const char *name);
static void show_message(const char *msg, unsigned char color);
static unsigned char show_confirm(const char *msg);
static void show_help_popup(void);
static unsigned char resume_restore_state(void);
static void resume_save_state(void);

/*---------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

static unsigned char get_indent(unsigned char task_idx) {
    return (task_flags[task_idx] & TASK_INDENT_MASK) >> TASK_INDENT_SHIFT;
}

static void set_indent(unsigned char task_idx, unsigned char level) {
    task_flags[task_idx] = (task_flags[task_idx] & ~TASK_INDENT_MASK)
                         | ((level << TASK_INDENT_SHIFT) & TASK_INDENT_MASK);
}

static unsigned char is_done(unsigned char task_idx) {
    return task_flags[task_idx] & TASK_FLAG_DONE;
}

static unsigned char has_note(unsigned char task_idx) {
    return task_flags[task_idx] & TASK_FLAG_HAS_NOTE;
}

/* Case-insensitive character comparison helper */
static unsigned char to_lower(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch + 32;
    return ch;
}

/* Case-insensitive substring search */
static unsigned char str_contains(const char *haystack, const char *needle) {
    unsigned char hi, ni, nlen, hlen;
    nlen = strlen(needle);
    hlen = strlen(haystack);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;
    for (hi = 0; hi <= hlen - nlen; ++hi) {
        for (ni = 0; ni < nlen; ++ni) {
            if (to_lower(haystack[hi + ni]) != to_lower(needle[ni])) break;
        }
        if (ni == nlen) return 1;
    }
    return 0;
}

static unsigned char note_contains(unsigned char task_idx, const char *needle) {
    unsigned int start, off, end;
    unsigned char ni, nlen;

    if (!has_note(task_idx) || note_offset[task_idx] == 0xFFFF || note_length[task_idx] == 0) {
        return 0;
    }

    nlen = (unsigned char)strlen(needle);
    if (nlen == 0) return 1;

    off = note_offset[task_idx];
    if (off >= NOTE_POOL_SIZE) return 0;

    end = off + note_length[task_idx];
    if (end > NOTE_POOL_SIZE) end = NOTE_POOL_SIZE;
    if ((unsigned int)nlen > (end - off)) return 0;

    for (start = off; start + nlen <= end; ++start) {
        for (ni = 0; ni < nlen; ++ni) {
            if (to_lower(note_pool[start + ni]) != to_lower(needle[ni])) break;
        }
        if (ni == nlen) return 1;
    }

    return 0;
}

/* Ensure cursor stays in bounds */
static void clamp_cursor(void) {
    if (view_count == 0) {
        cursor_pos = 0;
        scroll_y = 0;
        return;
    }
    if (cursor_pos >= view_count) {
        cursor_pos = view_count - 1;
    }
    if (cursor_pos < scroll_y) {
        scroll_y = cursor_pos;
    }
    if (cursor_pos >= scroll_y + LIST_HEIGHT) {
        scroll_y = cursor_pos - LIST_HEIGHT + 1;
    }
}

static unsigned char resume_validate_state(void) {
    unsigned char i;
    unsigned int off;
    unsigned int len;

    if (task_count == 0 || task_count > MAX_TASKS) {
        return 0;
    }
    if (note_pool_used > NOTE_POOL_SIZE) {
        return 0;
    }
    search_buf[MAX_SEARCH_LEN] = 0;
    filename[15] = 0;

    for (i = 0; i < task_count; ++i) {
        task_text[i][MAX_TASK_LEN] = 0;
        off = note_offset[i];
        len = note_length[i];

        if (off == 0xFFFF || len == 0) {
            note_offset[i] = 0xFFFF;
            note_length[i] = 0;
            task_flags[i] &= (unsigned char)~TASK_FLAG_HAS_NOTE;
            continue;
        }
        if (off >= NOTE_POOL_SIZE || len > NOTE_POOL_SIZE ||
            (unsigned int)(off + len) > note_pool_used ||
            (unsigned int)(off + len) > NOTE_POOL_SIZE) {
            note_offset[i] = 0xFFFF;
            note_length[i] = 0;
            task_flags[i] &= (unsigned char)~TASK_FLAG_HAS_NOTE;
            continue;
        }
        task_flags[i] |= TASK_FLAG_HAS_NOTE;
    }

    return 1;
}

static void resume_rebuild_runtime_view(void) {
    unsigned char i;
    search_active = search_active ? 1 : 0;
    if (search_active && search_buf[0] == 0) {
        search_active = 0;
    }
    if (filename[0] == 0) {
        strcpy(filename, "UNTITLED");
    }
    modified = modified ? 1 : 0;
    running = 1;

    for (i = task_count; i < MAX_TASKS; ++i) {
        note_offset[i] = 0xFFFF;
        note_length[i] = 0;
    }

    rebuild_view();
    clamp_cursor();
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_load_segments(tasklist_resume_read_segments, TASKLIST_RESUME_SEG_COUNT, &payload_len)) {
        return 0;
    }

    if (!resume_validate_state()) {
        resume_invalidate();
        return 0;
    }
    resume_rebuild_runtime_view();
    return 1;
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    search_buf[MAX_SEARCH_LEN] = 0;
    filename[15] = 0;
    modified = modified ? 1 : 0;
    search_active = search_active ? 1 : 0;
    (void)resume_save_segments(tasklist_resume_write_segments, TASKLIST_RESUME_SEG_COUNT);
}

/*---------------------------------------------------------------------------
 * View management
 *---------------------------------------------------------------------------*/

static void rebuild_view(void) {
    unsigned char i;

    if (search_active && search_buf[0] != 0) {
        apply_filter();
        return;
    }

    /* Unfiltered: show all tasks */
    view_count = task_count;
    view_is_filtered = 0;
    for (i = 0; i < task_count; ++i) {
        view_list[i] = i;
        view_flags[i] = 0;
    }
    clamp_cursor();
}

/*---------------------------------------------------------------------------
 * Search/filter (Phase 7)
 *---------------------------------------------------------------------------*/

static void apply_filter(void) {
    static char words[MAX_SEARCH_WORDS][MAX_SEARCH_LEN + 1];
    static unsigned char is_hashtag[MAX_SEARCH_WORDS];
    static unsigned char matched[MAX_TASKS];
    static unsigned char needed[MAX_TASKS];
    unsigned char word_count;
    unsigned char i, w, si, di;
    unsigned char indent, parent_indent;
    const char *src;

    /* Parse search into words */
    word_count = 0;
    src = search_buf;
    while (*src && word_count < MAX_SEARCH_WORDS) {
        /* Skip spaces */
        while (*src == ' ') ++src;
        if (*src == 0) break;

        /* Check for hashtag prefix */
        if (*src == '#') {
            is_hashtag[word_count] = 1;
            ++src;  /* skip # */
        } else {
            is_hashtag[word_count] = 0;
        }

        /* Copy word */
        di = 0;
        while (*src && *src != ' ' && di < MAX_SEARCH_LEN) {
            words[word_count][di] = *src;
            ++di;
            ++src;
        }
        words[word_count][di] = 0;
        if (di > 0) ++word_count;
    }

    if (word_count == 0) {
        /* Empty search - show all */
        view_is_filtered = 0;
        view_count = task_count;
        for (i = 0; i < task_count; ++i) {
            view_list[i] = i;
            view_flags[i] = 0;
        }
        clamp_cursor();
        return;
    }

    /* Mark direct matches */
    memset(matched, 0, MAX_TASKS);
    memset(needed, 0, MAX_TASKS);

    for (i = 0; i < task_count; ++i) {
        unsigned char all_match = 1;
        for (w = 0; w < word_count; ++w) {
            if (is_hashtag[w]) {
                /* Match hashtag in task text or note text */
                static char htag[MAX_SEARCH_LEN + 2];
                htag[0] = '#';
                strcpy(&htag[1], words[w]);
                if (!str_contains(task_text[i], htag) && !note_contains(i, htag)) {
                    all_match = 0;
                    break;
                }
            } else {
                if (!str_contains(task_text[i], words[w]) && !note_contains(i, words[w])) {
                    all_match = 0;
                    break;
                }
            }
        }
        matched[i] = all_match;
    }

    /* Include parent chain for matched tasks */
    for (i = 0; i < task_count; ++i) {
        if (!matched[i]) continue;
        needed[i] = 1;

        /* Walk backwards to find parents */
        indent = get_indent(i);
        if (indent > 0) {
            si = i;
            while (si > 0 && indent > 0) {
                --si;
                parent_indent = get_indent(si);
                if (parent_indent < indent) {
                    needed[si] = 1;
                    indent = parent_indent;
                }
            }
        }
    }

    /* Build view_list */
    view_count = 0;
    view_is_filtered = 1;
    for (i = 0; i < task_count; ++i) {
        if (matched[i] || needed[i]) {
            view_list[view_count] = i;
            view_flags[view_count] = matched[i] ? 0 : 1;  /* 1=parent-only */
            ++view_count;
        }
    }

    clamp_cursor();
}

static void clear_filter(void) {
    search_buf[0] = 0;
    search_active = 0;
    view_is_filtered = 0;
    rebuild_view();
}

/*---------------------------------------------------------------------------
 * Tree character computation
 *---------------------------------------------------------------------------*/

/* Check if task at `task_idx` has more siblings at the same indent level below it */
static unsigned char has_sibling_below(unsigned char task_idx) {
    unsigned char indent, k;
    indent = get_indent(task_idx);
    for (k = task_idx + 1; k < task_count; ++k) {
        if (get_indent(k) < indent) return 0;  /* parent found - no more siblings */
        if (get_indent(k) == indent) return 1;  /* sibling found */
    }
    return 0;
}

/* Check if an ancestor at `anc_indent` level has continuing siblings below `task_idx` */
static unsigned char ancestor_continues(unsigned char task_idx, unsigned char anc_indent) {
    unsigned char k;
    for (k = task_idx + 1; k < task_count; ++k) {
        if (get_indent(k) < anc_indent) return 0;
        if (get_indent(k) == anc_indent) return 1;
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * Drawing
 *---------------------------------------------------------------------------*/

static void draw_header(void) {
    TuiRect header;
    unsigned char done_count, ti;

    header.x = 0;
    header.y = HEADER_Y;
    header.w = 40;
    header.h = 2;
    tui_window(&header, TUI_COLOR_LIGHTBLUE);

    tui_puts(1, HEADER_Y, "TASKS:", TUI_COLOR_WHITE);
    tui_puts_n(8, HEADER_Y, filename, 14, TUI_COLOR_YELLOW);

    /* Count done tasks */
    done_count = 0;
    for (ti = 0; ti < task_count; ++ti) {
        if (is_done(ti)) ++done_count;
    }

    /* Show done/total right-aligned */
    tui_puts_n(26, HEADER_Y, "", 12, TUI_COLOR_CYAN);
    tui_print_uint(28, HEADER_Y, done_count, TUI_COLOR_CYAN);
    tui_puts(31, HEADER_Y, "/", TUI_COLOR_WHITE);
    tui_print_uint(32, HEADER_Y, task_count, TUI_COLOR_CYAN);
}

static void render_text_with_hashtags(unsigned char x, unsigned char y,
                                      const char *text, unsigned char maxw,
                                      unsigned char base_color,
                                      unsigned char hashtag_color) {
    unsigned int offset;
    unsigned char col, ch, color, in_hashtag;
    unsigned char len;

    offset = (unsigned int)y * 40 + x;
    len = strlen(text);
    in_hashtag = 0;

    for (col = 0; col < maxw; ++col) {
        if (col < len) {
            ch = text[col];

            /* Check for truncation */
            if (col >= maxw - 2 && len > maxw) {
                /* Show ".." at end */
                TUI_SCREEN[offset + col] = 46;  /* '.' screen code */
                TUI_COLOR_RAM[offset + col] = base_color;
                ++col;
                if (col < maxw) {
                    TUI_SCREEN[offset + col] = 46;
                    TUI_COLOR_RAM[offset + col] = base_color;
                }
                /* Fill remaining with spaces */
                for (++col; col < maxw; ++col) {
                    TUI_SCREEN[offset + col] = 32;
                    TUI_COLOR_RAM[offset + col] = base_color;
                }
                return;
            }

            /* Hashtag coloring */
            if (ch == '#') {
                in_hashtag = 1;
                color = hashtag_color;
            } else if (in_hashtag) {
                if (ch == ' ' || ch == ',' || ch == '.' || ch == '!' || ch == '?') {
                    in_hashtag = 0;
                    color = base_color;
                } else {
                    color = hashtag_color;
                }
            } else {
                color = base_color;
            }

            TUI_SCREEN[offset + col] = tui_ascii_to_screen(ch);
            TUI_COLOR_RAM[offset + col] = color;
        } else {
            TUI_SCREEN[offset + col] = 32;
            TUI_COLOR_RAM[offset + col] = base_color;
        }
    }
}

static void draw_task_line(unsigned char view_idx) {
    unsigned char screen_y, task_idx, indent, col;
    unsigned char text_color, is_selected;
    unsigned char avail_width;
    unsigned int offset;
    unsigned char lev;

    if (view_idx < scroll_y || view_idx >= scroll_y + LIST_HEIGHT) return;
    screen_y = LIST_START_Y + (view_idx - scroll_y);
    offset = (unsigned int)screen_y * 40;

    if (view_idx >= view_count) {
        /* Empty row */
        tui_clear_line(screen_y, 0, 40, TUI_COLOR_WHITE);
        return;
    }

    task_idx = view_list[view_idx];
    indent = get_indent(task_idx);
    is_selected = (view_idx == cursor_pos);

    /* Determine text color */
    if (is_done(task_idx)) {
        text_color = TUI_COLOR_GRAY2;
    } else if (view_is_filtered && view_flags[view_idx]) {
        text_color = TUI_COLOR_GRAY3;  /* Parent-only in filter */
    } else {
        text_color = indent_colors[indent & 7];
    }

    /* Col 0: Note indicator */
    if (has_note(task_idx)) {
        TUI_SCREEN[offset] = NOTE_STAR;
        TUI_COLOR_RAM[offset] = TUI_COLOR_YELLOW;
    } else {
        TUI_SCREEN[offset] = 32;
        TUI_COLOR_RAM[offset] = text_color;
    }

    /* Col 1: Checkbox */
    if (is_done(task_idx)) {
        TUI_SCREEN[offset + 1] = CHECK_DONE;
        TUI_COLOR_RAM[offset + 1] = TUI_COLOR_GRAY2;
    } else {
        TUI_SCREEN[offset + 1] = CHECK_ACTIVE;
        TUI_COLOR_RAM[offset + 1] = text_color;
    }

    /* Col 2: Space */
    TUI_SCREEN[offset + 2] = 32;
    TUI_COLOR_RAM[offset + 2] = text_color;

    /* Col 3+: Tree indent chars (2 chars per level) */
    col = 3;
    for (lev = 0; lev < indent && col < 38; ++lev) {
        if (lev == indent - 1) {
            /* This task's own connector */
            if (has_sibling_below(task_idx)) {
                TUI_SCREEN[offset + col] = TREE_BRANCH;
            } else {
                TUI_SCREEN[offset + col] = TREE_LAST;
            }
            TUI_COLOR_RAM[offset + col] = text_color;
            ++col;
            if (col < 40) {
                TUI_SCREEN[offset + col] = TREE_HORIZ;
                TUI_COLOR_RAM[offset + col] = text_color;
                ++col;
            }
        } else {
            /* Ancestor level: show vertical bar if ancestor continues */
            if (ancestor_continues(task_idx, lev + 1)) {
                TUI_SCREEN[offset + col] = TREE_VERT;
            } else {
                TUI_SCREEN[offset + col] = 32;
            }
            TUI_COLOR_RAM[offset + col] = text_color;
            ++col;
            if (col < 40) {
                TUI_SCREEN[offset + col] = 32;
                TUI_COLOR_RAM[offset + col] = text_color;
                ++col;
            }
        }
    }

    /* Remaining columns: task text with hashtag coloring */
    avail_width = 40 - col;
    if (avail_width > 0) {
        render_text_with_hashtags(col, screen_y, task_text[task_idx],
                                  avail_width, text_color,
                                  is_done(task_idx) ? TUI_COLOR_GRAY3
                                                    : TUI_COLOR_GREEN);
    }

    /* Apply reverse video for selection */
    if (is_selected) {
        for (col = 0; col < 40; ++col) {
            TUI_SCREEN[offset + col] |= 0x80;
        }
    }
}

static void draw_visible_tasks(void) {
    unsigned char row;
    for (row = 0; row < LIST_HEIGHT; ++row) {
        if (scroll_y + row < view_count) {
            draw_task_line(scroll_y + row);
        } else {
            tui_clear_line(LIST_START_Y + row, 0, 40, TUI_COLOR_WHITE);
        }
    }
}

static void draw_drive_status(void) {
    tui_puts(31, STATUS_Y, "D:", TUI_COLOR_GRAY3);
    tui_puts_n(33, STATUS_Y, "", 2, TUI_COLOR_CYAN);
    tui_print_uint(33, STATUS_Y, storage_device_get_default(), TUI_COLOR_CYAN);
}

static void draw_status(void) {
    /* Modified indicator */
    if (modified) {
        tui_puts_n(1, STATUS_Y, "*MODIFIED*", 12, TUI_COLOR_LIGHTRED);
    } else {
        tui_puts_n(1, STATUS_Y, "", 12, TUI_COLOR_WHITE);
    }

    /* Item count */
    tui_puts_n(15, STATUS_Y, "", 25, TUI_COLOR_GRAY3);
    tui_print_uint(15, STATUS_Y, task_count, TUI_COLOR_GRAY3);
    tui_puts(19, STATUS_Y, "ITEMS", TUI_COLOR_GRAY3);
    draw_drive_status();
}

static void draw_help(void) {
    tui_puts(0, HELP_Y1, "]:IN [:OUT SP:DONE E:EDIT N:NOTE /:S",
             TUI_COLOR_GRAY3);
    tui_puts(0, HELP_Y2, "F1:CPY F3:PST F5:SAV F6:SA F7:OPN F8:H",
             TUI_COLOR_GRAY3);
}

static void draw_search_bar(void) {
    if (search_active) {
        tui_puts(0, SEARCH_Y, "/SEARCH:", TUI_COLOR_YELLOW);
        tui_puts_n(9, SEARCH_Y, search_buf, 30, TUI_COLOR_CYAN);
    } else {
        tui_clear_line(SEARCH_Y, 0, 40, TUI_COLOR_WHITE);
    }
}

static void tasklist_draw(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    draw_visible_tasks();
    draw_search_bar();
    draw_status();
    draw_help();
}

/*---------------------------------------------------------------------------
 * Task manipulation
 *---------------------------------------------------------------------------*/

static void insert_task(unsigned char after_view_pos) {
    unsigned char insert_idx, i, indent;

    if (task_count >= MAX_TASKS) return;

    /* Determine actual position in task array */
    if (view_count == 0 || task_count == 0) {
        insert_idx = 0;
    } else if (after_view_pos < view_count) {
        insert_idx = view_list[after_view_pos] + 1;
    } else {
        insert_idx = task_count;
    }

    /* Shift tasks down */
    for (i = task_count; i > insert_idx; --i) {
        strcpy(task_text[i], task_text[i - 1]);
        task_flags[i] = task_flags[i - 1];
        note_offset[i] = note_offset[i - 1];
        note_length[i] = note_length[i - 1];
    }

    /* Initialize new task */
    task_text[insert_idx][0] = 0;
    task_flags[insert_idx] = 0;
    note_offset[insert_idx] = 0xFFFF;
    note_length[insert_idx] = 0;

    /* Match indent of task above */
    if (insert_idx > 0) {
        indent = get_indent(insert_idx - 1);
        set_indent(insert_idx, indent);
    }

    ++task_count;
    modified = 1;

    /* Rebuild view and position cursor on new task */
    rebuild_view();

    /* Find the new task in view */
    for (i = 0; i < view_count; ++i) {
        if (view_list[i] == insert_idx) {
            cursor_pos = i;
            break;
        }
    }
    clamp_cursor();
}

static void delete_task(unsigned char view_pos) {
    unsigned char task_idx, i;

    if (view_count == 0 || view_pos >= view_count) return;
    task_idx = view_list[view_pos];

    /* Confirm if task has content */
    if (task_text[task_idx][0] != 0) {
        if (!show_confirm("DELETE TASK?")) {
            tasklist_draw();
            return;
        }
    }

    /* Free note if any */
    note_offset[task_idx] = 0xFFFF;
    note_length[task_idx] = 0;

    /* Shift tasks up */
    for (i = task_idx; i < task_count - 1; ++i) {
        strcpy(task_text[i], task_text[i + 1]);
        task_flags[i] = task_flags[i + 1];
        note_offset[i] = note_offset[i + 1];
        note_length[i] = note_length[i + 1];
    }

    --task_count;
    if (task_count == 0) {
        task_count = 1;
        task_text[0][0] = 0;
        task_flags[0] = 0;
        note_offset[0] = 0xFFFF;
        note_length[0] = 0;
    }

    modified = 1;
    note_compact();
    rebuild_view();
    tasklist_draw();
}

static void toggle_done(unsigned char view_pos) {
    unsigned char task_idx;
    if (view_pos >= view_count) return;
    task_idx = view_list[view_pos];
    task_flags[task_idx] ^= TASK_FLAG_DONE;
    modified = 1;
}

static void indent_task(unsigned char view_pos) {
    unsigned char task_idx, level;
    if (view_pos >= view_count) return;
    task_idx = view_list[view_pos];
    level = get_indent(task_idx);

    /* Can't indent beyond MAX_INDENT or more than 1 deeper than previous task */
    if (level >= MAX_INDENT) return;
    if (task_idx > 0) {
        if (level >= get_indent(task_idx - 1) + 1) return;
    } else {
        /* First task can't be indented */
        return;
    }

    set_indent(task_idx, level + 1);
    modified = 1;
}

static void outdent_task(unsigned char view_pos) {
    unsigned char task_idx, level;
    if (view_pos >= view_count) return;
    task_idx = view_list[view_pos];
    level = get_indent(task_idx);
    if (level == 0) return;

    set_indent(task_idx, level - 1);
    modified = 1;
}

/*---------------------------------------------------------------------------
 * Edit task popup (Phase 4)
 *---------------------------------------------------------------------------*/

static void edit_task_popup(unsigned char view_pos) {
    TuiRect win;
    TuiInput input;
    unsigned char key, task_idx;

    if (view_pos >= view_count) return;
    task_idx = view_list[view_pos];

    win.x = 1;
    win.y = 8;
    win.w = 38;
    win.h = 7;
    tui_window_title(&win, "EDIT TASK", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_input_init(&input, 2, 10, 36, MAX_TASK_LEN, edit_buf, TUI_COLOR_CYAN);
    /* Copy current text to edit buffer (after init clears it) */
    strncpy(edit_buf, task_text[task_idx], MAX_TASK_LEN);
    edit_buf[MAX_TASK_LEN] = 0;
    input.cursor = strlen(edit_buf);
    tui_input_draw(&input);

    tui_puts(2, 13, "RET:SAVE  STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();

        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            break;  /* Cancel */
        }

        if (tui_input_key(&input, key)) {
            /* RETURN pressed - save */
            strcpy(task_text[task_idx], edit_buf);
            modified = 1;
            break;
        }

        tui_input_draw(&input);
    }
}

/*---------------------------------------------------------------------------
 * Note editor popup (Phase 6)
 *---------------------------------------------------------------------------*/

static void note_load_to_edit(unsigned char task_idx) {
    unsigned int off, end, col, line;
    unsigned char ch;

    memset(shared.note.lines, 0, sizeof(shared.note.lines));
    shared.note.line_count = 1;
    shared.note.cx = 0;
    shared.note.cy = 0;
    shared.note.scroll = 0;

    if (note_offset[task_idx] == 0xFFFF || note_length[task_idx] == 0) {
        shared.note.lines[0][0] = 0;
        return;
    }

    off = note_offset[task_idx];
    end = off + note_length[task_idx];
    line = 0;
    col = 0;

    while (off < end && off < NOTE_POOL_SIZE) {
        ch = note_pool[off];
        if (ch == '\n') {
            shared.note.lines[line][col] = 0;
            ++line;
            col = 0;
            if (line >= MAX_NOTE_LINES) break;
        } else {
            if (col < MAX_NOTE_LINE_LEN) {
                shared.note.lines[line][col] = ch;
                ++col;
            }
        }
        ++off;
    }
    shared.note.lines[line][col] = 0;
    shared.note.line_count = line + 1;
}

static void note_save_from_edit(unsigned char task_idx) {
    unsigned int new_len, off;
    unsigned char line, col;

    /* Calculate needed size */
    new_len = 0;
    for (line = 0; line < shared.note.line_count; ++line) {
        new_len += strlen(shared.note.lines[line]);
        if (line < shared.note.line_count - 1) ++new_len;  /* \n separator */
    }

    /* Check if note is empty */
    if (new_len == 0 || (shared.note.line_count == 1 && shared.note.lines[0][0] == 0)) {
        note_offset[task_idx] = 0xFFFF;
        note_length[task_idx] = 0;
        task_flags[task_idx] &= ~TASK_FLAG_HAS_NOTE;
        note_compact();
        return;
    }

    /* Invalidate old note */
    note_offset[task_idx] = 0xFFFF;
    note_length[task_idx] = 0;
    note_compact();

    /* Check if pool has room */
    if (note_pool_used + new_len > NOTE_POOL_SIZE) {
        /* Not enough room - truncate */
        new_len = NOTE_POOL_SIZE - note_pool_used;
        if (new_len == 0) return;
    }

    /* Write to pool */
    off = note_pool_used;
    note_offset[task_idx] = off;
    for (line = 0; line < shared.note.line_count; ++line) {
        for (col = 0; shared.note.lines[line][col] && off < NOTE_POOL_SIZE; ++col) {
            note_pool[off++] = shared.note.lines[line][col];
        }
        if (line < shared.note.line_count - 1 && off < NOTE_POOL_SIZE) {
            note_pool[off++] = '\n';
        }
    }
    note_length[task_idx] = off - note_offset[task_idx];
    note_pool_used = off;
    task_flags[task_idx] |= TASK_FLAG_HAS_NOTE;
    modified = 1;
}

static void note_draw_lines(void) {
    unsigned char row, line_idx, screen_y;
    unsigned int offset;
    unsigned char col, len;

    for (row = 0; row < 16; ++row) {
        screen_y = 5 + row;
        line_idx = shared.note.scroll + row;
        offset = (unsigned int)screen_y * 40 + 2;

        if (line_idx < shared.note.line_count) {
            len = strlen(shared.note.lines[line_idx]);
            for (col = 0; col < 36; ++col) {
                if (col < len) {
                    TUI_SCREEN[offset + col] = tui_ascii_to_screen(shared.note.lines[line_idx][col]);
                } else {
                    TUI_SCREEN[offset + col] = 32;
                }
                TUI_COLOR_RAM[offset + col] = TUI_COLOR_WHITE;
            }

            /* Show cursor */
            if (line_idx == shared.note.cy) {
                if (shared.note.cx < 36) {
                    TUI_SCREEN[offset + shared.note.cx] |= 0x80;
                }
            }
        } else {
            for (col = 0; col < 36; ++col) {
                TUI_SCREEN[offset + col] = 32;
                TUI_COLOR_RAM[offset + col] = TUI_COLOR_GRAY2;
            }
        }
    }
}

static void note_edit_popup(unsigned char view_pos) {
    TuiRect win;
    unsigned char key, task_idx, len;

    if (view_pos >= view_count) return;
    task_idx = view_list[view_pos];

    /* Load note into edit buffer */
    note_load_to_edit(task_idx);

    /* Draw popup window */
    win.x = 1;
    win.y = 3;
    win.w = 38;
    win.h = 20;
    tui_window_title(&win, "NOTE", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    /* Show truncated task text */
    tui_puts_n(2, 4, task_text[task_idx], 36, TUI_COLOR_GRAY3);

    tui_puts(1, HELP_Y1, "STOP:DONE  DEL:BACKSPACE             ", TUI_COLOR_GRAY3);
    tui_clear_line(HELP_Y2, 0, 40, TUI_COLOR_GRAY3);

    note_draw_lines();

    while (1) {
        key = tui_getkey();

        if (key == TUI_KEY_RUNSTOP) {
            /* Save and exit */
            note_save_from_edit(task_idx);
            break;
        }

        if (key == TUI_KEY_UP) {
            if (shared.note.cy > 0) {
                --shared.note.cy;
                len = strlen(shared.note.lines[shared.note.cy]);
                if (shared.note.cx > len) shared.note.cx = len;
                if (shared.note.cy < shared.note.scroll) {
                    shared.note.scroll = shared.note.cy;
                }
            }
        } else if (key == TUI_KEY_DOWN) {
            if (shared.note.cy < shared.note.line_count - 1) {
                ++shared.note.cy;
                len = strlen(shared.note.lines[shared.note.cy]);
                if (shared.note.cx > len) shared.note.cx = len;
                if (shared.note.cy >= shared.note.scroll + 16) {
                    shared.note.scroll = shared.note.cy - 15;
                }
            }
        } else if (key == TUI_KEY_LEFT) {
            if (shared.note.cx > 0) --shared.note.cx;
        } else if (key == TUI_KEY_RIGHT) {
            len = strlen(shared.note.lines[shared.note.cy]);
            if (shared.note.cx < len) ++shared.note.cx;
        } else if (key == TUI_KEY_RETURN) {
            /* New line */
            if (shared.note.line_count < MAX_NOTE_LINES) {
                unsigned char nl;
                /* Shift lines down */
                for (nl = shared.note.line_count; nl > shared.note.cy + 1; --nl) {
                    strcpy(shared.note.lines[nl], shared.note.lines[nl - 1]);
                }
                /* Split current line */
                len = strlen(shared.note.lines[shared.note.cy]);
                strcpy(shared.note.lines[shared.note.cy + 1],
                       &shared.note.lines[shared.note.cy][shared.note.cx]);
                shared.note.lines[shared.note.cy][shared.note.cx] = 0;
                ++shared.note.line_count;
                ++shared.note.cy;
                shared.note.cx = 0;
                if (shared.note.cy >= shared.note.scroll + 16) {
                    shared.note.scroll = shared.note.cy - 15;
                }
            }
        } else if (key == TUI_KEY_DEL) {
            if (shared.note.cx > 0) {
                /* Delete char before cursor */
                unsigned char di;
                len = strlen(shared.note.lines[shared.note.cy]);
                for (di = shared.note.cx - 1; di < len; ++di) {
                    shared.note.lines[shared.note.cy][di] =
                        shared.note.lines[shared.note.cy][di + 1];
                }
                --shared.note.cx;
            } else if (shared.note.cy > 0) {
                /* Join with previous line */
                unsigned char prev_len = strlen(shared.note.lines[shared.note.cy - 1]);
                unsigned char cur_len = strlen(shared.note.lines[shared.note.cy]);
                if (prev_len + cur_len < MAX_NOTE_LINE_LEN) {
                    unsigned char nl;
                    strcat(shared.note.lines[shared.note.cy - 1],
                           shared.note.lines[shared.note.cy]);
                    for (nl = shared.note.cy; nl < shared.note.line_count - 1; ++nl) {
                        strcpy(shared.note.lines[nl], shared.note.lines[nl + 1]);
                    }
                    --shared.note.line_count;
                    --shared.note.cy;
                    shared.note.cx = prev_len;
                }
            }
        } else if (key >= 32 && key < 128) {
            /* Insert character */
            len = strlen(shared.note.lines[shared.note.cy]);
            if (len < MAX_NOTE_LINE_LEN - 1) {
                unsigned char ci;
                for (ci = len + 1; ci > shared.note.cx; --ci) {
                    shared.note.lines[shared.note.cy][ci] =
                        shared.note.lines[shared.note.cy][ci - 1];
                }
                shared.note.lines[shared.note.cy][shared.note.cx] = key;
                ++shared.note.cx;
            }
        }

        note_draw_lines();
    }
}

static void note_compact(void) {
    unsigned int new_used;
    unsigned char i;
    unsigned int src_off, dst_off;

    new_used = 0;

    /* First pass: calculate compacted positions */
    for (i = 0; i < task_count; ++i) {
        if (note_offset[i] != 0xFFFF && note_length[i] > 0) {
            new_used += note_length[i];
        }
    }

    if (new_used == note_pool_used) return;  /* No fragmentation */

    /* Second pass: compact in-place */
    dst_off = 0;
    for (i = 0; i < task_count; ++i) {
        if (note_offset[i] != 0xFFFF && note_length[i] > 0) {
            src_off = note_offset[i];
            if (src_off != dst_off) {
                memmove(&note_pool[dst_off], &note_pool[src_off], note_length[i]);
            }
            note_offset[i] = dst_off;
            dst_off += note_length[i];
        }
    }

    note_pool_used = dst_off;
}

/*---------------------------------------------------------------------------
 * File I/O (Phase 5)
 *---------------------------------------------------------------------------*/

static void build_dir_display(unsigned char idx) {
    unsigned char i;
    unsigned char len;
    const char *type_text;

    strcpy(shared.file.browser.dir_display[idx],
           shared.file.browser.dir_entries[idx].name);
    len = strlen(shared.file.browser.dir_display[idx]);

    for (i = len; i < 17u; ++i) {
        shared.file.browser.dir_display[idx][i] = ' ';
    }

    type_text = dir_page_type_text(shared.file.browser.dir_entries[idx].type);
    shared.file.browser.dir_display[idx][17] = type_text[0];
    shared.file.browser.dir_display[idx][18] = type_text[1];
    shared.file.browser.dir_display[idx][19] = type_text[2];
    shared.file.browser.dir_display[idx][20] = 0;
}

static unsigned char read_directory(unsigned char start_index,
                                    unsigned char *out_total) {
    unsigned char idx;

    shared.file.browser.dir_count = 0;
    if (dir_page_read(storage_device_get_default(),
                      start_index,
                      DIR_PAGE_TYPE_ANY,
                      shared.file.browser.dir_entries,
                      MAX_DIR_ENTRIES,
                      &shared.file.browser.dir_count,
                      out_total) != DIR_PAGE_RC_OK) {
        if (out_total != 0) {
            *out_total = 0u;
        }
        return 0u;
    }

    for (idx = 0u; idx < shared.file.browser.dir_count; ++idx) {
        build_dir_display(idx);
        shared.file.browser.dir_ptrs[idx] = shared.file.browser.dir_display[idx];
    }

    return 1u;
}

static unsigned char show_open_dialog(DirPageEntry *out_entry) {
    TuiRect win;
    TuiMenu menu;
    unsigned char key;
    unsigned char menu_ready;
    unsigned char selected;
    unsigned char page_start;
    unsigned char total_count;
    unsigned char rel_index;

    selected = 0u;
    page_start = 0u;
    total_count = 0u;

    while (1) {
        tui_clear(TUI_COLOR_BLUE);

        win.x = 0;
        win.y = 0;
        win.w = 40;
        win.h = 24;
        tui_window_title(&win, "OPEN FILE", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

        tui_puts(10, 11, "READING DISK...", TUI_COLOR_YELLOW);
        (void)read_directory(page_start, &total_count);
        tui_clear_line(11, 1, 38, TUI_COLOR_WHITE);

        tui_clear_line(22, 1, 38, TUI_COLOR_GRAY3);
        tui_puts(1, 22, "DRIVE:", TUI_COLOR_GRAY3);
        tui_print_uint(8, 22, storage_device_get_default(), TUI_COLOR_CYAN);

        menu_ready = 0u;
        if (total_count == 0u || shared.file.browser.dir_count == 0u) {
            tui_puts(7, 10, "NO FILES FOUND ON DISK", TUI_COLOR_LIGHTRED);
            tui_puts(1, 24, "F3:DRV RET:OPEN STOP:CANCEL", TUI_COLOR_GRAY3);
        } else {
            tui_print_uint(12, 22, total_count, TUI_COLOR_GRAY3);
            tui_puts(15, 22, "FILE(S)", TUI_COLOR_GRAY3);
            tui_puts(1, 24, "UP/DN SEL F3:DRV RET:OPEN STOP", TUI_COLOR_GRAY3);

            tui_menu_init(&menu, 1, 2, 38, 18,
                          shared.file.browser.dir_ptrs,
                          shared.file.browser.dir_count);
            menu.selected = (unsigned char)(selected - page_start);
            menu.item_color = TUI_COLOR_WHITE;
            menu.sel_color = TUI_COLOR_CYAN;
            tui_menu_draw(&menu);
            menu_ready = 1u;
        }

        while (1) {
            key = tui_getkey();

            if (key == TUI_KEY_F3) {
                storage_device_set_default(
                    storage_device_toggle_8_9(storage_device_get_default()));
                selected = 0u;
                page_start = 0u;
                break;
            }

            if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
                return OPEN_DIALOG_RC_CANCEL;
            }

            if (!menu_ready) {
                continue;
            }

            if (key == TUI_KEY_RETURN) {
                rel_index = (unsigned char)(selected - page_start);
                strcpy(out_entry->name, shared.file.browser.dir_entries[rel_index].name);
                out_entry->type = shared.file.browser.dir_entries[rel_index].type;
                return OPEN_DIALOG_RC_OK;
            }

            if (key == TUI_KEY_HOME) {
                if (selected != 0u || page_start != 0u) {
                    selected = 0u;
                    page_start = 0u;
                    break;
                }
                continue;
            }

            if (key == TUI_KEY_UP) {
                if (selected == 0u) {
                    continue;
                }
                --selected;
                if (selected < page_start) {
                    page_start = (unsigned char)(page_start - MAX_DIR_ENTRIES);
                    break;
                }
                menu.selected = (unsigned char)(selected - page_start);
                tui_menu_draw(&menu);
                continue;
            }

            if (key == TUI_KEY_DOWN) {
                if ((unsigned char)(selected + 1u) >= total_count) {
                    continue;
                }
                ++selected;
                if (selected >= (unsigned char)(page_start + shared.file.browser.dir_count)) {
                    page_start = (unsigned char)(page_start + MAX_DIR_ENTRIES);
                    break;
                }
                menu.selected = (unsigned char)(selected - page_start);
                tui_menu_draw(&menu);
            }
        }
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

    tui_input_init(&input, 7, 11, 20, 16, shared.file.save_buf, TUI_COLOR_CYAN);

    if (strcmp(filename, "UNTITLED") != 0) {
        strcpy(shared.file.save_buf, filename);
        input.cursor = strlen(shared.file.save_buf);
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
            if (shared.file.save_buf[0] == 0) continue;

            tui_puts(7, 12, "SAVING...", TUI_COLOR_YELLOW);
            if (file_save(shared.file.save_buf) != 0) {
                show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                return 0;
            }
            return 1;
        }

        tui_input_draw(&input);
    }
}

static unsigned char file_load(const char *name) {
    static char open_str[24];
    static char io_buf[IO_BUF_SIZE];
    unsigned char ti;     /* task index */
    unsigned char col;
    int n;
    unsigned char bi;     /* buffer index */
    unsigned char ch;
    unsigned char indent_ch, done_ch;

    strcpy(open_str, name);
    strcat(open_str, ",s,r");

    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return 1;
    }

    /* Reset all data */
    task_count = 0;
    note_pool_used = 0;
    memset(task_flags, 0, MAX_TASKS);
    for (ti = 0; ti < MAX_TASKS; ++ti) {
        task_text[ti][0] = 0;
        note_offset[ti] = 0xFFFF;
        note_length[ti] = 0;
    }

    ti = 0;
    col = 0;

    /* State machine for parsing:
     * Each line is either:
     *   <indent><done> <text>CR   (task line, first 2 chars are digits)
     *   >note text CR             (note line for previous task)
     */
    {
        /* Line buffer for parsing one line at a time */
        static char line_buf[MAX_TASK_LEN + 2];
        unsigned char lpos = 0;

        while (1) {
            n = cbm_read(LFN_FILE, io_buf, IO_BUF_SIZE);
            if (n <= 0) break;

            for (bi = 0; bi < (unsigned char)n; ++bi) {
                ch = io_buf[bi];
                if (ch == CR) {
                    line_buf[lpos] = 0;

                    if (lpos > 0 && line_buf[0] == '>') {
                        /* Note line for current task */
                        if (ti > 0) {
                            unsigned char prev = ti - 1;
                            unsigned char nlen = lpos - 1;  /* skip '>' */

                            if (note_offset[prev] == 0xFFFF) {
                                note_offset[prev] = note_pool_used;
                                note_length[prev] = 0;
                            } else {
                                /* Add newline separator */
                                if (note_pool_used < NOTE_POOL_SIZE) {
                                    note_pool[note_pool_used++] = '\n';
                                    note_length[prev]++;
                                }
                            }

                            /* Copy note text */
                            {
                                unsigned char nc;
                                for (nc = 0; nc < nlen && note_pool_used < NOTE_POOL_SIZE; ++nc) {
                                    note_pool[note_pool_used++] = line_buf[1 + nc];
                                    note_length[prev]++;
                                }
                            }
                            task_flags[prev] |= TASK_FLAG_HAS_NOTE;
                        }
                    } else if (lpos >= 3 && ti < MAX_TASKS) {
                        /* Task line: first char is indent, second is done, third is space */
                        indent_ch = line_buf[0] - '0';
                        done_ch = line_buf[1] - '0';

                        if (indent_ch > MAX_INDENT) indent_ch = 0;

                        /* Copy task text (from pos 3 onward) */
                        if (lpos > 3) {
                            strncpy(task_text[ti], &line_buf[3], MAX_TASK_LEN);
                            task_text[ti][MAX_TASK_LEN] = 0;
                        } else {
                            task_text[ti][0] = 0;
                        }

                        task_flags[ti] = 0;
                        set_indent(ti, indent_ch);
                        if (done_ch) task_flags[ti] |= TASK_FLAG_DONE;
                        note_offset[ti] = 0xFFFF;
                        note_length[ti] = 0;

                        ++ti;
                    }

                    lpos = 0;
                } else {
                    if (lpos < MAX_TASK_LEN + 1) {
                        line_buf[lpos++] = ch;
                    }
                }
            }
        }

        /* Handle last line without trailing CR */
        if (lpos > 0) {
            line_buf[lpos] = 0;
            if (line_buf[0] == '>' && ti > 0) {
                unsigned char prev = ti - 1;
                unsigned char nlen = lpos - 1;
                if (note_offset[prev] == 0xFFFF) {
                    note_offset[prev] = note_pool_used;
                    note_length[prev] = 0;
                } else if (note_pool_used < NOTE_POOL_SIZE) {
                    note_pool[note_pool_used++] = '\n';
                    note_length[prev]++;
                }
                {
                    unsigned char nc;
                    for (nc = 0; nc < nlen && note_pool_used < NOTE_POOL_SIZE; ++nc) {
                        note_pool[note_pool_used++] = line_buf[1 + nc];
                        note_length[prev]++;
                    }
                }
                task_flags[prev] |= TASK_FLAG_HAS_NOTE;
            } else if (lpos >= 3 && ti < MAX_TASKS) {
                indent_ch = line_buf[0] - '0';
                done_ch = line_buf[1] - '0';
                if (indent_ch > MAX_INDENT) indent_ch = 0;
                if (lpos > 3) {
                    strncpy(task_text[ti], &line_buf[3], MAX_TASK_LEN);
                    task_text[ti][MAX_TASK_LEN] = 0;
                } else {
                    task_text[ti][0] = 0;
                }
                task_flags[ti] = 0;
                set_indent(ti, indent_ch);
                if (done_ch) task_flags[ti] |= TASK_FLAG_DONE;
                note_offset[ti] = 0xFFFF;
                note_length[ti] = 0;
                ++ti;
            }
        }
    }

    cbm_close(LFN_FILE);

    task_count = ti;
    if (task_count == 0) {
        task_count = 1;
        task_text[0][0] = 0;
        task_flags[0] = 0;
        note_offset[0] = 0xFFFF;
        note_length[0] = 0;
    }

    strncpy(filename, name, 15);
    filename[15] = 0;
    modified = 0;
    cursor_pos = 0;
    scroll_y = 0;
    search_active = 0;
    search_buf[0] = 0;

    rebuild_view();
    return 0;
}

static unsigned char file_save(const char *name) {
    static char cmd_str[24];
    static char open_str[24];
    unsigned char cr_byte;
    unsigned char ti, len;
    unsigned char indent, done;
    static char prefix[4];

    cr_byte = CR;

    /* Scratch existing file */
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

    for (ti = 0; ti < task_count; ++ti) {
        indent = get_indent(ti);
        done = is_done(ti) ? 1 : 0;

        /* Write prefix: indent digit, done digit, space */
        prefix[0] = '0' + indent;
        prefix[1] = '0' + done;
        prefix[2] = ' ';
        prefix[3] = 0;
        cbm_write(LFN_FILE, prefix, 3);

        /* Write task text */
        len = strlen(task_text[ti]);
        if (len > 0) {
            cbm_write(LFN_FILE, task_text[ti], len);
        }
        cbm_write(LFN_FILE, &cr_byte, 1);

        /* Write note lines if any */
        if (has_note(ti) && note_offset[ti] != 0xFFFF && note_length[ti] > 0) {
            unsigned int noff = note_offset[ti];
            unsigned int nend = noff + note_length[ti];
            static char gt = '>';

            while (noff < nend && noff < NOTE_POOL_SIZE) {
                /* Write '>' prefix */
                cbm_write(LFN_FILE, &gt, 1);

                /* Write note line until \n or end */
                {
                    unsigned int line_start = noff;
                    while (noff < nend && noff < NOTE_POOL_SIZE && note_pool[noff] != '\n') {
                        ++noff;
                    }
                    if (noff > line_start) {
                        cbm_write(LFN_FILE, &note_pool[line_start], noff - line_start);
                    }
                }

                cbm_write(LFN_FILE, &cr_byte, 1);

                /* Skip \n */
                if (noff < nend && note_pool[noff] == '\n') ++noff;
            }
        }
    }

    cbm_close(LFN_FILE);

    strncpy(filename, name, 15);
    filename[15] = 0;
    modified = 0;

    return 0;
}

/*---------------------------------------------------------------------------
 * Clipboard (Phase 8)
 *---------------------------------------------------------------------------*/

static void copy_task_to_clipboard(unsigned char view_pos) {
    static char clip_buf[200];
    unsigned char task_idx;
    unsigned int len, nlen;

    if (view_pos >= view_count) return;
    task_idx = view_list[view_pos];

    /* Start with task text */
    len = strlen(task_text[task_idx]);
    if (len > 190) len = 190;
    memcpy(clip_buf, task_text[task_idx], len);

    /* Append note if present */
    if (has_note(task_idx) && note_offset[task_idx] != 0xFFFF && note_length[task_idx] > 0) {
        nlen = note_length[task_idx];
        if (len + 5 + nlen > 199) {
            nlen = 199 - len - 5;
        }
        clip_buf[len++] = '\n';
        clip_buf[len++] = '-';
        clip_buf[len++] = '-';
        clip_buf[len++] = '-';
        clip_buf[len++] = '\n';
        memcpy(&clip_buf[len], &note_pool[note_offset[task_idx]], nlen);
        len += nlen;
    }

    clip_buf[len] = 0;
    clip_copy(CLIP_TYPE_TEXT, clip_buf, len);
}

static void paste_from_clipboard(unsigned char view_pos) {
    static char paste_buf[200];
    unsigned int plen;
    unsigned char insert_idx, i;
    unsigned char indent;
    char *sep;

    if (clip_item_count() == 0) return;
    plen = clip_paste(0, paste_buf, 199);
    if (plen == 0) return;
    paste_buf[plen] = 0;

    if (task_count >= MAX_TASKS) return;

    /* Determine insert position */
    if (view_count == 0 || task_count == 0) {
        insert_idx = 0;
    } else if (view_pos < view_count) {
        insert_idx = view_list[view_pos] + 1;
    } else {
        insert_idx = task_count;
    }

    /* Shift tasks down */
    for (i = task_count; i > insert_idx; --i) {
        strcpy(task_text[i], task_text[i - 1]);
        task_flags[i] = task_flags[i - 1];
        note_offset[i] = note_offset[i - 1];
        note_length[i] = note_length[i - 1];
    }

    /* Initialize new task */
    task_text[insert_idx][0] = 0;
    task_flags[insert_idx] = 0;
    note_offset[insert_idx] = 0xFFFF;
    note_length[insert_idx] = 0;

    /* Match indent of task above */
    if (insert_idx > 0) {
        indent = get_indent(insert_idx - 1);
        set_indent(insert_idx, indent);
    }

    ++task_count;

    /* Check for note separator: \n---\n */
    sep = 0;
    for (i = 0; i < plen - 4; ++i) {
        if (paste_buf[i] == '\n' && paste_buf[i+1] == '-' && paste_buf[i+2] == '-'
            && paste_buf[i+3] == '-' && paste_buf[i+4] == '\n') {
            sep = &paste_buf[i];
            break;
        }
    }

    if (sep) {
        /* Text before separator = task text */
        *sep = 0;
        strncpy(task_text[insert_idx], paste_buf, MAX_TASK_LEN);
        task_text[insert_idx][MAX_TASK_LEN] = 0;

        /* Text after separator = note */
        {
            char *note_text = sep + 5;
            unsigned int note_len = strlen(note_text);
            if (note_len > 0 && note_pool_used + note_len <= NOTE_POOL_SIZE) {
                note_offset[insert_idx] = note_pool_used;
                memcpy(&note_pool[note_pool_used], note_text, note_len);
                note_length[insert_idx] = note_len;
                note_pool_used += note_len;
                task_flags[insert_idx] |= TASK_FLAG_HAS_NOTE;
            }
        }
    } else {
        /* No separator - entire text is task */
        strncpy(task_text[insert_idx], paste_buf, MAX_TASK_LEN);
        task_text[insert_idx][MAX_TASK_LEN] = 0;
    }

    modified = 1;
    rebuild_view();

    /* Position cursor on new task */
    for (i = 0; i < view_count; ++i) {
        if (view_list[i] == insert_idx) {
            cursor_pos = i;
            break;
        }
    }
    clamp_cursor();
}

/*---------------------------------------------------------------------------
 * Dialogs
 *---------------------------------------------------------------------------*/

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

static void show_help_popup(void) {
    TuiRect win;
    unsigned char key;

    win.x = 1;
    win.y = 4;
    win.w = 38;
    win.h = 16;
    tui_window_title(&win, "TASKS HELP", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(3, 6, "READYOS TASKS", TUI_COLOR_WHITE);
    tui_puts(3, 7, "+/-:PAGE UP/DN", TUI_COLOR_GRAY3);
    tui_puts(3, 8, "RET:NEW   DEL:DELETE", TUI_COLOR_GRAY3);
    tui_puts(3, 9, "]:IN      [:OUT", TUI_COLOR_GRAY3);
    tui_puts(3, 10, "SP:DONE   E:EDIT", TUI_COLOR_GRAY3);
    tui_puts(3, 11, "N:NOTE    /:SEARCH", TUI_COLOR_GRAY3);
    tui_puts(3, 12, "F1:COPY   F3:PASTE", TUI_COLOR_GRAY3);
    tui_puts(3, 13, "F5:SAVE   F6:SAVE AS", TUI_COLOR_GRAY3);
    tui_puts(3, 14, "F7:OPEN   F8:HELP", TUI_COLOR_GRAY3);
    tui_puts(3, 15, "FILE DLG: F3 TOGGLE D8/D9", TUI_COLOR_GRAY3);
    tui_puts(3, 16, "F2/F4:APPS ^B:LAUNCHER", TUI_COLOR_GRAY3);
    tui_puts(3, 17, "RET/F8/STOP:CLOSE", TUI_COLOR_CYAN);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_F8 || key == TUI_KEY_RETURN ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return;
        }
    }
}

/*---------------------------------------------------------------------------
 * Initialization
 *---------------------------------------------------------------------------*/

static void tasklist_init(void) {
    unsigned char i;
    unsigned char bank;

    tui_init();
    reu_mgr_init();

    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 23) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }

    if (resume_restore_state()) {
        return;
    }

    /* Initialize task storage */
    task_count = 1;
    task_text[0][0] = 0;
    task_flags[0] = 0;

    for (i = 0; i < MAX_TASKS; ++i) {
        note_offset[i] = 0xFFFF;
        note_length[i] = 0;
    }
    note_pool_used = 0;

    /* Initialize state */
    strcpy(filename, "UNTITLED");
    modified = 0;
    running = 1;
    cursor_pos = 0;
    scroll_y = 0;
    search_active = 0;
    search_buf[0] = 0;

    rebuild_view();
}

/*---------------------------------------------------------------------------
 * Main Loop
 *---------------------------------------------------------------------------*/

static void tasklist_loop(void) {
    unsigned char key;
    unsigned char old_scroll;
    unsigned char nav_action;

    tasklist_draw();

    while (running) {
        key = tui_getkey();
        nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
        if (nav_action == TUI_HOTKEY_LAUNCHER) {
            resume_save_state();
            tui_return_to_launcher();
        }
        if (nav_action >= 1 && nav_action <= 23) {
            resume_save_state();
            tui_switch_to_app(nav_action);
            continue;
        }
        if (nav_action == TUI_HOTKEY_BIND_ONLY) {
            continue;
        }

        /* Search mode input */
        if (search_active) {
            if (key == TUI_KEY_RUNSTOP) {
                clear_filter();
                tasklist_draw();
                continue;
            }
            if (key == TUI_KEY_RETURN) {
                /* Keep filter active but stop editing */
                search_active = 0;
                draw_search_bar();
                continue;
            }
            if (key == TUI_KEY_DEL) {
                unsigned char slen = strlen(search_buf);
                if (slen > 0) {
                    search_buf[slen - 1] = 0;
                    rebuild_view();
                    tasklist_draw();
                }
                continue;
            }
            if (key >= 32 && key < 128) {
                unsigned char slen = strlen(search_buf);
                if (slen < MAX_SEARCH_LEN) {
                    search_buf[slen] = key;
                    search_buf[slen + 1] = 0;
                    rebuild_view();
                    tasklist_draw();
                }
                continue;
            }
            /* Allow UP/DOWN in search mode to navigate results */
            if (key != TUI_KEY_UP && key != TUI_KEY_DOWN) {
                continue;
            }
        }

        /* Normal mode keys */
        old_scroll = scroll_y;

        switch (key) {
            case TUI_KEY_RUNSTOP:
                running = 0;
                break;

            case TUI_KEY_UP:
                if (cursor_pos > 0) {
                    draw_task_line(cursor_pos);  /* Undraw old cursor */
                    --cursor_pos;
                    clamp_cursor();
                    if (scroll_y != old_scroll) {
                        draw_visible_tasks();
                    } else {
                        draw_task_line(cursor_pos);
                        draw_task_line(cursor_pos + 1);
                    }
                }
                continue;

            case TUI_KEY_DOWN:
                if (cursor_pos < view_count - 1) {
                    draw_task_line(cursor_pos);  /* Undraw old cursor */
                    ++cursor_pos;
                    clamp_cursor();
                    if (scroll_y != old_scroll) {
                        draw_visible_tasks();
                    } else {
                        draw_task_line(cursor_pos);
                        draw_task_line(cursor_pos - 1);
                    }
                }
                continue;

            case TUI_KEY_HOME:
                draw_task_line(cursor_pos);
                cursor_pos = 0;
                scroll_y = 0;
                draw_visible_tasks();
                continue;

            case TUI_KEY_RETURN:
                /* Insert new task below cursor */
                insert_task(cursor_pos);
                /* Go straight to edit mode for the new task */
                tasklist_draw();
                edit_task_popup(cursor_pos);
                rebuild_view();
                tasklist_draw();
                continue;

            case TUI_KEY_DEL:
                delete_task(cursor_pos);
                continue;

            case ' ':
                toggle_done(cursor_pos);
                draw_task_line(cursor_pos);
                draw_header();
                draw_status();
                continue;

            case ']':
                indent_task(cursor_pos);
                rebuild_view();
                draw_visible_tasks();
                draw_status();
                continue;

            case '[':
                outdent_task(cursor_pos);
                rebuild_view();
                draw_visible_tasks();
                draw_status();
                continue;

            case 'e':
            case 'E':
                edit_task_popup(cursor_pos);
                rebuild_view();
                tasklist_draw();
                continue;

            case 'n':
            case 'N':
                note_edit_popup(cursor_pos);
                rebuild_view();
                tasklist_draw();
                continue;

            case '/':
                search_active = 1;
                search_buf[0] = 0;
                draw_search_bar();
                continue;

            case '+':
                /* Page down */
                {
                    unsigned char jump = LIST_HEIGHT;
                    draw_task_line(cursor_pos);
                    if (cursor_pos + jump >= view_count) {
                        cursor_pos = view_count > 0 ? view_count - 1 : 0;
                    } else {
                        cursor_pos += jump;
                    }
                    clamp_cursor();
                    draw_visible_tasks();
                }
                continue;

            case '-':
                /* Page up */
                {
                    unsigned char jump = LIST_HEIGHT;
                    draw_task_line(cursor_pos);
                    if (cursor_pos < jump) {
                        cursor_pos = 0;
                    } else {
                        cursor_pos -= jump;
                    }
                    clamp_cursor();
                    draw_visible_tasks();
                }
                continue;

            case TUI_KEY_F8:
                show_help_popup();
                tasklist_draw();
                continue;

            case TUI_KEY_F1:
                /* Copy */
                copy_task_to_clipboard(cursor_pos);
                show_message("COPIED!", TUI_COLOR_LIGHTGREEN);
                tasklist_draw();
                continue;

            case TUI_KEY_F3:
                /* Paste */
                paste_from_clipboard(cursor_pos);
                tasklist_draw();
                continue;

            case TUI_KEY_F5:
                /* Save */
                if (strcmp(filename, "UNTITLED") == 0) {
                    show_save_dialog();
                } else {
                    tui_puts(1, STATUS_Y, "SAVING...", TUI_COLOR_YELLOW);
                    if (file_save(filename) != 0) {
                        show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                    }
                }
                tasklist_draw();
                continue;

            case TUI_KEY_F6:
                /* Save As */
                show_save_dialog();
                tasklist_draw();
                continue;

            case TUI_KEY_F7:
                /* Open */
                {
                    DirPageEntry selected_entry;

                    if (show_open_dialog(&selected_entry) == OPEN_DIALOG_RC_OK) {
                        if (modified) {
                            if (!show_confirm("DISCARD CHANGES?")) {
                                tasklist_draw();
                                continue;
                            }
                        }
                        tui_clear(TUI_COLOR_BLUE);
                        tui_puts(14, 12, "LOADING...", TUI_COLOR_YELLOW);
                        if (file_load(selected_entry.name) != 0) {
                            show_message("LOAD ERROR!", TUI_COLOR_LIGHTRED);
                        }
                    }
                }
                tasklist_draw();
                continue;
        }
    }

    __asm__("jmp $FCE2");
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

int main(void) {
    tasklist_init();
    tasklist_loop();
    return 0;
}
