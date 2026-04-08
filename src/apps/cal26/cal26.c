/*
 * cal26.c - Ready OS Calendar app for year 2026
 *
 * Views: month, week, day, upcoming
 * Storage: CAL26.REL (events/index), CAL26CFG.REL (config)
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include "../../lib/clipboard.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define CAL_YEAR 2026
#define CAL_DAYS 365

#define VIEW_MONTH    0
#define VIEW_WEEK     1
#define VIEW_DAY      2
#define VIEW_UPCOMING 3

/* REL files */
#define FILE_EVENTS_NAME "cal26.rel"
#define FILE_CFG_NAME    "cal26cfg.rel"

#define LFN_EVENTS  2
#define LFN_CFG     3
#define LFN_CMD     15
#define REL_SA      2

#define REC_EVENTS_LEN 64
#define REC_CFG_LEN    32

/* Internal REL error tracking */
#define REL_STEP_NONE      0
#define REL_STEP_OPEN_DATA 1
#define REL_STEP_OPEN_CMD  2
#define REL_STEP_POS       3
#define REL_STEP_READ      4
#define REL_STEP_WRITE     5
#define REL_STEP_VERIFY    6

#define REL_ERR_VERIFY     254
#define REL_ERR_UNKNOWN    255

/* CAL26.REL record layout */
#define REC_SUPERBLOCK 1
#define REC_DAY_BASE   2   /* day N is record REC_DAY_BASE + (N-1) */
#define REC_EVENT_BASE 367

#define SB_MAGIC0 0
#define SB_MAGIC1 1
#define SB_MAGIC2 2
#define SB_MAGIC3 3
#define SB_VERSION 4
#define SB_FREE_DEC 5
#define SB_NEXT_DEC 10
#define SB_CSUM_DEC 15

#define IDX_TYPE    0
#define IDX_DAY_DEC  1
#define IDX_HEAD_DEC 6
#define IDX_TAIL_DEC 11
#define IDX_CNT_DEC  16

#define EVT_TYPE    0
#define EVT_DONE    1
#define EVT_DELETED 2
#define EVT_DAY_DEC 3
#define EVT_PREV_DEC 6
#define EVT_NEXT_DEC 11
#define EVT_LEN_DEC 16
#define EVT_TEXT    18
#define EVT_TEXT_MAX (REC_EVENTS_LEN - EVT_TEXT)

#define REC_TYPE_INDEX 'i'
#define REC_TYPE_EVENT 'e'

#define EVT_FLAG_DONE    0x01
#define EVT_FLAG_DELETED 0x02

/* CAL26CFG.REL record layout */
#define CFG_MAGIC0   0
#define CFG_MAGIC1   1
#define CFG_MAGIC2   2
#define CFG_MAGIC3   3
#define CFG_VERSION  4
#define CFG_TODAY_DEC 5
#define CFG_WEEKSTART 8

/* UI */
#define LIST_TOP 4
#define LIST_H   16

#define DAY_ROW_VISIBLE_WIDTH 40
#define DAY_ROW_PREFIX_LEN     6

#define MAX_DAY_ITEMS 32
#define MAX_EVENT_TEXT EVT_TEXT_MAX

#define UPCOMING_PAGE_SIZE 6

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Data
 *---------------------------------------------------------------------------*/

typedef struct {
    unsigned int doy;
    const char *name;
    unsigned char color;
} Holiday;

typedef struct {
    unsigned char flags;
    unsigned int day;
    unsigned int prev;
    unsigned int next;
    unsigned char len;
    char text[MAX_EVENT_TEXT + 1];
} EventRec;

typedef struct {
    unsigned int doy;
    unsigned int recno;
    unsigned char flags;
    char text[MAX_EVENT_TEXT + 1];
} UpcomingItem;

static const unsigned char month_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const char *month_names[12] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
};

static const char *month_abbr[12] = {
    "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
};

static const char *dow_names[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"
};

/* Sunday=0. 2026-01-01 is Thursday. */
#define DOW_JAN1_2026 4

static const Holiday holidays[] = {
    { 1,   "NEW YEAR'S DAY", TUI_COLOR_YELLOW },
    { 19,  "MLK DAY", TUI_COLOR_YELLOW },
    { 45,  "VALENTINE'S DAY", TUI_COLOR_CYAN },
    { 47,  "WASHINGTON'S BDAY", TUI_COLOR_YELLOW },
    { 95,  "EASTER", TUI_COLOR_CYAN },
    { 130, "MOTHER'S DAY", TUI_COLOR_CYAN },
    { 145, "MEMORIAL DAY", TUI_COLOR_YELLOW },
    { 170, "JUNETEENTH", TUI_COLOR_YELLOW },
    { 172, "FATHER'S DAY", TUI_COLOR_CYAN },
    { 184, "INDEPENDENCE DAY OBS", TUI_COLOR_YELLOW },
    { 185, "INDEPENDENCE DAY", TUI_COLOR_YELLOW },
    { 250, "LABOR DAY", TUI_COLOR_YELLOW },
    { 285, "COLUMBUS/INDIGENOUS DAY", TUI_COLOR_YELLOW },
    { 304, "HALLOWEEN", TUI_COLOR_CYAN },
    { 315, "VETERANS DAY", TUI_COLOR_YELLOW },
    { 330, "THANKSGIVING", TUI_COLOR_YELLOW },
    { 331, "BLACK FRIDAY", TUI_COLOR_CYAN },
    { 358, "CHRISTMAS EVE", TUI_COLOR_CYAN },
    { 359, "CHRISTMAS DAY", TUI_COLOR_YELLOW },
    { 365, "NEW YEAR'S EVE", TUI_COLOR_CYAN }
};

#define HOLIDAY_COUNT (sizeof(holidays) / sizeof(holidays[0]))

/* Persistent in-memory index loaded from day records */
static unsigned int day_head[CAL_DAYS];
static unsigned int day_tail[CAL_DAYS];
static unsigned int day_count[CAL_DAYS];

/* Superblock values */
static unsigned int free_head;
static unsigned int next_record;

/* Day cache */
static unsigned int day_item_rec[MAX_DAY_ITEMS];
static unsigned char day_item_flags[MAX_DAY_ITEMS];
static char day_item_text[MAX_DAY_ITEMS][MAX_EVENT_TEXT + 1];
static unsigned char day_item_count;
static unsigned char day_sel;
static unsigned char day_scroll;

/* Upcoming cache */
static UpcomingItem upcoming_items[UPCOMING_PAGE_SIZE];
static unsigned char upcoming_count;
static unsigned char upcoming_sel;
static unsigned int upcoming_page;
static unsigned char upcoming_has_more;

/* App state */
static unsigned char running;
static unsigned char current_view;
static unsigned int selected_doy;
static unsigned int today_doy;
static unsigned char week_start_cfg;

static char status_msg[40];
static unsigned char status_color;
static unsigned char storage_loaded;
static unsigned char suppress_load_prompt;
static unsigned char day_cache_capped;
static unsigned char rel_last_step;
static unsigned char rel_last_dos_code;
static char rel_last_dos_msg[16];
static unsigned char week_label_valid;
static unsigned int week_label_start_doy;
static char week_label_text[7][20];
static unsigned char week_label_color[7];
static unsigned char resume_ready;

static ResumeWriteSegment cal26_resume_write_segments[] = {
    { day_head, sizeof(day_head) },
    { day_tail, sizeof(day_tail) },
    { day_count, sizeof(day_count) },
    { &free_head, sizeof(free_head) },
    { &next_record, sizeof(next_record) },
    { day_item_rec, sizeof(day_item_rec) },
    { day_item_flags, sizeof(day_item_flags) },
    { day_item_text, sizeof(day_item_text) },
    { &day_item_count, sizeof(day_item_count) },
    { &day_sel, sizeof(day_sel) },
    { &day_scroll, sizeof(day_scroll) },
    { &day_cache_capped, sizeof(day_cache_capped) },
    { upcoming_items, sizeof(upcoming_items) },
    { &upcoming_count, sizeof(upcoming_count) },
    { &upcoming_sel, sizeof(upcoming_sel) },
    { &upcoming_page, sizeof(upcoming_page) },
    { &upcoming_has_more, sizeof(upcoming_has_more) },
    { &current_view, sizeof(current_view) },
    { &selected_doy, sizeof(selected_doy) },
    { &today_doy, sizeof(today_doy) },
    { &week_start_cfg, sizeof(week_start_cfg) },
    { &storage_loaded, sizeof(storage_loaded) },
    { &suppress_load_prompt, sizeof(suppress_load_prompt) },
    { week_label_text, sizeof(week_label_text) },
    { week_label_color, sizeof(week_label_color) },
    { &week_label_valid, sizeof(week_label_valid) },
    { &week_label_start_doy, sizeof(week_label_start_doy) },
};

static ResumeReadSegment cal26_resume_read_segments[] = {
    { day_head, sizeof(day_head) },
    { day_tail, sizeof(day_tail) },
    { day_count, sizeof(day_count) },
    { &free_head, sizeof(free_head) },
    { &next_record, sizeof(next_record) },
    { day_item_rec, sizeof(day_item_rec) },
    { day_item_flags, sizeof(day_item_flags) },
    { day_item_text, sizeof(day_item_text) },
    { &day_item_count, sizeof(day_item_count) },
    { &day_sel, sizeof(day_sel) },
    { &day_scroll, sizeof(day_scroll) },
    { &day_cache_capped, sizeof(day_cache_capped) },
    { upcoming_items, sizeof(upcoming_items) },
    { &upcoming_count, sizeof(upcoming_count) },
    { &upcoming_sel, sizeof(upcoming_sel) },
    { &upcoming_page, sizeof(upcoming_page) },
    { &upcoming_has_more, sizeof(upcoming_has_more) },
    { &current_view, sizeof(current_view) },
    { &selected_doy, sizeof(selected_doy) },
    { &today_doy, sizeof(today_doy) },
    { &week_start_cfg, sizeof(week_start_cfg) },
    { &storage_loaded, sizeof(storage_loaded) },
    { &suppress_load_prompt, sizeof(suppress_load_prompt) },
    { week_label_text, sizeof(week_label_text) },
    { week_label_color, sizeof(week_label_color) },
    { &week_label_valid, sizeof(week_label_valid) },
    { &week_label_start_doy, sizeof(week_label_start_doy) },
};

