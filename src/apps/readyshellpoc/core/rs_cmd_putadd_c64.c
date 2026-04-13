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

#define RS_CMD_SESSION_EPOCH (*(unsigned char*)0xCFF1)

#define PUTADD_META_OFF  RS_CMD_SCRATCH_OFF
#define PUTADD_DATA_OFF  (RS_CMD_SCRATCH_OFF + 0x0020ul)
#define PUTADD_DATA_LEN  (RS_CMD_SCRATCH_LEN - 0x0020u)
#define PUTADD_KIND_PUT  1u
#define PUTADD_KIND_ADD  2u

static const char g_putadd_need_string[] = "NEED STRING";
static const char g_putadd_need_seq[] = "NEED SEQ";

static unsigned char g_putadd_buf[96];

static int putadd_meta_write(unsigned char epoch,
                             unsigned char kind,
                             unsigned char drive,
                             const char* name,
                             unsigned short used) {
  unsigned char i;

  memset(g_putadd_buf, 0, 24u);
  g_putadd_buf[0] = 'P';
  g_putadd_buf[1] = 'A';
  g_putadd_buf[2] = epoch;
  g_putadd_buf[3] = kind;
  g_putadd_buf[4] = drive;
  g_putadd_buf[5] = (unsigned char)(used & 0xFFu);
  g_putadd_buf[6] = (unsigned char)((used >> 8u) & 0xFFu);
  for (i = 0u; i < 16u && name[i] != '\0'; ++i) {
    g_putadd_buf[7u + i] = (unsigned char)name[i];
  }
  return rs_reu_write(PUTADD_META_OFF, g_putadd_buf, 24u);
}

