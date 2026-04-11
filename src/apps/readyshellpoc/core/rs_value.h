#ifndef RS_VALUE_H
#define RS_VALUE_H

#include "rs_errors.h"

typedef enum {
  RS_VAL_FALSE = 0x00,
  RS_VAL_TRUE = 0xFF,
  RS_VAL_FLOAT = 0x01,
  RS_VAL_STR = 0x02,
  RS_VAL_STR_PTR = 0x03,
  RS_VAL_PETSCII_Z = 0x04,
  RS_VAL_PETSCII_Z_PTR = 0x05,
  RS_VAL_ARRAY = 0x06,
  RS_VAL_ARRAY_PTR = 0x07,
  RS_VAL_OBJECT = 0x08,
  RS_VAL_U16 = 0x09,
  RS_VAL_OBJECT_PTR = 0x0A
} RSValueTag;

struct RSValue;

typedef struct RSArray {
  unsigned short count;
  struct RSValue* items;
} RSArray;

typedef struct RSObjectProp {
  char* name;
  struct RSValue* value;
} RSObjectProp;

typedef struct RSObject {
  unsigned char count;
  RSObjectProp* props;
} RSObject;

typedef struct RSValue {
  RSValueTag tag;
  union {
    unsigned short u16;
    struct {
      unsigned short off;
      unsigned short len;
      unsigned char aux;
    } ptr;
    struct {
      unsigned char len;
      char* bytes;
    } str;
    RSArray array;
    RSObject object;
  } as;
} RSValue;

void rs_value_init_false(RSValue* v);
void rs_value_init_true(RSValue* v);
void rs_value_init_bool(RSValue* v, int truthy);
void rs_value_init_u16(RSValue* v, unsigned short n);
int rs_value_init_string(RSValue* v, const char* s);
int rs_value_array_new(RSValue* v, unsigned short count);
int rs_value_array_from_u16_range(RSValue* v, unsigned short start, unsigned short end);
int rs_value_object_new(RSValue* v);
int rs_value_object_set(RSValue* v, const char* name, const RSValue* prop_value);
const RSValue* rs_value_object_get(const RSValue* v, const char* name);

void rs_value_free(RSValue* v);
int rs_value_clone(RSValue* out, const RSValue* in);
int rs_value_eq(const RSValue* a, const RSValue* b);
int rs_value_truthy(const RSValue* v);
int rs_value_to_u16(const RSValue* v, unsigned short* out);
int rs_value_is_string_like(const RSValue* v);
int rs_value_is_array_like(const RSValue* v);
int rs_value_is_object_like(const RSValue* v);
unsigned short rs_value_array_count(const RSValue* v);
unsigned short rs_value_object_count(const RSValue* v);
int rs_value_string_copy(const RSValue* v, char* out, unsigned short max);
int rs_value_array_get(const RSValue* v, unsigned short index, RSValue* out);
int rs_value_object_get_copy(const RSValue* v, const char* name, RSValue* out);
int rs_value_object_prop(const RSValue* v,
                         unsigned short index,
                         char* name_out,
                         unsigned short name_max,
                         RSValue* out_value);
void rs_value_heap_reset(void);
unsigned short rs_value_heap_next_free(void);

#endif
