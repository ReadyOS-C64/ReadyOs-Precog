#include <stdio.h>
#include <string.h>

#include "build_support/readyshell_reu_host.h"
#include "src/apps/readyshellpoc/core/rs_errors.h"
#include "src/apps/readyshellpoc/core/rs_vm.h"

#define OUT_MAX_LINES 32
#define OUT_LINE_MAX 96

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
  SmokeOut* out = (SmokeOut*)user;
  unsigned char kind = (rs_vm_current_output_kind() == RS_VM_OUTPUT_PRT)
                         ? SMOKE_LINE_PRT
                         : SMOKE_LINE_RENDER;
  return smoke_append(out, line, kind);
}

static int smoke_build_dir_entry(RSValue* out,
                                 const char* name,
                                 unsigned short blocks,
                                 const char* type) {
  RSValue tmp;
  if (!out) {
    return -1;
  }
  if (rs_value_object_new(out) != 0) {
    return -1;
  }
  rs_value_init_false(&tmp);
  if (rs_value_init_string(&tmp, name) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_object_set(out, "name", &tmp) != 0) {
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
  if (rs_value_init_string(&tmp, type) != 0) {
    rs_value_free(out);
    return -1;
  }
  if (rs_value_object_set(out, "type", &tmp) != 0) {
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
  for (i = 0; i < 3u; ++i) {
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
  if (rs_value_init_string(&tmp, "disk") != 0) {
    rs_value_free(out_obj);
    return -1;
  }
  if (rs_value_object_set(out_obj, "diskname", &tmp) != 0) {
    rs_value_free(&tmp);
    rs_value_free(out_obj);
    return -1;
  }
  rs_value_free(&tmp);
  return 0;
}

static void smoke_reset(SmokeOut* out) {
  unsigned int i;
  out->count = 0;
  for (i = 0; i < OUT_MAX_LINES; ++i) {
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

  for (i = 0; i < expect_count && i < out->count; ++i) {
    if (strcmp(out->lines[i], expect[i].line) != 0) {
      printf("FAIL src='%s' line[%u] got='%s' expected='%s'\n",
             source,
             i,
             out->lines[i],
             expect[i].line);
      fail = 1;
    }
    if (out->kinds[i] != expect[i].kind) {
      printf("FAIL src='%s' kind[%u] got=%d expected=%d\n",
             source,
             i,
             (int)out->kinds[i],
             (int)expect[i].kind);
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

int main(void) {
  RSVM vm;
  RSVMPlatform platform;
  SmokeOut out;
  int fail;
  static const SmokeExpect filtered_print[] = {
    { "hey 6", SMOKE_LINE_PRT },
    { "hey 7", SMOKE_LINE_PRT },
    { "hey 8", SMOKE_LINE_PRT },
    { "hey 9", SMOKE_LINE_PRT },
    { "hey 10", SMOKE_LINE_PRT }
  };
  static const SmokeExpect terminal_prt_assign[] = {
    { "1", SMOKE_LINE_PRT },
    { "2", SMOKE_LINE_PRT },
    { "3", SMOKE_LINE_PRT }
  };
  static const SmokeExpect false_line[] = { { "FALSE", SMOKE_LINE_PRT } };
  static const SmokeExpect mid_prt_stop[] = {
    { "1", SMOKE_LINE_PRT },
    { "2", SMOKE_LINE_PRT }
  };
  static const SmokeExpect foreach_inner_only[] = {
    { "hey 1", SMOKE_LINE_PRT },
    { "hey 2", SMOKE_LINE_PRT },
    { "hey 3", SMOKE_LINE_PRT }
  };
  static const SmokeExpect foreach_with_return[] = {
    { "hey 1", SMOKE_LINE_PRT },
    { "1", SMOKE_LINE_RENDER },
    { "hey 2", SMOKE_LINE_PRT },
    { "2", SMOKE_LINE_RENDER },
    { "hey 3", SMOKE_LINE_PRT },
    { "3", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect filtered_render[] = {
    { "4", SMOKE_LINE_RENDER },
    { "5", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect string_ci_render[] = {
    { "yo", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect top_first_five[] = {
    { "1", SMOKE_LINE_RENDER },
    { "2", SMOKE_LINE_RENDER },
    { "3", SMOKE_LINE_RENDER },
    { "4", SMOKE_LINE_RENDER },
    { "5", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect top_skip_two[] = {
    { "3", SMOKE_LINE_RENDER },
    { "4", SMOKE_LINE_RENDER },
    { "5", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect sel_name_lines[] = {
    { "alpha", SMOKE_LINE_RENDER },
    { "beta", SMOKE_LINE_RENDER },
    { "gamma", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect sel_pair_lines[] = {
    { "{name:alpha,type:PRG}", SMOKE_LINE_RENDER },
    { "{name:beta,type:SEQ}", SMOKE_LINE_RENDER },
    { "{name:gamma,type:USR}", SMOKE_LINE_RENDER }
  };
  static const SmokeExpect idx_250_line[] = { { "250", SMOKE_LINE_PRT } };
  static const SmokeExpect lst_index_name[] = { { "beta", SMOKE_LINE_PRT } };
  static const SmokeExpect lst_index_blocks[] = { { "30", SMOKE_LINE_PRT } };

  fail = 0;
  readyshell_reu_host_reset();
  smoke_reset(&out);
  rs_vm_init(&vm);
  memset(&platform, 0, sizeof(platform));
  platform.list_dir = smoke_list_dir;
  platform.drive_info = smoke_drive_info;
  rs_vm_set_platform(&vm, &platform);
  rs_vm_set_writer(&vm, smoke_writer, &out);

  fail |= smoke_run_expect(&vm, &out, "$A = 1..10", 0, 0);
  fail |= smoke_run_expect(&vm,
                           &out,
                           "$A | ?[ @ > 5 ] | PRT \"hey \", @",
                           filtered_print,
                           5);
  fail |= smoke_run_expect(&vm, &out, "$B = $A | ?[ @ > 5 ]", 0, 0);
  fail |= smoke_run_expect(&vm, &out, "$B | PRT \"hey \", @", filtered_print, 5);

  fail |= smoke_run_expect(&vm, &out, "$C = 1..3 | PRT @", terminal_prt_assign, 3);
  fail |= smoke_run_expect(&vm, &out, "PRT $C", false_line, 1);
  fail |= smoke_run_expect(&vm, &out, "1..2 | PRT @ | PRT \"again \", @", mid_prt_stop, 2);
  fail |= smoke_run_expect(&vm, &out, "1..3 | %[ PRT \"hey \", @ ]", foreach_inner_only, 3);
  fail |= smoke_run_expect(&vm, &out, "1..3 | %[ PRT \"hey \", @ ] | PRT @", foreach_inner_only, 3);
  fail |= smoke_run_expect(&vm,
                           &out,
                           "1..3 | %[ $B=@; PRT \"hey \", @; @ ]",
                           foreach_with_return,
                           6);
  fail |= smoke_run_expect(&vm, &out, "1..5 | ?[ @ > 3 ]", filtered_render, 2);
  fail |= smoke_run_expect(&vm, &out, "$T = \"yo\"", 0, 0);
  fail |= smoke_run_expect(&vm, &out, "$T | ?[ @ == \"YO\" ]", string_ci_render, 1);
  fail |= smoke_run_expect(&vm, &out, "1..10 | TOP 5", top_first_five, 5);
  fail |= smoke_run_expect(&vm, &out, "1..10 | TOP 3,2", top_skip_two, 3);
  fail |= smoke_run_expect(&vm, &out, "LST | SEL \"NAME\"", sel_name_lines, 3);
  fail |= smoke_run_expect(&vm, &out, "LST | SEL \"name\",\"type\"", sel_pair_lines, 3);
  fail |= smoke_run_expect(&vm, &out, "LST | SEL \"missing\"", 0, 0);
  fail |= smoke_run_expect(&vm, &out, "$D = 1..500", 0, 0);
  fail |= smoke_run_expect(&vm, &out, "PRT $D(249)", idx_250_line, 1);
  fail |= smoke_run_expect(&vm, &out, "$DIR = LST", 0, 0);
  fail |= smoke_run_expect(&vm, &out, "PRT $DIR(1).NAME", lst_index_name, 1);
  fail |= smoke_run_expect(&vm, &out, "PRT $DIR(2).BLOCKS", lst_index_blocks, 1);
  fail |= smoke_run_expect_error(&vm, &out, "1..3 | TOP");
  fail |= smoke_run_expect_error(&vm, &out, "LST | SEL 1");

  rs_vm_free(&vm);
  return fail;
}
