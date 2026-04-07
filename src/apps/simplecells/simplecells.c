/*
 * simplecells.c - ReadyOS spreadsheet app
 *
 * Single-sheet spreadsheet tuned for low redraw cost on the C64.
 */

#include "../../lib/tui.h"
#include "../../lib/dir_page.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"
#include "../../lib/storage_device.h"

#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

#define HEADER_Y          0
#define FORMULA_Y         3
#define COLHDR_Y          4
#define GRID_Y            5
#define GRID_ROWS         18
#define STATUS_Y          23
#define HELP_Y1           24

#define ROW_LABEL_W       2
#define GRID_X            3
#define GRID_W            37

#define MAX_ROWS          18
#define MAX_COLS          10
#define MAX_CELLS         (MAX_ROWS * MAX_COLS)

#define DEFAULT_COL_W     6
#define MIN_COL_W         4
#define MAX_COL_W         12

#define CONTENT_POOL_SIZE 320
#define MAX_RAW_LEN       27
#define RESULT_TEXT_LEN   14
#define MAX_EVAL_DEPTH    3
#define MAX_DIR_ENTRIES   18

#define KEY_CTRL_B        2
#define KEY_COPY          'c'
#define KEY_PASTE         'v'

#define LFN_DIR           1
#define LFN_FILE          2
#define LFN_CMD           15

#define CR                0x0D

#define FILE_FMT_MAGIC0   'S'
#define FILE_FMT_MAGIC1   'C'
#define FILE_FMT_MAGIC2   'L'
#define FILE_FMT_MAGIC3   'S'
#define FILE_FMT_MAGIC4   '1'
#define FILE_FMT_VERSION  2
#define FILE_END_MARKER   0xFF

#define CELL_FLAG_USED    0x01
#define CELL_FLAG_FORMULA 0x02
#define CELL_FLAG_DIRTY   0x04

#define CELL_COLOR_DEFAULT 0

#define COLTYPE_GENERAL   0
#define COLTYPE_TEXT      1
#define COLTYPE_INT       2
#define COLTYPE_FLOAT2    3
#define COLTYPE_CURRENCY  4

#define VALUE_EMPTY       0
#define VALUE_NUMBER      1
#define VALUE_TEXT        2
#define VALUE_ERROR       3

#define ERR_NONE          0
#define ERR_SYNTAX        1
#define ERR_REF           2
#define ERR_DIV0          3
#define ERR_CYCLE         4
#define ERR_TYPE          5
#define ERR_LONG          6

#define ROMFP_IN_LEN      32
#define ROMFP_TEXT_LEN    32
#define ROMFP_IN_ADDR     0xC580U
#define ROMFP_A_ADDR      0xC5A0U
#define ROMFP_B_ADDR      0xC5A5U
#define ROMFP_OUT_ADDR    0xC5AAU
#define ROMFP_TEXT_ADDR   0xC5AFU

typedef struct {
    unsigned char kind;
    unsigned char error;
    unsigned char text_len;
    unsigned char num[5];
    char text[RESULT_TEXT_LEN + 1];
} ScValue;

static unsigned int cell_off[MAX_CELLS];
static unsigned char cell_len[MAX_CELLS];
static unsigned char cell_flags[MAX_CELLS];
static unsigned char cell_color[MAX_CELLS];
static unsigned char col_width[MAX_COLS];
static unsigned char col_type[MAX_COLS];
static unsigned char content_pool[CONTENT_POOL_SIZE];
static unsigned int content_used;

static unsigned int eval_stack[MAX_EVAL_DEPTH];
static unsigned char eval_stack_depth;
static ScValue eval_tmp_a[MAX_EVAL_DEPTH];
static ScValue eval_tmp_b[MAX_EVAL_DEPTH];
static char eval_arg_buf[MAX_EVAL_DEPTH][MAX_RAW_LEN + 1];

static unsigned char active_row;
static unsigned char active_col;
static unsigned char scroll_col;
static unsigned char running;
static unsigned char modified;
static unsigned char resume_ready;

static char filename[16];
static char status_msg[32];
static unsigned char status_color;

static unsigned char clipboard_valid;
static unsigned char clipboard_src_row;
static unsigned char clipboard_src_col;
static unsigned char clipboard_color;
static unsigned char clipboard_len;
static char clipboard_raw[MAX_RAW_LEN + 1];

static DirPageEntry dir_entries[MAX_DIR_ENTRIES];
static const char *dir_ptrs[MAX_DIR_ENTRIES];
static unsigned char dir_count;
static char dialog_buf[MAX_RAW_LEN + 1];
static char draw_display_buf[RESULT_TEXT_LEN + 2];
static char draw_coord_buf[4];
static char format_general_buf[ROMFP_TEXT_LEN];
static char numeric_work_buf[MAX_RAW_LEN + 1];
static unsigned char file_raw_buf[MAX_RAW_LEN + 1];
static signed long format_value_fp;

static unsigned char visible_cols[MAX_COLS];
static unsigned char visible_x[MAX_COLS];
static unsigned char visible_w[MAX_COLS];
static unsigned char visible_count;

static const unsigned char* const romfp_in = (unsigned char*)ROMFP_IN_ADDR;
static unsigned char* const romfp_a = (unsigned char*)ROMFP_A_ADDR;
static unsigned char* const romfp_b = (unsigned char*)ROMFP_B_ADDR;
static unsigned char* const romfp_out = (unsigned char*)ROMFP_OUT_ADDR;
static unsigned char* const romfp_text = (unsigned char*)ROMFP_TEXT_ADDR;

extern void romfp_eval_literal(void);
extern void romfp_add(void);
extern void romfp_sub(void);
extern void romfp_div(void);
extern void romfp_to_str(void);
extern void romfp_int(void);

static ResumeWriteSegment simplecells_resume_write_segments[] = {
    { cell_off, sizeof(cell_off) },
    { cell_len, sizeof(cell_len) },
    { cell_flags, sizeof(cell_flags) },
    { cell_color, sizeof(cell_color) },
    { col_width, sizeof(col_width) },
    { col_type, sizeof(col_type) },
    { content_pool, sizeof(content_pool) },
    { &content_used, sizeof(content_used) },
    { &active_row, sizeof(active_row) },
    { &active_col, sizeof(active_col) },
    { &scroll_col, sizeof(scroll_col) },
    { &modified, sizeof(modified) },
    { filename, sizeof(filename) },
    { &clipboard_valid, sizeof(clipboard_valid) },
    { &clipboard_src_row, sizeof(clipboard_src_row) },
    { &clipboard_src_col, sizeof(clipboard_src_col) },
    { &clipboard_color, sizeof(clipboard_color) },
    { &clipboard_len, sizeof(clipboard_len) },
    { clipboard_raw, sizeof(clipboard_raw) },
};

static ResumeReadSegment simplecells_resume_read_segments[] = {
    { cell_off, sizeof(cell_off) },
    { cell_len, sizeof(cell_len) },
    { cell_flags, sizeof(cell_flags) },
    { cell_color, sizeof(cell_color) },
    { col_width, sizeof(col_width) },
    { col_type, sizeof(col_type) },
    { content_pool, sizeof(content_pool) },
    { &content_used, sizeof(content_used) },
    { &active_row, sizeof(active_row) },
    { &active_col, sizeof(active_col) },
    { &scroll_col, sizeof(scroll_col) },
    { &modified, sizeof(modified) },
    { filename, sizeof(filename) },
    { &clipboard_valid, sizeof(clipboard_valid) },
    { &clipboard_src_row, sizeof(clipboard_src_row) },
    { &clipboard_src_col, sizeof(clipboard_src_col) },
    { &clipboard_color, sizeof(clipboard_color) },
    { &clipboard_len, sizeof(clipboard_len) },
    { clipboard_raw, sizeof(clipboard_raw) },
};

#define SIMPLECELLS_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(simplecells_resume_read_segments) / sizeof(simplecells_resume_read_segments[0])))

static const char *format_menu_items[] = {
    "cell color",
    "col type",
    "col width",
    "recalc",
    "compact",
};

static const char *color_menu_items[] = {
    "default",
    "yellow",
    "green",
    "red",
    "blue",
};

static const unsigned char color_menu_values[] = {
    CELL_COLOR_DEFAULT,
    TUI_COLOR_YELLOW,
    TUI_COLOR_GREEN,
    TUI_COLOR_RED,
    TUI_COLOR_LIGHTBLUE,
};

static const char *type_menu_items[] = {
    "general",
    "text",
    "integer",
    "float2",
    "currency",
};

static const char *width_menu_items[] = {
    "4 chars",
    "6 chars",
    "8 chars",
    "10 chars",
    "12 chars",
};

static const unsigned char width_menu_values[] = {
    4, 6, 8, 10, 12
};

static void set_status(const char *msg, unsigned char color);
static void init_defaults(void);
static void mark_formula_cells_dirty(void);
static unsigned char sheet_validate_state(void);
static void reset_sheet_runtime(const char *msg, unsigned char color);
static void clamp_view(void);
static void ensure_active_col_visible(void);
static void compute_visible_columns(void);
static void draw_screen(void);
static void draw_header(void);
static void draw_formula_bar(void);
static void draw_column_headers(void);
static void draw_grid(void);
static void draw_grid_row(unsigned char row_on_screen);
static void draw_status(void);
static void draw_help(void);
static void redraw_active_transition(unsigned char old_row, unsigned char old_col);
static void redraw_active_cell(void);
static void show_help_popup(void);
static unsigned char show_simple_menu(const char *title,
                                      const char **items,
                                      unsigned char count,
                                      unsigned char initial);
static void show_format_menu(void);
static unsigned char show_confirm(const char *msg);
static void show_message(const char *msg, unsigned char color);
static unsigned char read_directory(unsigned char start_index,
                                    unsigned char *out_total);
static unsigned char show_open_dialog(void);
static unsigned char show_save_dialog(void);
static unsigned char file_save(const char *name);
static unsigned char file_load(const char *name);
static unsigned char edit_value_inline(void);
static unsigned char edit_formula_popup(void);
static void copy_cell_to_local_clipboard(void);
static void paste_cell_from_local_clipboard(void);
static void clear_active_cell(void);
static unsigned char compact_content_pool(void);
static unsigned char restore_resume_state(void);
static void resume_save_state(void);
static void fill_grid_display(unsigned char row,
                              unsigned char col,
                              unsigned char allow_formula,
                              ScValue *value,
                              char *display);
static void value_set_text(ScValue *value, const char *text);
static void eval_literal_cell(const char *raw, unsigned char col, ScValue *out);
static unsigned char eval_cell_value(unsigned char row,
                                     unsigned char col,
                                     unsigned char depth,
                                     ScValue *out,
                                     unsigned char *err);

