/*
 * dizzy.c - Ready OS Kanban board
 *
 * Storage:
 *   dizzy.rel (board records)
 *   dizzycfg.rel (view/config record)
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include "../../lib/resume_state.h"
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define NUM_COLUMNS 3
#define MAX_CARDS 96
#define CARD_TITLE_MAX 44
#define COL_TITLE_MAX 12
/* Compile out load/rebuild progress tracing by default to recover RAM headroom. */
#define DIZZY_IO_TRACE 0

#define FILE_BOARD_NAME "dizzy.rel"
#define FILE_CFG_NAME   "dizzycfg.rel"

#define LFN_BOARD 2
#define LFN_CFG   3
#define LFN_CMD  15
#define REL_SA   2

#define REC_BOARD_LEN 64
#define REC_CFG_LEN   32
#define REC_BOARD_SUPER 1
#define REC_BOARD_CARD_BASE 2

#define REL_STEP_NONE      0
#define REL_STEP_OPEN_DATA 1
#define REL_STEP_OPEN_CMD  2
#define REL_STEP_POS       3
#define REL_STEP_READ      4
#define REL_STEP_WRITE     5
#define REL_STEP_VERIFY    6

#define REL_ERR_VERIFY     254
#define REL_ERR_UNKNOWN    255

#define REL_BOARD_STAGE_NONE   0
#define REL_BOARD_STAGE_SUPER  1
#define REL_BOARD_STAGE_CARDS  2
#define REL_BOARD_STAGE_VERIFY 3

/* dizzy.rel superblock layout */
#define BRD_MAGIC0     0
#define BRD_MAGIC1     1
#define BRD_MAGIC2     2
#define BRD_MAGIC3     3
#define BRD_VERSION    4
#define BRD_USED_DEC   5  /* 2 digits */
#define BRD_CHKSUM_DEC 7  /* 5 digits */
#define BRD_MAX_DEC    12 /* 2 digits */
#define BRD_COLS_DEC   14 /* 1 digit */

/* dizzy.rel card record layout */
#define CR_TYPE        0
#define CR_USED        1      /* '0' or '1' */
#define CR_ID_DEC      2      /* 2 digits */
#define CR_COL_DEC     4      /* 1 digit */
#define CR_ORD_DEC     5      /* 2 digits */
#define CR_FLAGS_DEC   7      /* 2 digits */
#define CR_CREATED_DEC 9      /* 3 digits */
#define CR_DUE_DEC     12     /* 3 digits */
#define CR_SNOOZE_DEC  15     /* 3 digits */
#define CR_TITLE_DEC   18     /* 2 digits */
#define CR_TITLE       20

/* dizzycfg.rel record layout */
#define CFG_MAGIC0      0
#define CFG_MAGIC1      1
#define CFG_MAGIC2      2
#define CFG_MAGIC3      3
#define CFG_VERSION     4
#define CFG_SEL_COL_DEC   5   /* 1 digit */
#define CFG_SEL_IDX0_DEC  6   /* 2 digits */
#define CFG_SEL_IDX1_DEC  8   /* 2 digits */
#define CFG_SEL_IDX2_DEC  10  /* 2 digits */
#define CFG_VIEW_MODE_DEC 12  /* 1 digit */
#define CFG_CHKSUM_DEC    13  /* 5 digits */

#define CARD_FLAG_DONE       0x01
#define CARD_FLAG_ARCHIVED   0x02
#define CARD_FLAG_SNOOZED    0x04
#define CARD_FLAG_SUBSCRIBED 0x08

#define VIEW_ONE_EXPANDED 1
#define VIEW_TWO_EXPANDED 2

/* Layout */
#define HEADER_Y    0
#define SUBHDR_Y    1
#define COL_TITLE_Y 3
#define LIST_TOP_Y  4
#define LIST_ROWS   16
#define LIST_SEP_Y   LIST_TOP_Y
#define LIST_BODY_Y  (LIST_TOP_Y + 1)
#define LIST_BODY_ROWS (LIST_ROWS - 1)
#define STATUS_Y    20
#define HELP1_Y     23
#define HELP2_Y     24

#define COLLAPSED_W 3
#define COL_GAP     1

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Types
 *---------------------------------------------------------------------------*/

typedef struct {
    unsigned char used;
    unsigned char flags;
    unsigned int created_day;
    unsigned int due_day;
    unsigned int snooze_day;
    unsigned char title_len;
    char title[CARD_TITLE_MAX + 1];
} Card;

/*---------------------------------------------------------------------------
 * Data
 *---------------------------------------------------------------------------*/

static const char *col_titles[NUM_COLUMNS] = {
    "NOT NOW", "MAYBE?", "DONE"
};

static const unsigned char col_bar_colors[NUM_COLUMNS] = {
    TUI_COLOR_WHITE, TUI_COLOR_GRAY2, TUI_COLOR_GREEN
};

static const unsigned char col_hi_colors[NUM_COLUMNS] = {
    TUI_COLOR_YELLOW, TUI_COLOR_LIGHTBLUE, TUI_COLOR_LIGHTGREEN
};

static Card cards[MAX_CARDS];
static unsigned char col_cards[NUM_COLUMNS][MAX_CARDS];
static unsigned char visible_ids[NUM_COLUMNS][MAX_CARDS];

static unsigned char col_count[NUM_COLUMNS];
static unsigned char visible_count[NUM_COLUMNS];
static unsigned char selected_col;
static unsigned char selected_idx[NUM_COLUMNS];
static unsigned char scroll_row[NUM_COLUMNS];
static unsigned char view_mode;

static unsigned char col_x[NUM_COLUMNS];
static unsigned char col_w[NUM_COLUMNS];
static unsigned char col_expanded[NUM_COLUMNS];

static unsigned int today_day;
static unsigned char running;
static char search_buf[21];

static char status_msg[40];
static unsigned char status_color;
static unsigned char storage_loaded;
static unsigned char rel_last_step;
static unsigned char rel_last_dos_code;
static char rel_last_dos_msg[16];
static unsigned char rel_last_read_n;
static unsigned char rel_last_read_expect;
static unsigned char rel_last_board_stage;
#if DIZZY_IO_TRACE
static unsigned char io_trace_active;
static unsigned char io_trace_len;
static char io_trace[40];
#endif

static char text_buf[CARD_TITLE_MAX + 1];
static char line_buf[48];
static char num_buf[8];
static unsigned char rec_buf64[REC_BOARD_LEN];
static unsigned char rec_buf32[REC_CFG_LEN];
static unsigned char id_col_map[MAX_CARDS];
static unsigned char id_ord_map[MAX_CARDS];
static unsigned char resume_ready;

static ResumeWriteSegment dizzy_resume_write_segments[] = {
    { cards, sizeof(cards) },
    { col_cards, sizeof(col_cards) },
    { col_count, sizeof(col_count) },
    { &selected_col, sizeof(selected_col) },
    { selected_idx, sizeof(selected_idx) },
    { scroll_row, sizeof(scroll_row) },
    { &view_mode, sizeof(view_mode) },
    { &today_day, sizeof(today_day) },
    { search_buf, sizeof(search_buf) },
    { &storage_loaded, sizeof(storage_loaded) },
};

static ResumeReadSegment dizzy_resume_read_segments[] = {
    { cards, sizeof(cards) },
    { col_cards, sizeof(col_cards) },
    { col_count, sizeof(col_count) },
    { &selected_col, sizeof(selected_col) },
    { selected_idx, sizeof(selected_idx) },
    { scroll_row, sizeof(scroll_row) },
    { &view_mode, sizeof(view_mode) },
    { &today_day, sizeof(today_day) },
    { search_buf, sizeof(search_buf) },
    { &storage_loaded, sizeof(storage_loaded) },
};

#define DIZZY_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(dizzy_resume_read_segments) / sizeof(dizzy_resume_read_segments[0])))

/*---------------------------------------------------------------------------
 * Utility
 *---------------------------------------------------------------------------*/

static unsigned char to_lower(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (unsigned char)(ch + 32);
    return ch;
}

static unsigned char str_contains_ci(const char *haystack, const char *needle) {
    unsigned char hi, ni, hlen, nlen;

    hlen = strlen(haystack);
    nlen = strlen(needle);
    if (nlen == 0) return 1;
    if (nlen > hlen) return 0;

    for (hi = 0; hi <= hlen - nlen; ++hi) {
        for (ni = 0; ni < nlen; ++ni) {
            if (to_lower(haystack[hi + ni]) != to_lower(needle[ni])) {
                break;
            }
        }
        if (ni == nlen) return 1;
    }

    return 0;
}

static unsigned char parse_month_3(const char *m) {
    if (m[0] == 'J' && m[1] == 'a' && m[2] == 'n') return 1;
    if (m[0] == 'F' && m[1] == 'e' && m[2] == 'b') return 2;
    if (m[0] == 'M' && m[1] == 'a' && m[2] == 'r') return 3;
    if (m[0] == 'A' && m[1] == 'p' && m[2] == 'r') return 4;
    if (m[0] == 'M' && m[1] == 'a' && m[2] == 'y') return 5;
    if (m[0] == 'J' && m[1] == 'u' && m[2] == 'n') return 6;
    if (m[0] == 'J' && m[1] == 'u' && m[2] == 'l') return 7;
    if (m[0] == 'A' && m[1] == 'u' && m[2] == 'g') return 8;
    if (m[0] == 'S' && m[1] == 'e' && m[2] == 'p') return 9;
    if (m[0] == 'O' && m[1] == 'c' && m[2] == 't') return 10;
    if (m[0] == 'N' && m[1] == 'o' && m[2] == 'v') return 11;
    if (m[0] == 'D' && m[1] == 'e' && m[2] == 'c') return 12;
    return 1;
}

