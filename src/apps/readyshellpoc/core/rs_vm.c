#include "rs_vm.h"

#include "rs_cmd.h"
#include "rs_format.h"
#include "rs_pipe.h"
#include "rs_serialize.h"

#include <stdlib.h>
#include <string.h>

typedef struct RSExecOptions {
  const RSValue* at;
  int has_at;
  int capture_outputs;
  int stream_outputs;
  RSValue* outputs;
  unsigned short output_count;
} RSExecOptions;

static RSVMOutputKind g_rs_vm_output_kind = RS_VM_OUTPUT_RENDER;

static void rs_vm_err(RSError* err, const char* msg) {
  rs_error_set(err, RS_ERR_EXEC, msg, 0, 1, 1);
}

void rs_vm_init(RSVM* vm) {
  if (!vm) {
    return;
  }
  rs_vars_init(&vm->vars);
  vm->platform.user = 0;
  vm->platform.file_read = 0;
  vm->platform.file_write = 0;
  vm->platform.list_dir = 0;
  vm->platform.drive_info = 0;
  vm->write_line = 0;
  vm->write_user = 0;
  vm->tap_len = 0;
  vm->tap_log[0] = '\0';
}

void rs_vm_free(RSVM* vm) {
  if (!vm) {
    return;
  }
  rs_vars_free(&vm->vars);
}

void rs_vm_set_writer(RSVM* vm, RSVMWriteLineFn write_line, void* user) {
  if (!vm) {
    return;
  }
  vm->write_line = write_line;
  vm->write_user = user;
}

void rs_vm_set_platform(RSVM* vm, const RSVMPlatform* platform) {
  if (!vm || !platform) {
    return;
  }
  vm->platform = *platform;
}

void rs_vm_clear_tap_log(RSVM* vm) {
  if (!vm) {
    return;
  }
  vm->tap_len = 0;
  vm->tap_log[0] = '\0';
}

const char* rs_vm_get_tap_log(const RSVM* vm) {
  if (!vm) {
    return "";
  }
  return vm->tap_log;
}

RSVMOutputKind rs_vm_current_output_kind(void) {
  return g_rs_vm_output_kind;
}

static int rs_vm_write_line(RSVM* vm, const char* line) {
  if (!vm || !vm->write_line) {
    return 0;
  }
  g_rs_vm_output_kind = RS_VM_OUTPUT_RENDER;
  return vm->write_line(vm->write_user, line ? line : "");
}

static int rs_vm_write_prt_line(RSVM* vm, const char* line) {
  if (!vm || !vm->write_line) {
    return 0;
  }
  g_rs_vm_output_kind = RS_VM_OUTPUT_PRT;
  return vm->write_line(vm->write_user, line ? line : "");
}

static int rs_vm_tap_append(RSVM* vm, const char* tag, const RSValue* item) {
  char buf[256];
  unsigned short need;
  unsigned short tag_len;
  unsigned short item_len;
  if (!vm) {
    return -1;
  }
  if (!tag) {
    tag = "TAP";
  }
  if (rs_format_value(item, buf, sizeof(buf)) < 0) {
    return -1;
  }
  tag_len = (unsigned short)strlen(tag);
  item_len = (unsigned short)strlen(buf);
  need = (unsigned short)(tag_len + 1u + item_len + 1u);
  if ((unsigned long)vm->tap_len + (unsigned long)need + 1ul > (unsigned long)sizeof(vm->tap_log)) {
    return -1;
  }
  memcpy(vm->tap_log + vm->tap_len, tag, tag_len);
  vm->tap_len = (unsigned short)(vm->tap_len + tag_len);
  vm->tap_log[vm->tap_len++] = ':';
  memcpy(vm->tap_log + vm->tap_len, buf, item_len);
  vm->tap_len = (unsigned short)(vm->tap_len + item_len);
  vm->tap_log[vm->tap_len++] = '\n';
  vm->tap_log[vm->tap_len] = '\0';
  return 0;
}

static int rs_vm_eval_expr(RSVM* vm,
                           const RSExpr* expr,
                           const RSValue* at,
                           int has_at,
                           RSValue* out,
                           RSError* err);

