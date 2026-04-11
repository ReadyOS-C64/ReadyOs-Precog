#ifndef RS_PARSE_PRIV_H
#define RS_PARSE_PRIV_H

#include "rs_parse.h"
#include "rs_token.h"

typedef struct RSParser {
  const char* source;
  const RSToken* tokens;
  unsigned short count;
  unsigned short pos;
  RSError* err;
} RSParser;

char* rs_dup_slice_upper(const char* src, unsigned short start, unsigned short len);
char* rs_dup_string_token(const char* src, const RSToken* tok);
void rs_loc_from_offset(const char* src,
                        unsigned short offset,
                        unsigned short* out_line,
                        unsigned short* out_col);
void rs_parse_err_tok(RSParser* p, const char* msg, const RSToken* tok);
char* rs_dup_tok_upper(const RSParser* p, const RSToken* tok);
char* rs_dup_tok_string(const RSParser* p, const RSToken* tok);
unsigned short rs_tok_number(const RSToken* tok);
const RSToken* rs_cur(RSParser* p);
const RSToken* rs_prev(RSParser* p);
int rs_match(RSParser* p, RSTokenType type);
int rs_expect(RSParser* p, RSTokenType type, const char* msg);
RSExpr* rs_new_expr(RSExprKind kind);
int rs_program_add_stmt(RSProgram* prog, const RSStmt* stmt);
int rs_pipeline_add_stage(RSPipeline* pipeline, const RSStage* stage);
int rs_expr_list_add(RSExpr*** arr, unsigned short* count, RSExpr* expr);
int rs_is_expr_start(RSTokenType type);

#endif
