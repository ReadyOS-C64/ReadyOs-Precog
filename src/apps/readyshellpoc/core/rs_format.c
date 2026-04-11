#include "rs_format.h"

#include <string.h>

static char g_rs_format_name_buf[64];
static char g_rs_format_str_buf[256];

static int rs_append(char* out, unsigned short max, unsigned short* len, const char* s) {
  size_t n;
  if (!s) {
    s = "";
  }
  n = strlen(s);
  if ((unsigned long)*len + (unsigned long)n + 1ul > (unsigned long)max) {
    return -1;
  }
  memcpy(out + *len, s, n);
  *len = (unsigned short)(*len + (unsigned short)n);
  out[*len] = '\0';
  return 0;
}

static int rs_format_rec(const RSValue* v, char* out, unsigned short max, unsigned short* len) {
  char buf[32];
  char rev[16];
  RSValue tmp;
  unsigned short i;
  unsigned short n;
  unsigned short j;
  unsigned short k;

  if (!v) {
    return rs_append(out, max, len, "<null>");
  }

  if (v->tag == RS_VAL_FALSE) {
    return rs_append(out, max, len, "FALSE");
  }
  if (v->tag == RS_VAL_TRUE) {
    return rs_append(out, max, len, "TRUE");
  }
  if (v->tag == RS_VAL_U16) {
    n = v->as.u16;
    if (n == 0) {
      buf[0] = '0';
      buf[1] = '\0';
    } else {
      j = 0;
      while (n > 0 && j + 1u < sizeof(rev)) {
        rev[j++] = (char)('0' + (n % 10u));
        n = (unsigned short)(n / 10u);
      }
      for (k = 0; k < j; ++k) {
        buf[k] = rev[j - 1u - k];
      }
      buf[j] = '\0';
    }
    return rs_append(out, max, len, buf);
  }
  if (rs_value_is_string_like(v)) {
    if (rs_value_string_copy(v, g_rs_format_str_buf, sizeof(g_rs_format_str_buf)) != 0) {
      return -1;
    }
    n = (unsigned short)strlen(g_rs_format_str_buf);
    if ((unsigned long)*len + (unsigned long)n + 1ul > (unsigned long)max) {
      return -1;
    }
    memcpy(out + *len, g_rs_format_str_buf, n);
    *len = (unsigned short)(*len + n);
    out[*len] = '\0';
    return 0;
  }
  if (rs_value_is_array_like(v)) {
    if (rs_append(out, max, len, "[") != 0) {
      return -1;
    }
    rs_value_init_false(&tmp);
    for (i = 0; i < rs_value_array_count(v); ++i) {
      if (i != 0 && rs_append(out, max, len, ",") != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      if (rs_value_array_get(v, i, &tmp) != 0 ||
          rs_format_rec(&tmp, out, max, len) != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      rs_value_free(&tmp);
    }
    return rs_append(out, max, len, "]");
  }
  if (rs_value_is_object_like(v)) {
    if (rs_append(out, max, len, "{") != 0) {
      return -1;
    }
    rs_value_init_false(&tmp);
    for (i = 0; i < rs_value_object_count(v); ++i) {
      if (i != 0 && rs_append(out, max, len, ",") != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      if (rs_value_object_prop(v, i, g_rs_format_name_buf, sizeof(g_rs_format_name_buf), &tmp) != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      if (rs_append(out, max, len, g_rs_format_name_buf) != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      if (rs_append(out, max, len, ":") != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      if (rs_format_rec(&tmp, out, max, len) != 0) {
        rs_value_free(&tmp);
        return -1;
      }
      rs_value_free(&tmp);
    }
    return rs_append(out, max, len, "}");
  }

  return rs_append(out, max, len, "<unsupported>");
}

int rs_format_value(const RSValue* v, char* out, unsigned short max) {
  unsigned short len;
  if (!out || max == 0) {
    return -1;
  }
  out[0] = '\0';
  len = 0;
  if (rs_format_rec(v, out, max, &len) != 0) {
    return -1;
  }
  return (int)len;
}
