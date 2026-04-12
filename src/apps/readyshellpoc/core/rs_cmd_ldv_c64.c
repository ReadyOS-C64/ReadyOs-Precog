#include "rs_cmd_overlay.h"

#include "rs_cmd_ldv_local.h"
#include "rs_cmd_ser_local.h"

#include <cbm.h>
#include <string.h>

#if defined(__CC65__)
#pragma code-name(push, "OVERLAY5")
#pragma rodata-name(push, "OVERLAY5")
#pragma bss-name(push, "OVERLAY5")
#endif

static int ldv_name_with_mode(const char* path, char* out, unsigned short max) {
  unsigned short n;
  unsigned short i;
  int has_meta;
  if (!path || !out || max < 8u) {
    return -1;
  }
  has_meta = 0;
  n = (unsigned short)strlen(path);
  for (i = 0u; i < n; ++i) {
    if (path[i] == ':' || path[i] == ',') {
      has_meta = 1;
      break;
    }
  }
  if (has_meta) {
    if (n + 1u > max) {
      return -1;
    }
    memcpy(out, path, n + 1u);
    return 0;
  }
  if ((unsigned long)n + 8ul > (unsigned long)max) {
    return -1;
  }
  out[0] = '0';
  out[1] = ':';
  memcpy(out + 2u, path, n);
  out[2u + n] = ',';
  out[3u + n] = 's';
  out[4u + n] = ',';
  out[5u + n] = 'r';
  out[6u + n] = '\0';
  return 0;
}

static int ldv_read_file_to_reu(const char* path, unsigned short* out_len) {
  char namebuf[96];
  int n;
  unsigned short total;

  if (!path || !out_len || ldv_name_with_mode(path, namebuf, sizeof(namebuf)) != 0) {
    return -1;
  }
  if (cbm_open(2, 8, CBM_READ, namebuf) != 0) {
    cbm_k_clrch();
    return -1;
  }

  total = 0u;
  for (;;) {
    n = cbm_read(2, rs_cmd_ser_buf, sizeof(rs_cmd_ser_buf));
    if (n < 0) {
      cbm_close(2);
      cbm_k_clrch();
      return -1;
    }
    if (n == 0) {
      break;
    }
    if ((unsigned long)total + (unsigned long)n > (unsigned long)RS_CMD_SCRATCH_LEN) {
      cbm_close(2);
      cbm_k_clrch();
      return -1;
    }
    if (rs_reu_write(RS_CMD_SCRATCH_OFF + (unsigned long)total,
                     rs_cmd_ser_buf,
                     (unsigned short)n) != 0) {
      cbm_close(2);
      cbm_k_clrch();
      return -1;
    }
    total = (unsigned short)(total + (unsigned short)n);
  }

  cbm_close(2);
  cbm_k_clrch();
  *out_len = total;
  return 0;
}

static int ldv_validate_header(unsigned short len, unsigned short* payload_len) {
  unsigned char h[6];
  if (!payload_len || len < 6u) {
    return -1;
  }
  if (rs_reu_read(RS_CMD_SCRATCH_OFF, h, 6u) != 0) {
    return -1;
  }
  if (h[0] != 'R' || h[1] != 'S' || h[2] != 'V' || h[3] != '1') {
    return -1;
  }
  *payload_len = (unsigned short)(h[4] | ((unsigned short)h[5] << 8u));
  if ((unsigned short)(*payload_len + 6u) != len) {
    return -1;
  }
  return 0;
}

static int ldv_begin(RSCommandFrame* frame) {
  const char* path;
  unsigned short len;
  unsigned short payload_len;
  unsigned short pos;
  unsigned short root_off;
  RSValue root;

  if (!frame || !frame->args || frame->arg_count < 1u) {
    return -1;
  }
  path = rs_cmd_value_cstr(&frame->args[0]);
  if (!path) {
    return -2;
  }
  if (ldv_read_file_to_reu(path, &len) != 0 ||
      ldv_validate_header(len, &payload_len) != 0) {
    return -3;
  }

  pos = 0u;
  if (rs_cmd_store_rsv1_value_to_heap(RS_CMD_SCRATCH_OFF + 6ul,
                                      &pos,
                                      payload_len,
                                      &root_off) != 0 ||
      pos != payload_len) {
    return -3;
  }
  rs_cmd_value_init_false(&root);
  if (rs_cmd_heap_value_load(root_off, &root) != 0) {
    return -3;
  }
  frame->flags = 0u;
  frame->used = root_off;
  if (root.tag == RS_VAL_ARRAY_PTR) {
    frame->flags = RS_CMD_FRAME_F_ARRAY;
    frame->count = root.as.ptr.len;
  } else {
    frame->count = 1u;
  }
  frame->index = 0u;
  return 0;
}

static int ldv_item(RSCommandFrame* frame) {
  unsigned short child_off;

  if (!frame || !frame->out || frame->index >= frame->count) {
    return -1;
  }
  if (frame->flags & RS_CMD_FRAME_F_ARRAY) {
    if (rs_cmd_heap_read_u16((unsigned short)(frame->used + 3u + (frame->index * 2u)),
                             &child_off) != 0) {
      return -3;
    }
    return rs_cmd_heap_value_load(child_off, frame->out);
  }
  return rs_cmd_heap_value_load(frame->used, frame->out);
}

static int ldv_run(RSCommandFrame* frame) {
  const char* path;
  unsigned short len;
  unsigned short payload_len;
  if (!frame || !frame->out || !frame->args || frame->arg_count < 1u) {
    return -1;
  }
  path = rs_cmd_value_cstr(&frame->args[0]);
  if (!path) {
    return -2;
  }
  if (ldv_read_file_to_reu(path, &len) != 0) {
    rs_cmd_value_free(frame->out);
    rs_cmd_value_init_bool(frame->out, 0);
    return 0;
  }
  if (ldv_validate_header(len, &payload_len) != 0) {
    return -3;
  }
  if (rs_cmd_load_rsv1_value_to_heap(RS_CMD_SCRATCH_OFF + 6ul,
                                     payload_len,
                                     frame->out) != 0) {
    return -3;
  }
  frame->used = len;
  return 0;
}

int rs_vmovl_cmd_ldv(RSCommandFrame* frame) {
  if (!frame) {
    return -1;
  }
  if (frame->op == RS_CMD_OVL_OP_BEGIN) {
    return ldv_begin(frame);
  }
  if (frame->op == RS_CMD_OVL_OP_ITEM) {
    return ldv_item(frame);
  }
  if (frame->op == RS_CMD_OVL_OP_RUN) {
    return ldv_run(frame);
  }
  return -1;
}

#if defined(__CC65__)
#pragma bss-name(pop)
#pragma rodata-name(pop)
#pragma code-name(pop)
#endif
