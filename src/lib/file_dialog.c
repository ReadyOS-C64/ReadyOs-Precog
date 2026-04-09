/*
 * file_dialog.c - Shared paged file picker for ReadyOS app dialogs
 */

#include "file_dialog.h"

#include "storage_device.h"
#include "tui.h"

#include <string.h>

#define FILE_DIALOG_LIST_X        1u
#define FILE_DIALOG_LIST_Y        2u
#define FILE_DIALOG_LIST_W        38u
#define FILE_DIALOG_STATUS_Y      22u
#define FILE_DIALOG_HELP_Y        24u
#define FILE_DIALOG_COUNT_X       12u
#define FILE_DIALOG_LABEL_X       15u
#define FILE_DIALOG_ACTION_X      23u

static FileDialogState *s_state;
static const FileDialogConfig *s_cfg;

static unsigned char file_dialog_read_page(void) {
    s_state->count = 0u;
    s_state->total_count = 0u;

    if (dir_page_read(storage_device_get_default(),
                      s_state->page_start,
                      s_cfg->filter_type,
                      s_state->entries,
                      FILE_DIALOG_PAGE_SIZE,
                      &s_state->count,
                      &s_state->total_count) != DIR_PAGE_RC_OK) {
        return FILE_DIALOG_RC_IO;
    }

    return FILE_DIALOG_RC_OK;
}

static void file_dialog_draw_row(unsigned char row) {
    unsigned char abs_index;
    unsigned char color;
    unsigned char x;
    unsigned char i;
    const char *type_text;

    abs_index = (unsigned char)(s_state->page_start + row);
    color = (abs_index == s_state->selected) ? TUI_COLOR_CYAN : TUI_COLOR_WHITE;

    tui_clear_line((unsigned char)(FILE_DIALOG_LIST_Y + row),
                   FILE_DIALOG_LIST_X,
                   FILE_DIALOG_LIST_W,
                   color);

    tui_putc(FILE_DIALOG_LIST_X,
             (unsigned char)(FILE_DIALOG_LIST_Y + row),
             (abs_index == s_state->selected) ? tui_ascii_to_screen('>') : 32,
             color);

    if (row >= s_state->count) {
        return;
    }

    x = (unsigned char)(FILE_DIALOG_LIST_X + 2u);
    for (i = 0u; s_state->entries[row].name[i] != 0 &&
                 i + 1u < DIR_PAGE_NAME_LEN; ++i) {
        tui_putc((unsigned char)(x + i),
                 (unsigned char)(FILE_DIALOG_LIST_Y + row),
                 tui_ascii_to_screen((unsigned char)s_state->entries[row].name[i]),
                 color);
    }

    if (!s_cfg->show_type_suffix) {
        return;
    }

    type_text = dir_page_type_text(s_state->entries[row].type);
    tui_puts((unsigned char)(FILE_DIALOG_LIST_X + 19u),
             (unsigned char)(FILE_DIALOG_LIST_Y + row),
             type_text,
             color);
}

static void file_dialog_draw_list(void) {
    unsigned char row;

    for (row = 0u; row < FILE_DIALOG_PAGE_SIZE; ++row) {
        file_dialog_draw_row(row);
    }
}

static void file_dialog_draw_footer(unsigned char menu_ready) {
    unsigned char action_len;

    tui_clear_line(FILE_DIALOG_HELP_Y, 1u, 38u, TUI_COLOR_GRAY3);
    tui_puts(1u, FILE_DIALOG_HELP_Y, "F3:DRV", TUI_COLOR_GRAY3);
    if (!menu_ready) {
        tui_puts(8u, FILE_DIALOG_HELP_Y, "STOP:CANCEL", TUI_COLOR_GRAY3);
        return;
    }

    tui_puts(8u, FILE_DIALOG_HELP_Y, "UP/DN SEL RET:", TUI_COLOR_GRAY3);
    tui_puts(FILE_DIALOG_ACTION_X, FILE_DIALOG_HELP_Y,
             s_cfg->action_text, TUI_COLOR_GRAY3);
    action_len = (unsigned char)strlen(s_cfg->action_text);
    tui_puts((unsigned char)(FILE_DIALOG_ACTION_X + action_len),
             FILE_DIALOG_HELP_Y,
             " STOP",
             TUI_COLOR_GRAY3);
}

