#include <stdio.h>
#include <string.h>

#include "build_support/readyshell_reu_host.h"
#include "src/apps/readyshellpoc/core/rs_cmd_ldv_local.h"
#include "src/apps/readyshellpoc/core/rs_format.h"
#include "src/apps/readyshellpoc/core/rs_serialize.h"
#include "src/apps/readyshellpoc/core/rs_value.h"

static int expect_true(const char* label, int cond) {
  if (!cond) {
    printf("FAIL %s\n", label);
    return 1;
  }
  printf("OK   %s\n", label);
  return 0;
}

static int expect_string_value(const char* label, const RSValue* v, const char* expect) {
  char buf[256];
  if (rs_value_string_copy(v, buf, sizeof(buf)) != 0 || strcmp(buf, expect) != 0) {
    printf("FAIL %s got='%s' expected='%s'\n", label, buf, expect);
    return 1;
  }
  printf("OK   %s -> %s\n", label, expect);
  return 0;
}

int main(void) {
  RSValue str;
  RSValue stored_str;
  RSValue array;
  RSValue stored_array;
  RSValue tmp;
  RSValue obj;
  RSValue stored_obj;
  RSValue roundtrip;
  RSValue loaded;
  unsigned char bytes[2048];
  unsigned short used;
  unsigned short payload_used;
  char fmt[256];
  int fail;

  fail = 0;
  readyshell_reu_host_reset();
  rs_value_heap_reset();

  fail |= expect_true("heap reset", rs_value_heap_next_free() == 0x8100u);

  rs_value_init_false(&str);
  rs_value_init_false(&stored_str);
  fail |= expect_true("init string", rs_value_init_string(&str, "hello") == 0);
  fail |= expect_true("clone string to reu", rs_value_clone(&stored_str, &str) == 0);
  fail |= expect_true("stored string tag", stored_str.tag == RS_VAL_STR_PTR);
  fail |= expect_string_value("stored string copy", &stored_str, "hello");
  rs_value_free(&str);

  rs_value_init_false(&array);
  rs_value_init_false(&stored_array);
  rs_value_init_false(&tmp);
  fail |= expect_true("array new", rs_value_array_new(&array, 3u) == 0);
  rs_value_init_u16(&array.as.array.items[0], 7u);
  fail |= expect_true("array str", rs_value_init_string(&array.as.array.items[1], "beta") == 0);
  rs_value_init_u16(&array.as.array.items[2], 9u);
  fail |= expect_true("clone array to reu", rs_value_clone(&stored_array, &array) == 0);
  fail |= expect_true("stored array tag", stored_array.tag == RS_VAL_ARRAY_PTR);
  fail |= expect_true("array count", rs_value_array_count(&stored_array) == 3u);
  fail |= expect_true("array get 0", rs_value_array_get(&stored_array, 0u, &tmp) == 0 && tmp.tag == RS_VAL_U16 && tmp.as.u16 == 7u);
  rs_value_free(&tmp);
  fail |= expect_true("array get 1", rs_value_array_get(&stored_array, 1u, &tmp) == 0);
  fail |= expect_string_value("array get string", &tmp, "beta");
  rs_value_free(&tmp);
  fail |= expect_true("serialize array payload",
                      rs_serialize_file_payload(&stored_array, bytes, sizeof(bytes), &used) == 0);
  rs_value_init_false(&loaded);
  fail |= expect_true("write array payload to scratch",
                      rs_reu_write(RS_CMD_SCRATCH_OFF + 6ul,
                                   bytes + 6u,
                                   (unsigned short)(used - 6u)) == 0);
  fail |= expect_true("overlay ldv array loader",
                      rs_cmd_load_rsv1_value_to_heap(RS_CMD_SCRATCH_OFF + 6ul,
                                                     (unsigned short)(used - 6u),
                                                     &loaded) == 0);
  fail |= expect_true("overlay ldv array tag", loaded.tag == RS_VAL_ARRAY_PTR);
  fail |= expect_true("overlay ldv array get", rs_value_array_get(&loaded, 1u, &tmp) == 0);
  fail |= expect_string_value("overlay ldv array string", &tmp, "beta");
  rs_value_free(&tmp);
  rs_value_free(&loaded);
  rs_value_free(&array);

  rs_value_init_false(&obj);
  rs_value_init_false(&stored_obj);
  fail |= expect_true("object new", rs_value_object_new(&obj) == 0);
  fail |= expect_true("object name", rs_value_init_string(&tmp, "gamma") == 0);
  fail |= expect_true("object set name", rs_value_object_set(&obj, "name", &tmp) == 0);
  rs_value_free(&tmp);
  rs_value_init_u16(&tmp, 30u);
  fail |= expect_true("object set blocks", rs_value_object_set(&obj, "blocks", &tmp) == 0);
  rs_value_free(&tmp);
  fail |= expect_true("clone object to reu", rs_value_clone(&stored_obj, &obj) == 0);
  fail |= expect_true("stored object tag", stored_obj.tag == RS_VAL_OBJECT_PTR);
  fail |= expect_true("object get name", rs_value_object_get_copy(&stored_obj, "NAME", &tmp) == 0);
  fail |= expect_string_value("object get name value", &tmp, "gamma");
  rs_value_free(&tmp);
  fail |= expect_true("format object", rs_format_value(&stored_obj, fmt, sizeof(fmt)) > 0 && strcmp(fmt, "{name:gamma,blocks:30}") == 0);
  fail |= expect_true("serialize file payload", rs_serialize_file_payload(&stored_obj, bytes, sizeof(bytes), &used) == 0);
  rs_value_init_false(&roundtrip);
  payload_used = 0u;
  fail |= expect_true("deserialize file payload",
                      rs_deserialize_value(bytes + 6u,
                                           (unsigned short)(used - 6u),
                                           &roundtrip,
                                           &payload_used) == 0);
  fail |= expect_true("roundtrip equality", rs_value_eq(&stored_obj, &roundtrip));
  rs_value_init_false(&loaded);
  fail |= expect_true("write payload to scratch",
                      rs_reu_write(RS_CMD_SCRATCH_OFF + 6ul,
                                   bytes + 6u,
                                   (unsigned short)(used - 6u)) == 0);
  fail |= expect_true("overlay ldv loader",
                      rs_cmd_load_rsv1_value_to_heap(RS_CMD_SCRATCH_OFF + 6ul,
                                                     (unsigned short)(used - 6u),
                                                     &loaded) == 0);
  fail |= expect_true("overlay ldv equality", rs_value_eq(&stored_obj, &loaded));
  fail |= expect_true("overlay ldv prop", rs_value_object_get_copy(&loaded, "blocks", &tmp) == 0 &&
                                           tmp.tag == RS_VAL_U16 &&
                                           tmp.as.u16 == 30u);
  rs_value_free(&tmp);

  rs_value_free(&str);
  rs_value_free(&stored_str);
  rs_value_free(&array);
  rs_value_free(&stored_array);
  rs_value_free(&tmp);
  rs_value_free(&obj);
  rs_value_free(&stored_obj);
  rs_value_free(&roundtrip);
  rs_value_free(&loaded);
  return fail;
}
