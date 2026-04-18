#include <stdio.h>
#include <string.h>

#include "build_support/readyshell_host_fs.h"
#include "build_support/readyshell_reu_host.h"
#include "src/apps/readyshell/core/rs_errors.h"
#include "src/apps/readyshell/core/rs_serialize.h"
#include "src/apps/readyshell/core/rs_ui_state.h"
#include "src/apps/readyshell/core/rs_vm.h"
#include "src/apps/readyshell/platform/rs_platform.h"

#ifndef READYSHELL_VM_SMOKE_OVERLAY
#define READYSHELL_VM_SMOKE_OVERLAY 0
#endif

#define OUT_MAX_LINES 64
#define OUT_LINE_MAX  128

#ifndef __CC65__
void rs_overlay_debug_mark(unsigned char code) {
  (void)code;
}
#endif

typedef struct SmokeOut {
  char lines[OUT_MAX_LINES][OUT_LINE_MAX];
  unsigned char kinds[OUT_MAX_LINES];
  unsigned int count;
} SmokeOut;

typedef struct SmokeExpect {
  const char* line;
  unsigned char kind;
} SmokeExpect;

enum {
  SMOKE_LINE_RENDER = 0,
  SMOKE_LINE_PRT = 1
};

static int smoke_append(SmokeOut* out, const char* line, unsigned char kind) {
  if (!out || out->count >= OUT_MAX_LINES) {
    return -1;
  }
  if (!line) {
    line = "";
  }
  strncpy(out->lines[out->count], line, OUT_LINE_MAX - 1u);
  out->lines[out->count][OUT_LINE_MAX - 1u] = '\0';
  out->kinds[out->count] = kind;
  ++out->count;
  return 0;
}

static int smoke_writer(void* user, const char* line) {
  SmokeOut* out;
  unsigned char kind;
  out = (SmokeOut*)user;
  kind = (rs_vm_current_output_kind() == RS_VM_OUTPUT_PRT) ? SMOKE_LINE_PRT
                                                           : SMOKE_LINE_RENDER;
  return smoke_append(out, line, kind);
}

static void smoke_reset(SmokeOut* out) {
  unsigned int i;
  out->count = 0u;
  for (i = 0u; i < OUT_MAX_LINES; ++i) {
    out->lines[i][0] = '\0';
    out->kinds[i] = SMOKE_LINE_RENDER;
  }
}

static int smoke_exec(RSVM* vm, SmokeOut* out, const char* source) {
  RSError err;
  int rc;
  smoke_reset(out);
  rc = rs_vm_exec_source(vm, source, &err);
  if (rc != 0) {
    printf("ERR  src='%s' code=%d msg=%s line=%u col=%u\n",
           source,
           (int)err.code,
           err.message ? err.message : "<null>",
           (unsigned)err.line,
           (unsigned)err.column);
    return -1;
  }
  return 0;
}

static int smoke_expect_lines(const SmokeOut* out,
                              const char* source,
                              const SmokeExpect* expect,
                              unsigned int expect_count) {
  unsigned int i;
  int fail;
  fail = 0;
  if (out->count != expect_count) {
    printf("FAIL src='%s' line_count got=%u expected=%u\n",
           source,
           out->count,
           expect_count);
    fail = 1;
  }
  for (i = 0u; i < expect_count && i < out->count; ++i) {
    if (strcmp(out->lines[i], expect[i].line) != 0) {
      printf("FAIL src='%s' line[%u] got='%s' expected='%s'\n",
             source,
             i,
             out->lines[i],
             expect[i].line);
      fail = 1;
    }
    if (out->kinds[i] != expect[i].kind) {
      printf("FAIL src='%s' kind[%u] got=%u expected=%u\n",
             source,
             i,
             (unsigned)out->kinds[i],
             (unsigned)expect[i].kind);
      fail = 1;
    }
  }
  if (!fail) {
    printf("OK   src='%s' lines=%u\n", source, expect_count);
  }
  return fail;
}

