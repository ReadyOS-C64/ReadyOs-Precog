/*
 * xseqchk.c - Standalone SEQ append harness
 *
 * XSEQCHK_CASE:
 *   0 = run append matrix (name,s,a ; 0:name,s,a ; rewrite control)
 *   1 = append with "name,s,a"
 *   2 = append with "0:name,s,a"
 *   3 = rewrite control (read old + scratch + write combined)
 *   4 = hybrid ADD (append existing, create missing)
 */

#include <cbm.h>
#include <cbm_filetype.h>
#include <conio.h>
#include <string.h>

#ifndef XSEQCHK_CASE
#define XSEQCHK_CASE 0
#endif

#define DRIVE8 8
#define DRIVE9 9

#define LFN_DIR  1
#define LFN_FILE 2
#define LFN_CMD  15

#define RC_OK        0
#define RC_IO        1
#define RC_NOT_FOUND 2
#define RC_EXISTS    3

#define STEP_MODE    1u
#define STEP_PREP    2u
#define STEP_OPEN    3u
#define STEP_WRITE   4u
#define STEP_STATUS  5u
#define STEP_VERIFY  6u
#define STEP_DUMP    7u

#define DBG_LEN      0x280u
#define SLOT_BASE    0x20u
#define SLOT_SIZE    0x30u
#define SLOT_COUNT   12u

#define READ_BUF_CAP 48u
#define STATUS_CAP   16u
#define PREVIEW_LEN  16u

typedef struct {
    char name[17];
    unsigned int size;
    unsigned char type;
} HarnessEntry;

static unsigned char g_dbg[DBG_LEN];
static unsigned char g_read_buf[READ_BUF_CAP];
static unsigned char g_expect_buf[READ_BUF_CAP];

static const unsigned char g_append_text[] = { 'Y', 'O', '\r' };
static void cleanup_io(void) {
    cbm_k_clrch();
    cbm_k_clall();
}

static void print_line(const char* text) {
    cprintf("%s\r\n", text);
}

static void dbg_clear(void) {
    memset(g_dbg, 0, sizeof(g_dbg));
    g_dbg[0] = 0x51u;
    g_dbg[1] = 0x01u;
    g_dbg[2] = 0u;
    g_dbg[3] = (unsigned char)XSEQCHK_CASE;
    g_dbg[8] = SLOT_COUNT;
}

static void dbg_set_fail(unsigned char step, unsigned char detail) {
    if (g_dbg[4] == 0u) {
        g_dbg[4] = step;
        g_dbg[5] = detail;
    }
    g_dbg[2] = 0u;
}

static void copy_status_text(char* dst, unsigned char cap, const char* src) {
    if (!dst || cap == 0u) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = '\0';
}

static void parse_status_line(const char* line,
                              unsigned char* code_out,
                              char* msg_out,
                              unsigned char msg_cap) {
    unsigned int code;
    unsigned char i;
    const char* p;

    code = 0u;
    p = line;
    while (*p >= '0' && *p <= '9') {
        code = (unsigned int)(code * 10u + (unsigned int)(*p - '0'));
        ++p;
    }
    if (code_out) {
        *code_out = (unsigned char)((code > 255u) ? 255u : code);
    }
    if (!msg_out || msg_cap == 0u) {
        return;
    }
    if (*p == ',') {
        ++p;
    }
    while (*p == ' ') {
        ++p;
    }
    i = 0u;
    while (*p != '\0' && *p != ',' && *p != '\r' && *p != '\n' &&
           i + 1u < msg_cap) {
        msg_out[i++] = *p++;
    }
    msg_out[i] = '\0';
    if (msg_out[0] == '\0') {
        copy_status_text(msg_out, msg_cap, "STATUS");
    }
}

