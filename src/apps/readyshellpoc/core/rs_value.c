#include "rs_value.h"

#include "rs_token.h"
#include "../platform/rs_platform.h"

#include <stdlib.h>
#include <string.h>

#define RS_REU_BANK_BASE_OFF   0x480000ul
#define RS_REU_HEAP_META_REL   0x8000u
#define RS_REU_HEAP_ARENA_REL  0x8100u
#define RS_REU_HEAP_ARENA_END  0xFF00u

#define RS_REU_HEAP_MAGIC0 'R'
#define RS_REU_HEAP_MAGIC1 'S'
#define RS_REU_HEAP_MAGIC2 'H'
#define RS_REU_HEAP_MAGIC3 '1'

#ifdef __CC65__
#define RS_REU_HEAP_SESSION_FLAG (*(unsigned char*)0xCFF0)
#else
static unsigned char g_rs_reu_heap_session_flag = 0u;
#define RS_REU_HEAP_SESSION_FLAG g_rs_reu_heap_session_flag
#endif

#define RS_REC_FALSE  1u
#define RS_REC_TRUE   2u
#define RS_REC_U16    3u
#define RS_REC_STR    4u
#define RS_REC_ARRAY  5u
#define RS_REC_OBJECT 6u

static char* rs_strdup_local(const char* s) {
  size_t n;
  char* p;
  if (!s) {
    s = "";
  }
  n = strlen(s);
  p = (char*)malloc(n + 1u);
  if (!p) {
    return 0;
  }
  memcpy(p, s, n + 1u);
  return p;
}

static unsigned long rs_reu_abs(unsigned short rel_off) {
  return RS_REU_BANK_BASE_OFF + (unsigned long)rel_off;
}

static int rs_reu_read_u8(unsigned short rel_off, unsigned char* out) {
  return rs_reu_read(rs_reu_abs(rel_off), out, 1u);
}

static int rs_reu_write_u8(unsigned short rel_off, unsigned char v) {
  return rs_reu_write(rs_reu_abs(rel_off), &v, 1u);
}

static int rs_reu_read_u16(unsigned short rel_off, unsigned short* out) {
  unsigned char b[2];
  if (!out || rs_reu_read(rs_reu_abs(rel_off), b, 2u) != 0) {
    return -1;
  }
  *out = (unsigned short)(b[0] | ((unsigned short)b[1] << 8u));
  return 0;
}

static int rs_reu_write_u16(unsigned short rel_off, unsigned short v) {
  unsigned char b[2];
  b[0] = (unsigned char)(v & 0xFFu);
  b[1] = (unsigned char)((v >> 8u) & 0xFFu);
  return rs_reu_write(rs_reu_abs(rel_off), b, 2u);
}

static int rs_heap_write_header(unsigned short next_free) {
  unsigned char hdr[10];
  hdr[0] = RS_REU_HEAP_MAGIC0;
  hdr[1] = RS_REU_HEAP_MAGIC1;
  hdr[2] = RS_REU_HEAP_MAGIC2;
  hdr[3] = RS_REU_HEAP_MAGIC3;
  hdr[4] = (unsigned char)(next_free & 0xFFu);
  hdr[5] = (unsigned char)((next_free >> 8u) & 0xFFu);
  hdr[6] = (unsigned char)(RS_REU_HEAP_ARENA_REL & 0xFFu);
  hdr[7] = (unsigned char)((RS_REU_HEAP_ARENA_REL >> 8u) & 0xFFu);
  hdr[8] = (unsigned char)(RS_REU_HEAP_ARENA_END & 0xFFu);
  hdr[9] = (unsigned char)((RS_REU_HEAP_ARENA_END >> 8u) & 0xFFu);
  return rs_reu_write(rs_reu_abs(RS_REU_HEAP_META_REL), hdr, sizeof(hdr));
}