static int rs_vm_exec_program_internal(RSVM* vm,
                                       const RSProgram* program,
                                       const RSValue* at,
                                       int has_at,
                                       RSValue* out_last,
                                       int* out_has_last,
                                       RSError* err);

static int rs_vm_eval_expr(RSVM* vm,
                           const RSExpr* expr,
                           const RSValue* at,
                           int has_at,
                           RSValue* out,
                           RSError* err) {
  unsigned short i;
  RSValue left;
  RSValue right;
  RSValue tmp;
  unsigned short u1;
  unsigned short u2;
  int truth;
  const RSValue* found;

  (void)vm;

  if (!expr || !out) {
    rs_vm_err(err, "invalid expression");
    return -1;
  }

  rs_value_init_false(out);

  if (expr->kind == RS_EXPR_NUMBER) {
    rs_value_init_u16(out, expr->as.number);
    return 0;
  }
  if (expr->kind == RS_EXPR_STRING) {
    if (rs_value_init_string(out, expr->as.text) != 0) {
      rs_vm_err(err, "out of memory");
      return -1;
    }
    return 0;
  }
  if (expr->kind == RS_EXPR_BOOL) {
    rs_value_init_bool(out, expr->as.boolean);
    return 0;
  }
  if (expr->kind == RS_EXPR_VAR) {
    found = rs_vars_get(&vm->vars, expr->as.text);
    if (!found) {
      rs_value_init_false(out);
      return 0;
    }
    if (rs_value_clone(out, found) != 0) {
      rs_vm_err(err, "out of memory");
      return -1;
    }
    return 0;
  }
  if (expr->kind == RS_EXPR_AT) {
    if (!has_at || !at) {
      rs_value_init_false(out);
      return 0;
    }
    if (rs_value_clone(out, at) != 0) {
      rs_vm_err(err, "out of memory");
      return -1;
    }
    return 0;
  }
  if (expr->kind == RS_EXPR_ARRAY) {
    if (rs_value_array_new(out, expr->as.array.count) != 0) {
      rs_vm_err(err, "out of memory");
      return -1;
    }
    for (i = 0; i < expr->as.array.count; ++i) {
      if (rs_vm_eval_expr(vm, expr->as.array.items[i], at, has_at, &tmp, err) != 0) {
        return -1;
      }
      if (rs_value_clone(&out->as.array.items[i], &tmp) != 0) {
        rs_value_free(&tmp);
        rs_vm_err(err, "out of memory");
        return -1;
      }
      rs_value_free(&tmp);
    }
    return 0;
  }
  if (expr->kind == RS_EXPR_RANGE) {
    if (rs_vm_eval_expr(vm, expr->as.range.start, at, has_at, &left, err) != 0) {
      return -1;
    }
    if (rs_vm_eval_expr(vm, expr->as.range.end, at, has_at, &right, err) != 0) {
      rs_value_free(&left);
      return -1;
    }
    if (rs_value_to_u16(&left, &u1) != 0 || rs_value_to_u16(&right, &u2) != 0) {
      rs_value_free(&left);
      rs_value_free(&right);
      rs_vm_err(err, "range bounds must be numeric");
      return -1;
    }
    rs_value_free(&left);
    rs_value_free(&right);
    if (rs_value_array_from_u16_range(out, u1, u2) != 0) {
      rs_vm_err(err, "out of memory");
      return -1;
    }
    return 0;
  }
  if (expr->kind == RS_EXPR_PROP) {
    if (rs_vm_eval_expr(vm, expr->as.prop.target, at, has_at, &left, err) != 0) {
      return -1;
    }
    if (left.tag != RS_VAL_OBJECT) {
      rs_value_free(&left);
      rs_value_init_false(out);
      return 0;
    }
    found = rs_value_object_get(&left, expr->as.prop.name);
    if (!found) {
      rs_value_free(&left);
      rs_value_init_false(out);
      return 0;
    }
    if (rs_value_clone(out, found) != 0) {
      rs_value_free(&left);
      rs_vm_err(err, "out of memory");
      return -1;
    }
    rs_value_free(&left);
    return 0;
  }
  if (expr->kind == RS_EXPR_INDEX) {
    if (rs_vm_eval_expr(vm, expr->as.index.target, at, has_at, &left, err) != 0) {
      return -1;
    }
    if (rs_vm_eval_expr(vm, expr->as.index.index, at, has_at, &right, err) != 0) {
      rs_value_free(&left);
      return -1;
    }
    if (rs_value_to_u16(&right, &u1) != 0) {
      rs_value_free(&left);
      rs_value_free(&right);
      rs_vm_err(err, "index must be numeric");
      return -1;
    }
    if (left.tag == RS_VAL_ARRAY && u1 < left.as.array.count) {
      if (rs_value_clone(out, &left.as.array.items[u1]) != 0) {
        rs_value_free(&left);
        rs_value_free(&right);
        rs_vm_err(err, "out of memory");
        return -1;
      }
    } else {
      rs_value_init_false(out);
    }
    rs_value_free(&left);
    rs_value_free(&right);
    return 0;
  }
  if (expr->kind == RS_EXPR_BINARY) {
    if (rs_vm_eval_expr(vm, expr->as.binary.left, at, has_at, &left, err) != 0) {
      return -1;
    }
    if (rs_vm_eval_expr(vm, expr->as.binary.right, at, has_at, &right, err) != 0) {
      rs_value_free(&left);
      return -1;
    }

    if (expr->as.binary.op == RS_OP_EQ) {
      truth = rs_value_eq(&left, &right);
    } else if (expr->as.binary.op == RS_OP_NEQ) {
      truth = !rs_value_eq(&left, &right);
    } else {
      if (rs_value_to_u16(&left, &u1) != 0 || rs_value_to_u16(&right, &u2) != 0) {
        rs_value_free(&left);
        rs_value_free(&right);
        rs_vm_err(err, "comparison requires numeric operands");
        return -1;
      }
      if (expr->as.binary.op == RS_OP_GT) {
        truth = u1 > u2;
      } else if (expr->as.binary.op == RS_OP_LT) {
        truth = u1 < u2;
      } else if (expr->as.binary.op == RS_OP_GTE) {
        truth = u1 >= u2;
      } else {
        truth = u1 <= u2;
      }
    }

    rs_value_free(&left);
    rs_value_free(&right);
    rs_value_init_bool(out, truth);
    return 0;
  }
  if (expr->kind == RS_EXPR_SCRIPT) {
    int has_last;
    if (rs_vm_exec_program_internal(vm, expr->as.script, at, has_at, out, &has_last, err) != 0) {
      return -1;
    }
    if (!has_last) {
      rs_value_init_false(out);
    }
    return 0;
  }

  rs_vm_err(err, "unsupported expression kind");
  return -1;
}

