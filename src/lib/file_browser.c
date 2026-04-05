/*
 * file_browser.c - Compact CBM disk/file helpers for ReadyOS file UIs
 */

#include "file_browser.h"

#include <cbm.h>
#include <string.h>

#define FILE_BROWSER_LFN_DIR  1
#define FILE_BROWSER_LFN_SRC  2
#define FILE_BROWSER_LFN_DST  3
#define FILE_BROWSER_LFN_CMD  15

static void file_browser_cleanup_io(void) {
    cbm_k_clrch();
    cbm_k_clall();
}

static void file_browser_set_status(char *msg_out,
                                    unsigned char msg_cap,
                                    const char *text) {
    if (msg_out == 0 || msg_cap == 0) {
        return;
    }
    strncpy(msg_out, text, (unsigned char)(msg_cap - 1));
    msg_out[msg_cap - 1] = 0;
}

static void file_browser_parse_status_line(const char *line,
                                           unsigned char *code_out,
                                           char *msg_out,
                                           unsigned char msg_cap) {
    unsigned int code;
    unsigned char i;
    const char *p;

    code = 0;
    p = line;
    while (*p >= '0' && *p <= '9') {
        code = (unsigned int)(code * 10u + (unsigned int)(*p - '0'));
        ++p;
    }

    if (code_out != 0) {
        *code_out = (code > 255u) ? 255u : (unsigned char)code;
    }

    if (msg_out == 0 || msg_cap == 0) {
        return;
    }

    if (*p == ',') {
        ++p;
    }
    while (*p == ' ') {
        ++p;
    }

    i = 0;
    while (*p != 0 && *p != ',' && *p != '\r' && *p != '\n' &&
           i + 1u < msg_cap) {
        msg_out[i++] = *p++;
    }
    msg_out[i] = 0;

    if (msg_out[0] == 0) {
        file_browser_set_status(msg_out, msg_cap, "STATUS");
    }
}

static unsigned char file_browser_read_status_open(unsigned char *code_out,
                                                   char *msg_out,
                                                   unsigned char msg_cap) {
    char status_line[40];
    int n;

    n = cbm_read(FILE_BROWSER_LFN_CMD, status_line, sizeof(status_line) - 1u);
    if (n < 0) {
        n = 0;
    }
    status_line[n] = 0;

    while (n > 0 &&
           (status_line[n - 1] == '\r' || status_line[n - 1] == '\n')) {
        status_line[n - 1] = 0;
        --n;
    }

    file_browser_parse_status_line(status_line, code_out, msg_out, msg_cap);
    return (unsigned char)n;
}

static unsigned char file_browser_type_is_regular(unsigned char type) {
    return (unsigned char)((type & _CBM_T_REG) != 0);
}

static unsigned char file_browser_upchar(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (unsigned char)(ch - ('a' - 'A'));
    }
    if ((unsigned char)ch == 0xC1u || (unsigned char)ch == 0xC2u || (unsigned char)ch == 0xC4u ||
        (unsigned char)ch == 0xC5u || (unsigned char)ch == 0xC7u || (unsigned char)ch == 0xC8u ||
        (unsigned char)ch == 0xC9u || (unsigned char)ch == 0xCCu || (unsigned char)ch == 0xCFu ||
        (unsigned char)ch == 0xD0u || (unsigned char)ch == 0xD2u || (unsigned char)ch == 0xD3u ||
        (unsigned char)ch == 0xD5u || (unsigned char)ch == 0xD8u || (unsigned char)ch == 0xD9u) {
        return (unsigned char)(ch & 0x7Fu);
    }
    return ch;
}

static unsigned char file_browser_text_has(const char *text, const char *token) {
    unsigned char i;
    unsigned char j;

    if (text == 0 || token == 0 || token[0] == 0) {
        return 0u;
    }

    for (i = 0; text[i] != 0; ++i) {
        j = 0u;
        while (token[j] != 0 && text[i + j] == token[j]) {
            ++j;
        }
        if (token[j] == 0) {
            return 1u;
        }
    }

    return 0u;
}

static unsigned char file_browser_is_blocks_free_line(const char *text) {
    return (unsigned char)(file_browser_text_has(text, "BLOCKS") &&
                           file_browser_text_has(text, "FREE"));
}