static void file_dialog_draw_screen(unsigned char rc) {
    TuiRect win;

    tui_clear(TUI_COLOR_BLUE);

    win.x = 0u;
    win.y = 0u;
    win.w = 40u;
    win.h = 24u;
    tui_window_title(&win, s_cfg->title, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);

    tui_puts(1u, FILE_DIALOG_STATUS_Y, "DRIVE:", TUI_COLOR_GRAY3);
    tui_print_uint(8u, FILE_DIALOG_STATUS_Y,
                   storage_device_get_default(), TUI_COLOR_CYAN);

    tui_clear_line(FILE_DIALOG_STATUS_Y, FILE_DIALOG_COUNT_X, 27u, TUI_COLOR_GRAY3);

    if (rc == FILE_DIALOG_RC_IO) {
        tui_puts(10u, 11u, "DISK READ ERROR", TUI_COLOR_LIGHTRED);
        file_dialog_draw_footer(0u);
        return;
    }

    if (s_state->total_count == 0u || s_state->count == 0u) {
        tui_puts(6u, 10u, s_cfg->empty_text, TUI_COLOR_LIGHTRED);
        file_dialog_draw_footer(0u);
        return;
    }

    tui_print_uint(FILE_DIALOG_COUNT_X, FILE_DIALOG_STATUS_Y,
                   s_state->total_count, TUI_COLOR_GRAY3);
    tui_puts(FILE_DIALOG_LABEL_X, FILE_DIALOG_STATUS_Y, "FILE(S)", TUI_COLOR_GRAY3);
    file_dialog_draw_list();
    file_dialog_draw_footer(1u);
}

unsigned char file_dialog_pick(FileDialogState *state,
                               const FileDialogConfig *cfg,
                               DirPageEntry *out_entry) {
    unsigned char key;
    unsigned char rc;
    unsigned char rel_index;

    if (state == 0 || cfg == 0 || out_entry == 0) {
        return FILE_DIALOG_RC_CANCEL;
    }

    s_state = state;
    s_cfg = cfg;
    s_state->selected = 0u;
    s_state->page_start = 0u;

    while (1) {
        tui_clear(TUI_COLOR_BLUE);
        tui_puts(10u, 11u, "READING DISK...", TUI_COLOR_YELLOW);
        rc = file_dialog_read_page();
        file_dialog_draw_screen(rc);

        while (1) {
            key = tui_getkey();

            if (key == TUI_KEY_F3) {
                storage_device_set_default(
                    storage_device_toggle_8_9(storage_device_get_default()));
                s_state->selected = 0u;
                s_state->page_start = 0u;
                break;
            }

            if (key == TUI_KEY_RUNSTOP || key == TUI_KEY_LARROW) {
                return FILE_DIALOG_RC_CANCEL;
            }

            if (rc != FILE_DIALOG_RC_OK ||
                s_state->total_count == 0u ||
                s_state->count == 0u) {
                continue;
            }

            if (key == TUI_KEY_RETURN) {
                rel_index = (unsigned char)(s_state->selected - s_state->page_start);
                strcpy(out_entry->name, s_state->entries[rel_index].name);
                out_entry->type = s_state->entries[rel_index].type;
                return FILE_DIALOG_RC_OK;
            }

            if (key == TUI_KEY_HOME) {
                if (s_state->selected != 0u || s_state->page_start != 0u) {
                    s_state->selected = 0u;
                    s_state->page_start = 0u;
                    break;
                }
                continue;
            }

            if (key == TUI_KEY_UP) {
                if (s_state->selected == 0u) {
                    continue;
                }
                --s_state->selected;
                if (s_state->selected < s_state->page_start) {
                    s_state->page_start = (unsigned char)
                        (s_state->page_start - FILE_DIALOG_PAGE_SIZE);
                    break;
                }
                file_dialog_draw_list();
                continue;
            }

            if (key == TUI_KEY_DOWN) {
                if ((unsigned char)(s_state->selected + 1u) >= s_state->total_count) {
                    continue;
                }
                ++s_state->selected;
                if ((unsigned char)(s_state->selected - s_state->page_start) >=
                    FILE_DIALOG_PAGE_SIZE) {
                    s_state->page_start = (unsigned char)
                        (s_state->page_start + FILE_DIALOG_PAGE_SIZE);
                    break;
                }
                file_dialog_draw_list();
            }
        }
    }
}
