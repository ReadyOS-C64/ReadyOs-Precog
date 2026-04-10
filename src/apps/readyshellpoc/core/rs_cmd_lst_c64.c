#include "rs_cmd_overlay.h"

#include "rs_cmd_value_local.h"
#include "../platform/rs_platform.h"

#include <cbm.h>
#include <string.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY5")
#pragma rodata-name(push, "OVERLAY5")
#pragma bss-name(push, "OVERLAY5")
#endif

#define LST_RECORD_SIZE 28u
#define LST_MAX_RECORDS (RS_CMD_SCRATCH_LEN / LST_RECORD_SIZE)
#define LST_NAME_OFF 2u
#define LST_NAME_LEN 17u
#define LST_EXT_OFF 19u
#define LST_EXT_LEN 8u

static unsigned char g_lst_buf[LST_RECORD_SIZE];

static void lst_name_from_dirent(const char* in, char* out, unsigned short max) {
  unsigned short i;
  unsigned short j;
  char c;
  i = 0u;
  j = 0u;
  while (i < 16u && in[i] != '\0' && j + 1u < max) {
    c = in[i++];
    if (c == '"') {
      continue;
    }
    out[j++] = c;
  }
  while (j > 0u && out[j - 1u] == ' ') {
    --j;
  }
  out[j] = '\0';
}

static void lst_ext_from_name(const char* name, char* ext, unsigned short max) {
  unsigned short i;
  unsigned short dot;
  unsigned short j;
  i = 0u;
  dot = 0xFFFFu;
  while (name[i] != '\0') {
    if (name[i] == '.') {
      dot = i;
    }
    ++i;
  }
  if (dot == 0xFFFFu || name[dot + 1u] == '\0') {
    ext[0] = '\0';
    return;
  }
  j = 0u;
  i = (unsigned short)(dot + 1u);
  while (name[i] != '\0' && j + 1u < max) {
    ext[j++] = name[i++];
  }
  ext[j] = '\0';
}

static void lst_clear_record(void) {
  unsigned char i;
  for (i = 0u; i < LST_RECORD_SIZE; ++i) {
    g_lst_buf[i] = 0u;
  }
}

static int lst_write_record(unsigned short index,
                            const char* name,
                            const char* ext,
                            unsigned short blocks) {
  unsigned short i;
  unsigned long off;
  lst_clear_record();
  g_lst_buf[0] = (unsigned char)(blocks & 0xFFu);
  g_lst_buf[1] = (unsigned char)((blocks >> 8u) & 0xFFu);
  for (i = 0u; i + 1u < LST_NAME_LEN && name[i] != '\0'; ++i) {
    g_lst_buf[LST_NAME_OFF + i] = (unsigned char)name[i];
  }
  g_lst_buf[LST_NAME_OFF + LST_NAME_LEN - 1u] = '\0';
  for (i = 0u; i + 1u < LST_EXT_LEN && ext[i] != '\0'; ++i) {
    g_lst_buf[LST_EXT_OFF + i] = (unsigned char)ext[i];
  }
  g_lst_buf[LST_EXT_OFF + LST_EXT_LEN - 1u] = '\0';
  off = RS_CMD_SCRATCH_OFF + ((unsigned long)index * (unsigned long)LST_RECORD_SIZE);
  return rs_reu_write(off, g_lst_buf, LST_RECORD_SIZE);
}

static int lst_read_record(unsigned short index) {
  unsigned long off;
  off = RS_CMD_SCRATCH_OFF + ((unsigned long)index * (unsigned long)LST_RECORD_SIZE);
  return rs_reu_read(off, g_lst_buf, LST_RECORD_SIZE);
}

static int lst_parse_drive(RSCommandFrame* frame, unsigned char* out_drive) {
  unsigned short drive16;
  if (!frame || !out_drive) {
    return -1;
  }
  if (frame->arg_count == 0u) {
    *out_drive = 8u;
    return 0;
  }
  if (rs_cmd_value_to_u16(&frame->args[0], &drive16) != 0 || drive16 > 255u) {
    return -1;
  }
  *out_drive = (unsigned char)drive16;
  return 0;
}

static int lst_begin(RSCommandFrame* frame) {
  unsigned char drive;
  unsigned char st;
  unsigned short count;
  struct cbm_dirent ent;

  if (lst_parse_drive(frame, &drive) != 0) {
    return -2;
  }
  if (cbm_opendir(1, drive) != 0) {
    return -1;
  }

  count = 0u;
  for (;;) {
    char name[20];
    char ext[8];
    st = cbm_readdir(1, &ent);
    if (st != 0u) {
      break;
    }
    if (count >= (unsigned short)LST_MAX_RECORDS) {
      cbm_closedir(1);
      return -3;
    }
    lst_name_from_dirent(ent.name, name, sizeof(name));
    lst_ext_from_name(name, ext, sizeof(ext));
    if (lst_write_record(count, name, ext, ent.size) != 0) {
      cbm_closedir(1);
      return -1;
    }
    ++count;
  }

  cbm_closedir(1);
  frame->drive = drive;
  frame->count = count;
  frame->index = 0u;
  return 0;
}

static int lst_item(RSCommandFrame* frame) {
  RSValue vname;
  RSValue vblocks;
  RSValue vext;
  unsigned short blocks;

  if (!frame || !frame->out || frame->index >= frame->count) {
    return -1;
  }
  if (lst_read_record(frame->index) != 0) {
    return -1;
  }

  blocks = (unsigned short)(g_lst_buf[0] | ((unsigned short)g_lst_buf[1] << 8u));
  rs_cmd_value_free(frame->out);
  if (rs_cmd_value_object_new(frame->out) != 0) {
    return -1;
  }

  rs_cmd_value_init_false(&vname);
  rs_cmd_value_init_false(&vblocks);
  rs_cmd_value_init_false(&vext);
  if (rs_cmd_value_init_string(&vname, (const char*)(g_lst_buf + LST_NAME_OFF)) != 0 ||
      rs_cmd_value_init_string(&vext, (const char*)(g_lst_buf + LST_EXT_OFF)) != 0) {
    rs_cmd_value_free(frame->out);
    return -1;
  }
  rs_cmd_value_init_u16(&vblocks, blocks);

  if (rs_cmd_object_set(frame->out, "NAME", &vname) != 0 ||
      rs_cmd_object_set(frame->out, "BLOCKS", &vblocks) != 0 ||
      rs_cmd_object_set(frame->out, "EXT", &vext) != 0) {
    rs_cmd_value_free(&vname);
    rs_cmd_value_free(&vext);
    rs_cmd_value_free(frame->out);
    return -1;
  }

  rs_cmd_value_free(&vname);
  rs_cmd_value_free(&vext);
  return 0;
}

int rs_vmovl_cmd_lst(RSCommandFrame* frame) {
  if (!frame) {
    return -1;
  }
  if (frame->op == RS_CMD_OVL_OP_BEGIN) {
    return lst_begin(frame);
  }
  if (frame->op == RS_CMD_OVL_OP_ITEM) {
    return lst_item(frame);
  }
  return -1;
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