static unsigned int parse_compile_day(void) {
    static const unsigned char month_days[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    const char *d = __DATE__;
    unsigned char month, day, i;
    unsigned int out;

    month = parse_month_3(d);
    day = 0;
    if (d[4] >= '0' && d[4] <= '9') day = (unsigned char)(d[4] - '0');
    if (d[5] >= '0' && d[5] <= '9') day = (unsigned char)(day * 10 + (d[5] - '0'));
    if (day == 0) day = 1;

    out = day;
    for (i = 1; i < month; ++i) out += month_days[i - 1];
    return out;
}

static void set_status(const char *msg, unsigned char color) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = 0;
    status_color = color;
}

static unsigned char u16_to_dec(unsigned int v, char *out) {
    unsigned char len;
    unsigned int n;

    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return 1;
    }

    len = 0;
    n = v;
    while (n > 0 && len < 5) {
        out[len++] = (char)('0' + (n % 10));
        n /= 10;
    }

    out[len] = 0;

    {
        unsigned char i;
        char t;
        for (i = 0; i < len / 2; ++i) {
            t = out[i];
            out[i] = out[len - i - 1];
            out[len - i - 1] = t;
        }
    }

    return len;
}

static void append_text(char *dst, const char *src, unsigned char max) {
    unsigned char dlen;
    unsigned char si;

    dlen = strlen(dst);
    si = 0;
    while (src[si] != 0 && (unsigned char)(dlen + 1) < max) {
        dst[dlen++] = src[si++];
    }
    dst[dlen] = 0;
}

static unsigned int checksum_add_u16(unsigned int csum, unsigned int v) {
    csum = (unsigned int)(csum + (v & 0xFF));
    csum = (unsigned int)(csum + ((v >> 8) & 0xFF));
    return csum;
}

static unsigned int checksum_add_str(unsigned int csum, const char *s) {
    unsigned char i;

    for (i = 0; s[i] != 0; ++i) {
        csum = (unsigned int)(csum + (unsigned char)s[i]);
    }

    return csum;
}

/*---------------------------------------------------------------------------
 * Model helpers
 *---------------------------------------------------------------------------*/

static Card *card_by_id(unsigned int id) {
    Card *c;

    if (id == 0 || id > MAX_CARDS) return 0;
    c = &cards[id - 1];
    if (!c->used) return 0;
    return c;
}

static Card *alloc_card_slot(unsigned char *id_out) {
    unsigned char i;
    Card *c;

    for (i = 0; i < MAX_CARDS; ++i) {
        if (!cards[i].used) {
            c = &cards[i];
            memset(c, 0, sizeof(*c));
            c->used = 1;
            *id_out = (unsigned char)(i + 1);
            return c;
        }
    }

    return 0;
}

static void clear_board_model(void) {
    memset(cards, 0, sizeof(cards));
    memset(col_cards, 0, sizeof(col_cards));
    memset(visible_ids, 0, sizeof(visible_ids));
    memset(col_count, 0, sizeof(col_count));
    memset(visible_count, 0, sizeof(visible_count));
    memset(selected_idx, 0, sizeof(selected_idx));
    memset(scroll_row, 0, sizeof(scroll_row));

    selected_col = 1;
    view_mode = VIEW_ONE_EXPANDED;
}

static unsigned char find_card_pos_in_col(unsigned char col, unsigned char id, unsigned char *pos_out) {
    unsigned char i;

    for (i = 0; i < col_count[col]; ++i) {
        if (col_cards[col][i] == id) {
            *pos_out = i;
            return 0;
        }
    }

    return 1;
}

static unsigned char remove_from_column(unsigned char col, unsigned char id) {
    unsigned char pos;
    unsigned char i;

    if (find_card_pos_in_col(col, id, &pos) != 0) return 1;

    for (i = pos; (unsigned char)(i + 1) < col_count[col]; ++i) {
        col_cards[col][i] = col_cards[col][i + 1];
    }

    if (col_count[col] > 0) {
        --col_count[col];
        col_cards[col][col_count[col]] = 0;
    }

    return 0;
}

static unsigned char append_to_column(unsigned char col, unsigned char id) {
    if (col >= NUM_COLUMNS) return 1;
    if (col_count[col] >= MAX_CARDS) return 1;

    col_cards[col][col_count[col]] = id;
    ++col_count[col];
    return 0;
}

static unsigned char card_matches_search(const Card *c) {
    if (search_buf[0] == 0) return 1;
    return str_contains_ci(c->title, search_buf);
}

static void clamp_selection(unsigned char col) {
    if (visible_count[col] == 0) {
        selected_idx[col] = 0;
        scroll_row[col] = 0;
        return;
    }

    if (selected_idx[col] >= visible_count[col]) {
        selected_idx[col] = (unsigned char)(visible_count[col] - 1);
    }

    if (selected_idx[col] < scroll_row[col]) scroll_row[col] = selected_idx[col];
    if (selected_idx[col] >= (unsigned char)(scroll_row[col] + LIST_BODY_ROWS)) {
        scroll_row[col] = (unsigned char)(selected_idx[col] - LIST_BODY_ROWS + 1);
    }
}

static void rebuild_visible_lists(void) {
    unsigned char col;
    unsigned char i;
    unsigned char id;
    Card *c;

    for (col = 0; col < NUM_COLUMNS; ++col) {
        visible_count[col] = 0;

        for (i = 0; i < col_count[col] && visible_count[col] < MAX_CARDS; ++i) {
            id = col_cards[col][i];
            c = card_by_id(id);
            if (c == 0) continue;

            if (card_matches_search(c)) {
                visible_ids[col][visible_count[col]] = id;
                ++visible_count[col];
            }
        }

        clamp_selection(col);
    }
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    (void)resume_save_segments(dizzy_resume_write_segments, DIZZY_RESUME_SEG_COUNT);
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;
    unsigned char col;
    unsigned char i;
    unsigned char id;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_load_segments(dizzy_resume_read_segments, DIZZY_RESUME_SEG_COUNT, &payload_len)) {
        return 0;
    }

    if (selected_col >= NUM_COLUMNS) {
        selected_col = 0;
    }
    if (view_mode != VIEW_ONE_EXPANDED && view_mode != VIEW_TWO_EXPANDED) {
        view_mode = VIEW_ONE_EXPANDED;
    }
    search_buf[sizeof(search_buf) - 1] = 0;
    storage_loaded = storage_loaded ? 1 : 0;

    for (i = 0; i < MAX_CARDS; ++i) {
        cards[i].used = cards[i].used ? 1 : 0;
        if (cards[i].title_len > CARD_TITLE_MAX) {
            return 0;
        }
        cards[i].title[CARD_TITLE_MAX] = 0;
    }

    for (col = 0; col < NUM_COLUMNS; ++col) {
        if (col_count[col] > MAX_CARDS) {
            return 0;
        }
        for (i = 0; i < col_count[col]; ++i) {
            id = col_cards[col][i];
            if (id == 0 || id > MAX_CARDS) {
                return 0;
            }
        }
    }

    rebuild_visible_lists();
    for (col = 0; col < NUM_COLUMNS; ++col) {
        clamp_selection(col);
    }
    running = 1;
    return 1;
}

static unsigned int selected_card_id(void) {
    if (visible_count[selected_col] == 0) return 0;
    if (selected_idx[selected_col] >= visible_count[selected_col]) return 0;
    return visible_ids[selected_col][selected_idx[selected_col]];
}

static Card *selected_card(void) {
    return card_by_id(selected_card_id());
}

static unsigned char add_card_model(const char *title) {
    Card *c;
    unsigned char card_id;
    unsigned char len;

    if (title == 0 || title[0] == 0) return 1;

    c = alloc_card_slot(&card_id);
    if (c == 0) return 1;

    len = strlen(title);
    if (len > CARD_TITLE_MAX) len = CARD_TITLE_MAX;

    c->flags = 0;
    c->created_day = today_day;
    c->due_day = 0;
    c->snooze_day = 0;
    c->title_len = len;
    memcpy(c->title, title, len);
    c->title[len] = 0;

    if (append_to_column(selected_col, card_id) != 0) {
        c->used = 0;
        return 1;
    }

    return 0;
}

static unsigned char move_selected_horizontal_model(signed char delta) {
    unsigned char id;
    unsigned char target_col;

    id = (unsigned char)selected_card_id();
    if (id == 0) return 1;

    if (delta < 0) {
        if (selected_col == 0) return 0;
        target_col = (unsigned char)(selected_col - 1);
    } else {
        if ((unsigned char)(selected_col + 1) >= NUM_COLUMNS) return 0;
        target_col = (unsigned char)(selected_col + 1);
    }

    if (remove_from_column(selected_col, id) != 0) return 1;
    if (append_to_column(target_col, id) != 0) return 1;

    selected_col = target_col;
    if (col_count[selected_col] > 0) {
        selected_idx[selected_col] = (unsigned char)(col_count[selected_col] - 1);
    }

    return 0;
}

static unsigned char swap_selected_with_prev_model(void) {
    unsigned char id;
    unsigned char pos;
    unsigned char t;

    id = (unsigned char)selected_card_id();
    if (id == 0) return 1;
    if (find_card_pos_in_col(selected_col, id, &pos) != 0) return 1;
    if (pos == 0) return 0;

    t = col_cards[selected_col][pos - 1];
    col_cards[selected_col][pos - 1] = col_cards[selected_col][pos];
    col_cards[selected_col][pos] = t;

    return 0;
}