static int rs_vm_collect_output(RSVM* vm,
                                RSExecOptions* opt,
                                const RSValue* value,
                                RSError* err) {
  char line[512];
  if (!opt) {
    return 0;
  }
  if (!opt->capture_outputs) {
    if (opt->stream_outputs) {
      if (rs_format_value(value, line, sizeof(line)) < 0) {
        rs_vm_err(err, "print format overflow");
        return -1;
      }
      if (rs_vm_write_line(vm, line) != 0) {
        rs_vm_err(err, "write failed");
        return -1;
      }
      return 0;
    }
    return 0;
  }
  if (rs_pipe_append_item(&opt->outputs, &opt->output_count, value) != 0) {
    rs_vm_err(err, "out of memory");
    return -1;
  }
  return 0;
}

static int rs_vm_command_to_drive(const RSValue* args,
                                  unsigned short arg_count,
                                  unsigned char* out_drive) {
  unsigned short n;
  if (arg_count == 0) {
    *out_drive = 8;
    return 0;
  }
  if (rs_value_to_u16(&args[0], &n) != 0 || n > 255u) {
    return -1;
  }
  *out_drive = (unsigned char)n;
  return 0;
}

static int rs_vm_eval_cmd_args(RSVM* vm,
                               RSExpr** arg_exprs,
                               unsigned short arg_count,
                               const RSValue* at,
                               int has_at,
                               RSValue** out_args,
                               RSError* err) {
  RSValue* args;
  unsigned short i;
  args = 0;
  if (arg_count > 0) {
    args = (RSValue*)malloc(sizeof(RSValue) * arg_count);
    if (!args) {
      rs_vm_err(err, "out of memory");
      return -1;
    }
  }
  for (i = 0; i < arg_count; ++i) {
    rs_value_init_false(&args[i]);
    if (rs_vm_eval_expr(vm, arg_exprs[i], at, has_at, &args[i], err) != 0) {
      unsigned short j;
      for (j = 0; j <= i; ++j) {
        rs_value_free(&args[j]);
      }
      free(args);
      return -1;
    }
  }
  *out_args = args;
  return 0;
}

