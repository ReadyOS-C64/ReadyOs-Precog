#include "rs_parse_priv.h"

#include <stdlib.h>
#include <string.h>

char* rs_dup_slice_upper(const char* src, unsigned short start, unsigned short len) {
  char* p;
  unsigned short i;

  if (!src) {
    return 0;
  }
  p = (char*)malloc((size_t)len + 1u);
  if (!p) {
    return 0;
  }
  for (i = 0; i < len; ++i) {
    p[i] = (char)rs_ci_char((unsigned char)src[start + i]);
  }
  p[len] = '\0';
  return p;
}

char* rs_dup_string_token(const char* src, const RSToken* tok) {
  char* p;
  unsigned short i;
  unsigned short src_pos;
  unsigned short dst_pos;

  if (!src || !tok) {
    return 0;
  }

  p = (char*)malloc((size_t)tok->value + 1u);
  if (!p) {
    return 0;
  }

  src_pos = (unsigned short)(tok->offset + 1u);
  dst_pos = 0u;
  for (i = 0u; i < tok->value; ++i) {
    if (src[src_pos + i] == '\\' && i + 1u < tok->value) {
      ++i;
    }
    p[dst_pos++] = src[src_pos + i];
  }
  p[dst_pos] = '\0';
  return p;
}

void rs_loc_from_offset(const char* src,
                        unsigned short offset,
                        unsigned short* out_line,
                        unsigned short* out_col) {
  unsigned short i;
  unsigned short line;
  unsigned short col;

  line = 1u;
  col = 1u;
  if (!src) {
    if (out_line) *out_line = line;
    if (out_col) *out_col = col;
    return;
  }

  for (i = 0u; i < offset && src[i] != '\0'; ++i) {
    if (src[i] == '\n') {
      ++line;
      col = 1u;
    } else if (src[i] == '\r') {
      if (src[i + 1u] == '\n') {
        ++i;
      }
      ++line;
      col = 1u;
    } else {
      ++col;
    }
  }

  if (out_line) *out_line = line;
  if (out_col) *out_col = col;
}

void rs_parse_err_tok(RSParser* p, const char* msg, const RSToken* tok) {
  unsigned short line;
  unsigned short col;

  line = 1u;
  col = 1u;
  if (p && tok) {
    rs_loc_from_offset(p->source, tok->offset, &line, &col);
  }
  rs_error_set(p->err,
               RS_ERR_PARSE,
               msg,
               tok ? tok->offset : 0,
               line,
               col);
}

char* rs_dup_tok_upper(const RSParser* p, const RSToken* tok) {
  if (!p || !tok) {
    return 0;
  }
  return rs_dup_slice_upper(p->source, tok->offset, tok->value);
}

char* rs_dup_tok_string(const RSParser* p, const RSToken* tok) {
  if (!p || !tok) {
    return 0;
  }
  return rs_dup_string_token(p->source, tok);
}

unsigned short rs_tok_number(const RSToken* tok) {
  if (!tok) {
    return 0u;
  }
  return tok->value;
}

const RSToken* rs_cur(RSParser* p) {
  if (p->pos >= p->count) {
    return &p->tokens[p->count - 1u];
  }
  return &p->tokens[p->pos];
}

const RSToken* rs_prev(RSParser* p) {
  if (p->pos == 0) {
    return &p->tokens[0];
  }
  return &p->tokens[p->pos - 1u];
}

int rs_match(RSParser* p, RSTokenType type) {
  if (rs_cur(p)->type == type) {
    p->pos++;
    return 1;
  }
  return 0;
}

int rs_expect(RSParser* p, RSTokenType type, const char* msg) {
  if (rs_match(p, type)) {
    return 0;
  }
  rs_parse_err_tok(p, msg, rs_cur(p));
  return -1;
}

RSExpr* rs_new_expr(RSExprKind kind) {
  RSExpr* e;
  e = (RSExpr*)malloc(sizeof(RSExpr));
  if (!e) {
    return 0;
  }
  memset(e, 0, sizeof(RSExpr));
  e->kind = kind;
  return e;
}

int rs_program_add_stmt(RSProgram* prog, const RSStmt* stmt) {
  RSStmt* stmts;
  unsigned short n;
  n = (unsigned short)(prog->count + 1u);
  stmts = (RSStmt*)realloc(prog->stmts, sizeof(RSStmt) * n);
  if (!stmts) {
    return -1;
  }
  prog->stmts = stmts;
  prog->stmts[prog->count] = *stmt;
  prog->count = n;
  return 0;
}

int rs_pipeline_add_stage(RSPipeline* pipeline, const RSStage* stage) {
  RSStage* stages;
  unsigned short n;
  n = (unsigned short)(pipeline->count + 1u);
  stages = (RSStage*)realloc(pipeline->stages, sizeof(RSStage) * n);
  if (!stages) {
    return -1;
  }
  pipeline->stages = stages;
  pipeline->stages[pipeline->count] = *stage;
  pipeline->count = n;
  return 0;
}

int rs_expr_list_add(RSExpr*** arr, unsigned short* count, RSExpr* expr) {
  RSExpr** next;
  unsigned short n;
  n = (unsigned short)(*count + 1u);
  next = (RSExpr**)realloc(*arr, sizeof(RSExpr*) * n);
  if (!next) {
    return -1;
  }
  *arr = next;
  (*arr)[*count] = expr;
  *count = n;
  return 0;
}

int rs_is_expr_start(RSTokenType type) {
  return type == RS_TOK_NUMBER ||
         type == RS_TOK_STRING ||
         type == RS_TOK_TRUE ||
         type == RS_TOK_FALSE ||
         type == RS_TOK_VAR ||
         type == RS_TOK_AT ||
         type == RS_TOK_LPAREN ||
         type == RS_TOK_LBRACKET;
}
