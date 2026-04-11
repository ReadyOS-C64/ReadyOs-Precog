#include "rs_pipe.h"

#include <stdlib.h>

int rs_pipe_append_item(RSValue** items, unsigned short* count, const RSValue* item) {
  RSValue* next;
  unsigned short n;
  if (!items || !count || !item) {
    return -1;
  }
  n = (unsigned short)(*count + 1u);
  next = (RSValue*)realloc(*items, sizeof(RSValue) * n);
  if (!next) {
    return -1;
  }
  *items = next;
  rs_value_init_false(&(*items)[*count]);
  if (rs_value_clone(&(*items)[*count], item) != 0) {
    return -1;
  }
  *count = n;
  return 0;
}

int rs_pipe_expand_value(const RSValue* value, RSValue** out_items, unsigned short* out_count) {
  unsigned short i;
  unsigned short count;
  RSValue tmp;
  if (!value || !out_items || !out_count) {
    return -1;
  }
  *out_items = 0;
  *out_count = 0;
  if (rs_value_is_array_like(value)) {
    count = rs_value_array_count(value);
    rs_value_init_false(&tmp);
    for (i = 0; i < count; ++i) {
      if (rs_value_array_get(value, i, &tmp) != 0 ||
          rs_pipe_append_item(out_items, out_count, &tmp) != 0) {
        rs_value_free(&tmp);
        rs_pipe_free_items(*out_items, *out_count);
        *out_items = 0;
        *out_count = 0;
        return -1;
      }
      rs_value_free(&tmp);
    }
    return 0;
  }
  return rs_pipe_append_item(out_items, out_count, value);
}

void rs_pipe_free_items(RSValue* items, unsigned short count) {
  unsigned short i;
  if (!items) {
    return;
  }
  for (i = 0; i < count; ++i) {
    rs_value_free(&items[i]);
  }
  free(items);
}
