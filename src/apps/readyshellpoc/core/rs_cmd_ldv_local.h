#ifndef RS_CMD_LDV_LOCAL_H
#define RS_CMD_LDV_LOCAL_H

#include "rs_cmd_ser_local.h"

#define RS_CMD_REU_HEAP_META_REL   0x8000u
#define RS_CMD_REU_HEAP_ARENA_REL  0x8100u
#define RS_CMD_REU_HEAP_ARENA_END  0xFF00u

#define RS_CMD_REU_HEAP_MAGIC0 'R'
#define RS_CMD_REU_HEAP_MAGIC1 'S'
#define RS_CMD_REU_HEAP_MAGIC2 'H'
#define RS_CMD_REU_HEAP_MAGIC3 '1'

#define RS_CMD_REC_FALSE  1u
#define RS_CMD_REC_TRUE   2u
#define RS_CMD_REC_U16    3u
#define RS_CMD_REC_STR    4u
#define RS_CMD_REC_ARRAY  5u
#define RS_CMD_REC_OBJECT 6u

#ifdef __CC65__
#define RS_CMD_REU_HEAP_SESSION_FLAG (*(unsigned char*)0xCFF0)
#endif

static int rs_cmd_heap_write_u8(unsigned short rel_off, unsigned char value) {
  return rs_reu_write(RS_CMD_REU_BANK_BASE + (unsigned long)rel_off, &value, 1u);
}

static int rs_cmd_heap_write_u16(unsigned short rel_off, unsigned short value) {
  unsigned char b[2];
  b[0] = (unsigned char)(value & 0xFFu);
  b[1] = (unsigned char)((value >> 8u) & 0xFFu);
  return rs_reu_write(RS_CMD_REU_BANK_BASE + (unsigned long)rel_off, b, 2u);
}

static int rs_cmd_heap_write_header(unsigned short next_free) {
  unsigned char hdr[10];
  hdr[0] = RS_CMD_REU_HEAP_MAGIC0;
  hdr[1] = RS_CMD_REU_HEAP_MAGIC1;
  hdr[2] = RS_CMD_REU_HEAP_MAGIC2;
  hdr[3] = RS_CMD_REU_HEAP_MAGIC3;
  hdr[4] = (unsigned char)(next_free & 0xFFu);
  hdr[5] = (unsigned char)((next_free >> 8u) & 0xFFu);
  hdr[6] = (unsigned char)(RS_CMD_REU_HEAP_ARENA_REL & 0xFFu);
  hdr[7] = (unsigned char)((RS_CMD_REU_HEAP_ARENA_REL >> 8u) & 0xFFu);
  hdr[8] = (unsigned char)(RS_CMD_REU_HEAP_ARENA_END & 0xFFu);
  hdr[9] = (unsigned char)((RS_CMD_REU_HEAP_ARENA_END >> 8u) & 0xFFu);
  return rs_reu_write(RS_CMD_REU_BANK_BASE + (unsigned long)RS_CMD_REU_HEAP_META_REL,
                      hdr,
                      sizeof(hdr));
}

static int rs_cmd_heap_ready(unsigned short* out_next_free) {
  unsigned char hdr[10];
  unsigned short next_free;
  unsigned short start;
  unsigned short end;

  if (!out_next_free || !rs_reu_available()) {
    return -1;
  }

#ifdef __CC65__
  if (RS_CMD_REU_HEAP_SESSION_FLAG != 1u) {
    if (rs_cmd_heap_write_header(RS_CMD_REU_HEAP_ARENA_REL) != 0) {
      return -1;
    }
    RS_CMD_REU_HEAP_SESSION_FLAG = 1u;
  }
#endif

  if (rs_reu_read(RS_CMD_REU_BANK_BASE + (unsigned long)RS_CMD_REU_HEAP_META_REL,
                  hdr,
                  sizeof(hdr)) != 0) {
    return -1;
  }
  next_free = (unsigned short)(hdr[4] | ((unsigned short)hdr[5] << 8u));
  start = (unsigned short)(hdr[6] | ((unsigned short)hdr[7] << 8u));
  end = (unsigned short)(hdr[8] | ((unsigned short)hdr[9] << 8u));
  if (hdr[0] != RS_CMD_REU_HEAP_MAGIC0 ||
      hdr[1] != RS_CMD_REU_HEAP_MAGIC1 ||
      hdr[2] != RS_CMD_REU_HEAP_MAGIC2 ||
      hdr[3] != RS_CMD_REU_HEAP_MAGIC3 ||
      start != RS_CMD_REU_HEAP_ARENA_REL ||
      end != RS_CMD_REU_HEAP_ARENA_END ||
      next_free < RS_CMD_REU_HEAP_ARENA_REL ||
      next_free > RS_CMD_REU_HEAP_ARENA_END) {
    if (rs_cmd_heap_write_header(RS_CMD_REU_HEAP_ARENA_REL) != 0) {
      return -1;
    }
    next_free = RS_CMD_REU_HEAP_ARENA_REL;
  }
  *out_next_free = next_free;
  return 0;
}

