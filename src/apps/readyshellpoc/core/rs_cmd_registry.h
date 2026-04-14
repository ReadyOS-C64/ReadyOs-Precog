#ifndef RS_CMD_REGISTRY_H
#define RS_CMD_REGISTRY_H

#include "rs_cmd.h"

#define RS_CMD_REG_MAGIC0 'C'
#define RS_CMD_REG_MAGIC1 'R'
#define RS_CMD_REG_MAGIC2 'G'
#define RS_CMD_REG_MAGIC3 '1'
#define RS_CMD_REG_VERSION 4u

#define RS_CMD_REG_CAP_BEGIN   0x01u
#define RS_CMD_REG_CAP_ITEM    0x02u
#define RS_CMD_REG_CAP_RUN     0x04u
#define RS_CMD_REG_CAP_PROCESS 0x08u
#define RS_CMD_REG_CAP_END     0x10u

#define RS_CMD_OVL_LOAD_F_DISK      0x01u
#define RS_CMD_OVL_LOAD_F_REU_CACHE 0x02u

#define RS_CMD_OVL_STATE_SESSION_LOADED 0x01u
#define RS_CMD_OVL_STATE_CACHE_VALID    0x02u

#define RS_CMD_HANDLER_OVL3_DRVI 1u
#define RS_CMD_HANDLER_OVL3_LST  2u
#define RS_CMD_HANDLER_OVL4_LDV  1u
#define RS_CMD_HANDLER_OVL5_STV  1u
#define RS_CMD_HANDLER_OVL6_DEL  1u
#define RS_CMD_HANDLER_OVL6_REN  2u
#define RS_CMD_HANDLER_OVL6_PUT  3u
#define RS_CMD_HANDLER_OVL6_ADD  4u
#define RS_CMD_HANDLER_OVL7_CAT  1u
#define RS_CMD_HANDLER_OVL8_COPY 1u

typedef struct RSExternalCmdDescriptor {
  RSCommandId id;
  unsigned char overlay_index;
  unsigned char op_caps;
  unsigned char handler;
  unsigned char flags;
} RSExternalCmdDescriptor;

typedef struct RSExternalOverlayState {
  unsigned char overlay_phase;
  unsigned char load_flags;
  unsigned char load_state;
  unsigned char cache_bank;
  unsigned short cache_off;
  unsigned short slot_len;
  char disk_name[10];
} RSExternalOverlayState;

int rs_cmd_registry_seed(void);
int rs_cmd_registry_lookup_external(RSCommandId id, RSExternalCmdDescriptor* out);
int rs_cmd_registry_read_overlay_state(unsigned char index, RSExternalOverlayState* out);
int rs_cmd_registry_update_overlay_state(unsigned char index,
                                         unsigned char set_mask,
                                         unsigned char clear_mask);
int rs_cmd_is_external(RSCommandId id);

#endif
