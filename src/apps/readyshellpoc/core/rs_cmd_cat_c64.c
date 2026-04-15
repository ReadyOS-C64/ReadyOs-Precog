#include "rs_cmd_overlay.h"
#include "rs_cmd_registry.h"
#include "rs_cmd_file_local.h"

#include <string.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY7")
#pragma rodata-name(push, "OVERLAY7")
#pragma bss-name(push, "OVERLAY7")
#endif

/*
 * DEBUG_RSCAT gates only the CAT debug output path. The file-open/read logic
 * stays the same; when the flag is off CAT emits only payload lines.
 */
#if !defined(DEBUG_RSCAT)
#define DEBUG_RSCAT 0
#endif

#define CAT_REU_BASE          0x43D800ul
#define CAT_REU_LEN           0x1800u
#define CAT_META_OFF          CAT_REU_BASE
#define CAT_META_LEN          12u
#define CAT_REC_OFF           (CAT_REU_BASE + 0x0020ul)
#define CAT_REC_LEN           4u
#define CAT_REC_CAP           128u
#define CAT_DATA_OFF          (CAT_REU_BASE + 0x0220ul)
#define CAT_DATA_LEN          (CAT_REU_LEN - 0x0220u)
#define CAT_LINE_MAX          255u
#define CAT_OPEN_NAME_MAX     96u
#define CAT_IO_BUF_LEN        128u

#define CAT_META_TOTAL_OFF    4u
#define CAT_META_LOCAL_OFF    6u
#define CAT_META_LINE_OFF     8u
#define CAT_META_USED_OFF     10u
#define CAT_PAYLOAD_REC_BASE  1u

#if DEBUG_RSCAT
#define CAT_DEBUG_ITEM_COUNT  3u
#define CAT_PAYLOAD_INDEX_BASE 2u
#else
#define CAT_DEBUG_ITEM_COUNT  0u
#define CAT_PAYLOAD_INDEX_BASE 0u
#endif

static const char g_cat_magic[4] = { 'C', 'A', 'T', '2' };
static const char g_cat_err_read[] = "READ FAIL";
static const char g_cat_err_name[] = "BAD NAME";
static const char g_cat_err_big[] = "FILE TOO BIG";
static const char g_cat_err_line[] = "LINE TOO LONG";
static const char g_cat_err_lines[] = "TOO MANY LINES";
static const char g_cat_err_state[] = "CAT STATE";

static unsigned char g_cat_meta[CAT_META_LEN];
static unsigned char g_cat_rec[CAT_REC_LEN];
static unsigned char g_cat_io_buf[CAT_IO_BUF_LEN];
static char g_cat_open_name[CAT_OPEN_NAME_MAX];
#if DEBUG_RSCAT
static char g_cat_line[CAT_LINE_MAX + 1u];
static unsigned short g_cat_overlay_ram_marker;
#endif
static char g_cat_payload[CAT_LINE_MAX + 1u];

static unsigned short cat_meta_get_u16(unsigned char off) {
  return (unsigned short)(g_cat_meta[off] | ((unsigned short)g_cat_meta[(unsigned char)(off + 1u)] << 8u));
}

static void cat_meta_set_u16(unsigned char off, unsigned short value) {
  g_cat_meta[off] = (unsigned char)(value & 0xFFu);
  g_cat_meta[(unsigned char)(off + 1u)] = (unsigned char)((value >> 8u) & 0xFFu);
}

static int cat_meta_load(unsigned short* total,
                         unsigned short* local,
                         unsigned short* line_count,
                         unsigned short* data_used) {
  if (!total || !local || !line_count || !data_used) {
    return -1;
  }
  if (rs_reu_read(CAT_META_OFF, g_cat_meta, CAT_META_LEN) != 0 ||
      memcmp(g_cat_meta, g_cat_magic, sizeof(g_cat_magic)) != 0) {
    *total = 0u;
    *local = 0u;
    *line_count = 0u;
    *data_used = 0u;
    return -1;
  }
  *total = cat_meta_get_u16(CAT_META_TOTAL_OFF);
  *local = cat_meta_get_u16(CAT_META_LOCAL_OFF);
  *line_count = cat_meta_get_u16(CAT_META_LINE_OFF);
  *data_used = cat_meta_get_u16(CAT_META_USED_OFF);
  return 0;
}

