#include "build_support/readyshell_host_fs.h"
#include "src/apps/readyshell/core/rs_cmd_overlay.h"
#include "src/apps/readyshell/core/rs_serialize.h"

#include <stdlib.h>
#include <string.h>

#define HOST_NAME_MAX 32u
#define HOST_LINE_MAX 128u

typedef struct SmokeDirEntry {
  const char* name;
  unsigned short blocks;
  const char* type;
} SmokeDirEntry;

static const SmokeDirEntry g_dir_entries[] = {
  { "alpha", 10u, "PRG" },
  { "beta", 20u, "SEQ" },
  { "gamma", 30u, "USR" },
  { "todo", 40u, "PRG" },
  { "temp", 50u, "SEQ" },
  { "ta", 60u, "REL" }
};

static RSValue g_ldv_value;
static unsigned char g_ldv_ready = 0u;

static void ldv_reset(void) {
  if (g_ldv_ready) {
    rs_value_free(&g_ldv_value);
    g_ldv_ready = 0u;
  }
}

static int host_ci_equal(const char* a, const char* b) {
  unsigned char ca;
  unsigned char cb;
  if (!a || !b) {
    return 0;
  }
  do {
    ca = (unsigned char)*a++;
    cb = (unsigned char)*b++;
    if (ca >= 'a' && ca <= 'z') {
      ca = (unsigned char)(ca - ('a' - 'A'));
    }
    if (cb >= 'a' && cb <= 'z') {
      cb = (unsigned char)(cb - ('a' - 'A'));
    }
    if (ca != cb) {
      return 0;
    }
  } while (ca != '\0');
  return 1;
}

static int host_wild_match(const char* pattern, const char* text) {
  unsigned char pc;
  unsigned char tc;
  if (!pattern || !text) {
    return 0;
  }
  if (*pattern == '\0') {
    return *text == '\0';
  }
  if (*pattern == '*') {
    return host_wild_match(pattern + 1, text) ||
           (*text != '\0' && host_wild_match(pattern, text + 1));
  }
  if (*pattern == '?') {
    return *text != '\0' && host_wild_match(pattern + 1, text + 1);
  }
  pc = (unsigned char)*pattern;
  tc = (unsigned char)*text;
  if (pc >= 'a' && pc <= 'z') {
    pc = (unsigned char)(pc - ('a' - 'A'));
  }
  if (tc >= 'a' && tc <= 'z') {
    tc = (unsigned char)(tc - ('a' - 'A'));
  }
  return pc == tc && host_wild_match(pattern + 1, text + 1);
}

static int host_filter_has_type(const char* filter, const char* type) {
  char token[8];
  unsigned short pos;
  unsigned short i;
  if (!filter || filter[0] == '\0') {
    return 1;
  }
  pos = 0u;
  for (;;) {
    i = 0u;
    while (filter[pos] != '\0' && filter[pos] != ',') {
      if (i + 1u >= sizeof(token)) {
        return 0;
      }
      token[i++] = filter[pos++];
    }
    token[i] = '\0';
    if (host_ci_equal(token, type)) {
      return 1;
    }
    if (filter[pos] == '\0') {
      break;
    }
    ++pos;
  }
  return 0;
}

static int smoke_build_dir_entry(RSValue* out,
                                 const SmokeDirEntry* entry) {
  RSValue tmp;
  if (!out || !entry || rs_value_object_new(out) != 0) {
    return -1;
  }
  rs_value_init_false(&tmp);
  if (rs_value_init_string(&tmp, entry->name) != 0 ||
      rs_value_object_set(out, "name", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out);
    return -1;
  }
  rs_value_free(&tmp);
  rs_value_init_u16(&tmp, entry->blocks);
  if (rs_value_object_set(out, "blocks", &tmp) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_init_string(&tmp, entry->type) != 0 ||
      rs_value_object_set(out, "type", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out);
    return -1;
  }
  rs_value_free(&tmp);
  return 0;
}

static int smoke_drive_present(unsigned char drive) {
  return drive == 8u || drive == 9u;
}

