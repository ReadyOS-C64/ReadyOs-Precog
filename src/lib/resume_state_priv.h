/*
 * resume_state_priv.h - Internal shared state for split resume_state modules
 */

#ifndef RESUME_STATE_PRIV_H
#define RESUME_STATE_PRIV_H

#include "resume_state.h"

#define RESUME_MAGIC_0 'R'
#define RESUME_MAGIC_1 'S'
#define RESUME_MAGIC_2 'M'
#define RESUME_MAGIC_3 '1'

#define RESUME_HDR_SIZE 16

#define HDR_OFF_MAGIC0      0
#define HDR_OFF_MAGIC1      1
#define HDR_OFF_MAGIC2      2
#define HDR_OFF_MAGIC3      3
#define HDR_OFF_APP_ID      4
#define HDR_OFF_SCHEMA      5
#define HDR_OFF_FLAGS       6
#define HDR_OFF_RSVD0       7
#define HDR_OFF_SEQ_LO      8
#define HDR_OFF_SEQ_HI      9
#define HDR_OFF_LEN_LO      10
#define HDR_OFF_LEN_HI      11
#define HDR_OFF_CRC_LO      12
#define HDR_OFF_CRC_HI      13
#define HDR_OFF_RSVD1       14
#define HDR_OFF_RSVD2       15

extern unsigned char rs_resume_bank;
extern unsigned char rs_resume_app_id;
extern unsigned char rs_resume_schema;
extern unsigned int rs_resume_last_seq;

extern unsigned char rs_resume_hdr[RESUME_HDR_SIZE];
extern unsigned char rs_resume_zero_hdr[RESUME_HDR_SIZE];

#endif /* RESUME_STATE_PRIV_H */
