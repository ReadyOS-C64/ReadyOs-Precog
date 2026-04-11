#include "rs_cmd.h"

/* Overlay2-resident scratch buffers used by C64 VM command execution paths. */
char rs_vm_fmt_buf[128];
char rs_vm_line_buf[384];

RSCommandId rs_cmd_id(const char* name) {
  if (rs_ci_equal(name, "PRT")) {
    return RS_CMD_PRT;
  }
  if (rs_ci_equal(name, "TOP")) {
    return RS_CMD_TOP;
  }
  if (rs_ci_equal(name, "SEL")) {
    return RS_CMD_SEL;
  }
  if (rs_ci_equal(name, "LDV")) {
    return RS_CMD_LDV;
  }
  if (rs_ci_equal(name, "STV")) {
    return RS_CMD_STV;
  }
  if (rs_ci_equal(name, "LST")) {
    return RS_CMD_LST;
  }
  if (rs_ci_equal(name, "DRVI")) {
    return RS_CMD_DRVI;
  }
  if (rs_ci_equal(name, "GEN")) {
    return RS_CMD_GEN;
  }
  if (rs_ci_equal(name, "TAP")) {
    return RS_CMD_TAP;
  }
  return RS_CMD_UNKNOWN;
}
