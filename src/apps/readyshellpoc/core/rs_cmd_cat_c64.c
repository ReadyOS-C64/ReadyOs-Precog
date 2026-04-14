#include "rs_cmd_overlay.h"
#include "rs_cmd_registry.h"

#include "rs_cmd_file_local.h"

#include <cbm.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY7")
#pragma rodata-name(push, "OVERLAY7")
#pragma bss-name(push, "OVERLAY7")
#endif

#define CAT_REC_OFF   RS_CMD_SCRATCH_OFF
#define CAT_REC_LEN   4u
#define CAT_REC_CAP   (0x0800u / CAT_REC_LEN)
#define CAT_DATA_OFF  (RS_CMD_SCRATCH_OFF + 0x0800ul)
#define CAT_DATA_LEN  (RS_CMD_SCRATCH_LEN - 0x0800u)
#define CAT_BUF_LEN   64u
#define CAT_LINE_MAX  255u

static const char g_cat_line_too_long[] = "LINE TOO LONG";

static unsigned char g_cat_buf[CAT_BUF_LEN];
static char g_cat_line[CAT_LINE_MAX + 1u];

static int cat_write_rec(unsigned short index,
                         unsigned short start,
                         unsigned short len) {
  unsigned char rec[CAT_REC_LEN];
  rec[0] = (unsigned char)(start & 0xFFu);
  rec[1] = (unsigned char)((start >> 8u) & 0xFFu);
  rec[2] = (unsigned char)(len & 0xFFu);
  rec[3] = (unsigned char)((len >> 8u) & 0xFFu);
  return rs_reu_write(CAT_REC_OFF + ((unsigned long)index * (unsigned long)CAT_REC_LEN),
                      rec,
                      CAT_REC_LEN);
}

static int cat_read_rec(unsigned short index,
                        unsigned short* start,
                        unsigned short* len) {
  unsigned char rec[CAT_REC_LEN];
  if (!start || !len) {
    return -1;
  }
  if (rs_reu_read(CAT_REC_OFF + ((unsigned long)index * (unsigned long)CAT_REC_LEN),
                  rec,
                  CAT_REC_LEN) != 0) {
    return -1;
  }
  *start = (unsigned short)(rec[0] | ((unsigned short)rec[1] << 8u));
  *len = (unsigned short)(rec[2] | ((unsigned short)rec[3] << 8u));
  return 0;
}

static int cat_begin(RSCommandFrame* frame) {
  unsigned char drive;
  char name[17];
  unsigned short line_count;
  unsigned short line_start;
  unsigned short line_len;
  unsigned short data_used;
  int n;
  int i;

  if (!frame || !frame->args || frame->arg_count != 1u) {
    return -1;
  }
  if (rs_cmd_file_parse_embedded_name(&frame->args[0], &drive, name, sizeof(name)) != 0) {
    return -2;
  }
  if (rs_cmd_file_open_name(RS_CMD_FILE_LFN_DATA, drive, name, CBM_T_SEQ, 'r') != 0) {
    rs_cmd_file_note_status(frame->err, drive, 255u);
    frame->count = 0u;
    frame->used = 0u;
    return 0;
  }

  line_count = 0u;
  line_start = 0u;
  line_len = 0u;
  data_used = 0u;

  for (;;) {
    n = cbm_read(RS_CMD_FILE_LFN_DATA, g_cat_buf, CAT_BUF_LEN);
    if (n < 0) {
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_note_status(frame->err, drive, 255u);
      frame->count = 0u;
      frame->used = 0u;
      return 0;
    }
    if (n == 0) {
      break;
    }
    for (i = 0; i < n; ++i) {
      if (g_cat_buf[i] == '\r') {
        if (line_count >= CAT_REC_CAP || cat_write_rec(line_count, line_start, line_len) != 0) {
          cbm_close(RS_CMD_FILE_LFN_DATA);
          rs_cmd_file_cleanup_io();
          frame->count = 0u;
          frame->used = 0u;
          return 0;
        }
        ++line_count;
        line_start = data_used;
        line_len = 0u;
      } else if (g_cat_buf[i] != '\n') {
        if (line_len >= CAT_LINE_MAX || data_used >= CAT_DATA_LEN) {
          cbm_close(RS_CMD_FILE_LFN_DATA);
          rs_cmd_file_cleanup_io();
          rs_error_set(frame->err, RS_ERR_EXEC, g_cat_line_too_long, 0u, 1u, 1u);
          frame->count = 0u;
          frame->used = 0u;
          return 0;
        }
        if (rs_reu_write(CAT_DATA_OFF + (unsigned long)data_used, g_cat_buf + i, 1u) != 0) {
          cbm_close(RS_CMD_FILE_LFN_DATA);
          rs_cmd_file_cleanup_io();
          frame->count = 0u;
          frame->used = 0u;
          return 0;
        }
        ++data_used;
        ++line_len;
      }
    }
  }

  cbm_close(RS_CMD_FILE_LFN_DATA);
  rs_cmd_file_cleanup_io();

  if (line_len != 0u) {
    if (line_count >= CAT_REC_CAP || cat_write_rec(line_count, line_start, line_len) != 0) {
      frame->count = 0u;
      frame->used = 0u;
      return 0;
    }
    ++line_count;
  }

  frame->drive = drive;
  frame->count = line_count;
  frame->used = data_used;
  return 0;
}

static int cat_item(RSCommandFrame* frame) {
  unsigned short start;
  unsigned short len;

  if (!frame || !frame->out || frame->index >= frame->count) {
    return -1;
  }
  if (cat_read_rec(frame->index, &start, &len) != 0 || len > CAT_LINE_MAX) {
    return -1;
  }
  if (len != 0u &&
      rs_reu_read(CAT_DATA_OFF + (unsigned long)start, g_cat_line, len) != 0) {
    return -1;
  }
  g_cat_line[len] = '\0';
  rs_cmd_value_free(frame->out);
  return rs_cmd_value_init_string(frame->out, g_cat_line);
}

int rs_vmovl_overlay7(unsigned char handler, RSCommandFrame* frame) {
  if (!frame) {
    return -1;
  }
  if (handler == RS_CMD_HANDLER_OVL7_CAT) {
    if (frame->op == RS_CMD_OVL_OP_BEGIN) {
      return cat_begin(frame);
    }
    if (frame->op == RS_CMD_OVL_OP_ITEM) {
      return cat_item(frame);
    }
    return -1;
  }
  return -1;
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
