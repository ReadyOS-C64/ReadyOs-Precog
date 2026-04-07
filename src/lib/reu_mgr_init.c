/*
 * reu_mgr_init.c - REU manager initialization and shim bitmap sync
 */

#include "reu_mgr.h"

static void reu_sync_from_bitmap(void) {
    unsigned char bitmap_lo = *SHIM_REU_BITMAP_LO;
    unsigned char bitmap_hi = *SHIM_REU_BITMAP_HI;
    unsigned char bitmap_xhi = *SHIM_REU_BITMAP_XHI;
    unsigned char bank;
    unsigned char mask;

    for (bank = 0; bank < 8; ++bank) {
        mask = (unsigned char)(1 << bank);
        REU_ALLOC_TABLE[bank] = (bitmap_lo & mask) ? REU_APP_STATE : REU_RESERVED;
    }

    for (bank = 0; bank < 8; ++bank) {
        mask = (unsigned char)(1 << bank);
        REU_ALLOC_TABLE[bank + 8] = (bitmap_hi & mask) ? REU_APP_STATE : REU_RESERVED;
    }

    for (bank = 0; bank < 8; ++bank) {
        mask = (unsigned char)(1 << bank);
        REU_ALLOC_TABLE[bank + 16] = (bitmap_xhi & mask) ? REU_APP_STATE : REU_RESERVED;
    }
}

static void reu_apply_fixed_system_banks(void) {
    REU_ALLOC_TABLE[REU_BANK_RS_OVL1] = REU_RS_OVL1;
    REU_ALLOC_TABLE[REU_BANK_RS_OVL2] = REU_RS_OVL2;
    REU_ALLOC_TABLE[REU_BANK_RS_OVL3] = REU_RS_OVL3;
    REU_ALLOC_TABLE[REU_BANK_RS_DEBUG] = REU_RS_DEBUG;
}

void reu_mgr_init(void) {
    if (*REU_SYS_MAGIC == REU_MAGIC_VALUE) {
        reu_sync_from_bitmap();
        reu_apply_fixed_system_banks();
        return;
    }

    {
        unsigned int i;
        for (i = 0; i < REU_TOTAL_BANKS; ++i) {
            REU_ALLOC_TABLE[i] = REU_FREE;
        }
    }
    *REU_SYS_MAGIC = REU_MAGIC_VALUE;
    reu_sync_from_bitmap();
    reu_apply_fixed_system_banks();
}
