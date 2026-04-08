/*
 * calcplus.c - Ready OS Calculator Plus
 * Keyboard-first expression calculator with history navigation
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include <c64.h>
#include <conio.h>
#include <string.h>

#include "../../lib/clipboard.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define TITLE_Y            0
#define HISTORY_Y          3
#define HISTORY_MAX        10
#define STATUS_Y           22
#define HELP_Y             23

#define EDITOR_BOX_X       3
#define EDITOR_BOX_Y       (HISTORY_Y + HISTORY_MAX)
#define EDITOR_BOX_W       36
#define EDITOR_BOX_H       3
#define EDITOR_ROW         (EDITOR_BOX_Y + 1)
#define EDITOR_INNER_W     (EDITOR_BOX_W - 2)

#define EQUALS_COL         29
#define EXPR_MAX_LEN       48
#define EVAL_STACK_MAX     32
#define CLIP_BUF_LEN       80
#define BLINK_TICKS        28

#define MODE_EDITOR        0
#define MODE_HISTORY       1

#define CALC_MODE_INT      0
#define CALC_MODE_FIXED    1
#define CALC_MODE_FLOAT    2

#define ANGLE_RAD          0
#define ANGLE_DEG          1

#define ERR_NONE           0
#define ERR_SYNTAX         1
#define ERR_DIV_ZERO       2
#define ERR_OVERFLOW       3
#define ERR_BAD_FUNC       4
#define ERR_FUNC_MODE      5
#define ERR_DOMAIN         6
#define ERR_BAD_VAR        7

#define INT16_MIN_L        (-32767L - 1L)
#define INT16_MAX_L        32767L
#define FP_SCALE           100L
#define FP_MAX_ABS         3276799L
#define TF_STR_LEN         22
#define ROMFP_IN_LEN       32
#define ROMFP_TEXT_LEN     32
#define IDENT_MAX_LEN      10
#define VAR_MAX            10
#define VAR_NAME_MAX       10
#define ROMFP_IN_ADDR      0xC580U
#define ROMFP_A_ADDR       0xC5A0U
#define ROMFP_B_ADDR       0xC5A5U
#define ROMFP_OUT_ADDR     0xC5AAU
#define ROMFP_TEXT_ADDR    0xC5AFU
#define C64_UPARROW_KEY    30

#define REDRAW_NONE        0
#define REDRAW_EDITOR      1
#define REDRAW_STATUS      2
#define REDRAW_HISTORY     4
#define REDRAW_HISTORY_SEL 8
#define REDRAW_MODE        16

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

#define FN_NONE            0
#define FN_ABS             1
#define FN_SGN             2
#define FN_INT             3
#define FN_SQR             4
#define FN_EXP             5
#define FN_LOG             6
#define FN_SIN             7
#define FN_COS             8
#define FN_TAN             9
#define FN_ATN             10
#define FN_RND             11

/*---------------------------------------------------------------------------
 * Types
 *---------------------------------------------------------------------------*/

typedef struct {
    char expr[EXPR_MAX_LEN + 1];
    unsigned char result_mode;
    signed int result_i;
    signed long result_fp;
    char result_tf[TF_STR_LEN];
    unsigned char is_carry;
} HistoryEntry;

typedef struct {
    unsigned char used;
    char name[VAR_NAME_MAX + 1];
    unsigned char value_tf[5];
} CalcVariable;

/*---------------------------------------------------------------------------
 * Static state
 *---------------------------------------------------------------------------*/

static HistoryEntry history[HISTORY_MAX];
static unsigned char history_count;
static CalcVariable variables[VAR_MAX];

static char editor_buf[EXPR_MAX_LEN + 1];
static unsigned char editor_len;
static unsigned char editor_cursor;

static unsigned char focus_mode;
static unsigned char selected_hist;
static unsigned char calc_mode;
static unsigned char float_angle_mode;

static unsigned char have_last_result;
static signed int last_result_i;
static signed long last_result_fp;
static char last_result_tf[TF_STR_LEN];

static char status_msg[40];
static unsigned char status_color;

static unsigned char cursor_visible;
static unsigned char blink_counter;
static unsigned char running;
static unsigned char resume_ready;

/* Evaluator work buffers */
static unsigned char eval_values_tf[EVAL_STACK_MAX][5];
static unsigned char eval_ops[EVAL_STACK_MAX];

static ResumeWriteSegment calcplus_resume_write_segments[] = {
    { history, sizeof(history) },
    { &history_count, sizeof(history_count) },
    { variables, sizeof(variables) },
    { editor_buf, sizeof(editor_buf) },
    { &editor_len, sizeof(editor_len) },
    { &editor_cursor, sizeof(editor_cursor) },
    { &focus_mode, sizeof(focus_mode) },
    { &selected_hist, sizeof(selected_hist) },
    { &calc_mode, sizeof(calc_mode) },
    { &float_angle_mode, sizeof(float_angle_mode) },
    { &have_last_result, sizeof(have_last_result) },
    { &last_result_i, sizeof(last_result_i) },
    { &last_result_fp, sizeof(last_result_fp) },
    { last_result_tf, sizeof(last_result_tf) },
};

static ResumeReadSegment calcplus_resume_read_segments[] = {
    { history, sizeof(history) },
    { &history_count, sizeof(history_count) },
    { variables, sizeof(variables) },
    { editor_buf, sizeof(editor_buf) },
    { &editor_len, sizeof(editor_len) },
    { &editor_cursor, sizeof(editor_cursor) },
    { &focus_mode, sizeof(focus_mode) },
    { &selected_hist, sizeof(selected_hist) },
    { &calc_mode, sizeof(calc_mode) },
    { &float_angle_mode, sizeof(float_angle_mode) },
    { &have_last_result, sizeof(have_last_result) },
    { &last_result_i, sizeof(last_result_i) },
    { &last_result_fp, sizeof(last_result_fp) },
    { last_result_tf, sizeof(last_result_tf) },
};

#define CALCPLUS_RESUME_SEG_COUNT \
    ((unsigned char)(sizeof(calcplus_resume_read_segments) / sizeof(calcplus_resume_read_segments[0])))

/* ROM float bridge buffers in always-visible RAM (must stay below $C600). */
static unsigned char* const romfp_in = (unsigned char*)ROMFP_IN_ADDR;
static unsigned char* const romfp_a = (unsigned char*)ROMFP_A_ADDR;
static unsigned char* const romfp_b = (unsigned char*)ROMFP_B_ADDR;
static unsigned char* const romfp_out = (unsigned char*)ROMFP_OUT_ADDR;
static unsigned char* const romfp_text = (unsigned char*)ROMFP_TEXT_ADDR;

extern void romfp_eval_literal(void);
extern void romfp_add(void);
extern void romfp_sub(void);
extern void romfp_mul(void);
extern void romfp_div(void);
extern void romfp_to_str(void);
extern void romfp_set_pi(void);
extern void romfp_abs(void);
extern void romfp_sgn(void);
extern void romfp_int(void);
extern void romfp_sqr(void);
extern void romfp_exp(void);
extern void romfp_log(void);
extern void romfp_sin(void);
extern void romfp_cos(void);
extern void romfp_tan(void);
extern void romfp_atn(void);
extern void romfp_rnd(void);

/* Forward declarations for cross-referenced render helpers */
static void draw_static_frame(void);
static void draw_full_dynamic(void);
static void trim_span(const char *src, unsigned char *start, unsigned char *end);
static void tf_from_literal(const char *lit, unsigned char *out_val);
static unsigned char eval_expression_tf(const char *expr, unsigned char *result, unsigned char *err);
static void resume_save_state(void);
static unsigned char resume_restore_state(void);

/*---------------------------------------------------------------------------
 * Helpers
 *---------------------------------------------------------------------------*/

static unsigned char is_digit(unsigned char ch) {
    return (ch >= '0' && ch <= '9') ? 1 : 0;
}

static unsigned char is_alpha(unsigned char ch) {
    return ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) ? 1 : 0;
}

static unsigned char ascii_upper(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (unsigned char)(ch - ('a' - 'A'));
    }
    return ch;
}