static unsigned char swap_selected_with_next_model(void) {
    unsigned char id;
    unsigned char pos;
    unsigned char t;

    id = (unsigned char)selected_card_id();
    if (id == 0) return 1;
    if (find_card_pos_in_col(selected_col, id, &pos) != 0) return 1;
    if ((unsigned char)(pos + 1) >= col_count[selected_col]) return 0;

    t = col_cards[selected_col][pos + 1];
    col_cards[selected_col][pos + 1] = col_cards[selected_col][pos];
    col_cards[selected_col][pos] = t;

    return 0;
}

static unsigned char delete_selected_model(void) {
    unsigned char id;

    id = (unsigned char)selected_card_id();
    if (id == 0) return 1;
    if (remove_from_column(selected_col, id) != 0) return 1;

    memset(&cards[id - 1], 0, sizeof(cards[0]));
    return 0;
}

/*---------------------------------------------------------------------------
 * Disk / REL helpers
 *---------------------------------------------------------------------------*/

static void draw_status_now(void) {
    tui_puts_n(0, STATUS_Y, "                                        ", 40, TUI_COLOR_WHITE);
    tui_puts_n(0, STATUS_Y, status_msg, 39, status_color);
}

#if DIZZY_IO_TRACE
static void io_trace_render(unsigned char color) {
    if (!io_trace_active) return;
    set_status(io_trace, color);
    draw_status_now();
}

static void io_trace_append_char(char c) {
    if (io_trace_len >= 39) return;
    io_trace[io_trace_len++] = c;
    io_trace[io_trace_len] = 0;
}

static void io_trace_reset(const char *label) {
    io_trace_len = 0;
    io_trace[0] = 0;
    io_trace_active = 1;
    while (*label && io_trace_len < 39) {
        io_trace_append_char(*label++);
    }
}

static void io_trace_mark(char letter, unsigned char color) {
    if (!io_trace_active) return;
    if (io_trace_len > 0 && io_trace[io_trace_len - 1] != ' ') {
        io_trace_append_char(' ');
    }
    io_trace_append_char(letter);
    io_trace_render(color);
}

static void io_trace_dot(unsigned char color) {
    if (!io_trace_active || io_trace_len == 0) return;
    io_trace_append_char('.');
    io_trace_render(color);
}
#else
#define io_trace_reset(label) ((void)0)
#define io_trace_mark(letter, color) ((void)0)
#define io_trace_dot(color) ((void)0)
#endif

static void begin_save_indicator(void) {
    set_status("SAVING...", TUI_COLOR_YELLOW);
    draw_status_now();
}

static void end_save_indicator(unsigned char ok) {
    if (ok) {
        set_status("SAVED", TUI_COLOR_LIGHTGREEN);
    } else {
        set_status("SAVE ERROR", TUI_COLOR_LIGHTRED);
    }
    draw_status_now();
}

static void draw_screen(void);

static void begin_load_indicator(void) {
    TuiRect win;

    draw_screen();

    win.x = 12;
    win.y = 9;
    win.w = 16;
    win.h = 5;

    tui_window_title(&win, "LOADING", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(14, 11, "PLEASE WAIT", TUI_COLOR_WHITE);
}

static void end_load_indicator(void) {
    draw_screen();
}

static unsigned char is_digit_ch(char c) {
    return (unsigned char)(c >= '0' && c <= '9');
}

static void rel_clear_error(void) {
    rel_last_step = REL_STEP_NONE;
    rel_last_dos_code = 0;
    rel_last_dos_msg[0] = 0;
    rel_last_read_n = 0;
    rel_last_read_expect = 0;
    rel_last_board_stage = REL_BOARD_STAGE_NONE;
}

static void rel_set_error(unsigned char step, unsigned char code, const char *msg) {
    rel_last_step = step;
    rel_last_dos_code = code;
    if (msg && msg[0]) {
        strncpy(rel_last_dos_msg, msg, sizeof(rel_last_dos_msg) - 1);
        rel_last_dos_msg[sizeof(rel_last_dos_msg) - 1] = 0;
    } else {
        strcpy(rel_last_dos_msg, "ERROR");
    }
}

static void dos_parse_status_line(const char *st,
                                  unsigned char *code_out,
                                  char *msg_out,
                                  unsigned char msg_cap) {
    const char *p;
    unsigned char i = 0;

    *code_out = REL_ERR_UNKNOWN;
    if (msg_cap == 0) return;
    msg_out[0] = 0;

    if (is_digit_ch(st[0]) && is_digit_ch(st[1])) {
        *code_out = (unsigned char)((st[0] - '0') * 10 + (st[1] - '0'));
    }

    p = st;
    while (*p && *p != ',') ++p;
    if (*p == ',') ++p;
    while (*p == ' ') ++p;

    while (*p && *p != ',' && *p != '\r' && *p != '\n' && i + 1 < msg_cap) {
        msg_out[i++] = *p++;
    }
    msg_out[i] = 0;
    if (msg_out[0] == 0) {
        strncpy(msg_out, "STATUS", msg_cap - 1);
        msg_out[msg_cap - 1] = 0;
    }
}

static int dos_read_status(unsigned char *code_out, char *msg_out, unsigned char msg_cap) {
    char st[40];
    int n;

    n = cbm_read(LFN_CMD, st, sizeof(st) - 1);
    if (n < 0) n = 0;
    st[n] = 0;

    while (n > 0 && (st[n - 1] == '\r' || st[n - 1] == '\n')) {
        st[n - 1] = 0;
        --n;
    }

    dos_parse_status_line(st, code_out, msg_out, msg_cap);
    return n;
}

static unsigned char dos_scratch(const char *name) {
    static char cmd[24];
    strcpy(cmd, "s:");
    strcat(cmd, name);
    if (cbm_open(LFN_CMD, 8, 15, cmd) != 0) return 1;
    cbm_close(LFN_CMD);
    return 0;
}

static void scratch_board_variants(void) {
    /* Do not scratch bare "dizzy"; it collides with the app PRG name. */
    dos_scratch(FILE_BOARD_NAME);
}

static void scratch_cfg_variants(void) {
    dos_scratch(FILE_CFG_NAME);
}

static void rec_clear(unsigned char *rec, unsigned char len) {
    memset(rec, ' ', len);
}

static unsigned int rd_dec(const unsigned char *buf, unsigned char off, unsigned char digits) {
    unsigned char i;
    unsigned int out = 0;
    unsigned char c;

    for (i = 0; i < digits; ++i) {
        c = buf[off + i];
        if (c < '0' || c > '9') return 0xFFFF;
        out = (unsigned int)(out * 10 + (unsigned int)(c - '0'));
    }
    return out;
}

static void wr_dec(unsigned char *buf, unsigned char off, unsigned char digits, unsigned int v) {
    unsigned char i;
    for (i = 0; i < digits; ++i) {
        buf[off + digits - 1 - i] = (unsigned char)('0' + (v % 10));
        v /= 10;
    }
}

static unsigned int board_used_count(void) {
    unsigned int id;
    unsigned int used = 0;
    for (id = 1; id <= MAX_CARDS; ++id) {
        if (cards[id - 1].used) ++used;
    }
    return used;
}

static unsigned int board_payload_checksum(void) {
    unsigned char col;
    unsigned char i;
    unsigned int id;
    Card *c;
    unsigned int csum = 0;

    for (col = 0; col < NUM_COLUMNS; ++col) {
        csum = checksum_add_u16(csum, col_count[col]);
    }
    for (col = 0; col < NUM_COLUMNS; ++col) {
        for (i = 0; i < col_count[col]; ++i) {
            id = col_cards[col][i];
            c = card_by_id(id);
            if (c == 0) continue;
            csum = checksum_add_u16(csum, id);
            csum = checksum_add_u16(csum, col);
            csum = checksum_add_u16(csum, i);
            csum = checksum_add_u16(csum, c->flags);
            csum = checksum_add_u16(csum, c->created_day);
            csum = checksum_add_u16(csum, c->due_day);
            csum = checksum_add_u16(csum, c->snooze_day);
            csum = checksum_add_str(csum, c->title);
        }
    }
    return csum;
}

static unsigned int cfg_checksum(void) {
    unsigned int csum = 0;
    csum = checksum_add_u16(csum, selected_col);
    csum = checksum_add_u16(csum, selected_idx[0]);
    csum = checksum_add_u16(csum, selected_idx[1]);
    csum = checksum_add_u16(csum, selected_idx[2]);
    csum = checksum_add_u16(csum, view_mode);
    return csum;
}

static void build_id_layout_maps(void) {
    unsigned char col;
    unsigned char i;
    unsigned int id;

    memset(id_col_map, 0xFF, sizeof(id_col_map));
    memset(id_ord_map, 0xFF, sizeof(id_ord_map));

    for (col = 0; col < NUM_COLUMNS; ++col) {
        for (i = 0; i < col_count[col]; ++i) {
            id = col_cards[col][i];
            if (id == 0 || id > MAX_CARDS) continue;
            id_col_map[id - 1] = col;
            id_ord_map[id - 1] = i;
        }
    }
}

static void build_superblock_record(void) {
    rec_clear(rec_buf64, REC_BOARD_LEN);
    rec_buf64[BRD_MAGIC0] = 'd';
    rec_buf64[BRD_MAGIC1] = 'z';
    rec_buf64[BRD_MAGIC2] = 'r';
    rec_buf64[BRD_MAGIC3] = 'b';
    rec_buf64[BRD_VERSION] = '1';
    wr_dec(rec_buf64, BRD_USED_DEC, 2, board_used_count());
    wr_dec(rec_buf64, BRD_CHKSUM_DEC, 5, board_payload_checksum());
    wr_dec(rec_buf64, BRD_MAX_DEC, 2, MAX_CARDS);
    wr_dec(rec_buf64, BRD_COLS_DEC, 1, NUM_COLUMNS);
}

static unsigned char parse_superblock_record(unsigned int *used_out, unsigned int *csum_out) {
    unsigned int used;
    unsigned int csum;
    unsigned int max_cards;
    unsigned int cols;

    if (rec_buf64[BRD_MAGIC0] != 'd' || rec_buf64[BRD_MAGIC1] != 'z' ||
        rec_buf64[BRD_MAGIC2] != 'r' || rec_buf64[BRD_MAGIC3] != 'b' ||
        rec_buf64[BRD_VERSION] != '1') {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD SUPER");
        return 1;
    }

    used = rd_dec(rec_buf64, BRD_USED_DEC, 2);
    csum = rd_dec(rec_buf64, BRD_CHKSUM_DEC, 5);
    max_cards = rd_dec(rec_buf64, BRD_MAX_DEC, 2);
    cols = rd_dec(rec_buf64, BRD_COLS_DEC, 1);
    if (used == 0xFFFF || csum == 0xFFFF || max_cards == 0xFFFF || cols == 0xFFFF) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD SUPER");
        return 1;
    }
    if (max_cards != MAX_CARDS || cols != NUM_COLUMNS) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD SUPER");
        return 1;
    }

    *used_out = used;
    *csum_out = csum;
    return 0;
}