static void rs_vm_free_args(RSValue* args, unsigned short arg_count) {
  unsigned short i;
  if (!args) {
    return;
  }
  for (i = 0; i < arg_count; ++i) {
    rs_value_free(&args[i]);
  }
  free(args);
}

static const char* rs_vm_value_cstr(const RSValue* v, char* scratch, unsigned short max) {
  if (v->tag == RS_VAL_STR) {
    return v->as.str.bytes;
  }
  if (rs_format_value(v, scratch, max) < 0) {
    return "";
  }
  return scratch;
}

static int rs_vm_exec_pipeline_from(RSVM* vm,
                                    const RSPipeline* pipeline,
                                    unsigned short stage_index,
                                    const RSValue* item,
                                    int has_item,
                                    RSExecOptions* opt,
                                    RSError* err);
static int rs_vm_emit_value(RSVM* vm,
                            const RSPipeline* pipeline,
                            unsigned short next_stage_index,
                            const RSValue* value,
                            RSExecOptions* opt,
                            RSError* err);

static int rs_vm_exec_command_stage(RSVM* vm,
                                    const RSStage* stage,
                                    const RSPipeline* pipeline,
                                    unsigned short stage_index,
                                    const RSValue* item,
                                    int has_item,
                                    RSExecOptions* opt,
                                    RSError* err) {
  RSCommandId id;
  RSValue* args;
  unsigned short arg_count;
  RSValue out;
  RSValue bool_out;
  RSValue loaded;
  RSValue result;
  unsigned short i;
  unsigned short n;
  char line[512];
  unsigned short line_len;
  char scratch[256];
  const char* path;
  unsigned short payload_len;
  unsigned short bytes_len;
  unsigned char* bytes;
  unsigned char drive;

  args = 0;
  arg_count = stage->as.cmd.arg_count;
  if (rs_vm_eval_cmd_args(vm, stage->as.cmd.args, arg_count, item, has_item, &args, err) != 0) {
    return -1;
  }

  id = rs_cmd_id(stage->as.cmd.name);
  if (id == RS_CMD_UNKNOWN) {
    rs_vm_free_args(args, arg_count);
    rs_vm_err(err, "unknown command");
    return -1;
  }

  if (id == RS_CMD_GEN) {
    if (arg_count < 1 || rs_value_to_u16(&args[0], &n) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "GEN expects numeric count");
      return -1;
    }
    rs_vm_free_args(args, arg_count);
    for (i = 1; i <= n; ++i) {
      rs_value_init_u16(&out, i);
      if (rs_vm_exec_pipeline_from(vm,
                                   pipeline,
                                   (unsigned short)(stage_index + 1u),
                                   &out,
                                   1,
                                   opt,
                                   err) != 0) {
        return -1;
      }
    }
    return 0;
  }

  if (id == RS_CMD_TAP) {
    if (!has_item) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "TAP requires pipeline item");
      return -1;
    }
    if (arg_count > 0) {
      path = rs_vm_value_cstr(&args[0], scratch, sizeof(scratch));
    } else {
      path = "TAP";
    }
    if (rs_vm_tap_append(vm, path, item) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "tap log overflow");
      return -1;
    }
    rs_vm_free_args(args, arg_count);
    return rs_vm_exec_pipeline_from(vm,
                                    pipeline,
                                    (unsigned short)(stage_index + 1u),
                                    item,
                                    1,
                                    opt,
                                    err);
  }

  if (id == RS_CMD_PRT) {
    line[0] = '\0';
    line_len = 0;
    if (arg_count == 0) {
      if (has_item) {
        if (rs_format_value(item, line, sizeof(line)) < 0) {
          rs_vm_free_args(args, arg_count);
          rs_vm_err(err, "print format overflow");
          return -1;
        }
      }
    } else {
      for (i = 0; i < arg_count; ++i) {
        char part[256];
        int plen;
        plen = rs_format_value(&args[i], part, sizeof(part));
        if (plen < 0) {
          rs_vm_free_args(args, arg_count);
          rs_vm_err(err, "print format overflow");
          return -1;
        }
        if ((unsigned long)line_len + (unsigned long)plen + 1ul > (unsigned long)sizeof(line)) {
          rs_vm_free_args(args, arg_count);
          rs_vm_err(err, "print line too long");
          return -1;
        }
        memcpy(line + line_len, part, (size_t)plen);
        line_len = (unsigned short)(line_len + (unsigned short)plen);
        line[line_len] = '\0';
      }
    }
    if (rs_vm_write_prt_line(vm, line) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "write failed");
      return -1;
    }
    rs_vm_free_args(args, arg_count);
    return 0;
  }

  if (id == RS_CMD_LDV) {
    if (arg_count < 1) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "LDV expects path");
      return -1;
    }
    if (!vm->platform.file_read) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "LDV unavailable on this platform");
      return -1;
    }
    path = rs_vm_value_cstr(&args[0], scratch, sizeof(scratch));
    bytes = (unsigned char*)malloc(65535u);
    if (!bytes) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "out of memory");
      return -1;
    }
    if (vm->platform.file_read(vm->platform.user,
                               path,
                               bytes,
                               65535u,
                               &bytes_len) != 0) {
      free(bytes);
      rs_vm_free_args(args, arg_count);
      rs_value_init_false(&bool_out);
      return rs_vm_exec_pipeline_from(vm,
                                      pipeline,
                                      (unsigned short)(stage_index + 1u),
                                      &bool_out,
                                      1,
                                      opt,
                                      err);
    }
    rs_value_init_false(&loaded);
    if (rs_deserialize_file_payload(bytes, bytes_len, &loaded) != 0) {
      free(bytes);
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "LDV parse failed");
      return -1;
    }
    free(bytes);
    rs_vm_free_args(args, arg_count);
    n = rs_vm_exec_pipeline_from(vm,
                                 pipeline,
                                 (unsigned short)(stage_index + 1u),
                                 &loaded,
                                 1,
                                 opt,
                                 err);
    rs_value_free(&loaded);
    return n;
  }

  if (id == RS_CMD_STV) {
    if (arg_count < 2) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "STV expects value and path");
      return -1;
    }
    if (!vm->platform.file_write) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "STV unavailable on this platform");
      return -1;
    }
    bytes = (unsigned char*)malloc(65535u);
    if (!bytes) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "out of memory");
      return -1;
    }
    if (rs_serialize_file_payload(&args[0], bytes, 65535u, &payload_len) != 0) {
      free(bytes);
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "STV serialize failed");
      return -1;
    }
    path = rs_vm_value_cstr(&args[1], scratch, sizeof(scratch));
    rs_value_init_bool(&result,
                       vm->platform.file_write(vm->platform.user,
                                               path,
                                               bytes,
                                               payload_len) == 0);
    free(bytes);
    rs_vm_free_args(args, arg_count);
    return rs_vm_exec_pipeline_from(vm,
                                    pipeline,
                                    (unsigned short)(stage_index + 1u),
                                    &result,
                                    1,
                                    opt,
                                    err);
  }

  if (id == RS_CMD_LST) {
    rs_value_init_false(&result);
    if (!vm->platform.list_dir) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "LST unavailable on this platform");
      return -1;
    }
    if (rs_vm_command_to_drive(args, arg_count, &drive) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "LST expects drive number");
      return -1;
    }
    if (vm->platform.list_dir(vm->platform.user, drive, &result) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "LST failed");
      return -1;
    }
    rs_vm_free_args(args, arg_count);
    n = rs_vm_exec_pipeline_from(vm,
                                 pipeline,
                                 (unsigned short)(stage_index + 1u),
                                 &result,
                                 1,
                                 opt,
                                 err);
    rs_value_free(&result);
    return n;
  }

  if (id == RS_CMD_DRVI) {
    rs_value_init_false(&result);
    if (!vm->platform.drive_info) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "DRVI unavailable on this platform");
      return -1;
    }
    if (rs_vm_command_to_drive(args, arg_count, &drive) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "DRVI expects drive number");
      return -1;
    }
    if (vm->platform.drive_info(vm->platform.user, drive, &result) != 0) {
      rs_vm_free_args(args, arg_count);
      rs_vm_err(err, "DRVI failed");
      return -1;
    }
    rs_vm_free_args(args, arg_count);
    n = rs_vm_exec_pipeline_from(vm,
                                 pipeline,
                                 (unsigned short)(stage_index + 1u),
                                 &result,
                                 1,
                                 opt,
                                 err);
    rs_value_free(&result);
    return n;
  }

  rs_vm_free_args(args, arg_count);
  rs_vm_err(err, "command not implemented");
  return -1;
}

