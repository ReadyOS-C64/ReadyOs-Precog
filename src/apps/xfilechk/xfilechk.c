/*
 * xfilechk.c - Standalone IEC file-operation harness
 *
 * XFILECHK_CASE:
 *   0 = run all cases
 *   1 = probe matrix + recovery
 *   2 = duplicate on drive 8 with C0:
 *   3 = duplicate on drive 8 with C:
 *   4 = duplicate on drive 9 with C0:
 *   5 = duplicate on drive 9 with C:
 *   6 = stream copy 8 -> 9
 *   7 = stream copy 9 -> 8
 *   8 = staged duplicate 9 -> 8 temp -> 9
 *   9 = dump-only smoke case
 *  10 = dump then spin forever
 *  11 = probe missing drive via CMD then recover
 *  12 = probe missing drive via DIR open then recover
 *  13 = probe missing drive via full DIR scan then recover
 */

#include <cbm.h>
#include <cbm_filetype.h>
#include <conio.h>
#include <string.h>

#ifndef XFILECHK_CASE
#define XFILECHK_CASE 0
#endif

#define DRIVE8 8
#define DRIVE9 9

#define LFN_DIR  1
#define LFN_FILE 2
#define LFN_AUX  3
#define LFN_CMD  15

#define RC_OK        0
#define RC_IO        1
#define RC_NOT_FOUND 2
#define RC_EXISTS    3

#define READ_BUF_CAP 96
#define STATUS_CAP   24

#define DBG_LEN      0x80

#define STEP_NONE       0u
#define STEP_MODE       1u
#define STEP_PROBE      2u
#define STEP_SCRATCH    3u
#define STEP_COPY_CMD   4u
#define STEP_COPY_IO    5u
#define STEP_VERIFY     6u
#define STEP_DUMP       7u
#define STEP_STAGE      8u

typedef struct {
    char name[17];
    unsigned int size;
    unsigned char type;
} HarnessEntry;

static unsigned char file_buf_a[READ_BUF_CAP];
static unsigned char file_buf_b[READ_BUF_CAP];
static unsigned char dbg[DBG_LEN];

static void cleanup_io(void);
static unsigned char write_debug_dump_file(void);
static unsigned char write_named_dump_file(const char *name);

static void dbg_clear(void) {
    memset(dbg, 0, sizeof(dbg));
    dbg[0] = 0x58u;
    dbg[1] = 0x01u;
    dbg[2] = 0u;
    dbg[3] = (unsigned char)XFILECHK_CASE;
}

static void dbg_push_stage(char stage) {
    unsigned char len;

    len = dbg[0x3F];
    if (len >= 16u) {
        return;
    }
    dbg[0x30u + len] = (unsigned char)stage;
    dbg[0x3F] = (unsigned char)(len + 1u);
}

static void dbg_set_fail(unsigned char step, unsigned char detail) {
    dbg[4] = step;
    dbg[5] = detail;
    dbg[2] = 0u;
}

static void dbg_set_status_msg(const char *msg) {
    unsigned char i;

    for (i = 0u; i < 20u; ++i) {
        dbg[0x40u + i] = 0u;
    }
    if (msg == 0) {
        return;
    }
    for (i = 0u; i < 19u && msg[i] != 0; ++i) {
        dbg[0x40u + i] = (unsigned char)msg[i];
    }
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

static unsigned char dir_type_from_text(const char *type_text) {
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
    if (a == 'D' && b == 'I' && c == 'R') return CBM_T_DIR;
    if (a == 'C' && b == 'B' && c == 'M') return CBM_T_CBM;
    if (a == 'D' && b == 'E' && c == 'L') return CBM_T_DEL;
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

static unsigned char read_directory_first_entry(unsigned char device,
                                                HarnessEntry *out_entry,
                                                unsigned char *out_blocks_free) {
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

    if (out_blocks_free != 0) {
        *out_blocks_free = 0u;
    }
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
            if (out_blocks_free != 0) {
                *out_blocks_free = num[0];
            }
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
            if (in_quotes && !first_line && out_entry != 0 &&
                name_pos + 1u < sizeof(out_entry->name)) {
                out_entry->name[name_pos++] = (char)ch;
            }
        }

        type_text[0] = 0;
        type_text[1] = 0;
        type_text[2] = 0;
        type_text[3] = 0;
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
            if (out_entry != 0) {
                out_entry->name[name_pos] = 0;
                out_entry->size = (unsigned int)(num[0] | ((unsigned int)num[1] << 8u));
                out_entry->type = dir_type_from_text(type_text);
            }
            cbm_close(LFN_DIR);
            cleanup_io();
            return RC_OK;
        }

        first_line = 0u;
    }
}

