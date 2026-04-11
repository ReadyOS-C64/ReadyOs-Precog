#include "rs_vars.h"

#include "rs_token.h"

#include <string.h>

static int rs_find_slot(const RSVarTable* t, const char* name) {
  unsigned short i;
  if (!t || !name) {
    return -1;
  }
  for (i = 0; i < RS_VARS_MAX; ++i) {
    if (t->entries[i].used && rs_ci_equal(t->entries[i].name, name)) {
      return (int)i;
    }
  }
  return -1;
}

static int rs_find_or_alloc_slot(RSVarTable* t, const char* upper_name) {
  int idx;
  unsigned short i;

  idx = rs_find_slot(t, upper_name);
  if (idx >= 0) {
    return idx;
  }

  for (i = 0; i < RS_VARS_MAX; ++i) {
    if (!t->entries[i].used) {
      t->entries[i].used = 1;
      strncpy(t->entries[i].name, upper_name, RS_VAR_NAME_MAX);
      t->entries[i].name[RS_VAR_NAME_MAX] = '\0';
      return (int)i;
    }
  }

  return -1;
}

void rs_vars_init(RSVarTable* t) {
  unsigned short i;
  if (!t) {
    return;
  }
  for (i = 0; i < RS_VARS_MAX; ++i) {
    t->entries[i].used = 0;
    t->entries[i].name[0] = '\0';
    rs_value_init_false(&t->entries[i].value);
  }
}

void rs_vars_free(RSVarTable* t) {
  unsigned short i;
  if (!t) {
    return;
  }
  for (i = 0; i < RS_VARS_MAX; ++i) {
    if (t->entries[i].used) {
      rs_value_free(&t->entries[i].value);
      t->entries[i].used = 0;
      t->entries[i].name[0] = '\0';
    }
  }
}

int rs_vars_set(RSVarTable* t, const char* name, const RSValue* value) {
  int idx;
  char upper[RS_VAR_NAME_MAX + 1];
  if (!t || !name || !value) {
    return -1;
  }
  rs_upper_copy(upper, name, sizeof(upper));
  idx = rs_find_or_alloc_slot(t, upper);
  if (idx < 0) {
    return -1;
  }
  rs_value_free(&t->entries[idx].value);
  return rs_value_clone(&t->entries[idx].value, value);
}

int rs_vars_set_owned(RSVarTable* t, const char* name, RSValue* value) {
  int idx;
  char upper[RS_VAR_NAME_MAX + 1];
  RSValue stored;

  if (!t || !name || !value) {
    return -1;
  }

  rs_upper_copy(upper, name, sizeof(upper));
  idx = rs_find_or_alloc_slot(t, upper);
  if (idx < 0) {
    return -1;
  }

  rs_value_init_false(&stored);
  if (rs_value_clone(&stored, value) != 0) {
    rs_value_free(&stored);
    return -1;
  }
  rs_value_free(&t->entries[idx].value);
  t->entries[idx].value = stored;
  rs_value_free(value);
  rs_value_init_false(value);
  return 0;
}

const RSValue* rs_vars_get(const RSVarTable* t, const char* name) {
  int idx;
  char upper[RS_VAR_NAME_MAX + 1];
  if (!t || !name) {
    return 0;
  }
  rs_upper_copy(upper, name, sizeof(upper));
  idx = rs_find_slot(t, upper);
  if (idx < 0) {
    return 0;
  }
  return &t->entries[idx].value;
}
