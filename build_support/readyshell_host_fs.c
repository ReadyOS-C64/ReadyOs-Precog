#include "build_support/readyshell_host_fs.h"

#include <stdlib.h>
#include <string.h>

#define RS_HOST_FS_MAX_FILES 48u
#define RS_HOST_FS_NAME_MAX  32u

typedef struct RSHostFSFile {
  unsigned char in_use;
  unsigned char drive;
  unsigned char type;
  char name[RS_HOST_FS_NAME_MAX];
  unsigned char* bytes;
  unsigned short len;
} RSHostFSFile;

static RSHostFSFile g_files[RS_HOST_FS_MAX_FILES];

static unsigned char host_ci_char(unsigned char ch) {
  if (ch >= 'a' && ch <= 'z') {
    return (unsigned char)(ch - ('a' - 'A'));
  }
  return ch;
}

static int host_ci_equal(const char* a, const char* b) {
  unsigned char ca;
  unsigned char cb;
  if (!a || !b) {
    return 0;
  }
  do {
    ca = host_ci_char((unsigned char)*a++);
    cb = host_ci_char((unsigned char)*b++);
    if (ca != cb) {
      return 0;
    }
  } while (ca != '\0');
  return 1;
}

static RSHostFSFile* host_find_slot(unsigned char drive, const char* name) {
  unsigned short i;
  for (i = 0u; i < RS_HOST_FS_MAX_FILES; ++i) {
    if (g_files[i].in_use &&
        g_files[i].drive == drive &&
        host_ci_equal(g_files[i].name, name)) {
      return &g_files[i];
    }
  }
  return 0;
}

static RSHostFSFile* host_alloc_slot(void) {
  unsigned short i;
  for (i = 0u; i < RS_HOST_FS_MAX_FILES; ++i) {
    if (!g_files[i].in_use) {
      return &g_files[i];
    }
  }
  return 0;
}

static int host_set_bytes(RSHostFSFile* file,
                          unsigned char drive,
                          const char* name,
                          unsigned char type,
                          const unsigned char* src,
                          unsigned short len) {
  unsigned char* bytes;
  unsigned short name_len;
  if (!file || !name) {
    return -1;
  }
  name_len = (unsigned short)strlen(name);
  if (name_len == 0u || name_len >= RS_HOST_FS_NAME_MAX) {
    return -1;
  }
  bytes = 0;
  if (len > 0u) {
    bytes = (unsigned char*)malloc((size_t)len);
    if (!bytes) {
      return -1;
    }
    memcpy(bytes, src, len);
  }
  free(file->bytes);
  file->bytes = bytes;
  file->len = len;
  file->drive = drive;
  file->type = type;
  memcpy(file->name, name, (size_t)name_len + 1u);
  file->in_use = 1u;
  return 0;
}

void readyshell_host_fs_reset(void) {
  unsigned short i;
  for (i = 0u; i < RS_HOST_FS_MAX_FILES; ++i) {
    free(g_files[i].bytes);
    memset(&g_files[i], 0, sizeof(g_files[i]));
  }
}

int readyshell_host_fs_parse_path(const char* spec,
                                  unsigned char fallback_drive,
                                  unsigned char* out_drive,
                                  char* out_name,
                                  unsigned short max_name) {
  unsigned int drive;
  unsigned short pos;
  unsigned short name_len;
  if (!spec || !out_drive || !out_name || max_name == 0u) {
    return -1;
  }
  drive = 0u;
  pos = 0u;
  while (spec[pos] >= '0' && spec[pos] <= '9') {
    drive = (drive * 10u) + (unsigned int)(spec[pos] - '0');
    ++pos;
  }
  if (pos > 0u && spec[pos] == ':') {
    if (drive < 8u || drive > 11u) {
      return -1;
    }
    *out_drive = (unsigned char)drive;
    ++pos;
  } else {
    *out_drive = fallback_drive;
    pos = 0u;
  }
  name_len = (unsigned short)strlen(spec + pos);
  if (name_len == 0u || name_len >= max_name) {
    return -1;
  }
  memcpy(out_name, spec + pos, (size_t)name_len + 1u);
  return 0;
}