static unsigned char encode_card_record(unsigned int id) {
    unsigned char slot = (unsigned char)(id - 1);
    Card *c = &cards[slot];
    unsigned char i;

    rec_clear(rec_buf64, REC_BOARD_LEN);
    rec_buf64[CR_TYPE] = 'C';
    rec_buf64[CR_USED] = '0';
    wr_dec(rec_buf64, CR_ID_DEC, 2, id);
    if (!c->used) return 0;

    if (id_col_map[slot] >= NUM_COLUMNS || id_ord_map[slot] >= MAX_CARDS) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD MAP");
        return 1;
    }
    rec_buf64[CR_USED] = '1';
    wr_dec(rec_buf64, CR_COL_DEC, 1, id_col_map[slot]);
    wr_dec(rec_buf64, CR_ORD_DEC, 2, id_ord_map[slot]);
    wr_dec(rec_buf64, CR_FLAGS_DEC, 2, c->flags);
    wr_dec(rec_buf64, CR_CREATED_DEC, 3, c->created_day);
    wr_dec(rec_buf64, CR_DUE_DEC, 3, c->due_day);
    wr_dec(rec_buf64, CR_SNOOZE_DEC, 3, c->snooze_day);
    if (c->title_len > CARD_TITLE_MAX) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD TITLE");
        return 1;
    }
    wr_dec(rec_buf64, CR_TITLE_DEC, 2, c->title_len);
    for (i = 0; i < c->title_len; ++i) {
        rec_buf64[(unsigned char)(CR_TITLE + i)] = (unsigned char)c->title[i];
    }
    return 0;
}

static unsigned char decode_card_record(unsigned int id) {
    unsigned char slot = (unsigned char)(id - 1);
    Card *c = &cards[slot];
    unsigned char col;
    unsigned char ord;
    unsigned char tlen;
    unsigned char i;

    unsigned int rid;
    unsigned int col_dec;
    unsigned int ord_dec;
    unsigned int flags_dec;
    unsigned int created_dec;
    unsigned int due_dec;
    unsigned int snooze_dec;
    unsigned int tlen_dec;

    rid = rd_dec(rec_buf64, CR_ID_DEC, 2);
    if (rec_buf64[CR_TYPE] != 'C' || rid == 0xFFFF || rid != id) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CARD");
        return 1;
    }
    memset(c, 0, sizeof(*c));
    if (rec_buf64[CR_USED] == '0') return 0;
    if (rec_buf64[CR_USED] != '1') {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CARD");
        return 1;
    }

    col_dec = rd_dec(rec_buf64, CR_COL_DEC, 1);
    ord_dec = rd_dec(rec_buf64, CR_ORD_DEC, 2);
    flags_dec = rd_dec(rec_buf64, CR_FLAGS_DEC, 2);
    created_dec = rd_dec(rec_buf64, CR_CREATED_DEC, 3);
    due_dec = rd_dec(rec_buf64, CR_DUE_DEC, 3);
    snooze_dec = rd_dec(rec_buf64, CR_SNOOZE_DEC, 3);
    tlen_dec = rd_dec(rec_buf64, CR_TITLE_DEC, 2);
    if (col_dec == 0xFFFF || ord_dec == 0xFFFF || flags_dec == 0xFFFF ||
        created_dec == 0xFFFF || due_dec == 0xFFFF || snooze_dec == 0xFFFF ||
        tlen_dec == 0xFFFF) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CARD");
        return 1;
    }

    col = (unsigned char)col_dec;
    ord = (unsigned char)ord_dec;
    tlen = (unsigned char)tlen_dec;
    if (col >= NUM_COLUMNS || ord >= MAX_CARDS || tlen > CARD_TITLE_MAX || flags_dec > 0xFF) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CARD");
        return 1;
    }
    c->used = 1;
    c->flags = (unsigned char)flags_dec;
    c->created_day = created_dec;
    c->due_day = due_dec;
    c->snooze_day = snooze_dec;
    c->title_len = tlen;
    for (i = 0; i < tlen; ++i) c->title[i] = (char)rec_buf64[(unsigned char)(CR_TITLE + i)];
    c->title[tlen] = 0;
    id_col_map[slot] = col;
    id_ord_map[slot] = ord;
    return 0;
}

static unsigned char rebuild_columns_from_maps(void) {
    unsigned char col;
    unsigned char ord;
    unsigned int id;
    Card *c;

    memset(col_count, 0, sizeof(col_count));
    memset(col_cards, 0, sizeof(col_cards));

    for (col = 0; col < NUM_COLUMNS; ++col) {
        for (ord = 0; ord < MAX_CARDS; ++ord) {
            for (id = 1; id <= MAX_CARDS; ++id) {
                c = &cards[id - 1];
                if (!c->used) continue;
                if (id_col_map[id - 1] != col || id_ord_map[id - 1] != ord) continue;
                if (col_count[col] >= MAX_CARDS) {
                    rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD ORDER");
                    return 1;
                }
                col_cards[col][col_count[col]] = id;
                ++col_count[col];
            }
        }
    }
    for (id = 1; id <= MAX_CARDS; ++id) {
        c = &cards[id - 1];
        if (!c->used) continue;
        if (id_col_map[id - 1] >= NUM_COLUMNS || id_ord_map[id - 1] >= MAX_CARDS) {
            rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CARD");
            return 1;
        }
    }
    return 0;
}

static unsigned char rel_error_is_transient_io(void) {
    if (rel_last_dos_code == 30 || rel_last_dos_code == 31 || rel_last_dos_code == 39) return 1;
    if (rel_last_dos_code == 50 || rel_last_dos_code == 70) return 1;
    return 0;
}

static unsigned char rel_error_is_initable(void) {
    if (rel_last_dos_code == 62 || rel_last_dos_code == 63) return 1;
    if (rel_last_dos_code == 30 || rel_last_dos_code == 31 || rel_last_dos_code == 39) return 1;
    if (rel_last_dos_code == 50 || rel_last_dos_code == 70) return 1;
    if (rel_last_dos_code == REL_ERR_VERIFY) return 1;
    return 0;
}

static unsigned char rel_error_is_super_short_read_initable(void) {
    if (rel_last_step != REL_STEP_READ) return 0;
    if (rel_last_board_stage != REL_BOARD_STAGE_SUPER) return 0;
    if (rel_last_read_expect == 0 || rel_last_read_n >= rel_last_read_expect) return 0;
    if (rel_last_dos_code == 0 || rel_last_dos_code == 50 || rel_last_dos_code == REL_ERR_UNKNOWN) return 1;
    return 0;
}

static const char *rel_step_tag(unsigned char step) {
    switch (step) {
        case REL_STEP_OPEN_DATA: return "OD";
        case REL_STEP_OPEN_CMD:  return "OC";
        case REL_STEP_POS:       return "P";
        case REL_STEP_READ:      return "R";
        case REL_STEP_WRITE:     return "W";
        case REL_STEP_VERIFY:    return "V";
        default:                 return "?";
    }
}

static const char *rel_board_stage_tag(unsigned char stage) {
    switch (stage) {
        case REL_BOARD_STAGE_SUPER:  return "S";
        case REL_BOARD_STAGE_CARDS:  return "C";
        case REL_BOARD_STAGE_VERIFY: return "V";
        default:                     return "?";
    }
}

static void format_rel_error_status(const char *prefix) {
    char line[40];
    char code[3];
    char nbuf[8];
    char ebuf[8];
    unsigned char left;

    line[0] = 0;
#if DIZZY_IO_TRACE
    if (io_trace_active && io_trace[0]) {
        strncat(line, io_trace, sizeof(line) - 1);
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, " ", left);
    }
#endif
    strncat(line, prefix, sizeof(line) - 1);
    left = (unsigned char)(39 - strlen(line));
    if (left > 0) strncat(line, rel_step_tag(rel_last_step), left);

    left = (unsigned char)(39 - strlen(line));
    if (left > 0) strncat(line, " ", left);

    code[0] = (char)('0' + (rel_last_dos_code / 10));
    code[1] = (char)('0' + (rel_last_dos_code % 10));
    code[2] = 0;
    if (rel_last_dos_code > 99) {
        code[0] = '?';
        code[1] = '?';
    }
    left = (unsigned char)(39 - strlen(line));
    if (left > 0) strncat(line, code, left);
    left = (unsigned char)(39 - strlen(line));
    if (left > 0) strncat(line, ",", left);
    left = (unsigned char)(39 - strlen(line));
    if (left > 0) strncat(line, rel_last_dos_msg, left);

    if (rel_last_step == REL_STEP_READ && rel_last_read_expect > 0 &&
        rel_last_read_n != rel_last_read_expect) {
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, " ", left);
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, rel_board_stage_tag(rel_last_board_stage), left);
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, ":", left);
        u16_to_dec(rel_last_read_n, nbuf);
        u16_to_dec(rel_last_read_expect, ebuf);
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, nbuf, left);
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, "/", left);
        left = (unsigned char)(39 - strlen(line));
        if (left > 0) strncat(line, ebuf, left);
    }
    set_status(line, TUI_COLOR_LIGHTRED);
}

