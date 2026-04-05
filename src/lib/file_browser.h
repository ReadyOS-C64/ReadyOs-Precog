/*
 * file_browser.h - Compact CBM disk/file helpers for ReadyOS file UIs
 */

#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <cbm_filetype.h>

#define FILE_BROWSER_NAME_LEN 17

#define FILE_BROWSER_RC_OK            0
#define FILE_BROWSER_RC_IO            1
#define FILE_BROWSER_RC_UNSUPPORTED   2
#define FILE_BROWSER_RC_NOT_FOUND     3
#define FILE_BROWSER_RC_EXISTS        4

typedef struct {
    char name[FILE_BROWSER_NAME_LEN];
    unsigned int size;
    unsigned char type;
    unsigned char access;
} FileBrowserEntry;

unsigned char file_browser_probe_drive(unsigned char device);
unsigned char file_browser_probe_drives_8_11(unsigned char *out_mask);
unsigned char file_browser_read_directory(unsigned char device,
                                          FileBrowserEntry *entries,
                                          unsigned char max_entries,
                                          unsigned char *out_count,
                                          unsigned int *out_blocks_free);
unsigned char file_browser_read_status(unsigned char device,
                                       unsigned char *code_out,
                                       char *msg_out,
                                       unsigned char msg_cap);
unsigned char file_browser_scratch(unsigned char device,
                                   const char *name,
                                   unsigned char *code_out,
                                   char *msg_out,
                                   unsigned char msg_cap);
unsigned char file_browser_rename(unsigned char device,
                                  const char *old_name,
                                  const char *new_name,
                                  unsigned char *code_out,
                                  char *msg_out,
                                  unsigned char msg_cap);
unsigned char file_browser_copy_local(unsigned char device,
                                      const char *src_name,
                                      const char *dst_name,
                                      unsigned char *code_out,
                                      char *msg_out,
                                      unsigned char msg_cap);
unsigned char file_browser_copy_stream(unsigned char src_device,
                                       const FileBrowserEntry *entry,
                                       unsigned char dst_device,
                                       const char *dst_name,
                                       unsigned char *buffer,
                                       unsigned int buffer_len,
                                       unsigned char *code_out,
                                       char *msg_out,
                                       unsigned char msg_cap);
unsigned char file_browser_read_page(unsigned char device,
                                     const FileBrowserEntry *entry,
                                     unsigned int offset,
                                     unsigned char *buffer,
                                     unsigned int buffer_len,
                                     unsigned int *out_len);
unsigned char file_browser_type_marker(unsigned char type);
unsigned char file_browser_type_mode(unsigned char type);
unsigned char file_browser_is_copyable(const FileBrowserEntry *entry);
unsigned char file_browser_is_mutable(const FileBrowserEntry *entry);
unsigned char file_browser_is_viewable(const FileBrowserEntry *entry);

#endif /* FILE_BROWSER_H */