static int rs_vm_exec_pipeline_from(RSVM* vm,
                                    const RSPipeline* pipeline,
                                    unsigned short stage_index,
                                    const RSValue* item,
                                    int has_item,
                                    RSExecOptions* opt,
                                    RSError* err) {
  const RSStage* stage;
  RSValue value;
  RSValue* expanded;
  unsigned short expanded_count;
  unsigned short i;
  RSValue pred;
  int has_last;
  int n;

  if (stage_index >= pipeline->count) {
    if (has_item) {
      if (rs_vm_collect_output(vm, opt, item, err) != 0) {
        return -1;
      }
    }
    return 0;
  }

  stage = &pipeline->stages[stage_index];

  if (stage->kind == RS_STAGE_CMD) {
    return rs_vm_exec_command_stage(vm, stage, pipeline, stage_index, item, has_item, opt, err);
  }

  if (stage->kind == RS_STAGE_EXPR) {
    rs_value_init_false(&value);
    if (rs_vm_eval_expr(vm, stage->as.expr, item, has_item, &value, err) != 0) {
      rs_value_free(&value);
      return -1;
    }
    if (rs_pipe_expand_value(&value, &expanded, &expanded_count) != 0) {
      rs_value_free(&value);
      rs_vm_err(err, "out of memory");
      return -1;
    }
    for (i = 0; i < expanded_count; ++i) {
      if (rs_vm_exec_pipeline_from(vm,
                                   pipeline,
                                   (unsigned short)(stage_index + 1u),
                                   &expanded[i],
                                   1,
                                   opt,
                                   err) != 0) {
        rs_pipe_free_items(expanded, expanded_count);
        rs_value_free(&value);
        return -1;
      }
    }
    rs_pipe_free_items(expanded, expanded_count);
    rs_value_free(&value);
    return 0;
  }

  if (stage->kind == RS_STAGE_FILTER) {
    if (!has_item) {
      return 0;
    }
    rs_value_init_false(&pred);
    has_last = 0;
    if (rs_vm_exec_program_internal(vm,
                                    stage->as.script,
                                    item,
                                    1,
                                    &pred,
                                    &has_last,
                                    err) != 0) {
      rs_value_free(&pred);
      return -1;
    }
    if (has_last && rs_value_truthy(&pred)) {
      rs_value_free(&pred);
      return rs_vm_exec_pipeline_from(vm,
                                      pipeline,
                                      (unsigned short)(stage_index + 1u),
                                      item,
                                      1,
                                      opt,
                                      err);
    }
    rs_value_free(&pred);
    return 0;
  }

  if (stage->kind == RS_STAGE_FOREACH) {
    if (!has_item) {
      return 0;
    }
    rs_value_init_false(&pred);
    has_last = 0;
    if (rs_vm_exec_program_internal(vm,
                                    stage->as.script,
                                    item,
                                    1,
                                    &pred,
                                    &has_last,
                                    err) != 0) {
      rs_value_free(&pred);
      return -1;
    }
    if (!has_last) {
      rs_value_free(&pred);
      return 0;
    }
    n = rs_vm_emit_value(vm,
                         pipeline,
                         (unsigned short)(stage_index + 1u),
                         &pred,
                         opt,
                         err);
    rs_value_free(&pred);
    return n;
  }

  rs_vm_err(err, "invalid stage");
  return -1;
}