static unsigned char file_browser_type_from_text(const char *type_text) {
    unsigned char a;
    unsigned char b;
    unsigned char c;

    a = file_browser_upchar((unsigned char)type_text[0]);
    b = file_browser_upchar((unsigned char)type_text[1]);
    c = file_browser_upchar((unsigned char)type_text[2]);

    if (a == 'S' && b == 'E' && c == 'Q') return CBM_T_SEQ;
    if (a == 'P' && b == 'R' && c == 'G') return CBM_T_PRG;
    if (a == 'U' && b == 'S' && c == 'R') return CBM_T_USR;
    if (a == 'R' && b == 'E' && c == 'L') return CBM_T_REL;
    if (a == 'D' && b == 'I' && c == 'R') return CBM_T_DIR;
    if (a == 'C' && b == 'B' && c == 'M') return CBM_T_CBM;
    if (a == 'D' && b == 'E' && c == 'L') return CBM_T_DEL;
    return CBM_T_DEL;
}

static unsigned char file_browser_skip_dir_line(void) {
    unsigned char ch;
    int n;

    while (1) {
        n = cbm_read(FILE_BROWSER_LFN_DIR, &ch, 1u);
        if (n < 1 || ch == 0u) {
            break;
        }
    }
    return FILE_BROWSER_RC_OK;
}

static unsigned char file_browser_scan_directory(unsigned char device) {
    unsigned char ptr[2];
    unsigned char num[2];
    unsigned char ch;
    int n;

    file_browser_cleanup_io();
    if (cbm_open(FILE_BROWSER_LFN_DIR, device, 0, "$") != 0) {
        file_browser_cleanup_io();
        return FILE_BROWSER_RC_IO;
    }

    n = cbm_read(FILE_BROWSER_LFN_DIR, ptr, 2u);
    if (n < 2) {
        cbm_close(FILE_BROWSER_LFN_DIR);
        file_browser_cleanup_io();
        return FILE_BROWSER_RC_IO;
    }

    while (1) {
        n = cbm_read(FILE_BROWSER_LFN_DIR, ptr, 2u);
        if (n < 2) {
            cbm_close(FILE_BROWSER_LFN_DIR);
            file_browser_cleanup_io();
            return FILE_BROWSER_RC_IO;
        }
        n = cbm_read(FILE_BROWSER_LFN_DIR, num, 2u);
        if (n < 2) {
            cbm_close(FILE_BROWSER_LFN_DIR);
            file_browser_cleanup_io();
            return FILE_BROWSER_RC_IO;
        }

        while (1) {
            n = cbm_read(FILE_BROWSER_LFN_DIR, &ch, 1u);
            if (n < 1 || ch == 0u) {
                break;
            }
        }

        if (ptr[0] == 0u && ptr[1] == 0u) {
            cbm_close(FILE_BROWSER_LFN_DIR);
            file_browser_cleanup_io();
            return FILE_BROWSER_RC_OK;
        }
        if (n < 1) {
            cbm_close(FILE_BROWSER_LFN_DIR);
            file_browser_cleanup_io();
            return FILE_BROWSER_RC_IO;
        }
    }
}

static void file_browser_build_open_name(const char *name,
                                         unsigned char type,
                                         char mode,
                                         char *out) {
    unsigned char len;

    strcpy(out, name);
    len = (unsigned char)strlen(out);
    out[len] = ',';
    out[len + 1] = (char)file_browser_type_mode(type);
    out[len + 2] = ',';
    out[len + 3] = mode;
    out[len + 4] = 0;
}

static unsigned char file_browser_fetch_status(unsigned char device,
                                               unsigned char *code_out,
                                               char *msg_out,
                                               unsigned char msg_cap) {
    file_browser_cleanup_io();
    if (cbm_open(FILE_BROWSER_LFN_CMD, device, 15, "") != 0) {
        if (code_out != 0) {
            *code_out = 255;
        }
        file_browser_set_status(msg_out, msg_cap, "NO STATUS");
        return FILE_BROWSER_RC_IO;
    }

    (void)file_browser_read_status_open(code_out, msg_out, msg_cap);
    cbm_close(FILE_BROWSER_LFN_CMD);
    file_browser_cleanup_io();
    return FILE_BROWSER_RC_OK;
}

