#include <stdio.h>
#include <string.h>

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
  unsigned int count;
} SmokeOut;

static int smoke_writer(void* user, const char* line) {
  SmokeOut* out = (SmokeOut*)user;
  if (!out || out->count >= OUT_MAX_LINES) {
    return -1;
  }
  if (!line) {
    line = "";
  }
  strncpy(out->lines[out->count], line, OUT_LINE_MAX - 1u);
  out->lines[out->count][OUT_LINE_MAX - 1u] = '\0';
  ++out->count;
  return 0;
}

static void smoke_reset(SmokeOut* out) {
  unsigned int i;
  out->count = 0;
  for (i = 0; i < OUT_MAX_LINES; ++i) {
    out->lines[i][0] = '\0';
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
                              const char* const* expect,
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
    if (strcmp(out->lines[i], expect[i]) != 0) {
      printf("FAIL src='%s' line[%u] got='%s' expected='%s'\n",
             source,
             i,
             out->lines[i],
             expect[i]);
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
                            const char* const* expect,
                            unsigned int expect_count) {
  if (smoke_exec(vm, out, source) != 0) {
    return 1;
  }
  return smoke_expect_lines(out, source, expect, expect_count);
}

int main(void) {
  RSVM vm;
  SmokeOut out;
  int fail;
  static const char* const filtered_print[] = {
    "hey 6", "hey 7", "hey 8", "hey 9", "hey 10"
  };
  static const char* const terminal_prt_assign[] = { "1", "2", "3" };
  static const char* const false_line[] = { "FALSE" };
  static const char* const mid_prt_pass[] = { "1", "again 1", "2", "again 2" };

  fail = 0;
  smoke_reset(&out);
  rs_vm_init(&vm);
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
  fail |= smoke_run_expect(&vm, &out, "1..2 | PRT @ | PRT \"again \", @", mid_prt_pass, 4);

  rs_vm_free(&vm);
  return fail;
}
