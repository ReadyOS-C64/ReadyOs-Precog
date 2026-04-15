/*
 * clipmgr.c - Ready OS Clipboard Manager
 * View and manage multi-item clipboard
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include "../../lib/clipboard.h"
#include "../../lib/dir_page.h"
#include "../../lib/reu_mgr.h"
#include "../../lib/resume_state.h"
#include "../../lib/storage_device.h"
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define TITLE_Y      0
#define LIST_START_Y 4
#define LIST_HEIGHT  16
#define DETAIL_Y     20
#define HELP_Y       22
#define STATUS_Y     24

/* Max chars to preview per item */
#define PREVIEW_LEN  25

/* Full-content popup window */
#define VIEW_BOX_X       1
#define VIEW_BOX_Y       2
#define VIEW_BOX_W       38
#define VIEW_BOX_H       20
#define VIEW_TEXT_X      (VIEW_BOX_X + 1)
#define VIEW_TEXT_Y      (VIEW_BOX_Y + 3)
#define VIEW_TEXT_COLS   (VIEW_BOX_W - 2)
#define VIEW_TEXT_LINES  15
#define VIEW_PAGE_BYTES  (VIEW_TEXT_COLS * VIEW_TEXT_LINES)

/* File I/O */
#define MAX_DIR_ENTRIES 18
#define DIR_NAME_LEN    17
#define IO_BUF_SIZE     128
#define LFN_DIR         1
#define LFN_FILE        2
#define LFN_CMD         15
#define CLIP_MAX_BYTES  65535U

#define CLIP_BUNDLE_MAGIC0     'R'
#define CLIP_BUNDLE_MAGIC1     'C'
#define CLIP_BUNDLE_MAGIC2     'L'
#define CLIP_BUNDLE_MAGIC3     'P'
#define CLIP_BUNDLE_MAGIC4     '1'
#define CLIP_BUNDLE_VERSION    1u
#define CLIP_BUNDLE_HEADER_LEN 8u
#define CLIP_BUNDLE_RECORD_LEN 3u

#define LOAD_RC_OK            0u
#define LOAD_RC_IO            1u
#define LOAD_RC_FULL          2u
#define LOAD_RC_NO_REU        3u
#define LOAD_RC_PARTIAL_FULL  4u
#define LOAD_RC_PARTIAL_REU   5u
#define OPEN_DIALOG_RC_OK      0u
#define OPEN_DIALOG_RC_CANCEL  1u

#define SHIM_CURRENT_BANK (*(volatile unsigned char*)0xC834)

/*---------------------------------------------------------------------------
 * Static variables
 *---------------------------------------------------------------------------*/

static unsigned char running;
static unsigned char selected;
static unsigned char scroll_offset;

/* Temp buffer for previewing clipboard item data */
static char preview_buf[42];
static union {
    struct {
        DirPageEntry dir_entries[MAX_DIR_ENTRIES];
        char dir_display[MAX_DIR_ENTRIES][21];
        const char *dir_ptrs[MAX_DIR_ENTRIES];
        unsigned char dir_count;
    } browser;
    unsigned char view_buf[VIEW_PAGE_BYTES];
    char save_buf[17];
} file_scratch;
static char view_line_buf[VIEW_TEXT_COLS + 1];
static char io_buf[IO_BUF_SIZE];
static unsigned char bundle_hdr[CLIP_BUNDLE_HEADER_LEN];
static unsigned char bundle_rec[CLIP_BUNDLE_RECORD_LEN];
static unsigned char staged_banks[CLIP_MAX_ITEMS];
static unsigned int staged_sizes[CLIP_MAX_ITEMS];

typedef struct {
    unsigned char selected;
    unsigned char scroll_offset;
} ClipMgrResumeV1;

static ClipMgrResumeV1 resume_blob;
static unsigned char resume_ready;

static void resume_save_state(void) {
    if (!resume_ready) {
        return;
    }
    resume_blob.selected = selected;
    resume_blob.scroll_offset = scroll_offset;
    (void)resume_save(&resume_blob, sizeof(resume_blob));
}

static unsigned char resume_restore_state(void) {
    unsigned char count;
    unsigned int payload_len = 0;

    if (!resume_ready) {
        return 0;
    }
    if (!resume_try_load(&resume_blob, sizeof(resume_blob), &payload_len)) {
        return 0;
    }
    if (payload_len != sizeof(resume_blob)) {
        return 0;
    }

    count = clip_item_count();
    if (count == 0) {
        selected = 0;
        scroll_offset = 0;
        return 1;
    }

    selected = resume_blob.selected;
    scroll_offset = resume_blob.scroll_offset;

    if (selected >= count) {
        selected = (unsigned char)(count - 1);
    }
    if (scroll_offset > selected) {
        scroll_offset = selected;
    }
    if (selected >= scroll_offset + LIST_HEIGHT) {
        scroll_offset = (unsigned char)(selected - LIST_HEIGHT + 1);
    }
    return 1;
}

/*---------------------------------------------------------------------------
 * Drawing
 *---------------------------------------------------------------------------*/

