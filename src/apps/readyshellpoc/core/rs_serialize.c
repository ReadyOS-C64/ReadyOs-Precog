#include "rs_serialize.h"

#include <stdlib.h>
#include <string.h>

static int rs_write_u16(unsigned char* dst, unsigned short max, unsigned short* pos, unsigned short v) {
  if ((unsigned long)*pos + 2ul > (unsigned long)max) {
    return -1;
  }
  dst[*pos] = (unsigned char)(v & 0xFFu);
  dst[*pos + 1u] = (unsigned char)((v >> 8u) & 0xFFu);
  *pos = (unsigned short)(*pos + 2u);
  return 0;
}

static int rs_read_u16(const unsigned char* src, unsigned short len, unsigned short* pos, unsigned short* out) {
  if ((unsigned long)*pos + 2ul > (unsigned long)len) {
    return -1;
  }
  *out = (unsigned short)(src[*pos] | ((unsigned short)src[*pos + 1u] << 8u));
  *pos = (unsigned short)(*pos + 2u);
  return 0;
}

static int rs_serialize_rec(const RSValue* value,
                            unsigned char* dst,
                            unsigned short max,
                            unsigned short* pos) {
  RSValue name_val;
  RSValue prop_val;
  char name_buf[64];
  char str_buf[256];
  unsigned short i;
  unsigned short n;

  if (!value) {
    return -1;
  }

  if (*pos >= max) {
    return -1;
  }
  if (rs_value_is_string_like(value)) {
    dst[*pos] = (unsigned char)RS_VAL_STR;
  } else if (rs_value_is_array_like(value)) {
    dst[*pos] = (unsigned char)RS_VAL_ARRAY;
  } else if (rs_value_is_object_like(value)) {
    dst[*pos] = (unsigned char)RS_VAL_OBJECT;
  } else {
    dst[*pos] = (unsigned char)value->tag;
  }
  *pos = (unsigned short)(*pos + 1u);

  if (value->tag == RS_VAL_FALSE || value->tag == RS_VAL_TRUE) {
    return 0;
  }

  if (value->tag == RS_VAL_U16) {
    return rs_write_u16(dst, max, pos, value->as.u16);
  }

  if (rs_value_is_string_like(value)) {
    if (rs_value_string_copy(value, str_buf, sizeof(str_buf)) != 0) {
      return -1;
    }
    n = (unsigned short)strlen(str_buf);
    if ((unsigned long)*pos + 1ul + (unsigned long)n > (unsigned long)max) {
      return -1;
    }
    dst[*pos] = (unsigned char)n;
    *pos = (unsigned short)(*pos + 1u);
    memcpy(dst + *pos, str_buf, n);
    *pos = (unsigned short)(*pos + n);
    return 0;
  }

  if (rs_value_is_array_like(value)) {
    n = rs_value_array_count(value);
    if (rs_write_u16(dst, max, pos, n) != 0) {
      return -1;
    }
    rs_value_init_false(&prop_val);
    for (i = 0; i < n; ++i) {
      if (rs_value_array_get(value, i, &prop_val) != 0 ||
          rs_serialize_rec(&prop_val, dst, max, pos) != 0) {
        rs_value_free(&prop_val);
        return -1;
      }
      rs_value_free(&prop_val);
    }
    return 0;
  }

  if (rs_value_is_object_like(value)) {
    n = rs_value_object_count(value);
    if ((unsigned long)*pos + 1ul > (unsigned long)max) {
      return -1;
    }
    dst[*pos] = (unsigned char)n;
    *pos = (unsigned short)(*pos + 1u);
    rs_value_init_false(&name_val);
    rs_value_init_false(&prop_val);
    for (i = 0; i < n; ++i) {
      if (rs_value_object_prop(value, i, name_buf, sizeof(name_buf), &prop_val) != 0) {
        return -1;
      }
      if ((unsigned short)strlen(name_buf) > 255u) {
        rs_value_free(&prop_val);
        return -1;
      }
      if (rs_value_init_string(&name_val, name_buf) != 0) {
        rs_value_free(&prop_val);
        return -1;
      }
      if (rs_serialize_rec(&name_val, dst, max, pos) != 0) {
        rs_value_free(&name_val);
        rs_value_free(&prop_val);
        return -1;
      }
      rs_value_free(&name_val);
      if (rs_serialize_rec(&prop_val, dst, max, pos) != 0) {
        rs_value_free(&prop_val);
        return -1;
      }
      rs_value_free(&prop_val);
    }
    return 0;
  }

  return -1;
}

int rs_serialize_value(const RSValue* value,
                       unsigned char* dst,
                       unsigned short max,
                       unsigned short* out_len) {
  unsigned short pos;
  if (!value || !dst || !out_len) {
    return -1;
  }
  pos = 0;
  if (rs_serialize_rec(value, dst, max, &pos) != 0) {
    return -1;
  }
  *out_len = pos;
  return 0;
}