static int cat_meta_store(unsigned short total,
                          unsigned short local,
                          unsigned short line_count,
                          unsigned short data_used) {
  memcpy(g_cat_meta, g_cat_magic, sizeof(g_cat_magic));
  cat_meta_set_u16(CAT_META_TOTAL_OFF, total);
  cat_meta_set_u16(CAT_META_LOCAL_OFF, local);
  cat_meta_set_u16(CAT_META_LINE_OFF, line_count);
  cat_meta_set_u16(CAT_META_USED_OFF, data_used);
  return rs_reu_write(CAT_META_OFF, g_cat_meta, CAT_META_LEN);
}

static int cat_write_rec(unsigned short index,
                         unsigned short start,
                         unsigned short len) {
  g_cat_rec[0] = (unsigned char)(start & 0xFFu);
  g_cat_rec[1] = (unsigned char)((start >> 8u) & 0xFFu);
  g_cat_rec[2] = (unsigned char)(len & 0xFFu);
  g_cat_rec[3] = (unsigned char)((len >> 8u) & 0xFFu);
  return rs_reu_write(CAT_REC_OFF + ((unsigned long)index * (unsigned long)CAT_REC_LEN),
                      g_cat_rec,
                      CAT_REC_LEN);
}

static int cat_read_rec(unsigned short index,
                        unsigned short* start,
                        unsigned short* len) {
  if (!start || !len || index >= CAT_REC_CAP ||
      rs_reu_read(CAT_REC_OFF + ((unsigned long)index * (unsigned long)CAT_REC_LEN),
                  g_cat_rec,
                  CAT_REC_LEN) != 0) {
    return -1;
  }
  *start = (unsigned short)(g_cat_rec[0] | ((unsigned short)g_cat_rec[1] << 8u));
  *len = (unsigned short)(g_cat_rec[2] | ((unsigned short)g_cat_rec[3] << 8u));
  return 0;
}

#if DEBUG_RSCAT
static char* cat_append_str(char* dst, unsigned short* rem, const char* src) {
  if (!dst || !rem || !src) {
    return dst;
  }
  while (*src != '\0' && *rem > 1u) {
    *dst++ = *src++;
    --*rem;
  }
  *dst = '\0';
  return dst;
}

static char* cat_append_u16(char* dst, unsigned short* rem, unsigned short value) {
  char digits[5];
  unsigned char count;

  if (!dst || !rem) {
    return dst;
  }
  if (value == 0u) {
    if (*rem > 1u) {
      *dst++ = '0';
      --*rem;
      *dst = '\0';
    }
    return dst;
  }

  count = 0u;
  while (value != 0u && count < sizeof(digits)) {
    digits[count++] = (char)('0' + (value % 10u));
    value = (unsigned short)(value / 10u);
  }
  while (count != 0u && *rem > 1u) {
    *dst++ = digits[--count];
    --*rem;
  }
  *dst = '\0';
  return dst;
}

static void cat_build_dbg_prefix(const char* tag,
                                 unsigned short total,
                                 unsigned short local) {
  unsigned short rem;
  char* dst;

  rem = CAT_LINE_MAX + 1u;
  dst = g_cat_line;
  g_cat_line[0] = '\0';

  dst = cat_append_str(dst, &rem, "DBG:");
  dst = cat_append_str(dst, &rem, tag);
  dst = cat_append_str(dst, &rem, " total=");
  dst = cat_append_u16(dst, &rem, total);
  dst = cat_append_str(dst, &rem, " local=");
  dst = cat_append_u16(dst, &rem, local);
  dst = cat_append_str(dst, &rem, " ram=");
  (void)cat_append_u16(dst, &rem, g_cat_overlay_ram_marker);
}

