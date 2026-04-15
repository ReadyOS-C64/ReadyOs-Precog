#include "rs_cmd_overlay.h"
#include "rs_cmd_registry.h"

#include "rs_cmd_file_local.h"

#include <cbm.h>
#include <string.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY6")
#pragma rodata-name(push, "OVERLAY6")
#pragma bss-name(push, "OVERLAY6")
#endif

#define PUTADD_DATA_OFF  (RS_CMD_SCRATCH_OFF + 0x0020ul)
#define PUTADD_DATA_LEN  (RS_CMD_SCRATCH_LEN - 0x0020u)
#define PUTADD_REU_BANK_BASE   (RS_CMD_SCRATCH_OFF & 0xFF0000ul)
#define PUTADD_KIND_PUT  1u
#define PUTADD_KIND_ADD  2u
#define PUTADD_REC_FALSE 1u
#define PUTADD_REC_TRUE  2u
#define PUTADD_REC_U16   3u
#define PUTADD_REC_STR   4u
#define PUTADD_REC_ARRAY 5u
#define PUTADD_REC_OBJECT 6u

static const char g_putadd_need_value[] = "NEED STRING/ARRAY";
static const char g_putadd_need_seq[] = "NEED SEQ";
static const char g_putadd_put_ok[] = "CREATED";

static unsigned char g_putadd_buf[80];

static int putadd_heap_read_u8(unsigned short rel_off, unsigned char* out) {
  return rs_reu_read(PUTADD_REU_BANK_BASE + (unsigned long)rel_off, out, 1u);
}

static int putadd_heap_read_u16(unsigned short rel_off, unsigned short* out) {
  unsigned char b[2];
  if (!out ||
      rs_reu_read(PUTADD_REU_BANK_BASE + (unsigned long)rel_off, b, 2u) != 0) {
    return -1;
  }
  *out = (unsigned short)(b[0] | ((unsigned short)b[1] << 8u));
  return 0;
}

static int putadd_check_write_status(unsigned char drive, RSError* err) {
  unsigned char code;

  rs_cmd_file_status_msg[0] = '\0';
  if (rs_cmd_file_fetch_status(drive,
                               &code,
                               rs_cmd_file_status_msg,
                               sizeof(rs_cmd_file_status_msg)) != 0) {
    rs_cmd_file_set_error(err, 255u, rs_cmd_file_err_text);
    return -1;
  }
  if (code > 1u) {
    rs_cmd_file_set_error(err, code, rs_cmd_file_status_msg);
    return -1;
  }
  return 0;
}

static int putadd_write_spool(unsigned char drive,
                              const char* name,
                              unsigned short used,
                              char mode,
                              RSError* err) {
  unsigned short pos;
  unsigned short chunk;
  int wrote;

  if (rs_cmd_file_open_name(RS_CMD_FILE_LFN_DATA, drive, name, CBM_T_SEQ, mode) != 0) {
    rs_cmd_file_note_status(err, drive, 255u);
    return -1;
  }

  pos = 0u;
  while (pos < used) {
    chunk = (unsigned short)(used - pos);
    if (chunk > (unsigned short)sizeof(g_putadd_buf)) {
      chunk = (unsigned short)sizeof(g_putadd_buf);
    }
    if (rs_reu_read(PUTADD_DATA_OFF + (unsigned long)pos, g_putadd_buf, chunk) != 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      return -1;
    }
    wrote = cbm_write(RS_CMD_FILE_LFN_DATA, g_putadd_buf, chunk);
    if (wrote != (int)chunk) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_note_status(err, drive, 255u);
      return -1;
    }
    pos = (unsigned short)(pos + chunk);
  }

  cbm_close(RS_CMD_FILE_LFN_DATA);
  rs_cmd_file_cleanup_io();
  return putadd_check_write_status(drive, err);
}

static int putadd_check_existing_seq(unsigned char drive,
                                     const char* name,
                                     unsigned char* exists_out,
                                     RSError* err) {
  unsigned char type;

  if (!name || !exists_out) {
    return -1;
  }
  if (rs_cmd_file_find_entry(drive, name, &type, 0) != 0) {
    *exists_out = 0u;
    return 0;
  }
  if (type != CBM_T_SEQ) {
    rs_cmd_file_set_error(err, 255u, g_putadd_need_seq);
    return -1;
  }
  *exists_out = 1u;
  return 0;
}