static int rs_vm_emit_value(RSVM* vm,
                            const RSPipeline* pipeline,
                            unsigned short next_stage_index,
                            const RSValue* value,
                            RSExecOptions* opt,
                            RSError* err) {
  RSValue* expanded;
  unsigned short count;
  unsigned short i;

  if (rs_pipe_expand_value(value, &expanded, &count) != 0) {
    rs_vm_err(err, "out of memory");
    return -1;
  }

  for (i = 0; i < count; ++i) {
    if (rs_vm_exec_pipeline_from(vm,
                                 pipeline,
                                 next_stage_index,
                                 &expanded[i],
                                 1,
                                 opt,
                                 err) != 0) {
      rs_pipe_free_items(expanded, count);
      return -1;
    }
  }

  rs_pipe_free_items(expanded, count);
  return 0;
}

static int rs_vm_run_pipeline(RSVM* vm,
                              const RSPipeline* pipeline,
                              const RSValue* at,
                              int has_at,
                              RSExecOptions* opt,
                              RSError* err) {
  if (!pipeline) {
    rs_vm_err(err, "invalid pipeline");
    return -1;
  }
  if (pipeline->count == 0) {
    return 0;
  }
  return rs_vm_exec_pipeline_from(vm, pipeline, 0, at, has_at, opt, err);
}

