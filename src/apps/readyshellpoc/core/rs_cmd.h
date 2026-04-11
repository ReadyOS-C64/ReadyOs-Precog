#ifndef RS_CMD_H
#define RS_CMD_H

#include "rs_token.h"

typedef enum {
  RS_CMD_UNKNOWN = 0,
  RS_CMD_PRT,
  RS_CMD_TOP,
  RS_CMD_SEL,
  RS_CMD_LDV,
  RS_CMD_STV,
  RS_CMD_LST,
  RS_CMD_DRVI,
  RS_CMD_GEN,
  RS_CMD_TAP
} RSCommandId;

RSCommandId rs_cmd_id(const char* name);
int rs_cmd_is_known(const char* name);

#endif