static int rs_cmd_heap_alloc(unsigned short size, unsigned short* out_off) {
  unsigned short next_free;
  unsigned short alloc_off;

  if (!out_off || size == 0u || rs_cmd_heap_ready(&next_free) != 0) {
    return -1;
  }
  if (size & 1u) {
    ++size;
  }
  if ((unsigned long)next_free + (unsigned long)size >
      (unsigned long)RS_CMD_REU_HEAP_ARENA_END) {
    return -1;
  }
  alloc_off = next_free;
  next_free = (unsigned short)(next_free + size);
  if (rs_cmd_heap_write_header(next_free) != 0) {
    return -1;
  }
  *out_off = alloc_off;
  return 0;
}

static void rs_cmd_heap_value_init_ptr(RSValue* out,
                                       RSValueTag tag,
                                       unsigned short off,
                                       unsigned short len) {
  out->tag = tag;
  out->as.ptr.off = off;
  out->as.ptr.len = len;
  out->as.ptr.aux = 0u;
}

static int rs_cmd_heap_value_load(unsigned short off, RSValue* out) {
  unsigned char rec_type;
  unsigned short len;
  unsigned char count;

  if (!out || rs_cmd_heap_read_u8(off, &rec_type) != 0) {
    return -1;
  }
  rs_cmd_value_free(out);
  rs_cmd_value_init_false(out);
  if (rec_type == RS_CMD_REC_FALSE) {
    return 0;
  }
  if (rec_type == RS_CMD_REC_TRUE) {
    rs_cmd_value_init_bool(out, 1);
    return 0;
  }
  if (rec_type == RS_CMD_REC_U16) {
    if (rs_cmd_heap_read_u16((unsigned short)(off + 1u), &len) != 0) {
      return -1;
    }
    rs_cmd_value_init_u16(out, len);
    return 0;
  }
  if (rec_type == RS_CMD_REC_STR) {
    if (rs_cmd_heap_read_u16((unsigned short)(off + 1u), &len) != 0) {
      return -1;
    }
    rs_cmd_heap_value_init_ptr(out, RS_VAL_STR_PTR, off, len);
    return 0;
  }
  if (rec_type == RS_CMD_REC_ARRAY) {
    if (rs_cmd_heap_read_u16((unsigned short)(off + 1u), &len) != 0) {
      return -1;
    }
    rs_cmd_heap_value_init_ptr(out, RS_VAL_ARRAY_PTR, off, len);
    return 0;
  }
  if (rec_type == RS_CMD_REC_OBJECT) {
    if (rs_cmd_heap_read_u8((unsigned short)(off + 1u), &count) != 0) {
      return -1;
    }
    rs_cmd_heap_value_init_ptr(out, RS_VAL_OBJECT_PTR, off, (unsigned short)count);
    return 0;
  }
  return -1;
}

static int rs_cmd_store_rsv1_value_to_heap(unsigned long base,
                                           unsigned short* pos,
                                           unsigned short max,
                                           unsigned short* out_off);

static int rs_cmd_parse_name_size(unsigned long base,
                                  unsigned short pos,
                                  unsigned short max,
                                  unsigned short* out_size) {
  unsigned char tag;
  unsigned char len;
  if (!out_size) {
    return -1;
  }
  if (rs_cmd_reu_get(base, &pos, max, &tag) != 0 || tag != (unsigned char)RS_VAL_STR) {
    return -1;
  }
  if (rs_cmd_reu_get(base, &pos, max, &len) != 0) {
    return -1;
  }
  if ((unsigned short)(pos + (unsigned short)len) > max) {
    return -1;
  }
  *out_size = (unsigned short)(1u + (unsigned short)len + 2u);
  return 0;
}