static unsigned char file_browser_run_command(unsigned char device,
                                              const char *cmd,
                                              unsigned char *code_out,
                                              char *msg_out,
                                              unsigned char msg_cap) {
    unsigned char status_code;

    file_browser_cleanup_io();
    if (cbm_open(FILE_BROWSER_LFN_CMD, device, 15, cmd) != 0) {
        if (code_out != 0) {
            *code_out = 255;
        }
        file_browser_set_status(msg_out, msg_cap, "CMD OPEN");
        file_browser_cleanup_io();
        return FILE_BROWSER_RC_IO;
    }

    status_code = 255;
    (void)file_browser_read_status_open(&status_code, msg_out, msg_cap);
    cbm_close(FILE_BROWSER_LFN_CMD);
    file_browser_cleanup_io();

    if (code_out != 0) {
        *code_out = status_code;
    }

    if (status_code == 63u) {
        return FILE_BROWSER_RC_EXISTS;
    }
    if (status_code <= 1u) {
        return FILE_BROWSER_RC_OK;
    }
    if (status_code == 62u) {
        return FILE_BROWSER_RC_NOT_FOUND;
    }
    if (status_code == 50u) {
        return FILE_BROWSER_RC_NOT_FOUND;
    }
    return FILE_BROWSER_RC_IO;
}

unsigned char file_browser_probe_drive(unsigned char device) {
    return (unsigned char)(file_browser_scan_directory(device) == FILE_BROWSER_RC_OK);
}

unsigned char file_browser_probe_drives_8_11(unsigned char *out_mask) {
    unsigned char device;
    unsigned char mask;

    mask = 0;
    for (device = 8; device <= 11; ++device) {
        if (file_browser_probe_drive(device)) {
            mask |= (unsigned char)(1u << (device - 8u));
        }
    }

    if (out_mask != 0) {
        *out_mask = mask;
    }
    return (unsigned char)(mask != 0);
}

