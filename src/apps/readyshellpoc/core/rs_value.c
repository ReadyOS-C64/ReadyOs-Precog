#include "rs_value.h"

#include "rs_token.h"

#include <stdlib.h>
#include <string.h>

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

void rs_value_init_false(RSValue* v) {
  if (!v) {
    return;
  }
  v->tag = RS_VAL_FALSE;
  v->as.u16 = 0;
}

void rs_value_init_true(RSValue* v) {
  if (!v) {
    return;
  }
  v->tag = RS_VAL_TRUE;
  v->as.u16 = 1;
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
  if (count > 0) {
    items = (RSValue*)malloc(sizeof(RSValue) * count);
    if (!items) {
      return -1;
    }
  }
  v->tag = RS_VAL_ARRAY;
  v->as.array.count = count;
  v->as.array.items = items;
  for (i = 0; i < count; ++i) {
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
    for (i = 0; i < count; ++i) {
      rs_value_init_u16(&v->as.array.items[i], (unsigned short)(start + i));
    }
  } else {
    count = (unsigned short)(start - end + 1u);
    if (rs_value_array_new(v, count) != 0) {
      return -1;
    }
    for (i = 0; i < count; ++i) {
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
  v->as.object.count = 0;
  v->as.object.props = 0;
  return 0;
}

static int rs_object_find_index(const RSValue* v, const char* name) {
  unsigned short i;
  if (!v || v->tag != RS_VAL_OBJECT || !name) {
    return -1;
  }
  for (i = 0; i < v->as.object.count; ++i) {
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

  if (v->as.object.count == 255) {
    return -1;
  }

  props = (RSObjectProp*)malloc(sizeof(RSObjectProp) * (v->as.object.count + 1u));
  if (!props) {
    return -1;
  }

  for (i = 0; i < v->as.object.count; ++i) {
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
    v->as.str.len = 0;
  } else if (v->tag == RS_VAL_ARRAY) {
    for (i = 0; i < v->as.array.count; ++i) {
      rs_value_free(&v->as.array.items[i]);
    }
    free(v->as.array.items);
    v->as.array.items = 0;
    v->as.array.count = 0;
  } else if (v->tag == RS_VAL_OBJECT) {
    for (i = 0; i < v->as.object.count; ++i) {
      free(v->as.object.props[i].name);
      if (v->as.object.props[i].value) {
        rs_value_free(v->as.object.props[i].value);
        free(v->as.object.props[i].value);
      }
    }
    free(v->as.object.props);
    v->as.object.props = 0;
    v->as.object.count = 0;
  }
  rs_value_init_false(v);
}

int rs_value_clone(RSValue* out, const RSValue* in) {
  unsigned short i;
  int rc;
  if (!out || !in) {
    return -1;
  }
  if (out == in) {
    return 0;
  }
  rs_value_free(out);
  out->tag = in->tag;
  if (in->tag == RS_VAL_U16) {
    out->as.u16 = in->as.u16;
    return 0;
  }
  if (in->tag == RS_VAL_FALSE || in->tag == RS_VAL_TRUE) {
    return 0;
  }
  if (in->tag == RS_VAL_STR) {
    return rs_value_init_string(out, in->as.str.bytes);
  }
  if (in->tag == RS_VAL_ARRAY) {
    if (rs_value_array_new(out, in->as.array.count) != 0) {
      return -1;
    }
    for (i = 0; i < in->as.array.count; ++i) {
      rc = rs_value_clone(&out->as.array.items[i], &in->as.array.items[i]);
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
    for (i = 0; i < in->as.object.count; ++i) {
      rc = rs_value_object_set(out,
                               in->as.object.props[i].name,
                               in->as.object.props[i].value);
      if (rc != 0) {
        return rc;
      }
    }
    return 0;
  }
  return -1;
}

int rs_value_eq(const RSValue* a, const RSValue* b) {
  unsigned short i;
  const RSValue* av;
  const RSValue* bv;
  if (!a || !b) {
    return 0;
  }
  if (a->tag != b->tag) {
    return 0;
  }
  if (a->tag == RS_VAL_FALSE || a->tag == RS_VAL_TRUE) {
    return 1;
  }
  if (a->tag == RS_VAL_U16) {
    return a->as.u16 == b->as.u16;
  }
  if (a->tag == RS_VAL_STR) {
    if (a->as.str.len != b->as.str.len) {
      return 0;
    }
    return rs_ci_equal(a->as.str.bytes, b->as.str.bytes);
  }
  if (a->tag == RS_VAL_ARRAY) {
    if (a->as.array.count != b->as.array.count) {
      return 0;
    }
    for (i = 0; i < a->as.array.count; ++i) {
      if (!rs_value_eq(&a->as.array.items[i], &b->as.array.items[i])) {
        return 0;
      }
    }
    return 1;
  }
  if (a->tag == RS_VAL_OBJECT) {
    if (a->as.object.count != b->as.object.count) {
      return 0;
    }
    for (i = 0; i < a->as.object.count; ++i) {
      av = a->as.object.props[i].value;
      bv = rs_value_object_get(b, a->as.object.props[i].name);
      if (!bv || !rs_value_eq(av, bv)) {
        return 0;
      }
    }
    return 1;
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
    return v->as.u16 != 0;
  }
  if (v->tag == RS_VAL_STR) {
    return v->as.str.len != 0;
  }
  if (v->tag == RS_VAL_ARRAY) {
    return v->as.array.count != 0;
  }
  if (v->tag == RS_VAL_OBJECT) {
    return v->as.object.count != 0;
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
    *out = 1;
    return 0;
  }
  if (v->tag == RS_VAL_FALSE) {
    *out = 0;
    return 0;
  }
  return -1;
}
