#include "rs_cmd_overlay.h"
#include "rs_cmd_registry.h"

#include "rs_cmd_file_local.h"

#include <cbm.h>
#include <cbm_filetype.h>
#include <string.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY7")
#pragma rodata-name(push, "OVERLAY7")
#pragma bss-name(push, "OVERLAY7")
#endif

static const char g_copy_need_name[] = "NEED NAME";
static const char g_copy_need_new[] = "NEED NEW NAME";
static const char g_copy_need_file[] = "FILE NOT FOUND";
static const char g_copy_unsupported[] = "UNSUPPORTED";

static unsigned char g_copy_buf[128];

static char* copy_append(char* dst, const char* src) {
  while (*dst != '\0') {
    ++dst;
  }
  while (*src != '\0') {
    *dst++ = *src++;
  }
  *dst = '\0';
  return dst;
}

static int copy_parse_dest(const RSValue* arg,
                           const char* src_name,
                           unsigned char* out_drive,
                           char* out_name,
                           unsigned short max) {
  unsigned short drive16;

  if (!arg || !out_drive || !out_name) {
    return -1;
  }
  if (rs_cmd_value_to_u16(arg, &drive16) == 0) {
    if (drive16 < 8u || drive16 > 11u) {
      return -1;
    }
    *out_drive = (unsigned char)drive16;
    strncpy(out_name, src_name, max - 1u);
    out_name[max - 1u] = '\0';
    return 0;
  }
  return rs_cmd_file_parse_embedded_name(arg, out_drive, out_name, max);
}

static int copy_is_regular(unsigned char type) {
  return type == CBM_T_SEQ || type == CBM_T_PRG || type == CBM_T_USR;
}

static int copy_stream_file(unsigned char src_drive,
                            const char* src_name,
                            unsigned char dst_drive,
                            const char* dst_name,
                            unsigned char type,
                            RSError* err) {
  int nread;
  int nwrote;

  if (rs_cmd_file_open_name(RS_CMD_FILE_LFN_DATA, src_drive, src_name, type, 'r') != 0) {
    rs_cmd_file_note_status(err, src_drive, 255u);
    return -1;
  }
  if (rs_cmd_file_open_name(3u, dst_drive, dst_name, type, 'w') != 0) {
    cbm_close(RS_CMD_FILE_LFN_DATA);
    rs_cmd_file_cleanup_io();
    rs_cmd_file_note_status(err, dst_drive, 255u);
    return -1;
  }

  for (;;) {
    nread = cbm_read(RS_CMD_FILE_LFN_DATA, g_copy_buf, sizeof(g_copy_buf));
    if (nread < 0) {
      cbm_close(3u);
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_note_status(err, src_drive, 255u);
      return -1;
    }
    if (nread == 0) {
      break;
    }
    nwrote = cbm_write(3u, g_copy_buf, (unsigned int)nread);
    if (nwrote != nread) {
      cbm_close(3u);
      cbm_close(RS_CMD_FILE_LFN_DATA);
      rs_cmd_file_cleanup_io();
      rs_cmd_file_note_status(err, dst_drive, 255u);
      return -1;
    }
  }

  cbm_close(3u);
  cbm_close(RS_CMD_FILE_LFN_DATA);
  rs_cmd_file_cleanup_io();
  if (rs_cmd_file_fetch_status(dst_drive,
                               g_copy_buf,
                               rs_cmd_file_status_msg,
                               sizeof(rs_cmd_file_status_msg)) == 0 &&
      g_copy_buf[0] <= 1u) {
    return 0;
  }
  rs_cmd_file_set_error(err, g_copy_buf[0], rs_cmd_file_status_msg);
  return -1;
}

static int copy_run(RSCommandFrame* frame) {
  unsigned char src_drive;
  unsigned char dst_drive;
  unsigned char type;
  char src_name[17];
  char dst_name[17];
  char cmd[40];

  if (!frame || !frame->out || !frame->args || frame->arg_count != 2u) {
    return -1;
  }
  if (rs_cmd_file_parse_embedded_name(&frame->args[0], &src_drive, src_name, sizeof(src_name)) != 0 ||
      copy_parse_dest(&frame->args[1], src_name, &dst_drive, dst_name, sizeof(dst_name)) != 0) {
    return -2;
  }

  rs_cmd_value_free(frame->out);
  rs_cmd_value_init_bool(frame->out, 0);

  if (src_name[0] == '\0' || dst_name[0] == '\0') {
    rs_cmd_file_set_error(frame->err, 255u, g_copy_need_name);
    return 0;
  }
  if (src_drive == dst_drive && rs_cmd_file_str_eq(src_name, dst_name)) {
    rs_cmd_file_set_error(frame->err, 255u, g_copy_need_new);
    return 0;
  }
  if (rs_cmd_file_find_entry(src_drive, src_name, &type, 0) != 0) {
    rs_cmd_file_set_error(frame->err, 62u, g_copy_need_file);
    return 0;
  }
  if (!copy_is_regular(type)) {
    rs_cmd_file_set_error(frame->err, 255u, g_copy_unsupported);
    return 0;
  }

  if (src_drive == dst_drive) {
    strcpy(cmd, "c0:");
    copy_append(cmd, dst_name);
    copy_append(cmd, "=");
    copy_append(cmd, src_name);
    rs_cmd_value_init_bool(frame->out,
                           rs_cmd_file_run_command(src_drive, cmd, frame->err) == 0);
    return 0;
  }

  rs_cmd_value_init_bool(frame->out,
                         copy_stream_file(src_drive,
                                          src_name,
                                          dst_drive,
                                          dst_name,
                                          type,
                                          frame->err) == 0);
  return 0;
}

int rs_vmovl_overlay7_copy(unsigned char handler, RSCommandFrame* frame) {
  if (handler == RS_CMD_HANDLER_OVL7_COPY && frame && frame->op == RS_CMD_OVL_OP_RUN) {
    return copy_run(frame);
  }
  return -1;
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
