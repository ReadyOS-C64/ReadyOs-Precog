/*
 * dir_page.c - Tiny paged CBM directory reader for ReadyOS loaders
 */

#include "dir_page.h"

#include <cbm.h>
#include <string.h>

#define DIR_PAGE_LFN_DIR 1
#define DIR_PAGE_LFN_CMD 15

static void dir_page_cleanup_io(void) {
    cbm_k_clrch();
    cbm_k_clall();
}

static void dir_page_maybe_set_1571_mode(unsigned char device) {
    if (device != 8u && device != 9u) {
        return;
    }

    dir_page_cleanup_io();
    if (cbm_open(DIR_PAGE_LFN_CMD, device, 15, "u0>m1") == 0) {
        cbm_close(DIR_PAGE_LFN_CMD);
    }
    dir_page_cleanup_io();
}

static unsigned char dir_page_upchar(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (unsigned char)(ch - ('a' - 'A'));
    }
    if (ch == 0xC1u || ch == 0xC2u || ch == 0xC4u || ch == 0xC5u ||
        ch == 0xC7u || ch == 0xC8u || ch == 0xC9u || ch == 0xCCu ||
        ch == 0xCFu || ch == 0xD0u || ch == 0xD2u || ch == 0xD3u ||
        ch == 0xD5u || ch == 0xD8u || ch == 0xD9u) {
        return (unsigned char)(ch & 0x7Fu);
    }
    return ch;
}

static unsigned char dir_page_type_from_text(const char *type_text) {
    unsigned char a;
    unsigned char b;
    unsigned char c;

    a = dir_page_upchar((unsigned char)type_text[0]);
    b = dir_page_upchar((unsigned char)type_text[1]);
    c = dir_page_upchar((unsigned char)type_text[2]);

    if (a == 'S' && b == 'E' && c == 'Q') return CBM_T_SEQ;
    if (a == 'P' && b == 'R' && c == 'G') return CBM_T_PRG;
    if (a == 'U' && b == 'S' && c == 'R') return CBM_T_USR;
    if (a == 'R' && b == 'E' && c == 'L') return CBM_T_REL;
    if (a == 'D' && b == 'I' && c == 'R') return CBM_T_DIR;
    if (a == 'C' && b == 'B' && c == 'M') return CBM_T_CBM;
    if (a == 'D' && b == 'E' && c == 'L') return CBM_T_DEL;
    return CBM_T_DEL;
}

static unsigned char dir_page_type_matches(unsigned char filter_type,
                                           unsigned char type) {
    return (unsigned char)(filter_type == DIR_PAGE_TYPE_ANY || filter_type == type);
}

unsigned char dir_page_read(unsigned char device,
                            unsigned char start_index,
                            unsigned char filter_type,
                            DirPageEntry *entries,
                            unsigned char max_entries,
                            unsigned char *out_count,
                            unsigned char *out_total_count) {
    unsigned char ptr[2];
    unsigned char num[2];
    unsigned char ch;
    unsigned char count;
    unsigned char total_count;
    unsigned char first_line;
    unsigned char in_quotes;
    unsigned char name_pos;
    unsigned char type_pos;
    unsigned char past_space;
    unsigned char entry_type;
    char name_text[DIR_PAGE_NAME_LEN];
    char type_text[4];
    int n;

    count = 0u;
    total_count = 0u;
    if (out_count != 0) {
        *out_count = 0u;
    }
    if (out_total_count != 0) {
        *out_total_count = 0u;
    }

    dir_page_maybe_set_1571_mode(device);
    dir_page_cleanup_io();
    if (cbm_open(DIR_PAGE_LFN_DIR, device, 0, "$") != 0) {
        dir_page_cleanup_io();
        return DIR_PAGE_RC_IO;
    }

    (void)cbm_read(DIR_PAGE_LFN_DIR, ptr, 2u);
    first_line = 1u;

    while (1) {
        n = cbm_read(DIR_PAGE_LFN_DIR, ptr, 2u);
        if (n < 2) {
            break;
        }
        n = cbm_read(DIR_PAGE_LFN_DIR, num, 2u);
        if (n < 2) {
            break;
        }
        if (ptr[0] == 0u && ptr[1] == 0u) {
            while (1) {
                n = cbm_read(DIR_PAGE_LFN_DIR, &ch, 1u);
                if (n < 1 || ch == 0u) {
                    break;
                }
            }
            break;
        }

        in_quotes = 0u;
        name_pos = 0u;
        name_text[0] = 0;
        while (1) {
            n = cbm_read(DIR_PAGE_LFN_DIR, &ch, 1u);
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
            if (in_quotes && name_pos + 1u < DIR_PAGE_NAME_LEN) {
                name_text[name_pos++] = (char)ch;
            }
        }
        name_text[name_pos] = 0;

        type_pos = 0u;
        past_space = 0u;
        type_text[0] = 0;
        type_text[1] = 0;
        type_text[2] = 0;
        type_text[3] = 0;

        if (ch != 0u) {
            while (1) {
                n = cbm_read(DIR_PAGE_LFN_DIR, &ch, 1u);
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

        if (first_line) {
            first_line = 0u;
            continue;
        }
        if (name_pos == 0u) {
            continue;
        }

        entry_type = dir_page_type_from_text(type_text);
        if (!dir_page_type_matches(filter_type, entry_type)) {
            continue;
        }

        if (total_count >= start_index && count < max_entries && entries != 0) {
            strcpy(entries[count].name, name_text);
            entries[count].type = entry_type;
            ++count;
        }
        if (total_count != 255u) {
            ++total_count;
        }
    }

    cbm_close(DIR_PAGE_LFN_DIR);
    dir_page_cleanup_io();

    if (out_count != 0) {
        *out_count = count;
    }
    if (out_total_count != 0) {
        *out_total_count = total_count;
    }
    return DIR_PAGE_RC_OK;
}

const char *dir_page_type_text(unsigned char type) {
    switch (type) {
        case CBM_T_SEQ: return "SEQ";
        case CBM_T_PRG: return "PRG";
        case CBM_T_USR: return "USR";
        case CBM_T_REL: return "REL";
        case CBM_T_DIR: return "DIR";
        case CBM_T_CBM: return "CBM";
        case CBM_T_DEL: return "DEL";
        default:        return "???";
    }
}

unsigned char dir_page_type_mode(unsigned char type) {
    switch (type) {
        case CBM_T_PRG: return 'p';
        case CBM_T_USR: return 'u';
        case CBM_T_REL: return 'l';
        default:        return 's';
    }
}