static int putadd_scratch_existing(unsigned char drive,
                                   const char* name,
                                   RSError* err) {
  char cmd[24];

  if (!name) {
    return -1;
  }
  cmd[0] = 's';
  cmd[1] = ':';
  strcpy(cmd + 2u, name);
  return rs_cmd_file_run_command(drive, cmd, err);
}

static int putadd_append_bytes(const char* text,
                               unsigned short len,
                               unsigned short* used) {
  if (!text || !used) {
    return -1;
  }
  if ((unsigned long)*used + (unsigned long)len > (unsigned long)PUTADD_DATA_LEN) {
    return -1;
  }
  if (len != 0u &&
      rs_reu_write(PUTADD_DATA_OFF + (unsigned long)*used, text, len) != 0) {
    return -1;
  }
  *used = (unsigned short)(*used + len);
  return 0;
}

static int putadd_append_reu_bytes(unsigned short src_rel_off,
                                   unsigned short len,
                                   unsigned short* used) {
  unsigned short copied;
  unsigned short chunk;

  if (!used) {
    return -1;
  }
  if ((unsigned long)*used + (unsigned long)len > (unsigned long)PUTADD_DATA_LEN) {
    return -1;
  }
  copied = 0u;
  while (copied < len) {
    chunk = (unsigned short)(len - copied);
    if (chunk > (unsigned short)sizeof(g_putadd_buf)) {
      chunk = (unsigned short)sizeof(g_putadd_buf);
    }
    if (rs_reu_read(PUTADD_REU_BANK_BASE +
                    (unsigned long)src_rel_off +
                    (unsigned long)copied,
                    g_putadd_buf,
                    chunk) != 0 ||
        rs_reu_write(PUTADD_DATA_OFF + (unsigned long)*used,
                     g_putadd_buf,
                     chunk) != 0) {
      return -1;
    }
    *used = (unsigned short)(*used + chunk);
    copied = (unsigned short)(copied + chunk);
  }
  return 0;
}

static int putadd_append_line_break(unsigned short* used) {
  g_putadd_buf[0] = '\r';
  if (!used ||
      (unsigned long)*used + 1ul > (unsigned long)PUTADD_DATA_LEN ||
      rs_reu_write(PUTADD_DATA_OFF + (unsigned long)*used, g_putadd_buf, 1u) != 0) {
    return -1;
  }
  *used = (unsigned short)(*used + 1u);
  return 0;
}

static int putadd_append_text_line(const char* text,
                                   unsigned short len,
                                   unsigned short* used) {
  if (putadd_append_bytes(text, len, used) != 0) {
    return -1;
  }
  return putadd_append_line_break(used);
}

static unsigned char putadd_u16_text(unsigned short value, char* out) {
  char rev[5];
  unsigned char i;
  unsigned char n;

  if (!out) {
    return 0u;
  }
  if (value == 0u) {
    out[0] = '0';
    out[1] = '\0';
    return 1u;
  }

  n = 0u;
  while (value != 0u && n < (unsigned char)sizeof(rev)) {
    rev[n++] = (char)('0' + (value % 10u));
    value = (unsigned short)(value / 10u);
  }
  for (i = 0u; i < n; ++i) {
    out[i] = rev[n - 1u - i];
  }
  out[n] = '\0';
  return n;
}

static int putadd_append_heap_scalar_line(unsigned short off, unsigned short* used) {
  unsigned char rec_type;
  unsigned short len;

  if (!used || putadd_heap_read_u8(off, &rec_type) != 0) {
    return -1;
  }
  if (rec_type == PUTADD_REC_STR) {
    if (putadd_heap_read_u16((unsigned short)(off + 1u), &len) != 0) {
      return -1;
    }
    if (putadd_append_reu_bytes((unsigned short)(off + 3u), len, used) != 0) {
      return -1;
    }
    return putadd_append_line_break(used);
  }
  return -1;
}

static int putadd_append_scalar_line(const RSValue* value, unsigned short* used) {
  if (!value || !used) {
    return -1;
  }
  if (value->tag == RS_VAL_STR) {
    return putadd_append_text_line(value->as.str.bytes,
                                   (unsigned short)value->as.str.len,
                                   used);
  }
  if (value->tag == RS_VAL_STR_PTR) {
    if (putadd_append_reu_bytes((unsigned short)(value->as.ptr.off + 3u),
                                value->as.ptr.len,
                                used) != 0) {
      return -1;
    }
    return putadd_append_line_break(used);
  }
  return -1;
}

