#include "rs_cmd_registry.h"

#include "rs_ui_state.h"
#include "../platform/rs_overlay.h"
#include "../platform/rs_platform.h"

#define RS_CMD_REG_DESC_COUNT  10u
#define RS_CMD_REG_STATE_COUNT 6u

#define RS_CMD_REG_DESC_ID_OFF        0u
#define RS_CMD_REG_DESC_OVL_INDEX_OFF 1u
#define RS_CMD_REG_DESC_CAPS_OFF      2u
#define RS_CMD_REG_DESC_HANDLER_OFF   3u

#define RS_CMD_REG_STATE_PHASE_OFF      0u
#define RS_CMD_REG_STATE_LOAD_FLAGS_OFF 1u
#define RS_CMD_REG_STATE_LOAD_STATE_OFF 2u
#define RS_CMD_REG_STATE_CACHE_BANK_OFF 3u
#define RS_CMD_REG_STATE_CACHE_OFF_LO   4u
#define RS_CMD_REG_STATE_CACHE_OFF_HI   5u
#define RS_CMD_REG_STATE_DISK_NAME_OFF  6u

static const unsigned char g_cmd_seed[RS_CMD_REG_DESC_COUNT][RS_REU_CMD_REG_DESC_LEN] = {
  { (unsigned char)RS_CMD_DRVI, 0u, RS_CMD_REG_CAP_RUN, RS_CMD_HANDLER_OVL3_DRVI, 0u, 0u },
  { (unsigned char)RS_CMD_LST,  0u, (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_ITEM), RS_CMD_HANDLER_OVL3_LST, 0u, 0u },
  { (unsigned char)RS_CMD_LDV,  1u, (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_ITEM | RS_CMD_REG_CAP_RUN), RS_CMD_HANDLER_OVL4_LDV, 0u, 0u },
  { (unsigned char)RS_CMD_STV,  2u, (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_PROCESS | RS_CMD_REG_CAP_END | RS_CMD_REG_CAP_RUN), RS_CMD_HANDLER_OVL5_STV, 0u, 0u },
  { (unsigned char)RS_CMD_DEL,  3u, RS_CMD_REG_CAP_RUN, RS_CMD_HANDLER_OVL6_DEL, 0u, 0u },
  { (unsigned char)RS_CMD_REN,  3u, RS_CMD_REG_CAP_RUN, RS_CMD_HANDLER_OVL6_REN, 0u, 0u },
  { (unsigned char)RS_CMD_CAT,  4u, (unsigned char)(RS_CMD_REG_CAP_BEGIN | RS_CMD_REG_CAP_ITEM), RS_CMD_HANDLER_OVL7_CAT, 0u, 0u },
  { (unsigned char)RS_CMD_PUT,  3u, RS_CMD_REG_CAP_RUN, RS_CMD_HANDLER_OVL6_PUT, 0u, 0u },
  { (unsigned char)RS_CMD_ADD,  3u, RS_CMD_REG_CAP_RUN, RS_CMD_HANDLER_OVL6_ADD, 0u, 0u },
  { (unsigned char)RS_CMD_COPY, 5u, RS_CMD_REG_CAP_RUN, RS_CMD_HANDLER_OVL8_COPY, 0u, 0u }
};