static void rel_build_spec(char *spec, const char *name, unsigned char rec_len) {
    unsigned char n;
    strcpy(spec, "0:");
    strcat(spec, name);
    strcat(spec, ",l,");
    n = (unsigned char)strlen(spec);
    spec[n] = rec_len;
    spec[(unsigned char)(n + 1)] = 0;
}

static unsigned char rel_session_open(unsigned char lfn_data, const char *name, unsigned char rec_len) {
    char spec[24];
    rel_build_spec(spec, name, rec_len);
    rel_clear_error();

    if (cbm_open(LFN_CMD, 8, 15, "") != 0) {
        rel_set_error(REL_STEP_OPEN_CMD, REL_ERR_UNKNOWN, "OPEN CMD");
        return 1;
    }
    if (cbm_open(lfn_data, 8, REL_SA, spec) != 0) {
        rel_set_error(REL_STEP_OPEN_DATA, REL_ERR_UNKNOWN, "OPEN DATA");
        (void)dos_read_status(&rel_last_dos_code, rel_last_dos_msg, sizeof(rel_last_dos_msg));
        cbm_close(LFN_CMD);
        return 1;
    }
    return 0;
}

static void rel_session_close(unsigned char lfn_data) {
    cbm_close(lfn_data);
    cbm_close(LFN_CMD);
}

static unsigned char open_board_session(void) {
    return rel_session_open(LFN_BOARD, FILE_BOARD_NAME, REC_BOARD_LEN);
}

static unsigned char open_cfg_session(void) {
    return rel_session_open(LFN_CFG, FILE_CFG_NAME, REC_CFG_LEN);
}

static unsigned char rel_position_session(unsigned char lfn_data,
                                          unsigned int rec,
                                          unsigned char pos1) {
    unsigned char cmd[5];
    unsigned char i;
    unsigned char rc;

    (void)lfn_data;
    cmd[0] = 'p';
    cmd[1] = (unsigned char)(REL_SA + 96);
    cmd[2] = (unsigned char)(rec & 0xFF);
    cmd[3] = (unsigned char)(rec >> 8);
    cmd[4] = pos1;

    rc = cbm_k_ckout(LFN_CMD);
    if (rc != 0) {
        cbm_k_clrch();
        rel_set_error(REL_STEP_POS, REL_ERR_UNKNOWN, "POS CKOUT");
        return 1;
    }
    for (i = 0; i < 5; ++i) cbm_k_bsout(cmd[i]);
    cbm_k_clrch();

    if (dos_read_status(&rel_last_dos_code, rel_last_dos_msg, sizeof(rel_last_dos_msg)) > 0 &&
        rel_last_dos_code != 0 && rel_last_dos_code != 50) {
        rel_last_step = REL_STEP_POS;
        return 1;
    }
    return 0;
}

static unsigned char rel_read_record_session(unsigned char lfn_data,
                                             unsigned int rec,
                                             unsigned char *buf,
                                             unsigned char len) {
    int n;
    if (rel_position_session(lfn_data, rec, 1) != 0) return 1;
    rel_last_read_expect = len;
    n = cbm_read(lfn_data, buf, len);
    rel_last_read_n = (unsigned char)((n < 0) ? 0 : n);
    if (n != len) {
        if (dos_read_status(&rel_last_dos_code, rel_last_dos_msg, sizeof(rel_last_dos_msg)) > 0 &&
            rel_last_dos_code != 0 && rel_last_dos_code != 50) {
            rel_last_step = REL_STEP_READ;
        } else {
            rel_set_error(REL_STEP_READ, 0, "SHORT READ");
        }
        return 1;
    }
    return 0;
}

static unsigned char rel_write_record_session(unsigned char lfn_data,
                                              unsigned int rec,
                                              const unsigned char *buf,
                                              unsigned char len) {
    unsigned char rc;
    unsigned char i;

    if (rel_position_session(lfn_data, rec, 1) != 0) return 1;

    rc = cbm_k_ckout(lfn_data);
    if (rc != 0) {
        cbm_k_clrch();
        rel_set_error(REL_STEP_WRITE, REL_ERR_UNKNOWN, "CKOUT");
        return 1;
    }
    for (i = 0; i < len; ++i) cbm_k_bsout(buf[i]);
    cbm_k_clrch();

    if (dos_read_status(&rel_last_dos_code, rel_last_dos_msg, sizeof(rel_last_dos_msg)) > 0 &&
        rel_last_dos_code != 0 && rel_last_dos_code != 50) {
        rel_last_step = REL_STEP_WRITE;
        return 1;
    }
    return 0;
}

static unsigned char write_board_rel(void) {
    unsigned int id;

    build_id_layout_maps();
    build_superblock_record();

    if (open_board_session() != 0) return 1;
    if (rel_write_record_session(LFN_BOARD, REC_BOARD_SUPER, rec_buf64, REC_BOARD_LEN) != 0) {
        rel_session_close(LFN_BOARD);
        return 1;
    }
    for (id = 1; id <= MAX_CARDS; ++id) {
        if (encode_card_record(id) != 0) {
            rel_session_close(LFN_BOARD);
            return 1;
        }
        if (rel_write_record_session(LFN_BOARD,
                                     (unsigned int)(REC_BOARD_CARD_BASE + (id - 1)),
                                     rec_buf64,
                                     REC_BOARD_LEN) != 0) {
            rel_session_close(LFN_BOARD);
            return 1;
        }
    }
    rel_session_close(LFN_BOARD);
    return 0;
}

static unsigned char verify_board_superblock_once(void) {
    unsigned int used;
    unsigned int csum;

    if (open_board_session() != 0) return 1;
    rel_last_board_stage = REL_BOARD_STAGE_SUPER;
    if (rel_read_record_session(LFN_BOARD, REC_BOARD_SUPER, rec_buf64, REC_BOARD_LEN) != 0) {
        rel_session_close(LFN_BOARD);
        return 1;
    }
    if (parse_superblock_record(&used, &csum) != 0) {
        rel_session_close(LFN_BOARD);
        return 1;
    }
    rel_last_board_stage = REL_BOARD_STAGE_NONE;
    rel_session_close(LFN_BOARD);
    return 0;
}

static unsigned char load_board_rel(void) {
    unsigned int id;
    unsigned int expected_used;
    unsigned int expected_csum;

    if (open_board_session() != 0) return 1;
    rel_last_board_stage = REL_BOARD_STAGE_SUPER;
    io_trace_mark('S', TUI_COLOR_YELLOW);
    if (rel_read_record_session(LFN_BOARD, REC_BOARD_SUPER, rec_buf64, REC_BOARD_LEN) != 0) {
        rel_session_close(LFN_BOARD);
        return 1;
    }
    if (parse_superblock_record(&expected_used, &expected_csum) != 0) {
        rel_session_close(LFN_BOARD);
        return 1;
    }

    clear_board_model();
    memset(id_col_map, 0xFF, sizeof(id_col_map));
    memset(id_ord_map, 0xFF, sizeof(id_ord_map));
    rel_last_board_stage = REL_BOARD_STAGE_CARDS;
    io_trace_mark('C', TUI_COLOR_YELLOW);

    for (id = 1; id <= MAX_CARDS; ++id) {
        if (rel_read_record_session(LFN_BOARD,
                                    (unsigned int)(REC_BOARD_CARD_BASE + (id - 1)),
                                    rec_buf64,
                                    REC_BOARD_LEN) != 0) {
            rel_session_close(LFN_BOARD);
            return 1;
        }
        if (decode_card_record(id) != 0) {
            rel_session_close(LFN_BOARD);
            return 1;
        }
        if ((id & 0x1F) == 0 || id == MAX_CARDS) io_trace_dot(TUI_COLOR_YELLOW);
    }
    rel_session_close(LFN_BOARD);

    rel_last_board_stage = REL_BOARD_STAGE_VERIFY;
    io_trace_mark('F', TUI_COLOR_YELLOW);
    if (rebuild_columns_from_maps() != 0) return 1;
    if (board_used_count() != expected_used) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD COUNT");
        return 1;
    }
    if (board_payload_checksum() != expected_csum) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CHKSUM");
        return 1;
    }
    rel_last_board_stage = REL_BOARD_STAGE_NONE;
    return 0;
}

static unsigned char save_config_rel(void) {
    rec_clear(rec_buf32, REC_CFG_LEN);
    rec_buf32[CFG_MAGIC0] = 'd';
    rec_buf32[CFG_MAGIC1] = 'z';
    rec_buf32[CFG_MAGIC2] = 'c';
    rec_buf32[CFG_MAGIC3] = 'f';
    rec_buf32[CFG_VERSION] = '1';
    wr_dec(rec_buf32, CFG_SEL_COL_DEC, 1, selected_col);
    wr_dec(rec_buf32, CFG_SEL_IDX0_DEC, 2, selected_idx[0]);
    wr_dec(rec_buf32, CFG_SEL_IDX1_DEC, 2, selected_idx[1]);
    wr_dec(rec_buf32, CFG_SEL_IDX2_DEC, 2, selected_idx[2]);
    wr_dec(rec_buf32, CFG_VIEW_MODE_DEC, 1, view_mode);
    wr_dec(rec_buf32, CFG_CHKSUM_DEC, 5, cfg_checksum());

    if (open_cfg_session() != 0) return 1;
    if (rel_write_record_session(LFN_CFG, 1, rec_buf32, REC_CFG_LEN) != 0) {
        rel_session_close(LFN_CFG);
        return 1;
    }
    rel_session_close(LFN_CFG);
    return 0;
}