static void draw_header(void) {
    TuiRect box = {0, TITLE_Y, 40, 3};
    tui_window_title(&box, "CLIPBOARD MANAGER",
                     TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    /* Item count */
    tui_puts(2, TITLE_Y + 1, "ITEMS: ", TUI_COLOR_WHITE);
    tui_print_uint(9, TITLE_Y + 1, clip_item_count(), TUI_COLOR_CYAN);
    tui_puts(12, TITLE_Y + 1, "/16", TUI_COLOR_GRAY3);
}

static void draw_column_header(void) {
    tui_puts(1, 3, "#  TYPE  SIZE  PREVIEW", TUI_COLOR_GRAY3);
}

static unsigned char selection_is_marked(unsigned int mask, unsigned char item_idx) {
    return (unsigned char)((mask & ((unsigned int)1u << item_idx)) != 0u);
}

static unsigned char selection_count(unsigned int mask, unsigned char count) {
    unsigned char idx;
    unsigned char marked;

    marked = 0u;
    for (idx = 0u; idx < count; ++idx) {
        if (selection_is_marked(mask, idx)) {
            ++marked;
        }
    }
    return marked;
}

static unsigned int selection_all_mask(unsigned char count) {
    unsigned char idx;
    unsigned int mask;

    mask = 0u;
    for (idx = 0u; idx < count; ++idx) {
        mask |= (unsigned int)1u << idx;
    }
    return mask;
}

static void draw_item(unsigned char list_row, unsigned char item_idx,
                      unsigned char cursor_idx, unsigned char save_mode,
                      unsigned int select_mask) {
    unsigned char y;
    unsigned char color;
    unsigned int size;
    unsigned int fetched;
    unsigned char plen;
    unsigned char pi;

    y = LIST_START_Y + list_row;

    if (item_idx >= clip_item_count()) {
        /* Empty row */
        tui_clear_line(y, 0, 40, TUI_COLOR_WHITE);
        return;
    }

    color = (item_idx == cursor_idx) ? TUI_COLOR_CYAN : TUI_COLOR_WHITE;
    tui_clear_line(y, 0, 40, color);

    /* Selection indicator */
    if (item_idx == cursor_idx) {
        tui_putc(0, y, 0x3E, color);  /* '>' */
    } else {
        tui_putc(0, y, 32, color);    /* space */
    }

    if (save_mode) {
        tui_puts(1, y, selection_is_marked(select_mask, item_idx) ? "[x]" : "[ ]", color);
        tui_print_uint(5, y, item_idx + 1, color);
        tui_puts(7, y, ".", color);
    } else {
        tui_print_uint(1, y, item_idx + 1, color);
        tui_puts(3, y, ".", color);
    }

    /* Item number (1-based) */
    /* Type */
    if (clip_get_type(item_idx) == CLIP_TYPE_TEXT) {
        tui_puts(save_mode ? 9 : 5, y, "TXT", color);
    } else {
        tui_puts(save_mode ? 9 : 5, y, "???", color);
    }

    /* Size */
    size = clip_get_size(item_idx);
    if (size < 1000) {
        tui_puts_n(save_mode ? 13 : 10, y, "", 5, color);
        tui_print_uint(save_mode ? 13 : 10, y, size, color);
        tui_puts(save_mode ? 17 : 14, y, "B", color);
    } else {
        tui_puts_n(save_mode ? 13 : 10, y, "", 5, color);
        tui_print_uint(save_mode ? 13 : 10, y, size / 1024, color);
        tui_puts(save_mode ? 17 : 14, y, "K", color);
    }

    /* Preview: fetch first PREVIEW_LEN bytes */
    memset(preview_buf, 0, sizeof(preview_buf));
    fetched = clip_paste(item_idx, preview_buf, PREVIEW_LEN);

    /* Sanitize for display: replace non-printable chars */
    plen = (unsigned char)(fetched < PREVIEW_LEN ? fetched : PREVIEW_LEN);
    for (pi = 0; pi < plen; ++pi) {
        if (preview_buf[pi] < 32 || preview_buf[pi] >= 128) {
            preview_buf[pi] = '.';
        }
    }
    preview_buf[plen] = 0;

    tui_puts(save_mode ? 19 : 16, y, "\"", color);
    tui_puts_n(save_mode ? 20 : 17, y, preview_buf, save_mode ? 17 : 20, color);
    if (fetched > (save_mode ? 17u : 20u)) {
        tui_puts(37, y, "...", TUI_COLOR_GRAY3);
    } else {
        tui_puts((save_mode ? 20 : 17) + plen, y, "\"", color);
        /* Pad rest */
        tui_puts_n((save_mode ? 21 : 18) + plen, y, "", 40 - (save_mode ? 21 : 18) - plen, color);
    }
}

static void draw_list(void) {
    unsigned char row;
    unsigned char count;

    count = clip_item_count();

    for (row = 0; row < LIST_HEIGHT; ++row) {
        unsigned char idx = scroll_offset + row;
        if (idx < count) {
            draw_item(row, idx, selected, 0u, 0u);
        } else {
            tui_clear_line(LIST_START_Y + row, 0, 40, TUI_COLOR_WHITE);
        }
    }
}

static void draw_detail_for_item(unsigned char item_idx) {
    unsigned int size;
    unsigned int fetched;
    unsigned char i;

    tui_clear_line(DETAIL_Y, 0, 40, TUI_COLOR_WHITE);
    tui_clear_line(DETAIL_Y + 1, 0, 40, TUI_COLOR_WHITE);

    if (clip_item_count() == 0 || item_idx >= clip_item_count()) {
        tui_puts(4, DETAIL_Y, "CLIPBOARD IS EMPTY", TUI_COLOR_GRAY3);
        return;
    }

    /* Show full first 38 chars */
    size = clip_get_size(item_idx);
    memset(preview_buf, 0, sizeof(preview_buf));
    fetched = clip_paste(item_idx, preview_buf, 38);

    /* Sanitize */
    for (i = 0; i < (unsigned char)fetched; ++i) {
        if (preview_buf[i] < 32 || preview_buf[i] >= 128) {
            preview_buf[i] = '.';
        }
    }
    preview_buf[fetched] = 0;

    tui_puts_n(1, DETAIL_Y, preview_buf, 38, TUI_COLOR_LIGHTGREEN);

    /* Size info */
    tui_puts(1, DETAIL_Y + 1, "SIZE:", TUI_COLOR_GRAY3);
    tui_print_uint(7, DETAIL_Y + 1, size, TUI_COLOR_WHITE);
    tui_puts(13, DETAIL_Y + 1, "BYTES", TUI_COLOR_GRAY3);
}

static void draw_detail(void) {
    draw_detail_for_item(selected);
}

static void draw_help(void) {
    tui_puts(0, HELP_Y, "RET:VIEW F5:SAVE SET F7:LOAD DEL:DEL", TUI_COLOR_GRAY3);
    tui_puts(0, HELP_Y + 1, "C:CLEAR ALL F2/F4:APPS CTRL+B:HOME", TUI_COLOR_GRAY3);
}

static void draw_status(void) {
    unsigned char free_banks;
    free_banks = reu_count_free();
    tui_puts_n(0, STATUS_Y, "FREE REU BANKS: ", 16, TUI_COLOR_GRAY3);
    tui_print_uint(16, STATUS_Y, free_banks, TUI_COLOR_WHITE);
    tui_puts_n(20, STATUS_Y, "", 20, TUI_COLOR_WHITE);
}

static unsigned char clip_item_bank(unsigned char index) {
    unsigned char *entry;
    entry = CLIP_TABLE + ((unsigned int)index * 8);
    return entry[0];
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

static unsigned char file_write_exact(unsigned char lfn, const void *data, unsigned int len) {
    const unsigned char *ptr;
    unsigned int total;
    int nw;

    ptr = (const unsigned char*)data;
    total = 0u;
    while (total < len) {
        nw = cbm_write(lfn, ptr + total, len - total);
        if (nw <= 0) {
            return 0u;
        }
        total += (unsigned int)nw;
    }
    return 1u;
}

static unsigned char file_read_exact(unsigned char lfn, void *data, unsigned int len) {
    unsigned char *ptr;
    unsigned int total;
    int nr;

    ptr = (unsigned char*)data;
    total = 0u;
    while (total < len) {
        nr = cbm_read(lfn, ptr + total, len - total);
        if (nr <= 0) {
            return 0u;
        }
        total += (unsigned int)nr;
    }
    return 1u;
}

static unsigned char file_skip_exact(unsigned char lfn, unsigned int len) {
    unsigned int chunk;

    while (len > 0u) {
        chunk = (len < IO_BUF_SIZE) ? len : IO_BUF_SIZE;
        if (!file_read_exact(lfn, io_buf, chunk)) {
            return 0u;
        }
        len -= chunk;
    }
    return 1u;
}

static void build_dir_display(unsigned char idx) {
    unsigned char i;
    unsigned char len;
    const char *type_text;

    strcpy(file_scratch.browser.dir_display[idx],
           file_scratch.browser.dir_entries[idx].name);
    len = strlen(file_scratch.browser.dir_display[idx]);

    for (i = len; i < 17u; ++i) {
        file_scratch.browser.dir_display[idx][i] = ' ';
    }

    type_text = dir_page_type_text(file_scratch.browser.dir_entries[idx].type);
    file_scratch.browser.dir_display[idx][17] = type_text[0];
    file_scratch.browser.dir_display[idx][18] = type_text[1];
    file_scratch.browser.dir_display[idx][19] = type_text[2];
    file_scratch.browser.dir_display[idx][20] = 0;
}

static unsigned char read_directory(unsigned char start_index,
                                    unsigned char *out_total) {
    unsigned char idx;

    file_scratch.browser.dir_count = 0u;
    if (dir_page_read(storage_device_get_default(),
                      start_index,
                      DIR_PAGE_TYPE_ANY,
                      file_scratch.browser.dir_entries,
                      MAX_DIR_ENTRIES,
                      &file_scratch.browser.dir_count,
                      out_total) != DIR_PAGE_RC_OK) {
        if (out_total != 0) {
            *out_total = 0u;
        }
        return 0u;
    }

    for (idx = 0u; idx < file_scratch.browser.dir_count; ++idx) {
        build_dir_display(idx);
        file_scratch.browser.dir_ptrs[idx] = file_scratch.browser.dir_display[idx];
    }

    return 1u;
}

static unsigned char show_open_dialog(DirPageEntry *out_entry) {
    TuiRect win;
    TuiMenu menu;
    unsigned char key;
    unsigned char selected_idx;
    unsigned char page_start;
    unsigned char total_count;
    unsigned char rel_index;

    selected_idx = 0u;
    page_start = 0u;
    total_count = 0u;

    while (1) {
        tui_clear(TUI_COLOR_BLUE);

        win.x = 0;
        win.y = 0;
        win.w = 40;
        win.h = 24;
        tui_window_title(&win, "LOAD TO CLIPBOARD",
                         TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

        tui_puts(10, 11, "READING DISK...", TUI_COLOR_YELLOW);
        (void)read_directory(page_start, &total_count);
        tui_clear_line(11, 1, 38, TUI_COLOR_WHITE);
        tui_puts(1, 22, "DRIVE:", TUI_COLOR_GRAY3);
        tui_print_uint(8, 22, storage_device_get_default(), TUI_COLOR_CYAN);

        if (total_count == 0u || file_scratch.browser.dir_count == 0u) {
            tui_puts(6, 10, "NO FILES FOUND ON DISK", TUI_COLOR_LIGHTRED);
            tui_puts(1, 24, "F3:DRV STOP:CANCEL", TUI_COLOR_GRAY3);
        } else {
            tui_print_uint(12, 22, total_count, TUI_COLOR_GRAY3);
            tui_puts(15, 22, "FILE(S)", TUI_COLOR_GRAY3);
            tui_puts(1, 24, "UP/DN SEL F3:DRV RET:LOAD STOP", TUI_COLOR_GRAY3);

            tui_menu_init(&menu, 1, 2, 38, 18,
                          file_scratch.browser.dir_ptrs,
                          file_scratch.browser.dir_count);
            menu.selected = (unsigned char)(selected_idx - page_start);
            menu.item_color = TUI_COLOR_WHITE;
            menu.sel_color = TUI_COLOR_CYAN;
            tui_menu_draw(&menu);
        }

        while (1) {
            key = tui_getkey();

            if (key == TUI_KEY_F3) {
                storage_device_set_default(
                    storage_device_toggle_8_9(storage_device_get_default()));
                selected_idx = 0u;
                page_start = 0u;
                break;
            }
            if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
                return OPEN_DIALOG_RC_CANCEL;
            }
            if (key == TUI_KEY_RETURN) {
                if (file_scratch.browser.dir_count == 0u) {
                    continue;
                }
                rel_index = (unsigned char)(selected_idx - page_start);
                strcpy(out_entry->name,
                       file_scratch.browser.dir_entries[rel_index].name);
                out_entry->type = file_scratch.browser.dir_entries[rel_index].type;
                return OPEN_DIALOG_RC_OK;
            }
            if (key == TUI_KEY_HOME) {
                if (selected_idx != 0u || page_start != 0u) {
                    selected_idx = 0u;
                    page_start = 0u;
                    break;
                }
                continue;
            }
            if (key == TUI_KEY_UP) {
                if (selected_idx > 0u) {
                    --selected_idx;
                    if (selected_idx < page_start) {
                        page_start = (unsigned char)(page_start - MAX_DIR_ENTRIES);
                        break;
                    }
                    menu.selected = (unsigned char)(selected_idx - page_start);
                    tui_menu_draw(&menu);
                }
                continue;
            }
            if (key == TUI_KEY_DOWN) {
                if ((unsigned char)(selected_idx + 1u) < total_count) {
                    ++selected_idx;
                    if (selected_idx >=
                        (unsigned char)(page_start + file_scratch.browser.dir_count)) {
                        page_start = (unsigned char)(page_start + MAX_DIR_ENTRIES);
                        break;
                    }
                    menu.selected = (unsigned char)(selected_idx - page_start);
                    tui_menu_draw(&menu);
                }
                continue;
            }
        }
    }
}

static unsigned char clip_insert_bank_item(unsigned char bank, unsigned int size) {
    unsigned char count;
    unsigned char *entry;

    count = *CLIP_COUNT;
    if (count >= CLIP_MAX_ITEMS) {
        return 1;
    }

    if (count > 0) {
        memmove(CLIP_TABLE + 8, CLIP_TABLE, (unsigned int)count * 8);
    }

    entry = CLIP_TABLE;
    entry[0] = bank;
    entry[1] = CLIP_TYPE_TEXT;
    entry[2] = (unsigned char)(size & 0xFF);
    entry[3] = (unsigned char)(size >> 8);
    entry[4] = 0;
    entry[5] = 0;
    entry[6] = 0;
    entry[7] = 0;

    *CLIP_COUNT = count + 1;
    return 0;
}

static void free_staged_entries(unsigned char staged_count) {
    while (staged_count > 0u) {
        --staged_count;
        reu_free_bank(staged_banks[staged_count]);
        staged_banks[staged_count] = 0xFFu;
        staged_sizes[staged_count] = 0u;
    }
}

static unsigned char bundle_magic_matches(const unsigned char *hdr) {
    return (unsigned char)(
        hdr[0] == CLIP_BUNDLE_MAGIC0 &&
        hdr[1] == CLIP_BUNDLE_MAGIC1 &&
        hdr[2] == CLIP_BUNDLE_MAGIC2 &&
        hdr[3] == CLIP_BUNDLE_MAGIC3 &&
        hdr[4] == CLIP_BUNDLE_MAGIC4);
}

/* Load selected file into one clipboard slot, capped to 65535 bytes. */
static unsigned char file_load_raw_to_clipboard(const char *name, unsigned char type,
                                                unsigned char *truncated_out) {
    char open_str[24];
    unsigned char len;
    unsigned char bank;
    unsigned int total;
    unsigned int n_u;
    unsigned int remaining;
    int n;
    unsigned char truncated;

    *truncated_out = 0;
    truncated = 0;

    if (*CLIP_COUNT >= CLIP_MAX_ITEMS) {
        return 2;
    }

    bank = reu_alloc_bank(REU_CLIPBOARD);
    if (bank == 0xFF) {
        return 3;
    }

    strcpy(open_str, name);
    len = strlen(open_str);
    open_str[len] = ',';
    open_str[len + 1] = dir_page_type_mode(type);
    open_str[len + 2] = ',';
    open_str[len + 3] = 'r';
    open_str[len + 4] = 0;

    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        reu_free_bank(bank);
        return 1;
    }

    total = 0;
    while (1) {
        n = cbm_read(LFN_FILE, io_buf, IO_BUF_SIZE);
        if (n <= 0) break;

        n_u = (unsigned int)(unsigned char)n;
        remaining = CLIP_MAX_BYTES - total;
        if (n_u > remaining) {
            n_u = remaining;
            truncated = 1;
        }

        if (n_u > 0) {
            reu_dma_stash((unsigned int)io_buf, bank, total, n_u);
            total += n_u;
        }

        if (truncated || total >= CLIP_MAX_BYTES) {
            break;
        }
    }

    cbm_close(LFN_FILE);

    if (clip_insert_bank_item(bank, total) != 0) {
        reu_free_bank(bank);
        return 2;
    }

    *truncated_out = truncated;
    return 0;
}

static unsigned char file_load_bundle_open(unsigned char *loaded_count_out) {
    unsigned char entry_count;
    unsigned char staged_count;
    unsigned char idx;
    unsigned char bank;
    unsigned char rc;
    unsigned char avail_slots;
    unsigned char item_type;
    unsigned int item_size;
    unsigned int offset;
    unsigned int chunk;

    *loaded_count_out = 0u;
    entry_count = bundle_hdr[6];
    avail_slots = (unsigned char)(CLIP_MAX_ITEMS - *CLIP_COUNT);
    staged_count = 0u;
    rc = LOAD_RC_OK;

    for (idx = 0u; idx < entry_count; ++idx) {
        if (!file_read_exact(LFN_FILE, bundle_rec, CLIP_BUNDLE_RECORD_LEN)) {
            cbm_close(LFN_FILE);
            free_staged_entries(staged_count);
            return LOAD_RC_IO;
        }

        item_type = bundle_rec[0];
        item_size = (unsigned int)bundle_rec[1] | ((unsigned int)bundle_rec[2] << 8);
        if (item_type != CLIP_TYPE_TEXT || item_size == 0u) {
            cbm_close(LFN_FILE);
            free_staged_entries(staged_count);
            return LOAD_RC_IO;
        }

        if (staged_count < avail_slots && rc == LOAD_RC_OK) {
            bank = reu_alloc_bank(REU_CLIPBOARD);
            if (bank == 0xFFu) {
                rc = (staged_count > 0u) ? LOAD_RC_PARTIAL_REU : LOAD_RC_NO_REU;
                if (!file_skip_exact(LFN_FILE, item_size)) {
                    cbm_close(LFN_FILE);
                    free_staged_entries(staged_count);
                    return LOAD_RC_IO;
                }
                continue;
            }

            offset = 0u;
            while (offset < item_size) {
                chunk = item_size - offset;
                if (chunk > IO_BUF_SIZE) {
                    chunk = IO_BUF_SIZE;
                }
                if (!file_read_exact(LFN_FILE, io_buf, chunk)) {
                    cbm_close(LFN_FILE);
                    reu_free_bank(bank);
                    free_staged_entries(staged_count);
                    return LOAD_RC_IO;
                }
                reu_dma_stash((unsigned int)io_buf, bank, offset, chunk);
                offset += chunk;
            }

            staged_banks[staged_count] = bank;
            staged_sizes[staged_count] = item_size;
            ++staged_count;
        } else {
            if (rc == LOAD_RC_OK) {
                rc = (staged_count > 0u) ? LOAD_RC_PARTIAL_FULL : LOAD_RC_FULL;
            }
            if (!file_skip_exact(LFN_FILE, item_size)) {
                cbm_close(LFN_FILE);
                free_staged_entries(staged_count);
                return LOAD_RC_IO;
            }
        }
    }

    cbm_close(LFN_FILE);

    while (staged_count > 0u) {
        --staged_count;
        if (clip_insert_bank_item(staged_banks[staged_count],
                                  staged_sizes[staged_count]) != 0u) {
            reu_free_bank(staged_banks[staged_count]);
            return LOAD_RC_IO;
        }
        ++(*loaded_count_out);
    }

    return rc;
}

static unsigned char file_load_to_clipboard(const char *name, unsigned char type,
                                            unsigned char *truncated_out,
                                            unsigned char *loaded_count_out) {
    char open_str[24];
    int n;

    *truncated_out = 0u;
    *loaded_count_out = 0u;

    if (type != CBM_T_SEQ) {
        return file_load_raw_to_clipboard(name, type, truncated_out);
    }

    strcpy(open_str, name);
    strcat(open_str, ",s,r");
    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return LOAD_RC_IO;
    }

    n = cbm_read(LFN_FILE, bundle_hdr, CLIP_BUNDLE_HEADER_LEN);
    if (n >= 5 && bundle_magic_matches(bundle_hdr)) {
        if (n != CLIP_BUNDLE_HEADER_LEN ||
            bundle_hdr[5] != CLIP_BUNDLE_VERSION ||
            bundle_hdr[6] == 0u ||
            bundle_hdr[6] > CLIP_MAX_ITEMS ||
            bundle_hdr[7] != 0u) {
            cbm_close(LFN_FILE);
            return LOAD_RC_IO;
        }
        return file_load_bundle_open(loaded_count_out);
    }

    cbm_close(LFN_FILE);
    return file_load_raw_to_clipboard(name, type, truncated_out);
}

