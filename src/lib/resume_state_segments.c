/*
 * resume_state_segments.c - Segment-based resume helpers
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

static unsigned char write_segments_len(const ResumeWriteSegment *segments,
                                        unsigned char segment_count,
                                        unsigned int *out_total) {
    unsigned char i;
    unsigned int total = 0;
    unsigned int prev;

    for (i = 0; i < segment_count; ++i) {
        if (segments[i].len == 0) {
            continue;
        }
        if (segments[i].ptr == 0) {
            return 0;
        }
        prev = total;
        total = (unsigned int)(total + segments[i].len);
        if (total < prev) {
            return 0;
        }
    }
    *out_total = total;
    return 1;
}

static unsigned char read_segments_len(const ResumeReadSegment *segments,
                                       unsigned char segment_count,
                                       unsigned int *out_total) {
    unsigned char i;
    unsigned int total = 0;
    unsigned int prev;

    for (i = 0; i < segment_count; ++i) {
        if (segments[i].len == 0) {
            continue;
        }
        if (segments[i].ptr == 0) {
            return 0;
        }
        prev = total;
        total = (unsigned int)(total + segments[i].len);
        if (total < prev) {
            return 0;
        }
    }
    *out_total = total;
    return 1;
}

unsigned char resume_save_segments(const ResumeWriteSegment *segments,
                                   unsigned char segment_count) {
    unsigned int payload_len;
    unsigned int payload_off;
    unsigned char i;

    if (rs_resume_schema == 0) {
        return 0;
    }
    if (segments == 0 || segment_count == 0) {
        return 0;
    }
    if (!write_segments_len(segments, segment_count, &payload_len)) {
        return 0;
    }
    if (payload_len == 0 || payload_len > (REU_RESUME_TAIL_SIZE - RESUME_HDR_SIZE)) {
        return 0;
    }

    build_header(payload_len);

    payload_off = (unsigned int)(REU_RESUME_OFF + RESUME_HDR_SIZE);
    for (i = 0; i < segment_count; ++i) {
        if (segments[i].len == 0) {
            continue;
        }
        reu_dma_stash((unsigned int)segments[i].ptr, rs_resume_bank, payload_off, segments[i].len);
        payload_off = (unsigned int)(payload_off + segments[i].len);
    }
    reu_dma_stash((unsigned int)rs_resume_hdr, rs_resume_bank, REU_RESUME_OFF, RESUME_HDR_SIZE);
    return 1;
}

unsigned char resume_load_segments(const ResumeReadSegment *segments,
                                   unsigned char segment_count,
                                   unsigned int *out_len) {
    unsigned int stored_len;
    unsigned int stored_seq;
    unsigned int expected_len;
    unsigned int payload_off;
    unsigned char i;

    if (out_len != 0) {
        *out_len = 0;
    }
    if (rs_resume_schema == 0) {
        return 0;
    }
    if (segments == 0 || segment_count == 0) {
        return 0;
    }
    if (!read_segments_len(segments, segment_count, &expected_len)) {
        return 0;
    }
    if (expected_len == 0 || expected_len > (REU_RESUME_TAIL_SIZE - RESUME_HDR_SIZE)) {
        return 0;
    }

    reu_dma_fetch((unsigned int)rs_resume_hdr, rs_resume_bank, REU_RESUME_OFF, RESUME_HDR_SIZE);
    if (!header_is_valid(expected_len, &stored_len, &stored_seq)) {
        return 0;
    }
    if (stored_len != expected_len) {
        return 0;
    }

    payload_off = (unsigned int)(REU_RESUME_OFF + RESUME_HDR_SIZE);
    for (i = 0; i < segment_count; ++i) {
        if (segments[i].len == 0) {
            continue;
        }
        reu_dma_fetch((unsigned int)segments[i].ptr, rs_resume_bank, payload_off, segments[i].len);
        payload_off = (unsigned int)(payload_off + segments[i].len);
    }

    rs_resume_last_seq = stored_seq;
    if (out_len != 0) {
        *out_len = stored_len;
    }
    return 1;
}