static void cat_build_dbg_line(const char* tag,
                               const char* arg,
                               unsigned short total,
                               unsigned short local) {
  unsigned short rem;
  char* dst;

  cat_build_dbg_prefix(tag, total, local);
  if (!arg) {
    return;
  }
  rem = (unsigned short)(CAT_LINE_MAX + 1u - (unsigned short)strlen(g_cat_line));
  dst = g_cat_line + strlen(g_cat_line);
  dst = cat_append_str(dst, &rem, " arg=");
  (void)cat_append_str(dst, &rem, arg);
}
#endif

#if DEBUG_RSCAT
static int cat_store_text(const char* text,
                          unsigned short* start_out,
                          unsigned short* len_out,
                          unsigned short* data_used) {
  unsigned short len;

  if (!text || !start_out || !len_out || !data_used) {
    return -1;
  }
  len = (unsigned short)strlen(text);
  if (len > CAT_LINE_MAX || len > (unsigned short)(CAT_DATA_LEN - *data_used)) {
    return -1;
  }

  *start_out = *data_used;
  *len_out = len;
  if (len != 0u &&
      rs_reu_write(CAT_DATA_OFF + (unsigned long)(*data_used), text, len) != 0) {
    return -1;
  }
  *data_used = (unsigned short)(*data_used + len);
  return 0;
}
#endif

static int cat_read_text(unsigned short rec_index, char* out, unsigned short out_cap) {
  unsigned short start;
  unsigned short len;

  if (!out || out_cap == 0u ||
      cat_read_rec(rec_index, &start, &len) != 0 ||
      len >= out_cap) {
    return -1;
  }
  if (len != 0u &&
      rs_reu_read(CAT_DATA_OFF + (unsigned long)start, out, len) != 0) {
    return -1;
  }
  out[len] = '\0';
  return 0;
}

static int cat_build_open_name(const char* path, unsigned char* drive_out) {
  const char* name;
  unsigned char drive;
  unsigned short len;

  if (!path || !drive_out) {
    return -1;
  }

  if (rs_cmd_file_parse_drive_prefix(path,
                                     rs_cmd_file_default_drive(),
                                     &drive,
                                     &name) != 0) {
    return -1;
  }

  if (rs_cmd_file_has_char(name, ',')) {
    len = (unsigned short)strlen(name);
    if (len + 1u > sizeof(g_cat_open_name)) {
      return -1;
    }
    memcpy(g_cat_open_name, name, len + 1u);
  } else {
    len = (unsigned short)strlen(name);
    if ((unsigned long)len + 5ul > (unsigned long)sizeof(g_cat_open_name)) {
      return -1;
    }
    g_cat_open_name[0] = '0';
    g_cat_open_name[1] = ':';
    memcpy(g_cat_open_name + 2u, name, len);
    g_cat_open_name[2u + len] = ',';
    g_cat_open_name[3u + len] = 'r';
    g_cat_open_name[4u + len] = '\0';
  }

  *drive_out = drive;
  return 0;
}

static int cat_load_file_bytes(unsigned char drive,
                               unsigned short start_off,
                               unsigned short* file_len_out,
                               RSError* err) {
  unsigned short total;
  unsigned short remaining;
  unsigned short chunk;
  int nread;

  if (!file_len_out || start_off > CAT_DATA_LEN) {
    return -1;
  }

  total = 0u;
  rs_cmd_file_cleanup_io();
  if (cbm_open(RS_CMD_FILE_LFN_DATA, drive, 2, g_cat_open_name) != 0) {
    rs_cmd_file_note_status(err, drive, 255u);
    return -1;
  }

  for (;;) {
    remaining = (unsigned short)(CAT_DATA_LEN - start_off - total);
    if (remaining == 0u) {
      nread = cbm_read(RS_CMD_FILE_LFN_DATA, g_cat_io_buf, 1u);
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      if (nread < 0) {
        rs_cmd_file_set_error(err, 255u, g_cat_err_read);
        return -1;
      }
      if (nread > 0) {
        rs_cmd_file_set_error(err, 255u, g_cat_err_big);
        return -1;
      }
      break;
    }

    chunk = remaining;
    if (chunk > CAT_IO_BUF_LEN) {
      chunk = CAT_IO_BUF_LEN;
    }
    nread = cbm_read(RS_CMD_FILE_LFN_DATA, g_cat_io_buf, chunk);
    if (nread < 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_set_error(err, 255u, g_cat_err_read);
      return -1;
    }
    if (nread == 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      break;
    }
    if (rs_reu_write(CAT_DATA_OFF + (unsigned long)start_off + (unsigned long)total,
                     g_cat_io_buf,
                     (unsigned short)nread) != 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_set_error(err, 255u, g_cat_err_state);
      return -1;
    }
    total = (unsigned short)(total + (unsigned short)nread);
  }

  *file_len_out = total;
  return 0;
}