static const char* value_cstr(const RSValue* value,
                              char* buf,
                              unsigned short buf_len) {
  if (!value || rs_value_string_copy(value, buf, buf_len) != 0) {
    return 0;
  }
  return buf;
}

static int parse_path_args(const RSValue* path_arg,
                           const RSValue* drive_arg,
                           unsigned char default_drive,
                           unsigned char* out_drive,
                           char* out_name,
                           unsigned short out_name_len) {
  char path[HOST_NAME_MAX + 8u];
  unsigned short drive16;
  if (!path_arg) {
    return -1;
  }
  if (drive_arg) {
    if (rs_value_to_u16(drive_arg, &drive16) != 0 || drive16 < 8u || drive16 > 11u) {
      return -1;
    }
    default_drive = (unsigned char)drive16;
  }
  if (!value_cstr(path_arg, path, sizeof(path))) {
    return -1;
  }
  return readyshell_host_fs_parse_path(path,
                                       default_drive,
                                       out_drive,
                                       out_name,
                                       out_name_len);
}

static int parse_plain_name(const RSValue* value, char* out_name, unsigned short out_name_len) {
  if (!value_cstr(value, out_name, out_name_len) || strchr(out_name, ':') != 0) {
    return -1;
  }
  return 0;
}

static int parse_lst_args(RSCommandFrame* frame,
                          unsigned char* out_drive,
                          char* out_pattern,
                          unsigned short pattern_len,
                          char* out_type_filter,
                          unsigned short type_filter_len) {
  char buf[HOST_NAME_MAX + 8u];
  unsigned short drive16;
  if (!frame || !out_drive || !out_pattern || !out_type_filter) {
    return -1;
  }
  *out_drive = 8u;
  strncpy(out_pattern, "*", pattern_len);
  out_pattern[pattern_len - 1u] = '\0';
  out_type_filter[0] = '\0';
  if (frame->arg_count == 0u) {
    return 0;
  }
  if (frame->args[0].tag == RS_VAL_U16 || frame->args[0].tag == RS_VAL_TRUE || frame->args[0].tag == RS_VAL_FALSE) {
    if (rs_value_to_u16(&frame->args[0], &drive16) != 0 || !smoke_drive_present((unsigned char)drive16)) {
      return -1;
    }
    *out_drive = (unsigned char)drive16;
    return frame->arg_count == 1u ? 0 : -1;
  }
  if (!value_cstr(&frame->args[0], buf, sizeof(buf))) {
    return -1;
  }
  if (readyshell_host_fs_parse_path(buf, 8u, out_drive, out_pattern, pattern_len) != 0) {
    return -1;
  }
  if (!smoke_drive_present(*out_drive)) {
    return -1;
  }
  if (frame->arg_count >= 2u) {
    if (frame->args[1].tag == RS_VAL_U16 || frame->args[1].tag == RS_VAL_TRUE || frame->args[1].tag == RS_VAL_FALSE) {
      if (rs_value_to_u16(&frame->args[1], &drive16) != 0 || !smoke_drive_present((unsigned char)drive16)) {
        return -1;
      }
      *out_drive = (unsigned char)drive16;
      if (frame->arg_count >= 3u) {
        if (!value_cstr(&frame->args[2], out_type_filter, type_filter_len)) {
          return -1;
        }
      }
    } else {
      if (!value_cstr(&frame->args[1], out_type_filter, type_filter_len)) {
        return -1;
      }
    }
  }
  return 0;
}

static int count_lst_matches(RSCommandFrame* frame, unsigned short* out_count) {
  char pattern[HOST_NAME_MAX + 1u];
  char type_filter[16];
  unsigned char drive;
  unsigned short count;
  unsigned short i;
  if (!out_count || parse_lst_args(frame, &drive, pattern, sizeof(pattern), type_filter, sizeof(type_filter)) != 0) {
    return -1;
  }
  count = 0u;
  for (i = 0u; i < (unsigned short)(sizeof(g_dir_entries) / sizeof(g_dir_entries[0])); ++i) {
    if (host_wild_match(pattern, g_dir_entries[i].name) &&
        host_filter_has_type(type_filter, g_dir_entries[i].type)) {
      ++count;
    }
  }
  *out_count = count;
  return 0;
}