static void cleanup_io(void) {
    cbm_k_clrch();
    cbm_k_clall();
}

static void print_line(const char *text) {
    cprintf("%s\r\n", text);
}

static void print_label_uc(const char *label, unsigned char value) {
    cprintf("%s%u\r\n", label, value);
}

static void print_drive_text(unsigned char drive, const char *text) {
    cprintf("D%u %s\r\n", drive, text);
}

static void copy_status_text(char *dst, unsigned char cap, const char *src) {
    if (cap == 0u) {
        return;
    }
    strncpy(dst, src, cap - 1u);
    dst[cap - 1u] = 0;
}

static void dbg_store_entry(unsigned char offset, const HarnessEntry *entry) {
    if (entry == 0) {
        return;
    }
    dbg[offset + 0u] = entry->type;
    dbg[offset + 1u] = (unsigned char)(entry->size & 0xFFu);
    dbg[offset + 2u] = (unsigned char)((entry->size >> 8) & 0xFFu);
}

static void parse_status_line(const char *line,
                              unsigned char *code_out,
                              char *msg_out,
                              unsigned char msg_cap) {
    unsigned int code;
    unsigned char i;
    const char *p;

    code = 0u;
    p = line;
    while (*p >= '0' && *p <= '9') {
        code = (unsigned int)(code * 10u + (unsigned int)(*p - '0'));
        ++p;
    }

    if (code_out != 0) {
        *code_out = (code > 255u) ? 255u : (unsigned char)code;
    }

    if (msg_out == 0 || msg_cap == 0u) {
        return;
    }

    if (*p == ',') {
        ++p;
    }
    while (*p == ' ') {
        ++p;
    }

    i = 0u;
    while (*p != 0 && *p != ',' && *p != '\r' && *p != '\n' &&
           i + 1u < msg_cap) {
        msg_out[i++] = *p++;
    }
    msg_out[i] = 0;
    if (msg_out[0] == 0) {
        copy_status_text(msg_out, msg_cap, "STATUS");
    }
}

static unsigned char read_status_open(unsigned char *code_out,
                                      char *msg_out,
                                      unsigned char msg_cap) {
    char line[40];
    int n;

    n = cbm_read(LFN_CMD, line, sizeof(line) - 1u);
    if (n < 0) {
        n = 0;
    }
    line[n] = 0;
    while (n > 0 &&
           (line[n - 1] == '\r' || line[n - 1] == '\n')) {
        line[n - 1] = 0;
        --n;
    }

    parse_status_line(line, code_out, msg_out, msg_cap);
    return (unsigned char)n;
}