static const unsigned char g_state_seed[RS_CMD_REG_STATE_COUNT][RS_REU_CMD_REG_STATE_LEN] = {
  { RS_OVERLAY_PHASE_CMD3, (unsigned char)(RS_CMD_OVL_LOAD_F_DISK | RS_CMD_OVL_LOAD_F_REU_CACHE), 0u, RS_REU_OVL_CACHE_BANK,  (unsigned char)(RS_REU_OVL_CACHE_CMD3_REL & 0xFFu), (unsigned char)((RS_REU_OVL_CACHE_CMD3_REL >> 8u) & 0xFFu), 'r', 's', 'd', 'r', 'v', 'i', 'l', 's', 't', 0u, 0u, 0u },
  { RS_OVERLAY_PHASE_CMD4, (unsigned char)(RS_CMD_OVL_LOAD_F_DISK | RS_CMD_OVL_LOAD_F_REU_CACHE), 0u, RS_REU_OVL_CACHE_BANK2, (unsigned char)(RS_REU_OVL_CACHE_CMD4_REL & 0xFFu), (unsigned char)((RS_REU_OVL_CACHE_CMD4_REL >> 8u) & 0xFFu), 'r', 's', 'l', 'd', 'v', 0u, 0u, 0u, 0u, 0u, 0u, 0u },
  { RS_OVERLAY_PHASE_CMD5, (unsigned char)(RS_CMD_OVL_LOAD_F_DISK | RS_CMD_OVL_LOAD_F_REU_CACHE), 0u, RS_REU_OVL_CACHE_BANK,  (unsigned char)(RS_REU_OVL_CACHE_CMD5_REL & 0xFFu), (unsigned char)((RS_REU_OVL_CACHE_CMD5_REL >> 8u) & 0xFFu), 'r', 's', 's', 't', 'v', 0u, 0u, 0u, 0u, 0u, 0u, 0u },
  { RS_OVERLAY_PHASE_CMD6, (unsigned char)(RS_CMD_OVL_LOAD_F_DISK | RS_CMD_OVL_LOAD_F_REU_CACHE), 0u, RS_REU_OVL_CACHE_BANK2, (unsigned char)(RS_REU_OVL_CACHE_CMD6_REL & 0xFFu), (unsigned char)((RS_REU_OVL_CACHE_CMD6_REL >> 8u) & 0xFFu), 'r', 's', 'f', 'o', 'p', 's', 0u, 0u, 0u, 0u, 0u, 0u },
  { RS_OVERLAY_PHASE_CMD7, (unsigned char)(RS_CMD_OVL_LOAD_F_DISK | RS_CMD_OVL_LOAD_F_REU_CACHE), 0u, RS_REU_OVL_CACHE_BANK2, (unsigned char)(RS_REU_OVL_CACHE_CMD7_REL & 0xFFu), (unsigned char)((RS_REU_OVL_CACHE_CMD7_REL >> 8u) & 0xFFu), 'r', 's', 'c', 'a', 't', 0u, 0u, 0u, 0u, 0u, 0u, 0u },
  { RS_OVERLAY_PHASE_CMD8, (unsigned char)(RS_CMD_OVL_LOAD_F_DISK | RS_CMD_OVL_LOAD_F_REU_CACHE), 0u, RS_REU_OVL_CACHE_BANK2, (unsigned char)(RS_REU_OVL_CACHE_CMD8_REL & 0xFFu), (unsigned char)((RS_REU_OVL_CACHE_CMD8_REL >> 8u) & 0xFFu), 'r', 's', 'c', 'o', 'p', 'y', 0u, 0u, 0u, 0u, 0u, 0u }
};

static unsigned long rs_cmd_reg_desc_abs(unsigned char index) {
  return RS_REU_CMD_REG_DESC_OFF +
         ((unsigned long)index * (unsigned long)RS_REU_CMD_REG_DESC_LEN);
}

static unsigned long rs_cmd_reg_state_abs(unsigned char index) {
  return RS_REU_CMD_REG_STATE_OFF +
         ((unsigned long)index * (unsigned long)RS_REU_CMD_REG_STATE_LEN);
}

static int rs_cmd_registry_ready(void) {
  unsigned char hdr[RS_REU_CMD_REG_HDR_LEN];
  if (rs_reu_read(RS_REU_CMD_REG_HDR_OFF, hdr, sizeof(hdr)) != 0) {
    return 0;
  }
  return hdr[0] == (unsigned char)RS_CMD_REG_MAGIC0 &&
         hdr[1] == (unsigned char)RS_CMD_REG_MAGIC1 &&
         hdr[2] == (unsigned char)RS_CMD_REG_MAGIC2 &&
         hdr[3] == (unsigned char)RS_CMD_REG_MAGIC3 &&
         hdr[4] == RS_CMD_REG_VERSION &&
         hdr[5] == RS_CMD_REG_DESC_COUNT &&
         hdr[6] == RS_CMD_REG_STATE_COUNT;
}

static int rs_cmd_registry_ensure_seeded(void) {
  if (rs_cmd_registry_ready()) {
    return 0;
  }
  return rs_cmd_registry_seed();
}

