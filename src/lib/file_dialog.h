/*
 * file_dialog.h - Shared paged file picker for ReadyOS app dialogs
 */

#ifndef FILE_DIALOG_H
#define FILE_DIALOG_H

#include "dir_page.h"

#define FILE_DIALOG_RC_OK      0u
#define FILE_DIALOG_RC_CANCEL  1u
#define FILE_DIALOG_RC_IO      2u

#define FILE_DIALOG_PAGE_SIZE  18u

typedef struct {
    const char *title;
    const char *action_text;
    const char *empty_text;
    unsigned char filter_type;
    unsigned char show_type_suffix;
} FileDialogConfig;

typedef struct {
    DirPageEntry entries[FILE_DIALOG_PAGE_SIZE];
    unsigned char count;
    unsigned char total_count;
    unsigned char selected;
    unsigned char page_start;
} FileDialogState;

unsigned char file_dialog_pick(FileDialogState *state,
                               const FileDialogConfig *cfg,
                               DirPageEntry *out_entry);

#endif /* FILE_DIALOG_H */