static unsigned char read_status_open(unsigned char* code_out,
                                      char* msg_out,
                                      unsigned char msg_cap) {
    char line[40];
    int n;

    n = cbm_read(LFN_CMD, line, sizeof(line) - 1u);
    if (n < 0) {
        n = 0;
    }
    line[n] = '\0';
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n')) {
        line[n - 1] = '\0';
        --n;
    }
    parse_status_line(line, code_out, msg_out, msg_cap);
    return (unsigned char)n;
}

static unsigned char fetch_status(unsigned char device,
                                  const char* cmd,
                                  unsigned char* code_out,
                                  char* msg_out,
                                  unsigned char msg_cap) {
    const char* open_name;

    open_name = cmd;
    if (!open_name) {
        open_name = "";
    }
    cleanup_io();
    if (cbm_open(LFN_CMD, device, 15, open_name) != 0) {
        if (code_out) {
            *code_out = 255u;
        }
        copy_status_text(msg_out, msg_cap, "NO STATUS");
        cleanup_io();
        return RC_IO;
    }
    (void)read_status_open(code_out, msg_out, msg_cap);
    cbm_close(LFN_CMD);
    cleanup_io();
    return RC_OK;
}

static unsigned char run_command(unsigned char device,
                                 const char* cmd,
                                 unsigned char* code_out,
                                 char* msg_out,
                                 unsigned char msg_cap) {
    unsigned char code;
    const char* open_name;

    open_name = cmd;
    if (!open_name) {
        open_name = "";
    }

    cleanup_io();
    if (cbm_open(LFN_CMD, device, 15, open_name) != 0) {
        if (code_out) {
            *code_out = 255u;
        }
        copy_status_text(msg_out, msg_cap, "CMD OPEN");
        cleanup_io();
        return RC_IO;
    }
    code = 255u;
    (void)read_status_open(&code, msg_out, msg_cap);
    cbm_close(LFN_CMD);
    cleanup_io();
    if (code_out) {
        *code_out = code;
    }
    if (code <= 1u) {
        return RC_OK;
    }
    if (code == 62u || code == 50u) {
        return RC_NOT_FOUND;
    }
    if (code == 63u) {
        return RC_EXISTS;
    }
    return RC_IO;
}

static unsigned char set_1571_mode(unsigned char device) {
    unsigned char code;
    char msg[STATUS_CAP];

    if (run_command(device, "u0>m1", &code, msg, sizeof(msg)) != RC_OK) {
        cprintf("MODE D%u FAIL %u %s\r\n", device, code, msg);
        return 1u;
    }
    cprintf("MODE D%u OK %u %s\r\n", device, code, msg);
    return 0u;
}

static unsigned char dir_upchar(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (unsigned char)(ch - ('a' - 'A'));
    }
    if (ch >= 0xC1u && ch <= 0xDAu) {
        return (unsigned char)(ch & 0x7Fu);
    }
    return ch;
}

static unsigned char dir_type_from_text(const char* type_text) {
    unsigned char a;
    unsigned char b;
    unsigned char c;

    a = dir_upchar((unsigned char)type_text[0]);
    b = dir_upchar((unsigned char)type_text[1]);
    c = dir_upchar((unsigned char)type_text[2]);

    if (a == 'S' && b == 'E' && c == 'Q') return CBM_T_SEQ;
    if (a == 'P' && b == 'R' && c == 'G') return CBM_T_PRG;
    if (a == 'U' && b == 'S' && c == 'R') return CBM_T_USR;
    if (a == 'R' && b == 'E' && c == 'L') return CBM_T_REL;
    return CBM_T_DEL;
}

static unsigned char open_directory_raw(unsigned char device) {
    cleanup_io();
    if (cbm_open(LFN_DIR, device, 0, "$") != 0) {
        cleanup_io();
        return RC_IO;
    }
    return RC_OK;
}

static unsigned char skip_directory_line(void) {
    unsigned char ch;
    int n;

    while (1) {
        n = cbm_read(LFN_DIR, &ch, 1u);
        if (n < 1 || ch == 0u) {
            break;
        }
    }
    return RC_OK;
}

