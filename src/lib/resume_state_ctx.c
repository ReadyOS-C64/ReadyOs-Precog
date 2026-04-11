/*
 * resume_state_ctx.c - Shared resume state context for split modules
 */

#include "resume_state_priv.h"

unsigned char rs_resume_bank = 0;
unsigned char rs_resume_app_id = 0;
unsigned char rs_resume_schema = 0;
unsigned int rs_resume_last_seq = 0;

unsigned char rs_resume_hdr[RESUME_HDR_SIZE];
unsigned char rs_resume_zero_hdr[RESUME_HDR_SIZE];

void resume_init_for_app(unsigned char bank, unsigned char app_id,
                         unsigned char schema_version) {
    rs_resume_bank = bank;
    rs_resume_app_id = app_id;
    rs_resume_schema = schema_version;
    rs_resume_last_seq = 0;
}
