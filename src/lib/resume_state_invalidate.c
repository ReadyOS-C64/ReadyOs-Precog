/*
 * resume_state_invalidate.c - Resume payload invalidation helper
 */

#include "resume_state_priv.h"
#include "reu_mgr.h"

void resume_invalidate(void) {
    unsigned char i;

    if (rs_resume_bank == 0) {
        return;
    }

    for (i = 0; i < RESUME_HDR_SIZE; ++i) {
        rs_resume_zero_hdr[i] = 0;
    }
    reu_dma_stash((unsigned int)rs_resume_zero_hdr, rs_resume_bank, REU_RESUME_OFF, RESUME_HDR_SIZE);
    rs_resume_last_seq = 0;
}