static unsigned char init_or_load_cfg_rel(void) {
    unsigned int expected;
    unsigned int sel_col_dec;
    unsigned int sel_idx0_dec;
    unsigned int sel_idx1_dec;
    unsigned int sel_idx2_dec;
    unsigned int view_mode_dec;

    if (open_cfg_session() != 0) return 1;
    if (rel_read_record_session(LFN_CFG, 1, rec_buf32, REC_CFG_LEN) != 0 ||
        rec_buf32[CFG_MAGIC0] != 'd' || rec_buf32[CFG_MAGIC1] != 'z' ||
        rec_buf32[CFG_MAGIC2] != 'c' || rec_buf32[CFG_MAGIC3] != 'f' ||
        rec_buf32[CFG_VERSION] != '1') {
        rel_session_close(LFN_CFG);
        scratch_cfg_variants();
        selected_col = 1;
        selected_idx[0] = 0;
        selected_idx[1] = 0;
        selected_idx[2] = 0;
        view_mode = VIEW_ONE_EXPANDED;
        return save_config_rel();
    }

    sel_col_dec = rd_dec(rec_buf32, CFG_SEL_COL_DEC, 1);
    sel_idx0_dec = rd_dec(rec_buf32, CFG_SEL_IDX0_DEC, 2);
    sel_idx1_dec = rd_dec(rec_buf32, CFG_SEL_IDX1_DEC, 2);
    sel_idx2_dec = rd_dec(rec_buf32, CFG_SEL_IDX2_DEC, 2);
    view_mode_dec = rd_dec(rec_buf32, CFG_VIEW_MODE_DEC, 1);
    expected = rd_dec(rec_buf32, CFG_CHKSUM_DEC, 5);
    if (sel_col_dec == 0xFFFF || sel_idx0_dec == 0xFFFF || sel_idx1_dec == 0xFFFF ||
        sel_idx2_dec == 0xFFFF || view_mode_dec == 0xFFFF || expected == 0xFFFF) {
        rel_session_close(LFN_CFG);
        scratch_cfg_variants();
        selected_col = 1;
        selected_idx[0] = 0;
        selected_idx[1] = 0;
        selected_idx[2] = 0;
        view_mode = VIEW_ONE_EXPANDED;
        return save_config_rel();
    }

    selected_col = (unsigned char)sel_col_dec;
    selected_idx[0] = (unsigned char)sel_idx0_dec;
    selected_idx[1] = (unsigned char)sel_idx1_dec;
    selected_idx[2] = (unsigned char)sel_idx2_dec;
    view_mode = (unsigned char)view_mode_dec;

    if (selected_col >= NUM_COLUMNS ||
        (view_mode != VIEW_ONE_EXPANDED && view_mode != VIEW_TWO_EXPANDED) ||
        expected != cfg_checksum()) {
        rel_session_close(LFN_CFG);
        scratch_cfg_variants();
        selected_col = 1;
        selected_idx[0] = 0;
        selected_idx[1] = 0;
        selected_idx[2] = 0;
        view_mode = VIEW_ONE_EXPANDED;
        return save_config_rel();
    }

    rel_session_close(LFN_CFG);
    return 0;
}

static unsigned char init_board_rel(void) {
    unsigned int id;

    io_trace_mark('L', TUI_COLOR_YELLOW);
    clear_board_model();
    build_id_layout_maps();
    build_superblock_record();

    if (open_board_session() != 0) return 1;
    if (rel_write_record_session(LFN_BOARD, REC_BOARD_SUPER, rec_buf64, REC_BOARD_LEN) != 0) {
        rel_session_close(LFN_BOARD);
        return 1;
    }
    for (id = 1; id <= MAX_CARDS; ++id) {
        if (encode_card_record(id) != 0) {
            rel_session_close(LFN_BOARD);
            return 1;
        }
        if (rel_write_record_session(LFN_BOARD,
                                     (unsigned int)(REC_BOARD_CARD_BASE + (id - 1)),
                                     rec_buf64,
                                     REC_BOARD_LEN) != 0) {
            rel_session_close(LFN_BOARD);
            return 1;
        }
        if ((id & 0x1F) == 0 || id == MAX_CARDS) io_trace_dot(TUI_COLOR_YELLOW);
    }
    rel_session_close(LFN_BOARD);
    return 0;
}

static unsigned char load_board_rel_with_retry(void) {
    unsigned char attempt;
    for (attempt = 0; attempt < 2; ++attempt) {
        if (load_board_rel() == 0) return 0;
        if (!rel_error_is_transient_io()) return 1;
    }
    return 1;
}

static unsigned char init_or_load_board_rel(void) {
    io_trace_mark('B', TUI_COLOR_YELLOW);
    if (load_board_rel_with_retry() != 0) {
        if (!rel_error_is_initable() && !rel_error_is_super_short_read_initable()) return 1;
        if (init_board_rel() != 0) return 1;
        if (load_board_rel_with_retry() != 0) return 1;
    }
    return 0;
}

static unsigned char load_dizzy_storage(unsigned char preserve_trace) {
    rel_clear_error();
#if DIZZY_IO_TRACE
    if (!preserve_trace || !io_trace_active) io_trace_reset("LOAD");
#else
    (void)preserve_trace;
#endif
    io_trace_mark('A', TUI_COLOR_YELLOW);

    if (init_or_load_board_rel() != 0) {
        storage_loaded = 0;
        clear_board_model();
        format_rel_error_status("LOAD BRD ");
        return 1;
    }
    io_trace_mark('E', TUI_COLOR_YELLOW);
    io_trace_mark('F', TUI_COLOR_YELLOW);

    if (init_or_load_cfg_rel() != 0) {
        storage_loaded = 0;
        clear_board_model();
        format_rel_error_status("LOAD CFG ");
        return 1;
    }
    io_trace_mark('G', TUI_COLOR_YELLOW);
    storage_loaded = 1;
    io_trace_mark('J', TUI_COLOR_LIGHTGREEN);
    return 0;
}

static unsigned char rebuild_dizzy_storage(void) {
    io_trace_reset("REBLD");
    io_trace_mark('K', TUI_COLOR_YELLOW);
    rel_clear_error();
    clear_board_model();
    scratch_board_variants();
    scratch_cfg_variants();

    if (init_board_rel() != 0) {
        storage_loaded = 0;
        format_rel_error_status("REBLD BRD ");
        return 1;
    }
    if (verify_board_superblock_once() != 0) {
        storage_loaded = 0;
        format_rel_error_status("REBLD BRD ");
        return 1;
    }
    selected_col = 1;
    selected_idx[0] = 0;
    selected_idx[1] = 0;
    selected_idx[2] = 0;
    view_mode = VIEW_ONE_EXPANDED;
    if (save_config_rel() != 0) {
        storage_loaded = 0;
        format_rel_error_status("REBLD CFG ");
        return 1;
    }
    storage_loaded = 0;
    return load_dizzy_storage(1);
}

/*---------------------------------------------------------------------------
 * Board init/load
 *---------------------------------------------------------------------------*/

static void init_empty_board(void) {
    clear_board_model();
}

/*---------------------------------------------------------------------------
 * UI helpers
 *---------------------------------------------------------------------------*/

static unsigned char edit_text_popup(const char *title, char *buf, unsigned char maxlen) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    win.x = 2;
    win.y = 9;
    win.w = 36;
    win.h = 7;

    tui_window_title(&win, title, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(4, 11, "TEXT:", TUI_COLOR_WHITE);
    tui_input_init(&input, 10, 11, 25, maxlen, buf, TUI_COLOR_CYAN);
    input.cursor = strlen(buf);
    tui_input_draw(&input);
    tui_puts(4, 13, "RET:OK  STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) return 0;
        if (tui_input_key(&input, key)) return 1;
        tui_input_draw(&input);
    }
}

static unsigned char confirm_popup(const char *msg) {
    TuiRect win;
    unsigned char key;

    win.x = 8;
    win.y = 10;
    win.w = 24;
    win.h = 5;

    tui_window_title(&win, "CONFIRM", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts_n(10, 12, msg, 20, TUI_COLOR_WHITE);
    tui_puts(10, 13, "Y: YES   N: NO", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == 'y' || key == 'Y') return 1;
        if (key == 'n' || key == 'N' || key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) return 0;
    }
}

static unsigned char column_is_expanded(unsigned char col) {
    unsigned char next;

    if (view_mode == VIEW_ONE_EXPANDED) {
        return (col == selected_col);
    }

    next = (unsigned char)((selected_col + 1) % NUM_COLUMNS);
    return (col == selected_col || col == next);
}

static void compute_layout(void) {
    unsigned char col;
    unsigned char next;
    unsigned char x;

    for (col = 0; col < NUM_COLUMNS; ++col) {
        col_expanded[col] = column_is_expanded(col);
        col_w[col] = COLLAPSED_W;
    }

    if (view_mode == VIEW_ONE_EXPANDED) {
        col_w[selected_col] = (unsigned char)(40 -
                                              (COLLAPSED_W * (NUM_COLUMNS - 1)) -
                                              (COL_GAP * (NUM_COLUMNS - 1)));
    } else {
        next = (unsigned char)((selected_col + 1) % NUM_COLUMNS);
        col_w[selected_col] = 18;
        col_w[next] = 17;
    }

    x = 0;
    for (col = 0; col < NUM_COLUMNS; ++col) {
        col_x[col] = x;
        x = (unsigned char)(x + col_w[col]);
        if ((unsigned char)(col + 1) < NUM_COLUMNS) {
            x = (unsigned char)(x + COL_GAP);
        }
    }
}

