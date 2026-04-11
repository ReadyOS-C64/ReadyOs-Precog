#ifndef RS_VARS_H
#define RS_VARS_H

#include "rs_value.h"

#ifdef __CC65__
#define RS_VARS_MAX 24
#else
#define RS_VARS_MAX 128
#endif
#define RS_VAR_NAME_MAX 31

typedef struct RSVarEntry {
  int used;
  char name[RS_VAR_NAME_MAX + 1];
  RSValue value;
} RSVarEntry;

typedef struct RSVarTable {
  RSVarEntry entries[RS_VARS_MAX];
} RSVarTable;

void rs_vars_free(RSVarTable* t);
int rs_vars_set_owned(RSVarTable* t, const char* name, RSValue* value);
const RSValue* rs_vars_get(const RSVarTable* t, const char* name);
#ifndef __CC65__
void rs_vars_init(RSVarTable* t);
int rs_vars_set(RSVarTable* t, const char* name, const RSValue* value);
#endif

#endif