static int smoke_run_expect(RSVM* vm,
                            SmokeOut* out,
                            const char* source,
                            const SmokeExpect* expect,
                            unsigned int expect_count) {
  if (smoke_exec(vm, out, source) != 0) {
    return 1;
  }
  return smoke_expect_lines(out, source, expect, expect_count);
}

static int smoke_run_expect_error(RSVM* vm, SmokeOut* out, const char* source) {
  RSError err;
  smoke_reset(out);
  if (rs_vm_exec_source(vm, source, &err) == 0) {
    printf("FAIL src='%s' expected error\n", source);
    return 1;
  }
  printf("OK   src='%s' error=%d\n", source, (int)err.code);
  return 0;
}

static int smoke_expect_true(const char* label, int cond) {
  if (!cond) {
    printf("FAIL %s\n", label);
    return 1;
  }
  printf("OK   %s\n", label);
  return 0;
}

static int smoke_expect_file_view(const char* label,
                                  unsigned char drive,
                                  const char* name,
                                  unsigned char expect_type,
                                  const unsigned char* expect_prefix,
                                  unsigned short expect_prefix_len) {
  RSHostFSView view;
  if (readyshell_host_fs_get_view(drive, name, &view) != 0) {
    printf("FAIL %s missing file %u:%s\n", label, (unsigned)drive, name);
    return 1;
  }
  if (view.type != expect_type) {
    printf("FAIL %s type got=%u expected=%u\n",
           label,
           (unsigned)view.type,
           (unsigned)expect_type);
    return 1;
  }
  if (view.len < expect_prefix_len ||
      memcmp(view.bytes, expect_prefix, expect_prefix_len) != 0) {
    printf("FAIL %s prefix mismatch\n", label);
    return 1;
  }
  printf("OK   %s len=%u\n", label, (unsigned)view.len);
  return 0;
}

#if !READYSHELL_VM_SMOKE_OVERLAY
static int smoke_seed_snapshot(unsigned char drive,
                               const char* name,
                               const RSValue* value) {
  unsigned char bytes[2048];
  unsigned short len;
  if (rs_serialize_file_payload(value, bytes, sizeof(bytes), &len) != 0) {
    return -1;
  }
  return readyshell_host_fs_store_bytes(drive,
                                        name,
                                        READYSHELL_HOST_FS_TYPE_BINARY,
                                        bytes,
                                        len);
}

static int smoke_build_dir_entry(RSValue* out,
                                 const char* name,
                                 unsigned short blocks,
                                 const char* type) {
  RSValue tmp;
  if (!out || rs_value_object_new(out) != 0) {
    return -1;
  }
  rs_value_init_false(&tmp);
  if (rs_value_init_string(&tmp, name) != 0 ||
      rs_value_object_set(out, "name", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out);
    return -1;
  }
  rs_value_free(&tmp);
  rs_value_init_u16(&tmp, blocks);
  if (rs_value_object_set(out, "blocks", &tmp) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_init_string(&tmp, type) != 0 ||
      rs_value_object_set(out, "type", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out);
    return -1;
  }
  rs_value_free(&tmp);
  return 0;
}

static int smoke_list_dir(void* user, unsigned char drive, RSValue* out_array) {
  static const char* names[] = { "alpha", "beta", "gamma" };
  static const unsigned short blocks[] = { 10u, 20u, 30u };
  static const char* types[] = { "PRG", "SEQ", "USR" };
  unsigned short i;
  (void)user;
  (void)drive;
  if (rs_value_array_new(out_array, 3u) != 0) {
    return -1;
  }
  for (i = 0u; i < 3u; ++i) {
    if (smoke_build_dir_entry(&out_array->as.array.items[i], names[i], blocks[i], types[i]) != 0) {
      rs_value_free(out_array);
      return -1;
    }
  }
  return 0;
}