static unsigned char file_save_clip_bundle(const char *name, unsigned int select_mask) {
    char cmd_str[24];
    char open_str[24];
    unsigned char count;
    unsigned char item_idx;
    unsigned int size;
    unsigned int remain;
    unsigned int offset;
    unsigned int chunk;
    unsigned char bank;

    count = selection_count(select_mask, clip_item_count());
    if (count == 0u) {
        return 1u;
    }

    strcpy(cmd_str, "s:");
    strcat(cmd_str, name);
    cbm_open(LFN_CMD, storage_device_get_default(), 15, cmd_str);
    cbm_close(LFN_CMD);

    strcpy(open_str, name);
    strcat(open_str, ",s,w");

    if (cbm_open(LFN_FILE, storage_device_get_default(), 2, open_str) != 0) {
        return 1;
    }

    bundle_hdr[0] = CLIP_BUNDLE_MAGIC0;
    bundle_hdr[1] = CLIP_BUNDLE_MAGIC1;
    bundle_hdr[2] = CLIP_BUNDLE_MAGIC2;
    bundle_hdr[3] = CLIP_BUNDLE_MAGIC3;
    bundle_hdr[4] = CLIP_BUNDLE_MAGIC4;
    bundle_hdr[5] = CLIP_BUNDLE_VERSION;
    bundle_hdr[6] = count;
    bundle_hdr[7] = 0u;

    if (!file_write_exact(LFN_FILE, bundle_hdr, CLIP_BUNDLE_HEADER_LEN)) {
        cbm_close(LFN_FILE);
        return 1u;
    }

    count = clip_item_count();
    for (item_idx = 0u; item_idx < count; ++item_idx) {
        if (!selection_is_marked(select_mask, item_idx)) {
            continue;
        }

        size = clip_get_size(item_idx);
        bundle_rec[0] = clip_get_type(item_idx);
        bundle_rec[1] = (unsigned char)(size & 0xFFu);
        bundle_rec[2] = (unsigned char)(size >> 8);
        if (!file_write_exact(LFN_FILE, bundle_rec, CLIP_BUNDLE_RECORD_LEN)) {
            cbm_close(LFN_FILE);
            return 1u;
        }

        bank = clip_item_bank(item_idx);
        offset = 0u;
        while (offset < size) {
            remain = size - offset;
            chunk = (remain < IO_BUF_SIZE) ? remain : IO_BUF_SIZE;
            reu_dma_fetch((unsigned int)io_buf, bank, offset, chunk);
            if (!file_write_exact(LFN_FILE, io_buf, chunk)) {
                cbm_close(LFN_FILE);
                return 1u;
            }
            offset += chunk;
        }
    }

    cbm_close(LFN_FILE);
    return 0u;
}