static void draw_vertical_label(unsigned char x,
                                unsigned char y,
                                unsigned char max_rows,
                                const char *label,
                                unsigned char color) {
    unsigned char i;
    unsigned char row;
    unsigned char ch;

    i = 0;
    row = 0;

    while (label[i] != 0 && row < max_rows) {
        ch = (unsigned char)label[i++];
        if (ch == ' ') {
            ++row;
            continue;
        }
        tui_putc((unsigned char)(x + 1), (unsigned char)(y + row),
                 (unsigned char)(tui_ascii_to_screen(ch) | 0x80), color);
        ++row;
    }
}

static void draw_collapsed_column(unsigned char col) {
    unsigned char x;
    unsigned char w;
    unsigned char y;
    unsigned char dx;
    unsigned char color;
    unsigned char tlen;
    unsigned char ch;

    x = col_x[col];
    w = col_w[col];
    color = col_bar_colors[col];

    for (y = COL_TITLE_Y; y < (unsigned char)(LIST_TOP_Y + LIST_ROWS); ++y) {
        for (dx = 0; dx < w; ++dx) {
            tui_putc((unsigned char)(x + dx), y, 0xA0, color);
        }
    }

    for (dx = 0; dx < w; ++dx) {
        tui_putc((unsigned char)(x + dx), COL_TITLE_Y, 0xA0, color);
    }

    u16_to_dec(col_count[col], num_buf);
    tlen = strlen(num_buf);
    if (tlen > 2) tlen = 2;
    if (tlen == 1) {
        ch = (unsigned char)(tui_ascii_to_screen((unsigned char)num_buf[0]) | 0x80);
        tui_putc((unsigned char)(x + 1), COL_TITLE_Y, ch, color);
    } else {
        ch = (unsigned char)(tui_ascii_to_screen((unsigned char)num_buf[0]) | 0x80);
        tui_putc(x, COL_TITLE_Y, ch, color);
        ch = (unsigned char)(tui_ascii_to_screen((unsigned char)num_buf[1]) | 0x80);
        tui_putc((unsigned char)(x + 1), COL_TITLE_Y, ch, color);
    }

    draw_vertical_label(x,
                        (unsigned char)(COL_TITLE_Y + 4),
                        (unsigned char)(LIST_ROWS - 3),
                        col_titles[col],
                        color);
}

static unsigned char card_row_flag(const Card *c) {
    if (c->flags & CARD_FLAG_ARCHIVED) return 'A';
    if (c->flags & CARD_FLAG_DONE) return 'X';
    if (c->flags & CARD_FLAG_SNOOZED) return 'Z';
    return ' ';
}

static void draw_expanded_column(unsigned char col) {
    unsigned char x;
    unsigned char w;
    unsigned char row;
    unsigned char y;
    unsigned char idx;
    unsigned int id;
    Card *c;
    unsigned char tlen;
    unsigned char dx;
    unsigned char color;
    unsigned char row_color;
    unsigned char head_color;
    unsigned char ch;

    x = col_x[col];
    w = col_w[col];
    color = col_bar_colors[col];
    head_color = (col == selected_col) ? col_hi_colors[col] : color;

    for (dx = 0; dx < w; ++dx) {
        tui_putc((unsigned char)(x + dx), COL_TITLE_Y, 0xA0,
                 head_color);
    }

    for (dx = 0; dx < (unsigned char)(w - 3) && col_titles[col][dx] != 0; ++dx) {
        ch = (unsigned char)(tui_ascii_to_screen((unsigned char)col_titles[col][dx]) | 0x80);
        tui_putc((unsigned char)(x + dx), COL_TITLE_Y, ch, head_color);
    }

    u16_to_dec(col_count[col], num_buf);
    tlen = strlen(num_buf);
    if (tlen >= w) tlen = (unsigned char)(w - 1);
    for (dx = 0; dx < tlen; ++dx) {
        ch = (unsigned char)(tui_ascii_to_screen((unsigned char)num_buf[dx]) | 0x80);
        tui_putc((unsigned char)(x + w - tlen + dx), COL_TITLE_Y, ch, head_color);
    }

    for (row = 0; row < LIST_BODY_ROWS; ++row) {
        y = (unsigned char)(LIST_BODY_Y + row);
        row_color = color;
        idx = (unsigned char)(scroll_row[col] + row);
        if (col == selected_col && idx == selected_idx[col]) {
            row_color = col_hi_colors[col];
        }
        for (dx = 0; dx < w; ++dx) {
            tui_putc((unsigned char)(x + dx), y, 0xA0, row_color);
        }

        if (idx >= visible_count[col]) continue;

        id = visible_ids[col][idx];
        c = card_by_id(id);
        if (c == 0) continue;

        line_buf[0] = (char)card_row_flag(c);
        line_buf[1] = ' ';
        line_buf[2] = 0;
        append_text(line_buf, c->title, (unsigned char)(w + 1));

        for (dx = 0; dx < w && line_buf[dx] != 0; ++dx) {
            if (line_buf[dx] == ' ') continue;
            ch = (unsigned char)(tui_ascii_to_screen((unsigned char)line_buf[dx]) | 0x80);
            tui_putc((unsigned char)(x + dx), y, ch, row_color);
        }
    }
}

static void draw_columns(void) {
    unsigned char col;
    unsigned char y;
    unsigned char sx;

    compute_layout();

    for (col = 1; col < NUM_COLUMNS; ++col) {
        sx = (unsigned char)(col_x[col] - 1);
        for (y = COL_TITLE_Y; y < (unsigned char)(LIST_TOP_Y + LIST_ROWS); ++y) {
            tui_putc(sx, y, 0xA0, TUI_COLOR_GRAY3);
        }
    }

    for (col = 0; col < NUM_COLUMNS; ++col) {
        if (col_expanded[col]) {
            draw_expanded_column(col);
        } else {
            draw_collapsed_column(col);
        }
    }

    for (col = 0; col < NUM_COLUMNS; ++col) {
        for (sx = 0; sx < col_w[col]; ++sx) {
            tui_putc((unsigned char)(col_x[col] + sx), LIST_SEP_Y, 0xA0, TUI_COLOR_GRAY3);
        }
    }
}

static void draw_status(void) {
    tui_puts_n(0, STATUS_Y, "                                        ", 40, TUI_COLOR_WHITE);
    tui_puts_n(0, (unsigned char)(STATUS_Y + 1), "                                        ", 40, TUI_COLOR_WHITE);
    tui_puts_n(0, (unsigned char)(STATUS_Y + 2), "                                        ", 40, TUI_COLOR_WHITE);

    tui_puts_n(0, STATUS_Y, status_msg, 39, status_color);

    tui_puts(0, (unsigned char)(STATUS_Y + 1), "SEARCH:", TUI_COLOR_GRAY3);
    tui_puts_n(7, (unsigned char)(STATUS_Y + 1), search_buf, 20, TUI_COLOR_YELLOW);

    tui_puts(29, (unsigned char)(STATUS_Y + 1), "VIEW:", TUI_COLOR_GRAY3);
    if (view_mode == VIEW_TWO_EXPANDED) {
        tui_puts(34, (unsigned char)(STATUS_Y + 1), "DOUBLE", TUI_COLOR_LIGHTGREEN);
    } else {
        tui_puts(34, (unsigned char)(STATUS_Y + 1), "SINGLE", TUI_COLOR_LIGHTGREEN);
    }
}

static void draw_help(void) {
    if (!storage_loaded) {
        tui_puts_n(0, HELP1_Y, "BOARD NOT LOADED (PRESS L TO LOAD)", 40, TUI_COLOR_GRAY3);
        tui_puts_n(0, HELP2_Y, "                                        ", 40, TUI_COLOR_GRAY3);
        return;
    }
    tui_puts_n(0, HELP1_Y, "F1:NEW RET:EDIT +/-:MOVE ,/.:REORDER", 40, TUI_COLOR_GRAY3);
    tui_puts_n(0, HELP2_Y, "DEL:DELETE /:SEARCH F6/F7 VIEW F8:HELP", 40, TUI_COLOR_GRAY3);
}

static void show_help_screen(void) {
    tui_clear(TUI_COLOR_BLUE);
    tui_puts_n(1, 1, "DIZZY KANBAN HELP", 38, TUI_COLOR_YELLOW);
    tui_puts_n(1, 3, "F1: NEW CARD", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 4, "RET: EDIT SELECTED CARD", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 5, "ARROWS: MOVE FOCUS", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 6, "UP/DN: MOVE CARD IN COLUMN", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 7, ",/. : REORDER CARD", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 8, "D/A/S: TOGGLE FLAGS   DEL: DELETE", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 9, "F6: TOGGLE SINGLE/DOUBLE", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 10, "+/-: MOVE BETWEEN COLUMNS", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 11, "/: SEARCH  C: CLEAR SEARCH", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 12, "F7: CYCLE FOCUS COLUMN", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 13, "L:LOAD R:REBLD", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 14, "F8: HELP  F2/F4: SWITCH APPS", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 15, "CTRL+B: HOME/RETURN", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 16, "PRESS ANY KEY TO RETURN", 38, TUI_COLOR_CYAN);
    (void)tui_getkey();
}