static int rs_deserialize_rec(const unsigned char* src,
                              unsigned short len,
                              unsigned short* pos,
                              RSValue* out_value) {
  unsigned char tag;
  unsigned short n;
  unsigned short i;
  RSValue tmp;
  RSValue name_val;
  const char* name_ptr;

  if (*pos >= len) {
    return -1;
  }

  rs_value_free(out_value);

  tag = src[*pos];
  *pos = (unsigned short)(*pos + 1u);

  if (tag == RS_VAL_FALSE) {
    rs_value_init_false(out_value);
    return 0;
  }
  if (tag == RS_VAL_TRUE) {
    rs_value_init_true(out_value);
    return 0;
  }
  if (tag == RS_VAL_U16) {
    if (rs_read_u16(src, len, pos, &n) != 0) {
      return -1;
    }
    rs_value_init_u16(out_value, n);
    return 0;
  }
  if (tag == RS_VAL_STR) {
    char* tmp_buf;
    if (*pos >= len) {
      return -1;
    }
    n = src[*pos];
    *pos = (unsigned short)(*pos + 1u);
    if ((unsigned long)*pos + (unsigned long)n > (unsigned long)len) {
      return -1;
    }
    tmp_buf = (char*)malloc((size_t)n + 1u);
    if (!tmp_buf) {
      return -1;
    }
    memcpy(tmp_buf, src + *pos, n);
    tmp_buf[n] = '\0';
    *pos = (unsigned short)(*pos + n);
    if (rs_value_init_string(out_value, tmp_buf) != 0) {
      free(tmp_buf);
      return -1;
    }
    free(tmp_buf);
    return 0;
  }
  if (tag == RS_VAL_ARRAY) {
    if (rs_read_u16(src, len, pos, &n) != 0) {
      return -1;
    }
    if (rs_value_array_new(out_value, n) != 0) {
      return -1;
    }
    for (i = 0; i < n; ++i) {
      if (rs_deserialize_rec(src, len, pos, &out_value->as.array.items[i]) != 0) {
        return -1;
      }
    }
    return 0;
  }
  if (tag == RS_VAL_OBJECT) {
    if (*pos >= len) {
      return -1;
    }
    n = src[*pos];
    *pos = (unsigned short)(*pos + 1u);
    if (rs_value_object_new(out_value) != 0) {
      return -1;
    }
    for (i = 0; i < n; ++i) {
      rs_value_init_false(&name_val);
      if (rs_deserialize_rec(src, len, pos, &name_val) != 0) {
        rs_value_free(&name_val);
        return -1;
      }
      if (name_val.tag != RS_VAL_STR) {
        rs_value_free(&name_val);
        return -1;
      }
      name_ptr = name_val.as.str.bytes;
      rs_value_init_false(&tmp);
      if (rs_deserialize_rec(src, len, pos, &tmp) != 0) {
        rs_value_free(&name_val);
        rs_value_free(&tmp);
        return -1;
      }
      if (rs_value_object_set(out_value, name_ptr, &tmp) != 0) {
        rs_value_free(&name_val);
        rs_value_free(&tmp);
        return -1;
      }
      rs_value_free(&name_val);
      rs_value_free(&tmp);
    }
    return 0;
  }

  return -1;
}

int rs_deserialize_value(const unsigned char* src,
                         unsigned short len,
                         RSValue* out_value,
                         unsigned short* out_used) {
  unsigned short pos;
  if (!src || !out_value || !out_used) {
    return -1;
  }
  pos = 0;
  if (rs_deserialize_rec(src, len, &pos, out_value) != 0) {
    return -1;
  }
  *out_used = pos;
  return 0;
}

int rs_serialize_file_payload(const RSValue* value,
                              unsigned char* dst,
                              unsigned short max,
                              unsigned short* out_len) {
  unsigned short payload_len;
  if (!value || !dst || !out_len || max < 6u) {
    return -1;
  }
  if (rs_serialize_value(value, dst + 6u, (unsigned short)(max - 6u), &payload_len) != 0) {
    return -1;
  }
  dst[0] = 'R';
  dst[1] = 'S';
  dst[2] = 'V';
  dst[3] = '1';
  dst[4] = (unsigned char)(payload_len & 0xFFu);
  dst[5] = (unsigned char)((payload_len >> 8u) & 0xFFu);
  *out_len = (unsigned short)(payload_len + 6u);
  return 0;
}

int rs_deserialize_file_payload(const unsigned char* src,
                                unsigned short len,
                                RSValue* out_value) {
  unsigned short payload_len;
  unsigned short used;
  if (!src || len < 6u || !out_value) {
    return -1;
  }
  if (src[0] != 'R' || src[1] != 'S' || src[2] != 'V' || src[3] != '1') {
    return -1;
  }
  payload_len = (unsigned short)(src[4] | ((unsigned short)src[5] << 8u));
  if ((unsigned long)payload_len + 6ul != (unsigned long)len) {
    return -1;
  }
  if (rs_deserialize_value(src + 6u, payload_len, out_value, &used) != 0) {
    return -1;
  }
  if (used != payload_len) {
    return -1;
  }
  return 0;
}