static unsigned char ascii_upper(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (unsigned char)(ch - ('a' - 'A'));
    }
    return ch;
}

static unsigned char is_digit(unsigned char ch) {
    return (ch >= '0' && ch <= '9') ? 1 : 0;
}

static unsigned char is_alpha(unsigned char ch) {
    ch = ascii_upper(ch);
    return (ch >= 'A' && ch <= 'Z') ? 1 : 0;
}

static void str_copy_fit(char *dst, const char *src, unsigned char cap) {
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = 0;
}

static void trim_span(const char *src, unsigned char *out_start, unsigned char *out_end) {
    unsigned char s;
    unsigned char e;

    s = 0;
    e = (unsigned char)strlen(src);
    while (s < e && src[s] == ' ') {
        ++s;
    }
    while (e > s && src[e - 1u] == ' ') {
        --e;
    }
    *out_start = s;
    *out_end = e;
}

static void trim_text(char *text) {
    unsigned char s;
    unsigned char e;
    unsigned char i;

    trim_span(text, &s, &e);
    if (s == 0u && text[e] == 0) {
        return;
    }
    if (s >= e) {
        text[0] = 0;
        return;
    }
    for (i = 0; (unsigned char)(s + i) < e; ++i) {
        text[i] = text[s + i];
    }
    text[i] = 0;
}

static unsigned int cell_index(unsigned char row, unsigned char col) {
    return (unsigned int)row * MAX_COLS + col;
}

static unsigned char cell_used_idx(unsigned int idx) {
    return (unsigned char)(cell_flags[idx] & CELL_FLAG_USED);
}

static const char *cell_raw_ptr_idx(unsigned int idx) {
    if (!cell_used_idx(idx) || cell_off[idx] == 0xFFFFu || cell_off[idx] >= CONTENT_POOL_SIZE) {
        return "";
    }
    return (const char*)&content_pool[cell_off[idx]];
}

static void tf_copy(unsigned char *dst, const unsigned char *src) {
    unsigned char i;
    for (i = 0; i < 5u; ++i) {
        dst[i] = src[i];
    }
}

static void tf_from_literal(const char *lit, unsigned char *out) {
    str_copy_fit((char*)romfp_in, lit, ROMFP_IN_LEN);
    romfp_eval_literal();
    tf_copy(out, romfp_out);
}

static unsigned char tf_is_zero(const unsigned char *value) {
    return (value[0] == 0u) ? 1u : 0u;
}

static void tf_packed_to_str(const unsigned char *packed, char *out) {
    tf_copy(romfp_a, packed);
    romfp_to_str();
    str_copy_fit(out, (const char*)romfp_text, ROMFP_TEXT_LEN);
    trim_text(out);
    if (out[0] == 0) {
        strcpy(out, "0");
    }
}

static void int_to_str(signed int value, char *out) {
    signed long work;
    unsigned char pos;
    char rev[8];

    if (value == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }

    work = value;
    pos = 0;
    if (work < 0) {
        work = -work;
    }

    while (work > 0 && pos < sizeof(rev)) {
        rev[pos++] = (char)('0' + (work % 10));
        work /= 10;
    }

    {
        unsigned char out_pos;
        signed char i;

        out_pos = 0;
        if (value < 0) {
            out[out_pos++] = '-';
        }
        for (i = (signed char)pos - 1; i >= 0; --i) {
            out[out_pos++] = rev[(unsigned char)i];
        }
        out[out_pos] = 0;
    }
}

static void fixed2_to_str(signed long value_fp, char *out, unsigned char currency) {
    signed long abs_fp;
    signed int int_part;
    unsigned int frac_part;
    unsigned char pos;

    pos = 0;
    if (value_fp < 0) {
        out[pos++] = '-';
        abs_fp = -value_fp;
    } else {
        abs_fp = value_fp;
    }
    if (currency) {
        out[pos++] = '$';
    }

    int_part = (signed int)(abs_fp / 100L);
    frac_part = (unsigned int)(abs_fp % 100L);

    int_to_str(int_part, &out[pos]);
    pos = (unsigned char)strlen(out);
    out[pos++] = '.';
    out[pos++] = (char)('0' + (frac_part / 10u));
    out[pos++] = (char)('0' + (frac_part % 10u));
    out[pos] = 0;
}

static void tf_approx_to_fixed(const char *src, signed long *out_fp) {
    unsigned char i;
    signed long sig;
    signed int frac_digits;
    signed char sign;
    unsigned char seen_dot;
    unsigned char have_digit;

    i = 0;
    sig = 0;
    frac_digits = 0;
    sign = 1;
    seen_dot = 0;
    have_digit = 0;

    while (src[i] == ' ') {
        ++i;
    }
    if (src[i] == '+') {
        ++i;
    } else if (src[i] == '-') {
        sign = -1;
        ++i;
    }

    while (src[i]) {
        if (is_digit((unsigned char)src[i])) {
            have_digit = 1;
            if (sig < 214748364L) {
                sig = sig * 10L + (signed long)(src[i] - '0');
                if (seen_dot) {
                    ++frac_digits;
                }
            }
            ++i;
            continue;
        }
        if (src[i] == '.' && !seen_dot) {
            seen_dot = 1;
            ++i;
            continue;
        }
        break;
    }

    if (!have_digit) {
        *out_fp = 0;
        return;
    }

    while (frac_digits < 2) {
        sig *= 10L;
        ++frac_digits;
    }
    while (frac_digits > 2) {
        sig = (sig + 5L) / 10L;
        --frac_digits;
    }
    if (sign < 0) {
        sig = -sig;
    }
    *out_fp = sig;
}

static void value_clear(ScValue *value) {
    value->kind = VALUE_EMPTY;
    value->error = ERR_NONE;
    value->text_len = 0;
    memset(value->num, 0, sizeof(value->num));
    value->text[0] = 0;
}

static void value_copy(ScValue *dst, const ScValue *src) {
    memcpy(dst, src, sizeof(ScValue));
}

static void value_set_text(ScValue *value, const char *text) {
    value_clear(value);
    value->kind = VALUE_TEXT;
    str_copy_fit(value->text, text, RESULT_TEXT_LEN + 1);
    value->text_len = (unsigned char)strlen(value->text);
}

static void value_set_number_from_tf(ScValue *value, const unsigned char *packed) {
    value_clear(value);
    value->kind = VALUE_NUMBER;
    tf_copy(value->num, packed);
}

static void value_set_error(ScValue *value, unsigned char error) {
    value_clear(value);
    value->kind = VALUE_ERROR;
    value->error = error;
    switch (error) {
        case ERR_REF:
            strcpy(value->text, "#REF!");
            break;
        case ERR_DIV0:
            strcpy(value->text, "#DIV/0");
            break;
        case ERR_CYCLE:
            strcpy(value->text, "#CYCLE");
            break;
        case ERR_TYPE:
            strcpy(value->text, "#TYPE");
            break;
        case ERR_LONG:
            strcpy(value->text, "#LONG");
            break;
        default:
            strcpy(value->text, "#ERR");
            break;
    }
    value->text_len = (unsigned char)strlen(value->text);
}

static unsigned char value_is_textlike(const ScValue *value) {
    return (value->kind == VALUE_TEXT) ? 1u : 0u;
}

static void value_general_text(const ScValue *value, char *out) {
    if (value->kind == VALUE_TEXT || value->kind == VALUE_ERROR) {
        str_copy_fit(out, value->text, RESULT_TEXT_LEN + 1);
        return;
    }
    if (value->kind == VALUE_EMPTY) {
        out[0] = 0;
        return;
    }
    tf_packed_to_str(value->num, out);
}

static unsigned char numeric_literal_span(const char *expr,
                                          unsigned char start,
                                          unsigned char *out_end) {
    unsigned char i;
    unsigned char have_digit;
    unsigned char seen_dot;

    i = start;
    have_digit = 0;
    seen_dot = 0;

    if (expr[i] == '.') {
        seen_dot = 1;
        ++i;
    }

    while (is_digit((unsigned char)expr[i])) {
        have_digit = 1;
        ++i;
    }

    if (!seen_dot && expr[i] == '.') {
        seen_dot = 1;
        ++i;
        while (is_digit((unsigned char)expr[i])) {
            have_digit = 1;
            ++i;
        }
    }

    if (!have_digit) {
        return 0;
    }

    *out_end = i;
    return 1;
}

static unsigned char parse_cell_ref_token(const char *text,
                                          unsigned char start,
                                          unsigned char *out_col,
                                          unsigned char *out_row,
                                          unsigned char *out_end) {
    unsigned char col;
    unsigned char i;
    unsigned int row_value;

    if (!is_alpha((unsigned char)text[start])) {
        return 0;
    }

    col = (unsigned char)(ascii_upper((unsigned char)text[start]) - 'A');
    i = (unsigned char)(start + 1u);
    if (!is_digit((unsigned char)text[i])) {
        return 0;
    }

    row_value = 0u;
    while (is_digit((unsigned char)text[i])) {
        row_value = (unsigned int)(row_value * 10u + (unsigned int)(text[i] - '0'));
        ++i;
    }
    if (row_value == 0u) {
        return 0;
    }

    *out_col = col;
    *out_row = (unsigned char)(row_value - 1u);
    *out_end = i;
    return 1;
}

static unsigned char parse_range_token(const char *text,
                                       unsigned char *r0,
                                       unsigned char *c0,
                                       unsigned char *r1,
                                       unsigned char *c1) {
    unsigned char start;
    unsigned char end;
    unsigned char pos;
    unsigned char c;
    unsigned char r;

    trim_span(text, &start, &end);
    pos = start;
    if (!parse_cell_ref_token(text, pos, &c, &r, &pos)) {
        return 0;
    }
    *c0 = c;
    *r0 = r;

    while (pos < end && text[pos] == ' ') {
        ++pos;
    }
    if (pos >= end) {
        *c1 = *c0;
        *r1 = *r0;
        return 1;
    }
    if (text[pos] != ':') {
        return 0;
    }
    ++pos;
    while (pos < end && text[pos] == ' ') {
        ++pos;
    }
    if (!parse_cell_ref_token(text, pos, &c, &r, &pos)) {
        return 0;
    }
    while (pos < end && text[pos] == ' ') {
        ++pos;
    }
    if (pos != end) {
        return 0;
    }
    *c1 = c;
    *r1 = r;
    return 1;
}

static unsigned char append_content_bytes(const char *text, unsigned char len, unsigned int *out_off) {
    unsigned int need;

    need = (unsigned int)len + 1u;
    if ((unsigned int)(content_used + need) > CONTENT_POOL_SIZE) {
        if (!compact_content_pool()) {
            return 0;
        }
    }
    if ((unsigned int)(content_used + need) > CONTENT_POOL_SIZE) {
        return 0;
    }

    *out_off = content_used;
    memcpy(&content_pool[content_used], text, len);
    content_pool[(unsigned int)(content_used + len)] = 0;
    content_used = (unsigned int)(content_used + need);
    return 1;
}

