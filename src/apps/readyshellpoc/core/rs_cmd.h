#ifndef RS_CMD_H
#define RS_CMD_H

#include "rs_token.h"

typedef enum {
  RS_CMD_UNKNOWN = 0,
  RS_CMD_PRT,
  RS_CMD_MORE,
  RS_CMD_TOP,
  RS_CMD_SEL,
  RS_CMD_LDV,
  RS_CMD_STV,
  RS_CMD_LST,
  RS_CMD_DRVI,
  RS_CMD_PUT,
  RS_CMD_ADD,
  RS_CMD_COPY,
  RS_CMD_CAT,
  RS_CMD_DEL,
  RS_CMD_REN,
  RS_CMD_GEN,
  RS_CMD_TAP
} RSCommandId;

RSCommandId rs_cmd_id(const char* name);
int rs_cmd_is_external(RSCommandId id);
unsigned char rs_cmd_external_caps(RSCommandId id);

#endif