static void lowercase_filename_in_place(char *name) {
    while (*name != 0) {
        if (*name >= 'A' && *name <= 'Z') {
            *name = (char)(*name - 'A' + 'a');
        }
        ++name;
    }
}

static void build_default_save_name(void) {
    strcpy(file_scratch.save_buf, "clipset");
}

static void draw_save_select_header(unsigned int select_mask) {
    TuiRect box = {0, TITLE_Y, 40, 3};
    unsigned char count;

    count = clip_item_count();
    tui_window_title(&box, "SAVE CLIPBOARD SET",
                     TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    tui_puts(2, TITLE_Y + 1, "MARKED:", TUI_COLOR_WHITE);
    tui_print_uint(10, TITLE_Y + 1, selection_count(select_mask, count), TUI_COLOR_CYAN);
    tui_puts(14, TITLE_Y + 1, "ITEMS:", TUI_COLOR_WHITE);
    tui_print_uint(21, TITLE_Y + 1, count, TUI_COLOR_CYAN);
    tui_puts(24, TITLE_Y + 1, "/16", TUI_COLOR_GRAY3);
}

static void draw_save_select_help(void) {
    tui_puts(0, HELP_Y, "SPC:TOGGLE A:ALL C:CLEAR RET:FILENAME", TUI_COLOR_GRAY3);
    tui_puts(0, HELP_Y + 1, "F5:SAVE STOP:BACK CTRL+B:HOME", TUI_COLOR_GRAY3);
}

static void draw_save_select_screen(unsigned char cursor, unsigned char scroll,
                                    unsigned int select_mask) {
    unsigned char row;
    unsigned char count;

    tui_clear(TUI_COLOR_BLUE);
    draw_save_select_header(select_mask);
    tui_puts(0, 3, " SEL # TYPE  SIZE  PREVIEW", TUI_COLOR_GRAY3);

    count = clip_item_count();
    for (row = 0u; row < LIST_HEIGHT; ++row) {
        unsigned char idx = scroll + row;
        if (idx < count) {
            draw_item(row, idx, cursor, 1u, select_mask);
        } else {
            tui_clear_line(LIST_START_Y + row, 0, 40, TUI_COLOR_WHITE);
        }
    }

    draw_detail_for_item(cursor);
    draw_save_select_help();
    draw_status();
}

static unsigned char show_save_select_dialog(unsigned int *select_mask_out) {
    unsigned char count;
    unsigned char cursor;
    unsigned char scroll;
    unsigned char key;
    unsigned int select_mask;

    count = clip_item_count();
    if (count == 0u) {
        return 0u;
    }

    cursor = (selected < count) ? selected : 0u;
    scroll = scroll_offset;
    if (scroll > cursor) {
        scroll = cursor;
    }
    if (cursor >= scroll + LIST_HEIGHT) {
        scroll = (unsigned char)(cursor - LIST_HEIGHT + 1u);
    }
    select_mask = (unsigned int)1u << cursor;

    while (1) {
        draw_save_select_screen(cursor, scroll, select_mask);
        key = tui_getkey();

        if (key == TUI_KEY_UP) {
            if (cursor > 0u) {
                --cursor;
                if (cursor < scroll) {
                    scroll = cursor;
                }
            }
            continue;
        }
        if (key == TUI_KEY_DOWN) {
            if (cursor + 1u < count) {
                ++cursor;
                if (cursor >= scroll + LIST_HEIGHT) {
                    scroll = (unsigned char)(cursor - LIST_HEIGHT + 1u);
                }
            }
            continue;
        }
        if (key == TUI_KEY_HOME) {
            cursor = 0u;
            scroll = 0u;
            continue;
        }
        if (key == ' ') {
            select_mask ^= (unsigned int)1u << cursor;
            continue;
        }
        if (key == 'a' || key == 'A') {
            select_mask = selection_all_mask(count);
            continue;
        }
        if (key == 'c' || key == 'C') {
            select_mask = 0u;
            continue;
        }
        if (key == TUI_KEY_RETURN || key == TUI_KEY_F5) {
            if (selection_count(select_mask, count) == 0u) {
                show_message("SELECT ITEMS", TUI_COLOR_LIGHTRED);
                continue;
            }
            selected = cursor;
            scroll_offset = scroll;
            *select_mask_out = select_mask;
            return 1u;
        }
        if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            selected = cursor;
            scroll_offset = scroll;
            return 0u;
        }
    }
}