static int putadd_meta_read(unsigned char* epoch,
                            unsigned char* kind,
                            unsigned char* drive,
                            char* name,
                            unsigned short* used) {
  unsigned char i;

  if (!epoch || !kind || !drive || !name || !used) {
    return -1;
  }
  if (rs_reu_read(PUTADD_META_OFF, g_putadd_buf, 24u) != 0 ||
      g_putadd_buf[0] != 'P' || g_putadd_buf[1] != 'A') {
    return -1;
  }
  *epoch = g_putadd_buf[2];
  *kind = g_putadd_buf[3];
  *drive = g_putadd_buf[4];
  *used = (unsigned short)(g_putadd_buf[5] | ((unsigned short)g_putadd_buf[6] << 8u));
  for (i = 0u; i < 16u; ++i) {
    name[i] = (char)g_putadd_buf[7u + i];
    if (name[i] == '\0') {
      break;
    }
  }
  name[16] = '\0';
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

static int putadd_touch_file(unsigned char drive, const char* name, RSError* err) {
  if (rs_cmd_file_open_name(RS_CMD_FILE_LFN_DATA, drive, name, CBM_T_SEQ, 'w') != 0) {
    rs_cmd_file_note_status(err, drive, 255u);
    return -1;
  }
  cbm_close(RS_CMD_FILE_LFN_DATA);
  rs_cmd_file_cleanup_io();
  return putadd_check_write_status(drive, err);
}

static int putadd_write_all(unsigned char drive,
                            const char* name,
                            unsigned short used,
                            RSError* err) {
  unsigned short pos;
  unsigned short chunk;
  int wrote;

  if (rs_cmd_file_open_name(RS_CMD_FILE_LFN_DATA, drive, name, CBM_T_SEQ, 'w') != 0) {
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

static int putadd_append_line(const char* text, unsigned short* used) {
  unsigned short len;
  static const char cr = '\r';

  if (!text || !used) {
    return -1;
  }
  len = (unsigned short)strlen(text);
  if ((unsigned long)*used + (unsigned long)len + 1ul > (unsigned long)PUTADD_DATA_LEN) {
    return -1;
  }
  if (len != 0u &&
      rs_reu_write(PUTADD_DATA_OFF + (unsigned long)*used, text, len) != 0) {
    return -1;
  }
  *used = (unsigned short)(*used + len);
  if (rs_reu_write(PUTADD_DATA_OFF + (unsigned long)*used, &cr, 1u) != 0) {
    return -1;
  }
  ++*used;
  return 0;
}

static int putadd_load_existing(unsigned char drive,
                                const char* name,
                                unsigned short* used,
                                RSError* err) {
  unsigned char type;
  int n;

  if (!used) {
    return -1;
  }
  if (rs_cmd_file_find_entry(drive, name, &type, 0) != 0) {
    *used = 0u;
    return 0;
  }
  if (type != CBM_T_SEQ) {
    rs_cmd_file_set_error(err, 255u, g_putadd_need_seq);
    return -1;
  }
  if (rs_cmd_file_open_name(RS_CMD_FILE_LFN_DATA, drive, name, CBM_T_SEQ, 'r') != 0) {
    rs_cmd_file_note_status(err, drive, 255u);
    return -1;
  }

  *used = 0u;
  for (;;) {
    n = cbm_read(RS_CMD_FILE_LFN_DATA, g_putadd_buf, sizeof(g_putadd_buf));
    if (n < 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_note_status(err, drive, 255u);
      return -1;
    }
    if (n == 0) {
      break;
    }
    if ((unsigned long)*used + (unsigned long)n > (unsigned long)PUTADD_DATA_LEN ||
        rs_reu_write(PUTADD_DATA_OFF + (unsigned long)*used,
                     g_putadd_buf,
                     (unsigned short)n) != 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      return -1;
    }
    *used = (unsigned short)(*used + (unsigned short)n);
  }

  cbm_close(RS_CMD_FILE_LFN_DATA);
  rs_cmd_file_cleanup_io();
  return 0;
}

static int putadd_session_used(unsigned char kind,
                               unsigned char drive,
                               const char* name,
                               unsigned short* used,
                               RSError* err) {
  unsigned char epoch;
  unsigned char saved_kind;
  unsigned char saved_drive;
  char saved_name[17];

  if (!used) {
    return -1;
  }
  if (putadd_meta_read(&epoch, &saved_kind, &saved_drive, saved_name, used) == 0 &&
      epoch == RS_CMD_SESSION_EPOCH &&
      saved_kind == kind &&
      saved_drive == drive &&
      rs_cmd_file_str_eq(saved_name, name)) {
    return 0;
  }

  *used = 0u;
  if (kind == PUTADD_KIND_ADD &&
      putadd_load_existing(drive, name, used, err) != 0) {
    return -1;
  }
  return putadd_meta_write(RS_CMD_SESSION_EPOCH, kind, drive, name, *used);
}

static int putadd_run(RSCommandFrame* frame, unsigned char kind) {
  unsigned char drive;
  unsigned short used;
  char name[17];
  const char* text;

  if (!frame || !frame->out || !frame->args || frame->arg_count != 1u) {
    return -1;
  }
  if (rs_cmd_file_parse_embedded_name(&frame->args[0], &drive, name, sizeof(name)) != 0) {
    return -2;
  }

  rs_cmd_value_free(frame->out);
  rs_cmd_value_init_bool(frame->out, 0);

  if (!frame->item) {
    if (kind == PUTADD_KIND_ADD) {
      unsigned char type;
      if (rs_cmd_file_find_entry(drive, name, &type, 0) == 0) {
        if (type != CBM_T_SEQ) {
          rs_cmd_file_set_error(frame->err, 255u, g_putadd_need_seq);
          return 0;
        }
        rs_cmd_value_init_bool(frame->out, 1);
        return 0;
      }
    }
    rs_cmd_value_init_bool(frame->out, putadd_touch_file(drive, name, frame->err) == 0);
    return 0;
  }

  text = rs_cmd_value_cstr(frame->item);
  if (!text) {
    rs_cmd_file_set_error(frame->err, 255u, g_putadd_need_string);
    return 0;
  }

  if (putadd_session_used(kind, drive, name, &used, frame->err) != 0 ||
      putadd_append_line(text, &used) != 0 ||
      putadd_meta_write(RS_CMD_SESSION_EPOCH, kind, drive, name, used) != 0) {
    if (frame->err && frame->err->code == RS_ERR_NONE) {
      rs_cmd_file_set_error(frame->err, 255u, rs_cmd_file_err_text);
    }
    return 0;
  }

  rs_cmd_value_init_bool(frame->out, putadd_write_all(drive, name, used, frame->err) == 0);
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
