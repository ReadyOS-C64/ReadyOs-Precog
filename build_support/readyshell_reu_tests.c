#include <stdio.h>
#include <string.h>

#include "build_support/readyshell_reu_host.h"
#include "src/apps/readyshell/core/rs_cmd_drive_local.h"
#include "src/apps/readyshell/core/rs_cmd_ldv_local.h"
#include "src/apps/readyshell/core/rs_format.h"
#include "src/apps/readyshell/core/rs_serialize.h"
#include "src/apps/readyshell/core/rs_value.h"

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

static int expect_bytes(const char* label,
                        const unsigned char* got,
                        unsigned short got_len,
                        const unsigned char* expect,
                        unsigned short expect_len) {
  if (got_len != expect_len || memcmp(got, expect, expect_len) != 0) {
    printf("FAIL %s len=%u expected=%u\n",
           label,
           (unsigned)got_len,
           (unsigned)expect_len);
    return 1;
  }
  printf("OK   %s len=%u\n", label, (unsigned)got_len);
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
  RSValue nested;
  RSValue stored_nested;
  RSValue nested_meta;
  RSValue nested_items;
  unsigned char bytes[2048];
  unsigned short used;
  unsigned short payload_used;
  unsigned char drive;
  const char* name;
  char fmt[256];
  int fail;
  static const unsigned char expect_array_file[] = {
    'R', 'S', 'V', '1', 0x0Fu, 0x00u,
    0x06u, 0x03u, 0x00u,
    0x09u, 0x07u, 0x00u,
    0x02u, 0x04u, 'b', 'e', 't', 'a',
    0x09u, 0x09u, 0x00u
  };
  static const unsigned char expect_object_file[] = {
    'R', 'S', 'V', '1', 0x1Au, 0x00u,
    0x08u, 0x02u,
    0x02u, 0x04u, 'n', 'a', 'm', 'e',
    0x02u, 0x05u, 'g', 'a', 'm', 'm', 'a',
    0x02u, 0x06u, 'b', 'l', 'o', 'c', 'k', 's',
    0x09u, 0x1Eu, 0x00u
  };
  static const unsigned char bad_magic[] = {
    'B', 'A', 'D', '1', 0x03u, 0x00u, 0x09u, 0x2Au, 0x00u
  };
  static const unsigned char bad_truncated[] = {
    'R', 'S', 'V', '1', 0x03u, 0x00u, 0x09u, 0x2Au
  };
  static const unsigned char bad_len[] = {
    'R', 'S', 'V', '1', 0x04u, 0x00u, 0x09u, 0x2Au, 0x00u
  };
  static const unsigned char bad_nested[] = {
    'R', 'S', 'V', '1', 0x02u, 0x00u, 0x06u, 0x01u
  };

  fail = 0;
  readyshell_reu_host_reset();
  rs_value_heap_reset();

  fail |= expect_true("drive parse default",
                      rs_cmd_drive_parse_prefix("answer", 8u, &drive, &name) == 0 &&
                      drive == 8u &&
                      strcmp(name, "answer") == 0);
  fail |= expect_true("drive parse embedded unit",
                      rs_cmd_drive_parse_prefix("9:answer", 8u, &drive, &name) == 0 &&
                      drive == 9u &&
                      strcmp(name, "answer") == 0);
  fail |= expect_true("drive parse keeps raw open spec",
                      rs_cmd_drive_parse_prefix("0:answer,s,r", 8u, &drive, &name) == 0 &&
                      drive == 8u &&
                      strcmp(name, "0:answer,s,r") == 0);
  fail |= expect_true("drive parse fallback unit",
                      rs_cmd_drive_parse_prefix("answer", 9u, &drive, &name) == 0 &&
                      drive == 9u &&
                      strcmp(name, "answer") == 0);
  fail |= expect_true("drive char helper",
                      rs_cmd_drive_has_char("0:answer,s,r", ',') &&
                      !rs_cmd_drive_has_char("answer", ','));
  fail |= expect_true("drive canonicalize default",
                      rs_cmd_drive_canonicalize_path("answer", 8u, fmt, sizeof(fmt)) == 0 &&
                      strcmp(fmt, "answer") == 0);
  fail |= expect_true("drive canonicalize fallback",
                      rs_cmd_drive_canonicalize_path("answer", 9u, fmt, sizeof(fmt)) == 0 &&
                      strcmp(fmt, "9:answer") == 0);
  fail |= expect_true("drive canonicalize raw open spec",
                      rs_cmd_drive_canonicalize_path("0:answer,s,w", 9u, fmt, sizeof(fmt)) == 0 &&
                      strcmp(fmt, "9:0:answer,s,w") == 0);
  fail |= expect_true("drive canonicalize embedded wins",
                      rs_cmd_drive_canonicalize_path("10:answer", 9u, fmt, sizeof(fmt)) == 0 &&
                      strcmp(fmt, "10:answer") == 0);

  fail |= expect_true("heap reset", rs_value_heap_next_free() == 0x8100u);

  rs_value_init_false(&str);
  rs_value_init_false(&stored_str);
  fail |= expect_true("init string", rs_value_init_string(&str, "hello") == 0);
  fail |= expect_true("clone string to reu", rs_value_clone(&stored_str, &str) == 0);
  fail |= expect_true("stored string tag", stored_str.tag == RS_VAL_STR_PTR);
  fail |= expect_string_value("stored string copy", &stored_str, "hello");
  rs_value_free(&str);

  fail |= expect_true("init empty string", rs_value_init_string(&str, "") == 0);
  fail |= expect_true("clone empty string to reu", rs_value_clone(&stored_str, &str) == 0);
  fail |= expect_true("stored empty string tag", stored_str.tag == RS_VAL_STR_PTR);
  fail |= expect_string_value("stored empty string copy", &stored_str, "");
  rs_value_free(&str);
  rs_value_free(&stored_str);

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
  fail |= expect_bytes("serialize array bytes",
                       bytes,
                       used,
                       expect_array_file,
                       (unsigned short)sizeof(expect_array_file));
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
  fail |= expect_bytes("serialize object bytes",
                       bytes,
                       used,
                       expect_object_file,
                       (unsigned short)sizeof(expect_object_file));
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

  rs_value_init_false(&nested);
  rs_value_init_false(&stored_nested);
  rs_value_init_false(&nested_meta);
  rs_value_init_false(&nested_items);
  fail |= expect_true("nested object new", rs_value_object_new(&nested) == 0);
  fail |= expect_true("nested kind", rs_value_init_string(&tmp, "SNAP") == 0);
  fail |= expect_true("nested set kind", rs_value_object_set(&nested, "kind", &tmp) == 0);
  rs_value_free(&tmp);
  fail |= expect_true("nested items array", rs_value_array_new(&nested_items, 2u) == 0);
  fail |= expect_true("nested item a", rs_value_init_string(&nested_items.as.array.items[0], "A") == 0);
  fail |= expect_true("nested item b", rs_value_init_string(&nested_items.as.array.items[1], "B") == 0);
  fail |= expect_true("nested set items", rs_value_object_set(&nested, "items", &nested_items) == 0);
  fail |= expect_true("nested meta object", rs_value_object_new(&nested_meta) == 0);
  rs_value_init_u16(&tmp, 2u);
  fail |= expect_true("nested meta count", rs_value_object_set(&nested_meta, "count", &tmp) == 0);
  rs_value_init_true(&tmp);
  fail |= expect_true("nested meta ok", rs_value_object_set(&nested_meta, "ok", &tmp) == 0);
  fail |= expect_true("nested set meta", rs_value_object_set(&nested, "meta", &nested_meta) == 0);
  fail |= expect_true("clone nested to reu", rs_value_clone(&stored_nested, &nested) == 0);
  fail |= expect_true("nested serialize", rs_serialize_file_payload(&stored_nested, bytes, sizeof(bytes), &used) == 0);
  fail |= expect_true("nested root tag", used > 6u && bytes[6] == (unsigned char)RS_VAL_OBJECT);
  rs_value_init_false(&roundtrip);
  fail |= expect_true("nested deserialize",
                      rs_deserialize_file_payload(bytes, used, &roundtrip) == 0);
  fail |= expect_true("nested equality", rs_value_eq(&stored_nested, &roundtrip));
  rs_value_free(&loaded);
  rs_value_init_false(&loaded);
  fail |= expect_true("nested write payload to scratch",
                      rs_reu_write(RS_CMD_SCRATCH_OFF + 6ul,
                                   bytes + 6u,
                                   (unsigned short)(used - 6u)) == 0);
  fail |= expect_true("nested overlay ldv loader",
                      rs_cmd_load_rsv1_value_to_heap(RS_CMD_SCRATCH_OFF + 6ul,
                                                     (unsigned short)(used - 6u),
                                                     &loaded) == 0);
  fail |= expect_true("nested overlay equality", rs_value_eq(&stored_nested, &loaded));
  fail |= expect_true("reject bad magic",
                      rs_deserialize_file_payload(bad_magic,
                                                  (unsigned short)sizeof(bad_magic),
                                                  &roundtrip) != 0);
  fail |= expect_true("reject truncated payload",
                      rs_deserialize_file_payload(bad_truncated,
                                                  (unsigned short)sizeof(bad_truncated),
                                                  &roundtrip) != 0);
  fail |= expect_true("reject payload length mismatch",
                      rs_deserialize_file_payload(bad_len,
                                                  (unsigned short)sizeof(bad_len),
                                                  &roundtrip) != 0);
  fail |= expect_true("reject malformed nested payload",
                      rs_deserialize_file_payload(bad_nested,
                                                  (unsigned short)sizeof(bad_nested),
                                                  &roundtrip) != 0);

  rs_value_free(&str);
  rs_value_free(&stored_str);
  rs_value_free(&array);
  rs_value_free(&stored_array);
  rs_value_free(&tmp);
  rs_value_free(&obj);
  rs_value_free(&stored_obj);
  rs_value_free(&roundtrip);
  rs_value_free(&loaded);
  rs_value_free(&nested);
  rs_value_free(&stored_nested);
  rs_value_free(&nested_meta);
  rs_value_free(&nested_items);
  return fail;
}
