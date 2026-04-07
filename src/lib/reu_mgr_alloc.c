/*
 * reu_mgr_alloc.c - REU allocation/free helpers
 */

#include "reu_mgr.h"

static unsigned char reu_fixed_bank_type(unsigned char bank) {
    switch (bank) {
        case REU_BANK_RS_OVL1:  return REU_RS_OVL1;
        case REU_BANK_RS_OVL2:  return REU_RS_OVL2;
        case REU_BANK_RS_OVL3:  return REU_RS_OVL3;
        case REU_BANK_RS_DEBUG: return REU_RS_DEBUG;
        default:                return 0xFF;
    }
}

unsigned char reu_alloc_bank(unsigned char type) {
    unsigned int bank;

    for (bank = REU_FIRST_DYNAMIC; bank < REU_TOTAL_BANKS; ++bank) {
        if (reu_fixed_bank_type((unsigned char)bank) != 0xFF) {
            continue;
        }
        if (REU_ALLOC_TABLE[bank] == REU_FREE) {
            REU_ALLOC_TABLE[bank] = type;
            return (unsigned char)bank;
        }
    }

    return 0xFF;
}

void reu_free_bank(unsigned char bank) {
    unsigned char mask;
    unsigned char fixed_type = reu_fixed_bank_type(bank);

    if (fixed_type != 0xFF) {
        REU_ALLOC_TABLE[bank] = fixed_type;
        return;
    }

    if (bank < REU_FIRST_DYNAMIC) {
        REU_ALLOC_TABLE[bank] = REU_RESERVED;
        if (bank < 8) {
            mask = (unsigned char)(1 << bank);
            *SHIM_REU_BITMAP_LO &= (unsigned char)~mask;
        } else if (bank < 16) {
            mask = (unsigned char)(1 << (bank - 8));
            *SHIM_REU_BITMAP_HI &= (unsigned char)~mask;
        } else {
            mask = (unsigned char)(1 << (bank - 16));
            *SHIM_REU_BITMAP_XHI &= (unsigned char)~mask;
        }
        return;
    }

    REU_ALLOC_TABLE[bank] = REU_FREE;
}