static int lst_item_at(RSCommandFrame* frame, unsigned short wanted_index) {
  char pattern[HOST_NAME_MAX + 1u];
  char type_filter[16];
  unsigned char drive;
  unsigned short i;
  unsigned short seen;
  (void)drive;
  if (!frame || !frame->out ||
      parse_lst_args(frame, &drive, pattern, sizeof(pattern), type_filter, sizeof(type_filter)) != 0) {
    return -1;
  }
  seen = 0u;
  for (i = 0u; i < (unsigned short)(sizeof(g_dir_entries) / sizeof(g_dir_entries[0])); ++i) {
    if (!host_wild_match(pattern, g_dir_entries[i].name) ||
        !host_filter_has_type(type_filter, g_dir_entries[i].type)) {
      continue;
    }
    if (seen == wanted_index) {
      rs_value_free(frame->out);
      return smoke_build_dir_entry(frame->out, &g_dir_entries[i]);
    }
    ++seen;
  }
  return -1;
}

static int seq_value_to_lines(const RSValue* value,
                              const char* lines[],
                              unsigned short* out_count) {
  RSValue item;
  unsigned short i;
  char* line;
  if (!value || !lines || !out_count) {
    return -1;
  }
  *out_count = 0u;
  if (rs_value_is_string_like(value)) {
    line = (char*)malloc(HOST_LINE_MAX);
    if (!line || rs_value_string_copy(value, line, HOST_LINE_MAX) != 0) {
      free(line);
      return -1;
    }
    lines[0] = line;
    *out_count = 1u;
    return 0;
  }
  if (!rs_value_is_array_like(value)) {
    return -1;
  }
  rs_value_init_false(&item);
  for (i = 0u; i < rs_value_array_count(value); ++i) {
    line = (char*)malloc(HOST_LINE_MAX);
    if (!line || rs_value_array_get(value, i, &item) != 0 ||
        rs_value_string_copy(&item, line, HOST_LINE_MAX) != 0) {
      free(line);
      rs_value_free(&item);
      return -1;
    }
    rs_value_free(&item);
    lines[*out_count] = line;
    *out_count = (unsigned short)(*out_count + 1u);
  }
  return 0;
}

static void free_seq_lines(const char* lines[], unsigned short count) {
  unsigned short i;
  for (i = 0u; i < count; ++i) {
    free((void*)lines[i]);
  }
}

static int ldv_load_value(const RSValue* path_arg,
                          const RSValue* drive_arg,
                          RSValue* out_value) {
  unsigned char drive;
  char name[HOST_NAME_MAX];
  unsigned char bytes[65535];
  RSHostFSView view;
  if (parse_path_args(path_arg, drive_arg, 8u, &drive, name, sizeof(name)) != 0) {
    return -2;
  }
  if (readyshell_host_fs_get_view(drive, name, &view) != 0 ||
      view.type != READYSHELL_HOST_FS_TYPE_BINARY) {
    rs_value_free(out_value);
    rs_value_init_false(out_value);
    return 0;
  }
  if (view.len > sizeof(bytes)) {
    return -1;
  }
  memcpy(bytes, view.bytes, view.len);
  rs_value_free(out_value);
  rs_value_init_false(out_value);
  if (rs_deserialize_file_payload(bytes, view.len, out_value) != 0) {
    return -1;
  }
  return 0;
}

