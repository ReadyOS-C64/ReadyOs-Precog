#include "rs_cmd_overlay.h"
#include "rs_cmd_registry.h"

#include "rs_cmd_file_local.h"

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY6")
#pragma rodata-name(push, "OVERLAY6")
#pragma bss-name(push, "OVERLAY6")
#endif

int rs_vmovl_overlay6_putadd(unsigned char handler, RSCommandFrame* frame);

static char* delren_append(char* dst, const char* src) {
  while (*dst != '\0') {
    ++dst;
  }
  while (*src != '\0') {
    *dst++ = *src++;
  }
  *dst = '\0';
  return dst;
}

static int del_run(RSCommandFrame* frame) {
  unsigned char drive;
  char name[17];
  char cmd[24];

  if (!frame || !frame->out || !frame->args || frame->arg_count != 1u) {
    return -1;
  }
  if (rs_cmd_file_parse_embedded_name(&frame->args[0], &drive, name, sizeof(name)) != 0) {
    return -2;
  }

  strcpy(cmd, "s:");
  delren_append(cmd, name);
  rs_cmd_value_free(frame->out);
  rs_cmd_value_init_bool(frame->out,
                         rs_cmd_file_run_command(drive, cmd, frame->err) == 0);
  return 0;
}

static int ren_run(RSCommandFrame* frame) {
  unsigned char drive;
  char old_name[17];
  char new_name[17];
  char cmd[40];

  if (!frame || !frame->out || !frame->args ||
      frame->arg_count < 2u || frame->arg_count > 3u) {
    return -1;
  }
  if (rs_cmd_file_parse_plain_name(&frame->args[0], old_name, sizeof(old_name)) != 0 ||
      rs_cmd_file_parse_plain_name(&frame->args[1], new_name, sizeof(new_name)) != 0) {
    return -2;
  }
  if (rs_cmd_file_parse_optional_drive(frame->arg_count > 2u ? &frame->args[2] : 0,
                                       rs_cmd_file_default_drive(),
                                       &drive) != 0) {
    return -2;
  }
  rs_cmd_value_free(frame->out);
  if (rs_cmd_file_str_eq(old_name, new_name)) {
    rs_cmd_file_set_error(frame->err, 255u, "NEED NEW NAME");
    rs_cmd_value_init_bool(frame->out, 0);
  } else {
    strcpy(cmd, "r:");
    delren_append(cmd, new_name);
    delren_append(cmd, "=");
    delren_append(cmd, old_name);
    rs_cmd_value_init_bool(frame->out,
                           rs_cmd_file_run_command(drive, cmd, frame->err) == 0);
  }
  return 0;
}

int rs_vmovl_overlay6(unsigned char handler, RSCommandFrame* frame) {
  if (handler == RS_CMD_HANDLER_OVL6_DEL) {
    return del_run(frame);
  }
  if (handler == RS_CMD_HANDLER_OVL6_REN) {
    return ren_run(frame);
  }
  return rs_vmovl_overlay6_putadd(handler, frame);
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