static int rs_heap_ready(unsigned short* out_next_free) {
  unsigned char hdr[10];
  unsigned short next_free;
  unsigned short start;
  unsigned short end;

  if (!out_next_free || !rs_reu_available()) {
    return -1;
  }

  if (RS_REU_HEAP_SESSION_FLAG != 1u) {
    if (rs_heap_write_header(RS_REU_HEAP_ARENA_REL) != 0) {
      return -1;
    }
    RS_REU_HEAP_SESSION_FLAG = 1u;
  }

  if (rs_reu_read(rs_reu_abs(RS_REU_HEAP_META_REL), hdr, sizeof(hdr)) != 0) {
    return -1;
  }

  next_free = (unsigned short)(hdr[4] | ((unsigned short)hdr[5] << 8u));
  start = (unsigned short)(hdr[6] | ((unsigned short)hdr[7] << 8u));
  end = (unsigned short)(hdr[8] | ((unsigned short)hdr[9] << 8u));
  if (hdr[0] != RS_REU_HEAP_MAGIC0 ||
      hdr[1] != RS_REU_HEAP_MAGIC1 ||
      hdr[2] != RS_REU_HEAP_MAGIC2 ||
      hdr[3] != RS_REU_HEAP_MAGIC3 ||
      start != RS_REU_HEAP_ARENA_REL ||
      end != RS_REU_HEAP_ARENA_END ||
      next_free < RS_REU_HEAP_ARENA_REL ||
      next_free > RS_REU_HEAP_ARENA_END) {
    if (rs_heap_write_header(RS_REU_HEAP_ARENA_REL) != 0) {
      return -1;
    }
    next_free = RS_REU_HEAP_ARENA_REL;
  }

  *out_next_free = next_free;
  return 0;
}

static int rs_heap_alloc(unsigned short size, unsigned short* out_off) {
  unsigned short next_free;
  unsigned short alloc_off;

  if (!out_off || size == 0u || rs_heap_ready(&next_free) != 0) {
    return -1;
  }
  if (size & 1u) {
    ++size;
  }
  if ((unsigned long)next_free + (unsigned long)size > (unsigned long)RS_REU_HEAP_ARENA_END) {
    return -1;
  }
  alloc_off = next_free;
  next_free = (unsigned short)(next_free + size);
  if (rs_heap_write_header(next_free) != 0) {
    return -1;
  }
  *out_off = alloc_off;
  return 0;
}

static unsigned char rs_ascii_upper(unsigned char ch) {
  if (ch >= 'a' && ch <= 'z') {
    return (unsigned char)(ch - ('a' - 'A'));
  }
  return ch;
}

static int rs_name_equals_reu(unsigned short rel_off, const char* name, unsigned char* out_len) {
  unsigned char name_len;
  unsigned char i;
  unsigned char ch;
  if (!name || rs_reu_read_u8(rel_off, &name_len) != 0) {
    return -1;
  }
  if (out_len) {
    *out_len = name_len;
  }
  for (i = 0u; i < name_len; ++i) {
    if (name[i] == '\0') {
      return 0;
    }
    if (rs_reu_read_u8((unsigned short)(rel_off + 1u + i), &ch) != 0) {
      return -1;
    }
    if (rs_ascii_upper(ch) != rs_ascii_upper((unsigned char)name[i])) {
      return 0;
    }
  }
  if (name[name_len] != '\0') {
    return 0;
  }
  return 1;
}

static void rs_value_init_ptr(RSValue* v, RSValueTag tag, unsigned short off, unsigned short len) {
  if (!v) {
    return;
  }
  v->tag = tag;
  v->as.ptr.off = off;
  v->as.ptr.len = len;
  v->as.ptr.aux = 0u;
}

static int rs_value_load_from_reu(unsigned short off, RSValue* out) {
  unsigned char rec_type;
  unsigned short n;
  unsigned char count;

  if (!out || rs_reu_read_u8(off, &rec_type) != 0) {
    return -1;
  }
  rs_value_init_false(out);
  if (rec_type == RS_REC_FALSE) {
    return 0;
  }
  if (rec_type == RS_REC_TRUE) {
    out->tag = RS_VAL_TRUE;
    out->as.u16 = 1u;
    return 0;
  }
  if (rec_type == RS_REC_U16) {
    if (rs_reu_read_u16((unsigned short)(off + 1u), &n) != 0) {
      return -1;
    }
    out->tag = RS_VAL_U16;
    out->as.u16 = n;
    return 0;
  }
  if (rec_type == RS_REC_STR) {
    if (rs_reu_read_u16((unsigned short)(off + 1u), &n) != 0) {
      return -1;
    }
    rs_value_init_ptr(out, RS_VAL_STR_PTR, off, n);
    return 0;
  }
  if (rec_type == RS_REC_ARRAY) {
    if (rs_reu_read_u16((unsigned short)(off + 1u), &n) != 0) {
      return -1;
    }
    rs_value_init_ptr(out, RS_VAL_ARRAY_PTR, off, n);
    return 0;
  }
  if (rec_type == RS_REC_OBJECT) {
    if (rs_reu_read_u8((unsigned short)(off + 1u), &count) != 0) {
      return -1;
    }
    rs_value_init_ptr(out, RS_VAL_OBJECT_PTR, off, (unsigned short)count);
    return 0;
  }
  return -1;
}