static int cat_add_line_record(unsigned short start,
                               unsigned short len,
                               unsigned short* line_count) {
  if (!line_count) {
    return -1;
  }
  if (len > CAT_LINE_MAX) {
    return -2;
  }
  if (*line_count >= (unsigned short)(CAT_REC_CAP - CAT_PAYLOAD_REC_BASE)) {
    return -3;
  }
  if (cat_write_rec((unsigned short)(*line_count + CAT_PAYLOAD_REC_BASE), start, len) != 0) {
    return -1;
  }
  ++*line_count;
  return 0;
}

static int cat_parse_lines(unsigned short file_start,
                           unsigned short file_len,
                           unsigned short* line_count_out) {
  unsigned short pos;
  unsigned short line_start;
  unsigned short line_len;
  unsigned short line_count;
  unsigned short chunk;
  unsigned short i;
  unsigned char prev_cr;
  unsigned char ended_break;
  int rc;

  if (!line_count_out) {
    return -1;
  }

  pos = 0u;
  line_start = file_start;
  line_len = 0u;
  line_count = 0u;
  prev_cr = 0u;
  ended_break = 0u;

  while (pos < file_len) {
    chunk = (unsigned short)(file_len - pos);
    if (chunk > CAT_IO_BUF_LEN) {
      chunk = CAT_IO_BUF_LEN;
    }
    if (rs_reu_read(CAT_DATA_OFF + (unsigned long)file_start + (unsigned long)pos,
                    g_cat_io_buf,
                    chunk) != 0) {
      return -1;
    }

    for (i = 0u; i < chunk; ++i, ++pos) {
      unsigned char ch = g_cat_io_buf[i];

      if (prev_cr) {
        prev_cr = 0u;
        if (ch == '\n') {
          line_start = (unsigned short)(file_start + pos + 1u);
          ended_break = 1u;
          continue;
        }
      }

      if (ch == '\r') {
        rc = cat_add_line_record(line_start, line_len, &line_count);
        if (rc != 0) {
          return rc;
        }
        line_start = (unsigned short)(file_start + pos + 1u);
        line_len = 0u;
        prev_cr = 1u;
        ended_break = 1u;
        continue;
      }

      if (ch == '\n') {
        rc = cat_add_line_record(line_start, line_len, &line_count);
        if (rc != 0) {
          return rc;
        }
        line_start = (unsigned short)(file_start + pos + 1u);
        line_len = 0u;
        ended_break = 1u;
        continue;
      }

      ++line_len;
      if (line_len > CAT_LINE_MAX) {
        return -2;
      }
      ended_break = 0u;
    }
  }

  if (file_len != 0u && !ended_break) {
    rc = cat_add_line_record(line_start, line_len, &line_count);
    if (rc != 0) {
      return rc;
    }
  }

  *line_count_out = line_count;
  return 0;
}