static unsigned char find_entry(unsigned char device,
                                const char* name,
                                HarnessEntry* out_entry) {
    unsigned char ptr[2];
    unsigned char num[2];
    unsigned char ch;
    unsigned char first_line;
    unsigned char in_quotes;
    unsigned char name_pos;
    unsigned char type_pos;
    unsigned char past_space;
    char type_text[4];
    int n;
    HarnessEntry entry;

    if (open_directory_raw(device) != RC_OK) {
        return RC_IO;
    }
    (void)cbm_read(LFN_DIR, ptr, 2u);
    first_line = 1u;

    while (1) {
        n = cbm_read(LFN_DIR, ptr, 2u);
        if (n < 2) {
            cbm_close(LFN_DIR);
            cleanup_io();
            return RC_IO;
        }
        n = cbm_read(LFN_DIR, num, 2u);
        if (n < 2) {
            cbm_close(LFN_DIR);
            cleanup_io();
            return RC_IO;
        }
        if (ptr[0] == 0u && ptr[1] == 0u) {
            (void)skip_directory_line();
            cbm_close(LFN_DIR);
            cleanup_io();
            return RC_NOT_FOUND;
        }

        in_quotes = 0u;
        name_pos = 0u;
        while (1) {
            n = cbm_read(LFN_DIR, &ch, 1u);
            if (n < 1 || ch == 0u) {
                break;
            }
            if (ch == 0x22u) {
                if (in_quotes) {
                    break;
                }
                in_quotes = 1u;
                continue;
            }
            if (in_quotes && !first_line && name_pos + 1u < sizeof(entry.name)) {
                entry.name[name_pos++] = (char)ch;
            }
        }

        type_text[0] = '\0';
        type_text[1] = '\0';
        type_text[2] = '\0';
        type_text[3] = '\0';
        type_pos = 0u;
        past_space = 0u;
        if (ch != 0u) {
            while (1) {
                n = cbm_read(LFN_DIR, &ch, 1u);
                if (n < 1 || ch == 0u) {
                    break;
                }
                if (!past_space) {
                    if (ch != ' ' && ch != 0xA0u) {
                        past_space = 1u;
                        if (type_pos < 3u) {
                            type_text[type_pos++] = (char)ch;
                        }
                    }
                } else if (type_pos < 3u && ch != ' ' && ch != 0xA0u) {
                    type_text[type_pos++] = (char)ch;
                }
            }
        }

        if (!first_line) {
            entry.name[name_pos] = '\0';
            entry.size = (unsigned int)(num[0] | ((unsigned int)num[1] << 8u));
            entry.type = dir_type_from_text(type_text);
            if (strcmp(entry.name, name) == 0) {
                if (out_entry) {
                    *out_entry = entry;
                }
                cbm_close(LFN_DIR);
                cleanup_io();
                return RC_OK;
            }
        }
        first_line = 0u;
    }
}

static unsigned char scratch_name(unsigned char device, const char* name) {
    char cmd[24];
    unsigned char code;
    char msg[STATUS_CAP];
    unsigned char rc;

    strcpy(cmd, "s0:");
    strcat(cmd, name);
    rc = run_command(device, cmd, &code, msg, sizeof(msg));
    if (rc == RC_OK || rc == RC_NOT_FOUND) {
        return RC_OK;
    }
    return rc;
}