static int rs_value_store_nested(const RSValue* value, unsigned short* out_off) {
  unsigned short off;
  unsigned short i;
  unsigned short cursor;
  unsigned short child_off;
  unsigned short size;
  unsigned short name_len;

  if (!value || !out_off) {
    return -1;
  }
  if (value->tag == RS_VAL_STR_PTR ||
      value->tag == RS_VAL_ARRAY_PTR ||
      value->tag == RS_VAL_OBJECT_PTR) {
    *out_off = value->as.ptr.off;
    return 0;
  }

  if (value->tag == RS_VAL_FALSE) {
    if (rs_heap_alloc(1u, &off) != 0 || rs_reu_write_u8(off, RS_REC_FALSE) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (value->tag == RS_VAL_TRUE) {
    if (rs_heap_alloc(1u, &off) != 0 || rs_reu_write_u8(off, RS_REC_TRUE) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (value->tag == RS_VAL_U16) {
    if (rs_heap_alloc(3u, &off) != 0 ||
        rs_reu_write_u8(off, RS_REC_U16) != 0 ||
        rs_reu_write_u16((unsigned short)(off + 1u), value->as.u16) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (value->tag == RS_VAL_STR) {
    size = (unsigned short)(3u + value->as.str.len);
    if (rs_heap_alloc(size, &off) != 0 ||
        rs_reu_write_u8(off, RS_REC_STR) != 0 ||
        rs_reu_write_u16((unsigned short)(off + 1u), (unsigned short)value->as.str.len) != 0 ||
        rs_reu_write(rs_reu_abs((unsigned short)(off + 3u)),
                     value->as.str.bytes,
                     (unsigned short)value->as.str.len) != 0) {
      return -1;
    }
    *out_off = off;
    return 0;
  }
  if (value->tag == RS_VAL_ARRAY) {
    size = (unsigned short)(3u + (value->as.array.count * 2u));
    if (rs_heap_alloc(size, &off) != 0 ||
        rs_reu_write_u8(off, RS_REC_ARRAY) != 0 ||
        rs_reu_write_u16((unsigned short)(off + 1u), value->as.array.count) != 0) {
      return -1;
    }
    for (i = 0u; i < value->as.array.count; ++i) {
      if (rs_value_store_nested(&value->as.array.items[i], &child_off) != 0 ||
          rs_reu_write_u16((unsigned short)(off + 3u + (i * 2u)), child_off) != 0) {
        return -1;
      }
    }
    *out_off = off;
    return 0;
  }
  if (value->tag == RS_VAL_OBJECT) {
    size = 2u;
    for (i = 0u; i < value->as.object.count; ++i) {
      name_len = (unsigned short)strlen(value->as.object.props[i].name);
      if (name_len > 255u) {
        return -1;
      }
      size = (unsigned short)(size + 1u + name_len + 2u);
    }
    if (rs_heap_alloc(size, &off) != 0 ||
        rs_reu_write_u8(off, RS_REC_OBJECT) != 0 ||
        rs_reu_write_u8((unsigned short)(off + 1u), value->as.object.count) != 0) {
      return -1;
    }
    cursor = (unsigned short)(off + 2u);
    for (i = 0u; i < value->as.object.count; ++i) {
      name_len = (unsigned short)strlen(value->as.object.props[i].name);
      if (name_len > 255u) {
        return -1;
      }
      if (rs_reu_write_u8(cursor, (unsigned char)name_len) != 0) {
        return -1;
      }
      ++cursor;
      if (name_len != 0u &&
          rs_reu_write(rs_reu_abs(cursor),
                       value->as.object.props[i].name,
                       name_len) != 0) {
        return -1;
      }
      cursor = (unsigned short)(cursor + name_len);
      if (rs_value_store_nested(value->as.object.props[i].value, &child_off) != 0 ||
          rs_reu_write_u16(cursor, child_off) != 0) {
        return -1;
      }
      cursor = (unsigned short)(cursor + 2u);
    }
    *out_off = off;
    return 0;
  }
  return -1;
}

static int rs_value_clone_ram(RSValue* out, const RSValue* in) {
  unsigned short i;
  int rc;
  if (!out || !in) {
    return -1;
  }
  rs_value_free(out);
  out->tag = in->tag;
  if (in->tag == RS_VAL_U16) {
    out->as.u16 = in->as.u16;
    return 0;
  }
  if (in->tag == RS_VAL_FALSE || in->tag == RS_VAL_TRUE) {
    out->as.u16 = in->tag == RS_VAL_TRUE ? 1u : 0u;
    return 0;
  }
  if (in->tag == RS_VAL_STR) {
    return rs_value_init_string(out, in->as.str.bytes);
  }
  if (in->tag == RS_VAL_ARRAY) {
    if (rs_value_array_new(out, in->as.array.count) != 0) {
      return -1;
    }
    for (i = 0u; i < in->as.array.count; ++i) {
      rc = rs_value_clone_ram(&out->as.array.items[i], &in->as.array.items[i]);
      if (rc != 0) {
        return rc;
      }
    }
    return 0;
  }
  if (in->tag == RS_VAL_OBJECT) {
    if (rs_value_object_new(out) != 0) {
      return -1;
    }
    for (i = 0u; i < in->as.object.count; ++i) {
      rc = rs_value_object_set(out,
                               in->as.object.props[i].name,
                               in->as.object.props[i].value);
      if (rc != 0) {
        return rc;
      }
    }
    return 0;
  }
  if (in->tag == RS_VAL_STR_PTR ||
      in->tag == RS_VAL_ARRAY_PTR ||
      in->tag == RS_VAL_OBJECT_PTR) {
    out->as.ptr = in->as.ptr;
    return 0;
  }
  return -1;
}

void rs_value_init_false(RSValue* v) {
  if (!v) {
    return;
  }
  v->tag = RS_VAL_FALSE;
  v->as.u16 = 0u;
}

void rs_value_init_true(RSValue* v) {
  if (!v) {
    return;
  }
  v->tag = RS_VAL_TRUE;
  v->as.u16 = 1u;
}

void rs_value_init_bool(RSValue* v, int truthy) {
  if (truthy) {
    rs_value_init_true(v);
  } else {
    rs_value_init_false(v);
  }
}

void rs_value_init_u16(RSValue* v, unsigned short n) {
  if (!v) {
    return;
  }
  v->tag = RS_VAL_U16;
  v->as.u16 = n;
}

int rs_value_init_string(RSValue* v, const char* s) {
  size_t n;
  char* p;
  if (!v) {
    return -1;
  }
  if (!s) {
    s = "";
  }
  n = strlen(s);
  if (n > 255u) {
    return -1;
  }
  p = rs_strdup_local(s);
  if (!p) {
    return -1;
  }
  v->tag = RS_VAL_STR;
  v->as.str.len = (unsigned char)n;
  v->as.str.bytes = p;
  return 0;
}

int rs_value_array_new(RSValue* v, unsigned short count) {
  RSValue* items;
  unsigned short i;
  if (!v) {
    return -1;
  }
  items = 0;
  if (count > 0u) {
    items = (RSValue*)malloc(sizeof(RSValue) * count);
    if (!items) {
      return -1;
    }
  }
  v->tag = RS_VAL_ARRAY;
  v->as.array.count = count;
  v->as.array.items = items;
  for (i = 0u; i < count; ++i) {
    rs_value_init_false(&items[i]);
  }
  return 0;
}

int rs_value_array_from_u16_range(RSValue* v, unsigned short start, unsigned short end) {
  unsigned short count;
  unsigned short i;
  unsigned short val;
  if (start <= end) {
    count = (unsigned short)(end - start + 1u);
    if (rs_value_array_new(v, count) != 0) {
      return -1;
    }
    for (i = 0u; i < count; ++i) {
      rs_value_init_u16(&v->as.array.items[i], (unsigned short)(start + i));
    }
  } else {
    count = (unsigned short)(start - end + 1u);
    if (rs_value_array_new(v, count) != 0) {
      return -1;
    }
    for (i = 0u; i < count; ++i) {
      val = (unsigned short)(start - i);
      rs_value_init_u16(&v->as.array.items[i], val);
    }
  }
  return 0;
}

int rs_value_object_new(RSValue* v) {
  if (!v) {
    return -1;
  }
  v->tag = RS_VAL_OBJECT;
  v->as.object.count = 0u;
  v->as.object.props = 0;
  return 0;
}

static int rs_object_find_index(const RSValue* v, const char* name) {
  unsigned short i;
  if (!v || v->tag != RS_VAL_OBJECT || !name) {
    return -1;
  }
  for (i = 0u; i < v->as.object.count; ++i) {
    if (rs_ci_equal(v->as.object.props[i].name, name)) {
      return (int)i;
    }
  }
  return -1;
}

int rs_value_object_set(RSValue* v, const char* name, const RSValue* prop_value) {
  int idx;
  RSObjectProp* props;
  RSValue* pv;
  char* nm;
  unsigned short i;

  if (!v || v->tag != RS_VAL_OBJECT || !name || !prop_value) {
    return -1;
  }

  idx = rs_object_find_index(v, name);
  if (idx >= 0) {
    pv = v->as.object.props[idx].value;
    rs_value_free(pv);
    return rs_value_clone(pv, prop_value);
  }

  if (v->as.object.count == 255u) {
    return -1;
  }

  props = (RSObjectProp*)malloc(sizeof(RSObjectProp) * (v->as.object.count + 1u));
  if (!props) {
    return -1;
  }
  for (i = 0u; i < v->as.object.count; ++i) {
    props[i] = v->as.object.props[i];
  }

  nm = rs_strdup_local(name);
  if (!nm) {
    free(props);
    return -1;
  }
  pv = (RSValue*)malloc(sizeof(RSValue));
  if (!pv) {
    free(nm);
    free(props);
    return -1;
  }
  rs_value_init_false(pv);
  if (rs_value_clone(pv, prop_value) != 0) {
    free(pv);
    free(nm);
    free(props);
    return -1;
  }
  props[v->as.object.count].name = nm;
  props[v->as.object.count].value = pv;

  if (v->as.object.props) {
    free(v->as.object.props);
  }
  v->as.object.props = props;
  v->as.object.count = (unsigned char)(v->as.object.count + 1u);
  return 0;
}

const RSValue* rs_value_object_get(const RSValue* v, const char* name) {
  int idx;
  if (!v || v->tag != RS_VAL_OBJECT || !name) {
    return 0;
  }
  idx = rs_object_find_index(v, name);
  if (idx < 0) {
    return 0;
  }
  return v->as.object.props[idx].value;
}

void rs_value_free(RSValue* v) {
  unsigned short i;
  if (!v) {
    return;
  }
  if (v->tag == RS_VAL_STR) {
    free(v->as.str.bytes);
    v->as.str.bytes = 0;
    v->as.str.len = 0u;
  } else if (v->tag == RS_VAL_ARRAY) {
    for (i = 0u; i < v->as.array.count; ++i) {
      rs_value_free(&v->as.array.items[i]);
    }
    free(v->as.array.items);
    v->as.array.items = 0;
    v->as.array.count = 0u;
  } else if (v->tag == RS_VAL_OBJECT) {
    for (i = 0u; i < v->as.object.count; ++i) {
      free(v->as.object.props[i].name);
      if (v->as.object.props[i].value) {
        rs_value_free(v->as.object.props[i].value);
        free(v->as.object.props[i].value);
      }
    }
    free(v->as.object.props);
    v->as.object.props = 0;
    v->as.object.count = 0u;
  }
  rs_value_init_false(v);
}

int rs_value_clone(RSValue* out, const RSValue* in) {
  unsigned short off;
  if (!out || !in) {
    return -1;
  }
  if (out == in) {
    return 0;
  }
  rs_value_free(out);
  if (in->tag == RS_VAL_FALSE || in->tag == RS_VAL_TRUE || in->tag == RS_VAL_U16) {
    out->tag = in->tag;
    out->as.u16 = in->as.u16;
    return 0;
  }
  if (in->tag == RS_VAL_STR_PTR ||
      in->tag == RS_VAL_ARRAY_PTR ||
      in->tag == RS_VAL_OBJECT_PTR) {
    out->tag = in->tag;
    out->as.ptr = in->as.ptr;
    return 0;
  }
  if (!rs_reu_available()) {
    return rs_value_clone_ram(out, in);
  }
  if (in->tag == RS_VAL_STR) {
    if (rs_value_store_nested(in, &off) != 0) {
      return -1;
    }
    rs_value_init_ptr(out, RS_VAL_STR_PTR, off, (unsigned short)in->as.str.len);
    return 0;
  }
  if (in->tag == RS_VAL_ARRAY) {
    if (rs_value_store_nested(in, &off) != 0) {
      return -1;
    }
    rs_value_init_ptr(out, RS_VAL_ARRAY_PTR, off, in->as.array.count);
    return 0;
  }
  if (in->tag == RS_VAL_OBJECT) {
    if (rs_value_store_nested(in, &off) != 0) {
      return -1;
    }
    rs_value_init_ptr(out, RS_VAL_OBJECT_PTR, off, (unsigned short)in->as.object.count);
    return 0;
  }
  return -1;
}

int rs_value_is_string_like(const RSValue* v) {
  return v ? (v->tag == RS_VAL_STR || v->tag == RS_VAL_STR_PTR) : 0;
}

int rs_value_is_array_like(const RSValue* v) {
  return v ? (v->tag == RS_VAL_ARRAY || v->tag == RS_VAL_ARRAY_PTR) : 0;
}

int rs_value_is_object_like(const RSValue* v) {
  return v ? (v->tag == RS_VAL_OBJECT || v->tag == RS_VAL_OBJECT_PTR) : 0;
}

unsigned short rs_value_array_count(const RSValue* v) {
  if (!v) {
    return 0u;
  }
  if (v->tag == RS_VAL_ARRAY) {
    return v->as.array.count;
  }
  if (v->tag == RS_VAL_ARRAY_PTR) {
    return v->as.ptr.len;
  }
  return 0u;
}

unsigned short rs_value_object_count(const RSValue* v) {
  if (!v) {
    return 0u;
  }
  if (v->tag == RS_VAL_OBJECT) {
    return (unsigned short)v->as.object.count;
  }
  if (v->tag == RS_VAL_OBJECT_PTR) {
    return v->as.ptr.len;
  }
  return 0u;
}

int rs_value_string_copy(const RSValue* v, char* out, unsigned short max) {
  unsigned short len;
  if (!v || !out || max == 0u) {
    return -1;
  }
  if (v->tag == RS_VAL_STR) {
    if ((unsigned short)v->as.str.len + 1u > max) {
      return -1;
    }
    memcpy(out, v->as.str.bytes, (unsigned short)v->as.str.len);
    out[v->as.str.len] = '\0';
    return 0;
  }
  if (v->tag == RS_VAL_STR_PTR) {
    len = v->as.ptr.len;
    if (len + 1u > max ||
        rs_reu_read(rs_reu_abs((unsigned short)(v->as.ptr.off + 3u)), out, len) != 0) {
      return -1;
    }
    out[len] = '\0';
    return 0;
  }
  return -1;
}

int rs_value_array_get(const RSValue* v, unsigned short index, RSValue* out) {
  unsigned short child_off;
  if (!v || !out) {
    return -1;
  }
  rs_value_free(out);
  rs_value_init_false(out);
  if (v->tag == RS_VAL_ARRAY) {
    if (index >= v->as.array.count) {
      return -1;
    }
    return rs_value_clone(out, &v->as.array.items[index]);
  }
  if (v->tag == RS_VAL_ARRAY_PTR) {
    if (index >= v->as.ptr.len ||
        rs_reu_read_u16((unsigned short)(v->as.ptr.off + 3u + (index * 2u)), &child_off) != 0) {
      return -1;
    }
    return rs_value_load_from_reu(child_off, out);
  }
  return -1;
}

int rs_value_object_get_copy(const RSValue* v, const char* name, RSValue* out) {
  int idx;
  unsigned short cursor;
  unsigned short i;
  unsigned short child_off;
  unsigned char name_len;
  int match;
  if (!v || !name || !out) {
    return -1;
  }
  rs_value_free(out);
  rs_value_init_false(out);
  if (v->tag == RS_VAL_OBJECT) {
    idx = rs_object_find_index(v, name);
    if (idx < 0) {
      return -1;
    }
    return rs_value_clone(out, v->as.object.props[idx].value);
  }
  if (v->tag != RS_VAL_OBJECT_PTR) {
    return -1;
  }
  cursor = (unsigned short)(v->as.ptr.off + 2u);
  for (i = 0u; i < v->as.ptr.len; ++i) {
    match = rs_name_equals_reu(cursor, name, &name_len);
    if (match < 0) {
      return -1;
    }
    if (rs_reu_read_u16((unsigned short)(cursor + 1u + name_len), &child_off) != 0) {
      return -1;
    }
    if (match == 1) {
      return rs_value_load_from_reu(child_off, out);
    }
    cursor = (unsigned short)(cursor + 1u + name_len + 2u);
  }
  return -1;
}

int rs_value_object_prop(const RSValue* v,
                         unsigned short index,
                         char* name_out,
                         unsigned short name_max,
                         RSValue* out_value) {
  unsigned short cursor;
  unsigned short i;
  unsigned char name_len;
  unsigned short child_off;
  if (!v || !name_out || name_max == 0u || !out_value) {
    return -1;
  }
  rs_value_free(out_value);
  rs_value_init_false(out_value);
  if (v->tag == RS_VAL_OBJECT) {
    if (index >= (unsigned short)v->as.object.count) {
      return -1;
    }
    strncpy(name_out, v->as.object.props[index].name, name_max - 1u);
    name_out[name_max - 1u] = '\0';
    return rs_value_clone(out_value, v->as.object.props[index].value);
  }
  if (v->tag != RS_VAL_OBJECT_PTR || index >= v->as.ptr.len) {
    return -1;
  }
  cursor = (unsigned short)(v->as.ptr.off + 2u);
  for (i = 0u; i < index; ++i) {
    if (rs_reu_read_u8(cursor, &name_len) != 0) {
      return -1;
    }
    cursor = (unsigned short)(cursor + 1u + name_len + 2u);
  }
  if (rs_reu_read_u8(cursor, &name_len) != 0 ||
      (unsigned short)name_len + 1u > name_max ||
      rs_reu_read(rs_reu_abs((unsigned short)(cursor + 1u)), name_out, name_len) != 0 ||
      rs_reu_read_u16((unsigned short)(cursor + 1u + name_len), &child_off) != 0) {
    return -1;
  }
  name_out[name_len] = '\0';
  return rs_value_load_from_reu(child_off, out_value);
}

static unsigned short rs_value_string_len_local(const RSValue* v) {
  if (!v) {
    return 0u;
  }
  if (v->tag == RS_VAL_STR) {
    return (unsigned short)v->as.str.len;
  }
  if (v->tag == RS_VAL_STR_PTR) {
    return v->as.ptr.len;
  }
  return 0u;
}

static int rs_value_string_char(const RSValue* v, unsigned short index, unsigned char* out) {
  if (!v || !out) {
    return -1;
  }
  if (v->tag == RS_VAL_STR) {
    if (index >= (unsigned short)v->as.str.len) {
      return -1;
    }
    *out = (unsigned char)v->as.str.bytes[index];
    return 0;
  }
  if (v->tag == RS_VAL_STR_PTR) {
    if (index >= v->as.ptr.len) {
      return -1;
    }
    return rs_reu_read_u8((unsigned short)(v->as.ptr.off + 3u + index), out);
  }
  return -1;
}

static int rs_value_eq_strings(const RSValue* a, const RSValue* b) {
  unsigned short i;
  unsigned short len;
  unsigned char ca;
  unsigned char cb;
  len = rs_value_string_len_local(a);
  if (len != rs_value_string_len_local(b)) {
    return 0;
  }
  for (i = 0u; i < len; ++i) {
    if (rs_value_string_char(a, i, &ca) != 0 ||
        rs_value_string_char(b, i, &cb) != 0 ||
        rs_ascii_upper(ca) != rs_ascii_upper(cb)) {
      return 0;
    }
  }
  return 1;
}

static int rs_value_eq_arrays(const RSValue* a, const RSValue* b) {
  unsigned short i;
  RSValue av;
  RSValue bv;
  if (rs_value_array_count(a) != rs_value_array_count(b)) {
    return 0;
  }
  rs_value_init_false(&av);
  rs_value_init_false(&bv);
  for (i = 0u; i < rs_value_array_count(a); ++i) {
    if (rs_value_array_get(a, i, &av) != 0 ||
        rs_value_array_get(b, i, &bv) != 0 ||
        !rs_value_eq(&av, &bv)) {
      rs_value_free(&av);
      rs_value_free(&bv);
      return 0;
    }
    rs_value_free(&av);
    rs_value_free(&bv);
  }
  return 1;
}

static int rs_value_eq_objects(const RSValue* a, const RSValue* b) {
  unsigned short i;
  RSValue av;
  RSValue bv;
  char name[64];
  if (rs_value_object_count(a) != rs_value_object_count(b)) {
    return 0;
  }
  rs_value_init_false(&av);
  rs_value_init_false(&bv);
  for (i = 0u; i < rs_value_object_count(a); ++i) {
    if (rs_value_object_prop(a, i, name, sizeof(name), &av) != 0 ||
        rs_value_object_get_copy(b, name, &bv) != 0 ||
        !rs_value_eq(&av, &bv)) {
      rs_value_free(&av);
      rs_value_free(&bv);
      return 0;
    }
    rs_value_free(&av);
    rs_value_free(&bv);
  }
  return 1;
}

int rs_value_eq(const RSValue* a, const RSValue* b) {
  if (!a || !b) {
    return 0;
  }
  if ((a->tag == RS_VAL_FALSE || a->tag == RS_VAL_TRUE) &&
      (b->tag == RS_VAL_FALSE || b->tag == RS_VAL_TRUE)) {
    return (a->tag == RS_VAL_TRUE) == (b->tag == RS_VAL_TRUE);
  }
  if (a->tag == RS_VAL_U16 && b->tag == RS_VAL_U16) {
    return a->as.u16 == b->as.u16;
  }
  if (rs_value_is_string_like(a) && rs_value_is_string_like(b)) {
    return rs_value_eq_strings(a, b);
  }
  if (rs_value_is_array_like(a) && rs_value_is_array_like(b)) {
    return rs_value_eq_arrays(a, b);
  }
  if (rs_value_is_object_like(a) && rs_value_is_object_like(b)) {
    return rs_value_eq_objects(a, b);
  }
  return 0;
}

int rs_value_truthy(const RSValue* v) {
  if (!v) {
    return 0;
  }
  if (v->tag == RS_VAL_FALSE) {
    return 0;
  }
  if (v->tag == RS_VAL_TRUE) {
    return 1;
  }
  if (v->tag == RS_VAL_U16) {
    return v->as.u16 != 0u;
  }
  if (v->tag == RS_VAL_STR) {
    return v->as.str.len != 0u;
  }
  if (v->tag == RS_VAL_STR_PTR) {
    return v->as.ptr.len != 0u;
  }
  if (v->tag == RS_VAL_ARRAY || v->tag == RS_VAL_ARRAY_PTR) {
    return rs_value_array_count(v) != 0u;
  }
  if (v->tag == RS_VAL_OBJECT || v->tag == RS_VAL_OBJECT_PTR) {
    return rs_value_object_count(v) != 0u;
  }
  return 0;
}

int rs_value_to_u16(const RSValue* v, unsigned short* out) {
  if (!v || !out) {
    return -1;
  }
  if (v->tag == RS_VAL_U16) {
    *out = v->as.u16;
    return 0;
  }
  if (v->tag == RS_VAL_TRUE) {
    *out = 1u;
    return 0;
  }
  if (v->tag == RS_VAL_FALSE) {
    *out = 0u;
    return 0;
  }
  return -1;
}

void rs_value_heap_reset(void) {
  if (!rs_reu_available()) {
    return;
  }
  RS_REU_HEAP_SESSION_FLAG = 1u;
  (void)rs_heap_write_header(RS_REU_HEAP_ARENA_REL);
}

unsigned short rs_value_heap_next_free(void) {
  unsigned short next_free;
  if (rs_heap_ready(&next_free) != 0) {
    return 0u;
  }
  return next_free;
}
