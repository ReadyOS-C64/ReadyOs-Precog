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
#define RS_REU_CMD_REG_STATE_CAP       6u

#define RS_REU_SHARED_META_OFF         0x4880F0ul
#define RS_REU_OVL_CACHE_META_OFF      RS_REU_SHARED_META_OFF
#define RS_REU_OVL_CACHE_META_LEN      12u
#define RS_REU_OVL_CACHE_META_VERSION  2u
#define RS_REU_OVL_CACHE_BANK          0x40u
#define RS_REU_OVL_CACHE_BANK2         0x41u
#define RS_REU_OVL_CACHE_PARSE_REL     0x0000u
#define RS_REU_OVL_CACHE_EXEC_REL      0x3800u
#define RS_REU_OVL_CACHE_CMD3_REL      0x7000u
#define RS_REU_OVL_CACHE_CMD5_REL      0xA800u
#define RS_REU_OVL_CACHE_CMD4_REL      0x0000u
#define RS_REU_OVL_CACHE_CMD6_REL      0x3800u
#define RS_REU_OVL_CACHE_CMD7_REL      0x7000u
#define RS_REU_OVL_CACHE_CMD8_REL      0xA800u
#define RS_REU_OVL_CACHE_SLOT_LEN      0x3800u
#define RS_REU_OVL_CACHE_VALID_PARSE   0x01u
#define RS_REU_OVL_CACHE_VALID_EXEC    0x02u
#define RS_REU_OVL_CACHE_VALID_CMD3    0x04u
#define RS_REU_OVL_CACHE_VALID_CMD4    0x08u
#define RS_REU_OVL_CACHE_VALID_CMD5    0x10u
#define RS_REU_OVL_CACHE_VALID_CMD6    0x20u
#define RS_REU_OVL_CACHE_VALID_CMD7    0x40u
#define RS_REU_OVL_CACHE_VALID_CMD8    0x80u

#define RS_REU_UI_FLAGS_OFF 0x4880FCul
#define RS_UI_FLAG_PAUSED 0x01u

#endif