static void mark_formula_cells_dirty(void) {
    unsigned int idx;

    for (idx = 0u; idx < MAX_CELLS; ++idx) {
        if (cell_flags[idx] & CELL_FLAG_FORMULA) {
            cell_flags[idx] |= CELL_FLAG_DIRTY;
        }
    }
}

static void clear_sheet_state(void) {
    unsigned int idx;
    unsigned char i;

    for (idx = 0u; idx < MAX_CELLS; ++idx) {
        cell_off[idx] = 0xFFFFu;
        cell_len[idx] = 0;
        cell_flags[idx] = 0;
        cell_color[idx] = CELL_COLOR_DEFAULT;
    }
    for (i = 0u; i < MAX_COLS; ++i) {
        col_width[i] = DEFAULT_COL_W;
        col_type[i] = COLTYPE_GENERAL;
    }
    content_used = 0u;
}

static void init_defaults(void) {
    tui_init();
    reu_mgr_init();
    cursor(0);
    clear_sheet_state();
    active_row = 0;
    active_col = 0;
    scroll_col = 0;
    running = 1;
    modified = 0;
    resume_ready = 0;
    filename[0] = 0;
    clipboard_valid = 0;
    clipboard_len = 0;
    clipboard_raw[0] = 0;
    set_status("", TUI_COLOR_GRAY3);
}

static unsigned char sheet_validate_state(void) {
    unsigned int idx;

    if (active_row >= MAX_ROWS || active_col >= MAX_COLS || scroll_col >= MAX_COLS) {
        return 0u;
    }
    if (content_used > CONTENT_POOL_SIZE) {
        return 0u;
    }
    for (idx = 0u; idx < MAX_COLS; ++idx) {
        if (col_width[idx] < MIN_COL_W || col_width[idx] > MAX_COL_W) {
            return 0u;
        }
        if (col_type[idx] > COLTYPE_CURRENCY) {
            return 0u;
        }
    }
    for (idx = 0u; idx < MAX_CELLS; ++idx) {
        if (!cell_used_idx(idx)) {
            continue;
        }
        if (cell_off[idx] == 0xFFFFu) {
            return 0u;
        }
        if (cell_len[idx] > MAX_RAW_LEN) {
            return 0u;
        }
        if ((unsigned int)(cell_off[idx] + cell_len[idx] + 1u) > CONTENT_POOL_SIZE) {
            return 0u;
        }
        if (cell_color[idx] != CELL_COLOR_DEFAULT &&
            cell_color[idx] != TUI_COLOR_YELLOW &&
            cell_color[idx] != TUI_COLOR_GREEN &&
            cell_color[idx] != TUI_COLOR_RED &&
            cell_color[idx] != TUI_COLOR_LIGHTBLUE) {
            return 0u;
        }
    }
    return 1u;
}

static void reset_sheet_runtime(const char *msg, unsigned char color) {
    clear_sheet_state();
    active_row = 0u;
    active_col = 0u;
    scroll_col = 0u;
    modified = 0u;
    filename[0] = 0;
    clipboard_valid = 0u;
    clipboard_len = 0u;
    clipboard_raw[0] = 0;
    clamp_view();
    set_status(msg, color);
}

static unsigned char compact_content_pool(void) {
    unsigned int new_used;
    unsigned int remaining;

    new_used = 0u;
    remaining = MAX_CELLS;

    while (remaining > 0u) {
        unsigned int idx;
        unsigned int best_idx;
        unsigned int best_off;

        best_idx = 0xFFFFu;
        best_off = 0xFFFFu;

        for (idx = 0u; idx < MAX_CELLS; ++idx) {
            if (!cell_used_idx(idx)) {
                continue;
            }
            if (cell_off[idx] == 0xFFFFu || cell_off[idx] < new_used) {
                continue;
            }
            if (cell_off[idx] < best_off) {
                best_off = cell_off[idx];
                best_idx = idx;
            }
        }

        if (best_idx == 0xFFFFu) {
            break;
        }

        if (best_off != new_used) {
            memmove(&content_pool[new_used],
                    &content_pool[best_off],
                    (unsigned int)cell_len[best_idx] + 1u);
            cell_off[best_idx] = new_used;
        }
        new_used = (unsigned int)(new_used + cell_len[best_idx] + 1u);
        --remaining;
    }
    content_used = new_used;
    return 1;
}

static unsigned char set_cell_raw(unsigned char row, unsigned char col, const char *text) {
    unsigned int idx;
    unsigned char len;
    unsigned int off;

    idx = cell_index(row, col);
    len = (unsigned char)strlen(text);
    if (len > MAX_RAW_LEN) {
        set_status("TEXT TOO LONG", TUI_COLOR_LIGHTRED);
        return 0;
    }

    if (len == 0u) {
        cell_off[idx] = 0xFFFFu;
        cell_len[idx] = 0u;
        cell_flags[idx] = 0u;
        modified = 1;
        mark_formula_cells_dirty();
        return 1;
    }

    if (!append_content_bytes(text, len, &off)) {
        set_status("OUT OF CELL RAM", TUI_COLOR_LIGHTRED);
        return 0;
    }

    cell_off[idx] = off;
    cell_len[idx] = len;
    cell_flags[idx] = CELL_FLAG_USED | CELL_FLAG_DIRTY;
    if (text[0] == '=') {
        cell_flags[idx] |= CELL_FLAG_FORMULA;
    }
    if (!sheet_validate_state()) {
        reset_sheet_runtime("BAD CELL RESET", TUI_COLOR_LIGHTRED);
        return 0;
    }
    modified = 1;
    mark_formula_cells_dirty();
    return 1;
}

static void clear_active_cell(void) {
    unsigned int idx;

    idx = cell_index(active_row, active_col);
    if (!cell_used_idx(idx)) {
        set_status("CELL ALREADY EMPTY", TUI_COLOR_GRAY3);
        return;
    }
    cell_off[idx] = 0xFFFFu;
    cell_len[idx] = 0;
    cell_flags[idx] = 0;
    cell_color[idx] = CELL_COLOR_DEFAULT;
    if (!sheet_validate_state()) {
        reset_sheet_runtime("BAD CELL RESET", TUI_COLOR_LIGHTRED);
        return;
    }
    modified = 1;
    mark_formula_cells_dirty();
    set_status("CELL CLEARED", TUI_COLOR_LIGHTGREEN);
}

static void set_status(const char *msg, unsigned char color) {
    str_copy_fit(status_msg, msg, sizeof(status_msg));
    status_color = color;
}

static unsigned char cell_default_color(unsigned char col) {
    if (col_type[col] == COLTYPE_CURRENCY) {
        return TUI_COLOR_LIGHTGREEN;
    }
    if (col_type[col] == COLTYPE_TEXT) {
        return TUI_COLOR_WHITE;
    }
    if (col_type[col] == COLTYPE_INT) {
        return TUI_COLOR_CYAN;
    }
    return TUI_COLOR_WHITE;
}

static unsigned char cell_effective_color(unsigned char row, unsigned char col, const ScValue *value) {
    unsigned int idx;

    idx = cell_index(row, col);
    if (value->kind == VALUE_ERROR) {
        return TUI_COLOR_LIGHTRED;
    }
    if (cell_color[idx] != CELL_COLOR_DEFAULT) {
        return cell_color[idx];
    }
    return cell_default_color(col);
}

static void format_display_value(const ScValue *value, unsigned char col, char *out) {
    if (value->kind == VALUE_EMPTY) {
        out[0] = 0;
        return;
    }
    if (value->kind == VALUE_TEXT || value->kind == VALUE_ERROR) {
        str_copy_fit(out, value->text, RESULT_TEXT_LEN + 1);
        return;
    }

    tf_packed_to_str(value->num, format_general_buf);
    if (col_type[col] == COLTYPE_GENERAL || col_type[col] == COLTYPE_TEXT) {
        str_copy_fit(out, format_general_buf, RESULT_TEXT_LEN + 1);
        return;
    }

    tf_approx_to_fixed(format_general_buf, &format_value_fp);
    if (col_type[col] == COLTYPE_INT) {
        signed int rounded;

        if (format_value_fp >= 0) {
            rounded = (signed int)((format_value_fp + 50L) / 100L);
        } else {
            rounded = (signed int)((format_value_fp - 50L) / 100L);
        }
        int_to_str(rounded, out);
        return;
    }

    fixed2_to_str(format_value_fp, out, (unsigned char)(col_type[col] == COLTYPE_CURRENCY));
}

static void fill_grid_display(unsigned char row,
                              unsigned char col,
                              unsigned char allow_formula,
                              ScValue *value,
                              char *display) {
    unsigned int idx;
    const char *raw;

    idx = cell_index(row, col);
    value_clear(value);
    display[0] = 0;

    if (!cell_used_idx(idx)) {
        return;
    }

    raw = cell_raw_ptr_idx(idx);
    if ((cell_flags[idx] & CELL_FLAG_FORMULA) != 0u) {
        unsigned char err;

        if (!allow_formula) {
            strcpy(display, "=");
            return;
        }
        err = ERR_NONE;
        (void)eval_cell_value(row, col, 0u, value, &err);
        format_display_value(value, col, display);
        return;
    }

    eval_literal_cell(raw, col, value);
    format_display_value(value, col, display);
}

static void write_cell_span(unsigned char x,
                            unsigned char y,
                            const char *text,
                            unsigned char width,
                            unsigned char color,
                            unsigned char align_right,
                            unsigned char reverse) {
    unsigned int screen_off;
    unsigned char len;
    unsigned char start;
    unsigned char i;

    screen_off = (unsigned int)y * 40u + x;
    len = (unsigned char)strlen(text);
    if (len > width) {
        len = width;
    }

    start = 0u;
    if (align_right && len < width) {
        start = (unsigned char)(width - len);
    }

    for (i = 0u; i < width; ++i) {
        unsigned char ch;

        ch = 32u;
        if (i >= start && (unsigned char)(i - start) < len) {
            ch = tui_ascii_to_screen((unsigned char)text[(unsigned char)(i - start)]);
        }
        if (reverse) {
            ch |= 0x80u;
        }
        TUI_SCREEN[screen_off + i] = ch;
        TUI_COLOR_RAM[screen_off + i] = color;
    }
}