int readyshell_host_fs_store_bytes(unsigned char drive,
                                   const char* name,
                                   unsigned char type,
                                   const unsigned char* src,
                                   unsigned short len) {
  RSHostFSFile* file;
  if ((type != READYSHELL_HOST_FS_TYPE_BINARY &&
       type != READYSHELL_HOST_FS_TYPE_SEQ) ||
      drive < 8u || drive > 11u ||
      (!src && len > 0u)) {
    return -1;
  }
  file = host_find_slot(drive, name);
  if (!file) {
    file = host_alloc_slot();
  }
  if (!file) {
    return -1;
  }
  return host_set_bytes(file, drive, name, type, src, len);
}

int readyshell_host_fs_get_view(unsigned char drive,
                                const char* name,
                                RSHostFSView* out_view) {
  RSHostFSFile* file;
  if (!out_view) {
    return -1;
  }
  file = host_find_slot(drive, name);
  if (!file) {
    return -1;
  }
  out_view->drive = file->drive;
  out_view->type = file->type;
  out_view->bytes = file->bytes;
  out_view->len = file->len;
  return 0;
}

int readyshell_host_fs_delete_file(unsigned char drive, const char* name) {
  RSHostFSFile* file;
  file = host_find_slot(drive, name);
  if (!file) {
    return -1;
  }
  free(file->bytes);
  memset(file, 0, sizeof(*file));
  return 0;
}

int readyshell_host_fs_rename_file(unsigned char drive,
                                   const char* src_name,
                                   const char* dst_name) {
  RSHostFSFile* src;
  RSHostFSFile* clash;
  unsigned short name_len;
  if (!src_name || !dst_name) {
    return -1;
  }
  src = host_find_slot(drive, src_name);
  if (!src) {
    return -1;
  }
  clash = host_find_slot(drive, dst_name);
  if (clash && clash != src) {
    return -1;
  }
  name_len = (unsigned short)strlen(dst_name);
  if (name_len == 0u || name_len >= RS_HOST_FS_NAME_MAX) {
    return -1;
  }
  memcpy(src->name, dst_name, (size_t)name_len + 1u);
  return 0;
}

int readyshell_host_fs_copy_file(unsigned char src_drive,
                                 const char* src_name,
                                 unsigned char dst_drive,
                                 const char* dst_name) {
  RSHostFSFile* src;
  src = host_find_slot(src_drive, src_name);
  if (!src) {
    return -1;
  }
  return readyshell_host_fs_store_bytes(dst_drive,
                                        dst_name,
                                        src->type,
                                        src->bytes,
                                        src->len);
}

int readyshell_host_fs_write_seq_lines(unsigned char drive,
                                       const char* name,
                                       const char* const* lines,
                                       unsigned short count,
                                       int append) {
  RSHostFSView view;
  unsigned char* bytes;
  unsigned short old_len;
  unsigned short i;
  unsigned short line_len;
  unsigned short total;
  unsigned short pos;

  if (!name || (count > 0u && !lines)) {
    return -1;
  }

  old_len = 0u;
  if (append) {
    if (readyshell_host_fs_get_view(drive, name, &view) == 0) {
      if (view.type != READYSHELL_HOST_FS_TYPE_SEQ) {
        return -1;
      }
      old_len = view.len;
    }
  }

  total = old_len;
  for (i = 0u; i < count; ++i) {
    if (!lines[i]) {
      return -1;
    }
    line_len = (unsigned short)strlen(lines[i]);
    if ((unsigned long)total + (unsigned long)line_len + 1ul > 65535ul) {
      return -1;
    }
    total = (unsigned short)(total + line_len + 1u);
  }

  bytes = 0;
  if (total > 0u) {
    bytes = (unsigned char*)malloc((size_t)total);
    if (!bytes) {
      return -1;
    }
  }
  pos = 0u;
  if (append && old_len > 0u) {
    memcpy(bytes, view.bytes, old_len);
    pos = old_len;
  }
  for (i = 0u; i < count; ++i) {
    line_len = (unsigned short)strlen(lines[i]);
    if (line_len > 0u) {
      memcpy(bytes + pos, lines[i], line_len);
      pos = (unsigned short)(pos + line_len);
    }
    bytes[pos++] = '\r';
  }

  if (readyshell_host_fs_store_bytes(drive,
                                     name,
                                     READYSHELL_HOST_FS_TYPE_SEQ,
                                     bytes,
                                     total) != 0) {
    free(bytes);
    return -1;
  }
  free(bytes);
  return 0;
}

