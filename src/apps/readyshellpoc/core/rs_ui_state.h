#ifndef RS_UI_STATE_H
#define RS_UI_STATE_H

/*
 * Shared ReadyShell state lives in REU bank 0x48 metadata space instead of
 * overlay or resident heap RAM. This keeps small cross-layer state visible
 * across the resident/output boundary without colliding with command scratch
 * payloads or the REU value arena.
 */
#define RS_REU_CMD_REG_HDR_OFF         0x488010ul
#define RS_REU_CMD_REG_HDR_LEN         8u
#define RS_REU_CMD_REG_DESC_OFF        0x488020ul
#define RS_REU_CMD_REG_DESC_LEN        6u
#define RS_REU_CMD_REG_DESC_CAP        16u
#define RS_REU_CMD_REG_STATE_OFF       0x488080ul
#define RS_REU_CMD_REG_STATE_LEN       18u
#define RS_REU_CMD_REG_STATE_CAP       4u

#define RS_REU_SHARED_META_OFF         0x4880E0ul
#define RS_REU_OVL_CACHE_META_OFF      RS_REU_SHARED_META_OFF
#define RS_REU_OVL_CACHE_META_LEN      12u
#define RS_REU_OVL_CACHE_BANK          0x40u
#define RS_REU_OVL_CACHE_PARSE_REL     0x0000u
#define RS_REU_OVL_CACHE_EXEC_REL      0x3800u
#define RS_REU_OVL_CACHE_SLOT_LEN      0x3800u
#define RS_REU_OVL_CACHE_VALID_PARSE   0x01u
#define RS_REU_OVL_CACHE_VALID_EXEC    0x02u

#define RS_REU_UI_FLAGS_OFF 0x4880F0ul
#define RS_UI_FLAG_PAUSED 0x01u

#endif