static unsigned char eval_numeric_literal(const char *text, ScValue *out) {
    unsigned char start;
    unsigned char end;
    unsigned char i;
    unsigned char pos;

    trim_span(text, &start, &end);
    if (start >= end) {
        value_clear(out);
        return 1;
    }
    if (!numeric_literal_span(text, start, &pos) || pos != end) {
        return 0;
    }

    pos = 0u;
    for (i = start; i < end; ++i) {
        numeric_work_buf[pos++] = text[i];
    }
    numeric_work_buf[pos] = 0;
    tf_from_literal(numeric_work_buf, out->num);
    out->kind = VALUE_NUMBER;
    out->error = ERR_NONE;
    out->text[0] = 0;
    out->text_len = 0;
    return 1;
}

static void eval_literal_cell(const char *raw, unsigned char col, ScValue *out) {
    if (col_type[col] == COLTYPE_TEXT) {
        value_set_text(out, raw);
        return;
    }
    if (eval_numeric_literal(raw, out)) {
        return;
    }
    value_set_text(out, raw);
}

static unsigned char eval_extract_arg(const char *expr,
                                      unsigned char *idx,
                                      unsigned char depth,
                                      unsigned char *err) {
    unsigned char start;
    unsigned char pos;
    signed char nesting;
    unsigned char out_pos;

    if (expr[*idx] != '(') {
        *err = ERR_SYNTAX;
        return 0;
    }
    start = (unsigned char)(*idx + 1u);
    pos = start;
    nesting = 1;
    while (expr[pos] != 0) {
        if (expr[pos] == '(') {
            ++nesting;
        } else if (expr[pos] == ')') {
            --nesting;
            if (nesting == 0) {
                break;
            }
        }
        ++pos;
    }
    if (expr[pos] != ')' || nesting != 0) {
        *err = ERR_SYNTAX;
        return 0;
    }
    if ((unsigned char)(pos - start) > MAX_RAW_LEN) {
        *err = ERR_LONG;
        return 0;
    }
    out_pos = 0u;
    while (start < pos) {
        eval_arg_buf[depth][out_pos++] = expr[start++];
    }
    eval_arg_buf[depth][out_pos] = 0;
    *idx = (unsigned char)(pos + 1u);
    return 1;
}

static unsigned char eval_expression(const char *expr,
                                     unsigned char *idx,
                                     unsigned char depth,
                                     ScValue *out,
                                     unsigned char *err);

static void value_to_text_for_concat(const ScValue *value, char *out) {
    if (value->kind == VALUE_EMPTY) {
        out[0] = 0;
        return;
    }
    value_general_text(value, out);
}

static unsigned char value_apply_add(const ScValue *lhs, const ScValue *rhs, ScValue *out) {
    char lhs_text[RESULT_TEXT_LEN + 1];
    char rhs_text[RESULT_TEXT_LEN + 1];
    unsigned char len;

    if (lhs->kind == VALUE_ERROR) {
        value_copy(out, lhs);
        return 1;
    }
    if (rhs->kind == VALUE_ERROR) {
        value_copy(out, rhs);
        return 1;
    }

    if (value_is_textlike(lhs) || value_is_textlike(rhs)) {
        value_to_text_for_concat(lhs, lhs_text);
        value_to_text_for_concat(rhs, rhs_text);
        len = (unsigned char)(strlen(lhs_text) + strlen(rhs_text));
        if (len > RESULT_TEXT_LEN) {
            value_set_error(out, ERR_LONG);
            return 1;
        }
        strcpy(out->text, lhs_text);
        strcat(out->text, rhs_text);
        out->kind = VALUE_TEXT;
        out->error = ERR_NONE;
        out->text_len = (unsigned char)strlen(out->text);
        memset(out->num, 0, sizeof(out->num));
        return 1;
    }

    if (lhs->kind == VALUE_EMPTY) {
        tf_from_literal("0", romfp_a);
    } else {
        tf_copy(romfp_a, lhs->num);
    }
    if (rhs->kind == VALUE_EMPTY) {
        tf_from_literal("0", romfp_b);
    } else {
        tf_copy(romfp_b, rhs->num);
    }
    romfp_add();
    value_set_number_from_tf(out, romfp_out);
    return 1;
}

static unsigned char value_apply_sub(const ScValue *lhs, const ScValue *rhs, ScValue *out) {
    if (lhs->kind == VALUE_ERROR) {
        value_copy(out, lhs);
        return 1;
    }
    if (rhs->kind == VALUE_ERROR) {
        value_copy(out, rhs);
        return 1;
    }
    if (lhs->kind == VALUE_TEXT || rhs->kind == VALUE_TEXT) {
        value_set_error(out, ERR_TYPE);
        return 1;
    }

    if (lhs->kind == VALUE_EMPTY) {
        tf_from_literal("0", romfp_a);
    } else {
        tf_copy(romfp_a, lhs->num);
    }
    if (rhs->kind == VALUE_EMPTY) {
        tf_from_literal("0", romfp_b);
    } else {
        tf_copy(romfp_b, rhs->num);
    }
    if (tf_is_zero(romfp_b) && lhs->kind == VALUE_EMPTY && rhs->kind == VALUE_EMPTY) {
        value_set_number_from_tf(out, romfp_b);
        return 1;
    }
    romfp_sub();
    value_set_number_from_tf(out, romfp_out);
    return 1;
}

static unsigned char eval_cell_value(unsigned char row,
                                     unsigned char col,
                                     unsigned char depth,
                                     ScValue *out,
                                     unsigned char *err) {
    unsigned int idx;
    unsigned char i;
    const char *raw;

    if (row >= MAX_ROWS || col >= MAX_COLS) {
        *err = ERR_REF;
        value_set_error(out, ERR_REF);
        return 0;
    }

    idx = cell_index(row, col);
    if (!cell_used_idx(idx)) {
        value_clear(out);
        return 1;
    }

    for (i = 0u; i < eval_stack_depth; ++i) {
        if (eval_stack[i] == idx) {
            *err = ERR_CYCLE;
            value_set_error(out, ERR_CYCLE);
            return 0;
        }
    }
    if (eval_stack_depth >= MAX_EVAL_DEPTH) {
        *err = ERR_CYCLE;
        value_set_error(out, ERR_CYCLE);
        return 0;
    }

    eval_stack[eval_stack_depth++] = idx;
    raw = cell_raw_ptr_idx(idx);
    if (cell_flags[idx] & CELL_FLAG_FORMULA) {
        unsigned char pos;

        pos = 0u;
        if (!eval_expression(raw + 1, &pos, depth + 1u, out, err)) {
            value_set_error(out, *err);
        } else {
            while (raw[(unsigned char)(pos + 1u)] == ' ') {
                ++pos;
            }
            if (raw[(unsigned char)(pos + 1u)] != 0) {
                *err = ERR_SYNTAX;
                value_set_error(out, ERR_SYNTAX);
            }
        }
    } else {
        eval_literal_cell(raw, col, out);
    }

    --eval_stack_depth;
    return (unsigned char)(out->kind != VALUE_ERROR);
}

static unsigned char eval_function_call(const char *name,
                                        const char *arg,
                                        unsigned char depth,
                                        ScValue *out,
                                        unsigned char *err) {
    unsigned char r0;
    unsigned char c0;
    unsigned char r1;
    unsigned char c1;
    unsigned char row;
    unsigned char col;
    unsigned char count;
    unsigned char have_numeric;
    ScValue *tmp;

    if (strcmp(name, "SUM") != 0 &&
        strcmp(name, "AVG") != 0 &&
        strcmp(name, "COUNT") != 0) {
        *err = ERR_SYNTAX;
        value_set_error(out, ERR_SYNTAX);
        return 0;
    }

    if (parse_range_token(arg, &r0, &c0, &r1, &c1)) {
        unsigned char lo_row;
        unsigned char hi_row;
        unsigned char lo_col;
        unsigned char hi_col;

        if (r0 > r1) {
            lo_row = r1;
            hi_row = r0;
        } else {
            lo_row = r0;
            hi_row = r1;
        }
        if (c0 > c1) {
            lo_col = c1;
            hi_col = c0;
        } else {
            lo_col = c0;
            hi_col = c1;
        }

        count = 0u;
        have_numeric = 0u;
        tf_from_literal("0", romfp_out);
        for (row = lo_row; row <= hi_row; ++row) {
            for (col = lo_col; col <= hi_col; ++col) {
                tmp = &eval_tmp_a[depth];
                if (!eval_cell_value(row, col, depth + 1u, tmp, err)) {
                    if (tmp->kind == VALUE_ERROR) {
                        value_copy(out, tmp);
                        return 0;
                    }
                }
                if (tmp->kind == VALUE_NUMBER) {
                    if (!have_numeric) {
                        tf_copy(romfp_out, tmp->num);
                        have_numeric = 1u;
                    } else {
                        tf_copy(romfp_a, romfp_out);
                        tf_copy(romfp_b, tmp->num);
                        romfp_add();
                    }
                    ++count;
                }
                if (row == hi_row && col == hi_col) {
                    break;
                }
            }
            if (row == hi_row) {
                break;
            }
        }

        if (strcmp(name, "COUNT") == 0) {
            char count_buf[6];

            int_to_str((signed int)count, count_buf);
            tf_from_literal(count_buf, romfp_out);
            value_set_number_from_tf(out, romfp_out);
            return 1;
        }
        if (!have_numeric) {
            if (strcmp(name, "SUM") == 0) {
                tf_from_literal("0", romfp_out);
                value_set_number_from_tf(out, romfp_out);
                return 1;
            }
            *err = ERR_DIV0;
            value_set_error(out, ERR_DIV0);
            return 0;
        }
        if (strcmp(name, "AVG") == 0) {
            char count_buf[6];

            int_to_str((signed int)count, count_buf);
            tf_copy(romfp_a, romfp_out);
            tf_from_literal(count_buf, romfp_b);
            romfp_div();
        }
        value_set_number_from_tf(out, romfp_out);
        return 1;
    }

    row = 0u;
    if (!eval_expression(arg, &row, depth + 1u, out, err)) {
        return 0;
    }
    if (strcmp(name, "COUNT") == 0) {
        if (out->kind == VALUE_NUMBER) {
            tf_from_literal("1", romfp_out);
        } else {
            tf_from_literal("0", romfp_out);
        }
        value_set_number_from_tf(out, romfp_out);
        return 1;
    }
    if (out->kind == VALUE_NUMBER) {
        return 1;
    }
    if (strcmp(name, "SUM") == 0) {
        tf_from_literal("0", romfp_out);
        value_set_number_from_tf(out, romfp_out);
        return 1;
    }
    *err = ERR_DIV0;
    value_set_error(out, ERR_DIV0);
    return 0;
}