static unsigned char is_operator(unsigned char ch) {
    return (ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^') ? 1 : 0;
}

static unsigned char is_ident_tail(unsigned char ch) {
    return (is_alpha(ch) || is_digit(ch) || ch == '_') ? 1 : 0;
}

static unsigned char normalize_expr_input_char(unsigned char ch) {
    if (ch == C64_UPARROW_KEY) return '^';
    return ch;
}

static unsigned char expr_ascii_to_screen(unsigned char ch) {
    if (ch == '^') return 30; /* PETSCII up-arrow screen code */
    return tui_ascii_to_screen(ch);
}

static unsigned char is_expr_char(unsigned char ch, unsigned char allow_dot) {
    if (is_digit(ch) || is_operator(ch) || is_alpha(ch)) return 1;
    if (ch == '(' || ch == ')' || ch == ' ') return 1;
    if (ch == '$' || ch == '_') return 1;
    if (ch == '=') return 1;
    if (ch == ';') return 1; /* One-shot "fresh expression" prefix */
    if (allow_dot && ch == '.') return 1;
    return 0;
}

static void set_status(const char *msg, unsigned char color) {
    strncpy(status_msg, msg, sizeof(status_msg) - 1);
    status_msg[sizeof(status_msg) - 1] = 0;
    status_color = color;
}

static void set_eval_error_status(unsigned char err) {
    if (err == ERR_DIV_ZERO) {
        set_status("ERROR: DIVIDE BY ZERO", TUI_COLOR_LIGHTRED);
    } else if (err == ERR_OVERFLOW) {
        set_status("ERROR: OVERFLOW", TUI_COLOR_LIGHTRED);
    } else if (err == ERR_FUNC_MODE) {
        set_status("ERROR: FUNC NOT IN MODE", TUI_COLOR_LIGHTRED);
    } else if (err == ERR_BAD_FUNC) {
        set_status("ERROR: UNKNOWN FUNCTION", TUI_COLOR_LIGHTRED);
    } else if (err == ERR_DOMAIN) {
        set_status("ERROR: ILLEGAL QUANTITY", TUI_COLOR_LIGHTRED);
    } else if (err == ERR_BAD_VAR) {
        set_status("ERROR: UNKNOWN VAR", TUI_COLOR_LIGHTRED);
    } else {
        set_status("ERROR: BAD SYNTAX", TUI_COLOR_LIGHTRED);
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
        unsigned char o = 0;
        signed char i;
        if (value < 0) {
            out[o++] = '-';
        }
        for (i = (signed char)pos - 1; i >= 0; --i) {
            out[o++] = rev[(unsigned char)i];
        }
        out[o] = 0;
    }
}

static void fp_to_str(signed long value_fp, char *out) {
    signed long abs_fp;
    signed int int_part;
    unsigned int frac_part;
    unsigned char pos;
    unsigned char d1;
    unsigned char d2;

    if (value_fp < 0) {
        out[0] = '-';
        out[1] = 0;
        abs_fp = -value_fp;
        out = &out[1];
    } else {
        abs_fp = value_fp;
    }

    int_part = (signed int)(abs_fp / FP_SCALE);
    frac_part = (unsigned int)(abs_fp % FP_SCALE);

    int_to_str(int_part, out);
    pos = (unsigned char)strlen(out);

    if (frac_part == 0) {
        return;
    }

    d1 = (unsigned char)(frac_part / 10U);
    d2 = (unsigned char)(frac_part % 10U);

    out[pos++] = '.';
    out[pos++] = (char)('0' + d1);
    if (d2 != 0) {
        out[pos++] = (char)('0' + d2);
    }
    out[pos] = 0;
}

static unsigned char mode_allows_dot(unsigned char mode) {
    return (mode == CALC_MODE_INT) ? 0 : 1;
}

static void str_copy_fit(char *dst, const char *src, unsigned char dst_len) {
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = 0;
}

static void trim_text(char *txt) {
    unsigned char s;
    unsigned char e;
    unsigned char i;

    trim_span(txt, &s, &e);
    if (s == 0 && txt[e] == 0) {
        return;
    }
    if (s >= e) {
        txt[0] = 0;
        return;
    }
    for (i = 0; (unsigned char)(s + i) < e; ++i) {
        txt[i] = txt[s + i];
    }
    txt[i] = 0;
}

static void tf_copy(unsigned char *dst, const unsigned char *src) {
    unsigned char i;
    for (i = 0; i < 5; ++i) {
        dst[i] = src[i];
    }
}

static unsigned char tf_is_zero(const unsigned char *v) {
    return (v[0] == 0) ? 1 : 0;
}

static unsigned char tf_is_negative(const unsigned char *v) {
    if (v[0] == 0) return 0;
    return (v[1] & 0x80U) ? 1 : 0;
}

static unsigned char tf_is_positive(const unsigned char *v) {
    if (v[0] == 0) return 0;
    return (v[1] & 0x80U) ? 0 : 1;
}

static void tf_packed_to_str(const unsigned char *packed, char *out) {
    tf_copy(romfp_a, packed);
    romfp_to_str();
    str_copy_fit(out, (const char*)romfp_text, TF_STR_LEN);
    trim_text(out);
    if (out[0] == 0) {
        out[0] = '0';
        out[1] = 0;
    }
}

static void tf_approx_to_fixed(const char *src, signed long *out_fp, signed int *out_i) {
    unsigned char i;
    signed long sig;
    signed int exp10;
    signed int frac_digits;
    signed int eff;
    signed char sign;
    unsigned char seen_dot;
    unsigned char have_digit;

    i = 0;
    sign = 1;
    sig = 0;
    exp10 = 0;
    frac_digits = 0;
    seen_dot = 0;
    have_digit = 0;

    while (src[i] == ' ') ++i;
    if (src[i] == '+') {
        ++i;
    } else if (src[i] == '-') {
        sign = -1;
        ++i;
    }

    while (src[i]) {
        unsigned char ch = (unsigned char)src[i];
        if (is_digit(ch)) {
            have_digit = 1;
            if (sig <= 99999999L) {
                sig = sig * 10 + (ch - '0');
                if (seen_dot) ++frac_digits;
            }
            ++i;
            continue;
        }
        if (ch == '.' && !seen_dot) {
            seen_dot = 1;
            ++i;
            continue;
        }
        break;
    }

    if (src[i] == 'E' || src[i] == 'e') {
        signed char esign;
        unsigned int eval;

        ++i;
        esign = 1;
        eval = 0;
        if (src[i] == '+') {
            ++i;
        } else if (src[i] == '-') {
            esign = -1;
            ++i;
        }
        while (is_digit((unsigned char)src[i])) {
            if (eval < 100U) {
                eval = (unsigned int)(eval * 10U + (unsigned int)(src[i] - '0'));
            }
            ++i;
        }
        exp10 = (signed int)eval;
        if (esign < 0) exp10 = (signed int)-exp10;
    }

    if (!have_digit) {
        *out_fp = 0;
        *out_i = 0;
        return;
    }

    eff = (signed int)(exp10 - frac_digits + 2);
    if (eff >= 0) {
        while (eff > 0) {
            if (sig > (214748364L - 1L)) {
                sig = 2147483647L;
                break;
            }
            sig *= 10;
            --eff;
        }
    } else {
        while (eff < 0) {
            if (sig >= 0) {
                sig = (sig + 5) / 10;
            } else {
                sig = (sig - 5) / 10;
            }
            ++eff;
        }
    }

    if (sign < 0) sig = -sig;

    if (sig > FP_MAX_ABS) sig = FP_MAX_ABS;
    if (sig < -FP_MAX_ABS) sig = -FP_MAX_ABS;

    *out_fp = sig;
    *out_i = (signed int)(sig / FP_SCALE);
}

static unsigned char parse_identifier(const char *expr, unsigned char *idx,
                                      char *out, unsigned char out_len) {
    unsigned char i;
    unsigned char n;

    i = *idx;
    n = 0;

    if (!is_alpha((unsigned char)expr[i])) {
        return 0;
    }

    while (is_ident_tail((unsigned char)expr[i])) {
        if (n >= (unsigned char)(out_len - 1)) {
            return 0;
        }
        out[n++] = (char)ascii_upper((unsigned char)expr[i]);
        ++i;
    }
    out[n] = 0;
    *idx = i;
    return 1;
}

static unsigned char function_from_name(const char *name) {
    if (strcmp(name, "ABS") == 0) return FN_ABS;
    if (strcmp(name, "SGN") == 0) return FN_SGN;
    if (strcmp(name, "INT") == 0) return FN_INT;
    if (strcmp(name, "SQR") == 0) return FN_SQR;
    if (strcmp(name, "EXP") == 0) return FN_EXP;
    if (strcmp(name, "LOG") == 0) return FN_LOG;
    if (strcmp(name, "SIN") == 0) return FN_SIN;
    if (strcmp(name, "COS") == 0) return FN_COS;
    if (strcmp(name, "TAN") == 0) return FN_TAN;
    if (strcmp(name, "ATN") == 0) return FN_ATN;
    if (strcmp(name, "RND") == 0) return FN_RND;
    return FN_NONE;
}

static unsigned char parse_var_name(const char *expr, unsigned char *idx,
                                    char *out, unsigned char out_len,
                                    unsigned char require_dollar) {
    unsigned char i;

    i = *idx;
    if (expr[i] == '$') {
        ++i;
    } else if (require_dollar) {
        return 0;
    }

    if (!parse_identifier(expr, &i, out, out_len)) {
        return 0;
    }

    *idx = i;
    return 1;
}

static signed char variable_index(const char *name) {
    unsigned char i;
    for (i = 0; i < VAR_MAX; ++i) {
        if (variables[i].used && strcmp(variables[i].name, name) == 0) {
            return (signed char)i;
        }
    }
    return -1;
}

static unsigned char variable_name_reserved(const char *name) {
    if (strcmp(name, "PI") == 0 || strcmp(name, "STORE") == 0) return 1;
    return (function_from_name(name) == FN_NONE) ? 0 : 1;
}

static unsigned char variable_store_tf(const char *name, const unsigned char *value_tf) {
    signed char idx;
    unsigned char i;

    idx = variable_index(name);
    if (idx < 0) {
        for (i = 0; i < VAR_MAX; ++i) {
            if (!variables[i].used) {
                idx = (signed char)i;
                variables[i].used = 1;
                str_copy_fit(variables[i].name, name, VAR_NAME_MAX + 1);
                break;
            }
        }
    }
    if (idx < 0) return 0;

    tf_copy(variables[(unsigned char)idx].value_tf, value_tf);
    return 1;
}

static unsigned char variable_store_from_last(const char *name) {
    unsigned char tf_tmp[5];
    tf_from_literal(last_result_tf, tf_tmp);
    return variable_store_tf(name, tf_tmp);
}

static unsigned char variable_get_tf(const char *name, unsigned char *out_val, unsigned char *err) {
    signed char idx;

    idx = variable_index(name);
    if (idx < 0) {
        *err = ERR_BAD_VAR;
        return 0;
    }
    tf_copy(out_val, variables[(unsigned char)idx].value_tf);
    return 1;
}

static unsigned char extract_call_arg(const char *expr, unsigned char *idx,
                                      char *out, unsigned char out_len,
                                      unsigned char *err) {
    unsigned char i;
    unsigned char open_i;
    unsigned char depth;
    unsigned char len;
    unsigned char p;

    i = *idx;
    while (expr[i] == ' ') ++i;

    if (expr[i] != '(') {
        *err = ERR_SYNTAX;
        return 0;
    }

    open_i = i;
    depth = 0;
    while (expr[i] != 0) {
        if (expr[i] == '(') {
            ++depth;
        } else if (expr[i] == ')') {
            --depth;
            if (depth == 0) break;
        }
        ++i;
    }

    if (expr[i] != ')' || depth != 0) {
        *err = ERR_SYNTAX;
        return 0;
    }

    if ((unsigned char)(i - open_i) <= 1) {
        *err = ERR_SYNTAX;
        return 0;
    }

    len = (unsigned char)(i - open_i - 1);
    if (len >= out_len) {
        *err = ERR_OVERFLOW;
        return 0;
    }

    for (p = 0; p < len; ++p) {
        out[p] = expr[(unsigned char)(open_i + 1 + p)];
    }
    out[len] = 0;
    *idx = (unsigned char)(i + 1);
    return 1;
}

static void tf_from_literal(const char *lit, unsigned char *out_val) {
    str_copy_fit((char*)romfp_in, lit, ROMFP_IN_LEN);
    romfp_eval_literal();
    tf_copy(out_val, romfp_out);
}

static void tf_apply_sign(signed long sign, unsigned char *val) {
    unsigned char zero[5];
    unsigned char i;

    if (sign >= 0) return;

    for (i = 0; i < 5; ++i) {
        zero[i] = 0;
    }

    /* Negate as 0 - value to avoid parsing "-1" through ROM FIN edge cases. */
    tf_copy(romfp_a, zero);
    tf_copy(romfp_b, val);
    romfp_sub();
    tf_copy(val, romfp_out);
}

static void tf_deg_to_rad(const unsigned char *deg_val, unsigned char *out_val) {
    unsigned char pi_val[5];
    unsigned char one_eighty[5];
    unsigned char tmp[5];

    romfp_set_pi();
    tf_copy(pi_val, romfp_out);
    tf_from_literal("180", one_eighty);

    tf_copy(romfp_a, deg_val);
    tf_copy(romfp_b, pi_val);
    romfp_mul();
    tf_copy(tmp, romfp_out);

    tf_copy(romfp_a, tmp);
    tf_copy(romfp_b, one_eighty);
    romfp_div();
    tf_copy(out_val, romfp_out);
}

static unsigned char apply_func_tf(unsigned char fn, const unsigned char *in_val,
                                   unsigned char *out_val, unsigned char *err) {
    unsigned char trig_input[5];

    if (calc_mode != CALC_MODE_FLOAT &&
        !(fn == FN_ABS || fn == FN_SGN || fn == FN_INT)) {
        *err = ERR_FUNC_MODE;
        return 0;
    }

    if (fn == FN_SQR && tf_is_negative(in_val)) {
        *err = ERR_DOMAIN;
        return 0;
    }
    if (fn == FN_LOG && !tf_is_positive(in_val)) {
        *err = ERR_DOMAIN;
        return 0;
    }

    if ((fn == FN_SIN || fn == FN_COS || fn == FN_TAN) &&
        float_angle_mode == ANGLE_DEG) {
        tf_deg_to_rad(in_val, trig_input);
        tf_copy(romfp_a, trig_input);
    } else {
        tf_copy(romfp_a, in_val);
    }

    switch (fn) {
        case FN_ABS: romfp_abs(); break;
        case FN_SGN: romfp_sgn(); break;
        case FN_INT: romfp_int(); break;
        case FN_SQR: romfp_sqr(); break;
        case FN_EXP: romfp_exp(); break;
        case FN_LOG: romfp_log(); break;
        case FN_SIN: romfp_sin(); break;
        case FN_COS: romfp_cos(); break;
        case FN_TAN: romfp_tan(); break;
        case FN_ATN: romfp_atn(); break;
        case FN_RND: romfp_rnd(); break;
        default:
            *err = ERR_BAD_FUNC;
            return 0;
    }

    tf_copy(out_val, romfp_out);
    return 1;
}

static void clear_editor(void) {
    editor_buf[0] = 0;
    editor_len = 0;
    editor_cursor = 0;
}

static void trim_span(const char *src, unsigned char *start, unsigned char *end) {
    unsigned char s;
    unsigned char e;

    s = 0;
    e = (unsigned char)strlen(src);

    while (s < e && src[s] == ' ') ++s;
    while (e > s && src[e - 1] == ' ') --e;

    *start = s;
    *end = e;
}

static unsigned char copy_span(char *dst, const char *src,
                               unsigned char start, unsigned char end,
                               unsigned char max_len) {
    unsigned char len;
    unsigned char i;

    len = (unsigned char)(end - start);
    if (len > max_len) {
        len = max_len;
    }

    for (i = 0; i < len; ++i) {
        dst[i] = src[start + i];
    }
    dst[len] = 0;
    return len;
}

static unsigned char precedence(unsigned char op) {
    if (op == '^') return 3;
    if (op == '*' || op == '/') return 2;
    if (op == '+' || op == '-') return 1;
    return 0;
}

static unsigned char should_reduce(unsigned char stack_op, unsigned char incoming_op) {
    unsigned char p_stack;
    unsigned char p_in;

    if (stack_op == '(') return 0;
    p_stack = precedence(stack_op);
    p_in = precedence(incoming_op);
    if (p_stack > p_in) return 1;
    if (p_stack < p_in) return 0;
    /* '^' is right-associative. */
    return (incoming_op == '^') ? 0 : 1;
}

static unsigned char tf_is_integer_value(const unsigned char *v) {
    unsigned char whole[5];
    unsigned char i;

    tf_copy(romfp_a, v);
    romfp_int();
    tf_copy(whole, romfp_out);
    for (i = 0; i < 5; ++i) {
        if (whole[i] != v[i]) return 0;
    }
    return 1;
}

static unsigned char tf_pow_nonneg_int(const unsigned char *base,
                                       signed int exp,
                                       unsigned char *out_val) {
    unsigned char acc[5];
    unsigned char cur[5];

    tf_from_literal("1", acc);
    tf_copy(cur, base);

    while (exp > 0) {
        if (exp & 1) {
            tf_copy(romfp_a, acc);
            tf_copy(romfp_b, cur);
            romfp_mul();
            tf_copy(acc, romfp_out);
        }
        exp >>= 1;
        if (exp > 0) {
            tf_copy(romfp_a, cur);
            tf_copy(romfp_b, cur);
            romfp_mul();
            tf_copy(cur, romfp_out);
        }
    }

    tf_copy(out_val, acc);
    return 1;
}

static unsigned char tf_pow(const unsigned char *lhs, const unsigned char *rhs,
                            unsigned char *out_val, unsigned char *err) {
    unsigned char rhs_zero;
    unsigned char lhs_zero;
    unsigned char lhs_neg;

    rhs_zero = tf_is_zero(rhs);
    lhs_zero = tf_is_zero(lhs);
    lhs_neg = tf_is_negative(lhs);

    if (rhs_zero) {
        tf_from_literal("1", out_val);
        return 1;
    }

    if (lhs_zero) {
        if (tf_is_negative(rhs)) {
            *err = ERR_DOMAIN;
            return 0;
        }
        memset(out_val, 0, 5);
        return 1;
    }

    if (lhs_neg) {
        char rhs_txt[TF_STR_LEN];
        signed long rhs_fp;
        signed int rhs_i;

        if (!tf_is_integer_value(rhs)) {
            *err = ERR_DOMAIN;
            return 0;
        }

        tf_packed_to_str(rhs, rhs_txt);
        tf_approx_to_fixed(rhs_txt, &rhs_fp, &rhs_i);
        if ((rhs_fp % FP_SCALE) != 0 || rhs_i < 0) {
            *err = ERR_DOMAIN;
            return 0;
        }

        return tf_pow_nonneg_int(lhs, rhs_i, out_val);
    }

    /* Positive base: pow(a,b) = exp(log(a) * b) */
    tf_copy(romfp_a, lhs);
    romfp_log();
    tf_copy(romfp_a, romfp_out);
    tf_copy(romfp_b, rhs);
    romfp_mul();
    tf_copy(romfp_a, romfp_out);
    romfp_exp();
    tf_copy(out_val, romfp_out);
    return 1;
}

static unsigned char apply_top_tf(signed char *vtop, signed char *otop, unsigned char *err) {
    unsigned char op;
    unsigned char lhs_i;
    unsigned char rhs_i;

    if (*otop < 0 || *vtop < 1) {
        *err = ERR_SYNTAX;
        return 0;
    }

    op = eval_ops[(unsigned char)*otop];
    --(*otop);

    rhs_i = (unsigned char)*vtop;
    lhs_i = (unsigned char)(*vtop - 1);

    tf_copy(romfp_a, eval_values_tf[lhs_i]);
    tf_copy(romfp_b, eval_values_tf[rhs_i]);

    switch (op) {
        case '+':
            romfp_add();
            break;
        case '-':
            romfp_sub();
            break;
        case '*':
            romfp_mul();
            break;
        case '/':
            if (tf_is_zero(romfp_b)) {
                *err = ERR_DIV_ZERO;
                return 0;
            }
            romfp_div();
            break;
        case '^':
            if (!tf_pow(eval_values_tf[lhs_i], eval_values_tf[rhs_i],
                        eval_values_tf[lhs_i], err)) {
                return 0;
            }
            --(*vtop);
            return 1;
        default:
            *err = ERR_SYNTAX;
            return 0;
    }

    tf_copy(eval_values_tf[lhs_i], romfp_out);
    --(*vtop);
    return 1;
}

static unsigned char parse_number_tf(const char *expr, unsigned char *idx,
                                     signed long sign, unsigned char *out_val,
                                     unsigned char *err) {
    unsigned char i;
    unsigned char start;
    unsigned char pos;
    unsigned char seen_dot;
    unsigned char have_digit;
    char ident[IDENT_MAX_LEN + 1];
    unsigned char fn;
    char arg_buf[EXPR_MAX_LEN + 25];

    i = *idx;
    start = i;
    seen_dot = 0;
    have_digit = 0;

    while (1) {
        unsigned char ch = (unsigned char)expr[i];
        if (is_digit(ch)) {
            have_digit = 1;
            ++i;
            continue;
        }
        if (ch == '.' && !seen_dot) {
            seen_dot = 1;
            ++i;
            continue;
        }
        break;
    }

    if (expr[i] == '$') {
        if (!parse_var_name(expr, &i, ident, sizeof(ident), 1)) {
            *err = ERR_SYNTAX;
            return 0;
        }
        if (!variable_get_tf(ident, out_val, err)) {
            return 0;
        }
    } else if (have_digit) {
        if ((unsigned char)(i - start) > (ROMFP_IN_LEN - 4)) {
            *err = ERR_OVERFLOW;
            return 0;
        }

        pos = 0;
        if (expr[start] == '.') {
            romfp_in[pos++] = '0';
        }
        while (start < i && pos < (ROMFP_IN_LEN - 2)) {
            romfp_in[pos++] = (unsigned char)expr[start++];
        }
        if (pos > 0 && romfp_in[pos - 1] == '.') {
            romfp_in[pos++] = '0';
        }
        romfp_in[pos] = 0;

        romfp_eval_literal();
        tf_copy(out_val, romfp_out);
    } else if (is_alpha((unsigned char)expr[i])) {
        if (!parse_identifier(expr, &i, ident, sizeof(ident))) {
            *err = ERR_BAD_FUNC;
            return 0;
        }

        if (strcmp(ident, "PI") == 0) {
            romfp_set_pi();
            tf_copy(out_val, romfp_out);
        } else {
            unsigned char arg_val[5];

            fn = function_from_name(ident);
            if (fn == FN_NONE) {
                *err = ERR_BAD_FUNC;
                return 0;
            }
            if (!extract_call_arg(expr, &i, arg_buf, sizeof(arg_buf), err)) {
                return 0;
            }
            if (!eval_expression_tf(arg_buf, arg_val, err)) {
                return 0;
            }
            if (!apply_func_tf(fn, arg_val, out_val, err)) {
                return 0;
            }
        }
    } else {
        *err = ERR_SYNTAX;
        return 0;
    }

    tf_apply_sign(sign, out_val);
    *idx = i;
    return 1;
}

static unsigned char eval_expression_tf(const char *expr, unsigned char *result, unsigned char *err) {
    unsigned char len;
    unsigned char i;
    unsigned char c;
    unsigned char expect_value;
    signed char vtop;
    signed char otop;

    len = (unsigned char)strlen(expr);
    if (len == 0) {
        *err = ERR_SYNTAX;
        return 0;
    }

    i = 0;
    expect_value = 1;
    vtop = -1;
    otop = -1;
    *err = ERR_NONE;

    while (1) {
        while (i < len && expr[i] == ' ') ++i;
        if (i >= len) break;

        c = (unsigned char)expr[i];

        if (expect_value) {
            signed long sign;

            if (c == '(') {
                if (otop >= (EVAL_STACK_MAX - 1)) {
                    *err = ERR_SYNTAX;
                    return 0;
                }
                eval_ops[(unsigned char)(++otop)] = c;
                ++i;
                continue;
            }

            sign = 1;
            if (c == '+' || c == '-') {
                sign = (c == '-') ? -1 : 1;
                ++i;
                while (i < len && expr[i] == ' ') ++i;
                if (i >= len) {
                    *err = ERR_SYNTAX;
                    return 0;
                }
                c = (unsigned char)expr[i];

                if (c == '(') {
                    unsigned char z;

                    if (vtop >= (EVAL_STACK_MAX - 1) || otop >= (EVAL_STACK_MAX - 1)) {
                        *err = ERR_SYNTAX;
                        return 0;
                    }
                    ++vtop;
                    for (z = 0; z < 5; ++z) {
                        eval_values_tf[(unsigned char)vtop][z] = 0;
                    }

                    while (otop >= 0 &&
                           should_reduce(eval_ops[(unsigned char)otop], (sign < 0) ? '-' : '+')) {
                        if (!apply_top_tf(&vtop, &otop, err)) return 0;
                    }

                    eval_ops[(unsigned char)(++otop)] = (sign < 0) ? '-' : '+';
                    continue;
                }
            }

            if (vtop >= (EVAL_STACK_MAX - 1)) {
                *err = ERR_SYNTAX;
                return 0;
            }
            ++vtop;
            if (!parse_number_tf(expr, &i, sign, eval_values_tf[(unsigned char)vtop], err)) {
                return 0;
            }
            expect_value = 0;
            continue;
        }

        if (is_operator(c)) {
            while (otop >= 0 &&
                   should_reduce(eval_ops[(unsigned char)otop], c)) {
                if (!apply_top_tf(&vtop, &otop, err)) return 0;
            }

            if (otop >= (EVAL_STACK_MAX - 1)) {
                *err = ERR_SYNTAX;
                return 0;
            }
            eval_ops[(unsigned char)(++otop)] = c;
            ++i;
            expect_value = 1;
            continue;
        }

        if (c == ')') {
            while (otop >= 0 && eval_ops[(unsigned char)otop] != '(') {
                if (!apply_top_tf(&vtop, &otop, err)) return 0;
            }
            if (otop < 0 || eval_ops[(unsigned char)otop] != '(') {
                *err = ERR_SYNTAX;
                return 0;
            }
            --otop;
            ++i;
            expect_value = 0;
            continue;
        }

        *err = ERR_SYNTAX;
        return 0;
    }

    if (expect_value) {
        *err = ERR_SYNTAX;
        return 0;
    }

    while (otop >= 0) {
        if (eval_ops[(unsigned char)otop] == '(') {
            *err = ERR_SYNTAX;
            return 0;
        }
        if (!apply_top_tf(&vtop, &otop, err)) return 0;
    }

    if (vtop != 0) {
        *err = ERR_SYNTAX;
        return 0;
    }

    tf_copy(result, eval_values_tf[0]);
    return 1;
}

/*---------------------------------------------------------------------------
 * History + clipboard
 *---------------------------------------------------------------------------*/

static void push_history(const char *display_expr, unsigned char result_mode,
                         signed int result_i, signed long result_fp,
                         const char *result_tf, unsigned char is_carry) {
    unsigned char i;

    if (history_count >= HISTORY_MAX) {
        for (i = 1; i < HISTORY_MAX; ++i) {
            history[i - 1] = history[i];
        }
        history_count = HISTORY_MAX - 1;
    }

    strncpy(history[history_count].expr, display_expr, EXPR_MAX_LEN);
    history[history_count].expr[EXPR_MAX_LEN] = 0;
    history[history_count].result_mode = result_mode;
    history[history_count].result_i = result_i;
    history[history_count].result_fp = result_fp;
    str_copy_fit(history[history_count].result_tf, result_tf, TF_STR_LEN);
    history[history_count].is_carry = is_carry;
    ++history_count;

    have_last_result = 1;
    last_result_i = result_i;
    last_result_fp = result_fp;
    str_copy_fit(last_result_tf, result_tf, TF_STR_LEN);
}

static void history_result_to_str(unsigned char idx, char *out) {
    if (idx >= history_count) {
        out[0] = 0;
        return;
    }

    if (history[idx].result_mode == CALC_MODE_FLOAT) {
        str_copy_fit(out, history[idx].result_tf, TF_STR_LEN);
    } else if (history[idx].result_mode == CALC_MODE_FIXED) {
        fp_to_str(history[idx].result_fp, out);
    } else {
        int_to_str(history[idx].result_i, out);
    }
}

static void format_history_copy(unsigned char idx, char *out, unsigned char out_len) {
    char num_buf[TF_STR_LEN];
    unsigned char e_len;
    unsigned char n_len;
    unsigned char pos;
    unsigned char i;

    out[0] = 0;
    if (idx >= history_count || out_len < 6) return;

    history_result_to_str(idx, num_buf);
    e_len = (unsigned char)strlen(history[idx].expr);
    n_len = (unsigned char)strlen(num_buf);

    pos = 0;
    for (i = 0; i < e_len && pos < (unsigned char)(out_len - 1); ++i) {
        out[pos++] = history[idx].expr[i];
    }
    if (pos < (unsigned char)(out_len - 1)) out[pos++] = ' ';
    if (pos < (unsigned char)(out_len - 1)) out[pos++] = '=';
    if (pos < (unsigned char)(out_len - 1)) out[pos++] = ' ';
    for (i = 0; i < n_len && pos < (unsigned char)(out_len - 1); ++i) {
        out[pos++] = num_buf[i];
    }
    out[pos] = 0;
}

static void do_copy(void) {
    char copy_buf[64];
    unsigned char len;

    if (focus_mode == MODE_HISTORY) {
        format_history_copy(selected_hist, copy_buf, sizeof(copy_buf));
        len = (unsigned char)strlen(copy_buf);
        if (len == 0) return;
        if (clip_copy(CLIP_TYPE_TEXT, copy_buf, len) == 0) {
            set_status("COPIED HISTORY LINE", TUI_COLOR_LIGHTGREEN);
        } else {
            set_status("COPY FAILED", TUI_COLOR_LIGHTRED);
        }
        return;
    }

    if (editor_len == 0) {
        set_status("NOTHING TO COPY", TUI_COLOR_GRAY3);
        return;
    }

    if (clip_copy(CLIP_TYPE_TEXT, editor_buf, editor_len) == 0) {
        set_status("COPIED EDITOR", TUI_COLOR_LIGHTGREEN);
    } else {
        set_status("COPY FAILED", TUI_COLOR_LIGHTRED);
    }
}

static void do_paste(void) {
    char paste_buf[CLIP_BUF_LEN];
    unsigned int raw_len;
    unsigned char i;
    unsigned char end;
    unsigned char start;
    unsigned char final_len;

    if (clip_item_count() == 0) {
        set_status("CLIPBOARD EMPTY", TUI_COLOR_GRAY3);
        return;
    }

    raw_len = clip_paste(0, paste_buf, CLIP_BUF_LEN - 1);
    if (raw_len == 0) {
        set_status("CLIPBOARD EMPTY", TUI_COLOR_GRAY3);
        return;
    }

    paste_buf[raw_len] = 0;

    end = (unsigned char)raw_len;
    for (i = 0; i < (unsigned char)raw_len; ++i) {
        if (paste_buf[i] == '=') {
            end = i;
            break;
        }
    }
    paste_buf[end] = 0;

    trim_span(paste_buf, &start, &end);
    if (start >= end) {
        set_status("PASTE ERROR: EMPTY", TUI_COLOR_LIGHTRED);
        return;
    }

    final_len = (unsigned char)(end - start);
    if (final_len > EXPR_MAX_LEN) {
        set_status("PASTE ERROR: TOO LONG", TUI_COLOR_LIGHTRED);
        return;
    }

    for (i = start; i < end; ++i) {
        paste_buf[i] = (char)normalize_expr_input_char((unsigned char)paste_buf[i]);
        if (!is_expr_char((unsigned char)paste_buf[i], mode_allows_dot(calc_mode))) {
            set_status("PASTE ERROR: INVALID", TUI_COLOR_LIGHTRED);
            return;
        }
    }

    for (i = 0; i < final_len; ++i) {
        editor_buf[i] = paste_buf[start + i];
    }
    editor_buf[final_len] = 0;
    editor_len = final_len;
    editor_cursor = final_len;
    focus_mode = MODE_EDITOR;

    set_status("PASTED TO EDITOR", TUI_COLOR_LIGHTGREEN);
}

/*---------------------------------------------------------------------------
 * Rendering
 *---------------------------------------------------------------------------*/

static signed char history_idx_for_row(unsigned char row) {
    unsigned char first_row;

    if (history_count == 0) {
        return -1;
    }

    first_row = (unsigned char)(HISTORY_MAX - history_count);
    if (row < first_row) {
        return -1;
    }

    return (signed char)(row - first_row);
}

static unsigned char history_row_for_idx(unsigned char idx) {
    return (unsigned char)(HISTORY_MAX - history_count + idx);
}

static void draw_history_line(unsigned char row) {
    unsigned char y;
    unsigned char expr_color;
    unsigned char result_color;
    char num_buf[TF_STR_LEN];
    signed char idx_signed;
    unsigned char idx;
    unsigned char e_len;
    unsigned char r_len;
    unsigned char draw_len;
    unsigned char draw_x;
    unsigned char expr_start;
    unsigned char marker;
    unsigned char i;

    y = HISTORY_Y + row;
    tui_puts_n(0, y, "", 40, TUI_COLOR_WHITE);

    idx_signed = history_idx_for_row(row);
    if (idx_signed < 0) {
        return;
    }
    idx = (unsigned char)idx_signed;

    marker = 32;
    if (focus_mode == MODE_HISTORY && idx == selected_hist) {
        marker = 0x3E;
    }
    tui_putc(0, y, marker, TUI_COLOR_CYAN);

    if (focus_mode == MODE_HISTORY && idx == selected_hist) {
        expr_color = TUI_COLOR_CYAN;
        result_color = TUI_COLOR_CYAN;
    } else if (history[idx].is_carry) {
        expr_color = TUI_COLOR_LIGHTGREEN;
        result_color = (idx == history_count - 1) ? TUI_COLOR_YELLOW : expr_color;
    } else {
        expr_color = TUI_COLOR_WHITE;
        result_color = (idx == history_count - 1) ? TUI_COLOR_YELLOW : expr_color;
    }

    history_result_to_str(idx, num_buf);
    e_len = (unsigned char)strlen(history[idx].expr);
    r_len = (unsigned char)strlen(num_buf);

    draw_len = e_len;
    if (draw_len > (EQUALS_COL - 2)) {
        draw_len = (EQUALS_COL - 2);
    }
    expr_start = (unsigned char)(e_len - draw_len);
    draw_x = (unsigned char)(EQUALS_COL - 1 - draw_len);

    for (i = 0; i < draw_len; ++i) {
        tui_putc((unsigned char)(draw_x + i), y,
                 expr_ascii_to_screen((unsigned char)history[idx].expr[expr_start + i]),
                 expr_color);
    }
    tui_putc(EQUALS_COL, y, tui_ascii_to_screen('='), expr_color);
    tui_puts_n(EQUALS_COL + 2, y, num_buf, r_len, result_color);
}

static void draw_history(void) {
    unsigned char i;

    for (i = 0; i < HISTORY_MAX; ++i) {
        draw_history_line(i);
    }
}

static void draw_history_idx(unsigned char idx) {
    if (idx >= history_count) return;
    draw_history_line(history_row_for_idx(idx));
}

static void draw_editor(void) {
    unsigned char i;
    unsigned char start;
    unsigned char disp_len;
    unsigned char x;
    unsigned char ch;
    unsigned int off;

    tui_puts_n(0, EDITOR_ROW, "", 3, TUI_COLOR_WHITE);
    tui_putc(0, EDITOR_ROW, (focus_mode == MODE_EDITOR) ? 0x3E : 32, TUI_COLOR_CYAN);

    tui_puts(1, EDITOR_ROW, " ", TUI_COLOR_WHITE);

    start = 0;
    if (editor_cursor >= EDITOR_INNER_W) {
        start = (unsigned char)(editor_cursor - EDITOR_INNER_W + 1);
    }

    disp_len = editor_len;
    if (disp_len > start) {
        disp_len = (unsigned char)(disp_len - start);
    } else {
        disp_len = 0;
    }
    if (disp_len > EDITOR_INNER_W) {
        disp_len = EDITOR_INNER_W;
    }

    for (i = 0; i < EDITOR_INNER_W; ++i) {
        x = (unsigned char)(EDITOR_BOX_X + 1 + i);
        off = (unsigned int)EDITOR_ROW * 40 + x;

        if (i < disp_len) {
            ch = expr_ascii_to_screen((unsigned char)editor_buf[start + i]);
        } else {
            ch = 32;
        }

        if (focus_mode == MODE_EDITOR && cursor_visible &&
            (unsigned char)(start + i) == editor_cursor) {
            ch = 0xA0;
        }

        TUI_SCREEN[off] = ch;
        TUI_COLOR_RAM[off] = TUI_COLOR_WHITE;
    }
}

static void draw_status(void) {
    tui_puts_n(0, STATUS_Y, "", 40, TUI_COLOR_WHITE);
    tui_puts_n(0, STATUS_Y, status_msg, 39, status_color);
}

static void draw_mode_indicator(void) {
    if (calc_mode == CALC_MODE_FLOAT) {
        if (float_angle_mode == ANGLE_DEG) {
            tui_puts_n(22, 1, "MODE: FLOAT DEG", 17, TUI_COLOR_YELLOW);
        } else {
            tui_puts_n(22, 1, "MODE: FLOAT RAD", 17, TUI_COLOR_YELLOW);
        }
    } else if (calc_mode == CALC_MODE_FIXED) {
        tui_puts_n(22, 1, "MODE: FIXED", 17, TUI_COLOR_LIGHTGREEN);
    } else {
        tui_puts_n(22, 1, "MODE: INTEGER", 17, TUI_COLOR_WHITE);
    }
}

static void draw_help(void) {
    tui_puts_n(0, HELP_Y, "F1:CPY F3:PST F6:ANG F7:MODE F8:HELP", 39, TUI_COLOR_GRAY3);
    tui_puts_n(0, HELP_Y + 1, "CLR:; ST:$A $A=1 ENT UP/DN", 39, TUI_COLOR_GRAY3);
}

static void show_help_screen(void) {
    tui_clear(TUI_COLOR_BLUE);
    tui_puts_n(1, 1, "CALC PLUS HELP", 38, TUI_COLOR_YELLOW);
    tui_puts_n(1, 3, "OPS: +-*/^()   CONST: PI", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 4, "INT/FIX: ABS SGN INT", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 5, "FLT: ABS SGN INT SQR EXP LOG", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 6, "FLT: SIN COS TAN ATN RND", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 7, "F6: TRIG RAD/DEG", 38, TUI_COLOR_LIGHTGREEN);
    tui_puts_n(1, 8, "CLR PREFIX: ;expr", 38, TUI_COLOR_LIGHTGREEN);
    tui_puts_n(1, 9, "EXP: ^ / UP-ARROW", 38, TUI_COLOR_LIGHTGREEN);
    tui_puts_n(1, 10, "VARS: ST $A OR $A=EXPR, USE $A", 38, TUI_COLOR_LIGHTGREEN);
    tui_puts_n(1, 11, "F1 COPY  F3 PASTE  F8 HELP", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 12, "F6 ANGLE  F7 MODE", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 13, "ENTER EVAL  UP/DOWN HISTORY", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 14, "F2/F4 APPS  CTRL+B HOME", 38, TUI_COLOR_WHITE);
    tui_puts_n(1, 15, "PRESS ANY KEY TO RETURN", 38, TUI_COLOR_CYAN);
    (void)tui_getkey();

    draw_static_frame();
    draw_full_dynamic();
}

static void draw_static_frame(void) {
    TuiRect title;
    TuiRect edit_box;

    tui_clear(TUI_COLOR_BLUE);

    title.x = 0;
    title.y = TITLE_Y;
    title.w = 40;
    title.h = 2;
    tui_window_title(&title, "CALC PLUS", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts_n(1, 2, "TYPE EXPRESSION, ENTER TO EVAL.", 38, TUI_COLOR_GRAY3);

    edit_box.x = EDITOR_BOX_X;
    edit_box.y = EDITOR_BOX_Y;
    edit_box.w = EDITOR_BOX_W;
    edit_box.h = EDITOR_BOX_H;
    tui_window(&edit_box, TUI_COLOR_CYAN);

    draw_mode_indicator();
}

static void draw_full_dynamic(void) {
    draw_history();
    draw_editor();
    draw_status();
    draw_help();
    draw_mode_indicator();
}

/*---------------------------------------------------------------------------
 * Editor + submit
 *---------------------------------------------------------------------------*/

static unsigned char editor_insert_char(unsigned char key) {
    unsigned char i;

    key = normalize_expr_input_char(key);

    if (editor_len >= EXPR_MAX_LEN) {
        set_status("EDITOR FULL", TUI_COLOR_LIGHTRED);
        return (REDRAW_EDITOR | REDRAW_STATUS);
    }

    if (!is_expr_char(key, mode_allows_dot(calc_mode))) {
        if (key == '.' && calc_mode == CALC_MODE_INT) {
            set_status("INTEGER MODE: NO DECIMALS", TUI_COLOR_LIGHTRED);
            return (REDRAW_EDITOR | REDRAW_STATUS);
        }
        return REDRAW_NONE;
    }

    for (i = editor_len + 1; i > editor_cursor; --i) {
        editor_buf[i] = editor_buf[i - 1];
    }
    editor_buf[editor_cursor] = (char)key;
    ++editor_cursor;
    ++editor_len;
    return REDRAW_EDITOR;
}

static void editor_backspace(void) {
    unsigned char i;

    if (editor_cursor == 0 || editor_len == 0) return;

    --editor_cursor;
    for (i = editor_cursor; i < editor_len; ++i) {
        editor_buf[i] = editor_buf[i + 1];
    }
    --editor_len;
}

static unsigned char evaluate_for_current_mode(const char *expr,
                                               signed int *out_i,
                                               signed long *out_fp,
                                               char *out_tf,
                                               unsigned char *out_tf_packed,
                                               unsigned char *err) {
    signed long int_l;

    if (!eval_expression_tf(expr, out_tf_packed, err)) {
        return 0;
    }

    tf_packed_to_str(out_tf_packed, out_tf);
    tf_approx_to_fixed(out_tf, out_fp, out_i);

    if (calc_mode == CALC_MODE_INT) {
        int_l = (*out_fp) / FP_SCALE;
        if (int_l < INT16_MIN_L || int_l > INT16_MAX_L) {
            *err = ERR_OVERFLOW;
            return 0;
        }
        *out_i = (signed int)int_l;
        *out_fp = int_l * FP_SCALE;
        int_to_str(*out_i, out_tf);
    } else if (calc_mode == CALC_MODE_FIXED) {
        *out_i = (signed int)((*out_fp) / FP_SCALE);
        fp_to_str(*out_fp, out_tf);
    }

    return 1;
}

static unsigned char try_assign_command(unsigned char s, unsigned char e) {
    unsigned char i;
    unsigned char err;
    char var_name[VAR_NAME_MAX + 1];
    char rhs_expr[EXPR_MAX_LEN + 24];
    signed int result_i;
    signed long result_fp;
    char result_tf[TF_STR_LEN];
    unsigned char result_tf_packed[5];

    i = s;
    if (!parse_var_name(editor_buf, &i, var_name, sizeof(var_name), 1)) {
        return REDRAW_NONE;
    }
    while (i < e && editor_buf[i] == ' ') ++i;
    if (i >= e || editor_buf[i] != '=') {
        return REDRAW_NONE;
    }
    ++i;
    while (i < e && editor_buf[i] == ' ') ++i;
    if (i >= e) {
        set_status("ASSIGN SYNTAX", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }
    if (variable_name_reserved(var_name)) {
        set_status("ASSIGN RESERVED", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }

    copy_span(rhs_expr, editor_buf, i, e, sizeof(rhs_expr) - 1);
    if (!evaluate_for_current_mode(rhs_expr, &result_i, &result_fp, result_tf,
                                   result_tf_packed, &err)) {
        set_eval_error_status(err);
        return REDRAW_STATUS;
    }
    if (!variable_store_tf(var_name, result_tf_packed)) {
        set_status("STORE FULL", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }

    have_last_result = 1;
    last_result_i = result_i;
    last_result_fp = result_fp;
    str_copy_fit(last_result_tf, result_tf, TF_STR_LEN);

    clear_editor();
    focus_mode = MODE_EDITOR;
    cursor_visible = 1;
    blink_counter = 0;
    set_status("ASSIGNED", TUI_COLOR_LIGHTGREEN);
    return (REDRAW_EDITOR | REDRAW_STATUS);
}

static unsigned char try_store_command(unsigned char s, unsigned char e) {
    unsigned char cmd_i;
    char var_name[VAR_NAME_MAX + 1];

    cmd_i = s;
    if (!(parse_identifier(editor_buf, &cmd_i, var_name, sizeof(var_name)) &&
          strcmp(var_name, "STORE") == 0)) {
        return REDRAW_NONE;
    }

    while (cmd_i < e && editor_buf[cmd_i] == ' ') ++cmd_i;
    if (!parse_var_name(editor_buf, &cmd_i, var_name, sizeof(var_name), 1)) {
        set_status("STORE SYNTAX", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }
    while (cmd_i < e && editor_buf[cmd_i] == ' ') ++cmd_i;
    if (cmd_i != e) {
        set_status("STORE SYNTAX", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }
    if (variable_name_reserved(var_name)) {
        set_status("STORE RESERVED", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }
    if (!have_last_result) {
        set_status("STORE NO VALUE", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }
    if (!variable_store_from_last(var_name)) {
        set_status("STORE FULL", TUI_COLOR_LIGHTRED);
        return REDRAW_STATUS;
    }

    clear_editor();
    focus_mode = MODE_EDITOR;
    cursor_visible = 1;
    blink_counter = 0;
    set_status("STORED", TUI_COLOR_LIGHTGREEN);
    return (REDRAW_EDITOR | REDRAW_STATUS);
}

static unsigned char submit_editor(void) {
    unsigned char s;
    unsigned char redraw;
    unsigned char expr_start;
    unsigned char e;
    unsigned char starts_with_op;
    unsigned char force_fresh;
    unsigned char err;
    signed int result_i;
    signed long result_fp;
    unsigned char result_tf_packed[5];
    char result_tf[TF_STR_LEN];
    char eval_expr[EXPR_MAX_LEN + 24];
    char display_expr[EXPR_MAX_LEN + 1];
    char base_buf[TF_STR_LEN];
    unsigned char len;
    unsigned char i;
    unsigned char is_carry;

    trim_span(editor_buf, &s, &e);
    if (s >= e) {
        set_status("ENTER AN EXPRESSION", TUI_COLOR_GRAY3);
        return REDRAW_STATUS;
    }

    redraw = try_assign_command(s, e);
    if (redraw != REDRAW_NONE) {
        return redraw;
    }

    redraw = try_store_command(s, e);
    if (redraw != REDRAW_NONE) {
        return redraw;
    }

    expr_start = s;
    force_fresh = 0;
    if (editor_buf[expr_start] == ';') {
        force_fresh = 1;
        ++expr_start;
        while (expr_start < e && editor_buf[expr_start] == ' ') ++expr_start;
        if (expr_start >= e) {
            set_status("ENTER EXPRESSION AFTER ';'", TUI_COLOR_GRAY3);
            return REDRAW_STATUS;
        }
    }

    starts_with_op = is_operator((unsigned char)editor_buf[expr_start]);
    is_carry = (unsigned char)(starts_with_op && !force_fresh);
    if (is_carry) {
        if (calc_mode == CALC_MODE_FLOAT) {
            if (have_last_result) {
                str_copy_fit(base_buf, last_result_tf, sizeof(base_buf));
            } else {
                str_copy_fit(base_buf, "0", sizeof(base_buf));
            }
        } else if (calc_mode == CALC_MODE_FIXED) {
            signed long base_fp = have_last_result ? last_result_fp : 0;
            fp_to_str(base_fp, base_buf);
        } else {
            signed int base_i = have_last_result ? last_result_i : 0;
            int_to_str(base_i, base_buf);
        }

        eval_expr[0] = 0;
        strncat(eval_expr, base_buf, sizeof(eval_expr) - 1);
        strncat(eval_expr, &editor_buf[expr_start], sizeof(eval_expr) - 1 - (unsigned char)strlen(eval_expr));

        copy_span(display_expr, editor_buf, expr_start, e, EXPR_MAX_LEN);
    } else {
        copy_span(eval_expr, editor_buf, expr_start, e, EXPR_MAX_LEN);
        copy_span(display_expr, editor_buf, expr_start, e, EXPR_MAX_LEN);
    }

    len = (unsigned char)strlen(eval_expr);
    for (i = 0; i < len; ++i) {
        if (!is_expr_char((unsigned char)eval_expr[i], mode_allows_dot(calc_mode))) {
            set_status("ERROR: INVALID CHAR", TUI_COLOR_LIGHTRED);
            return REDRAW_STATUS;
        }
    }

    if (!evaluate_for_current_mode(eval_expr, &result_i, &result_fp, result_tf,
                                   result_tf_packed, &err)) {
        set_eval_error_status(err);
        return REDRAW_STATUS;
    }

    push_history(display_expr, calc_mode, result_i, result_fp, result_tf, is_carry);
    clear_editor();
    focus_mode = MODE_EDITOR;
    cursor_visible = 1;
    blink_counter = 0;

    set_status("OK", TUI_COLOR_LIGHTGREEN);
    return (REDRAW_HISTORY | REDRAW_EDITOR | REDRAW_STATUS);
}

/*---------------------------------------------------------------------------
 * Input handling
 *---------------------------------------------------------------------------*/

static unsigned char handle_history_enter(void) {
    if (history_count == 0 || selected_hist >= history_count) {
        return REDRAW_NONE;
    }

    history_count = selected_hist + 1;
    last_result_i = history[selected_hist].result_i;
    last_result_fp = history[selected_hist].result_fp;
    str_copy_fit(last_result_tf, history[selected_hist].result_tf, TF_STR_LEN);
    have_last_result = 1;

    focus_mode = MODE_EDITOR;
    clear_editor();
    set_status("CONTINUE FROM SELECTED RESULT", TUI_COLOR_LIGHTGREEN);
    return (REDRAW_HISTORY | REDRAW_EDITOR | REDRAW_STATUS);
}

static unsigned char handle_key(unsigned char key) {
    unsigned char nav_action;

    nav_action = tui_handle_global_hotkey(key, SHIM_CURRENT_BANK, 1);
    if (nav_action == TUI_HOTKEY_LAUNCHER) {
        resume_save_state();
        tui_return_to_launcher();
    }

    if (nav_action >= 1 && nav_action <= 15) {
        resume_save_state();
        tui_switch_to_app(nav_action);
        return REDRAW_NONE;
    }

    if (nav_action == TUI_HOTKEY_BIND_ONLY) {
        return REDRAW_NONE;
    }

    if (key == TUI_KEY_RUNSTOP) {
        running = 0;
        return REDRAW_NONE;
    }

    if (key == TUI_KEY_F1) {
        do_copy();
        return REDRAW_STATUS;
    }
    if (key == TUI_KEY_F3) {
        unsigned char was_history = (focus_mode == MODE_HISTORY);
        do_paste();
        if (was_history) {
            return (REDRAW_HISTORY_SEL | REDRAW_EDITOR | REDRAW_STATUS);
        }
        return (REDRAW_EDITOR | REDRAW_STATUS);
    }
    if (key == TUI_KEY_F8) {
        show_help_screen();
        return REDRAW_NONE;
    }
    if (key == TUI_KEY_F6) {
        if (calc_mode != CALC_MODE_FLOAT) {
            set_status("ANGLE TOGGLE: FLOAT ONLY", TUI_COLOR_GRAY3);
            return REDRAW_STATUS;
        }
        float_angle_mode = (float_angle_mode == ANGLE_RAD) ? ANGLE_DEG : ANGLE_RAD;
        if (float_angle_mode == ANGLE_DEG) {
            set_status("ANGLE MODE: DEGREES", TUI_COLOR_LIGHTGREEN);
        } else {
            set_status("ANGLE MODE: RADIANS", TUI_COLOR_LIGHTGREEN);
        }
        return (REDRAW_MODE | REDRAW_STATUS);
    }
    if (key == TUI_KEY_F7) {
        if (calc_mode == CALC_MODE_INT) {
            calc_mode = CALC_MODE_FIXED;
            set_status("MODE: FIXED", TUI_COLOR_LIGHTGREEN);
        } else if (calc_mode == CALC_MODE_FIXED) {
            calc_mode = CALC_MODE_FLOAT;
            set_status("MODE: FLOAT", TUI_COLOR_LIGHTGREEN);
        } else {
            calc_mode = CALC_MODE_INT;
            set_status("MODE: INTEGER", TUI_COLOR_LIGHTGREEN);
        }
        return (REDRAW_MODE | REDRAW_STATUS | REDRAW_EDITOR);
    }
    if (focus_mode == MODE_HISTORY) {
        if (key == TUI_KEY_UP) {
            if (selected_hist > 0) {
                --selected_hist;
                return REDRAW_HISTORY_SEL;
            }
            return REDRAW_NONE;
        }
        if (key == TUI_KEY_DOWN) {
            if (selected_hist + 1 < history_count) {
                ++selected_hist;
                return REDRAW_HISTORY_SEL;
            } else {
                focus_mode = MODE_EDITOR;
                return (REDRAW_HISTORY_SEL | REDRAW_EDITOR);
            }
        }
        if (key == TUI_KEY_RETURN) {
            return handle_history_enter();
        }
        return REDRAW_NONE;
    }

    /* Editor mode */
    if (key == TUI_KEY_UP) {
        if (history_count > 0) {
            focus_mode = MODE_HISTORY;
            selected_hist = history_count - 1;
            return (REDRAW_HISTORY_SEL | REDRAW_EDITOR);
        }
        return REDRAW_NONE;
    }

    if (key == TUI_KEY_RETURN) {
        return submit_editor();
    }

    if (key == TUI_KEY_DEL) {
        editor_backspace();
        return REDRAW_EDITOR;
    }

    if (key == TUI_KEY_LEFT) {
        if (editor_cursor > 0) --editor_cursor;
        return REDRAW_EDITOR;
    }

    if (key == TUI_KEY_RIGHT) {
        if (editor_cursor < editor_len) ++editor_cursor;
        return REDRAW_EDITOR;
    }

    if (key == TUI_KEY_HOME) {
        editor_cursor = 0;
        return REDRAW_EDITOR;
    }

    if ((key >= 32 && key < 128) || key == C64_UPARROW_KEY) {
        return editor_insert_char(key);
    }

    return REDRAW_NONE;
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

static void calcplus_init(void) {
    unsigned char i;

    tui_init();
    reu_mgr_init();

    history_count = 0;
    focus_mode = MODE_EDITOR;
    selected_hist = 0;
    calc_mode = CALC_MODE_INT;
    float_angle_mode = ANGLE_RAD;
    have_last_result = 0;
    last_result_i = 0;
    last_result_fp = 0;
    str_copy_fit(last_result_tf, "0", TF_STR_LEN);

    for (i = 0; i < VAR_MAX; ++i) {
        variables[i].used = 0;
        variables[i].name[0] = 0;
        memset(variables[i].value_tf, 0, 5);
    }

    clear_editor();
    set_status("READY", TUI_COLOR_LIGHTGREEN);

    cursor_visible = 1;
    blink_counter = 0;
    running = 1;
}

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    (void)resume_save_segments(calcplus_resume_write_segments, CALCPLUS_RESUME_SEG_COUNT);
}

static unsigned char resume_restore_state(void) {
    unsigned int payload_len = 0;
    unsigned char i;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_load_segments(calcplus_resume_read_segments, CALCPLUS_RESUME_SEG_COUNT, &payload_len)) {
        return 0;
    }

    if (history_count > HISTORY_MAX) {
        return 0;
    }
    for (i = 0; i < HISTORY_MAX; ++i) {
        history[i].expr[EXPR_MAX_LEN] = 0;
        history[i].result_tf[TF_STR_LEN - 1] = 0;
    }
    for (i = 0; i < VAR_MAX; ++i) {
        variables[i].name[VAR_NAME_MAX] = 0;
    }

    editor_buf[EXPR_MAX_LEN] = 0;
    editor_len = (unsigned char)strlen(editor_buf);
    if (editor_len > EXPR_MAX_LEN) {
        editor_len = EXPR_MAX_LEN;
    }
    if (editor_cursor > editor_len) {
        editor_cursor = editor_len;
    }

    if (focus_mode > MODE_HISTORY) {
        focus_mode = MODE_EDITOR;
    }
    if (history_count == 0) {
        focus_mode = MODE_EDITOR;
        selected_hist = 0;
    } else if (selected_hist >= history_count) {
        selected_hist = (unsigned char)(history_count - 1);
    }

    if (calc_mode > CALC_MODE_FLOAT) {
        calc_mode = CALC_MODE_INT;
    }
    if (float_angle_mode > ANGLE_DEG) {
        float_angle_mode = ANGLE_RAD;
    }
    have_last_result = have_last_result ? 1 : 0;
    last_result_tf[TF_STR_LEN - 1] = 0;
    cursor_visible = 1;
    blink_counter = 0;
    running = 1;
    set_status("RESUMED", TUI_COLOR_LIGHTGREEN);
    return 1;
}

static void idle_delay(void) {
    volatile unsigned int wait;
    for (wait = 0; wait < 1200; ++wait) {
        /* busy wait for cursor blink pacing */
    }
}

static void calcplus_loop(void) {
    unsigned char key;
    unsigned char redraw;
    unsigned char prev_focus;
    unsigned char prev_sel;

    draw_static_frame();
    draw_full_dynamic();

    while (running) {
        if (tui_kbhit()) {
            key = tui_getkey();
            prev_focus = focus_mode;
            prev_sel = selected_hist;
            redraw = handle_key(key);
            cursor_visible = 1;
            blink_counter = 0;

            if (redraw & REDRAW_HISTORY) {
                draw_history();
            } else if (redraw & REDRAW_HISTORY_SEL) {
                if (prev_focus == MODE_HISTORY && prev_sel < history_count) {
                    draw_history_idx(prev_sel);
                }
                if (focus_mode == MODE_HISTORY && selected_hist < history_count) {
                    draw_history_idx(selected_hist);
                }
            }
            if (redraw & REDRAW_EDITOR) {
                draw_editor();
            }
            if (redraw & REDRAW_MODE) {
                draw_mode_indicator();
            }
            if (redraw & REDRAW_STATUS) {
                draw_status();
            }
            continue;
        }

        idle_delay();

        if (focus_mode == MODE_EDITOR) {
            ++blink_counter;
            if (blink_counter >= BLINK_TICKS) {
                blink_counter = 0;
                cursor_visible = (unsigned char)!cursor_visible;
                draw_editor();
            }
        }
    }

    __asm__("jmp $FCE2");
}

int main(void) {
    unsigned char bank;

    calcplus_init();
    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 15) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();
    calcplus_loop();
    return 0;
}