static void draw_screen(void) {
    TuiRect header;
    header.x = 0;
    header.y = HEADER_Y;
    header.w = 40;
    header.h = 2;
    tui_window_title(&header, "DIZZY KANBAN", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    rebuild_visible_lists();
    draw_columns();
    draw_status();
    draw_help();
}

/*---------------------------------------------------------------------------
 * Actions
 *---------------------------------------------------------------------------*/

static unsigned char persist_board(const char *ok_msg) {
    unsigned char ok;
    (void)ok_msg;

    if (!storage_loaded) return 1;

    begin_save_indicator();
    ok = (write_board_rel() == 0);
    if (ok) ok = (save_config_rel() == 0);
    end_save_indicator(ok);
    return ok ? 0 : 1;
}

static void action_new_card(void) {
    strcpy(text_buf, "");

    if (!edit_text_popup("NEW CARD", text_buf, CARD_TITLE_MAX)) {
        set_status("CANCELLED", TUI_COLOR_GRAY3);
        return;
    }

    if (add_card_model(text_buf) != 0) {
        set_status("CREATE FAILED", TUI_COLOR_LIGHTRED);
        return;
    }

    (void)persist_board("CARD CREATED");
}

static void action_edit_card(void) {
    Card *c;

    c = selected_card();
    if (c == 0) {
        set_status("NO CARD SELECTED", TUI_COLOR_YELLOW);
        return;
    }

    strncpy(text_buf, c->title, CARD_TITLE_MAX);
    text_buf[CARD_TITLE_MAX] = 0;

    if (!edit_text_popup("EDIT CARD", text_buf, CARD_TITLE_MAX)) {
        set_status("CANCELLED", TUI_COLOR_GRAY3);
        return;
    }

    c->title_len = strlen(text_buf);
    memcpy(c->title, text_buf, c->title_len);
    c->title[c->title_len] = 0;

    (void)persist_board("CARD UPDATED");
}

static void action_search(void) {
    strncpy(text_buf, search_buf, sizeof(search_buf) - 1);
    text_buf[sizeof(search_buf) - 1] = 0;

    if (!edit_text_popup("SEARCH", text_buf, sizeof(search_buf) - 1)) {
        set_status("SEARCH UNCHANGED", TUI_COLOR_GRAY3);
        return;
    }

    strncpy(search_buf, text_buf, sizeof(search_buf) - 1);
    search_buf[sizeof(search_buf) - 1] = 0;
    set_status("FILTER APPLIED", TUI_COLOR_LIGHTGREEN);
}

static unsigned char handle_global_switch(unsigned char key) {
    unsigned char nav_action;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        resume_save_state();
        tui_return_to_launcher();
        return 1;
    }

    if (nav_action >= 1 && nav_action <= 23) {
        resume_save_state();
        tui_switch_to_app(nav_action);
        return 1;
    }

    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return 1;
    }

    return 0;
}

static void dizzy_loop(void) {
    unsigned char key;
    unsigned char redraw_mode;

    draw_screen();

    while (running) {
        key = tui_getkey();
        if (handle_global_switch(key)) continue;

        if (key == 'l' || key == 'L') {
            begin_load_indicator();
            (void)load_dizzy_storage(0);
            end_load_indicator();
            continue;
        }
        if (key == 'r' || key == 'R') {
            if (confirm_popup("REBUILD BOARD?")) {
                begin_load_indicator();
                (void)rebuild_dizzy_storage();
                end_load_indicator();
            } else {
                set_status("CANCELLED", TUI_COLOR_GRAY3);
                draw_screen();
            }
            continue;
        }

        if (!storage_loaded) {
            if (key == TUI_KEY_RUNSTOP) {
                running = 0;
                continue;
            }
            draw_screen();
            continue;
        }

        redraw_mode = 0;

        switch (key) {
            case TUI_KEY_RUNSTOP:
                running = 0;
                break;

            case TUI_KEY_LEFT:
                if (selected_col > 0) --selected_col;
                break;

            case TUI_KEY_RIGHT:
                if ((unsigned char)(selected_col + 1) < NUM_COLUMNS) ++selected_col;
                break;

            case TUI_KEY_UP:
                if (selected_idx[selected_col] > 0) {
                    --selected_idx[selected_col];
                    redraw_mode = 1;
                } else {
                    redraw_mode = 2;
                }
                break;

            case TUI_KEY_DOWN:
                if ((unsigned char)(selected_idx[selected_col] + 1) < visible_count[selected_col]) {
                    ++selected_idx[selected_col];
                    redraw_mode = 1;
                } else {
                    redraw_mode = 2;
                }
                break;

            case ',':
                if (swap_selected_with_prev_model() == 0) {
                    if (selected_idx[selected_col] > 0) --selected_idx[selected_col];
                    (void)persist_board("CARD MOVED UP");
                } else {
                    set_status("MOVE FAILED", TUI_COLOR_LIGHTRED);
                }
                break;

            case '.':
                if (swap_selected_with_next_model() == 0) {
                    if ((unsigned char)(selected_idx[selected_col] + 1) < visible_count[selected_col]) {
                        ++selected_idx[selected_col];
                    }
                    (void)persist_board("CARD MOVED DOWN");
                } else {
                    set_status("MOVE FAILED", TUI_COLOR_LIGHTRED);
                }
                break;

            case TUI_KEY_F1:
                action_new_card();
                break;

            case TUI_KEY_F3:
            case TUI_KEY_RETURN:
                action_edit_card();
                break;

            case '+':
            case '=':
                if (move_selected_horizontal_model(1) != 0) {
                    set_status("MOVE RIGHT FAILED", TUI_COLOR_LIGHTRED);
                } else {
                    (void)persist_board("CARD MOVED RIGHT");
                }
                break;

            case '-':
                if (move_selected_horizontal_model(-1) != 0) {
                    set_status("MOVE LEFT FAILED", TUI_COLOR_LIGHTRED);
                } else {
                    (void)persist_board("CARD MOVED LEFT");
                }
                break;

            case 'd':
            case 'D': {
                Card *c;
                c = selected_card();
                if (c == 0) {
                    set_status("NO CARD SELECTED", TUI_COLOR_YELLOW);
                } else {
                    c->flags ^= CARD_FLAG_DONE;
                    (void)persist_board("DONE TOGGLED");
                }
                break;
            }

            case 'a':
            case 'A': {
                Card *c2;
                c2 = selected_card();
                if (c2 == 0) {
                    set_status("NO CARD SELECTED", TUI_COLOR_YELLOW);
                } else {
                    c2->flags ^= CARD_FLAG_ARCHIVED;
                    (void)persist_board("ARCHIVE TOGGLED");
                }
                break;
            }

            case 's':
            case 'S': {
                Card *c3;
                c3 = selected_card();
                if (c3 == 0) {
                    set_status("NO CARD SELECTED", TUI_COLOR_YELLOW);
                } else {
                    if (c3->snooze_day <= today_day) {
                        c3->snooze_day = (unsigned int)(today_day + 1);
                    } else if (c3->snooze_day == (unsigned int)(today_day + 1)) {
                        c3->snooze_day = (unsigned int)(today_day + 3);
                    } else if (c3->snooze_day == (unsigned int)(today_day + 3)) {
                        c3->snooze_day = (unsigned int)(today_day + 7);
                    } else {
                        c3->snooze_day = 0;
                    }

                    if (c3->snooze_day != 0) {
                        c3->flags |= CARD_FLAG_SNOOZED;
                    } else {
                        c3->flags &= (unsigned char)~CARD_FLAG_SNOOZED;
                    }

                    (void)persist_board("SNOOZE UPDATED");
                }
                break;
            }

            case TUI_KEY_DEL: {
                Card *c4;
                c4 = selected_card();
                if (c4 == 0) {
                    set_status("NO CARD SELECTED", TUI_COLOR_YELLOW);
                } else {
                    if (!confirm_popup("DELETE CARD?")) {
                        set_status("CANCELLED", TUI_COLOR_GRAY3);
                    } else if (delete_selected_model() != 0) {
                        set_status("DELETE FAILED", TUI_COLOR_LIGHTRED);
                    } else {
                        (void)persist_board("CARD DELETED");
                    }
                }
                break;
            }

            case '/':
                action_search();
                break;

            case 'c':
            case 'C':
                search_buf[0] = 0;
                set_status("FILTER CLEARED", TUI_COLOR_LIGHTGREEN);
                break;

            case TUI_KEY_F8:
                show_help_screen();
                break;

            case TUI_KEY_F6:
                if (view_mode == VIEW_TWO_EXPANDED) {
                    view_mode = VIEW_ONE_EXPANDED;
                    set_status("VIEW SINGLE", TUI_COLOR_LIGHTGREEN);
                } else {
                    view_mode = VIEW_TWO_EXPANDED;
                    set_status("VIEW DOUBLE", TUI_COLOR_LIGHTGREEN);
                }
                break;

            case TUI_KEY_F7:
                selected_col = (unsigned char)((selected_col + 1) % NUM_COLUMNS);
                break;

            default:
                break;
        }
        if (redraw_mode == 2) continue;
        if (redraw_mode == 1) {
            rebuild_visible_lists();
            draw_expanded_column(selected_col);
            draw_status();
            continue;
        }
        draw_screen();
    }

    __asm__("jmp $FCE2");
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

int main(void) {
    unsigned char bank;

    tui_init();

    running = 1;
    resume_ready = 0;
    today_day = parse_compile_day();
    search_buf[0] = 0;
    storage_loaded = 0;
    rel_clear_error();
#if DIZZY_IO_TRACE
    io_trace_active = 0;
    io_trace_len = 0;
    io_trace[0] = 0;
#endif
    init_empty_board();
    set_status("", TUI_COLOR_GRAY3);

    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 23) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();

    dizzy_loop();
    return 0;
}