static int putadd_append_value(const RSValue* value,
                               unsigned short* used,
                               unsigned short* line_count) {
  unsigned short i;
  unsigned short child_off;

  if (!value || !used || !line_count) {
    return -1;
  }
  if (value->tag == RS_VAL_ARRAY) {
    for (i = 0u; i < value->as.array.count; ++i) {
      if (putadd_append_scalar_line(&value->as.array.items[i], used) != 0) {
        return -1;
      }
      ++*line_count;
    }
    return 0;
  }
  if (value->tag == RS_VAL_ARRAY_PTR) {
    for (i = 0u; i < value->as.ptr.len; ++i) {
      if (putadd_heap_read_u16((unsigned short)(value->as.ptr.off + 3u + (i * 2u)),
                               &child_off) != 0 ||
          putadd_append_heap_scalar_line(child_off, used) != 0) {
        return -1;
      }
      ++*line_count;
    }
    return 0;
  }
  if (putadd_append_scalar_line(value, used) != 0) {
    return -1;
  }
  *line_count = (unsigned short)(*line_count + 1u);
  return 0;
}

static void putadd_set_success_line(RSCommandFrame* frame,
                                    unsigned char kind,
                                    unsigned short line_count) {
  unsigned char len;

  if (!frame) {
    return;
  }
  if (kind == PUTADD_KIND_PUT) {
    strncpy(frame->line, g_putadd_put_ok, RS_CMD_FRAME_LINE_CAP - 1u);
    frame->line[RS_CMD_FRAME_LINE_CAP - 1u] = '\0';
    frame->flags |= RS_CMD_FRAME_F_PRT_LINE;
    return;
  }

  len = putadd_u16_text(line_count, frame->line);
  if (line_count == 1u) {
    memcpy(frame->line + len, " LINE ADDED", 12u);
    frame->line[len + 11u] = '\0';
  } else {
    memcpy(frame->line + len, " LINES ADDED", 13u);
    frame->line[len + 12u] = '\0';
  }
  frame->flags |= RS_CMD_FRAME_F_PRT_LINE;
}

static int putadd_run(RSCommandFrame* frame, unsigned char kind) {
  unsigned char drive;
  unsigned char exists;
  unsigned short used;
  unsigned short line_count;
  char name[17];
  int ok;

  if (!frame || !frame->out || !frame->args) {
    return -1;
  }
  if (frame->arg_count != 2u) {
    return -2;
  }
  if (rs_cmd_file_parse_embedded_name(&frame->args[1], &drive, name, sizeof(name)) != 0) {
    return -2;
  }

  rs_cmd_value_free(frame->out);
  rs_cmd_value_init_bool(frame->out, 0);
  exists = 0u;
  used = 0u;
  line_count = 0u;
  ok = 0;

  if (putadd_append_value(&frame->args[0], &used, &line_count) != 0) {
    rs_cmd_file_set_error(frame->err, 255u, g_putadd_need_value);
    return 0;
  }
  if (kind == PUTADD_KIND_PUT) {
    ok = (putadd_write_spool(drive, name, used, 'w', frame->err) == 0);
    if (!ok && frame->err && frame->err->offset == 63u &&
        putadd_check_existing_seq(drive, name, &exists, frame->err) == 0 &&
        exists &&
        putadd_scratch_existing(drive, name, frame->err) == 0) {
      ok = (putadd_write_spool(drive, name, used, 'w', frame->err) == 0);
    }
  } else {
    ok = (putadd_write_spool(drive, name, used, 'a', frame->err) == 0);
    if (!ok && frame->err &&
        (frame->err->offset == 50u || frame->err->offset == 62u)) {
      ok = (putadd_write_spool(drive, name, used, 'w', frame->err) == 0);
    } else if (!ok) {
      (void)putadd_check_existing_seq(drive, name, &exists, frame->err);
    }
  }
  if (ok) {
    putadd_set_success_line(frame, kind, line_count);
    rs_cmd_value_init_bool(frame->out, 1);
  }
  return 0;
}

int rs_vmovl_overlay6_putadd(unsigned char handler, RSCommandFrame* frame) {
  if (handler == RS_CMD_HANDLER_OVL6_PUT) {
    return putadd_run(frame, PUTADD_KIND_PUT);
  }
  if (handler == RS_CMD_HANDLER_OVL6_ADD) {
    return putadd_run(frame, PUTADD_KIND_ADD);
  }
  return -1;
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