static unsigned char show_save_dialog(unsigned int select_mask) {
    TuiRect win;
    TuiInput input;
    unsigned char key;

    tui_clear(TUI_COLOR_BLUE);

    win.x = 5;
    win.y = 7;
    win.w = 30;
    win.h = 8;
    tui_window_title(&win, "SAVE CLIP AS SEQ", TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(7, 10, "FILENAME:", TUI_COLOR_WHITE);

    tui_input_init(&input, 7, 11, 20, 16, file_scratch.save_buf, TUI_COLOR_CYAN);
    build_default_save_name();
    input.cursor = strlen(file_scratch.save_buf);
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
            return 0u;
        }

        if (tui_input_key(&input, key)) {
            if (file_scratch.save_buf[0] == 0) {
                continue;
            }

            lowercase_filename_in_place(file_scratch.save_buf);
            tui_puts(7, 12, "SAVING...", TUI_COLOR_YELLOW);
            if (file_save_clip_bundle(file_scratch.save_buf, select_mask) != 0u) {
                show_message("SAVE ERROR!", TUI_COLOR_LIGHTRED);
                return 0u;
            }
            return 1u;
        }

        tui_input_draw(&input);
    }
}

static unsigned char sanitize_byte(unsigned char b) {
    if (b >= 32 && b < 128) return b;
    return '.';
}