#define CAL26_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(cal26_resume_read_segments) / sizeof(cal26_resume_read_segments[0])))

/* Shared buffers to reduce stack pressure */
static unsigned char rec_buf64[REC_EVENTS_LEN];
static unsigned char rec_buf32[REC_CFG_LEN];
static char text_buf[80];
static char clip_buf[180];
static char holiday_buf[40];

/* Forward declarations for functions used before definition */
static void set_status(const char *msg, unsigned char color);
static void draw_status_line(void);
static unsigned char init_or_load_events(void);
static unsigned char init_or_load_config(void);
static unsigned char load_day_cache(unsigned int doy);
static unsigned char format_week_task_label(unsigned int doy, char *out, unsigned char out_cap);
static unsigned char build_upcoming_page(unsigned int page);
static void clear_calendar_data(void);
static void invalidate_week_label_cache(void);
static unsigned int week_view_start_doy(unsigned int doy);
static void draw_week_selected_date_field(void);
static void build_week_label_cache(unsigned int start);
static void draw_week_row_from_cache(unsigned int start, unsigned char row, unsigned char selected);
static unsigned char load_calendar_storage(unsigned char preserve_trace);
static void format_rel_error_status(const char *prefix);
static unsigned char rebuild_calendar_storage(void);
static void redraw(void);
static void resume_save_state(void);
static unsigned char resume_restore_state(void);
static void cal26_return_to_launcher(void);
static void show_help_screen(void);

/*---------------------------------------------------------------------------
 * Utility helpers
 *---------------------------------------------------------------------------*/