static unsigned char fetch_status(unsigned char device,
                                  const char *cmd,
                                  unsigned char *code_out,
                                  char *msg_out,
                                  unsigned char msg_cap) {
    const char *open_name;

    open_name = cmd;
    if (open_name == 0) {
        open_name = "";
    }

    cleanup_io();
    if (cbm_open(LFN_CMD, device, 15, open_name) != 0) {
        if (code_out != 0) {
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
                                 const char *cmd,
                                 unsigned char *code_out,
                                 char *msg_out,
                                 unsigned char msg_cap) {
    unsigned char code;
    const char *open_name;

    open_name = cmd;
    if (open_name == 0) {
        open_name = "";
    }

    cleanup_io();
    if (cbm_open(LFN_CMD, device, 15, open_name) != 0) {
        if (code_out != 0) {
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

    if (code_out != 0) {
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
        dbg_set_status_msg(msg);
        cprintf("MODE D%u FAIL %u %s\r\n", device, code, msg);
        return 1u;
    }
    dbg_set_status_msg(msg);
    cprintf("MODE D%u OK %u %s\r\n", device, code, msg);
    return 0u;
}

static unsigned char probe_cmd(unsigned char device) {
    unsigned char code;
    char msg[STATUS_CAP];
    unsigned char rc;

    rc = fetch_status(device, "", &code, msg, sizeof(msg));
    cprintf("CMD  D%u RC%u ST%u %s\r\n", device, rc, code, msg);
    dbg_set_status_msg(msg);
    return (unsigned char)(rc == RC_OK && code != 255u);
}

static unsigned char probe_dir_open(unsigned char device) {
    int rc;
    unsigned char code;
    char msg[STATUS_CAP];

    cleanup_io();
    rc = cbm_open(LFN_DIR, device, 0, "$");
    if (rc == 0) {
        cbm_close(LFN_DIR);
    }
    cleanup_io();
    (void)fetch_status(device, "", &code, msg, sizeof(msg));
    dbg_set_status_msg(msg);
    cprintf("DOPN D%u ORC%u ST%u %s\r\n",
            device,
            (unsigned char)((rc < 0) ? 255u : (unsigned char)rc),
            code,
            msg);
    return (unsigned char)(rc == 0);
}

static unsigned char probe_dir_first(unsigned char device) {
    HarnessEntry entry;
    unsigned char rc_read;
    unsigned char code;
    char msg[STATUS_CAP];

    rc_read = read_directory_first_entry(device, &entry, 0);
    (void)fetch_status(device, "", &code, msg, sizeof(msg));
    dbg_set_status_msg(msg);
    cprintf("DFST D%u ORC0 RRC%u ST%u %s\r\n",
            device,
            rc_read,
            code,
            msg);
    return (unsigned char)(rc_read == RC_OK || rc_read == RC_NOT_FOUND);
}

static unsigned char probe_dir_scan(unsigned char device) {
    unsigned char ptr[2];
    unsigned char num[2];
    unsigned char ch;
    int n;

    if (open_directory_raw(device) != RC_OK) {
        return 0u;
    }

    n = cbm_read(LFN_DIR, ptr, 2u);
    if (n < 2) {
        cbm_close(LFN_DIR);
        cleanup_io();
        return 0u;
    }

    while (1) {
        n = cbm_read(LFN_DIR, ptr, 2u);
        if (n < 2) {
            cbm_close(LFN_DIR);
            cleanup_io();
            return 0u;
        }
        n = cbm_read(LFN_DIR, num, 2u);
        if (n < 2) {
            cbm_close(LFN_DIR);
            cleanup_io();
            return 0u;
        }

        while (1) {
            n = cbm_read(LFN_DIR, &ch, 1u);
            if (n < 1 || ch == 0u) {
                break;
            }
        }

        if (ptr[0] == 0u && ptr[1] == 0u) {
            cbm_close(LFN_DIR);
            cleanup_io();
            return 1u;
        }
        if (n < 1) {
            cbm_close(LFN_DIR);
            cleanup_io();
            return 0u;
        }
    }
}

static unsigned char find_entry(unsigned char device,
                                const char *name,
                                HarnessEntry *out_entry) {
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

        type_text[0] = 0;
        type_text[1] = 0;
        type_text[2] = 0;
        type_text[3] = 0;
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
            entry.name[name_pos] = 0;
            entry.size = (unsigned int)(num[0] | ((unsigned int)num[1] << 8u));
            entry.type = dir_type_from_text(type_text);
            if (strcmp(entry.name, name) == 0) {
                if (out_entry != 0) {
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

static unsigned char scratch_name(unsigned char device,
                                  const char *name,
                                  unsigned char prefix_zero) {
    char cmd[24];
    unsigned char code;
    char msg[STATUS_CAP];
    unsigned char rc;

    strcpy(cmd, prefix_zero ? "s0:" : "s:");
    strcat(cmd, name);
    rc = run_command(device, cmd, &code, msg, sizeof(msg));
    dbg[0x18] = rc;
    dbg[0x19] = code;
    dbg_set_status_msg(msg);
    cprintf("SCR  D%u %s RC%u ST%u %s\r\n", device, name, rc, code, msg);
    if (rc == RC_OK || rc == RC_NOT_FOUND) {
        return RC_OK;
    }
    return rc;
}

static unsigned char dos_copy_name(unsigned char device,
                                   const char *src_name,
                                   const char *dst_name,
                                   unsigned char prefix_zero) {
    char cmd[40];
    unsigned char code;
    char msg[STATUS_CAP];
    unsigned char rc;

    strcpy(cmd, prefix_zero ? "c0:" : "c:");
    strcat(cmd, dst_name);
    strcat(cmd, "=");
    strcat(cmd, src_name);
    rc = run_command(device, cmd, &code, msg, sizeof(msg));
    dbg[0x1A] = rc;
    dbg[0x1B] = code;
    dbg_set_status_msg(msg);
    cprintf("COPY D%u %s=%s RC%u ST%u %s\r\n",
            device,
            dst_name,
            src_name,
            rc,
            code,
            msg);
    return rc;
}

static unsigned char read_seq_all(unsigned char device,
                                  const char *name,
                                  unsigned char *buf,
                                  unsigned char cap,
                                  unsigned char *out_len) {
    char spec[24];
    int n;
    unsigned char total;

    if (out_len != 0) {
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

    if (out_len != 0) {
        *out_len = total;
    }
    return RC_OK;
}

static unsigned char stream_copy_seq(unsigned char src_device,
                                     const char *src_name,
                                     unsigned char dst_device,
                                     const char *dst_name) {
    char src_spec[24];
    char dst_spec[24];
    int nread;
    int nwrote;
    unsigned char code;
    char msg[STATUS_CAP];

    strcpy(src_spec, src_name);
    strcat(src_spec, ",s,r");
    strcpy(dst_spec, dst_name);
    strcat(dst_spec, ",s,w");

    cleanup_io();
    if (cbm_open(LFN_FILE, src_device, 2, src_spec) != 0) {
        cleanup_io();
        print_drive_text(src_device, "SRC OPEN FAIL");
        dbg[0x1A] = RC_IO;
        return RC_IO;
    }
    if (cbm_open(LFN_AUX, dst_device, 2, dst_spec) != 0) {
        cbm_close(LFN_FILE);
        cleanup_io();
        print_drive_text(dst_device, "DST OPEN FAIL");
        dbg[0x1A] = RC_IO;
        return RC_IO;
    }

    while (1) {
        nread = cbm_read(LFN_FILE, file_buf_a, sizeof(file_buf_a));
        if (nread < 0) {
            cbm_close(LFN_AUX);
            cbm_close(LFN_FILE);
            cleanup_io();
            return RC_IO;
        }
        if (nread == 0) {
            break;
        }

        nwrote = cbm_write(LFN_AUX, file_buf_a, (unsigned int)nread);
        if (nwrote != nread) {
            cbm_close(LFN_AUX);
            cbm_close(LFN_FILE);
            cleanup_io();
            return RC_IO;
        }
    }

    cbm_close(LFN_AUX);
    cbm_close(LFN_FILE);
    cleanup_io();
    (void)fetch_status(dst_device, "", &code, msg, sizeof(msg));
    dbg[0x1A] = RC_OK;
    dbg[0x1B] = code;
    dbg_set_status_msg(msg);
    cprintf("STRM D%u:%s->D%u:%s ST%u %s\r\n",
            src_device,
            src_name,
            dst_device,
            dst_name,
            code,
            msg);
    return (unsigned char)(code <= 1u ? RC_OK : RC_IO);
}

static unsigned char verify_seq_match(unsigned char src_device,
                                      const char *src_name,
                                      unsigned char dst_device,
                                      const char *dst_name) {
    HarnessEntry src_entry;
    HarnessEntry dst_entry;
    unsigned char src_len;
    unsigned char dst_len;

    if (find_entry(src_device, src_name, &src_entry) != RC_OK) {
        cprintf("MISS D%u:%s\r\n", src_device, src_name);
        dbg[0x1C] = 1u;
        return 0u;
    }
    if (find_entry(dst_device, dst_name, &dst_entry) != RC_OK) {
        cprintf("MISS D%u:%s\r\n", dst_device, dst_name);
        dbg[0x1C] = 2u;
        return 0u;
    }
    dbg_store_entry(0x10u, &src_entry);
    dbg_store_entry(0x13u, &dst_entry);

    if (read_seq_all(src_device, src_name, file_buf_a, sizeof(file_buf_a), &src_len) != RC_OK) {
        cprintf("READ FAIL D%u:%s\r\n", src_device, src_name);
        dbg[0x1C] = 3u;
        return 0u;
    }
    if (read_seq_all(dst_device, dst_name, file_buf_b, sizeof(file_buf_b), &dst_len) != RC_OK) {
        cprintf("READ FAIL D%u:%s\r\n", dst_device, dst_name);
        dbg[0x1C] = 4u;
        return 0u;
    }
    dbg[0x16] = src_len;
    dbg[0x17] = dst_len;

    cprintf("VER  %s/%u -> %s/%u\r\n",
            src_name,
            src_entry.size,
            dst_name,
            dst_entry.size);

    if (src_entry.type != dst_entry.type || src_entry.size != dst_entry.size ||
        src_len != dst_len || memcmp(file_buf_a, file_buf_b, src_len) != 0) {
        print_line("VER  FAIL");
        dbg[0x1C] = 5u;
        return 0u;
    }

    print_line("VER  OK");
    dbg[0x1C] = 0u;
    return 1u;
}

static unsigned char write_named_dump_file(const char *name) {
    int rc;
    int n;
    char open_name[24];

    strcpy(open_name, name);
    strcat(open_name, ",s,w");

    (void)scratch_name(DRIVE8, name, 1u);
    rc = cbm_open(7, DRIVE8, 2, open_name);
    dbg[0x1D] = (unsigned char)((rc < 0) ? 0xFFu : (unsigned char)rc);
    if (rc != 0) {
        dbg_set_fail(STEP_DUMP, 1u);
        return 1u;
    }

    n = cbm_write(7, dbg, DBG_LEN);
    dbg[0x1E] = (unsigned char)((n < 0) ? 0xFFu : (unsigned char)n);
    cbm_close(7);
    cleanup_io();
    if (n != DBG_LEN) {
        dbg_set_fail(STEP_DUMP, 2u);
        return 1u;
    }
    return 0u;
}

static unsigned char write_debug_dump_file(void) {
    return write_named_dump_file("xfilestat");
}

static void checkpoint_dump(char stage) {
    dbg_push_stage(stage);
    (void)write_debug_dump_file();
}

static unsigned char test_probe_matrix(void) {
    unsigned char ok8;
    unsigned char ok9;
    unsigned char bits;

    print_line("");
    print_line("CASE PROBE");
    dbg_push_stage('P');
    bits = 0u;
    if (probe_cmd(DRIVE8)) bits |= 0x01u;
    if (probe_cmd(DRIVE9)) bits |= 0x02u;
    if (probe_cmd(10u)) bits |= 0x04u;
    if (probe_cmd(11u)) bits |= 0x08u;
    dbg[0x20] = bits;

    bits = 0u;
    if (probe_dir_open(DRIVE8)) bits |= 0x01u;
    if (probe_dir_open(DRIVE9)) bits |= 0x02u;
    if (probe_dir_open(10u)) bits |= 0x04u;
    if (probe_dir_open(11u)) bits |= 0x08u;
    dbg[0x21] = bits;

    bits = 0u;
    if (probe_dir_first(DRIVE8)) bits |= 0x01u;
    if (probe_dir_first(DRIVE9)) bits |= 0x02u;
    if (probe_dir_first(10u)) bits |= 0x04u;
    if (probe_dir_first(11u)) bits |= 0x08u;
    dbg[0x22] = bits;

    ok8 = (unsigned char)((dbg[0x22] & 0x01u) != 0u);
    ok9 = (unsigned char)((dbg[0x22] & 0x02u) != 0u);
    if (!ok8 || !ok9) {
        print_line("BASE PROBE FAIL");
        dbg_set_fail(STEP_PROBE, 1u);
        return 1u;
    }

    bits = 0u;
    if (probe_dir_first(DRIVE8)) bits |= 0x01u;
    if (probe_dir_first(DRIVE9)) bits |= 0x02u;
    if (probe_dir_first(10u)) bits |= 0x04u;
    if (probe_dir_first(11u)) bits |= 0x08u;
    dbg[0x23] = bits;

    ok8 = (unsigned char)((bits & 0x01u) != 0u);
    ok9 = (unsigned char)((bits & 0x02u) != 0u);
    if (!ok8 || !ok9) {
        print_line("RECOVERY FAIL");
        dbg_set_fail(STEP_PROBE, 2u);
        return 1u;
    }

    print_line("PROBE PASS");
    return 0u;
}

static unsigned char test_duplicate(unsigned char device,
                                    const char *src_name,
                                    const char *dst_name,
                                    unsigned char prefix_zero) {
    unsigned char rc;

    print_line("");
    cprintf("CASE DUP D%u %s\r\n", device, prefix_zero ? "C0:" : "C:");
    dbg_push_stage('D');
    dbg[0x0C] = device;
    dbg[0x0D] = prefix_zero;

    if (scratch_name(device, dst_name, 1u) != RC_OK) {
        dbg_set_fail(STEP_SCRATCH, 1u);
        return 1u;
    }

    rc = dos_copy_name(device, src_name, dst_name, prefix_zero);
    if (rc != RC_OK) {
        print_line("DUP CMD FAIL");
        dbg_set_fail(STEP_COPY_CMD, 1u);
        return 1u;
    }

    if (!verify_seq_match(device, src_name, device, dst_name)) {
        dbg_set_fail(STEP_VERIFY, dbg[0x1C] ? dbg[0x1C] : 1u);
        return 1u;
    }

    print_line("DUP PASS");
    return 0u;
}

static unsigned char test_stream_copy(unsigned char src_device,
                                      const char *src_name,
                                      unsigned char dst_device,
                                      const char *dst_name) {
    print_line("");
    cprintf("CASE CPY D%u->D%u\r\n", src_device, dst_device);
    dbg_push_stage('C');
    dbg[0x0C] = src_device;
    dbg[0x0D] = dst_device;

    if (scratch_name(dst_device, dst_name, 1u) != RC_OK) {
        dbg_set_fail(STEP_SCRATCH, 2u);
        return 1u;
    }
    if (stream_copy_seq(src_device, src_name, dst_device, dst_name) != RC_OK) {
        print_line("CPY FAIL");
        dbg_set_fail(STEP_COPY_IO, 1u);
        return 1u;
    }
    if (!verify_seq_match(src_device, src_name, dst_device, dst_name)) {
        dbg_set_fail(STEP_VERIFY, dbg[0x1C] ? dbg[0x1C] : 2u);
        return 1u;
    }

    print_line("CPY PASS");
    return 0u;
}

static unsigned char test_stage_duplicate(unsigned char src_device,
                                          const char *src_name,
                                          unsigned char stage_device,
                                          const char *stage_name,
                                          const char *dst_name) {
    unsigned char rc;

    print_line("");
    cprintf("CASE STAGE D%u->D%u->D%u\r\n", src_device, stage_device, src_device);
    dbg_push_stage('S');
    dbg[0x0C] = src_device;
    dbg[0x0D] = stage_device;

    if (scratch_name(stage_device, stage_name, 1u) != RC_OK) {
        dbg_set_fail(STEP_SCRATCH, 3u);
        return 1u;
    }
    if (scratch_name(src_device, dst_name, 1u) != RC_OK) {
        dbg_set_fail(STEP_SCRATCH, 4u);
        return 1u;
    }

    rc = stream_copy_seq(src_device, src_name, stage_device, stage_name);
    if (rc != RC_OK) {
        print_line("STAGE1 FAIL");
        dbg_set_fail(STEP_STAGE, 1u);
        return 1u;
    }
    if (!verify_seq_match(src_device, src_name, stage_device, stage_name)) {
        dbg_set_fail(STEP_VERIFY, dbg[0x1C] ? dbg[0x1C] : 3u);
        return 1u;
    }

    rc = stream_copy_seq(stage_device, stage_name, src_device, dst_name);
    if (rc != RC_OK) {
        print_line("STAGE2 FAIL");
        dbg_set_fail(STEP_STAGE, 2u);
        return 1u;
    }
    if (!verify_seq_match(src_device, src_name, src_device, dst_name)) {
        dbg_set_fail(STEP_VERIFY, dbg[0x1C] ? dbg[0x1C] : 4u);
        return 1u;
    }

    (void)scratch_name(stage_device, stage_name, 1u);
    print_line("STAGE PASS");
    return 0u;
}

static unsigned char probe_known_file(unsigned char device, const char *name) {
    HarnessEntry entry;
    return (unsigned char)(find_entry(device, name, &entry) == RC_OK);
}

static unsigned char run_selected_case(void) {
    switch (XFILECHK_CASE) {
        case 1:
            return test_probe_matrix();
        case 2:
            return test_duplicate(DRIVE8, "src8", "src8dup", 1u);
        case 3:
            return test_duplicate(DRIVE8, "src8", "src8dupc", 0u);
        case 4:
            return test_duplicate(DRIVE9, "testa", "testab", 1u);
        case 5:
            return test_duplicate(DRIVE9, "testa", "testac", 0u);
        case 6:
            return test_stream_copy(DRIVE8, "src8", DRIVE9, "from8");
        case 7:
            return test_stream_copy(DRIVE9, "testa", DRIVE8, "from9");
        case 8:
            return test_stage_duplicate(DRIVE9, "testa", DRIVE8, "sfstage", "testab");
        case 9:
            dbg_push_stage('Z');
            print_line("");
            print_line("CASE DUMP");
            return 0u;
        case 10:
            dbg_push_stage('H');
            print_line("");
            print_line("CASE HANG");
            while (1) {
            }
        case 11:
            dbg_push_stage('p');
            print_line("");
            print_line("CASE PCMD");
            dbg[0x20] = probe_known_file(DRIVE8, "src8");
            checkpoint_dump('a');
            dbg[0x21] = probe_known_file(DRIVE9, "testa");
            checkpoint_dump('b');
            dbg[0x22] = probe_cmd(10u);
            checkpoint_dump('c');
            dbg[0x23] = probe_known_file(DRIVE8, "src8");
            checkpoint_dump('d');
            dbg[0x24] = probe_known_file(DRIVE9, "testa");
            checkpoint_dump('e');
            if (dbg[0x20] == 0u || dbg[0x21] == 0u ||
                dbg[0x23] == 0u || dbg[0x24] == 0u) {
                dbg_set_fail(STEP_PROBE, 3u);
                return 1u;
            }
            return 0u;
        case 12:
            dbg_push_stage('o');
            print_line("");
            print_line("CASE PDOPN");
            dbg[0x20] = probe_known_file(DRIVE8, "src8");
            checkpoint_dump('a');
            dbg[0x21] = probe_known_file(DRIVE9, "testa");
            checkpoint_dump('b');
            dbg[0x22] = probe_dir_open(10u);
            checkpoint_dump('c');
            dbg[0x23] = probe_known_file(DRIVE8, "src8");
            checkpoint_dump('d');
            dbg[0x24] = probe_known_file(DRIVE9, "testa");
            checkpoint_dump('e');
            if (dbg[0x20] == 0u || dbg[0x21] == 0u ||
                dbg[0x23] == 0u || dbg[0x24] == 0u) {
                dbg_set_fail(STEP_PROBE, 4u);
                return 1u;
            }
            return 0u;
        case 13:
            dbg_push_stage('f');
            print_line("");
            print_line("CASE PDSCAN");
            dbg[0x20] = probe_known_file(DRIVE8, "src8");
            checkpoint_dump('a');
            dbg[0x21] = probe_known_file(DRIVE9, "testa");
            checkpoint_dump('b');
            dbg[0x22] = probe_dir_scan(10u);
            checkpoint_dump('c');
            dbg[0x23] = probe_known_file(DRIVE8, "src8");
            checkpoint_dump('d');
            dbg[0x24] = probe_known_file(DRIVE9, "testa");
            checkpoint_dump('e');
            if (dbg[0x20] == 0u || dbg[0x21] == 0u ||
                dbg[0x23] == 0u || dbg[0x24] == 0u) {
                dbg_set_fail(STEP_PROBE, 5u);
                return 1u;
            }
            return 0u;
        default:
            break;
    }

    if (test_probe_matrix() != 0u) {
        return 1u;
    }
    if (test_duplicate(DRIVE8, "src8", "src8dup", 1u) != 0u) {
        return 1u;
    }
    if (test_duplicate(DRIVE8, "src8", "src8dupc", 0u) != 0u) {
        return 1u;
    }
    if (test_duplicate(DRIVE9, "testa", "testab", 1u) != 0u) {
        return 1u;
    }
    if (test_duplicate(DRIVE9, "testa", "testac", 0u) != 0u) {
        return 1u;
    }
    if (test_stream_copy(DRIVE8, "src8", DRIVE9, "from8") != 0u) {
        return 1u;
    }
    if (test_stream_copy(DRIVE9, "testa", DRIVE8, "from9") != 0u) {
        return 1u;
    }
    return 0u;
}

int main(void) {
    unsigned char failed;

    clrscr();
    dbg_clear();
    print_line("XFILECHK IEC HARNESS");
    print_label_uc("CASE ", (unsigned char)XFILECHK_CASE);

    dbg_push_stage('M');
    if (set_1571_mode(DRIVE8) != 0u) {
        dbg[6] = 1u;
        dbg_set_fail(STEP_MODE, 1u);
        print_line("MODE SET FAIL");
        (void)write_debug_dump_file();
        return 1;
    }
    dbg[6] = 0u;
    if (set_1571_mode(DRIVE9) != 0u) {
        dbg[7] = 1u;
        dbg_set_fail(STEP_MODE, 2u);
        print_line("MODE SET FAIL");
        (void)write_debug_dump_file();
        return 1;
    }
    dbg[7] = 0u;
    (void)write_debug_dump_file();

    failed = run_selected_case();
    print_line("");
    if (failed) {
        print_line("RESULT FAIL");
        dbg[2] = 0u;
    } else {
        print_line("RESULT PASS");
        dbg[2] = 1u;
    }
    (void)write_debug_dump_file();
    return failed ? 1 : 0;
}