static void draw_view_page(unsigned char item_idx, unsigned int page_offset) {
    TuiRect box = {VIEW_BOX_X, VIEW_BOX_Y, VIEW_BOX_W, VIEW_BOX_H};
    unsigned int size;
    unsigned int remain;
    unsigned int fetched;
    unsigned int src;
    unsigned int end_off;
    unsigned int page_no;
    unsigned int page_total;
    unsigned int i;
    unsigned char row;
    unsigned char col;
    unsigned char ch;
    unsigned char bank;

    size = clip_get_size(item_idx);
    remain = (page_offset < size) ? (size - page_offset) : 0;
    fetched = (remain < VIEW_PAGE_BYTES) ? remain : VIEW_PAGE_BYTES;

    tui_window(&box, TUI_COLOR_CYAN);
    tui_puts_n(3, VIEW_BOX_Y, "CLIP ITEM VIEW", 20, TUI_COLOR_YELLOW);

    tui_puts_n(VIEW_TEXT_X, VIEW_BOX_Y + 1, "BYTE ", 5, TUI_COLOR_GRAY3);
    tui_print_uint(VIEW_TEXT_X + 5, VIEW_BOX_Y + 1, page_offset, TUI_COLOR_WHITE);
    tui_puts_n(VIEW_TEXT_X + 10, VIEW_BOX_Y + 1, "-", 1, TUI_COLOR_GRAY3);

    end_off = page_offset;
    if (fetched > 0) {
        end_off = page_offset + fetched - 1;
    }
    tui_print_uint(VIEW_TEXT_X + 11, VIEW_BOX_Y + 1, end_off, TUI_COLOR_WHITE);
    tui_puts_n(VIEW_TEXT_X + 17, VIEW_BOX_Y + 1, " OF ", 4, TUI_COLOR_GRAY3);
    tui_print_uint(VIEW_TEXT_X + 21, VIEW_BOX_Y + 1, size, TUI_COLOR_WHITE);

    page_no = (page_offset / VIEW_PAGE_BYTES) + 1;
    page_total = (size == 0) ? 1 : ((size - 1) / VIEW_PAGE_BYTES) + 1;
    tui_puts_n(VIEW_TEXT_X + 28, VIEW_BOX_Y + 1, "P", 1, TUI_COLOR_GRAY3);
    tui_print_uint(VIEW_TEXT_X + 29, VIEW_BOX_Y + 1, page_no, TUI_COLOR_WHITE);
    tui_puts_n(VIEW_TEXT_X + 32, VIEW_BOX_Y + 1, "/", 1, TUI_COLOR_GRAY3);
    tui_print_uint(VIEW_TEXT_X + 33, VIEW_BOX_Y + 1, page_total, TUI_COLOR_WHITE);

    for (row = 0; row < VIEW_TEXT_LINES; ++row) {
        tui_puts_n(VIEW_TEXT_X, VIEW_TEXT_Y + row, "", VIEW_TEXT_COLS,
                   TUI_COLOR_LIGHTGREEN);
    }

    if (fetched == 0) {
        tui_puts_n(VIEW_TEXT_X, VIEW_TEXT_Y, "(EMPTY)", VIEW_TEXT_COLS, TUI_COLOR_GRAY3);
    } else {
        bank = clip_item_bank(item_idx);
        reu_dma_fetch((unsigned int)file_scratch.view_buf, bank, page_offset, fetched);

        src = 0;
        for (row = 0; row < VIEW_TEXT_LINES; ++row) {
            memset(view_line_buf, ' ', VIEW_TEXT_COLS);
            view_line_buf[VIEW_TEXT_COLS] = 0;

            col = 0;
            while (col < VIEW_TEXT_COLS && src < fetched) {
                ch = file_scratch.view_buf[src++];
                if (ch == 13 || ch == 10) {
                    if (ch == 13 && src < fetched && file_scratch.view_buf[src] == 10) {
                        ++src;  /* Coalesce CRLF */
                    }
                    break;
                }
                view_line_buf[col++] = (char)sanitize_byte(ch);
            }

            tui_puts_n(VIEW_TEXT_X, VIEW_TEXT_Y + row, view_line_buf, VIEW_TEXT_COLS,
                       TUI_COLOR_LIGHTGREEN);

            if (src >= fetched) {
                for (i = (unsigned int)(row + 1); i < VIEW_TEXT_LINES; ++i) {
                    tui_puts_n(VIEW_TEXT_X, VIEW_TEXT_Y + (unsigned char)i, "", VIEW_TEXT_COLS,
                               TUI_COLOR_LIGHTGREEN);
                }
                break;
            }
        }
    }

    tui_puts_n(VIEW_TEXT_X, VIEW_BOX_Y + VIEW_BOX_H - 2,
               "UP/DN:PAGE  RET/STOP:BACK", VIEW_TEXT_COLS, TUI_COLOR_GRAY3);
}