int rs_cmd_registry_seed(void) {
  static const unsigned char hdr[RS_REU_CMD_REG_HDR_LEN] = {
    (unsigned char)RS_CMD_REG_MAGIC0,
    (unsigned char)RS_CMD_REG_MAGIC1,
    (unsigned char)RS_CMD_REG_MAGIC2,
    (unsigned char)RS_CMD_REG_MAGIC3,
    RS_CMD_REG_VERSION,
    RS_CMD_REG_DESC_COUNT,
    RS_CMD_REG_STATE_COUNT,
    0u
  };
  unsigned char i;

  if (!rs_reu_available() ||
      rs_reu_write(RS_REU_CMD_REG_HDR_OFF, hdr, sizeof(hdr)) != 0) {
    return -1;
  }

  for (i = 0u; i < RS_CMD_REG_DESC_COUNT; ++i) {
    if (rs_reu_write(rs_cmd_reg_desc_abs(i), g_cmd_seed[i], RS_REU_CMD_REG_DESC_LEN) != 0) {
      return -1;
    }
  }
  for (i = 0u; i < RS_CMD_REG_STATE_COUNT; ++i) {
    if (rs_reu_write(rs_cmd_reg_state_abs(i), g_state_seed[i], RS_REU_CMD_REG_STATE_LEN) != 0) {
      return -1;
    }
  }
  return 0;
}

int rs_cmd_registry_lookup_external(RSCommandId id, RSExternalCmdDescriptor* out) {
  unsigned char buf[RS_REU_CMD_REG_DESC_LEN];
  unsigned char i;

  if (!out || rs_cmd_registry_ensure_seeded() != 0) {
    return -1;
  }

  for (i = 0u; i < RS_CMD_REG_DESC_COUNT; ++i) {
    if (rs_reu_read(rs_cmd_reg_desc_abs(i), buf, sizeof(buf)) != 0) {
      return -1;
    }
    if ((RSCommandId)buf[RS_CMD_REG_DESC_ID_OFF] != id) {
      continue;
    }
    out->id = id;
    out->overlay_index = buf[RS_CMD_REG_DESC_OVL_INDEX_OFF];
    out->op_caps = buf[RS_CMD_REG_DESC_CAPS_OFF];
    out->handler = buf[RS_CMD_REG_DESC_HANDLER_OFF];
    out->flags = 0u;
    return 0;
  }
  return -1;
}

int rs_cmd_registry_read_overlay_state(unsigned char index, RSExternalOverlayState* out) {
  unsigned char buf[RS_REU_CMD_REG_STATE_LEN];
  unsigned char i;

  if (!out || index >= RS_CMD_REG_STATE_COUNT || rs_cmd_registry_ensure_seeded() != 0) {
    return -1;
  }
  if (rs_reu_read(rs_cmd_reg_state_abs(index), buf, sizeof(buf)) != 0) {
    return -1;
  }

  out->overlay_phase = buf[RS_CMD_REG_STATE_PHASE_OFF];
  out->load_flags = buf[RS_CMD_REG_STATE_LOAD_FLAGS_OFF];
  out->load_state = buf[RS_CMD_REG_STATE_LOAD_STATE_OFF];
  out->cache_bank = buf[RS_CMD_REG_STATE_CACHE_BANK_OFF];
  out->cache_off = (unsigned short)(buf[RS_CMD_REG_STATE_CACHE_OFF_LO] |
                                    ((unsigned short)buf[RS_CMD_REG_STATE_CACHE_OFF_HI] << 8u));
  out->slot_len = 0u;
  for (i = 0u; i < sizeof(out->disk_name); ++i) {
    out->disk_name[i] = (char)buf[RS_CMD_REG_STATE_DISK_NAME_OFF + i];
  }
  out->disk_name[sizeof(out->disk_name) - 1u] = '\0';
  return 0;
}

int rs_cmd_registry_update_overlay_state(unsigned char index,
                                         unsigned char set_mask,
                                         unsigned char clear_mask) {
  unsigned char state;
  unsigned long off;

  if (index >= RS_CMD_REG_STATE_COUNT || rs_cmd_registry_ensure_seeded() != 0) {
    return -1;
  }
  off = rs_cmd_reg_state_abs(index) + RS_CMD_REG_STATE_LOAD_STATE_OFF;
  if (rs_reu_read(off, &state, 1u) != 0) {
    return -1;
  }
  state = (unsigned char)((state & (unsigned char)(~clear_mask)) | set_mask);
  return rs_reu_write(off, &state, 1u);
}
