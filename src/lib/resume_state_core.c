/*
 * resume_state_core.c - Simple save/load helpers for Ready OS app resume
 */

#include "resume_state_priv.h"
#include "reu_mgr.h"

static unsigned int get_u16(const unsigned char *buf, unsigned char off) {
    return (unsigned int)buf[off] | ((unsigned int)buf[(unsigned char)(off + 1)] << 8);
}

static void put_u16(unsigned char *buf, unsigned char off, unsigned int value) {
    buf[off] = (unsigned char)(value & 0xFF);
    buf[(unsigned char)(off + 1)] = (unsigned char)(value >> 8);
}

static unsigned char header_is_valid(unsigned int max_len, unsigned int *out_len,
                                     unsigned int *out_seq) {
    unsigned int len;
    unsigned int seq;

    if (rs_resume_hdr[HDR_OFF_MAGIC0] != RESUME_MAGIC_0 ||
        rs_resume_hdr[HDR_OFF_MAGIC1] != RESUME_MAGIC_1 ||
        rs_resume_hdr[HDR_OFF_MAGIC2] != RESUME_MAGIC_2 ||
        rs_resume_hdr[HDR_OFF_MAGIC3] != RESUME_MAGIC_3) {
        return 0;
    }
    if (rs_resume_hdr[HDR_OFF_APP_ID] != rs_resume_app_id ||
        rs_resume_hdr[HDR_OFF_SCHEMA] != rs_resume_schema) {
        return 0;
    }

    len = get_u16(rs_resume_hdr, HDR_OFF_LEN_LO);
    seq = get_u16(rs_resume_hdr, HDR_OFF_SEQ_LO);

    if (len == 0) {
        return 0;
    }
    if (len > max_len) {
        return 0;
    }
    if (len > (REU_RESUME_TAIL_SIZE - RESUME_HDR_SIZE)) {
        return 0;
    }

    *out_len = len;
    *out_seq = seq;
    return 1;
}

static void build_header(unsigned int payload_len) {
    rs_resume_last_seq = (unsigned int)(rs_resume_last_seq + 1);

    rs_resume_hdr[HDR_OFF_MAGIC0] = RESUME_MAGIC_0;
    rs_resume_hdr[HDR_OFF_MAGIC1] = RESUME_MAGIC_1;
    rs_resume_hdr[HDR_OFF_MAGIC2] = RESUME_MAGIC_2;
    rs_resume_hdr[HDR_OFF_MAGIC3] = RESUME_MAGIC_3;
    rs_resume_hdr[HDR_OFF_APP_ID] = rs_resume_app_id;
    rs_resume_hdr[HDR_OFF_SCHEMA] = rs_resume_schema;
    rs_resume_hdr[HDR_OFF_FLAGS] = 0;
    rs_resume_hdr[HDR_OFF_RSVD0] = 0;
    put_u16(rs_resume_hdr, HDR_OFF_SEQ_LO, rs_resume_last_seq);
    put_u16(rs_resume_hdr, HDR_OFF_LEN_LO, payload_len);
    put_u16(rs_resume_hdr, HDR_OFF_CRC_LO, 0);
    rs_resume_hdr[HDR_OFF_RSVD1] = 0;
    rs_resume_hdr[HDR_OFF_RSVD2] = 0;
}

unsigned char resume_try_load(void *dst, unsigned int dst_len,
                              unsigned int *out_len) {
    unsigned int stored_len;
    unsigned int stored_seq;

    if (out_len != 0) {
        *out_len = 0;
    }
    if (rs_resume_schema == 0 || dst == 0 || dst_len == 0) {
        return 0;
    }
    if (dst_len > (REU_RESUME_TAIL_SIZE - RESUME_HDR_SIZE)) {
        return 0;
    }

    reu_dma_fetch((unsigned int)rs_resume_hdr, rs_resume_bank, REU_RESUME_OFF, RESUME_HDR_SIZE);
    if (!header_is_valid(dst_len, &stored_len, &stored_seq)) {
        return 0;
    }

    reu_dma_fetch((unsigned int)dst,
                  rs_resume_bank,
                  (unsigned int)(REU_RESUME_OFF + RESUME_HDR_SIZE),
                  stored_len);
    rs_resume_last_seq = stored_seq;
    if (out_len != 0) {
        *out_len = stored_len;
    }
    return 1;
}

unsigned char resume_save(const void *src, unsigned int src_len) {
    if (rs_resume_schema == 0 || src == 0 || src_len == 0) {
        return 0;
    }
    if (src_len > (REU_RESUME_TAIL_SIZE - RESUME_HDR_SIZE)) {
        return 0;
    }

    build_header(src_len);
    reu_dma_stash((unsigned int)src,
                  rs_resume_bank,
                  (unsigned int)(REU_RESUME_OFF + RESUME_HDR_SIZE),
                  src_len);
    reu_dma_stash((unsigned int)rs_resume_hdr, rs_resume_bank, REU_RESUME_OFF, RESUME_HDR_SIZE);
    return 1;
}