static unsigned char eval_primary(const char *expr,
                                  unsigned char *idx,
                                  unsigned char depth,
                                  ScValue *out,
                                  unsigned char *err) {
    unsigned char sign;
    unsigned char pos;
    unsigned char col;
    unsigned char row;
    unsigned char end;
    char ident[8];
    unsigned char ident_len;
    char lit[MAX_RAW_LEN + 1];
    unsigned char lit_len;

    while (expr[*idx] == ' ') {
        ++(*idx);
    }
    sign = 0u;
    while (expr[*idx] == '+' || expr[*idx] == '-') {
        if (expr[*idx] == '-') {
            sign ^= 1u;
        }
        ++(*idx);
        while (expr[*idx] == ' ') {
            ++(*idx);
        }
    }

    if (expr[*idx] == '(') {
        ++(*idx);
        if (!eval_expression(expr, idx, depth + 1u, out, err)) {
            return 0;
        }
        while (expr[*idx] == ' ') {
            ++(*idx);
        }
        if (expr[*idx] != ')') {
            *err = ERR_SYNTAX;
            value_set_error(out, ERR_SYNTAX);
            return 0;
        }
        ++(*idx);
    } else if (parse_cell_ref_token(expr, *idx, &col, &row, &end)) {
        *idx = end;
        if (!eval_cell_value(row, col, depth + 1u, out, err)) {
            return 0;
        }
    } else if (numeric_literal_span(expr, *idx, &end)) {
        lit_len = 0u;
        for (pos = *idx; pos < end; ++pos) {
            lit[lit_len++] = expr[pos];
        }
        lit[lit_len] = 0;
        tf_from_literal(lit, romfp_out);
        value_set_number_from_tf(out, romfp_out);
        *idx = end;
    } else if (is_alpha((unsigned char)expr[*idx])) {
        ident_len = 0u;
        while (is_alpha((unsigned char)expr[*idx]) &&
               ident_len < (unsigned char)(sizeof(ident) - 1u)) {
            ident[ident_len++] = (char)ascii_upper((unsigned char)expr[*idx]);
            ++(*idx);
        }
        ident[ident_len] = 0;
        while (expr[*idx] == ' ') {
            ++(*idx);
        }
        if (!eval_extract_arg(expr, idx, depth, err)) {
            value_set_error(out, *err);
            return 0;
        }
        if (!eval_function_call(ident, eval_arg_buf[depth], depth, out, err)) {
            return 0;
        }
    } else {
        *err = ERR_SYNTAX;
        value_set_error(out, ERR_SYNTAX);
        return 0;
    }

    if (sign) {
        if (out->kind == VALUE_TEXT) {
            *err = ERR_TYPE;
            value_set_error(out, ERR_TYPE);
            return 0;
        }
        if (out->kind == VALUE_EMPTY) {
            tf_from_literal("0", romfp_out);
            value_set_number_from_tf(out, romfp_out);
            return 1;
        }
        tf_from_literal("0", romfp_a);
        tf_copy(romfp_b, out->num);
        romfp_sub();
        value_set_number_from_tf(out, romfp_out);
    }
    return 1;
}

static unsigned char eval_expression(const char *expr,
                                     unsigned char *idx,
                                     unsigned char depth,
                                     ScValue *out,
                                     unsigned char *err) {
    unsigned char op;
    ScValue *rhs;

    if (depth >= MAX_EVAL_DEPTH) {
        *err = ERR_CYCLE;
        value_set_error(out, ERR_CYCLE);
        return 0;
    }

    if (!eval_primary(expr, idx, depth, out, err)) {
        return 0;
    }

    while (1) {
        while (expr[*idx] == ' ') {
            ++(*idx);
        }
        op = (unsigned char)expr[*idx];
        if (op != '+' && op != '-') {
            break;
        }
        ++(*idx);
        rhs = &eval_tmp_b[depth];
        if (!eval_primary(expr, idx, depth, rhs, err)) {
            return 0;
        }
        if (op == '+') {
            value_apply_add(out, rhs, out);
        } else {
            value_apply_sub(out, rhs, out);
        }
        if (out->kind == VALUE_ERROR) {
            *err = out->error;
            return 0;
        }
    }

    return 1;
}

static void compute_visible_columns(void) {
    unsigned char col;
    unsigned char x;
    unsigned char use_w;

    visible_count = 0u;
    x = GRID_X;
    col = scroll_col;
    while (col < MAX_COLS && x < 40u && visible_count < MAX_COLS) {
        use_w = col_width[col];
        if (use_w < MIN_COL_W) {
            use_w = MIN_COL_W;
        }
        if (use_w > MAX_COL_W) {
            use_w = MAX_COL_W;
        }
        if ((unsigned char)(x + use_w) > 40u) {
            use_w = (unsigned char)(40u - x);
        }
        if (use_w == 0u) {
            break;
        }
        visible_cols[visible_count] = col;
        visible_x[visible_count] = x;
        visible_w[visible_count] = use_w;
        ++visible_count;
        x = (unsigned char)(x + use_w);
        if (x < 40u) {
            ++x;
        }
        ++col;
    }
}

static void clamp_view(void) {
    if (active_row >= MAX_ROWS) {
        active_row = (unsigned char)(MAX_ROWS - 1u);
    }
    if (active_col >= MAX_COLS) {
        active_col = (unsigned char)(MAX_COLS - 1u);
    }
    if (scroll_col > (unsigned char)(MAX_COLS - 1u)) {
        scroll_col = (unsigned char)(MAX_COLS - 1u);
    }
    ensure_active_col_visible();
    compute_visible_columns();
}

static void ensure_active_col_visible(void) {
    unsigned char test_count;
    unsigned char i;
    unsigned char found;

    if (active_col < scroll_col) {
        scroll_col = active_col;
    }

    while (1) {
        compute_visible_columns();
        found = 0u;
        test_count = visible_count;
        for (i = 0u; i < test_count; ++i) {
            if (visible_cols[i] == active_col) {
                found = 1u;
                break;
            }
        }
        if (found) {
            break;
        }
        scroll_col = active_col;
        if (scroll_col >= MAX_COLS) {
            scroll_col = (unsigned char)(MAX_COLS - 1u);
        }
        break;
    }
}