static unsigned int rd_dec(const unsigned char *buf, unsigned char off, unsigned char digits) {
    unsigned char i;
    unsigned int out = 0;
    unsigned char c;

    for (i = 0; i < digits; ++i) {
        c = buf[off + i];
        if (c < '0' || c > '9') {
            return 0xFFFF;
        }
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

static void rec_clear(unsigned char *buf, unsigned char len) {
    memset(buf, ' ', len);
}

static unsigned int clamp_doy(unsigned int doy) {
    if (doy < 1) return 1;
    if (doy > CAL_DAYS) return CAL_DAYS;
    return doy;
}

static unsigned int doy_from_month_day(unsigned char month, unsigned char day) {
    unsigned char m;
    unsigned int doy = day;

    for (m = 1; m < month; ++m) {
        doy += month_days[m - 1];
    }
    return doy;
}

static void month_day_from_doy(unsigned int doy, unsigned char *month, unsigned char *day) {
    unsigned char m = 1;
    unsigned int rem = doy;

    while (m <= 12) {
        if (rem <= month_days[m - 1]) {
            *month = m;
            *day = (unsigned char)rem;
            return;
        }
        rem -= month_days[m - 1];
        ++m;
    }

    *month = 12;
    *day = 31;
}

static unsigned char dow_from_doy(unsigned int doy) {
    return (unsigned char)((DOW_JAN1_2026 + (doy - 1)) % 7);
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
    return 0;
}

static unsigned int parse_compile_today(void) {
    /* __DATE__ format: "Mmm dd yyyy" */
    const char *d = __DATE__;
    unsigned char month;
    unsigned char day;
    unsigned int year;

    month = parse_month_3(d);

    day = 0;
    if (d[4] >= '0' && d[4] <= '9') {
        day = (unsigned char)(d[4] - '0');
    }
    if (d[5] >= '0' && d[5] <= '9') {
        day = (unsigned char)(day * 10 + (d[5] - '0'));
    }

    year = 0;
    if (d[7] >= '0' && d[7] <= '9') year += (unsigned int)(d[7] - '0') * 1000;
    if (d[8] >= '0' && d[8] <= '9') year += (unsigned int)(d[8] - '0') * 100;
    if (d[9] >= '0' && d[9] <= '9') year += (unsigned int)(d[9] - '0') * 10;
    if (d[10] >= '0' && d[10] <= '9') year += (unsigned int)(d[10] - '0');

    if (year != CAL_YEAR || month == 0 || day == 0 || day > month_days[month - 1]) {
        /* Fallback to 2026-02-11 */
        return 42;
    }

    return doy_from_month_day(month, day);
}

static void format_mmdd(unsigned int doy, char *out) {
    unsigned char m, d;
    month_day_from_doy(doy, &m, &d);
    out[0] = (char)('0' + (m / 10));
    out[1] = (char)('0' + (m % 10));
    out[2] = '/';
    out[3] = (char)('0' + (d / 10));
    out[4] = (char)('0' + (d % 10));
    out[5] = 0;
}

static void format_iso(unsigned int doy, char *out) {
    unsigned char m, d;
    month_day_from_doy(doy, &m, &d);

    out[0] = '2'; out[1] = '0'; out[2] = '2'; out[3] = '6'; out[4] = '-';
    out[5] = (char)('0' + (m / 10));
    out[6] = (char)('0' + (m % 10));
    out[7] = '-';
    out[8] = (char)('0' + (d / 10));
    out[9] = (char)('0' + (d % 10));
    out[10] = 0;
}

static void format_month_day(unsigned int doy, char *out) {
    unsigned char m, d;

    month_day_from_doy(doy, &m, &d);

    out[0] = month_abbr[m - 1][0];
    out[1] = month_abbr[m - 1][1];
    out[2] = month_abbr[m - 1][2];
    out[3] = ' ';
    if (d >= 10) {
        out[4] = (char)('0' + (d / 10));
        out[5] = (char)('0' + (d % 10));
        out[6] = 0;
    } else {
        out[4] = ' ';
        out[5] = (char)('0' + d);
        out[6] = 0;
    }
}

static void show_help_screen(void) {
    tui_clear(TUI_COLOR_BLUE);
    tui_puts_n(1, 1, "CALENDAR 26 HELP", 38, TUI_COLOR_YELLOW);
    tui_puts_n(1, 3, "ARROWS: MOVE DATE SELECTION", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 4, ",/.: CHANGE MONTH/WEEK", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 5, "F7:NEXT VIEW F8:HELP", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 6, "L:LOAD R:REBLD CALENDAR", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 7, "RETURN:DAY/JUMP, T:SET TODAY", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 8, "N:NEW A:EDIT D:DELETE", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 9, "ENTER: SAVE EVENT CHANGES", 38, TUI_COLOR_WHITE);
    tui_puts_n(1,10, "F1/F3: COPY/PASTE FROM DAY", 38, TUI_COLOR_WHITE);
    tui_puts_n(1,11, "UP/DN: LIST APPOINTMENTS, SPC:DONE", 38, TUI_COLOR_WHITE);
    tui_puts_n(1,12, "F2/F4: APP SWITCH", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 16, "PRESS ANY KEY TO RETURN", 38, TUI_COLOR_CYAN);
    (void)tui_getkey();
}

static unsigned char has_holiday(unsigned int doy) {
    unsigned char i;
    for (i = 0; i < HOLIDAY_COUNT; ++i) {
        if (holidays[i].doy == doy) return 1;
    }
    return 0;
}

static const Holiday *first_holiday(unsigned int doy) {
    unsigned char i;
    for (i = 0; i < HOLIDAY_COUNT; ++i) {
        if (holidays[i].doy == doy) return &holidays[i];
    }
    return 0;
}

static const char *holiday_list(unsigned int doy) {
    unsigned char i;
    unsigned char first = 1;
    unsigned char left = 39;

    holiday_buf[0] = 0;

    for (i = 0; i < HOLIDAY_COUNT; ++i) {
        if (holidays[i].doy != doy) continue;

        if (!first) {
            if (left < 2) break;
            strcat(holiday_buf, ", ");
            left -= 2;
        }

        if (strlen(holidays[i].name) <= left) {
            strcat(holiday_buf, holidays[i].name);
            left = (unsigned char)(left - strlen(holidays[i].name));
        } else {
            strncat(holiday_buf, holidays[i].name, left);
            left = 0;
        }

        first = 0;
        if (left == 0) break;
    }

    return holiday_buf;
}

static void set_status(const char *msg, unsigned char color) {
    strncpy(status_msg, msg, 39);
    status_msg[39] = 0;
    status_color = color;
}

static void draw_status_line(void) {
    tui_clear_line(24, 0, 40, status_color);
    tui_puts_n(0, 24, status_msg, 40, status_color);
}

static void begin_save_indicator(void) {
    set_status("SAVING...", TUI_COLOR_YELLOW);
    draw_status_line();
}

static void end_save_indicator(unsigned char ok) {
    if (ok) {
        set_status("SAVED", TUI_COLOR_LIGHTGREEN);
    } else {
        set_status("SAVE ERROR", TUI_COLOR_LIGHTRED);
    }
}

static void begin_load_indicator(void) {
    TuiRect win;

    redraw();

    win.x = 12;
    win.y = 9;
    win.w = 16;
    win.h = 5;

    tui_window_title(&win, "LOADING", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(14, 11, "PLEASE WAIT", TUI_COLOR_WHITE);
}

static void end_load_indicator(void) {
    redraw();
}

static void invert_line(unsigned char y, unsigned char x, unsigned char len) {
    unsigned int off;
    unsigned char i;

    off = (unsigned int)y * 40 + x;
    for (i = 0; i < len && (x + i) < 40; ++i) {
        TUI_SCREEN[off + i] |= 0x80;
    }
}

/*---------------------------------------------------------------------------
 * REL / disk helpers
 *---------------------------------------------------------------------------*/

static unsigned char is_digit_ch(char c) {
    return (unsigned char)(c >= '0' && c <= '9');
}

static void rel_clear_error(void) {
    rel_last_step = REL_STEP_NONE;
    rel_last_dos_code = 0;
    rel_last_dos_msg[0] = 0;
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

static unsigned char rel_error_is_transient_io(void) {
    if (rel_last_dos_code == 30 || rel_last_dos_code == 31 || rel_last_dos_code == 39) return 1;
    if (rel_last_dos_code == 50 || rel_last_dos_code == 70) return 1;
    return 0;
}

static unsigned char rel_error_is_initable(void) {
    /* Missing/not-yet-valid events store may report either DOS missing or
     * verify failure against an empty newly-created REL. */
    if (rel_last_dos_code == 62 || rel_last_dos_code == 63) return 1;
    if (rel_last_dos_code == 30 || rel_last_dos_code == 31 || rel_last_dos_code == 39) return 1;
    if (rel_last_dos_code == 50 || rel_last_dos_code == 70) return 1;
    if (rel_last_dos_code == REL_ERR_VERIFY) return 1;
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

static void format_rel_error_status(const char *prefix) {
    char line[40];
    char code[3];
    unsigned char left;

    line[0] = 0;
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

    set_status(line, TUI_COLOR_LIGHTRED);
}

static unsigned char dos_scratch(const char *name) {
    static char cmd[24];
    strcpy(cmd, "s:");
    strcat(cmd, name);
    if (cbm_open(LFN_CMD, 8, 15, cmd) != 0) {
        return 1;
    }
    cbm_close(LFN_CMD);
    return 0;
}

static void scratch_cfg_variants(void) {
    dos_scratch(FILE_CFG_NAME);
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
        cbm_close(LFN_CMD);
        rel_set_error(REL_STEP_OPEN_DATA, REL_ERR_UNKNOWN, "OPEN DATA");
        return 1;
    }
    return 0;
}

static void rel_session_close(unsigned char lfn_data) {
    cbm_close(lfn_data);
    cbm_close(LFN_CMD);
}

static unsigned char open_events_session(void) {
    return rel_session_open(LFN_EVENTS, FILE_EVENTS_NAME, REC_EVENTS_LEN);
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

    for (i = 0; i < 5; ++i) {
        cbm_k_bsout(cmd[i]);
    }
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
    n = cbm_read(lfn_data, buf, len);
    if (n != len) {
        rel_set_error(REL_STEP_READ, REL_ERR_UNKNOWN, "SHORT READ");
        return 1;
    }

    return 0;
}

static unsigned char rel_write_record_session(unsigned char lfn_data,
                                              unsigned int rec,
                                              const unsigned char *buf,
                                              unsigned char len) {
    unsigned char i;
    unsigned char rc;

    if (rel_position_session(lfn_data, rec, 1) != 0) return 1;

    rc = cbm_k_ckout(lfn_data);
    if (rc != 0) {
        cbm_k_clrch();
        rel_set_error(REL_STEP_WRITE, REL_ERR_UNKNOWN, "CKOUT");
        return 1;
    }

    for (i = 0; i < len; ++i) {
        cbm_k_bsout(buf[i]);
    }
    cbm_k_clrch();

    return 0;
}

static unsigned int sb_checksum(const unsigned char *sb) {
    unsigned int s = 0;
    unsigned char i;
    for (i = 0; i < SB_CSUM_DEC; ++i) s += sb[i];
    return s;
}

/*---------------------------------------------------------------------------
 * Storage model
 *---------------------------------------------------------------------------*/

static void build_superblock(unsigned char *sb) {
    rec_clear(sb, REC_EVENTS_LEN);
    sb[SB_MAGIC0] = 'c';
    sb[SB_MAGIC1] = '2';
    sb[SB_MAGIC2] = '6';
    sb[SB_MAGIC3] = 'e';
    sb[SB_VERSION] = '1';
    wr_dec(sb, SB_FREE_DEC, 5, free_head);
    wr_dec(sb, SB_NEXT_DEC, 5, next_record);
    wr_dec(sb, SB_CSUM_DEC, 5, sb_checksum(sb));
}

static unsigned char write_superblock(void) {
    unsigned char ok;

    build_superblock(rec_buf64);

    if (open_events_session() != 0) return 1;
    ok = (rel_write_record_session(LFN_EVENTS, REC_SUPERBLOCK, rec_buf64, REC_EVENTS_LEN) == 0);
    rel_session_close(LFN_EVENTS);

    return ok ? 0 : 1;
}

static unsigned char load_superblock(void) {
    unsigned int csum_stored;
    unsigned int csum_calc;
    unsigned int v;

    if (open_events_session() != 0) return 1;
    if (rel_read_record_session(LFN_EVENTS, REC_SUPERBLOCK, rec_buf64, REC_EVENTS_LEN) != 0) {
        rel_session_close(LFN_EVENTS);
        return 1;
    }
    rel_session_close(LFN_EVENTS);

    if (rec_buf64[SB_MAGIC0] != 'c' || rec_buf64[SB_MAGIC1] != '2' ||
        rec_buf64[SB_MAGIC2] != '6' || rec_buf64[SB_MAGIC3] != 'e' ||
        rec_buf64[SB_VERSION] != '1') {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD SUPER");
        return 1;
    }

    csum_stored = rd_dec(rec_buf64, SB_CSUM_DEC, 5);
    csum_calc = sb_checksum(rec_buf64);
    if (csum_stored == 0xFFFF || csum_stored != csum_calc) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD CHKSUM");
        return 1;
    }

    v = rd_dec(rec_buf64, SB_FREE_DEC, 5);
    if (v == 0xFFFF) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD SUPER");
        return 1;
    }
    free_head = v;

    v = rd_dec(rec_buf64, SB_NEXT_DEC, 5);
    if (v == 0xFFFF) {
        rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD SUPER");
        return 1;
    }
    next_record = v;
    if (next_record < REC_EVENT_BASE) next_record = REC_EVENT_BASE;

    return 0;
}

static void build_day_index_record(unsigned int doy, unsigned char *buf) {
    rec_clear(buf, REC_EVENTS_LEN);
    buf[IDX_TYPE] = REC_TYPE_INDEX;
    wr_dec(buf, IDX_DAY_DEC, 3, doy);
    wr_dec(buf, IDX_HEAD_DEC, 5, day_head[doy - 1]);
    wr_dec(buf, IDX_TAIL_DEC, 5, day_tail[doy - 1]);
    wr_dec(buf, IDX_CNT_DEC, 5, day_count[doy - 1]);
}

static unsigned char write_day_index(unsigned int doy) {
    unsigned int recno;
    unsigned char ok;

    recno = REC_DAY_BASE + (doy - 1);
    build_day_index_record(doy, rec_buf64);

    if (open_events_session() != 0) return 1;
    ok = (rel_write_record_session(LFN_EVENTS, recno, rec_buf64, REC_EVENTS_LEN) == 0);
    rel_session_close(LFN_EVENTS);

    return ok ? 0 : 1;
}

static unsigned char load_day_index_table(void) {
    unsigned int doy;
    unsigned int recno;
    unsigned int v;

    if (open_events_session() != 0) return 1;

    for (doy = 1; doy <= CAL_DAYS; ++doy) {
        recno = REC_DAY_BASE + (doy - 1);
        if (rel_read_record_session(LFN_EVENTS, recno, rec_buf64, REC_EVENTS_LEN) != 0) {
            rel_session_close(LFN_EVENTS);
            return 1;
        }
        v = rd_dec(rec_buf64, IDX_DAY_DEC, 3);
        if (rec_buf64[IDX_TYPE] != REC_TYPE_INDEX || v == 0xFFFF || v != doy) {
            rel_set_error(REL_STEP_VERIFY, REL_ERR_VERIFY, "BAD INDEX");
            rel_session_close(LFN_EVENTS);
            return 1;
        }
        v = rd_dec(rec_buf64, IDX_HEAD_DEC, 5);
        day_head[doy - 1] = (v == 0xFFFF) ? 0 : v;
        v = rd_dec(rec_buf64, IDX_TAIL_DEC, 5);
        day_tail[doy - 1] = (v == 0xFFFF) ? 0 : v;
        v = rd_dec(rec_buf64, IDX_CNT_DEC, 5);
        day_count[doy - 1] = (v == 0xFFFF) ? 0 : v;
    }

    rel_session_close(LFN_EVENTS);
    return 0;
}

static void event_to_record(const EventRec *ev, unsigned char *buf) {
    rec_clear(buf, REC_EVENTS_LEN);
    buf[EVT_TYPE] = REC_TYPE_EVENT;
    buf[EVT_DONE] = (ev->flags & EVT_FLAG_DONE) ? '1' : '0';
    buf[EVT_DELETED] = (ev->flags & EVT_FLAG_DELETED) ? '1' : '0';
    wr_dec(buf, EVT_DAY_DEC, 3, ev->day);
    wr_dec(buf, EVT_PREV_DEC, 5, ev->prev);
    wr_dec(buf, EVT_NEXT_DEC, 5, ev->next);
    wr_dec(buf, EVT_LEN_DEC, 2, ev->len);
    if (ev->len > 0) {
        memcpy(&buf[EVT_TEXT], ev->text, ev->len);
    }
}

static void record_to_event(const unsigned char *buf, EventRec *ev) {
    unsigned int v;

    ev->flags = 0;
    if (buf[EVT_DONE] == '1') ev->flags |= EVT_FLAG_DONE;
    if (buf[EVT_DELETED] == '1') ev->flags |= EVT_FLAG_DELETED;

    v = rd_dec(buf, EVT_DAY_DEC, 3);
    ev->day = (v == 0xFFFF) ? 0 : v;
    v = rd_dec(buf, EVT_PREV_DEC, 5);
    ev->prev = (v == 0xFFFF) ? 0 : v;
    v = rd_dec(buf, EVT_NEXT_DEC, 5);
    ev->next = (v == 0xFFFF) ? 0 : v;
    v = rd_dec(buf, EVT_LEN_DEC, 2);
    if (v == 0xFFFF || v > MAX_EVENT_TEXT) v = MAX_EVENT_TEXT;
    ev->len = (unsigned char)v;
    if (ev->len > 0) memcpy(ev->text, &buf[EVT_TEXT], ev->len);
    ev->text[ev->len] = 0;
}

static unsigned char read_event(unsigned int recno, EventRec *ev) {
    if (open_events_session() != 0) return 1;
    if (rel_read_record_session(LFN_EVENTS, recno, rec_buf64, REC_EVENTS_LEN) != 0) {
        rel_session_close(LFN_EVENTS);
        return 1;
    }
    rel_session_close(LFN_EVENTS);

    if (rec_buf64[EVT_TYPE] != REC_TYPE_EVENT) return 1;
    record_to_event(rec_buf64, ev);
    return 0;
}

static unsigned char write_event(unsigned int recno, const EventRec *ev) {
    event_to_record(ev, rec_buf64);

    if (open_events_session() != 0) return 1;
    if (rel_write_record_session(LFN_EVENTS, recno, rec_buf64, REC_EVENTS_LEN) != 0) {
        rel_session_close(LFN_EVENTS);
        return 1;
    }
    rel_session_close(LFN_EVENTS);
    return 0;
}

static unsigned char init_events_file(void) {
    unsigned int doy;

    free_head = 0;
    next_record = REC_EVENT_BASE;

    if (open_events_session() != 0) return 1;

    build_superblock(rec_buf64);
    if (rel_write_record_session(LFN_EVENTS, REC_SUPERBLOCK, rec_buf64, REC_EVENTS_LEN) != 0) {
        rel_session_close(LFN_EVENTS);
        return 1;
    }

    for (doy = 1; doy <= CAL_DAYS; ++doy) {
        day_head[doy - 1] = 0;
        day_tail[doy - 1] = 0;
        day_count[doy - 1] = 0;

        build_day_index_record(doy, rec_buf64);
        if (rel_write_record_session(LFN_EVENTS,
                                     REC_DAY_BASE + (doy - 1),
                                     rec_buf64,
                                     REC_EVENTS_LEN) != 0) {
            rel_session_close(LFN_EVENTS);
            return 1;
        }
    }

    rel_session_close(LFN_EVENTS);
    return 0;
}

static unsigned char save_config(void) {
    begin_save_indicator();

    rec_clear(rec_buf32, REC_CFG_LEN);
    rec_buf32[CFG_MAGIC0] = 'c';
    rec_buf32[CFG_MAGIC1] = '2';
    rec_buf32[CFG_MAGIC2] = '6';
    rec_buf32[CFG_MAGIC3] = 'c';
    rec_buf32[CFG_VERSION] = '1';
    wr_dec(rec_buf32, CFG_TODAY_DEC, 3, today_doy);
    rec_buf32[CFG_WEEKSTART] = week_start_cfg ? '1' : '0';

    if (open_cfg_session() != 0) {
        end_save_indicator(0);
        return 1;
    }
    if (rel_write_record_session(LFN_CFG, 1, rec_buf32, REC_CFG_LEN) != 0) {
        rel_session_close(LFN_CFG);
        end_save_indicator(0);
        return 1;
    }
    rel_session_close(LFN_CFG);

    end_save_indicator(1);
    return 0;
}

static unsigned char init_or_load_config(void) {
    unsigned int v;

    if (open_cfg_session() != 0) {
        return 1;
    }

    if (rel_read_record_session(LFN_CFG, 1, rec_buf32, REC_CFG_LEN) != 0 ||
        rec_buf32[CFG_MAGIC0] != 'c' || rec_buf32[CFG_MAGIC1] != '2' ||
        rec_buf32[CFG_MAGIC2] != '6' || rec_buf32[CFG_MAGIC3] != 'c' ||
        rec_buf32[CFG_VERSION] != '1') {
        rel_session_close(LFN_CFG);

        scratch_cfg_variants();

        today_doy = parse_compile_today();
        week_start_cfg = 0; /* Sunday */
        return save_config();
    }

    v = rd_dec(rec_buf32, CFG_TODAY_DEC, 3);
    if (v == 0xFFFF) {
        rel_session_close(LFN_CFG);
        scratch_cfg_variants();
        today_doy = parse_compile_today();
        week_start_cfg = 0;
        return save_config();
    }
    today_doy = clamp_doy(v);
    week_start_cfg = (rec_buf32[CFG_WEEKSTART] == '1') ? 1 : 0;

    rel_session_close(LFN_CFG);
    return 0;
}

static unsigned char load_superblock_with_retry(void) {
    unsigned char attempt;
    for (attempt = 0; attempt < 2; ++attempt) {
        if (load_superblock() == 0) return 0;
        if (!rel_error_is_transient_io()) return 1;
    }
    return 1;
}

static unsigned char load_day_index_table_with_retry(void) {
    unsigned char attempt;
    for (attempt = 0; attempt < 2; ++attempt) {
        if (load_day_index_table() == 0) return 0;
        if (!rel_error_is_transient_io()) return 1;
    }
    return 1;
}

static unsigned char init_or_load_events(void) {
    if (load_superblock_with_retry() != 0) {
        if (!rel_error_is_initable()) return 1;
        if (init_events_file() != 0) return 1;
        if (load_superblock_with_retry() != 0) return 1;
    }

    if (load_day_index_table_with_retry() != 0) {
        if (!rel_error_is_initable()) return 1;
        if (init_events_file() != 0) return 1;
        if (load_superblock_with_retry() != 0) return 1;
        if (load_day_index_table_with_retry() != 0) return 1;
    }

    return 0;
}

static unsigned int alloc_event_record(void) {
    unsigned int rec;
    EventRec ev;

    if (free_head != 0) {
        rec = free_head;
        if (read_event(rec, &ev) != 0) {
            return 0;
        }
        free_head = ev.next;
    } else {
        rec = next_record;
        ++next_record;
        if (next_record == 0) {
            /* overflow */
            return 0;
        }
    }

    if (write_superblock() != 0) return 0;
    return rec;
}

static unsigned char free_event_record(unsigned int rec) {
    EventRec ev;

    memset(&ev, 0, sizeof(ev));
    ev.flags = EVT_FLAG_DELETED;
    ev.next = free_head;

    if (write_event(rec, &ev) != 0) return 1;

    free_head = rec;
    if (write_superblock() != 0) return 1;
    return 0;
}

static unsigned char event_record_in_range(unsigned int rec) {
    return (unsigned char)(rec >= REC_EVENT_BASE && rec < next_record);
}

static unsigned int active_event_traversal_budget(void) {
    if (next_record <= REC_EVENT_BASE) {
        return 0;
    }
    return (unsigned int)(next_record - REC_EVENT_BASE);
}

static unsigned char validate_active_event_link(unsigned int expected_doy,
                                                unsigned int rec,
                                                const EventRec *ev) {
    if (!event_record_in_range(rec)) {
        return 0;
    }
    if (ev->day != expected_doy) {
        return 0;
    }
    if ((ev->flags & EVT_FLAG_DELETED) != 0) {
        return 0;
    }
    if (ev->prev == rec || ev->next == rec) {
        return 0;
    }
    if (ev->prev != 0 && !event_record_in_range(ev->prev)) {
        return 0;
    }
    if (ev->next != 0 && !event_record_in_range(ev->next)) {
        return 0;
    }
    return 1;
}

static void format_day_item_line(char *out,
                                 unsigned char out_cap,
                                 unsigned char selected,
                                 unsigned char flags,
                                 const char *text) {
    unsigned char copy_len;

    if (out_cap <= DAY_ROW_PREFIX_LEN) {
        if (out_cap > 0) {
            out[0] = 0;
        }
        return;
    }

    out[0] = selected ? '>' : ' ';
    out[1] = ' ';
    out[2] = '[';
    out[3] = (flags & EVT_FLAG_DONE) ? 'X' : ' ';
    out[4] = ']';
    out[5] = ' ';
    out[6] = 0;

    copy_len = (unsigned char)(out_cap - 1 - DAY_ROW_PREFIX_LEN);
    strncat(out, text, copy_len);
}

/*---------------------------------------------------------------------------
 * Day cache and CRUD
 *---------------------------------------------------------------------------*/

static void clamp_day_selection(void) {
    if (day_item_count == 0) {
        day_sel = 0;
        day_scroll = 0;
        return;
    }

    if (day_sel >= day_item_count) day_sel = day_item_count - 1;

    if (day_sel < day_scroll) day_scroll = day_sel;
    if (day_sel >= day_scroll + LIST_H) day_scroll = day_sel - LIST_H + 1;
}

static unsigned char load_day_cache(unsigned int doy) {
    unsigned int rec;
    unsigned int traversal_budget;
    unsigned char count;
    EventRec ev;

    day_item_count = 0;
    day_sel = 0;
    day_scroll = 0;
    day_cache_capped = 0;

    rec = day_head[doy - 1];
    traversal_budget = active_event_traversal_budget();
    count = 0;

    while (rec != 0 && count < MAX_DAY_ITEMS) {
        if (!event_record_in_range(rec) || traversal_budget == 0) {
            set_status("BAD DAY CHAIN", TUI_COLOR_LIGHTRED);
            return 1;
        }
        --traversal_budget;

        if (read_event(rec, &ev) != 0) return 1;
        if (!validate_active_event_link(doy, rec, &ev)) {
            set_status("BAD DAY CHAIN", TUI_COLOR_LIGHTRED);
            return 1;
        }

        day_item_rec[count] = rec;
        day_item_flags[count] = ev.flags;
        strncpy(day_item_text[count], ev.text, MAX_EVENT_TEXT);
        day_item_text[count][MAX_EVENT_TEXT] = 0;

        ++count;
        rec = ev.next;
    }

    if (rec != 0) {
        day_cache_capped = 1;
    }

    day_item_count = count;
    clamp_day_selection();
    return 0;
}

static unsigned char update_day_index_on_disk(unsigned int doy) {
    return write_day_index(doy);
}

static unsigned char create_event_on_day(unsigned int doy,
                                         const char *text,
                                         unsigned char done) {
    unsigned int rec;
    EventRec ev;
    EventRec tail_ev;
    unsigned int tail_rec;

    rec = alloc_event_record();
    if (rec == 0) return 1;

    memset(&ev, 0, sizeof(ev));
    ev.flags = done ? EVT_FLAG_DONE : 0;
    ev.day = doy;
    ev.prev = day_tail[doy - 1];
    ev.next = 0;
    ev.len = (unsigned char)strlen(text);
    if (ev.len > MAX_EVENT_TEXT) ev.len = MAX_EVENT_TEXT;
    memcpy(ev.text, text, ev.len);
    ev.text[ev.len] = 0;

    if (write_event(rec, &ev) != 0) return 1;

    tail_rec = day_tail[doy - 1];
    if (tail_rec != 0) {
        if (read_event(tail_rec, &tail_ev) != 0) return 1;
        tail_ev.next = rec;
        if (write_event(tail_rec, &tail_ev) != 0) return 1;
    } else {
        day_head[doy - 1] = rec;
    }

    day_tail[doy - 1] = rec;
    day_count[doy - 1]++;

    if (update_day_index_on_disk(doy) != 0) return 1;
    invalidate_week_label_cache();

    return 0;
}

static unsigned char delete_event_on_day(unsigned int doy, unsigned char idx) {
    EventRec ev;
    EventRec other;
    unsigned int rec;

    if (idx >= day_item_count) return 1;

    rec = day_item_rec[idx];
    if (read_event(rec, &ev) != 0) return 1;

    if (ev.prev != 0) {
        if (read_event(ev.prev, &other) != 0) return 1;
        other.next = ev.next;
        if (write_event(ev.prev, &other) != 0) return 1;
    } else {
        day_head[doy - 1] = ev.next;
    }

    if (ev.next != 0) {
        if (read_event(ev.next, &other) != 0) return 1;
        other.prev = ev.prev;
        if (write_event(ev.next, &other) != 0) return 1;
    } else {
        day_tail[doy - 1] = ev.prev;
    }

    if (day_count[doy - 1] > 0) day_count[doy - 1]--;

    if (free_event_record(rec) != 0) return 1;
    if (update_day_index_on_disk(doy) != 0) return 1;

    if (load_day_cache(doy) != 0) return 1;
    invalidate_week_label_cache();
    return 0;
}

static unsigned char toggle_done_on_day(unsigned int doy, unsigned char idx) {
    EventRec ev;

    (void)doy;

    if (idx >= day_item_count) return 1;
    if (read_event(day_item_rec[idx], &ev) != 0) return 1;

    ev.flags ^= EVT_FLAG_DONE;

    if (write_event(day_item_rec[idx], &ev) != 0) return 1;
    day_item_flags[idx] = ev.flags;
    invalidate_week_label_cache();
    return 0;
}

static unsigned char edit_event_text_on_day(unsigned int doy, unsigned char idx, const char *text) {
    EventRec ev;

    (void)doy;

    if (idx >= day_item_count) return 1;
    if (read_event(day_item_rec[idx], &ev) != 0) return 1;

    ev.len = (unsigned char)strlen(text);
    if (ev.len > MAX_EVENT_TEXT) ev.len = MAX_EVENT_TEXT;
    memcpy(ev.text, text, ev.len);
    ev.text[ev.len] = 0;

    if (write_event(day_item_rec[idx], &ev) != 0) return 1;

    strncpy(day_item_text[idx], ev.text, MAX_EVENT_TEXT);
    day_item_text[idx][MAX_EVENT_TEXT] = 0;
    invalidate_week_label_cache();
    return 0;
}

static unsigned char swap_event_payload(unsigned int rec_a, unsigned int rec_b) {
    EventRec a;
    EventRec b;

    if (read_event(rec_a, &a) != 0) return 1;
    if (read_event(rec_b, &b) != 0) return 1;

    {
        unsigned char flags_tmp;
        unsigned char len_tmp;
        char txt_tmp[MAX_EVENT_TEXT + 1];

        flags_tmp = a.flags;
        len_tmp = a.len;
        strcpy(txt_tmp, a.text);

        a.flags = b.flags;
        a.len = b.len;
        strcpy(a.text, b.text);

        b.flags = flags_tmp;
        b.len = len_tmp;
        strcpy(b.text, txt_tmp);
    }

    if (write_event(rec_a, &a) != 0) return 1;
    if (write_event(rec_b, &b) != 0) return 1;

    return 0;
}

static unsigned char move_event_up(unsigned int doy, unsigned char idx) {
    if (idx == 0 || idx >= day_item_count) return 0;
    if (swap_event_payload(day_item_rec[idx - 1], day_item_rec[idx]) != 0) return 1;
    if (load_day_cache(doy) != 0) return 1;
    day_sel = idx - 1;
    clamp_day_selection();
    invalidate_week_label_cache();
    return 0;
}

static unsigned char move_event_down(unsigned int doy, unsigned char idx) {
    if (idx + 1 >= day_item_count) return 0;
    if (swap_event_payload(day_item_rec[idx], day_item_rec[idx + 1]) != 0) return 1;
    if (load_day_cache(doy) != 0) return 1;
    day_sel = idx + 1;
    clamp_day_selection();
    invalidate_week_label_cache();
    return 0;
}

/*---------------------------------------------------------------------------
 * Clipboard integration
 *---------------------------------------------------------------------------*/

static unsigned char copy_selected_to_clipboard(void) {
    unsigned char done;
    char date_iso[11];
    unsigned int len;

    if (day_item_count == 0 || day_sel >= day_item_count) return 1;

    done = (day_item_flags[day_sel] & EVT_FLAG_DONE) ? 1 : 0;
    format_iso(selected_doy, date_iso);

    strcpy(clip_buf, "RDYCAL1\nDATE=");
    strcat(clip_buf, date_iso);
    strcat(clip_buf, "\nDONE=");
    strcat(clip_buf, done ? "1" : "0");
    strcat(clip_buf, "\nTEXT=");
    strncat(clip_buf, day_item_text[day_sel], sizeof(clip_buf) - strlen(clip_buf) - 1);

    len = strlen(clip_buf);

    if (clip_copy(CLIP_TYPE_TEXT, clip_buf, len) != 0) {
        return 1;
    }

    return 0;
}

static unsigned char parse_clip_payload(unsigned char *out_done, char *out_text) {
    unsigned int got;
    char *p;
    char *line;
    char *next;
    unsigned char have_header = 0;
    unsigned char have_done = 0;
    unsigned char have_text = 0;

    if (clip_item_count() == 0) return 1;

    got = clip_paste(0, clip_buf, sizeof(clip_buf) - 1);
    if (got == 0) return 1;
    clip_buf[got] = 0;

    p = clip_buf;
    while (*p) {
        line = p;
        next = strchr(p, '\n');
        if (next) {
            *next = 0;
            p = next + 1;
        } else {
            p += strlen(p);
        }

        if (strcmp(line, "RDYCAL1") == 0) {
            have_header = 1;
        } else if (strncmp(line, "DONE=", 5) == 0) {
            *out_done = (line[5] == '1') ? 1 : 0;
            have_done = 1;
        } else if (strncmp(line, "TEXT=", 5) == 0) {
            strncpy(out_text, &line[5], MAX_EVENT_TEXT);
            out_text[MAX_EVENT_TEXT] = 0;
            have_text = 1;
        }
    }

    if (!have_header || !have_done || !have_text) return 1;
    return 0;
}

static unsigned char paste_from_clipboard_to_day(unsigned int doy) {
    unsigned char done = 0;
    char text[MAX_EVENT_TEXT + 1];

    if (parse_clip_payload(&done, text) != 0) return 1;
    if (create_event_on_day(doy, text, done) != 0) return 1;
    if (load_day_cache(doy) != 0) return 1;

    if (day_item_count > 0) {
        day_sel = day_item_count - 1;
        clamp_day_selection();
    }

    return 0;
}

/*---------------------------------------------------------------------------
 * Upcoming builder
 *---------------------------------------------------------------------------*/

static unsigned char build_upcoming_page(unsigned int page) {
    unsigned int skip;
    unsigned int doy;
    unsigned int rec;
    unsigned int traversal_budget;
    EventRec ev;

    upcoming_count = 0;
    upcoming_sel = 0;
    upcoming_page = page;
    upcoming_has_more = 0;

    skip = page * UPCOMING_PAGE_SIZE;
    traversal_budget = active_event_traversal_budget();

    for (doy = today_doy; doy <= CAL_DAYS; ++doy) {
        rec = day_head[doy - 1];
        while (rec != 0) {
            if (!event_record_in_range(rec) || traversal_budget == 0) {
                set_status("BAD UPCOMING CHAIN", TUI_COLOR_LIGHTRED);
                return 1;
            }
            --traversal_budget;

            if (read_event(rec, &ev) != 0) return 1;
            if (!validate_active_event_link(doy, rec, &ev)) {
                set_status("BAD UPCOMING CHAIN", TUI_COLOR_LIGHTRED);
                return 1;
            }

            if ((ev.flags & EVT_FLAG_DONE) == 0) {
                if (skip > 0) {
                    --skip;
                } else if (upcoming_count < UPCOMING_PAGE_SIZE) {
                    upcoming_items[upcoming_count].doy = doy;
                    upcoming_items[upcoming_count].recno = rec;
                    upcoming_items[upcoming_count].flags = ev.flags;
                    strncpy(upcoming_items[upcoming_count].text, ev.text, MAX_EVENT_TEXT);
                    upcoming_items[upcoming_count].text[MAX_EVENT_TEXT] = 0;
                    ++upcoming_count;
                } else {
                    upcoming_has_more = 1;
                    return 0;
                }
            }

            rec = ev.next;
        }
    }

    return 0;
}

/*---------------------------------------------------------------------------
 * UI popups
 *---------------------------------------------------------------------------*/

static unsigned char edit_text_popup(const char *title, char *buf, unsigned char maxlen) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    win.x = 3;
    win.y = 8;
    win.w = 34;
    win.h = 8;

    tui_window_title(&win, title, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(5, 11, "TEXT:", TUI_COLOR_WHITE);

    tui_input_init(&input, 10, 11, 24, maxlen, buf, TUI_COLOR_CYAN);
    input.cursor = strlen(buf);
    tui_input_draw(&input);

    tui_puts(5, 13, "RET:OK STOP:CANCEL", TUI_COLOR_GRAY3);

    while (1) {
        key = tui_getkey();

        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            return 0;
        }

        if (tui_input_key(&input, key)) {
            return 1;
        }

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

/*---------------------------------------------------------------------------
 * Draw routines
 *---------------------------------------------------------------------------*/

static void draw_common_header(const char *view_name) {
    char date[11];
    TuiRect box = {0, 0, 40, 3};

    tui_window_title(&box, "CALENDAR 26", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    format_month_day(selected_doy, date);
    tui_puts(2, 1, view_name, TUI_COLOR_CYAN);
    tui_puts(13, 1, date, TUI_COLOR_WHITE);

    format_month_day(today_doy, date);
    tui_puts(25, 1, "TODAY", TUI_COLOR_GRAY3);
    tui_puts(31, 1, date, TUI_COLOR_LIGHTGREEN);
}

static unsigned char format_week_task_label(unsigned int doy, char *out, unsigned char out_cap) {
    EventRec ev;
    unsigned int rec;
    unsigned char count;
    unsigned char idx;
    unsigned char copy_len;

    if (out_cap < 2 || doy < 1 || doy > CAL_DAYS) return 1;

    count = day_count[doy - 1];
    if (count == 0) return 1;

    rec = day_head[doy - 1];
    if (rec == 0) return 1;
    if (!event_record_in_range(rec)) return 1;
    if (read_event(rec, &ev) != 0) return 1;
    if (!validate_active_event_link(doy, rec, &ev)) return 1;

    out[0] = 0;
    idx = 0;

    if (count > 1) {
        out[idx++] = '1';
        out[idx++] = '/';
        if (count >= 100) {
            out[idx++] = (char)('0' + (count / 100));
            out[idx++] = (char)('0' + ((count / 10) % 10));
            out[idx++] = (char)('0' + (count % 10));
        } else if (count >= 10) {
            out[idx++] = (char)('0' + (count / 10));
            out[idx++] = (char)('0' + (count % 10));
        } else {
            out[idx++] = (char)('0' + count);
        }
        out[idx++] = ' ';
    }

    if (idx >= (unsigned char)(out_cap - 1)) {
        out[out_cap - 1] = 0;
        return 0;
    }

    copy_len = (unsigned char)(out_cap - 1 - idx);
    if (copy_len > 0) {
        strncpy(&out[idx], ev.text, copy_len);
        out[idx + copy_len] = 0;
    }

    return 0;
}

static void invalidate_week_label_cache(void) {
    week_label_valid = 0;
}

static unsigned int week_view_start_doy(unsigned int doy) {
    unsigned int start = doy - dow_from_doy(doy);
    if (start < 1) start = 1;
    return start;
}

static void draw_week_selected_date_field(void) {
    char date[11];
    format_month_day(selected_doy, date);
    tui_puts_n(13, 1, "      ", 6, TUI_COLOR_WHITE);
    tui_puts(13, 1, date, TUI_COLOR_WHITE);
}

static unsigned char month_for_doy(unsigned int doy) {
    unsigned char month;
    unsigned char day;
    month_day_from_doy(doy, &month, &day);
    return month;
}

static void draw_month_selected_date_field(void) {
    char date[11];
    format_month_day(selected_doy, date);
    tui_puts_n(13, 1, "      ", 6, TUI_COLOR_WHITE);
    tui_puts(13, 1, date, TUI_COLOR_WHITE);
}

static void draw_month_holiday_line(void) {
    if (has_holiday(selected_doy)) {
        tui_puts_n(2, 19, holiday_list(selected_doy), 38, TUI_COLOR_YELLOW);
    } else {
        tui_puts_n(2, 19, "", 38, TUI_COLOR_WHITE);
    }
}

static void month_cell_coords(unsigned int doy, unsigned char *x, unsigned char *y) {
    unsigned char month;
    unsigned char day;
    unsigned int first_doy;
    unsigned char first_dow;
    unsigned char slot;

    month_day_from_doy(doy, &month, &day);
    first_doy = doy_from_month_day(month, 1);
    first_dow = dow_from_doy(first_doy);
    slot = (unsigned char)(first_dow + (day - 1));

    *x = (unsigned char)(2 + (slot % 7) * 5);
    *y = (unsigned char)(6 + (slot / 7) * 2);
}

static void draw_month_cell_at(unsigned int doy, unsigned char x, unsigned char y, unsigned char selected) {
    unsigned char month;
    unsigned char day;
    char cell[5];
    const Holiday *h;

    month_day_from_doy(doy, &month, &day);

    cell[0] = (char)('0' + (day / 10));
    cell[1] = (char)('0' + (day % 10));
    if (day < 10) cell[0] = ' ';
    cell[2] = (day_count[doy - 1] > 0) ? '+' : ' ';
    h = first_holiday(doy);
    cell[3] = h ? '*' : ' ';
    cell[4] = 0;

    tui_puts_n(x, y, cell, 4, h ? h->color : TUI_COLOR_WHITE);

    if (doy == today_doy) {
        tui_putc((unsigned char)(x + 3), y, 'T', TUI_COLOR_LIGHTGREEN);
    }
    if (selected) {
        invert_line(y, x, 4);
    }
}

static void draw_month_cell_by_doy(unsigned int doy, unsigned char selected) {
    unsigned char x;
    unsigned char y;
    month_cell_coords(doy, &x, &y);
    draw_month_cell_at(doy, x, y, selected);
}

static void build_week_label_cache(unsigned int start) {
    unsigned char i;
    unsigned int doy;
    const Holiday *h;

    for (i = 0; i < 7; ++i) {
        doy = start + i;
        week_label_color[i] = TUI_COLOR_WHITE;
        week_label_text[i][0] = 0;
        if (doy > CAL_DAYS) continue;

        if (storage_loaded && format_week_task_label(doy, week_label_text[i], sizeof(week_label_text[i])) == 0) {
            continue;
        }

        h = first_holiday(doy);
        if (h) {
            strncpy(week_label_text[i], h->name, sizeof(week_label_text[i]) - 1);
            week_label_text[i][sizeof(week_label_text[i]) - 1] = 0;
            week_label_color[i] = h->color;
        } else {
            week_label_text[i][0] = '-';
            week_label_text[i][1] = 0;
        }
    }

    week_label_start_doy = start;
    week_label_valid = 1;
}

static void draw_week_row_from_cache(unsigned int start, unsigned char row, unsigned char selected) {
    unsigned int doy;
    unsigned char y;
    char mmdd[6];
    char line[40];

    y = (unsigned char)(5 + row * 2);
    doy = start + row;

    /* Clear one contiguous span so previous row content never leaves residue. */
    tui_clear_line(y, 1, 39, TUI_COLOR_WHITE);
    if (doy > CAL_DAYS) return;

    format_mmdd(doy, mmdd);
    strcpy(line, dow_names[row]);
    strcat(line, " ");
    strcat(line, mmdd);
    strcat(line, " ");
    strncat(line, week_label_text[row], sizeof(week_label_text[row]) - 1);

    tui_puts_n(2, y, line, 30, week_label_color[row]);
    tui_print_uint(35, y, day_count[doy - 1], TUI_COLOR_CYAN);

    if (selected) {
        invert_line(y, 1, 38);
    }
}

static void draw_month_view(void) {
    unsigned char month, day;
    unsigned int first_doy;
    unsigned char first_dow;
    unsigned char mdays;
    unsigned char d;
    unsigned int doy;
    unsigned char col, row;
    unsigned char x, y;

    tui_clear(TUI_COLOR_BLUE);
    draw_common_header("MONTH");

    month_day_from_doy(selected_doy, &month, &day);
    tui_puts(2, 3, month_names[month - 1], TUI_COLOR_YELLOW);

    tui_puts(2, 4, "SU   MO   TU   WE   TH   FR   SA", TUI_COLOR_GRAY3);

    first_doy = doy_from_month_day(month, 1);
    first_dow = dow_from_doy(first_doy);
    mdays = month_days[month - 1];

    d = 1;
    for (row = 0; row < 6; ++row) {
        y = (unsigned char)(6 + row * 2);
        for (col = 0; col < 7; ++col) {
            x = (unsigned char)(2 + col * 5);

            if (row == 0 && col < first_dow) {
                tui_puts_n(x, y, "    ", 4, TUI_COLOR_WHITE);
                continue;
            }
            if (d > mdays) {
                tui_puts_n(x, y, "    ", 4, TUI_COLOR_WHITE);
                continue;
            }

            doy = doy_from_month_day(month, d);
            draw_month_cell_at(doy, x, y, (unsigned char)(doy == selected_doy));

            ++d;
        }
    }

    draw_month_holiday_line();

    if (!storage_loaded && !suppress_load_prompt) {
        tui_puts_n(0, 22, "press l to load calandar items", 38, TUI_COLOR_LIGHTGREEN);
    }

    tui_puts(0, 23, "ARROWS:DAY ,/.:MONTH RET:DAY F7:NEXT F8:HELP", TUI_COLOR_GRAY3);
    draw_status_line();
}

static void draw_week_view(void) {
    unsigned int start;
    unsigned char i;
    unsigned int doy;

    tui_clear(TUI_COLOR_BLUE);
    draw_common_header("WEEK");

    start = week_view_start_doy(selected_doy);
    if (!week_label_valid || week_label_start_doy != start) {
        build_week_label_cache(start);
    }

    for (i = 0; i < 7; ++i) {
        doy = start + i;
        if (doy > CAL_DAYS) break;
        draw_week_row_from_cache(start, i, (unsigned char)(doy == selected_doy));
    }

    tui_puts(0, 23, "ARROWS:NAV RET:DAY F7:NEXT ,/.:WEEK F8:HELP", TUI_COLOR_GRAY3);
    draw_status_line();
}

static void draw_day_view(void) {
    unsigned char i;
    unsigned char row;
    char line[DAY_ROW_VISIBLE_WIDTH + 1];

    tui_clear(TUI_COLOR_BLUE);
    draw_common_header("DAY");

    if (has_holiday(selected_doy)) {
        tui_puts_n(2, 3, holiday_list(selected_doy), 38, TUI_COLOR_YELLOW);
    } else {
        tui_puts_n(2, 3, "(NO HOLIDAY)", 38, TUI_COLOR_GRAY3);
    }

    if (!storage_loaded) {
        tui_puts(2, 10, "CALENDAR NOT LOADED", TUI_COLOR_YELLOW);
        tui_puts(2, 12, "PRESS L TO LOAD OR R TO REBUILD", TUI_COLOR_GRAY3);
        tui_puts(0, 23, "L:LOAD R:REBLD F7:NEXT CTRL+B:HOME F2/F4:APPS", TUI_COLOR_GRAY3);
        draw_status_line();
        return;
    }

    for (row = 0; row < LIST_H; ++row) {
        i = (unsigned char)(day_scroll + row);
        tui_clear_line((unsigned char)(LIST_TOP + row), 0, 40, TUI_COLOR_WHITE);
        if (i >= day_item_count) continue;

        format_day_item_line(line, sizeof(line),
                             (unsigned char)(i == day_sel),
                             day_item_flags[i],
                             day_item_text[i]);

        tui_puts_n(0, (unsigned char)(LIST_TOP + row), line, 40,
                   (day_item_flags[i] & EVT_FLAG_DONE) ? TUI_COLOR_GRAY2 : TUI_COLOR_WHITE);

        if (i == day_sel) {
            invert_line((unsigned char)(LIST_TOP + row), 0, 40);
        }
    }

    if (day_item_count == 0) {
        tui_puts(2, 10, "NO APPOINTMENTS", TUI_COLOR_GRAY3);
    }
    tui_clear_line(21, 0, 40, TUI_COLOR_WHITE);
    if (day_cache_capped) {
        tui_puts(2, 21, "DAY LIST CAPPED (32)", TUI_COLOR_YELLOW);
    }

    tui_puts(0, 23, "N:NEW RET:EDIT DEL:DEL +/- MOVE F1/F3 CPY/PST F8:HELP", TUI_COLOR_GRAY3);
    draw_status_line();
}

static void draw_upcoming_view(void) {
    unsigned char i;
    char line[40];
    char mmdd[6];

    tui_clear(TUI_COLOR_BLUE);
    draw_common_header("UPCOMING");

    tui_puts(2, 3, "NEXT 6 NOT-DONE ITEMS", TUI_COLOR_YELLOW);
    tui_puts(29, 3, "PAGE", TUI_COLOR_GRAY3);
    tui_print_uint(34, 3, (unsigned int)(upcoming_page + 1), TUI_COLOR_CYAN);

    if (!storage_loaded) {
        tui_puts(2, 8, "CALENDAR NOT LOADED", TUI_COLOR_YELLOW);
        tui_puts(2, 10, "PRESS L TO LOAD OR R TO REBUILD", TUI_COLOR_GRAY3);
        tui_puts(0, 23, "L:LOAD R:REBLD F7:NEXT CTRL+B:HOME F2/F4:APPS", TUI_COLOR_GRAY3);
        draw_status_line();
        return;
    }

    for (i = 0; i < UPCOMING_PAGE_SIZE; ++i) {
        tui_clear_line((unsigned char)(5 + i), 0, 40, TUI_COLOR_WHITE);
        if (i >= upcoming_count) continue;

        format_mmdd(upcoming_items[i].doy, mmdd);

        line[0] = (i == upcoming_sel) ? '>' : ' ';
        line[1] = ' ';
        line[2] = mmdd[0];
        line[3] = mmdd[1];
        line[4] = '/';
        line[5] = mmdd[3];
        line[6] = mmdd[4];
        line[7] = ' ';
        line[8] = '[';
        line[9] = (upcoming_items[i].flags & EVT_FLAG_DONE) ? 'X' : ' ';
        line[10] = ']';
        line[11] = ' ';
        line[12] = 0;

        strncat(line, upcoming_items[i].text, 26);

        tui_puts_n(0, (unsigned char)(5 + i), line, 40, TUI_COLOR_WHITE);
        if (i == upcoming_sel) invert_line((unsigned char)(5 + i), 0, 40);
    }

    if (upcoming_count == 0) {
        tui_puts(2, 8, "NO UPCOMING ITEMS", TUI_COLOR_GRAY3);
    }

    tui_puts(0, 23, "UP/DN:SEL RET:JUMP ,/.:PAGE F7:NEXT F8:HELP", TUI_COLOR_GRAY3);
    draw_status_line();
}

static void redraw(void) {
    switch (current_view) {
        case VIEW_MONTH:    draw_month_view(); break;
        case VIEW_WEEK:     draw_week_view(); break;
        case VIEW_DAY:      draw_day_view(); break;
        default:            draw_upcoming_view(); break;
    }
}

/*---------------------------------------------------------------------------
 * Navigation helpers
 *---------------------------------------------------------------------------*/

static void move_selected_day(int delta) {
    int d = (int)selected_doy + delta;
    if (d < 1) d = 1;
    if (d > CAL_DAYS) d = CAL_DAYS;
    selected_doy = (unsigned int)d;
}

static void move_selected_month(int delta) {
    unsigned char m, d;
    int nm;

    month_day_from_doy(selected_doy, &m, &d);
    nm = (int)m + delta;
    if (nm < 1) nm = 1;
    if (nm > 12) nm = 12;

    if (d > month_days[nm - 1]) d = month_days[nm - 1];
    selected_doy = doy_from_month_day((unsigned char)nm, d);
}

static void cycle_view(void) {
    current_view = (unsigned char)((current_view + 1) & 0x03);

    if (current_view == VIEW_DAY) {
        if (storage_loaded) {
            load_day_cache(selected_doy);
        }
    } else if (current_view == VIEW_UPCOMING) {
        if (storage_loaded) {
            build_upcoming_page(0);
        }
    }
}

static void jump_to_day_view(unsigned int doy) {
    selected_doy = clamp_doy(doy);
    current_view = VIEW_DAY;
    if (storage_loaded) {
        load_day_cache(selected_doy);
    }
}

/*---------------------------------------------------------------------------
 * Input handlers
 *---------------------------------------------------------------------------*/

static void handle_global_keys(unsigned char key, unsigned char *handled) {
    unsigned char nav_action;

    *handled = 1;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        cal26_return_to_launcher();
        return;
    }
    if (nav_action >= 1 && nav_action <= 15) {
        resume_save_state();
        tui_switch_to_app(nav_action);
        return;
    }
    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return;
    }

    if (key == TUI_KEY_F7) {
        cycle_view();
        redraw();
        return;
    }

    if (key == 'l' || key == 'L') {
        suppress_load_prompt = 1;
        begin_load_indicator();
        load_calendar_storage(0);
        end_load_indicator();
        return;
    }

    if (key == 'r' || key == 'R') {
        if (!confirm_popup("REBUILD EVENTS?")) {
            redraw();
            return;
        }
        begin_load_indicator();
        rebuild_calendar_storage();
        end_load_indicator();
        return;
    }

    if (key == 't' || key == 'T') {
        today_doy = selected_doy;
        if (storage_loaded) {
            save_config();
        } else {
            set_status("TODAY SET (LOAD TO SAVE)", TUI_COLOR_YELLOW);
        }
        redraw();
        return;
    }

    if (key == TUI_KEY_F8) {
        show_help_screen();
        redraw();
        return;
    }

    *handled = 0;
}

static void handle_month_keys(unsigned char key) {
    unsigned int old_doy;
    unsigned char old_month;
    unsigned char new_month;

    old_doy = selected_doy;

    switch (key) {
        case TUI_KEY_LEFT:  move_selected_day(-1); break;
        case TUI_KEY_RIGHT: move_selected_day(1); break;
        case TUI_KEY_UP:    move_selected_day(-7); break;
        case TUI_KEY_DOWN:  move_selected_day(7); break;
        case ',':           move_selected_month(-1); break;
        case '.':           move_selected_month(1); break;
        case TUI_KEY_RETURN:
            jump_to_day_view(selected_doy);
            redraw();
            return;
        default:
            return;
    }

    if (selected_doy == old_doy) return;

    old_month = month_for_doy(old_doy);
    new_month = month_for_doy(selected_doy);
    if (new_month != old_month) {
        redraw();
        return;
    }

    draw_month_cell_by_doy(old_doy, 0);
    draw_month_cell_by_doy(selected_doy, 1);
    draw_month_selected_date_field();
    draw_month_holiday_line();
}

static void handle_week_keys(unsigned char key) {
    unsigned int old_doy;
    unsigned int old_start;
    unsigned int new_start;
    unsigned char old_row;
    unsigned char new_row;

    old_doy = selected_doy;
    old_start = week_view_start_doy(old_doy);

    switch (key) {
        case TUI_KEY_LEFT:  move_selected_day(-7); break;
        case TUI_KEY_RIGHT: move_selected_day(7); break;
        case TUI_KEY_UP:    move_selected_day(-1); break;
        case TUI_KEY_DOWN:  move_selected_day(1); break;
        case ',':           move_selected_day(-7); break;
        case '.':           move_selected_day(7); break;
        case TUI_KEY_RETURN:
            jump_to_day_view(selected_doy);
            redraw();
            return;
        default:
            return;
    }

    if (selected_doy == old_doy) return;

    new_start = week_view_start_doy(selected_doy);
    if (new_start == old_start) {
        if (!week_label_valid || week_label_start_doy != old_start) {
            build_week_label_cache(old_start);
        }
        old_row = (unsigned char)(old_doy - old_start);
        new_row = (unsigned char)(selected_doy - old_start);
        draw_week_row_from_cache(old_start, old_row, 0);
        draw_week_row_from_cache(old_start, new_row, 1);
        draw_week_selected_date_field();
        return;
    }

    redraw();
}

static void handle_day_keys(unsigned char key) {
    unsigned char ok;

    if (!storage_loaded) {
        set_status("PRESS L LOAD OR R REBUILD", TUI_COLOR_YELLOW);
        redraw();
        return;
    }

    switch (key) {
        case TUI_KEY_LEFT:
            move_selected_day(-1);
            load_day_cache(selected_doy);
            redraw();
            return;

        case TUI_KEY_RIGHT:
            move_selected_day(1);
            load_day_cache(selected_doy);
            redraw();
            return;

        case TUI_KEY_UP:
            if (day_sel > 0) {
                --day_sel;
                clamp_day_selection();
                redraw();
            }
            return;

        case TUI_KEY_DOWN:
            if (day_sel + 1 < day_item_count) {
                ++day_sel;
                clamp_day_selection();
                redraw();
            }
            return;

        case 'n':
        case 'N':
            text_buf[0] = 0;
            if (edit_text_popup("NEW APPOINTMENT", text_buf, MAX_EVENT_TEXT)) {
                begin_save_indicator();
                ok = (create_event_on_day(selected_doy, text_buf, 0) == 0);
                if (ok) ok = (load_day_cache(selected_doy) == 0);
                end_save_indicator(ok);
            }
            redraw();
            return;

        case TUI_KEY_RETURN:
            if (day_item_count == 0 || day_sel >= day_item_count) return;
            strcpy(text_buf, day_item_text[day_sel]);
            if (edit_text_popup("EDIT APPOINTMENT", text_buf, MAX_EVENT_TEXT)) {
                begin_save_indicator();
                ok = (edit_event_text_on_day(selected_doy, day_sel, text_buf) == 0);
                end_save_indicator(ok);
            }
            redraw();
            return;

        case TUI_KEY_DEL:
            if (day_item_count == 0 || day_sel >= day_item_count) return;
            if (!confirm_popup("DELETE ITEM?")) {
                redraw();
                return;
            }
            begin_save_indicator();
            ok = (delete_event_on_day(selected_doy, day_sel) == 0);
            end_save_indicator(ok);
            redraw();
            return;

        case ' ':
            if (day_item_count == 0 || day_sel >= day_item_count) return;
            begin_save_indicator();
            ok = (toggle_done_on_day(selected_doy, day_sel) == 0);
            end_save_indicator(ok);
            redraw();
            return;

        case '+':
            if (day_item_count == 0 || day_sel >= day_item_count) return;
            begin_save_indicator();
            ok = (move_event_down(selected_doy, day_sel) == 0);
            end_save_indicator(ok);
            redraw();
            return;

        case '-':
            if (day_item_count == 0 || day_sel >= day_item_count) return;
            begin_save_indicator();
            ok = (move_event_up(selected_doy, day_sel) == 0);
            end_save_indicator(ok);
            redraw();
            return;

        case TUI_KEY_F1:
            if (copy_selected_to_clipboard() == 0) {
                set_status("COPIED", TUI_COLOR_LIGHTGREEN);
            } else {
                set_status("COPY FAILED", TUI_COLOR_LIGHTRED);
            }
            redraw();
            return;

        case 'x':
        case 'X':
            if (day_item_count == 0 || day_sel >= day_item_count) return;
            if (copy_selected_to_clipboard() != 0) {
                set_status("CUT FAILED (COPY)", TUI_COLOR_LIGHTRED);
                redraw();
                return;
            }
            begin_save_indicator();
            ok = (delete_event_on_day(selected_doy, day_sel) == 0);
            end_save_indicator(ok);
            redraw();
            return;

        case TUI_KEY_F3:
            begin_save_indicator();
            ok = (paste_from_clipboard_to_day(selected_doy) == 0);
            end_save_indicator(ok);
            redraw();
            return;

        default:
            return;
    }
}

static void handle_upcoming_keys(unsigned char key) {
    if (!storage_loaded) {
        set_status("PRESS L LOAD OR R REBUILD", TUI_COLOR_YELLOW);
        redraw();
        return;
    }

    switch (key) {
        case TUI_KEY_UP:
            if (upcoming_sel > 0) {
                --upcoming_sel;
                redraw();
            }
            return;

        case TUI_KEY_DOWN:
            if (upcoming_sel + 1 < upcoming_count) {
                ++upcoming_sel;
                redraw();
            }
            return;

        case ',':
            if (upcoming_page > 0) {
                if (build_upcoming_page(upcoming_page - 1) == 0) {
                    redraw();
                }
            }
            return;

        case '.':
            if (upcoming_has_more) {
                if (build_upcoming_page(upcoming_page + 1) == 0) {
                    redraw();
                }
            }
            return;

        case TUI_KEY_RETURN:
            if (upcoming_count == 0 || upcoming_sel >= upcoming_count) return;
            jump_to_day_view(upcoming_items[upcoming_sel].doy);

            /* Try to select matching record in day list */
            {
                unsigned char i;
                for (i = 0; i < day_item_count; ++i) {
                    if (day_item_rec[i] == upcoming_items[upcoming_sel].recno) {
                        day_sel = i;
                        clamp_day_selection();
                        break;
                    }
                }
            }
            redraw();
            return;

        default:
            return;
    }
}

/*---------------------------------------------------------------------------
 * Init / main
 *---------------------------------------------------------------------------*/

static void clear_calendar_data(void) {
    invalidate_week_label_cache();

    memset(day_head, 0, sizeof(day_head));
    memset(day_tail, 0, sizeof(day_tail));
    memset(day_count, 0, sizeof(day_count));

    day_item_count = 0;
    day_sel = 0;
    day_scroll = 0;
    day_cache_capped = 0;

    upcoming_count = 0;
    upcoming_sel = 0;
    upcoming_page = 0;
    upcoming_has_more = 0;

    free_head = 0;
    next_record = REC_EVENT_BASE;
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    (void)resume_save_segments(cal26_resume_write_segments, CAL26_RESUME_SEG_COUNT);
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_load_segments(cal26_resume_read_segments, CAL26_RESUME_SEG_COUNT, &payload_len)) {
        return 0;
    }
    if (current_view > VIEW_UPCOMING) {
        return 0;
    }
    if (selected_doy < 1 || selected_doy > CAL_DAYS) {
        return 0;
    }
    if (today_doy < 1 || today_doy > CAL_DAYS) {
        return 0;
    }
    if (week_start_cfg > 1) {
        return 0;
    }
    if (day_item_count > MAX_DAY_ITEMS) {
        return 0;
    }
    if (upcoming_count > UPCOMING_PAGE_SIZE) {
        return 0;
    }

    storage_loaded = storage_loaded ? 1 : 0;
    suppress_load_prompt = suppress_load_prompt ? 1 : 0;
    day_cache_capped = day_cache_capped ? 1 : 0;
    upcoming_has_more = upcoming_has_more ? 1 : 0;
    week_label_valid = week_label_valid ? 1 : 0;

    if (day_item_count == 0) {
        day_sel = 0;
        day_scroll = 0;
    } else {
        if (day_sel >= day_item_count) {
            day_sel = (unsigned char)(day_item_count - 1);
        }
        if (day_scroll > day_sel) {
            day_scroll = day_sel;
        }
    }

    if (upcoming_count == 0) {
        upcoming_sel = 0;
    } else if (upcoming_sel >= upcoming_count) {
        upcoming_sel = (unsigned char)(upcoming_count - 1);
    }

    if (week_label_start_doy < 1 || week_label_start_doy > CAL_DAYS) {
        week_label_valid = 0;
    }
    return 1;
}

static void cal26_return_to_launcher(void) {
    resume_save_state();
    tui_return_to_launcher();
}

static unsigned char load_calendar_storage(unsigned char preserve_trace) {
    rel_clear_error();
    invalidate_week_label_cache();
    (void)preserve_trace;

    if (init_or_load_events() != 0) {
        storage_loaded = 0;
        clear_calendar_data();
        format_rel_error_status("LOAD EV ");
        return 1;
    }

    if (init_or_load_config() != 0) {
        storage_loaded = 0;
        clear_calendar_data();
        format_rel_error_status("LOAD CFG ");
        return 1;
    }

    storage_loaded = 1;

    if (load_day_cache(selected_doy) != 0) {
        if (status_msg[0] == 0) {
            set_status("LOAD WARN: DAY CACHE", TUI_COLOR_LIGHTRED);
        }
        return 1;
    }
    if (build_upcoming_page(0) != 0) {
        if (status_msg[0] == 0) {
            set_status("LOAD WARN: UPCOMING", TUI_COLOR_LIGHTRED);
        }
        return 1;
    }

    return 0;
}

static unsigned char rebuild_calendar_storage(void) {
    rel_clear_error();
    clear_calendar_data();

    if (init_events_file() != 0) {
        storage_loaded = 0;
        format_rel_error_status("REBLD EV ");
        return 1;
    }

    storage_loaded = 0;
    return load_calendar_storage(1);
}

static unsigned char cal26_init(void) {
    reu_mgr_init();
    tui_init();

    current_view = VIEW_MONTH;
    today_doy = parse_compile_today();
    selected_doy = today_doy;
    week_start_cfg = 0;
    storage_loaded = 0;
    suppress_load_prompt = 0;
    day_cache_capped = 0;
    rel_clear_error();
    clear_calendar_data();
    status_msg[0] = 0;
    status_color = TUI_COLOR_GRAY3;

    running = 1;
    set_status("", TUI_COLOR_GRAY3);

    return 0;
}

static void cal26_loop(void) {
    unsigned char key;
    unsigned char handled;

    redraw();

    while (running) {
        key = tui_getkey();

        handle_global_keys(key, &handled);
        if (handled) continue;

        if (key == TUI_KEY_RUNSTOP) {
            running = 0;
            break;
        }

        switch (current_view) {
            case VIEW_MONTH:    handle_month_keys(key); break;
            case VIEW_WEEK:     handle_week_keys(key); break;
            case VIEW_DAY:      handle_day_keys(key); break;
            default:            handle_upcoming_keys(key); break;
        }
    }

    __asm__("jmp $FCE2");
}

int main(void) {
    unsigned char bank;

    if (cal26_init() != 0) {
        redraw();
        while (1) {
            unsigned char key = tui_getkey();
            unsigned char nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);

            if (key == TUI_KEY_LARROW || nav_action == TUI_HOTKEY_LAUNCHER) {
                cal26_return_to_launcher();
            } else if (nav_action >= 1 && nav_action <= 15) {
                resume_save_state();
                tui_switch_to_app(nav_action);
            } else if (nav_action == TUI_HOTKEY_BIND_ONLY) {
                /* consumed */
            } else if (key == TUI_KEY_RUNSTOP) {
                __asm__("jmp $FCE2");
            }
        }
    }

    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();

    cal26_loop();
    return 0;
}
