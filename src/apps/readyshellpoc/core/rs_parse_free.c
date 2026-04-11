#include "rs_parse.h"

#include <stdlib.h>

void rs_expr_free(RSExpr* expr) {
  unsigned short i;

  if (!expr) {
    return;
  }
  if (expr->kind == RS_EXPR_STRING || expr->kind == RS_EXPR_VAR) {
    free(expr->as.text);
  } else if (expr->kind == RS_EXPR_ARRAY) {
    for (i = 0; i < expr->as.array.count; ++i) {
      rs_expr_free(expr->as.array.items[i]);
    }
    free(expr->as.array.items);
  } else if (expr->kind == RS_EXPR_RANGE) {
    rs_expr_free(expr->as.range.start);
    rs_expr_free(expr->as.range.end);
  } else if (expr->kind == RS_EXPR_BINARY) {
    rs_expr_free(expr->as.binary.left);
    rs_expr_free(expr->as.binary.right);
  } else if (expr->kind == RS_EXPR_PROP) {
    rs_expr_free(expr->as.prop.target);
    free(expr->as.prop.name);
  } else if (expr->kind == RS_EXPR_INDEX) {
    rs_expr_free(expr->as.index.target);
    rs_expr_free(expr->as.index.index);
  } else if (expr->kind == RS_EXPR_SCRIPT) {
    rs_program_free(expr->as.script);
    free(expr->as.script);
  }
  free(expr);
}

void rs_stage_free(RSStage* stage) {
  unsigned short i;

  if (!stage) {
    return;
  }
  if (stage->kind == RS_STAGE_CMD) {
    free(stage->as.cmd.name);
    for (i = 0; i < stage->as.cmd.arg_count; ++i) {
      rs_expr_free(stage->as.cmd.args[i]);
    }
    free(stage->as.cmd.args);
  } else if (stage->kind == RS_STAGE_FILTER || stage->kind == RS_STAGE_FOREACH) {
    rs_program_free(stage->as.script);
    free(stage->as.script);
  } else if (stage->kind == RS_STAGE_EXPR) {
    rs_expr_free(stage->as.expr);
  }
}

void rs_pipeline_free(RSPipeline* pipeline) {
  unsigned short i;

  if (!pipeline) {
    return;
  }
  for (i = 0; i < pipeline->count; ++i) {
    rs_stage_free(&pipeline->stages[i]);
  }
  free(pipeline->stages);
  pipeline->stages = 0;
  pipeline->count = 0;
}

void rs_stmt_free(RSStmt* stmt) {
  if (!stmt) {
    return;
  }
  if (stmt->kind == RS_STMT_ASSIGN) {
    free(stmt->as.assign.name);
    if (stmt->as.assign.rhs_is_pipeline) {
      rs_pipeline_free(&stmt->as.assign.pipeline);
    } else {
      rs_expr_free(stmt->as.assign.expr);
    }
  } else if (stmt->kind == RS_STMT_PIPELINE) {
    rs_pipeline_free(&stmt->as.pipeline);
  } else if (stmt->kind == RS_STMT_EXPR) {
    rs_expr_free(stmt->as.expr);
  }
}

void rs_program_free(RSProgram* program) {
  unsigned short i;

  if (!program) {
    return;
  }
  for (i = 0; i < program->count; ++i) {
    rs_stmt_free(&program->stmts[i]);
  }
  free(program->stmts);
  program->stmts = 0;
  program->count = 0;
}