static void draw_header(void) {
    TuiRect box;
    unsigned char file_x;

    box.x = 0;
    box.y = HEADER_Y;
    box.w = 40;
    box.h = 3;
    tui_window_title(&box, "SIMPLE CELLS", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    draw_coord_buf[0] = (char)('A' + active_col);
    if (active_row + 1u >= 10u) {
        draw_coord_buf[1] = (char)('0' + ((active_row + 1u) / 10u));
        draw_coord_buf[2] = (char)('0' + ((active_row + 1u) % 10u));
        draw_coord_buf[3] = 0;
    } else {
        draw_coord_buf[1] = (char)('0' + (active_row + 1u));
        draw_coord_buf[2] = 0;
    }

    tui_puts_n(1, HEADER_Y + 1, "cell", 4, TUI_COLOR_WHITE);
    tui_puts_n(6, HEADER_Y + 1, draw_coord_buf, 3, TUI_COLOR_CYAN);
    tui_puts_n(10, HEADER_Y + 1, "w:", 2, TUI_COLOR_WHITE);
    draw_display_buf[0] = (char)('0' + (col_width[active_col] / 10u));
    draw_display_buf[1] = (char)('0' + (col_width[active_col] % 10u));
    draw_display_buf[2] = 0;
    tui_puts_n(12, HEADER_Y + 1, draw_display_buf, 2, TUI_COLOR_CYAN);
    tui_puts_n(15, HEADER_Y + 1, type_menu_items[col_type[active_col]], 8, TUI_COLOR_WHITE);
    tui_puts_n(24, HEADER_Y + 1, "d:", 2, TUI_COLOR_WHITE);
    {
        unsigned char drive;

        drive = storage_device_get_default();
        if (drive >= 10u) {
            draw_display_buf[0] = (char)('0' + (drive / 10u));
            draw_display_buf[1] = (char)('0' + (drive % 10u));
            draw_display_buf[2] = 0;
            tui_puts_n(26, HEADER_Y + 1, draw_display_buf, 2, TUI_COLOR_CYAN);
            file_x = 29u;
        } else {
            draw_display_buf[0] = (char)('0' + drive);
            draw_display_buf[1] = 0;
            tui_puts_n(26, HEADER_Y + 1, draw_display_buf, 1, TUI_COLOR_CYAN);
            file_x = 28u;
        }
    }
    if (filename[0] != 0) {
        tui_puts_n(file_x, HEADER_Y + 1, filename, 8, TUI_COLOR_WHITE);
    } else {
        tui_puts_n(file_x, HEADER_Y + 1, "untitled", 8, TUI_COLOR_WHITE);
    }
    tui_puts_n(37, HEADER_Y + 1, modified ? "*" : " ", 1, TUI_COLOR_YELLOW);
}

static void draw_formula_bar(void) {
    unsigned int idx;
    char raw_show[33];
    const char *raw;

    tui_clear_line(FORMULA_Y, 0, 40, TUI_COLOR_WHITE);
    tui_puts(0, FORMULA_Y, "raw:", TUI_COLOR_GRAY3);
    idx = cell_index(active_row, active_col);
    if (cell_used_idx(idx)) {
        raw = cell_raw_ptr_idx(idx);
        str_copy_fit(raw_show, raw, sizeof(raw_show));
        tui_puts_n(5, FORMULA_Y, raw_show, 35, TUI_COLOR_WHITE);
    } else {
        tui_puts_n(5, FORMULA_Y, "", 35, TUI_COLOR_WHITE);
    }
}

static void draw_column_headers(void) {
    unsigned char i;
    unsigned char col;
    char label[4];
    unsigned char gutter_color;

    tui_clear_line(COLHDR_Y, 0, 40, TUI_COLOR_GRAY3);
    tui_puts(0, COLHDR_Y, "##", TUI_COLOR_GREEN);
    tui_putc((unsigned char)(GRID_X - 1u), COLHDR_Y, TUI_VLINE, TUI_COLOR_GRAY2);
    for (i = 0u; i < visible_count; ++i) {
        unsigned char color;
        unsigned char x;
        unsigned char w;

        col = visible_cols[i];
        x = visible_x[i];
        w = visible_w[i];
        label[0] = (char)('A' + col);
        label[1] = 0;
        color = (col == active_col) ? TUI_COLOR_YELLOW : TUI_COLOR_LIGHTBLUE;
        write_cell_span(x, COLHDR_Y, "", w, color, 0, 0);
        write_cell_span(x, COLHDR_Y, label, w, color, 0, 0);

        if ((unsigned char)(x + w) < 40u) {
            gutter_color = (unsigned char)((i & 1u) ? TUI_COLOR_GRAY2 : TUI_COLOR_GRAY3);
            tui_putc((unsigned char)(x + w), COLHDR_Y, TUI_VLINE, gutter_color);
        }
    }
}

static unsigned char find_visible_col(unsigned char col, unsigned char *out_idx) {
    unsigned char i;

    for (i = 0u; i < visible_count; ++i) {
        if (visible_cols[i] == col) {
            *out_idx = i;
            return 1;
        }
    }
    return 0;
}

static void draw_grid_row(unsigned char row_on_screen) {
    unsigned char sheet_row;
    unsigned char i;
    char row_label[3];

    sheet_row = row_on_screen;
    tui_clear_line((unsigned char)(GRID_Y + row_on_screen), 0, 40, TUI_COLOR_WHITE);

    row_label[0] = (char)('0' + ((sheet_row + 1u) / 10u));
    row_label[1] = (char)('0' + ((sheet_row + 1u) % 10u));
    row_label[2] = 0;
    write_cell_span(0, (unsigned char)(GRID_Y + row_on_screen), row_label, 2, TUI_COLOR_GREEN, 1, 0);
    tui_putc((unsigned char)(GRID_X - 1u),
             (unsigned char)(GRID_Y + row_on_screen),
             TUI_VLINE,
             TUI_COLOR_GRAY2);

    for (i = 0u; i < visible_count; ++i) {
        unsigned char col;
        unsigned char x;
        unsigned char w;
        unsigned char selected;
        unsigned char color;
        unsigned char align_right;
        unsigned int idx;
        ScValue *value;
        unsigned char gutter_color;

        col = visible_cols[i];
        x = visible_x[i];
        w = visible_w[i];
        idx = cell_index(sheet_row, col);
        selected = (unsigned char)(sheet_row == active_row && col == active_col);
        value = &eval_tmp_a[0];
        fill_grid_display(sheet_row,
                          col,
                          (unsigned char)(selected != 0u),
                          value,
                          draw_display_buf);
        color = cell_effective_color(sheet_row, col, value);
        align_right = (unsigned char)(value->kind == VALUE_NUMBER);
        write_cell_span(x,
                        (unsigned char)(GRID_Y + row_on_screen),
                        draw_display_buf,
                        w,
                        color,
                        align_right,
                        selected);
        if ((unsigned char)(x + w) < 40u) {
            gutter_color = (unsigned char)((i & 1u) ? TUI_COLOR_GRAY2 : TUI_COLOR_GRAY3);
            tui_putc((unsigned char)(x + w),
                     (unsigned char)(GRID_Y + row_on_screen),
                     TUI_VLINE,
                     gutter_color);
        }
    }
}

static void draw_grid(void) {
    unsigned char row;

    for (row = 0u; row < GRID_ROWS; ++row) {
        draw_grid_row(row);
    }
}

static void draw_status(void) {
    tui_clear_line(STATUS_Y, 0, 40, TUI_COLOR_WHITE);
    tui_puts_n(0, STATUS_Y, status_msg, 40, status_color);
}

static void draw_help(void) {
    tui_clear_line(HELP_Y1, 0, 40, TUI_COLOR_GRAY3);
    tui_puts_n(0, HELP_Y1, "ret edit f3 form f1 fmt f5/f7 c/v del ^b", 40, TUI_COLOR_GRAY3);
}

static void draw_screen(void) {
    cursor(0);
    compute_visible_columns();
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    draw_formula_bar();
    draw_column_headers();
    draw_grid();
    draw_status();
    draw_help();
}

static void redraw_active_transition(unsigned char old_row, unsigned char old_col) {
    unsigned char vis_idx;

    if (old_row != active_row && find_visible_col(old_col, &vis_idx)) {
        draw_grid_row(old_row);
    }
    redraw_active_cell();
    draw_header();
    draw_formula_bar();
    draw_column_headers();
}

static void redraw_active_cell(void) {
    draw_grid_row(active_row);
}

static void move_active_vertical(signed char delta) {
    unsigned char old_row;
    unsigned char old_col;
    unsigned char old_scroll_col;

    old_row = active_row;
    old_col = active_col;
    old_scroll_col = scroll_col;

    if (delta < 0) {
        if (active_row > 0u) {
            --active_row;
        }
    } else if (delta > 0) {
        if (active_row + 1u < MAX_ROWS) {
            ++active_row;
        }
    }
    ensure_active_col_visible();
    if (scroll_col != old_scroll_col) {
        draw_screen();
        return;
    }
    redraw_active_transition(old_row, old_col);
}

static void move_active_horizontal(signed char delta) {
    unsigned char old_row;
    unsigned char old_col;
    unsigned char old_scroll_col;

    old_row = active_row;
    old_col = active_col;
    old_scroll_col = scroll_col;

    if (delta < 0) {
        if (active_col > 0u) {
            --active_col;
        }
    } else if (delta > 0) {
        if (active_col + 1u < MAX_COLS) {
            ++active_col;
        }
    }
    ensure_active_col_visible();
    if (scroll_col != old_scroll_col) {
        draw_screen();
        return;
    }
    redraw_active_transition(old_row, old_col);
}

static unsigned char show_confirm(const char *msg) {
    TuiRect win;
    unsigned char key;

    win.x = 8;
    win.y = 9;
    win.w = 24;
    win.h = 6;
    tui_window_title(&win, "confirm", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts_n(10, 11, msg, 20, TUI_COLOR_WHITE);
    tui_puts(10, 13, "y:yes  n:no", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == 'y' || key == 'Y') {
            return 1;
        }
        if (key == 'n' || key == 'N' || key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0;
        }
    }
}

static void show_message(const char *msg, unsigned char color) {
    TuiRect win;

    win.x = 6;
    win.y = 10;
    win.w = 28;
    win.h = 5;
    tui_window(&win, TUI_COLOR_LIGHTBLUE);
    tui_puts_n(8, 11, msg, 24, color);
    tui_puts(10, 13, "press any key", TUI_COLOR_GRAY3);
    tui_getkey();
}

static unsigned char show_simple_menu(const char *title,
                                      const char **items,
                                      unsigned char count,
                                      unsigned char initial) {
    TuiRect win;
    TuiMenu menu;
    unsigned char key;
    unsigned char result;

    win.x = 10;
    win.y = 6;
    win.w = 20;
    win.h = 11;
    tui_window_title(&win, title, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_menu_init(&menu, 11, 8, 18, count, items, count);
    menu.selected = initial;
    menu.item_color = TUI_COLOR_WHITE;
    menu.sel_color = TUI_COLOR_CYAN;
    tui_menu_draw(&menu);
    tui_puts(11, 15, "ret:ok stop:cancel", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 255u;
        }
        if (key == TUI_KEY_RETURN) {
            return menu.selected;
        }
        result = tui_menu_input(&menu, key);
        if (result != 255u) {
            return result;
        }
        tui_menu_draw(&menu);
    }
}

static void show_format_menu(void) {
    unsigned char choice;
    unsigned int idx;
    unsigned char sub_choice;

    choice = show_simple_menu("format", format_menu_items, 5u, 0u);
    if (choice == 255u) {
        draw_screen();
        return;
    }

    idx = cell_index(active_row, active_col);
    switch (choice) {
        case 0:
            sub_choice = show_simple_menu("cell color", color_menu_items, 5u, 0u);
            if (sub_choice != 255u) {
                cell_color[idx] = color_menu_values[sub_choice];
                modified = 1;
                set_status("CELL COLOR SET", TUI_COLOR_LIGHTGREEN);
            }
            break;

        case 1:
            sub_choice = show_simple_menu("col type", type_menu_items, 5u, col_type[active_col]);
            if (sub_choice != 255u) {
                col_type[active_col] = sub_choice;
                modified = 1;
                mark_formula_cells_dirty();
                set_status("COLUMN TYPE SET", TUI_COLOR_LIGHTGREEN);
            }
            break;

        case 2:
            sub_choice = show_simple_menu("col width", width_menu_items, 5u, 1u);
            if (sub_choice != 255u) {
                col_width[active_col] = width_menu_values[sub_choice];
                modified = 1;
                ensure_active_col_visible();
                set_status("COLUMN WIDTH SET", TUI_COLOR_LIGHTGREEN);
            }
            break;

        case 3:
            mark_formula_cells_dirty();
            set_status("RECALCULATED", TUI_COLOR_LIGHTGREEN);
            break;

        case 4:
            if (compact_content_pool()) {
                set_status("CONTENT COMPACTED", TUI_COLOR_LIGHTGREEN);
            } else {
                set_status("COMPACT FAILED", TUI_COLOR_LIGHTRED);
            }
            break;
    }
    draw_screen();
}

static void show_help_popup(void) {
    TuiRect win;
    unsigned char key;

    tui_clear(TUI_COLOR_BLUE);
    win.x = 1;
    win.y = 1;
    win.w = 38;
    win.h = 22;
    tui_window_title(&win, "simple cells help", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(3, 4, "arrows move across fixed 18 rows", TUI_COLOR_WHITE);
    tui_puts(3, 5, "ret edits plain cell inline", TUI_COLOR_WHITE);
    tui_puts(3, 6, "f3 edits formula raw text", TUI_COLOR_WHITE);
    tui_puts(3, 7, "formulas start with =", TUI_COLOR_WHITE);
    tui_puts(3, 8, "ops: + and - with () grouping", TUI_COLOR_WHITE);
    tui_puts(3, 9, "funcs: sum() avg() count()", TUI_COLOR_WHITE);
    tui_puts(3, 10, "refs: a1..j18, ranges: a1:a5", TUI_COLOR_WHITE);
    tui_puts(3, 11, "text in + expressions concatenates", TUI_COLOR_WHITE);
    tui_puts(3, 12, "f1 format menu: color/type/width", TUI_COLOR_WHITE);
    tui_puts(3, 13, "c/v copy one cell and adjust refs", TUI_COLOR_WHITE);
    tui_puts(3, 14, "del clears cell", TUI_COLOR_WHITE);
    tui_puts(3, 15, "f5 save  f7 load", TUI_COLOR_WHITE);
    tui_puts(3, 16, "file dialogs: f3 toggles d8/d9", TUI_COLOR_WHITE);
    tui_puts(3, 17, "f2/f4 app switch  ctrl+b launcher", TUI_COLOR_WHITE);
    tui_puts(3, 18, "run/stop exits when standalone", TUI_COLOR_WHITE);
    tui_puts(3, 20, "ret/f8/stop closes", TUI_COLOR_CYAN);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_F8 || key == TUI_KEY_RETURN ||
            key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            break;
        }
    }
    draw_screen();
}

static unsigned char read_directory(unsigned char start_index,
                                    unsigned char *out_total) {
    unsigned char idx;

    dir_count = 0u;
    if (dir_page_read(storage_device_get_default(),
                      start_index,
                      CBM_T_SEQ,
                      dir_entries,
                      MAX_DIR_ENTRIES,
                      &dir_count,
                      out_total) != DIR_PAGE_RC_OK) {
        if (out_total != 0) {
            *out_total = 0;
        }
        return 0u;
    }
    for (idx = 0u; idx < dir_count; ++idx) {
        dir_ptrs[idx] = dir_entries[idx].name;
    }
    return 1u;
}

