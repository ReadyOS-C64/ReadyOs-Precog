/*
 * dir_page.h - Tiny paged CBM directory reader for ReadyOS loaders
 */

#ifndef DIR_PAGE_H
#define DIR_PAGE_H

#include <cbm_filetype.h>

#define DIR_PAGE_NAME_LEN 17
#define DIR_PAGE_RC_OK    0
#define DIR_PAGE_RC_IO    1
#define DIR_PAGE_TYPE_ANY 0xFFu

typedef struct {
    char name[DIR_PAGE_NAME_LEN];
    unsigned char type;
} DirPageEntry;

unsigned char dir_page_read(unsigned char device,
                            unsigned char start_index,
                            unsigned char filter_type,
                            DirPageEntry *entries,
                            unsigned char max_entries,
                            unsigned char *out_count,
                            unsigned char *out_total_count);
const char *dir_page_type_text(unsigned char type);
unsigned char dir_page_type_mode(unsigned char type);

#endif /* DIR_PAGE_H */