static int smoke_drive_info(void* user, unsigned char drive, RSValue* out_obj) {
  RSValue tmp;
  (void)user;
  if (rs_value_object_new(out_obj) != 0) {
    return -1;
  }
  rs_value_init_false(&tmp);
  rs_value_init_u16(&tmp, drive);
  if (rs_value_object_set(out_obj, "drive", &tmp) != 0) {
    rs_value_free(out_obj);
    return -1;
  }
  if (rs_value_init_string(&tmp, "readyos") != 0 ||
      rs_value_object_set(out_obj, "diskname", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out_obj);
    return -1;
  }
  rs_value_free(&tmp);
  if (rs_value_init_string(&tmp, "ro") != 0 ||
      rs_value_object_set(out_obj, "id", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out_obj);
    return -1;
  }
  rs_value_free(&tmp);
  rs_value_init_u16(&tmp, 664u);
  if (rs_value_object_set(out_obj, "blocksfree", &tmp) != 0) {
    rs_value_free(out_obj);
    return -1;
  }
  return 0;
}

static int run_generic_suite(RSVM* vm, SmokeOut* out) {
  int fail;
  static const SmokeExpect array_literal[] = {
    { "[10,20,30]", SMOKE_LINE_PRT }
  };
  static const SmokeExpect nested_values[] = {
    { "HEAD", SMOKE_LINE_PRT },
    { "TAIL", SMOKE_LINE_PRT },
    { "5", SMOKE_LINE_PRT }
  };
  static const SmokeExpect expr_stage_lines[] = {
    { "FALSE", SMOKE_LINE_RENDER },
    { "TRUE", SMOKE_LINE_RENDER },
    { "TRUE", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect filter_lines[] = {
    { "3", SMOKE_LINE_RENDER },
    { "4", SMOKE_LINE_RENDER },
    { "5", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect foreach_lines[] = {
    { "ITEM 1", SMOKE_LINE_PRT },
    { "1", SMOKE_LINE_RENDER },
    { "ITEM 2", SMOKE_LINE_PRT },
    { "2", SMOKE_LINE_RENDER },
    { "ITEM 3", SMOKE_LINE_PRT },
    { "3", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect auto_print_lines[] = {
    { "1", SMOKE_LINE_RENDER },
    { "2", SMOKE_LINE_RENDER },
    { "3", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect prt_only_lines[] = {
    { "1", SMOKE_LINE_PRT },
    { "2", SMOKE_LINE_PRT },
    { "3", SMOKE_LINE_PRT }
  };
  static const SmokeExpect capture_shapes[] = {
    { "FALSE", SMOKE_LINE_PRT },
    { "1", SMOKE_LINE_PRT },
    { "3", SMOKE_LINE_PRT }
  };
  static const SmokeExpect ci_match_lines[] = {
    { "yo", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect top_skip_lines[] = {
    { "3", SMOKE_LINE_RENDER },
    { "4", SMOKE_LINE_RENDER },
    { "5", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect sel_name_lines[] = {
    { "alpha", SMOKE_LINE_RENDER },
    { "beta", SMOKE_LINE_RENDER },
    { "gamma", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect sel_scalar_and_object[] = {
    { "alpha", SMOKE_LINE_PRT },
    { "alpha", SMOKE_LINE_PRT },
    { "PRG", SMOKE_LINE_PRT },
    { "SEQ", SMOKE_LINE_PRT }
  };
  static const SmokeExpect missing_property_lines[] = {
    { "beta", SMOKE_LINE_PRT },
    { "FALSE", SMOKE_LINE_PRT }
  };
  static const SmokeExpect drvi_lines[] = {
    { "{DRIVE:8,DISKNAME:readyos}", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect more_lines[] = {
    { "1", SMOKE_LINE_RENDER },
    { "2", SMOKE_LINE_RENDER },
    { "3", SMOKE_LINE_RENDER },
    { "4", SMOKE_LINE_RENDER },
    { "5", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect scalar_roundtrip[] = {
    { "TRUE", SMOKE_LINE_PRT },
    { "42", SMOKE_LINE_PRT }
  };
  static const SmokeExpect array_roundtrip[] = {
    { "TRUE", SMOKE_LINE_PRT },
    { "3", SMOKE_LINE_PRT }
  };
  static const SmokeExpect object_roundtrip[] = {
    { "alpha", SMOKE_LINE_PRT },
    { "PRG", SMOKE_LINE_PRT }
  };
  static const SmokeExpect missing_ldv[] = {
    { "FALSE", SMOKE_LINE_PRT }
  };
  static const unsigned char scalar_prefix[] = {
    'R', 'S', 'V', '1', 0x03u, 0x00u, 0x09u, 0x2Au, 0x00u
  };
  RSValue dir_array;
  RSValue row;
  RSValue tmp;
  RSHostFSView view;

  fail = 0;
  fail |= smoke_run_expect(vm, out, "$A = 10,20,30; PRT $A", array_literal, 1u);
  fail |= smoke_run_expect(vm,
                           out,
                           "$A = 1..5; $B = \"HEAD\",\"TAIL\",$A; PRT $B(0); PRT $B(1); PRT $B(2)(4)",
                           nested_values,
                           3u);
  fail |= smoke_run_expect(vm, out, "GEN 3 | @ > 1", expr_stage_lines, 3u);
  fail |= smoke_run_expect(vm, out, "GEN 5 | ?[ $LAST = @; @ >= 3 ]", filter_lines, 3u);
  fail |= smoke_run_expect(vm,
                           out,
                           "1..3 | %[ PRT \"ITEM \", @; @ ]",
                           foreach_lines,
                           6u);
  fail |= smoke_run_expect(vm, out, "GEN 3", auto_print_lines, 3u);
  fail |= smoke_run_expect(vm, out, "GEN 3 | PRT @", prt_only_lines, 3u);
  fail |= smoke_run_expect(vm,
                           out,
                           "$NONE = GEN 5 | ?[ FALSE ]; PRT $NONE; $ONE = GEN 5 | TOP 1; PRT $ONE; $MANY = GEN 3 | @; PRT $MANY(2)",
                           capture_shapes,
                           3u);
  fail |= smoke_run_expect(vm, out, "$T = \"yo\"; $T | ?[ @ == \"YO\" ]", ci_match_lines, 1u);
  fail |= smoke_run_expect(vm, out, "1..10 | TOP 3,2", top_skip_lines, 3u);
  fail |= smoke_run_expect(vm, out, "LST | SEL \"NAME\"", sel_name_lines, 3u);
  fail |= smoke_run_expect(vm,
                           out,
                           "$NAME = LST | TOP 1 | SEL \"NAME\"; PRT $NAME; $ROW = LST | TOP 1 | SEL \"NAME\",\"TYPE\"; PRT $ROW.NAME; PRT $ROW.TYPE; $ROWS = LST | TOP 2 | SEL \"NAME\",\"TYPE\"; PRT $ROWS(1).TYPE",
                           sel_scalar_and_object,
                           4u);
  fail |= smoke_run_expect(vm,
                           out,
                           "$DIR = LST; PRT $DIR(1).NAME; PRT $DIR(1).MISSING",
                           missing_property_lines,
                           2u);
  fail |= smoke_run_expect(vm, out, "DRVI | SEL \"DRIVE\",\"DISKNAME\"", drvi_lines, 1u);

  fail |= smoke_run_expect(vm, out, "1..5 | MORE", more_lines, 5u);
  fail |= smoke_run_expect_error(vm, out, "1..3 | MORE 0");
  fail |= smoke_run_expect_error(vm, out, "1..3 | MORE \"X\"");
  fail |= smoke_run_expect_error(vm, out, "1..3 | TOP");
  fail |= smoke_run_expect_error(vm, out, "LST | SEL 1");

  fail |= smoke_run_expect(vm,
                           out,
                           "$OK = STV 42, \"answer\"; PRT $OK; $ANSWER = LDV \"answer\"; PRT $ANSWER",
                           scalar_roundtrip,
                           2u);
  fail |= smoke_expect_file_view("generic stv scalar bytes",
                                 8u,
                                 "answer",
                                 READYSHELL_HOST_FS_TYPE_BINARY,
                                 scalar_prefix,
                                 sizeof(scalar_prefix));

  fail |= smoke_run_expect(vm,
                           out,
                           "$LIST = 1,2,3; $OK = STV $LIST, \"9:list\"; PRT $OK; $BACK = LDV \"9:list\"; PRT $BACK(2)",
                           array_roundtrip,
                           2u);
  fail |= smoke_expect_true("generic stv array tag",
                            readyshell_host_fs_get_view(9u, "list", &view) == 0 &&
                            view.type == READYSHELL_HOST_FS_TYPE_BINARY &&
                            view.len >= 7u &&
                            view.bytes[6] == (unsigned char)RS_VAL_ARRAY);

  rs_value_init_false(&dir_array);
  rs_value_init_false(&row);
  rs_value_init_false(&tmp);
  if (smoke_list_dir(0, 8u, &dir_array) != 0 ||
      rs_value_array_get(&dir_array, 0u, &row) != 0 ||
      smoke_seed_snapshot(8u, "row", &row) != 0) {
    fail |= smoke_expect_true("seed object snapshot", 0);
  } else {
    fail |= smoke_run_expect(vm,
                             out,
                             "$ROW = LDV \"row\"; PRT $ROW.NAME; PRT $ROW.TYPE",
                             object_roundtrip,
                             2u);
    fail |= smoke_expect_true("generic stv object tag",
                              readyshell_host_fs_get_view(8u, "row", &view) == 0 &&
                              view.type == READYSHELL_HOST_FS_TYPE_BINARY &&
                              view.len >= 7u &&
                              view.bytes[6] == (unsigned char)RS_VAL_OBJECT);
  }
  rs_value_free(&tmp);
  rs_value_free(&row);
  rs_value_free(&dir_array);

  fail |= smoke_run_expect(vm,
                           out,
                           "$MISS = LDV \"doesnotexist\"; PRT $MISS",
                           missing_ldv,
                           1u);
  fail |= smoke_expect_true("seed bad payload",
                            readyshell_host_fs_store_bytes(8u,
                                                           "bad",
                                                           READYSHELL_HOST_FS_TYPE_BINARY,
                                                           (const unsigned char*)"RSV1\x01\x00\x09",
                                                           7u) == 0);
  fail |= smoke_run_expect_error(vm, out, "LDV \"bad\"");

  return fail;
}
#else
static int run_overlay_suite(RSVM* vm, SmokeOut* out) {
  int fail;
  static const SmokeExpect drvi_lines[] = {
    { "{DRIVE:8,DISKNAME:readyos}", SMOKE_LINE_RENDER },
    { "9", SMOKE_LINE_PRT }
  };
  static const SmokeExpect lst_filter_lines[] = {
    { "todo", SMOKE_LINE_RENDER },
    { "temp", SMOKE_LINE_RENDER },
    { "ta", SMOKE_LINE_RENDER },
    { "{NAME:ta,TYPE:REL}", SMOKE_LINE_RENDER },
    { "{NAME:todo,TYPE:PRG}", SMOKE_LINE_RENDER },
    { "{NAME:temp,TYPE:SEQ}", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect lst_data_lines[] = {
    { "todo", SMOKE_LINE_PRT },
    { "temp", SMOKE_LINE_PRT }
  };
  static const SmokeExpect cat_and_put_lines[] = {
    { "HELLO", SMOKE_LINE_RENDER },
    { "WORLD", SMOKE_LINE_RENDER },
    { "HI", SMOKE_LINE_RENDER },
    { "A", SMOKE_LINE_RENDER },
    { "B", SMOKE_LINE_RENDER },
    { "A", SMOKE_LINE_RENDER },
    { "B", SMOKE_LINE_RENDER },
    { "C", SMOKE_LINE_RENDER },
    { "D", SMOKE_LINE_RENDER },
    { "ONE", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect del_ren_copy_lines[] = {
    { "TRUE", SMOKE_LINE_RENDER },
    { "FALSE", SMOKE_LINE_RENDER },
    { "TRUE", SMOKE_LINE_RENDER },
    { "A", SMOKE_LINE_RENDER },
    { "B", SMOKE_LINE_RENDER },
    { "C", SMOKE_LINE_RENDER },
    { "D", SMOKE_LINE_RENDER },
    { "TRUE", SMOKE_LINE_RENDER },
    { "A", SMOKE_LINE_RENDER },
    { "B", SMOKE_LINE_RENDER },
    { "C", SMOKE_LINE_RENDER },
    { "D", SMOKE_LINE_RENDER },
    { "TRUE", SMOKE_LINE_RENDER },
    { "A", SMOKE_LINE_RENDER },
    { "B", SMOKE_LINE_RENDER },
    { "C", SMOKE_LINE_RENDER },
    { "D", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect stv_ldv_lines[] = {
    { "TRUE", SMOKE_LINE_PRT },
    { "42", SMOKE_LINE_PRT },
    { "alpha", SMOKE_LINE_RENDER },
    { "todo", SMOKE_LINE_RENDER },
    { "FALSE", SMOKE_LINE_PRT }
  };
  static const unsigned char answer_prefix[] = {
    'R', 'S', 'V', '1', 0x03u, 0x00u, 0x09u, 0x2Au, 0x00u
  };
  static const char* notes_lines[] = { "HELLO", "WORLD" };

  fail = 0;
  fail |= smoke_expect_true("seed overlay notes",
                            readyshell_host_fs_write_seq_lines(8u,
                                                               "notes",
                                                               notes_lines,
                                                               2u,
                                                               0) == 0);

  fail |= smoke_run_expect(vm,
                           out,
                           "DRVI | SEL \"DRIVE\",\"DISKNAME\"; $I = DRVI 9; PRT $I.DRIVE",
                           drvi_lines,
                           2u);
  fail |= smoke_run_expect_error(vm, out, "DRVI 10");

  fail |= smoke_run_expect(vm,
                           out,
                           "LST \"t*\" | SEL \"NAME\"; LST \"t?\" | SEL \"NAME\",\"TYPE\"; LST \"t*\", 9, \"PRG,SEQ\" | SEL \"NAME\",\"TYPE\"",
                           lst_filter_lines,
                           6u);
  fail |= smoke_run_expect(vm,
                           out,
                           "$PRGS = LST | ?[ @.TYPE == \"PRG\" ]; PRT $PRGS(1).NAME; $NAMES = LST \"9:t*\" | TOP 2 | SEL \"NAME\"; PRT $NAMES(1)",
                           lst_data_lines,
                           2u);

  fail |= smoke_run_expect(vm,
                           out,
                           "CAT \"notes\"; PUT \"HI\", \"notes\"; CAT \"notes\"; $LINES = \"A\",\"B\"; PUT $LINES, \"dirnames\"; CAT \"dirnames\"; $MORE = \"C\",\"D\"; ADD $MORE, \"dirnames\"; CAT \"dirnames\"; ADD \"ONE\", \"newnotes\"; CAT \"newnotes\"",
                           cat_and_put_lines,
                           10u);
  fail |= smoke_run_expect(vm,
                           out,
                           "DEL \"notes\"; DEL \"notes\"; REN \"dirnames\", \"dirnames.old\"; CAT \"dirnames.old\"; COPY \"dirnames.old\", \"dirnames.bak\"; CAT \"dirnames.bak\"; COPY \"8:dirnames.old\", \"9:dirnames.copy\"; CAT \"9:dirnames.copy\"",
                           del_ren_copy_lines,
                           17u);
  fail |= smoke_run_expect(vm,
                           out,
                           "COPY \"dirnames.old\", 9; CAT \"9:dirnames.old\"",
                           (const SmokeExpect[]){
                             { "TRUE", SMOKE_LINE_RENDER },
                             { "A", SMOKE_LINE_RENDER },
                             { "B", SMOKE_LINE_RENDER },
                             { "C", SMOKE_LINE_RENDER },
                             { "D", SMOKE_LINE_RENDER }
                           },
                           5u);

  fail |= smoke_run_expect(vm,
                           out,
                           "$OK = STV 42, \"answer\", 9; PRT $OK; $ANSWER = LDV \"answer\", 9; PRT $ANSWER; $DIR = LST | ?[ @.TYPE == \"PRG\" ] | SEL \"NAME\",\"TYPE\"; $OK2 = STV $DIR, \"9:prgdir\"; LDV \"9:prgdir\" | SEL \"NAME\"; $MISS = LDV \"missing\"; PRT $MISS",
                           stv_ldv_lines,
                           5u);
  fail |= smoke_expect_file_view("overlay stv scalar bytes",
                                 9u,
                                 "answer",
                                 READYSHELL_HOST_FS_TYPE_BINARY,
                                 answer_prefix,
                                 sizeof(answer_prefix));
  fail |= smoke_expect_true("seed overlay bad payload",
                            readyshell_host_fs_store_bytes(8u,
                                                           "bad",
                                                           READYSHELL_HOST_FS_TYPE_BINARY,
                                                           (const unsigned char*)"RSV1\x01\x00\x09",
                                                           7u) == 0);
  fail |= smoke_run_expect_error(vm, out, "LDV \"bad\"");
  fail |= smoke_run_expect_error(vm, out, "ADD 42, \"oops\"");

  return fail;
}
#endif

static void smoke_pause_flag_clear(void) {
  unsigned char flags;
  flags = 0u;
  (void)rs_reu_write(RS_REU_UI_FLAGS_OFF, &flags, 1u);
}

static int smoke_expect_pause_flag(const char* label, int expect_set) {
  unsigned char flags;
  flags = 0u;
  if (rs_reu_read(RS_REU_UI_FLAGS_OFF, &flags, 1u) != 0) {
    printf("FAIL %s pause unreadable\n", label);
    return 1;
  }
  if (((flags & RS_UI_FLAG_PAUSED) != 0u) != expect_set) {
    printf("FAIL %s pause got=%u expected=%u\n",
           label,
           (unsigned)((flags & RS_UI_FLAG_PAUSED) != 0u),
           (unsigned)expect_set);
    return 1;
  }
  printf("OK   %s pause=%u\n", label, (unsigned)expect_set);
  return 0;
}

int main(void) {
  RSVM vm;
  RSVMPlatform platform;
  SmokeOut out;
  int fail;

  fail = 0;
  readyshell_reu_host_reset();
  readyshell_host_fs_reset();
  smoke_reset(&out);
  rs_vm_init(&vm);
  memset(&platform, 0, sizeof(platform));
#if !READYSHELL_VM_SMOKE_OVERLAY
  platform.list_dir = smoke_list_dir;
  platform.drive_info = smoke_drive_info;
  platform.file_read = readyshell_host_fs_snapshot_read_cb;
  platform.file_write = readyshell_host_fs_snapshot_write_cb;
#endif
  rs_vm_set_platform(&vm, &platform);
  rs_vm_set_writer(&vm, smoke_writer, &out);

#if !READYSHELL_VM_SMOKE_OVERLAY
  fail |= run_generic_suite(&vm, &out);
#else
  fail |= run_overlay_suite(&vm, &out);
#endif

  smoke_pause_flag_clear();
  fail |= smoke_run_expect(&vm,
                           &out,
                           "1..21 | MORE | TOP 1",
                           (const SmokeExpect[]){{ "1", SMOKE_LINE_RENDER }},
                           1u);
  fail |= smoke_expect_pause_flag("more pause", 1);

  rs_vm_free(&vm);
  return fail;
}