static void show_full_item_view(unsigned char item_idx) {
    unsigned char key;
    unsigned int size;
    unsigned int page_offset;

    size = clip_get_size(item_idx);
    page_offset = 0;

    while (1) {
        draw_view_page(item_idx, page_offset);
        key = tui_getkey();

        if (key == TUI_KEY_UP) {
            if (page_offset >= VIEW_PAGE_BYTES) {
                page_offset -= VIEW_PAGE_BYTES;
            } else {
                page_offset = 0;
            }
            continue;
        }

        if (key == TUI_KEY_DOWN) {
            if (page_offset + VIEW_PAGE_BYTES < size) {
                page_offset += VIEW_PAGE_BYTES;
            }
            continue;
        }

        if (key == TUI_KEY_RETURN || key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
            break;
        }
    }
}

static void clipmgr_draw(void) {
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    draw_column_header();
    draw_list();
    draw_detail();
    draw_help();
    draw_status();
}

/*---------------------------------------------------------------------------
 * Input handling
 *---------------------------------------------------------------------------*/

static void clipmgr_loop(void) {
    unsigned char key;
    unsigned char count;
    unsigned char truncated;
    unsigned char loaded_count;
    unsigned char rc;
    unsigned char nav_action;
    unsigned int save_select_mask;

    clipmgr_draw();

    while (running) {
        key = tui_getkey();
        count = clip_item_count();
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

        switch (key) {
            case TUI_KEY_UP:
                if (selected > 0) {
                    --selected;
                    if (selected < scroll_offset) {
                        scroll_offset = selected;
                    }
                    draw_list();
                    draw_detail();
                }
                break;

            case TUI_KEY_DOWN:
                if (count > 0 && selected < count - 1) {
                    ++selected;
                    if (selected >= scroll_offset + LIST_HEIGHT) {
                        scroll_offset = selected - LIST_HEIGHT + 1;
                    }
                    draw_list();
                    draw_detail();
                }
                break;

            case TUI_KEY_DEL:
                if (count > 0 && selected < count) {
                    clip_delete(selected);
                    count = clip_item_count();
                    if (selected >= count && selected > 0) {
                        --selected;
                    }
                    clipmgr_draw();
                }
                break;

            case TUI_KEY_RETURN:
                if (count > 0 && selected < count) {
                    show_full_item_view(selected);
                    clipmgr_draw();
                }
                break;

            case TUI_KEY_F7:
                {
                    DirPageEntry selected_entry;

                    if (show_open_dialog(&selected_entry) != OPEN_DIALOG_RC_OK) {
                        clipmgr_draw();
                        break;
                    }
                    tui_clear(TUI_COLOR_BLUE);
                    tui_puts(13, 12, "LOADING...", TUI_COLOR_YELLOW);
                    rc = file_load_to_clipboard(selected_entry.name,
                                                selected_entry.type,
                                                &truncated,
                                                &loaded_count);
                    if (rc == 0) {
                        selected = 0;
                        scroll_offset = 0;
                        if (loaded_count > 1u) {
                            show_message("IMPORTED", TUI_COLOR_LIGHTGREEN);
                        } else if (loaded_count == 1u) {
                            show_message("IMPORTED", TUI_COLOR_LIGHTGREEN);
                        } else if (truncated) {
                            show_message("LOADED (TRUNC 64K)", TUI_COLOR_YELLOW);
                        } else {
                            show_message("LOADED", TUI_COLOR_LIGHTGREEN);
                        }
                    } else if (rc == 2) {
                        show_message("CLIPBOARD FULL", TUI_COLOR_LIGHTRED);
                    } else if (rc == 3) {
                        show_message("NO REU SPACE", TUI_COLOR_LIGHTRED);
                    } else if (rc == LOAD_RC_PARTIAL_FULL || rc == LOAD_RC_PARTIAL_REU) {
                        selected = 0;
                        scroll_offset = 0;
                        show_message("IMPORT PARTIAL", TUI_COLOR_YELLOW);
                    } else {
                        show_message("LOAD ERROR!", TUI_COLOR_LIGHTRED);
                    }
                }
                clipmgr_draw();
                break;

            case TUI_KEY_F5:
                if (count == 0 || selected >= count) {
                    show_message("CLIPBOARD EMPTY", TUI_COLOR_LIGHTRED);
                    clipmgr_draw();
                    break;
                }
                if (show_save_select_dialog(&save_select_mask)) {
                    if (show_save_dialog(save_select_mask)) {
                        show_message("SAVED", TUI_COLOR_LIGHTGREEN);
                    }
                }
                clipmgr_draw();
                break;

            case 'c':
            case 'C':
                if (count > 0) {
                    clip_clear();
                    selected = 0;
                    scroll_offset = 0;
                    clipmgr_draw();
                }
                break;

            case TUI_KEY_RUNSTOP:
                running = 0;
                break;
        }
    }

    __asm__("jmp $FCE2");
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

int main(void) {
    unsigned char bank;

    tui_init();
    reu_mgr_init();

    selected = 0;
    scroll_offset = 0;
    resume_ready = 0;
    bank = SHIM_CURRENT_BANK;
    if (bank >= 1 && bank <= 23) {
        resume_init_for_app(bank, bank, RESUME_SCHEMA_V1);
        resume_ready = 1;
    }
    (void)resume_restore_state();
    running = 1;

    clipmgr_loop();
    return 0;
}