static unsigned char read_seq_all(unsigned char device,
                                  const char* name,
                                  unsigned char* buf,
                                  unsigned char cap,
                                  unsigned char* out_len) {
    char spec[24];
    int n;
    unsigned char total;

    if (out_len) {
        *out_len = 0u;
    }
    strcpy(spec, name);
    strcat(spec, ",s,r");

    cleanup_io();
    if (cbm_open(LFN_FILE, device, 2, spec) != 0) {
        cleanup_io();
        return RC_IO;
    }

    total = 0u;
    while (1) {
        n = cbm_read(LFN_FILE, buf + total, (unsigned int)(cap - total));
        if (n < 0) {
            cbm_close(LFN_FILE);
            cleanup_io();
            return RC_IO;
        }
        if (n == 0) {
            break;
        }
        total = (unsigned char)(total + (unsigned char)n);
        if (total >= cap) {
            cbm_close(LFN_FILE);
            cleanup_io();
            return RC_IO;
        }
    }

    cbm_close(LFN_FILE);
    cleanup_io();
    if (out_len) {
        *out_len = total;
    }
    return RC_OK;
}

static unsigned char write_named_dump_file(const char* name) {
    int rc;
    int n;
    char open_name[24];

    if (scratch_name(DRIVE8, name) != RC_OK) {
        dbg_set_fail(STEP_DUMP, 1u);
        return 1u;
    }

    strcpy(open_name, name);
    strcat(open_name, ",s,w");

    rc = cbm_open(7u, DRIVE8, 2, open_name);
    if (rc != 0) {
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

static unsigned char write_debug_dump_file(void) {
    return write_named_dump_file("xseqstat");
}

static unsigned char* slot_ptr(unsigned char index) {
    return g_dbg + SLOT_BASE + ((unsigned int)index * (unsigned int)SLOT_SIZE);
}

static void slot_store_text(unsigned char* slot,
                            unsigned char off,
                            const char* text,
                            unsigned char cap) {
    unsigned char i;

    for (i = 0u; i < cap; ++i) {
        slot[off + i] = (text && text[i] != '\0') ? (unsigned char)text[i] : 0u;
    }
}

static void slot_store_preview(unsigned char* slot,
                               const unsigned char* text,
                               unsigned char len) {
    unsigned char i;

    for (i = 0u; i < PREVIEW_LEN; ++i) {
        slot[18u + i] = (text && i < len) ? text[i] : 0u;
    }
}

static void build_seq_spec(const char* name,
                           unsigned char prefix_zero,
                           char mode,
                           char* out,
                           unsigned char out_cap) {
    unsigned char pos;
    unsigned char i;

    pos = 0u;
    if (prefix_zero && pos + 2u < out_cap) {
        out[pos++] = '0';
        out[pos++] = ':';
    }
    for (i = 0u; name[i] != '\0' && pos + 5u < out_cap; ++i) {
        out[pos++] = name[i];
    }
    out[pos++] = ',';
    out[pos++] = 's';
    out[pos++] = ',';
    out[pos++] = mode;
    out[pos] = '\0';
}

static unsigned char verify_content(unsigned char* slot,
                                    unsigned char device,
                                    const char* name,
                                    const unsigned char* expected,
                                    unsigned char expected_len) {
    HarnessEntry entry;
    unsigned char len;
    unsigned char rc;

    rc = find_entry(device, name, &entry);
    slot[12u] = rc;
    if (rc != RC_OK) {
        return 6u;
    }
    slot[13u] = entry.type;
    slot[14u] = (unsigned char)(entry.size & 0xFFu);
    slot[15u] = (unsigned char)((entry.size >> 8u) & 0xFFu);

    rc = read_seq_all(device, name, g_read_buf, sizeof(g_read_buf), &len);
    slot[11u] = rc;
    slot[16u] = len;
    slot[17u] = expected_len;
    slot_store_preview(slot, g_read_buf, len);
    if (rc != RC_OK) {
        return 5u;
    }
    if (entry.type != CBM_T_SEQ) {
        return 7u;
    }
    if (len != expected_len) {
        return 8u;
    }
    if (memcmp(g_read_buf, expected, expected_len) != 0) {
        return 9u;
    }
    return 0u;
}

static unsigned char run_append_slot(unsigned char slot_index,
                                     unsigned char drive,
                                     const char* name,
                                     unsigned char prefix_zero,
                                     unsigned char existed_before) {
    unsigned char* slot;
    HarnessEntry entry;
    unsigned char rc;
    unsigned char code;
    char msg[STATUS_CAP];
    char spec[24];
    int open_rc;
    int nwrite;
    unsigned char base_len;
    unsigned char expected_len;
    unsigned char op_rc;

    slot = slot_ptr(slot_index);
    memset(slot, 0, SLOT_SIZE);
    slot[0u] = drive;
    slot[1u] = existed_before ? 'E' : 'N';
    slot[2u] = 'A';
    slot[3u] = prefix_zero;
    slot[4u] = 'a';
    slot[6u] = existed_before;

    if (existed_before) {
        rc = find_entry(drive, name, &entry);
        if (rc != RC_OK || entry.type != CBM_T_SEQ) {
            slot[5u] = 1u;
            return 1u;
        }
        rc = read_seq_all(drive, name, g_expect_buf, sizeof(g_expect_buf), &base_len);
        if (rc != RC_OK) {
            slot[5u] = 11u;
            return 11u;
        }
        memcpy(g_expect_buf + base_len, g_append_text, sizeof(g_append_text));
        expected_len = (unsigned char)(base_len + (unsigned char)sizeof(g_append_text));
    } else {
        if (scratch_name(drive, name) != RC_OK) {
            slot[5u] = 10u;
            return 10u;
        }
        memcpy(g_expect_buf, g_append_text, sizeof(g_append_text));
        expected_len = (unsigned char)sizeof(g_append_text);
    }

    build_seq_spec(name, prefix_zero, 'a', spec, sizeof(spec));
    cleanup_io();
    open_rc = cbm_open(LFN_FILE, drive, 2, spec);
    slot[7u] = (unsigned char)((open_rc < 0) ? 0xFFu : (unsigned char)open_rc);
    if (open_rc != 0) {
        slot[9u] = fetch_status(drive, "", &code, msg, sizeof(msg));
        slot[10u] = code;
        slot_store_text(slot, 34u, msg, 14u);
        slot[5u] = 2u;
        cleanup_io();
        return 2u;
    }

    nwrite = cbm_write(LFN_FILE, g_append_text, sizeof(g_append_text));
    slot[8u] = (unsigned char)((nwrite < 0) ? 0xFFu : (unsigned char)nwrite);
    cbm_close(LFN_FILE);
    cleanup_io();
    slot[9u] = fetch_status(drive, "", &code, msg, sizeof(msg));
    slot[10u] = code;
    slot_store_text(slot, 34u, msg, 14u);

    if (nwrite != (int)sizeof(g_append_text)) {
        slot[5u] = 3u;
        return 3u;
    }
    if (code > 1u) {
        slot[5u] = 4u;
        return 4u;
    }

    op_rc = verify_content(slot, drive, name, g_expect_buf, expected_len);
    slot[5u] = op_rc;
    return op_rc;
}

static unsigned char run_rewrite_slot(unsigned char slot_index,
                                      unsigned char drive,
                                      const char* name,
                                      unsigned char existed_before) {
    unsigned char* slot;
    HarnessEntry entry;
    unsigned char rc;
    unsigned char code;
    unsigned char len;
    char msg[STATUS_CAP];
    char spec[24];
    int open_rc;
    int nwrite;
    unsigned char expected_len;
    unsigned char op_rc;

    slot = slot_ptr(slot_index);
    memset(slot, 0, SLOT_SIZE);
    slot[0u] = drive;
    slot[1u] = existed_before ? 'E' : 'N';
    slot[2u] = 'R';
    slot[3u] = 0u;
    slot[4u] = 'w';
    slot[6u] = existed_before;

    if (existed_before) {
        rc = find_entry(drive, name, &entry);
        if (rc != RC_OK || entry.type != CBM_T_SEQ) {
            slot[5u] = 1u;
            return 1u;
        }
        rc = read_seq_all(drive, name, g_expect_buf, sizeof(g_expect_buf), &len);
        if (rc != RC_OK) {
            slot[5u] = 11u;
            return 11u;
        }
        memcpy(g_expect_buf + len, g_append_text, sizeof(g_append_text));
        expected_len = (unsigned char)(len + (unsigned char)sizeof(g_append_text));
        if (scratch_name(drive, name) != RC_OK) {
            slot[5u] = 10u;
            return 10u;
        }
    } else {
        if (scratch_name(drive, name) != RC_OK) {
            slot[5u] = 10u;
            return 10u;
        }
        memcpy(g_expect_buf, g_append_text, sizeof(g_append_text));
        expected_len = (unsigned char)sizeof(g_append_text);
    }

    build_seq_spec(name, 0u, 'w', spec, sizeof(spec));
    cleanup_io();
    open_rc = cbm_open(LFN_FILE, drive, 2, spec);
    slot[7u] = (unsigned char)((open_rc < 0) ? 0xFFu : (unsigned char)open_rc);
    if (open_rc != 0) {
        slot[9u] = fetch_status(drive, "", &code, msg, sizeof(msg));
        slot[10u] = code;
        slot_store_text(slot, 34u, msg, 14u);
        slot[5u] = 12u;
        cleanup_io();
        return 12u;
    }

    nwrite = cbm_write(LFN_FILE, g_expect_buf, expected_len);
    slot[8u] = (unsigned char)((nwrite < 0) ? 0xFFu : (unsigned char)nwrite);
    cbm_close(LFN_FILE);
    cleanup_io();
    slot[9u] = fetch_status(drive, "", &code, msg, sizeof(msg));
    slot[10u] = code;
    slot_store_text(slot, 34u, msg, 14u);

    if (nwrite != (int)expected_len) {
        slot[5u] = 13u;
        return 13u;
    }
    if (code > 1u) {
        slot[5u] = 4u;
        return 4u;
    }

    op_rc = verify_content(slot, drive, name, g_expect_buf, expected_len);
    slot[5u] = op_rc;
    return op_rc;
}

static unsigned char run_append_case(unsigned char prefix_zero,
                                     const char* old_name,
                                     const char* new_name,
                                     unsigned char slot_index) {
    unsigned char rc;

    rc = 0u;
    if (run_append_slot(slot_index + 0u, DRIVE8, old_name, prefix_zero, 1u) != 0u) {
        rc = 1u;
    }
    if (run_append_slot(slot_index + 1u, DRIVE8, new_name, prefix_zero, 0u) != 0u) {
        rc = 1u;
    }
    if (run_append_slot(slot_index + 2u, DRIVE9, old_name, prefix_zero, 1u) != 0u) {
        rc = 1u;
    }
    if (run_append_slot(slot_index + 3u, DRIVE9, new_name, prefix_zero, 0u) != 0u) {
        rc = 1u;
    }
    return rc;
}

static unsigned char run_rewrite_case(const char* old_name,
                                      const char* new_name,
                                      unsigned char slot_index) {
    unsigned char rc;

    rc = 0u;
    if (run_rewrite_slot(slot_index + 0u, DRIVE8, old_name, 1u) != 0u) {
        rc = 1u;
    }
    if (run_rewrite_slot(slot_index + 1u, DRIVE8, new_name, 0u) != 0u) {
        rc = 1u;
    }
    if (run_rewrite_slot(slot_index + 2u, DRIVE9, old_name, 1u) != 0u) {
        rc = 1u;
    }
    if (run_rewrite_slot(slot_index + 3u, DRIVE9, new_name, 0u) != 0u) {
        rc = 1u;
    }
    return rc;
}

static unsigned char run_hybrid_case(const char* old_name,
                                     const char* new_name,
                                     unsigned char slot_index) {
    unsigned char rc;

    rc = 0u;
    if (run_append_slot(slot_index + 0u, DRIVE8, old_name, 0u, 1u) != 0u) {
        rc = 1u;
    }
    if (run_rewrite_slot(slot_index + 1u, DRIVE8, new_name, 0u) != 0u) {
        rc = 1u;
    }
    if (run_append_slot(slot_index + 2u, DRIVE9, old_name, 0u, 1u) != 0u) {
        rc = 1u;
    }
    if (run_rewrite_slot(slot_index + 3u, DRIVE9, new_name, 0u) != 0u) {
        rc = 1u;
    }

    slot_ptr(slot_index + 0u)[2u] = 'H';
    slot_ptr(slot_index + 1u)[2u] = 'H';
    slot_ptr(slot_index + 2u)[2u] = 'H';
    slot_ptr(slot_index + 3u)[2u] = 'H';
    return rc;
}

static unsigned char run_selected_case(void) {
    unsigned char fail;
    unsigned char winners;

    winners = 0u;
    fail = 0u;

    switch (XSEQCHK_CASE) {
        case 1:
            print_line("CASE APPEND NAME");
            if (run_append_case(0u, "olda", "newa", 0u) == 0u) {
                winners |= 0x01u;
            } else {
                fail = 1u;
            }
            break;
        case 2:
            print_line("CASE APPEND 0:NAME");
            if (run_append_case(1u, "oldb", "newb", 0u) == 0u) {
                winners |= 0x02u;
            } else {
                fail = 1u;
            }
            break;
        case 3:
            print_line("CASE REWRITE CTRL");
            if (run_rewrite_case("oldc", "newc", 0u) == 0u) {
                winners |= 0x04u;
            } else {
                fail = 1u;
            }
            break;
        case 4:
            print_line("CASE HYBRID ADD");
            if (run_hybrid_case("oldc", "newc", 0u) == 0u) {
                winners |= 0x08u;
            } else {
                fail = 1u;
            }
            break;
        default:
            print_line("CASE MATRIX");
            if (run_append_case(0u, "olda", "newa", 0u) == 0u) {
                winners |= 0x01u;
            } else {
                fail = 1u;
            }
            if (run_append_case(1u, "oldb", "newb", 4u) == 0u) {
                winners |= 0x02u;
            }
            if (run_rewrite_case("oldc", "newc", 8u) == 0u) {
                winners |= 0x04u;
            }
            if (winners == 0u) {
                fail = 1u;
            }
            break;
    }

    g_dbg[9] = winners;
    return fail;
}

int main(void) {
    unsigned char failed;

    clrscr();
    dbg_clear();
    print_line("XSEQCHK SEQ HARNESS");
    cprintf("CASE %u\r\n", (unsigned char)XSEQCHK_CASE);

    if (set_1571_mode(DRIVE8) != 0u) {
        g_dbg[6] = 1u;
        dbg_set_fail(STEP_MODE, 1u);
        (void)write_debug_dump_file();
        print_line("MODE SET FAIL");
        return 1;
    }
    if (set_1571_mode(DRIVE9) != 0u) {
        g_dbg[7] = 1u;
        dbg_set_fail(STEP_MODE, 2u);
        (void)write_debug_dump_file();
        print_line("MODE SET FAIL");
        return 1;
    }

    g_dbg[6] = 0u;
    g_dbg[7] = 0u;
    (void)write_debug_dump_file();

    failed = run_selected_case();
    print_line("");
    if (failed) {
        print_line("RESULT FAIL");
        g_dbg[2] = 0u;
        if (g_dbg[4] == 0u) {
            dbg_set_fail(STEP_VERIFY, 1u);
        }
    } else {
        print_line("RESULT PASS");
        g_dbg[2] = 1u;
    }
    (void)write_debug_dump_file();
    return failed ? 1 : 0;
}
