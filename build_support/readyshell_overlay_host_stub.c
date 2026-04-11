#include "src/apps/readyshellpoc/core/rs_cmd_overlay.h"

#include <string.h>

static int smoke_build_dir_entry(RSValue* out,
                                 const char* name,
                                 unsigned short blocks,
                                 const char* type) {
  RSValue tmp;
  if (!out) {
    return -1;
  }
  if (rs_value_object_new(out) != 0) {
    return -1;
  }
  rs_value_init_false(&tmp);
  if (rs_value_init_string(&tmp, name) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_object_set(out, "name", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out);
    return -1;
  }
  rs_value_free(&tmp);
  rs_value_init_u16(&tmp, blocks);
  if (rs_value_object_set(out, "blocks", &tmp) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_init_string(&tmp, type) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_object_set(out, "type", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out);
    return -1;
  }
  rs_value_free(&tmp);
  return 0;
}

int rs_overlay_command_call(RSCommandId id, unsigned char op, RSCommandFrame* frame) {
  static const char* names[] = { "alpha", "beta", "gamma" };
  static const unsigned short blocks[] = { 10u, 20u, 30u };
  static const char* types[] = { "PRG", "SEQ", "USR" };
  unsigned short drive16;
  unsigned char drive;
  if (!frame) {
    return -1;
  }
  if (id == RS_CMD_LST) {
    if (op == RS_CMD_OVL_OP_BEGIN) {
      frame->count = 3u;
      return 0;
    }
    if (op == RS_CMD_OVL_OP_ITEM) {
      if (!frame->out || frame->index >= 3u) {
        return -1;
      }
      rs_value_free(frame->out);
      return smoke_build_dir_entry(frame->out,
                                   names[frame->index],
                                   blocks[frame->index],
                                   types[frame->index]);
    }
    return -1;
  }
  if (id == RS_CMD_DRVI && op == RS_CMD_OVL_OP_RUN) {
    RSValue tmp;
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
    if (rs_value_init_string(&tmp, "disk") != 0) {
      rs_value_free(frame->out);
      return -1;
    }
    if (rs_value_object_set(frame->out, "diskname", &tmp) != 0) {
      rs_value_free(&tmp);
      rs_value_free(frame->out);
      return -1;
    }
    rs_value_free(&tmp);
    if (rs_value_init_string(&tmp, "") != 0) {
      rs_value_free(frame->out);
      return -1;
    }
    if (rs_value_object_set(frame->out, "id", &tmp) != 0) {
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
    if (rs_value_init_string(&tmp, "1541") != 0) {
      rs_value_free(frame->out);
      return -1;
    }
    if (rs_value_object_set(frame->out, "type", &tmp) != 0) {
      rs_value_free(&tmp);
      rs_value_free(frame->out);
      return -1;
    }
    rs_value_free(&tmp);
    return 0;
  }
  return -1;
}
