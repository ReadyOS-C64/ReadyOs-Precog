#ifndef READYSHELL_HOST_FS_H
#define READYSHELL_HOST_FS_H

#include "src/apps/readyshell/core/rs_vm.h"

#define READYSHELL_HOST_FS_TYPE_BINARY 1u
#define READYSHELL_HOST_FS_TYPE_SEQ    2u

typedef struct RSHostFSView {
  unsigned char drive;
  unsigned char type;
  const unsigned char* bytes;
  unsigned short len;
} RSHostFSView;

void readyshell_host_fs_reset(void);

int readyshell_host_fs_parse_path(const char* spec,
                                  unsigned char fallback_drive,
                                  unsigned char* out_drive,
                                  char* out_name,
                                  unsigned short max_name);

int readyshell_host_fs_store_bytes(unsigned char drive,
                                   const char* name,
                                   unsigned char type,
                                   const unsigned char* src,
                                   unsigned short len);

int readyshell_host_fs_get_view(unsigned char drive,
                                const char* name,
                                RSHostFSView* out_view);

int readyshell_host_fs_delete_file(unsigned char drive, const char* name);
int readyshell_host_fs_rename_file(unsigned char drive,
                                   const char* src_name,
                                   const char* dst_name);
int readyshell_host_fs_copy_file(unsigned char src_drive,
                                 const char* src_name,
                                 unsigned char dst_drive,
                                 const char* dst_name);

int readyshell_host_fs_write_seq_lines(unsigned char drive,
                                       const char* name,
                                       const char* const* lines,
                                       unsigned short count,
                                       int append);

int readyshell_host_fs_seq_line_count(unsigned char drive,
                                      const char* name,
                                      unsigned short* out_count);

int readyshell_host_fs_seq_line_copy(unsigned char drive,
                                     const char* name,
                                     unsigned short index,
                                     char* out,
                                     unsigned short max);

int readyshell_host_fs_snapshot_read_cb(void* user,
                                        const char* path,
                                        unsigned char* dst,
                                        unsigned short max,
                                        unsigned short* out_len);

int readyshell_host_fs_snapshot_write_cb(void* user,
                                         const char* path,
                                         const unsigned char* src,
                                         unsigned short len);

#endif