unsigned char file_browser_read_directory(unsigned char device,
                                          FileBrowserEntry *entries,
                                          unsigned char max_entries,
                                          unsigned char *out_count,
                                          unsigned int *out_blocks_free,
                                          char *out_title) {
    unsigned char ptr[2];
    unsigned char num[2];
    unsigned char ch;
    unsigned char count;
    unsigned char first_line;
    unsigned char in_quotes;
    unsigned char name_pos;
    unsigned char type_pos;
    unsigned char past_space;
    unsigned char text_pos;
    char type_text[4];
    char line_text[16];
    unsigned int blocks;
    int n;

    count = 0;
    if (out_count != 0) {
        *out_count = 0;
    }
    if (out_blocks_free != 0) {
        *out_blocks_free = 0;
    }
    if (out_title != 0) {
        out_title[0] = 0;
    }

    file_browser_cleanup_io();
    if (cbm_open(FILE_BROWSER_LFN_DIR, device, 0, "$") != 0) {
        file_browser_cleanup_io();
        return FILE_BROWSER_RC_IO;
    }

    (void)cbm_read(FILE_BROWSER_LFN_DIR, ptr, 2u);
    first_line = 1u;

    while (1) {
        n = cbm_read(FILE_BROWSER_LFN_DIR, ptr, 2u);
        if (n < 2) {
            break;
        }
        n = cbm_read(FILE_BROWSER_LFN_DIR, num, 2u);
        if (n < 2) {
            break;
        }

        blocks = (unsigned int)(num[0] | ((unsigned int)num[1] << 8u));

        if (ptr[0] == 0u && ptr[1] == 0u) {
            if (out_blocks_free != 0) {
                *out_blocks_free = blocks;
            }
            (void)file_browser_skip_dir_line();
            break;
        }

        in_quotes = 0u;
        name_pos = 0u;
        text_pos = 0u;
        line_text[0] = 0;
        while (1) {
            n = cbm_read(FILE_BROWSER_LFN_DIR, &ch, 1u);
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
            if (!in_quotes && !first_line &&
                ch != ' ' && ch != 0xA0u &&
                text_pos + 1u < sizeof(line_text)) {
                line_text[text_pos++] = (char)file_browser_upchar(ch);
                line_text[text_pos] = 0;
            }
            if (in_quotes &&
                ((first_line && out_title != 0) ||
                 (!first_line && count < max_entries && entries != 0)) &&
                name_pos + 1u < FILE_BROWSER_TITLE_LEN) {
                if (first_line) {
                    out_title[name_pos++] = (char)ch;
                } else {
                    entries[count].name[name_pos++] = (char)ch;
                }
            }
        }

        type_pos = 0u;
        past_space = 0u;
        type_text[0] = 0;
        type_text[1] = 0;
        type_text[2] = 0;
        type_text[3] = 0;

        if (ch != 0u) {
            while (1) {
                n = cbm_read(FILE_BROWSER_LFN_DIR, &ch, 1u);
                if (n < 1 || ch == 0u) {
                    break;
                }
                if (ch != ' ' && ch != 0xA0u && text_pos + 1u < sizeof(line_text)) {
                    line_text[text_pos++] = (char)file_browser_upchar(ch);
                    line_text[text_pos] = 0;
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

        if (first_line) {
            if (out_title != 0) {
                out_title[name_pos] = 0;
            }
        } else if (name_pos == 0u) {
            if (out_blocks_free != 0 && file_browser_is_blocks_free_line(line_text)) {
                *out_blocks_free = blocks;
            }
        } else if (name_pos > 0u && count < max_entries && entries != 0) {
            entries[count].name[name_pos] = 0;
            entries[count].size = blocks;
            entries[count].type = file_browser_type_from_text(type_text);
            entries[count].access = 0u;
            ++count;
        }
        first_line = 0u;
    }

    cbm_close(FILE_BROWSER_LFN_DIR);
    file_browser_cleanup_io();

    if (out_count != 0) {
        *out_count = count;
    }
    return FILE_BROWSER_RC_OK;
}

unsigned char file_browser_read_status(unsigned char device,
                                       unsigned char *code_out,
                                       char *msg_out,
                                       unsigned char msg_cap) {
    return file_browser_fetch_status(device, code_out, msg_out, msg_cap);
}

unsigned char file_browser_scratch(unsigned char device,
                                   const char *name,
                                   unsigned char *code_out,
                                   char *msg_out,
                                   unsigned char msg_cap) {
    char cmd[24];

    strcpy(cmd, "s:");
    strcat(cmd, name);
    return file_browser_run_command(device, cmd, code_out, msg_out, msg_cap);
}

unsigned char file_browser_rename(unsigned char device,
                                  const char *old_name,
                                  const char *new_name,
                                  unsigned char *code_out,
                                  char *msg_out,
                                  unsigned char msg_cap) {
    char cmd[40];

    strcpy(cmd, "r:");
    strcat(cmd, new_name);
    strcat(cmd, "=");
    strcat(cmd, old_name);
    return file_browser_run_command(device, cmd, code_out, msg_out, msg_cap);
}

unsigned char file_browser_copy_local(unsigned char device,
                                      const char *src_name,
                                      const char *dst_name,
                                      unsigned char *code_out,
                                      char *msg_out,
                                      unsigned char msg_cap) {
    char cmd[40];

    strcpy(cmd, "c0:");
    strcat(cmd, dst_name);
    strcat(cmd, "=");
    strcat(cmd, src_name);
    return file_browser_run_command(device, cmd, code_out, msg_out, msg_cap);
}

unsigned char file_browser_copy_stream(unsigned char src_device,
                                       const FileBrowserEntry *entry,
                                       unsigned char dst_device,
                                       const char *dst_name,
                                       unsigned char *buffer,
                                       unsigned int buffer_len,
                                       unsigned char *code_out,
                                       char *msg_out,
                                       unsigned char msg_cap) {
    char src_name[24];
    char dst_name_buf[24];
    int nread;
    int nwrote;
    unsigned char rc;

    if (entry == 0 || buffer == 0 || buffer_len == 0u ||
        !file_browser_is_copyable(entry)) {
        if (code_out != 0) {
            *code_out = 255;
        }
        file_browser_set_status(msg_out, msg_cap, "UNSUPPORTED");
        return FILE_BROWSER_RC_UNSUPPORTED;
    }

    file_browser_build_open_name(entry->name, entry->type, 'r', src_name);
    file_browser_build_open_name(dst_name, entry->type, 'w', dst_name_buf);

    file_browser_cleanup_io();
    if (cbm_open(FILE_BROWSER_LFN_SRC, src_device, 2, src_name) != 0) {
        return file_browser_fetch_status(src_device, code_out, msg_out, msg_cap);
    }
    if (cbm_open(FILE_BROWSER_LFN_DST, dst_device, 2, dst_name_buf) != 0) {
        cbm_close(FILE_BROWSER_LFN_SRC);
        file_browser_cleanup_io();
        return file_browser_fetch_status(dst_device, code_out, msg_out, msg_cap);
    }

    while (1) {
        nread = cbm_read(FILE_BROWSER_LFN_SRC, buffer, buffer_len);
        if (nread < 0) {
            cbm_close(FILE_BROWSER_LFN_DST);
            cbm_close(FILE_BROWSER_LFN_SRC);
            file_browser_cleanup_io();
            return file_browser_fetch_status(src_device, code_out, msg_out, msg_cap);
        }
        if (nread == 0) {
            break;
        }

        nwrote = cbm_write(FILE_BROWSER_LFN_DST, buffer, (unsigned int)nread);
        if (nwrote != nread) {
            cbm_close(FILE_BROWSER_LFN_DST);
            cbm_close(FILE_BROWSER_LFN_SRC);
            file_browser_cleanup_io();
            return file_browser_fetch_status(dst_device, code_out, msg_out, msg_cap);
        }
    }

    cbm_close(FILE_BROWSER_LFN_DST);
    cbm_close(FILE_BROWSER_LFN_SRC);
    file_browser_cleanup_io();

    rc = file_browser_fetch_status(dst_device, code_out, msg_out, msg_cap);
    if (rc == FILE_BROWSER_RC_OK && code_out != 0 && *code_out <= 1u) {
        return FILE_BROWSER_RC_OK;
    }
    return rc;
}

unsigned char file_browser_read_page(unsigned char device,
                                     const FileBrowserEntry *entry,
                                     unsigned int offset,
                                     unsigned char *buffer,
                                     unsigned int buffer_len,
                                     unsigned int *out_len) {
    char name_buf[24];
    unsigned int skipped;
    unsigned int chunk;
    unsigned int read_total;
    int nread;

    if (out_len != 0) {
        *out_len = 0;
    }
    if (entry == 0 || buffer == 0 || buffer_len == 0u ||
        !file_browser_is_viewable(entry)) {
        return FILE_BROWSER_RC_UNSUPPORTED;
    }

    file_browser_build_open_name(entry->name, entry->type, 'r', name_buf);
    file_browser_cleanup_io();
    if (cbm_open(FILE_BROWSER_LFN_SRC, device, 2, name_buf) != 0) {
        file_browser_cleanup_io();
        return FILE_BROWSER_RC_IO;
    }

    skipped = 0;
    while (skipped < offset) {
        chunk = (unsigned int)(offset - skipped);
        if (chunk > buffer_len) {
            chunk = buffer_len;
        }
        nread = cbm_read(FILE_BROWSER_LFN_SRC, buffer, chunk);
        if (nread <= 0) {
            cbm_close(FILE_BROWSER_LFN_SRC);
            file_browser_cleanup_io();
            return FILE_BROWSER_RC_OK;
        }
        skipped = (unsigned int)(skipped + (unsigned int)nread);
    }

    read_total = 0;
    while (read_total < buffer_len) {
        nread = cbm_read(FILE_BROWSER_LFN_SRC,
                         buffer + read_total,
                         (unsigned int)(buffer_len - read_total));
        if (nread < 0) {
            cbm_close(FILE_BROWSER_LFN_SRC);
            file_browser_cleanup_io();
            return FILE_BROWSER_RC_IO;
        }
        if (nread == 0) {
            break;
        }
        read_total = (unsigned int)(read_total + (unsigned int)nread);
    }

    cbm_close(FILE_BROWSER_LFN_SRC);
    file_browser_cleanup_io();

    if (out_len != 0) {
        *out_len = read_total;
    }
    return FILE_BROWSER_RC_OK;
}

unsigned char file_browser_type_marker(unsigned char type) {
    switch (type) {
        case CBM_T_SEQ: return 'S';
        case CBM_T_PRG: return 'P';
        case CBM_T_USR: return 'U';
        case CBM_T_REL: return 'R';
        case CBM_T_DIR: return 'D';
        case CBM_T_CBM: return 'B';
        case CBM_T_DEL: return '-';
        default:        return '?';
    }
}

unsigned char file_browser_type_mode(unsigned char type) {
    switch (type) {
        case CBM_T_PRG: return 'p';
        case CBM_T_USR: return 'u';
        case CBM_T_REL: return 'l';
        default:        return 's';
    }
}

unsigned char file_browser_is_copyable(const FileBrowserEntry *entry) {
    if (entry == 0) {
        return 0;
    }
    if (!file_browser_type_is_regular(entry->type)) {
        return 0;
    }
    return (unsigned char)(entry->type != CBM_T_REL);
}

unsigned char file_browser_is_mutable(const FileBrowserEntry *entry) {
    if (entry == 0) {
        return 0;
    }
    return (unsigned char)(entry->type != CBM_T_HEADER && entry->type != CBM_T_DIR &&
                           entry->type != CBM_T_CBM);
}

unsigned char file_browser_is_viewable(const FileBrowserEntry *entry) {
    if (entry == 0) {
        return 0;
    }
    return (unsigned char)(entry->type == CBM_T_SEQ);
}