static int rs_vm_assign_from_outputs(RSValue* out, const RSExecOptions* opt) {
  unsigned short i;
  if (opt->output_count == 0) {
    rs_value_init_false(out);
    return 0;
  }
  if (opt->output_count == 1) {
    return rs_value_clone(out, &opt->outputs[0]);
  }
  if (rs_value_array_new(out, opt->output_count) != 0) {
    return -1;
  }
  for (i = 0; i < opt->output_count; ++i) {
    if (rs_value_clone(&out->as.array.items[i], &opt->outputs[i]) != 0) {
      return -1;
    }
  }
  return 0;
}

static int rs_vm_should_auto_print_pipeline(const RSPipeline* pipeline) {
  const RSStage* last;
  RSCommandId id;
  if (!pipeline || pipeline->count == 0) {
    return 0;
  }
  last = &pipeline->stages[pipeline->count - 1u];
  if (last->kind == RS_STAGE_EXPR) {
    return 1;
  }
  if (last->kind == RS_STAGE_FILTER || last->kind == RS_STAGE_FOREACH) {
    return 1;
  }
  if (last->kind != RS_STAGE_CMD) {
    return 0;
  }
  id = rs_cmd_id(last->as.cmd.name);
  return id != RS_CMD_PRT;
}

static int rs_vm_auto_print_value(RSVM* vm,
                                  const RSValue* value,
                                  int has_at,
                                  RSError* err) {
  RSValue* expanded;
  unsigned short count;
  unsigned short i;
  char line[512];
  if (!vm || !value || has_at) {
    return 0;
  }
  if (rs_pipe_expand_value(value, &expanded, &count) != 0) {
    rs_vm_err(err, "out of memory");
    return -1;
  }
  for (i = 0; i < count; ++i) {
    if (rs_format_value(&expanded[i], line, sizeof(line)) < 0) {
      rs_pipe_free_items(expanded, count);
      rs_vm_err(err, "print format overflow");
      return -1;
    }
    if (rs_vm_write_line(vm, line) != 0) {
      rs_pipe_free_items(expanded, count);
      rs_vm_err(err, "write failed");
      return -1;
    }
  }
  rs_pipe_free_items(expanded, count);
  return 0;
}