int readyshell_host_fs_seq_line_count(unsigned char drive,
                                      const char* name,
                                      unsigned short* out_count) {
  RSHostFSView view;
  unsigned short count;
  unsigned short i;
  if (!out_count || readyshell_host_fs_get_view(drive, name, &view) != 0 ||
      view.type != READYSHELL_HOST_FS_TYPE_SEQ) {
    return -1;
  }
  count = 0u;
  if (view.len == 0u) {
    *out_count = 0u;
    return 0;
  }
  for (i = 0u; i < view.len; ++i) {
    if (view.bytes[i] == '\r') {
      ++count;
    }
  }
  if (view.bytes[view.len - 1u] != '\r') {
    ++count;
  }
  *out_count = count;
  return 0;
}

int readyshell_host_fs_seq_line_copy(unsigned char drive,
                                     const char* name,
                                     unsigned short index,
                                     char* out,
                                     unsigned short max) {
  RSHostFSView view;
  unsigned short start;
  unsigned short end;
  unsigned short current;
  unsigned short len;
  if (!out || max == 0u ||
      readyshell_host_fs_get_view(drive, name, &view) != 0 ||
      view.type != READYSHELL_HOST_FS_TYPE_SEQ) {
    return -1;
  }
  start = 0u;
  current = 0u;
  while (start <= view.len) {
    end = start;
    while (end < view.len && view.bytes[end] != '\r') {
      ++end;
    }
    if (current == index) {
      len = (unsigned short)(end - start);
      if (len + 1u > max) {
        return -1;
      }
      if (len > 0u) {
        memcpy(out, view.bytes + start, len);
      }
      out[len] = '\0';
      return 0;
    }
    ++current;
    if (end >= view.len) {
      break;
    }
    start = (unsigned short)(end + 1u);
  }
  return -1;
}

int readyshell_host_fs_snapshot_read_cb(void* user,
                                        const char* path,
                                        unsigned char* dst,
                                        unsigned short max,
                                        unsigned short* out_len) {
  unsigned char drive;
  char name[RS_HOST_FS_NAME_MAX];
  RSHostFSView view;
  (void)user;
  if (!dst || !out_len ||
      readyshell_host_fs_parse_path(path, 8u, &drive, name, sizeof(name)) != 0 ||
      readyshell_host_fs_get_view(drive, name, &view) != 0 ||
      view.type != READYSHELL_HOST_FS_TYPE_BINARY ||
      view.len > max) {
    return -1;
  }
  memcpy(dst, view.bytes, view.len);
  *out_len = view.len;
  return 0;
}

int readyshell_host_fs_snapshot_write_cb(void* user,
                                         const char* path,
                                         const unsigned char* src,
                                         unsigned short len) {
  unsigned char drive;
  char name[RS_HOST_FS_NAME_MAX];
  (void)user;
  if (readyshell_host_fs_parse_path(path, 8u, &drive, name, sizeof(name)) != 0) {
    return -1;
  }
  return readyshell_host_fs_store_bytes(drive,
                                        name,
                                        READYSHELL_HOST_FS_TYPE_BINARY,
                                        src,
                                        len);
}
