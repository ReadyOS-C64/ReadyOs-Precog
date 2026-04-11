#ifndef RS_CMD_OVERLAY_H
#define RS_CMD_OVERLAY_H

#include "rs_cmd.h"
#include "rs_errors.h"
#include "rs_value.h"

#define RS_CMD_OVL_OP_BEGIN   1u
#define RS_CMD_OVL_OP_ITEM    2u
#define RS_CMD_OVL_OP_RUN     3u
#define RS_CMD_OVL_OP_PROCESS 4u
#define RS_CMD_OVL_OP_END     5u

#define RS_CMD_FRAME_F_ARRAY  0x01u

/* REU-backed inter-overlay handoff area for streaming command state. */
#define RS_CMD_SCRATCH_OFF 0x480000ul
#define RS_CMD_SCRATCH_LEN 0x8000u

typedef struct RSCommandFrame {
  RSCommandId id;
  unsigned char op;
  RSValue* args;
  unsigned short arg_count;
  const RSValue* item;
  RSValue* out;
  unsigned short index;
  unsigned short count;
  unsigned short used;
  unsigned char drive;
  unsigned char flags;
  RSError* err;
} RSCommandFrame;

int rs_overlay_command_call(RSCommandId id, unsigned char op, RSCommandFrame* frame);

#endif