static int cat_begin(RSCommandFrame* frame) {
  const char* arg;
  unsigned char drive;
  unsigned short total;
  unsigned short local;
  unsigned short line_count;
  unsigned short data_used;
#if DEBUG_RSCAT
  unsigned short arg_start;
  unsigned short arg_len;
#endif
  unsigned short file_len;
  int parse_rc;

  if (!frame || !frame->args || !frame->err || frame->arg_count != 1u) {
    return -1;
  }

  arg = rs_cmd_value_cstr(&frame->args[0]);
  if (!arg) {
    return -2;
  }
  if (cat_build_open_name(arg, &drive) != 0) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_name);
    return -2;
  }

  (void)cat_meta_load(&total, &local, &line_count, &data_used);
  local = 0u;
  line_count = 0u;
  data_used = 0u;
  if (cat_meta_store(total, local, line_count, data_used) != 0) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_state);
    return -1;
  }

#if DEBUG_RSCAT
  ++g_cat_overlay_ram_marker;
#endif

#if DEBUG_RSCAT
  if (cat_store_text(arg, &arg_start, &arg_len, &data_used) != 0 ||
      cat_write_rec(0u, arg_start, arg_len) != 0) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_state);
    return -1;
  }
#endif

  if (cat_load_file_bytes(drive, data_used, &file_len, frame->err) != 0) {
    return -1;
  }

  parse_rc = cat_parse_lines(data_used, file_len, &line_count);
  if (parse_rc == -2) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_line);
    return -1;
  }
  if (parse_rc == -3) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_lines);
    return -1;
  }
  if (parse_rc != 0) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_state);
    return -1;
  }

  data_used = (unsigned short)(data_used + file_len);
  if (cat_meta_store(total, local, line_count, data_used) != 0) {
    rs_cmd_file_set_error(frame->err, 255u, g_cat_err_state);
    return -1;
  }

  frame->count = (unsigned short)(line_count + CAT_DEBUG_ITEM_COUNT);
  frame->used = data_used;
  frame->drive = drive;
  return 0;
}

static int cat_item(RSCommandFrame* frame) {
  unsigned short total;
  unsigned short local;
  unsigned short line_count;
  unsigned short data_used;

  if (!frame || !frame->out || frame->index >= frame->count) {
    return -1;
  }

#if DEBUG_RSCAT
  ++g_cat_overlay_ram_marker;
#endif

  if (cat_meta_load(&total, &local, &line_count, &data_used) != 0) {
    total = 0u;
    local = 0u;
    line_count = 0u;
    data_used = 0u;
  }
  ++total;
  ++local;
  if (cat_meta_store(total, local, line_count, data_used) != 0) {
    return -1;
  }

#if DEBUG_RSCAT
  if (frame->index == 0u) {
    cat_build_dbg_line("BEGIN", 0, total, local);
    rs_cmd_value_free(frame->out);
    return rs_cmd_value_init_string(frame->out, g_cat_line);
  }

  if (frame->index == 1u) {
    if (cat_read_text(0u, g_cat_payload, sizeof(g_cat_payload)) != 0) {
      return -1;
    }
    cat_build_dbg_line("PARAM", g_cat_payload, total, local);
    rs_cmd_value_free(frame->out);
    return rs_cmd_value_init_string(frame->out, g_cat_line);
  }

  if (frame->index == (unsigned short)(line_count + 2u)) {
    cat_build_dbg_line("END", 0, total, local);
    rs_cmd_value_free(frame->out);
    return rs_cmd_value_init_string(frame->out, g_cat_line);
  }
#endif

  if (cat_read_text((unsigned short)((frame->index - CAT_PAYLOAD_INDEX_BASE) + CAT_PAYLOAD_REC_BASE),
                    g_cat_payload,
                    sizeof(g_cat_payload)) != 0) {
    return -1;
  }
  rs_cmd_value_free(frame->out);
  return rs_cmd_value_init_string(frame->out, g_cat_payload);
}

int rs_vmovl_overlay7(unsigned char handler, RSCommandFrame* frame) {
  if (!frame) {
    return -1;
  }
  if (handler != RS_CMD_HANDLER_OVL7_CAT) {
    return -1;
  }
  if (frame->op == RS_CMD_OVL_OP_BEGIN) {
    return cat_begin(frame);
  }
  if (frame->op == RS_CMD_OVL_OP_ITEM) {
    return cat_item(frame);
  }
  return -1;
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