static unsigned char show_open_dialog(void) {
    TuiRect win;
    TuiMenu menu;
    unsigned char key;
    unsigned char menu_ready;
    unsigned char selected;
    unsigned char page_start;
    unsigned char total_count;

    selected = 0u;
    page_start = 0u;
    total_count = 0u;

    while (1) {
        tui_clear(TUI_COLOR_BLUE);
        win.x = 0;
        win.y = 0;
        win.w = 40;
        win.h = 24;
        tui_window_title(&win, "load sheet", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
        tui_puts(10, 11, "reading disk...", TUI_COLOR_YELLOW);
        (void)read_directory(page_start, &total_count);
        tui_clear_line(11, 1, 38, TUI_COLOR_WHITE);
        tui_puts(1, 22, "drive:", TUI_COLOR_GRAY3);
        tui_print_uint(8, 22, storage_device_get_default(), TUI_COLOR_CYAN);

        menu_ready = 0u;
        if (total_count == 0u || dir_count == 0u) {
            tui_puts(7, 10, "no seq files found", TUI_COLOR_LIGHTRED);
            tui_puts(1, 24, "f3:drv stop:cancel", TUI_COLOR_GRAY3);
        } else {
            tui_menu_init(&menu, 1, 2, 38, 18, dir_ptrs, dir_count);
            menu.selected = (unsigned char)(selected - page_start);
            menu.item_color = TUI_COLOR_WHITE;
            menu.sel_color = TUI_COLOR_CYAN;
            tui_menu_draw(&menu);
            tui_puts(1, 24, "up/dn sel f3:drv ret:open stop", TUI_COLOR_GRAY3);
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
                return 255u;
            }
            if (!menu_ready) {
                continue;
            }
            if (key == TUI_KEY_RETURN) {
                return (unsigned char)(selected - page_start);
            }
            if (key == TUI_KEY_HOME) {
                selected = 0u;
                page_start = 0u;
                break;
            }
            if (key == TUI_KEY_UP) {
                if (selected > 0u) {
                    --selected;
                    if (selected < page_start) {
                        page_start = selected;
                        break;
                    }
                    menu.selected = (unsigned char)(selected - page_start);
                    tui_menu_draw(&menu);
                }
                continue;
            }
            if (key == TUI_KEY_DOWN) {
                if ((unsigned char)(selected + 1u) < total_count) {
                    ++selected;
                    if (selected >= (unsigned char)(page_start + dir_count)) {
                        page_start = (unsigned char)(selected - dir_count + 1u);
                        break;
                    }
                    menu.selected = (unsigned char)(selected - page_start);
                    tui_menu_draw(&menu);
                }
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
    tui_window_title(&win, "save sheet", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(7, 10, "filename:", TUI_COLOR_WHITE);

    tui_input_init(&input, 7, 11, 20, 16, dialog_buf, TUI_COLOR_CYAN);
    if (filename[0] != 0) {
        strcpy(dialog_buf, filename);
        input.cursor = (unsigned char)strlen(dialog_buf);
    }
    tui_input_draw(&input);
    tui_puts(7, 12, "drive:", TUI_COLOR_GRAY3);
    tui_print_uint(14, 12, storage_device_get_default(), TUI_COLOR_CYAN);
    tui_puts(7, 13, "f3:drv ret:save stop:cancel", TUI_COLOR_GRAY3);

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
            if (dialog_buf[0] == 0) {
                continue;
            }
            if (file_save(dialog_buf)) {
                return 1u;
            }
            return 0u;
        }
        tui_input_draw(&input);
    }
}

static unsigned char file_write_exact(unsigned char lfn, const void *data, unsigned char len) {
    return (unsigned char)(cbm_write(lfn, data, len) == len);
}

static unsigned char file_read_exact(unsigned char lfn, void *data, unsigned char len) {
    unsigned char total;
    int n;

    total = 0u;
    while (total < len) {
        n = cbm_read(lfn, (unsigned char*)data + total, (unsigned char)(len - total));
        if (n <= 0) {
            return 0u;
        }
        total = (unsigned char)(total + (unsigned char)n);
    }
    return 1u;
}

static unsigned char file_save(const char *name) {
    char cmd[24];
    char open_str[24];
    unsigned int idx;
    unsigned char header[11];
    unsigned char rec[5];
    unsigned char end_byte;

    if (!compact_content_pool()) {
        set_status("COMPACT BEFORE SAVE FAIL", TUI_COLOR_LIGHTRED);
        return 0u;
    }

    strcpy(cmd, "s:");
    strcat(cmd, name);
    cbm_open(LFN_CMD, storage_device_get_default(), 15, cmd);
    cbm_close(LFN_CMD);

    strcpy(open_str, name);
    strcat(open_str, ",s,w");
    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        show_message("save open error", TUI_COLOR_LIGHTRED);
        return 0u;
    }

    header[0] = FILE_FMT_MAGIC0;
    header[1] = FILE_FMT_MAGIC1;
    header[2] = FILE_FMT_MAGIC2;
    header[3] = FILE_FMT_MAGIC3;
    header[4] = FILE_FMT_MAGIC4;
    header[5] = FILE_FMT_VERSION;
    header[6] = MAX_ROWS;
    header[7] = MAX_COLS;
    header[8] = active_row;
    header[9] = active_col;
    header[10] = scroll_col;

    if (!file_write_exact(LFN_FILE, header, sizeof(header))) {
        cbm_close(LFN_FILE);
        show_message("save write error", TUI_COLOR_LIGHTRED);
        return 0u;
    }
    for (idx = 0u; idx < MAX_COLS; ++idx) {
        rec[0] = col_width[idx];
        rec[1] = col_type[idx];
        if (!file_write_exact(LFN_FILE, rec, 2u)) {
            cbm_close(LFN_FILE);
            show_message("save col error", TUI_COLOR_LIGHTRED);
            return 0u;
        }
    }
    for (idx = 0u; idx < MAX_CELLS; ++idx) {
        if (!cell_used_idx(idx)) {
            continue;
        }
        rec[0] = (unsigned char)(idx / MAX_COLS);
        rec[1] = (unsigned char)(idx % MAX_COLS);
        rec[2] = (cell_flags[idx] & CELL_FLAG_FORMULA) ? 1u : 0u;
        rec[3] = cell_color[idx];
        rec[4] = cell_len[idx];
        if (!file_write_exact(LFN_FILE, rec, sizeof(rec)) ||
            !file_write_exact(LFN_FILE, cell_raw_ptr_idx(idx), cell_len[idx])) {
            cbm_close(LFN_FILE);
            show_message("save cell error", TUI_COLOR_LIGHTRED);
            return 0u;
        }
    }
    end_byte = FILE_END_MARKER;
    if (!file_write_exact(LFN_FILE, &end_byte, 1u)) {
        cbm_close(LFN_FILE);
        show_message("save end error", TUI_COLOR_LIGHTRED);
        return 0u;
    }

    cbm_close(LFN_FILE);
    str_copy_fit(filename, name, sizeof(filename));
    modified = 0u;
    set_status("SAVED", TUI_COLOR_LIGHTGREEN);
    return 1u;
}

static unsigned char file_load(const char *name) {
    char open_str[24];
    unsigned char header[11];
    unsigned char rec[5];
    unsigned char row;
    unsigned char col;
    unsigned char flags;
    unsigned char color;
    unsigned char len;
    unsigned int idx;

    strcpy(open_str, name);
    strcat(open_str, ",s,r");
    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        show_message("load open error", TUI_COLOR_LIGHTRED);
        return 0u;
    }

    if (!file_read_exact(LFN_FILE, header, sizeof(header)) ||
        header[0] != FILE_FMT_MAGIC0 ||
        header[1] != FILE_FMT_MAGIC1 ||
        header[2] != FILE_FMT_MAGIC2 ||
        header[3] != FILE_FMT_MAGIC3 ||
        header[4] != FILE_FMT_MAGIC4 ||
        header[5] != FILE_FMT_VERSION ||
        header[6] != MAX_ROWS ||
        header[7] != MAX_COLS) {
        cbm_close(LFN_FILE);
        show_message("invalid simple cells file", TUI_COLOR_LIGHTRED);
        return 0u;
    }

    clear_sheet_state();
    for (idx = 0u; idx < MAX_COLS; ++idx) {
        if (!file_read_exact(LFN_FILE, rec, 2u)) {
            cbm_close(LFN_FILE);
            show_message("load column error", TUI_COLOR_LIGHTRED);
            return 0u;
        }
        col_width[idx] = rec[0];
        col_type[idx] = rec[1];
    }

    while (1) {
        if (!file_read_exact(LFN_FILE, &row, 1u)) {
            cbm_close(LFN_FILE);
            show_message("load truncation", TUI_COLOR_LIGHTRED);
            return 0u;
        }
        if (row == FILE_END_MARKER) {
            break;
        }
        if (!file_read_exact(LFN_FILE, rec, 4u)) {
            cbm_close(LFN_FILE);
            show_message("load record error", TUI_COLOR_LIGHTRED);
            return 0u;
        }
        col = rec[0];
        flags = rec[1];
        color = rec[2];
        len = rec[3];
        if (row >= MAX_ROWS || col >= MAX_COLS || len > MAX_RAW_LEN ||
            !file_read_exact(LFN_FILE, file_raw_buf, len)) {
            cbm_close(LFN_FILE);
            show_message("load data error", TUI_COLOR_LIGHTRED);
            return 0u;
        }
        file_raw_buf[len] = 0;
        if (!set_cell_raw(row, col, (const char*)file_raw_buf)) {
            cbm_close(LFN_FILE);
            show_message("load cell ram full", TUI_COLOR_LIGHTRED);
            return 0u;
        }
        idx = cell_index(row, col);
        if ((flags & 1u) == 0u) {
            cell_flags[idx] &= (unsigned char)~CELL_FLAG_FORMULA;
        }
        cell_color[idx] = color;
    }
    cbm_close(LFN_FILE);

    active_row = header[8];
    active_col = header[9];
    scroll_col = header[10];
    if (!sheet_validate_state()) {
        reset_sheet_runtime("BAD LOAD RESET", TUI_COLOR_LIGHTRED);
        return 0u;
    }
    clamp_view();
    str_copy_fit(filename, name, sizeof(filename));
    modified = 0u;
    mark_formula_cells_dirty();
    set_status("LOADED", TUI_COLOR_LIGHTGREEN);
    return 1u;
}

