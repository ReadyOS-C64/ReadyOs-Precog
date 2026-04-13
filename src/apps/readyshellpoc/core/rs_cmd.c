#include "rs_cmd.h"
#include "rs_cmd_registry.h"

/* Overlay2-resident scratch buffers used by C64 VM command execution paths. */
char rs_vm_fmt_buf[128];
char rs_vm_line_buf[384];

typedef struct RSCommandInfo {
  const char* name;
  RSCommandId id;
  unsigned char caps;
} RSCommandInfo;

static const RSCommandInfo g_cmd_info[] = {
  { "PRT",  RS_CMD_PRT,  0u },
  { "MORE", RS_CMD_MORE, 0u },
  { "TOP",  RS_CMD_TOP,  0u },
  { "SEL",  RS_CMD_SEL,  0u },
  { "LDV",  RS_CMD_LDV,  (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_ITEM | RS_CMD_REG_CAP_RUN) },
  { "STV",  RS_CMD_STV,  (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_PROCESS | RS_CMD_REG_CAP_END | RS_CMD_REG_CAP_RUN) },
  { "LST",  RS_CMD_LST,  (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_ITEM) },
  { "DRVI", RS_CMD_DRVI, RS_CMD_REG_CAP_RUN },
  { "PUT",  RS_CMD_PUT,  RS_CMD_REG_CAP_RUN },
  { "ADD",  RS_CMD_ADD,  RS_CMD_REG_CAP_RUN },
  { "COPY", RS_CMD_COPY, RS_CMD_REG_CAP_RUN },
  { "CAT",  RS_CMD_CAT,  (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_ITEM) },
  { "DEL",  RS_CMD_DEL,  RS_CMD_REG_CAP_RUN },
  { "REN",  RS_CMD_REN,  RS_CMD_REG_CAP_RUN },
  { "GEN",  RS_CMD_GEN,  0u },
  { "TAP",  RS_CMD_TAP,  0u }
};

static const RSCommandInfo* rs_cmd_info_find(RSCommandId id) {
  unsigned char i;
  for (i = 0u; i < (unsigned char)(sizeof(g_cmd_info) / sizeof(g_cmd_info[0])); ++i) {
    if (g_cmd_info[i].id == id) {
      return &g_cmd_info[i];
    }
  }
  return 0;
}

RSCommandId rs_cmd_id(const char* name) {
  unsigned char i;
  for (i = 0u; i < (unsigned char)(sizeof(g_cmd_info) / sizeof(g_cmd_info[0])); ++i) {
    if (rs_ci_equal(name, g_cmd_info[i].name)) {
      return g_cmd_info[i].id;
    }
  }
  return RS_CMD_UNKNOWN;
}

int rs_cmd_is_external(RSCommandId id) {
  const RSCommandInfo* info;
  info = rs_cmd_info_find(id);
  return info && info->caps != 0u;
}

unsigned char rs_cmd_external_caps(RSCommandId id) {
  const RSCommandInfo* info;
  info = rs_cmd_info_find(id);
  return info ? info->caps : 0u;
}