static int rs_cmd_store_rsv1_value_to_heap(unsigned long base,
                                           unsigned short* pos,
                                           unsigned short max,
                                           unsigned short* out_off) {
  unsigned char tag;
  unsigned short count;
  unsigned short size;
  unsigned short off;
  unsigned short i;
  unsigned short child_off;
  unsigned short cursor;
  unsigned short scan_pos;
  unsigned short name_size;
  unsigned char len8;
  unsigned char ch;

  if (!pos || !out_off || rs_cmd_reu_get(base, pos, max, &tag) != 0) {
    return -1;
  }

  if (tag == (unsigned char)RS_VAL_FALSE) {
    if (rs_cmd_heap_alloc(1u, &off) != 0 ||
        rs_cmd_heap_write_u8(off, RS_CMD_REC_FALSE) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (tag == (unsigned char)RS_VAL_TRUE) {
    if (rs_cmd_heap_alloc(1u, &off) != 0 ||
        rs_cmd_heap_write_u8(off, RS_CMD_REC_TRUE) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (tag == (unsigned char)RS_VAL_U16) {
    if (rs_cmd_reu_get_u16(base, pos, max, &count) != 0 ||
        rs_cmd_heap_alloc(3u, &off) != 0 ||
        rs_cmd_heap_write_u8(off, RS_CMD_REC_U16) != 0 ||
        rs_cmd_heap_write_u16((unsigned short)(off + 1u), count) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (tag == (unsigned char)RS_VAL_STR) {
    if (rs_cmd_reu_get(base, pos, max, &len8) != 0) {
      return -1;
    }
    size = (unsigned short)(3u + (unsigned short)len8);
    if ((unsigned short)(*pos + (unsigned short)len8) > max ||
        rs_cmd_heap_alloc(size, &off) != 0 ||
        rs_cmd_heap_write_u8(off, RS_CMD_REC_STR) != 0 ||
        rs_cmd_heap_write_u16((unsigned short)(off + 1u), (unsigned short)len8) != 0) {
      return -1;
    }
    for (i = 0u; i < (unsigned short)len8; ++i) {
      if (rs_cmd_reu_get(base, pos, max, &ch) != 0 ||
          rs_cmd_heap_write_u8((unsigned short)(off + 3u + i), ch) != 0) {
        return -1;
      }
    }
    *out_off = off;
    return 0;
  }
  if (tag == (unsigned char)RS_VAL_ARRAY) {
    if (rs_cmd_reu_get_u16(base, pos, max, &count) != 0) {
      return -1;
    }
    size = (unsigned short)(3u + (count * 2u));
    if (rs_cmd_heap_alloc(size, &off) != 0 ||
        rs_cmd_heap_write_u8(off, RS_CMD_REC_ARRAY) != 0 ||
        rs_cmd_heap_write_u16((unsigned short)(off + 1u), count) != 0) {
      return -1;
    }
    for (i = 0u; i < count; ++i) {
      if (rs_cmd_store_rsv1_value_to_heap(base, pos, max, &child_off) != 0 ||
          rs_cmd_heap_write_u16((unsigned short)(off + 3u + (i * 2u)), child_off) != 0) {
        return -1;
      }
    }
    *out_off = off;
    return 0;
  }
  if (tag != (unsigned char)RS_VAL_OBJECT) {
    return -1;
  }

  if (rs_cmd_reu_get(base, pos, max, &len8) != 0) {
    return -1;
  }
  count = (unsigned short)len8;
  scan_pos = *pos;
  size = 2u;
  for (i = 0u; i < count; ++i) {
    if (rs_cmd_parse_name_size(base, scan_pos, max, &name_size) != 0) {
      return -1;
    }
    size = (unsigned short)(size + name_size);
    if (rs_cmd_skip_value_from_reu(base, &scan_pos, max) != 0 ||
        rs_cmd_skip_value_from_reu(base, &scan_pos, max) != 0) {
      return -1;
    }
  }
  if (rs_cmd_heap_alloc(size, &off) != 0 ||
      rs_cmd_heap_write_u8(off, RS_CMD_REC_OBJECT) != 0 ||
      rs_cmd_heap_write_u8((unsigned short)(off + 1u), (unsigned char)count) != 0) {
    return -1;
  }
  cursor = (unsigned short)(off + 2u);
  for (i = 0u; i < count; ++i) {
    if (rs_cmd_reu_get(base, pos, max, &tag) != 0 ||
        tag != (unsigned char)RS_VAL_STR ||
        rs_cmd_reu_get(base, pos, max, &len8) != 0 ||
        rs_cmd_heap_write_u8(cursor, len8) != 0) {
      return -1;
    }
    ++cursor;
    for (name_size = 0u; name_size < (unsigned short)len8; ++name_size) {
      if (rs_cmd_reu_get(base, pos, max, &ch) != 0 ||
          rs_cmd_heap_write_u8(cursor, ch) != 0) {
        return -1;
      }
      ++cursor;
    }
    if (rs_cmd_store_rsv1_value_to_heap(base, pos, max, &child_off) != 0 ||
        rs_cmd_heap_write_u16(cursor, child_off) != 0) {
      return -1;
    }
    cursor = (unsigned short)(cursor + 2u);
  }
  *out_off = off;
  return 0;
}

static int rs_cmd_load_rsv1_value_to_heap(unsigned long base,
                                          unsigned short len,
                                          RSValue* out) {
  unsigned short pos;
  unsigned short off;

  if (!out) {
    return -1;
  }
  pos = 0u;
  if (rs_cmd_store_rsv1_value_to_heap(base, &pos, len, &off) != 0 || pos != len) {
    return -1;
  }
  return rs_cmd_heap_value_load(off, out);
}

#endif
