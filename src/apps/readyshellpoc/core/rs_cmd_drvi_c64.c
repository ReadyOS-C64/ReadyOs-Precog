#include "rs_cmd_overlay.h"

#include "rs_cmd_dir_local.h"
#include "rs_cmd_value_local.h"

#include <cbm.h>
#include <string.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY3")
#pragma rodata-name(push, "OVERLAY3")
#pragma bss-name(push, "OVERLAY3")
#endif

#define DRVI_LFN_CMD 15u

static void drvi_trim_field(char* s) {
  unsigned short i;
  unsigned short end;
  if (!s) {
    return;
  }
  end = (unsigned short)strlen(s);
  while (end > 0u && (s[end - 1u] == ' ' || (unsigned char)s[end - 1u] == 0xA0u)) {
    --end;
  }
  s[end] = '\0';
  i = 0u;
  while (s[i] == ' ' || (unsigned char)s[i] == 0xA0u) {
    ++i;
  }
  if (i != 0u) {
    memmove(s, s + i, strlen(s + i) + 1u);
  }
}

static void drvi_name_from_dirent(const char* src,
                                  char* out,
                                  unsigned short max) {
  unsigned short i;
  unsigned short j;
  if (!out || max == 0u) {
    return;
  }
  out[0] = '\0';
  if (!src) {
    return;
  }
  j = 0u;
  for (i = 0u; i < 16u && src[i] != '\0' && j + 1u < max; ++i) {
    if (src[i] == '"') {
      continue;
    }
    out[j++] = src[i];
  }
  out[j] = '\0';
  drvi_trim_field(out);
}

static void drvi_maybe_set_1571_mode(unsigned char drive) {
  if (drive != 8u && drive != 9u) {
    return;
  }
  rs_cmd_dir_cleanup_io();
  if (cbm_open(DRVI_LFN_CMD, drive, 15, "u0>m1") == 0) {
    cbm_close(DRVI_LFN_CMD);
  }
  rs_cmd_dir_cleanup_io();
}

static int drvi_make_object(unsigned char drive, RSValue* out) {
  RSValue vdrive;
  RSValue vdisk;
  RSValue vfree;
  unsigned short free_blocks;
  unsigned char st;
  char diskname[20];
  struct cbm_dirent ent;

  if (!out) {
    return -1;
  }

  diskname[0] = '\0';
  free_blocks = 0u;
  drvi_maybe_set_1571_mode(drive);
  if (rs_cmd_dir_open_header(drive, &ent) != 0) {
    return -1;
  }
  drvi_name_from_dirent(ent.name, diskname, sizeof(diskname));

  for (;;) {
    st = cbm_readdir(1, &ent);
    if (st != 0u) {
      if (st == 2u) {
        free_blocks = ent.size;
      }
      break;
    }
  }
  cbm_closedir(1);

  rs_cmd_value_free(out);
  if (rs_cmd_value_object_new(out) != 0) {
    return -1;
  }

  rs_cmd_value_init_u16(&vdrive, drive);
  rs_cmd_value_init_u16(&vfree, free_blocks);
  rs_cmd_value_init_false(&vdisk);

  if (rs_cmd_value_init_string(&vdisk, diskname) != 0) {
    rs_cmd_value_free(out);
    return -1;
  }

  if (rs_cmd_object_set(out, "DRIVE", &vdrive) != 0 ||
      rs_cmd_object_set(out, "DISKNAME", &vdisk) != 0 ||
      rs_cmd_object_set(out, "BLOCKSFREE", &vfree) != 0) {
    rs_cmd_value_free(&vdisk);
    rs_cmd_value_free(out);
    return -1;
  }

  rs_cmd_value_free(&vdisk);
  return 0;
}

int rs_vmovl_cmd_drvi(RSCommandFrame* frame) {
  unsigned short drive16;
  unsigned char drive;

  if (!frame || !frame->out) {
    return -1;
  }

  if (frame->arg_count == 0u) {
    drive = 8u;
  } else if (rs_cmd_value_to_u16(&frame->args[0], &drive16) == 0 && drive16 <= 255u) {
    drive = (unsigned char)drive16;
  } else {
    return -2;
  }

  frame->drive = drive;
  return drvi_make_object(drive, frame->out);
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
