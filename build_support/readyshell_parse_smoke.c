#include <stdio.h>

#include "src/apps/readyshell/core/rs_errors.h"
#include "src/apps/readyshell/core/rs_parse.h"

typedef struct ParseCase {
  const char* label;
  const char* source;
  int expect_ok;
} ParseCase;

static int run_case(const ParseCase* tc) {
  RSProgram program;
  RSError err;
  int rc;
  int pass;

  rc = rs_parse_source(tc->source, &program, &err);
  pass = tc->expect_ok ? (rc == 0) : (rc != 0);

  if (rc == 0) {
    printf("OK   %-26s stmts=%u src='%s'\n",
           tc->label,
           (unsigned)program.count,
           tc->source);
    rs_program_free(&program);
  } else {
    printf("ERR  %-26s code=%d msg=%s line=%u col=%u src='%s'\n",
           tc->label,
           (int)err.code,
           err.message ? err.message : "<null>",
           (unsigned)err.line,
           (unsigned)err.column,
           tc->source);
  }

  return pass ? 0 : 1;
}

int main(void) {
  static const ParseCase cases[] = {
    { "scalar number", "1", 1 },
    { "scalar string", "\"READY\"", 1 },
    { "scalar bool", "TRUE", 1 },
    { "assignment", "$A = 1", 1 },
    { "array literal", "$A = [1,2,3]", 1 },
    { "comma array", "$A = 1,2,3", 1 },
    { "descending range", "PRT 5..1", 1 },
    { "nested arrays", "$A = 1..5; $B = [\"HEAD\",\"TAIL\",$A]; PRT $B(2)(4)", 1 },
    { "object property", "$DIR = LST; PRT $DIR(0).NAME", 1 },
    { "comparison filter", "1..8 | ?[ @ <> 3 ] | ?[ @ <= 5 ]", 1 },
    { "expr stage", "GEN 5 | @ > 2", 1 },
    { "filter block", "GEN 5 | ?[ $LAST = @; @ >= 3 ]", 1 },
    { "foreach block", "1..3 | %[ PRT \"ITEM \", @ ] | PRT @", 1 },
    { "top more sel", "LST | TOP 2,1 | MORE 5 | SEL \"NAME\",\"TYPE\"", 1 },
    { "drive commands", "DRVI 9 | SEL \"DRIVE\",\"DISKNAME\"", 1 },
    { "value commands", "STV 42, \"answer\", 9; $A = LDV \"answer\", 9", 1 },
    { "text commands", "PUT \"HELLO\", \"notes\"; ADD \"TAIL\", \"notes\"; CAT \"notes\"", 1 },
    { "copy rename delete", "COPY \"notes\", \"notes.bak\"; REN \"notes\", \"notes.old\"; DEL \"notes.old\"", 1 },
    { "multi statement", "$A = 3; $B = 5; 1..8 | ?[ @ <> $A ] | ?[ @ <= $B ]", 1 },
    { "bad array", "$A = [1,2", 0 },
    { "bad range", "$A = 1..", 0 },
    { "bad filter", "GEN 5 | ?[ @ > 2 ", 0 },
    { "bad foreach", "1..3 | %[ PRT @ ", 0 },
    { "bad pipeline", "GEN 3 | | PRT @", 0 },
    { "bad property", "PRT @.", 0 },
    { "bad command args", "SEL", 1 }
  };
  unsigned int i;
  int fail;

  fail = 0;
  for (i = 0; i < (unsigned int)(sizeof(cases) / sizeof(cases[0])); ++i) {
    fail |= run_case(&cases[i]);
  }
  return fail;
}