int rs_overlay_command_call(RSCommandId id, unsigned char op, RSCommandFrame* frame) {
  RSValue tmp;
  unsigned char drive;
  unsigned char src_drive;
  unsigned char dst_drive;
  char name[HOST_NAME_MAX];
  char dst_name[HOST_NAME_MAX];
  char line[HOST_LINE_MAX];
  const char* seq_lines[16];
  unsigned short line_count;
  unsigned short drive16;
  unsigned short len;
  unsigned short i;
  unsigned char bytes[65535];

  if (!frame) {
    return -1;
  }

  if (id == RS_CMD_LST) {
    if (op == RS_CMD_OVL_OP_BEGIN) {
      return count_lst_matches(frame, &frame->count);
    }
    if (op == RS_CMD_OVL_OP_ITEM) {
      return lst_item_at(frame, frame->index);
    }
    return -1;
  }

  if (id == RS_CMD_DRVI && op == RS_CMD_OVL_OP_RUN) {
    if (!frame->out) {
      return -1;
    }
    drive = 8u;
    if (frame->arg_count > 0u) {
      if (rs_value_to_u16(&frame->args[0], &drive16) != 0 || drive16 > 255u) {
        return -2;
      }
      drive = (unsigned char)drive16;
    }
    if (!smoke_drive_present(drive)) {
      return -1;
    }
    rs_value_free(frame->out);
    if (rs_value_object_new(frame->out) != 0) {
      return -1;
    }
    rs_value_init_false(&tmp);
    rs_value_init_u16(&tmp, drive);
    if (rs_value_object_set(frame->out, "drive", &tmp) != 0) {
      rs_value_free(frame->out);
      return -1;
    }
    if (rs_value_init_string(&tmp, "readyos") != 0 ||
        rs_value_object_set(frame->out, "diskname", &tmp) != 0) {
      rs_value_free(&tmp);
      rs_value_free(frame->out);
      return -1;
    }
    rs_value_free(&tmp);
    rs_value_init_u16(&tmp, 664u);
    if (rs_value_object_set(frame->out, "blocksfree", &tmp) != 0) {
      rs_value_free(frame->out);
      return -1;
    }
    return 0;
  }

  if (id == RS_CMD_CAT) {
    if (frame->arg_count < 1u) {
      return -2;
    }
    if (op == RS_CMD_OVL_OP_BEGIN) {
      if (parse_path_args(&frame->args[0], frame->arg_count >= 2u ? &frame->args[1] : 0, 8u,
                          &drive, name, sizeof(name)) != 0 ||
          readyshell_host_fs_seq_line_count(drive, name, &frame->count) != 0) {
        return -1;
      }
      return 0;
    }
    if (op == RS_CMD_OVL_OP_ITEM) {
      if (!frame->out ||
          parse_path_args(&frame->args[0], frame->arg_count >= 2u ? &frame->args[1] : 0, 8u,
                          &drive, name, sizeof(name)) != 0 ||
          readyshell_host_fs_seq_line_copy(drive, name, frame->index, line, sizeof(line)) != 0) {
        return -1;
      }
      rs_value_free(frame->out);
      return rs_value_init_string(frame->out, line);
    }
    return -1;
  }

  if (id == RS_CMD_PUT || id == RS_CMD_ADD) {
    if (op != RS_CMD_OVL_OP_RUN || frame->arg_count < 2u || !frame->out) {
      return frame->arg_count < 2u ? -2 : -1;
    }
    if (parse_path_args(&frame->args[1],
                        frame->arg_count >= 3u ? &frame->args[2] : 0,
                        8u,
                        &drive,
                        name,
                        sizeof(name)) != 0) {
      return -2;
    }
    if (seq_value_to_lines(&frame->args[0], seq_lines, &line_count) != 0) {
      return -2;
    }
    i = (unsigned short)(readyshell_host_fs_write_seq_lines(drive,
                                                            name,
                                                            seq_lines,
                                                            line_count,
                                                            id == RS_CMD_ADD) == 0);
    free_seq_lines(seq_lines, line_count);
    rs_value_free(frame->out);
    rs_value_init_bool(frame->out, i != 0u);
    return i ? 0 : -1;
  }

  if (id == RS_CMD_DEL && op == RS_CMD_OVL_OP_RUN) {
    if (frame->arg_count < 1u || !frame->out ||
        parse_path_args(&frame->args[0], frame->arg_count >= 2u ? &frame->args[1] : 0, 8u,
                        &drive, name, sizeof(name)) != 0) {
      return -2;
    }
    rs_value_free(frame->out);
    rs_value_init_bool(frame->out, readyshell_host_fs_delete_file(drive, name) == 0);
    return 0;
  }

  if (id == RS_CMD_REN && op == RS_CMD_OVL_OP_RUN) {
    if (frame->arg_count < 2u || !frame->out ||
        parse_path_args(&frame->args[0], frame->arg_count >= 3u ? &frame->args[2] : 0, 8u,
                        &drive, name, sizeof(name)) != 0 ||
        parse_plain_name(&frame->args[1], dst_name, sizeof(dst_name)) != 0) {
      return -2;
    }
    rs_value_free(frame->out);
    rs_value_init_bool(frame->out, readyshell_host_fs_rename_file(drive, name, dst_name) == 0);
    return 0;
  }

  if (id == RS_CMD_COPY && op == RS_CMD_OVL_OP_RUN) {
    if (frame->arg_count < 2u || !frame->out ||
        parse_path_args(&frame->args[0], 0, 8u, &src_drive, name, sizeof(name)) != 0) {
      return -2;
    }
    if (frame->args[1].tag == RS_VAL_U16 || frame->args[1].tag == RS_VAL_TRUE || frame->args[1].tag == RS_VAL_FALSE) {
      if (rs_value_to_u16(&frame->args[1], &drive16) != 0 || drive16 < 8u || drive16 > 11u) {
        return -2;
      }
      if (readyshell_host_fs_copy_file(src_drive, name, (unsigned char)drive16, name) != 0) {
        rs_value_free(frame->out);
        rs_value_init_false(frame->out);
      } else {
        rs_value_free(frame->out);
        rs_value_init_true(frame->out);
      }
      return 0;
    }
    dst_drive = src_drive;
    if (parse_path_args(&frame->args[1], 0, dst_drive, &dst_drive, dst_name, sizeof(dst_name)) != 0) {
      return -2;
    }
    rs_value_free(frame->out);
    rs_value_init_bool(frame->out, readyshell_host_fs_copy_file(src_drive, name, dst_drive, dst_name) == 0);
    return 0;
  }

  if (id == RS_CMD_STV && op == RS_CMD_OVL_OP_RUN) {
    if (frame->arg_count < 2u || !frame->out ||
        parse_path_args(&frame->args[1], frame->arg_count >= 3u ? &frame->args[2] : 0, 8u,
                        &drive, name, sizeof(name)) != 0 ||
        rs_serialize_file_payload(&frame->args[0], bytes, sizeof(bytes), &len) != 0) {
      return -2;
    }
    rs_value_free(frame->out);
    rs_value_init_bool(frame->out,
                       readyshell_host_fs_store_bytes(drive,
                                                      name,
                                                      READYSHELL_HOST_FS_TYPE_BINARY,
                                                      bytes,
                                                      len) == 0);
    return rs_value_truthy(frame->out) ? 0 : -1;
  }

  if (id == RS_CMD_LDV) {
    if (frame->arg_count < 1u) {
      return -2;
    }
    if (op == RS_CMD_OVL_OP_BEGIN) {
      ldv_reset();
      rs_value_init_false(&g_ldv_value);
      if (ldv_load_value(&frame->args[0],
                         frame->arg_count >= 2u ? &frame->args[1] : 0,
                         &g_ldv_value) != 0) {
        ldv_reset();
        return -1;
      }
      g_ldv_ready = 1u;
      frame->count = rs_value_is_array_like(&g_ldv_value) ? rs_value_array_count(&g_ldv_value) : 1u;
      return 0;
    }
    if (op == RS_CMD_OVL_OP_ITEM) {
      if (!g_ldv_ready || !frame->out) {
        return -1;
      }
      rs_value_free(frame->out);
      if (rs_value_is_array_like(&g_ldv_value)) {
        return rs_value_array_get(&g_ldv_value, frame->index, frame->out);
      }
      if (frame->index != 0u) {
        return -1;
      }
      return rs_value_clone(frame->out, &g_ldv_value);
    }
    if (op == RS_CMD_OVL_OP_RUN) {
      if (!frame->out) {
        return -1;
      }
      rs_value_free(frame->out);
      rs_value_init_false(frame->out);
      return ldv_load_value(&frame->args[0],
                            frame->arg_count >= 2u ? &frame->args[1] : 0,
                            frame->out);
    }
    return -1;
  }

  return -1;
}