static unsigned char edit_value_inline(void) {
    TuiInput input;
    unsigned char key;
    unsigned int idx;

    idx = cell_index(active_row, active_col);
    if (cell_used_idx(idx) && (cell_flags[idx] & CELL_FLAG_FORMULA)) {
        set_status("USE F3 FOR FORMULA", TUI_COLOR_YELLOW);
        draw_status();
        return 0u;
    }

    dialog_buf[0] = 0;
    if (cell_used_idx(idx)) {
        str_copy_fit(dialog_buf, cell_raw_ptr_idx(idx), sizeof(dialog_buf));
    }

    tui_clear_line(FORMULA_Y, 0, 40, TUI_COLOR_WHITE);
    tui_puts(0, FORMULA_Y, "value:", TUI_COLOR_GRAY3);
    tui_input_init(&input, 7, FORMULA_Y, 32, MAX_RAW_LEN, dialog_buf, TUI_COLOR_CYAN);
    input.cursor = (unsigned char)strlen(dialog_buf);
    tui_input_draw(&input);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            cursor(0);
            draw_formula_bar();
            return 0u;
        }
        if (tui_input_key(&input, key)) {
            if (set_cell_raw(active_row, active_col, dialog_buf)) {
                set_status("VALUE UPDATED", TUI_COLOR_LIGHTGREEN);
            }
            cursor(0);
            draw_screen();
            return 1u;
        }
        tui_input_draw(&input);
    }
}

static unsigned char edit_formula_popup(void) {
    TuiRect win;
    TuiInput input;
    unsigned char key;
    unsigned int idx;

    idx = cell_index(active_row, active_col);
    dialog_buf[0] = '=';
    dialog_buf[1] = 0;
    if (cell_used_idx(idx) && (cell_flags[idx] & CELL_FLAG_FORMULA)) {
        str_copy_fit(dialog_buf, cell_raw_ptr_idx(idx), sizeof(dialog_buf));
    }

    tui_clear(TUI_COLOR_BLUE);
    win.x = 3;
    win.y = 7;
    win.w = 34;
    win.h = 8;
    tui_window_title(&win, "edit formula", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(5, 10, "formula:", TUI_COLOR_WHITE);
    tui_input_init(&input, 5, 11, 28, MAX_RAW_LEN, dialog_buf, TUI_COLOR_CYAN);
    input.cursor = (unsigned char)strlen(dialog_buf);
    tui_input_draw(&input);
    tui_puts(5, 13, "ret:save stop:cancel", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            cursor(0);
            draw_screen();
            return 0u;
        }
        if (tui_input_key(&input, key)) {
            if (dialog_buf[0] == 0) {
                draw_screen();
                return 0u;
            }
            if (dialog_buf[0] != '=') {
                unsigned char len;

                len = (unsigned char)strlen(dialog_buf);
                if (len >= MAX_RAW_LEN) {
                    show_message("formula too long", TUI_COLOR_LIGHTRED);
                    cursor(0);
                    draw_screen();
                    return 0u;
                }
                memmove(&dialog_buf[1], &dialog_buf[0], len + 1u);
                dialog_buf[0] = '=';
            }
            if (strcmp(dialog_buf, "=") == 0) {
                clear_active_cell();
            } else {
                set_cell_raw(active_row, active_col, dialog_buf);
                set_status("FORMULA UPDATED", TUI_COLOR_LIGHTGREEN);
            }
            cursor(0);
            draw_screen();
            return 1u;
        }
        tui_input_draw(&input);
    }
}

static void copy_cell_to_local_clipboard(void) {
    unsigned int idx;

    idx = cell_index(active_row, active_col);
    if (!cell_used_idx(idx)) {
        set_status("NOTHING TO COPY", TUI_COLOR_YELLOW);
        return;
    }
    str_copy_fit(clipboard_raw, cell_raw_ptr_idx(idx), sizeof(clipboard_raw));
    clipboard_len = (unsigned char)strlen(clipboard_raw);
    clipboard_src_row = active_row;
    clipboard_src_col = active_col;
    clipboard_color = cell_color[idx];
    clipboard_valid = 1u;
    set_status("CELL COPIED", TUI_COLOR_LIGHTGREEN);
}

static void append_adjusted_ref(char *out,
                                unsigned char *out_pos,
                                signed char row_delta,
                                signed char col_delta,
                                unsigned char src_row,
                                unsigned char src_col) {
    signed char new_row;
    signed char new_col;
    char buf[5];
    unsigned char pos;

    new_row = (signed char)src_row + row_delta;
    new_col = (signed char)src_col + col_delta;

    if (new_row < 0 || new_col < 0) {
        out[(*out_pos)++] = 'A';
        out[(*out_pos)++] = '0';
        out[*out_pos] = 0;
        return;
    }

    if (new_col > 25) {
        out[(*out_pos)++] = 'Z';
        out[(*out_pos)++] = '0';
        out[*out_pos] = 0;
        return;
    }

    out[(*out_pos)++] = (char)('A' + new_col);
    int_to_str((signed int)(new_row + 1), buf);
    pos = 0u;
    while (buf[pos] != 0 && *out_pos < MAX_RAW_LEN) {
        out[(*out_pos)++] = buf[pos++];
    }
    out[*out_pos] = 0;
}

static void rewrite_formula_refs(const char *src,
                                 unsigned char src_row,
                                 unsigned char src_col,
                                 unsigned char dst_row,
                                 unsigned char dst_col,
                                 char *out) {
    unsigned char i;
    unsigned char out_pos;
    signed char row_delta;
    signed char col_delta;

    i = 0u;
    out_pos = 0u;
    row_delta = (signed char)dst_row - (signed char)src_row;
    col_delta = (signed char)dst_col - (signed char)src_col;

    while (src[i] != 0 && out_pos < MAX_RAW_LEN) {
        unsigned char ref_col;
        unsigned char ref_row;
        unsigned char ref_end;

        if (parse_cell_ref_token(src, i, &ref_col, &ref_row, &ref_end)) {
            append_adjusted_ref(out,
                                &out_pos,
                                row_delta,
                                col_delta,
                                ref_row,
                                ref_col);
            i = ref_end;
            continue;
        }
        out[out_pos++] = src[i++];
    }
    out[out_pos] = 0;
}

static void paste_cell_from_local_clipboard(void) {
    unsigned int idx;
    char raw[MAX_RAW_LEN + 1];

    if (!clipboard_valid) {
        set_status("CLIPBOARD EMPTY", TUI_COLOR_YELLOW);
        return;
    }

    if (clipboard_raw[0] == '=') {
        rewrite_formula_refs(clipboard_raw,
                             clipboard_src_row,
                             clipboard_src_col,
                             active_row,
                             active_col,
                             raw);
    } else {
        str_copy_fit(raw, clipboard_raw, sizeof(raw));
    }

    if (set_cell_raw(active_row, active_col, raw)) {
        idx = cell_index(active_row, active_col);
        cell_color[idx] = clipboard_color;
        if (!sheet_validate_state()) {
            reset_sheet_runtime("BAD PASTE RESET", TUI_COLOR_LIGHTRED);
            return;
        }
        modified = 1u;
        set_status("CELL PASTED", TUI_COLOR_LIGHTGREEN);
    }
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    (void)resume_save_segments(simplecells_resume_write_segments, SIMPLECELLS_RESUME_SEG_COUNT);
}

static unsigned char restore_resume_state(void) {
    unsigned int payload_len;

    payload_len = 0u;
    if (!resume_ready) {
        return 0u;
    }
    if (!resume_load_segments(simplecells_resume_read_segments,
                              SIMPLECELLS_RESUME_SEG_COUNT,
                              &payload_len)) {
        return 0u;
    }
    clamp_view();
    mark_formula_cells_dirty();
    clipboard_raw[sizeof(clipboard_raw) - 1u] = 0;
    filename[sizeof(filename) - 1u] = 0;
    if (!sheet_validate_state()) {
        reset_sheet_runtime("BAD RESUME RESET", TUI_COLOR_LIGHTRED);
        return 0u;
    }
    return 1u;
}

static void app_loop(void) {
    unsigned char key;

    draw_screen();
    while (running) {
        key = tui_getkey();

        if (key == KEY_CTRL_B) {
            resume_save_state();
            tui_return_to_launcher();
        }
        if (key == TUI_KEY_NEXT_APP) {
            unsigned char current;
            unsigned char next;

            current = SHIM_CURRENT_BANK;
            next = tui_get_next_app(current);
            if (next != 0u) {
                resume_save_state();
                tui_switch_to_app(next);
            }
            continue;
        }
        if (key == TUI_KEY_PREV_APP) {
            unsigned char current;
            unsigned char prev;

            current = SHIM_CURRENT_BANK;
            prev = tui_get_prev_app(current);
            if (prev != 0u) {
                resume_save_state();
                tui_switch_to_app(prev);
            }
            continue;
        }

        switch (key) {
            case TUI_KEY_RUNSTOP:
                running = 0u;
                break;

            case TUI_KEY_UP:
                move_active_vertical(-1);
                break;

            case TUI_KEY_DOWN:
                move_active_vertical(1);
                break;

            case TUI_KEY_LEFT:
                move_active_horizontal(-1);
                break;

            case TUI_KEY_RIGHT:
                move_active_horizontal(1);
                break;

            case TUI_KEY_RETURN:
                (void)edit_value_inline();
                break;

            case TUI_KEY_F1:
                show_format_menu();
                break;

            case TUI_KEY_F3:
                (void)edit_formula_popup();
                break;

            case TUI_KEY_F5:
                (void)show_save_dialog();
                draw_screen();
                break;

            case TUI_KEY_F7:
                {
                    unsigned char selected;

                    if (modified && !show_confirm("discard changes?")) {
                        draw_screen();
                        break;
                    }
                    selected = show_open_dialog();
                    if (selected != 255u && selected < dir_count) {
                        tui_clear(TUI_COLOR_BLUE);
                        tui_puts(14, 12, "loading...", TUI_COLOR_YELLOW);
                        (void)file_load(dir_entries[selected].name);
                    }
                    draw_screen();
                }
                break;

            case TUI_KEY_F8:
                show_help_popup();
                break;

            case TUI_KEY_DEL:
                clear_active_cell();
                draw_screen();
                break;

            case 'c':
            case 'C':
                copy_cell_to_local_clipboard();
                draw_status();
                break;

            case 'v':
            case 'V':
                paste_cell_from_local_clipboard();
                draw_screen();
                break;

            default:
                break;
        }
    }

    __asm__("jmp $FCE2");
}

int main(void) {
    unsigned char bank;

    init_defaults();
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1u && bank <= 15u) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1u;
    }
    if (!restore_resume_state()) {
        clamp_view();
    }
    if (!sheet_validate_state()) {
        reset_sheet_runtime("BAD STATE RESET", TUI_COLOR_LIGHTRED);
    }
    app_loop();
    return 0;
}