static int rs_vm_exec_stmt(RSVM* vm,
                           const RSStmt* stmt,
                           const RSValue* at,
                           int has_at,
                           RSValue* out_last,
                           int* out_has_last,
                           RSError* err) {
  RSValue value;
  RSExecOptions opt;

  rs_value_init_false(&value);
  opt.at = at;
  opt.has_at = has_at;
  opt.capture_outputs = 1;
  opt.stream_outputs = 0;
  opt.outputs = 0;
  opt.output_count = 0;

  if (stmt->kind == RS_STMT_ASSIGN) {
    if (stmt->as.assign.rhs_is_pipeline) {
      if (rs_vm_run_pipeline(vm,
                             &stmt->as.assign.pipeline,
                             at,
                             has_at,
                             &opt,
                             err) != 0) {
        rs_pipe_free_items(opt.outputs, opt.output_count);
        return -1;
      }
      if (rs_vm_assign_from_outputs(&value, &opt) != 0) {
        rs_pipe_free_items(opt.outputs, opt.output_count);
        rs_vm_err(err, "out of memory");
        return -1;
      }
      rs_pipe_free_items(opt.outputs, opt.output_count);
    } else {
      if (rs_vm_eval_expr(vm, stmt->as.assign.expr, at, has_at, &value, err) != 0) {
        return -1;
      }
    }
    if (rs_vars_set(&vm->vars, stmt->as.assign.name, &value) != 0) {
      rs_value_free(&value);
      rs_vm_err(err, "variable table full");
      return -1;
    }
    rs_value_free(out_last);
    if (rs_value_clone(out_last, &value) != 0) {
      rs_value_free(&value);
      rs_vm_err(err, "out of memory");
      return -1;
    }
    rs_value_free(&value);
    *out_has_last = 1;
    return 0;
  }

  if (stmt->kind == RS_STMT_PIPELINE) {
    if (!has_at) {
      opt.capture_outputs = 0;
      opt.stream_outputs = rs_vm_should_auto_print_pipeline(&stmt->as.pipeline);
    }
    if (rs_vm_run_pipeline(vm, &stmt->as.pipeline, at, has_at, &opt, err) != 0) {
      rs_pipe_free_items(opt.outputs, opt.output_count);
      return -1;
    }
    rs_value_free(out_last);
    if (!has_at || opt.output_count == 0) {
      rs_value_init_false(out_last);
      *out_has_last = 0;
    } else {
      if (rs_vm_assign_from_outputs(out_last, &opt) != 0) {
        rs_pipe_free_items(opt.outputs, opt.output_count);
        rs_vm_err(err, "out of memory");
        return -1;
      }
      *out_has_last = 1;
    }
    rs_pipe_free_items(opt.outputs, opt.output_count);
    return 0;
  }

  if (stmt->kind == RS_STMT_EXPR) {
    if (rs_vm_eval_expr(vm, stmt->as.expr, at, has_at, &value, err) != 0) {
      return -1;
    }
    if (rs_vm_auto_print_value(vm, &value, has_at, err) != 0) {
      rs_value_free(&value);
      return -1;
    }
    rs_value_free(out_last);
    if (rs_value_clone(out_last, &value) != 0) {
      rs_value_free(&value);
      rs_vm_err(err, "out of memory");
      return -1;
    }
    rs_value_free(&value);
    *out_has_last = 1;
    return 0;
  }

  rs_vm_err(err, "unknown statement");
  return -1;
}

static int rs_vm_exec_program_internal(RSVM* vm,
                                       const RSProgram* program,
                                       const RSValue* at,
                                       int has_at,
                                       RSValue* out_last,
                                       int* out_has_last,
                                       RSError* err) {
  unsigned short i;

  if (!vm || !program || !out_last || !out_has_last) {
    rs_vm_err(err, "invalid program args");
    return -1;
  }

  *out_has_last = 0;
  rs_value_init_false(out_last);

  for (i = 0; i < program->count; ++i) {
    if (rs_vm_exec_stmt(vm, &program->stmts[i], at, has_at, out_last, out_has_last, err) != 0) {
      return -1;
    }
  }

  return 0;
}

int rs_vm_exec_program(RSVM* vm, const RSProgram* program, RSError* err) {
  RSValue last;
  int has_last;
  rs_error_init(err);
  rs_value_init_false(&last);
  has_last = 0;
  if (rs_vm_exec_program_internal(vm, program, 0, 0, &last, &has_last, err) != 0) {
    rs_value_free(&last);
    return -1;
  }
  rs_value_free(&last);
  return 0;
}

int rs_vm_exec_source(RSVM* vm, const char* source, RSError* err) {
  RSProgram program;
  int rc;
  if (!vm || !source) {
    rs_vm_err(err, "invalid source");
    return -1;
  }
  if (rs_parse_source(source, &program, err) != 0) {
    return -1;
  }
  rc = rs_vm_exec_program(vm, &program, err);
  rs_program_free(&program);
  return rc;
}
