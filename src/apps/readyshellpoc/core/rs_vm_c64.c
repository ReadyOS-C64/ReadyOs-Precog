#include "rs_vm.h"

#include "rs_cmd.h"
#include "rs_cmd_overlay.h"
#include "rs_format.h"
#include "rs_pipe.h"
#include "../platform/rs_memcfg.h"
#include "../platform/rs_overlay.h"

#include <stdlib.h>
#include <string.h>

#ifndef RS_C64_OVERLAY_RUNTIME
#define RS_C64_OVERLAY_RUNTIME 0
#endif

typedef struct RSTopStageState {
  unsigned short seen;
  unsigned short count;
  unsigned short skip;
  unsigned char init;
} RSTopStageState;

#define RS_TOP_STAGE_MAX 4u

typedef struct RSOutCollect {
  RSValue* items;
  unsigned short count;
  int enabled;
  int stream_print;
  unsigned char top_state_count;
  unsigned char top_stage_index[RS_TOP_STAGE_MAX];
  RSTopStageState top_states[RS_TOP_STAGE_MAX];
} RSOutCollect;

static RSVMOutputKind g_rs_vm_output_kind = RS_VM_OUTPUT_RENDER;

extern char rs_vm_fmt_buf[128];
extern char rs_vm_line_buf[384];

static void vm_err(RSError* err, const char* msg) {
  rs_error_set(err, RS_ERR_EXEC, msg, 0, 1, 1);
}

#if RS_C64_OVERLAY_RUNTIME
extern int rs_parse_source_overlay_call(const char* source, RSProgram* out_program, RSError* err);

static void vm_overlay_enter(void) {
  rs_memcfg_push_ram_under_basic();
}

static void vm_overlay_leave(void) {
  rs_memcfg_pop();
}
#endif

static int vm_parse_source_guarded(const char* source, RSProgram* out_program, RSError* err) {
#if RS_C64_OVERLAY_RUNTIME
  int rc;
  rs_overlay_debug_mark('m');
  rs_overlay_debug_mark('n');
  rc = rs_parse_source_overlay_call(source, out_program, err);
  rs_overlay_debug_mark('o');
  return rc;
#else
  return rs_parse_source(source, out_program, err);
#endif
}

static void vm_program_free_guarded(RSProgram* program) {
#if RS_C64_OVERLAY_RUNTIME
  vm_overlay_enter();
  rs_program_free(program);
  vm_overlay_leave();
#else
  rs_program_free(program);
#endif
}

void rs_vm_init(RSVM* vm) {
  if (!vm) {
    return;
  }
  /* Keep startup resident-safe: vars helpers live in overlay2 on C64. */
  memset(&vm->vars, 0, sizeof(vm->vars));
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
#if RS_C64_OVERLAY_RUNTIME
  if (rs_overlay_active()) {
    if (rs_overlay_prepare_exec() == 0) {
      vm_overlay_enter();
      rs_vars_free(&vm->vars);
      vm_overlay_leave();
      return;
    }
  }
  /* If overlays are unavailable, avoid jumping into unloaded code. */
  memset(&vm->vars, 0, sizeof(vm->vars));
  return;
#endif
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

static int vm_write_line(RSVM* vm, const char* line) {
  if (!vm || !vm->write_line) {
    return 0;
  }
  g_rs_vm_output_kind = RS_VM_OUTPUT_RENDER;
  if (line) {
    return vm->write_line(vm->write_user, line);
  }
  return vm->write_line(vm->write_user, "");
}

static int vm_write_prt_line(RSVM* vm, const char* line) {
  if (!vm || !vm->write_line) {
    return 0;
  }
  g_rs_vm_output_kind = RS_VM_OUTPUT_PRT;
  if (line) {
    return vm->write_line(vm->write_user, line);
  }
  return vm->write_line(vm->write_user, "");
}

static const char* vm_value_cstr(const RSValue* v) {
  if (v->tag == RS_VAL_STR) {
    return v->as.str.bytes;
  }
  if (rs_format_value(v, rs_vm_fmt_buf, sizeof(rs_vm_fmt_buf)) < 0) {
    return "";
  }
  return rs_vm_fmt_buf;
}

static int vm_tap_append(RSVM* vm, const char* tag, const RSValue* item) {
  unsigned short tag_len;
  unsigned short item_len;
  unsigned short need;
  if (!vm || !item) {
    return -1;
  }
  if (!tag) {
    tag = "TAP";
  }
  if (rs_format_value(item, rs_vm_fmt_buf, sizeof(rs_vm_fmt_buf)) < 0) {
    return -1;
  }
  tag_len = (unsigned short)strlen(tag);
  item_len = (unsigned short)strlen(rs_vm_fmt_buf);
  need = (unsigned short)(tag_len + item_len + 2u);
  if ((unsigned long)vm->tap_len + (unsigned long)need + 1ul > (unsigned long)sizeof(vm->tap_log)) {
    return -1;
  }
  memcpy(vm->tap_log + vm->tap_len, tag, tag_len);
  vm->tap_len = (unsigned short)(vm->tap_len + tag_len);
  vm->tap_log[vm->tap_len++] = ':';
  memcpy(vm->tap_log + vm->tap_len, rs_vm_fmt_buf, item_len);
  vm->tap_len = (unsigned short)(vm->tap_len + item_len);
  vm->tap_log[vm->tap_len++] = '\n';
  vm->tap_log[vm->tap_len] = '\0';
  return 0;
}

static int vm_eval_expr(RSVM* vm,
                        const RSExpr* expr,
                        const RSValue* at,
                        int has_at,
                        RSValue* out,
                        RSError* err);

static int vm_exec_program_internal(RSVM* vm,
                                    const RSProgram* program,
                                    const RSValue* at,
                                    int has_at,
                                    RSValue* out_last,
                                    int* out_has_last,
                                    RSError* err);

static int vm_eval_array(RSVM* vm,
                         const RSExpr* expr,
                         const RSValue* at,
                         int has_at,
                         RSValue* out,
                         RSError* err) {
  unsigned short i;
  RSValue tmp;
  if (rs_value_array_new(out, expr->as.array.count) != 0) {
    vm_err(err, "out of memory");
    return -1;
  }
  for (i = 0; i < expr->as.array.count; ++i) {
    rs_value_init_false(&tmp);
    if (vm_eval_expr(vm, expr->as.array.items[i], at, has_at, &tmp, err) != 0) {
      rs_value_free(&tmp);
      return -1;
    }
    if (rs_value_clone(&out->as.array.items[i], &tmp) != 0) {
      rs_value_free(&tmp);
      vm_err(err, "out of memory");
      return -1;
    }
    rs_value_free(&tmp);
  }
  return 0;
}

static int vm_eval_range(RSVM* vm,
                         const RSExpr* expr,
                         const RSValue* at,
                         int has_at,
                         RSValue* out,
                         RSError* err) {
  RSValue a;
  RSValue b;
  unsigned short n1;
  unsigned short n2;

  rs_value_init_false(&a);
  rs_value_init_false(&b);
  if (vm_eval_expr(vm, expr->as.range.start, at, has_at, &a, err) != 0) {
    rs_value_free(&a);
    rs_value_free(&b);
    return -1;
  }
  if (vm_eval_expr(vm, expr->as.range.end, at, has_at, &b, err) != 0) {
    rs_value_free(&a);
    rs_value_free(&b);
    return -1;
  }
  if (rs_value_to_u16(&a, &n1) != 0 || rs_value_to_u16(&b, &n2) != 0) {
    rs_value_free(&a);
    rs_value_free(&b);
    vm_err(err, "range bounds must be numeric");
    return -1;
  }
  rs_value_free(&a);
  rs_value_free(&b);
  if (rs_value_array_from_u16_range(out, n1, n2) != 0) {
    vm_err(err, "out of memory");
    return -1;
  }
  return 0;
}

static int vm_eval_prop(RSVM* vm,
                        const RSExpr* expr,
                        const RSValue* at,
                        int has_at,
                        RSValue* out,
                        RSError* err) {
  RSValue obj;
  const RSValue* v;
  rs_value_init_false(&obj);
  if (vm_eval_expr(vm, expr->as.prop.target, at, has_at, &obj, err) != 0) {
    rs_value_free(&obj);
    return -1;
  }
  if (obj.tag != RS_VAL_OBJECT) {
    rs_value_free(&obj);
    rs_value_init_false(out);
    return 0;
  }
  v = rs_value_object_get(&obj, expr->as.prop.name);
  if (!v) {
    rs_value_free(&obj);
    rs_value_init_false(out);
    return 0;
  }
  if (rs_value_clone(out, v) != 0) {
    rs_value_free(&obj);
    vm_err(err, "out of memory");
    return -1;
  }
  rs_value_free(&obj);
  return 0;
}

static int vm_eval_index(RSVM* vm,
                         const RSExpr* expr,
                         const RSValue* at,
                         int has_at,
                         RSValue* out,
                         RSError* err) {
  RSValue arr;
  RSValue idx;
  unsigned short n;

  rs_value_init_false(&arr);
  rs_value_init_false(&idx);

  if (vm_eval_expr(vm, expr->as.index.target, at, has_at, &arr, err) != 0) {
    rs_value_free(&arr);
    rs_value_free(&idx);
    return -1;
  }
  if (vm_eval_expr(vm, expr->as.index.index, at, has_at, &idx, err) != 0) {
    rs_value_free(&arr);
    rs_value_free(&idx);
    return -1;
  }

  if (rs_value_to_u16(&idx, &n) != 0) {
    rs_value_free(&arr);
    rs_value_free(&idx);
    vm_err(err, "index must be numeric");
    return -1;
  }

  if (arr.tag == RS_VAL_ARRAY && n < arr.as.array.count) {
    if (rs_value_clone(out, &arr.as.array.items[n]) != 0) {
      rs_value_free(&arr);
      rs_value_free(&idx);
      vm_err(err, "out of memory");
      return -1;
    }
  } else {
    rs_value_init_false(out);
  }

  rs_value_free(&arr);
  rs_value_free(&idx);
  return 0;
}

static int vm_eval_binary(RSVM* vm,
                          const RSExpr* expr,
                          const RSValue* at,
                          int has_at,
                          RSValue* out,
                          RSError* err) {
  RSValue a;
  RSValue b;
  unsigned short n1;
  unsigned short n2;
  int truth;

  rs_value_init_false(&a);
  rs_value_init_false(&b);
  if (vm_eval_expr(vm, expr->as.binary.left, at, has_at, &a, err) != 0) {
    rs_value_free(&a);
    rs_value_free(&b);
    return -1;
  }
  if (vm_eval_expr(vm, expr->as.binary.right, at, has_at, &b, err) != 0) {
    rs_value_free(&a);
    rs_value_free(&b);
    return -1;
  }

  if (expr->as.binary.op == RS_OP_EQ) {
    truth = rs_value_eq(&a, &b);
  } else if (expr->as.binary.op == RS_OP_NEQ) {
    truth = !rs_value_eq(&a, &b);
  } else {
    if (rs_value_to_u16(&a, &n1) != 0 || rs_value_to_u16(&b, &n2) != 0) {
      rs_value_free(&a);
      rs_value_free(&b);
      vm_err(err, "comparison requires numeric operands");
      return -1;
    }
    if (expr->as.binary.op == RS_OP_GT) {
      truth = n1 > n2;
    } else if (expr->as.binary.op == RS_OP_LT) {
      truth = n1 < n2;
    } else if (expr->as.binary.op == RS_OP_GTE) {
      truth = n1 >= n2;
    } else {
      truth = n1 <= n2;
    }
  }

  rs_value_free(&a);
  rs_value_free(&b);
  rs_value_init_bool(out, truth);
  return 0;
}

static int vm_eval_script(RSVM* vm,
                          const RSExpr* expr,
                          const RSValue* at,
                          int has_at,
                          RSValue* out,
                          RSError* err) {
  int has_last;
  if (vm_exec_program_internal(vm, expr->as.script, at, has_at, out, &has_last, err) != 0) {
    return -1;
  }
  if (!has_last) {
    rs_value_init_false(out);
  }
  return 0;
}

static int vm_eval_expr(RSVM* vm,
                        const RSExpr* expr,
                        const RSValue* at,
                        int has_at,
                        RSValue* out,
                        RSError* err) {
  const RSValue* found;

  if (!expr || !out) {
    vm_err(err, "invalid expression");
    return -1;
  }

  rs_value_free(out);
  rs_value_init_false(out);

  if (expr->kind == RS_EXPR_NUMBER) {
    rs_value_init_u16(out, expr->as.number);
    return 0;
  }
  if (expr->kind == RS_EXPR_STRING) {
    if (rs_value_init_string(out, expr->as.text) != 0) {
      vm_err(err, "out of memory");
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
      vm_err(err, "out of memory");
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
      vm_err(err, "out of memory");
      return -1;
    }
    return 0;
  }
  if (expr->kind == RS_EXPR_ARRAY) {
    return vm_eval_array(vm, expr, at, has_at, out, err);
  }
  if (expr->kind == RS_EXPR_RANGE) {
    return vm_eval_range(vm, expr, at, has_at, out, err);
  }
  if (expr->kind == RS_EXPR_PROP) {
    return vm_eval_prop(vm, expr, at, has_at, out, err);
  }
  if (expr->kind == RS_EXPR_INDEX) {
    return vm_eval_index(vm, expr, at, has_at, out, err);
  }
  if (expr->kind == RS_EXPR_BINARY) {
    return vm_eval_binary(vm, expr, at, has_at, out, err);
  }
  if (expr->kind == RS_EXPR_SCRIPT) {
    return vm_eval_script(vm, expr, at, has_at, out, err);
  }

  vm_err(err, "unsupported expression kind");
  return -1;
}

static int vm_collect_add(RSOutCollect* collect, const RSValue* item, RSError* err) {
  if (!collect || !collect->enabled) {
    return 0;
  }
  if (rs_pipe_append_item(&collect->items, &collect->count, item) != 0) {
    vm_err(err, "out of memory");
    return -1;
  }
  return 0;
}

static int vm_eval_args(RSVM* vm,
                        RSExpr** exprs,
                        unsigned short expr_count,
                        const RSValue* at,
                        int has_at,
                        RSValue** out_args,
                        RSError* err) {
  RSValue* args;
  unsigned short i;
  args = 0;
  if (expr_count > 0) {
    args = (RSValue*)malloc(sizeof(RSValue) * expr_count);
    if (!args) {
      vm_err(err, "out of memory");
      return -1;
    }
  }
  for (i = 0; i < expr_count; ++i) {
    rs_value_init_false(&args[i]);
    if (vm_eval_expr(vm, exprs[i], at, has_at, &args[i], err) != 0) {
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

static void vm_free_args(RSValue* args, unsigned short count) {
  unsigned short i;
  if (!args) {
    return;
  }
  for (i = 0; i < count; ++i) {
    rs_value_free(&args[i]);
  }
  free(args);
}

static int vm_append_line_part(const char* s) {
  unsigned short cur;
  unsigned short n;
  if (!s) {
    s = "";
  }
  cur = (unsigned short)strlen(rs_vm_line_buf);
  n = (unsigned short)strlen(s);
  if ((unsigned long)cur + (unsigned long)n + 1ul > (unsigned long)sizeof(rs_vm_line_buf)) {
    return -1;
  }
  memcpy(rs_vm_line_buf + cur, s, n + 1u);
  return 0;
}

static int vm_exec_pipeline_from(RSVM* vm,
                                 const RSPipeline* pipeline,
                                 unsigned short stage_index,
                                 const RSValue* item,
                                 int has_item,
                                 RSOutCollect* collect,
                                 RSError* err);
static int vm_emit_value(RSVM* vm,
                         const RSPipeline* pipeline,
                         unsigned short next_stage_index,
                         const RSValue* value,
                         RSOutCollect* collect,
                         RSError* err);

static int vm_parse_top_args(const RSValue* args,
                             unsigned short arg_count,
                             unsigned short* out_count,
                             unsigned short* out_skip) {
  unsigned short count;
  unsigned short skip;
  if (!out_count || !out_skip || arg_count < 1u) {
    return -1;
  }
  if (rs_value_to_u16(&args[0], &count) != 0) {
    return -1;
  }
  skip = 0u;
  if (arg_count > 1u) {
    if (rs_value_to_u16(&args[1], &skip) != 0) {
      return -1;
    }
  }
  *out_count = count;
  *out_skip = skip;
  return 0;
}

static RSTopStageState* vm_top_state(RSOutCollect* collect, unsigned short stage_index) {
  unsigned char i;
  if (!collect) {
    return 0;
  }
  for (i = 0u; i < collect->top_state_count; ++i) {
    if ((unsigned short)collect->top_stage_index[i] == stage_index) {
      return &collect->top_states[i];
    }
  }
  if (collect->top_state_count >= RS_TOP_STAGE_MAX || stage_index > 255u) {
    return 0;
  }
  i = collect->top_state_count++;
  collect->top_stage_index[i] = (unsigned char)stage_index;
  memset(&collect->top_states[i], 0, sizeof(collect->top_states[i]));
  return &collect->top_states[i];
}

static int vm_cmd_gen(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  unsigned short n;
  unsigned short i;
  RSValue item;
  if (arg_count < 1 || rs_value_to_u16(&args[0], &n) != 0) {
    vm_err(err, "GEN expects numeric count");
    return -1;
  }
  for (i = 1; i <= n; ++i) {
    rs_value_init_false(&item);
    rs_value_init_u16(&item, i);
    if (vm_exec_pipeline_from(vm,
                              pipeline,
                              (unsigned short)(stage_index + 1u),
                              &item,
                              1,
                              collect,
                              err) != 0) {
      rs_value_free(&item);
      return -1;
    }
    rs_value_free(&item);
  }
  return 0;
}

static int vm_cmd_tap(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      const RSValue* current,
                      int has_current,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  const char* tag;
  if (!has_current || !current) {
    vm_err(err, "TAP requires pipeline item");
    return -1;
  }
  if (arg_count > 0) {
    tag = vm_value_cstr(&args[0]);
  } else {
    tag = "TAP";
  }
  if (vm_tap_append(vm, tag, current) != 0) {
    vm_err(err, "tap log overflow");
    return -1;
  }
  return vm_exec_pipeline_from(vm,
                               pipeline,
                               (unsigned short)(stage_index + 1u),
                               current,
                               1,
                               collect,
                               err);
}

static int vm_cmd_prt(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      const RSValue* current,
                      int has_current,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  unsigned short i;
  rs_vm_line_buf[0] = '\0';

  if (arg_count == 0) {
    if (has_current && current) {
      if (rs_format_value(current, rs_vm_line_buf, sizeof(rs_vm_line_buf)) < 0) {
        vm_err(err, "print format overflow");
        return -1;
      }
    }
  } else {
    for (i = 0; i < arg_count; ++i) {
      if (rs_format_value(&args[i], rs_vm_fmt_buf, sizeof(rs_vm_fmt_buf)) < 0) {
        vm_err(err, "print format overflow");
        return -1;
      }
      if (vm_append_line_part(rs_vm_fmt_buf) != 0) {
        vm_err(err, "print line too long");
        return -1;
      }
    }
  }

  if (vm_write_prt_line(vm, rs_vm_line_buf) != 0) {
    vm_err(err, "write failed");
    return -1;
  }
  (void)pipeline;
  (void)stage_index;
  (void)collect;
  return 0;
}

static int vm_cmd_top(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      const RSValue* current,
                      int has_current,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  RSTopStageState* state;
  (void)vm;
  if (!has_current || !current) {
    return 0;
  }
  state = vm_top_state(collect, stage_index);
  if (!state) {
    vm_err(err, "TOP");
    return -1;
  }
  if (!state->init) {
    if (vm_parse_top_args(args, arg_count, &state->count, &state->skip) != 0) {
      vm_err(err, "TOP");
      return -1;
    }
    state->seen = 0u;
    state->init = 1u;
  }
  if (state->seen < state->skip) {
    ++state->seen;
    return 0;
  }
  if ((unsigned short)(state->seen - state->skip) >= state->count) {
    ++state->seen;
    return 0;
  }
  ++state->seen;
  return vm_exec_pipeline_from(vm,
                               pipeline,
                               (unsigned short)(stage_index + 1u),
                               current,
                               1,
                               collect,
                               err);
}

static int vm_sel_name(const RSValue* value, const char** out_name) {
  if (!value || !out_name || value->tag != RS_VAL_STR) {
    return -1;
  }
  *out_name = value->as.str.bytes;
  return 0;
}

static int vm_cmd_sel(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      const RSValue* current,
                      int has_current,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  const RSValue* found;
  RSValue result;
  const char* name;
  unsigned short i;
  int rc;

  (void)vm;
  if (arg_count == 0u) {
    vm_err(err, "SEL");
    return -1;
  }
  for (i = 0u; i < arg_count; ++i) {
    if (vm_sel_name(&args[i], &name) != 0) {
      vm_err(err, "SEL");
      return -1;
    }
  }
  if (!has_current || !current || current->tag != RS_VAL_OBJECT) {
    return 0;
  }

  if (arg_count == 1u) {
    name = args[0].as.str.bytes;
    found = rs_value_object_get(current, name);
    if (!found) {
      return 0;
    }
    return vm_emit_value(vm,
                         pipeline,
                         (unsigned short)(stage_index + 1u),
                         found,
                         collect,
                         err);
  }

  rs_value_init_false(&result);
  if (rs_value_object_new(&result) != 0) {
    vm_err(err, "out of memory");
    return -1;
  }
  for (i = 0u; i < arg_count; ++i) {
    name = args[i].as.str.bytes;
    found = rs_value_object_get(current, name);
    if (!found) {
      rs_value_free(&result);
      return 0;
    }
    if (rs_value_object_set(&result, name, found) != 0) {
      rs_value_free(&result);
      vm_err(err, "out of memory");
      return -1;
    }
  }
  rc = vm_emit_value(vm,
                     pipeline,
                     (unsigned short)(stage_index + 1u),
                     &result,
                     collect,
                     err);
  rs_value_free(&result);
  return rc;
}

static int vm_emit_result(RSVM* vm,
                          const RSPipeline* pipeline,
                          unsigned short next_stage_index,
                          RSValue* result,
                          RSOutCollect* collect,
                          RSError* err) {
  unsigned short i;
  if (next_stage_index < pipeline->count && result->tag == RS_VAL_ARRAY) {
    for (i = 0u; i < result->as.array.count; ++i) {
      if (vm_exec_pipeline_from(vm,
                                pipeline,
                                next_stage_index,
                                &result->as.array.items[i],
                                1,
                                collect,
                                err) != 0) {
        return -1;
      }
    }
    return 0;
  }
  return vm_exec_pipeline_from(vm, pipeline, next_stage_index, result, 1, collect, err);
}

static void vm_cmd_frame_init(RSCommandFrame* frame,
                              RSValue* args,
                              unsigned short arg_count,
                              const RSValue* current,
                              int has_current,
                              RSValue* out,
                              RSError* err) {
  memset(frame, 0, sizeof(*frame));
  frame->args = args;
  frame->arg_count = arg_count;
  frame->item = has_current ? current : 0;
  frame->out = out;
  frame->err = err;
}

static int vm_cmd_drvi(RSVM* vm,
                       const RSPipeline* pipeline,
                       unsigned short stage_index,
                       RSValue* args,
                       unsigned short arg_count,
                       RSOutCollect* collect,
                       RSError* err) {
  RSValue result;
  RSCommandFrame frame;
  int rc;
  (void)vm;
  rs_value_init_false(&result);
  vm_cmd_frame_init(&frame, args, arg_count, 0, 0, &result, err);
  rc = rs_overlay_command_call(RS_CMD_DRVI, RS_CMD_OVL_OP_RUN, &frame);
  if (rc != 0) {
    rs_value_free(&result);
    vm_err(err, rc == -2 ? "DRVI arg" : "DRVI fail");
    return -1;
  }
  rc = vm_exec_pipeline_from(vm,
                             pipeline,
                             (unsigned short)(stage_index + 1u),
                             &result,
                             1,
                             collect,
                             err);
  rs_value_free(&result);
  return rc;
}

static int vm_cmd_lst(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  RSCommandFrame frame;
  RSValue item;
  unsigned short i;
  int rc;
  rs_value_init_false(&item);
  vm_cmd_frame_init(&frame, args, arg_count, 0, 0, &item, err);
  rc = rs_overlay_command_call(RS_CMD_LST, RS_CMD_OVL_OP_BEGIN, &frame);
  if (rc != 0) {
    vm_err(err, rc == -2 ? "LST arg" : "LST fail");
    return -1;
  }
  for (i = 0u; i < frame.count; ++i) {
    frame.index = i;
    rs_value_free(&item);
    rs_value_init_false(&item);
    rc = rs_overlay_command_call(RS_CMD_LST, RS_CMD_OVL_OP_ITEM, &frame);
    if (rc != 0) {
      rs_value_free(&item);
      vm_err(err, "LST fail");
      return -1;
    }
    if (vm_exec_pipeline_from(vm,
                              pipeline,
                              (unsigned short)(stage_index + 1u),
                              &item,
                              1,
                              collect,
                              err) != 0) {
      rs_value_free(&item);
      return -1;
    }
  }
  rs_value_free(&item);
  return 0;
}

static int vm_cmd_ldv(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  RSValue result;
  RSCommandFrame frame;
  unsigned short i;
  int rc;
  rs_value_init_false(&result);
  vm_cmd_frame_init(&frame, args, arg_count, 0, 0, &result, err);
  if (stage_index + 1u < pipeline->count) {
    rc = rs_overlay_command_call(RS_CMD_LDV, RS_CMD_OVL_OP_BEGIN, &frame);
    if (rc != 0) {
      rs_value_free(&result);
      vm_err(err, rc == -2 ? "LDV path" : "LDV parse");
      return -1;
    }
    for (i = 0u; i < frame.count; ++i) {
      frame.index = i;
      rs_value_free(&result);
      rs_value_init_false(&result);
      rc = rs_overlay_command_call(RS_CMD_LDV, RS_CMD_OVL_OP_ITEM, &frame);
      if (rc != 0) {
        rs_value_free(&result);
        vm_err(err, "LDV parse");
        return -1;
      }
      if (vm_exec_pipeline_from(vm,
                                pipeline,
                                (unsigned short)(stage_index + 1u),
                                &result,
                                1,
                                collect,
                                err) != 0) {
        rs_value_free(&result);
        return -1;
      }
    }
    rs_value_free(&result);
    return 0;
  }
  rc = rs_overlay_command_call(RS_CMD_LDV, RS_CMD_OVL_OP_RUN, &frame);
  if (rc != 0) {
    rs_value_free(&result);
    vm_err(err, rc == -2 ? "LDV path" : "LDV parse");
    return -1;
  }
  rc = vm_emit_result(vm,
                      pipeline,
                      (unsigned short)(stage_index + 1u),
                      &result,
                      collect,
                      err);
  rs_value_free(&result);
  return rc;
}

static int vm_cmd_stv(RSVM* vm,
                      const RSPipeline* pipeline,
                      unsigned short stage_index,
                      const RSValue* current,
                      int has_current,
                      RSValue* args,
                      unsigned short arg_count,
                      RSOutCollect* collect,
                      RSError* err) {
  RSValue result;
  RSCommandFrame frame;
  int rc;

  rs_value_init_false(&result);
  vm_cmd_frame_init(&frame, args, arg_count, current, has_current, &result, err);
  rc = rs_overlay_command_call(RS_CMD_STV, RS_CMD_OVL_OP_RUN, &frame);
  if (rc != 0) {
    rs_value_free(&result);
    vm_err(err, rc == -2 ? "STV args" : "STV ser");
    return -1;
  }
  rc = vm_exec_pipeline_from(vm,
                             pipeline,
                             (unsigned short)(stage_index + 1u),
                             &result,
                             1,
                             collect,
                             err);
  rs_value_free(&result);
  return rc;
}

static int vm_exec_command_stage(RSVM* vm,
                                 const RSStage* stage,
                                 const RSPipeline* pipeline,
                                 unsigned short stage_index,
                                 const RSValue* current,
                                 int has_current,
                                 RSOutCollect* collect,
                                 RSError* err) {
  RSValue* args;
  unsigned short arg_count;
  RSCommandId id;
  int rc;

  args = 0;
  arg_count = stage->as.cmd.arg_count;
  if (vm_eval_args(vm, stage->as.cmd.args, arg_count, current, has_current, &args, err) != 0) {
    return -1;
  }

  id = rs_cmd_id(stage->as.cmd.name);
  if (id == RS_CMD_UNKNOWN) {
    vm_free_args(args, arg_count);
    vm_err(err, "unknown command");
    return -1;
  }

  rc = 0;
  if (id == RS_CMD_GEN) {
    rc = vm_cmd_gen(vm, pipeline, stage_index, args, arg_count, collect, err);
  } else if (id == RS_CMD_TAP) {
    rc = vm_cmd_tap(vm, pipeline, stage_index, current, has_current, args, arg_count, collect, err);
  } else if (id == RS_CMD_PRT) {
    rc = vm_cmd_prt(vm, pipeline, stage_index, current, has_current, args, arg_count, collect, err);
  } else if (id == RS_CMD_TOP) {
    rc = vm_cmd_top(vm, pipeline, stage_index, current, has_current, args, arg_count, collect, err);
  } else if (id == RS_CMD_SEL) {
    rc = vm_cmd_sel(vm, pipeline, stage_index, current, has_current, args, arg_count, collect, err);
  } else if (id == RS_CMD_DRVI) {
    rc = vm_cmd_drvi(vm, pipeline, stage_index, args, arg_count, collect, err);
  } else if (id == RS_CMD_LST) {
    rc = vm_cmd_lst(vm, pipeline, stage_index, args, arg_count, collect, err);
  } else if (id == RS_CMD_LDV) {
    rc = vm_cmd_ldv(vm, pipeline, stage_index, args, arg_count, collect, err);
  } else if (id == RS_CMD_STV) {
    rc = vm_cmd_stv(vm, pipeline, stage_index, current, has_current, args, arg_count, collect, err);
  } else {
    rc = -1;
    vm_err(err, "command not implemented");
  }

  vm_free_args(args, arg_count);
  return rc;
}

static int vm_exec_expr_stage(RSVM* vm,
                              const RSStage* stage,
                              const RSPipeline* pipeline,
                              unsigned short stage_index,
                              const RSValue* current,
                              int has_current,
                              RSOutCollect* collect,
                              RSError* err) {
  RSValue value;
  RSValue* expanded;
  unsigned short expanded_count;
  unsigned short i;

  rs_value_init_false(&value);
  if (vm_eval_expr(vm, stage->as.expr, current, has_current, &value, err) != 0) {
    rs_value_free(&value);
    return -1;
  }

  if (rs_pipe_expand_value(&value, &expanded, &expanded_count) != 0) {
    rs_value_free(&value);
    vm_err(err, "out of memory");
    return -1;
  }

  for (i = 0; i < expanded_count; ++i) {
    if (vm_exec_pipeline_from(vm,
                              pipeline,
                              (unsigned short)(stage_index + 1u),
                              &expanded[i],
                              1,
                              collect,
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

static int vm_exec_filter_stage(RSVM* vm,
                                const RSStage* stage,
                                const RSPipeline* pipeline,
                                unsigned short stage_index,
                                const RSValue* current,
                                int has_current,
                                RSOutCollect* collect,
                                RSError* err) {
  RSValue last;
  int has_last;

  if (!has_current || !current) {
    return 0;
  }

  rs_value_init_false(&last);
  has_last = 0;
  if (vm_exec_program_internal(vm, stage->as.script, current, 1, &last, &has_last, err) != 0) {
    rs_value_free(&last);
    return -1;
  }

  if (has_last && rs_value_truthy(&last)) {
    rs_value_free(&last);
    return vm_exec_pipeline_from(vm,
                                 pipeline,
                                 (unsigned short)(stage_index + 1u),
                                 current,
                                 1,
                                 collect,
                                 err);
  }

  rs_value_free(&last);
  return 0;
}

static int vm_exec_foreach_stage(RSVM* vm,
                                 const RSStage* stage,
                                 const RSPipeline* pipeline,
                                 unsigned short stage_index,
                                 const RSValue* current,
                                 int has_current,
                                 RSOutCollect* collect,
                                 RSError* err) {
  RSValue last;
  int has_last;
  int rc;

  if (!has_current || !current) {
    return 0;
  }

  rs_value_init_false(&last);
  has_last = 0;
  if (vm_exec_program_internal(vm, stage->as.script, current, 1, &last, &has_last, err) != 0) {
    rs_value_free(&last);
    return -1;
  }
  if (!has_last) {
    rs_value_free(&last);
    return 0;
  }
  rc = vm_emit_value(vm,
                     pipeline,
                     (unsigned short)(stage_index + 1u),
                     &last,
                     collect,
                     err);
  rs_value_free(&last);
  return rc;
}

static int vm_exec_pipeline_from(RSVM* vm,
                                 const RSPipeline* pipeline,
                                 unsigned short stage_index,
                                 const RSValue* item,
                                 int has_item,
                                 RSOutCollect* collect,
                                 RSError* err) {
  const RSStage* stage;

  if (stage_index >= pipeline->count) {
    if (has_item && item) {
      if (collect && collect->stream_print) {
        if (rs_format_value(item, rs_vm_line_buf, sizeof(rs_vm_line_buf)) < 0) {
          vm_err(err, "print format overflow");
          return -1;
        }
        if (vm_write_line(vm, rs_vm_line_buf) != 0) {
          vm_err(err, "write failed");
          return -1;
        }
        return 0;
      }
      return vm_collect_add(collect, item, err);
    }
    return 0;
  }

  stage = &pipeline->stages[stage_index];
  if (stage->kind == RS_STAGE_CMD) {
    return vm_exec_command_stage(vm, stage, pipeline, stage_index, item, has_item, collect, err);
  }
  if (stage->kind == RS_STAGE_EXPR) {
    return vm_exec_expr_stage(vm, stage, pipeline, stage_index, item, has_item, collect, err);
  }
  if (stage->kind == RS_STAGE_FILTER) {
    return vm_exec_filter_stage(vm, stage, pipeline, stage_index, item, has_item, collect, err);
  }
  if (stage->kind == RS_STAGE_FOREACH) {
    return vm_exec_foreach_stage(vm, stage, pipeline, stage_index, item, has_item, collect, err);
  }

  vm_err(err, "invalid stage kind");
  return -1;
}

static int vm_emit_value(RSVM* vm,
                         const RSPipeline* pipeline,
                         unsigned short next_stage_index,
                         const RSValue* value,
                         RSOutCollect* collect,
                         RSError* err) {
  RSValue* expanded;
  unsigned short count;
  unsigned short i;

  if (rs_pipe_expand_value(value, &expanded, &count) != 0) {
    vm_err(err, "out of memory");
    return -1;
  }

  for (i = 0; i < count; ++i) {
    if (vm_exec_pipeline_from(vm,
                              pipeline,
                              next_stage_index,
                              &expanded[i],
                              1,
                              collect,
                              err) != 0) {
      rs_pipe_free_items(expanded, count);
      return -1;
    }
  }

  rs_pipe_free_items(expanded, count);
  return 0;
}

static int vm_pipeline_run(RSVM* vm,
                           const RSPipeline* pipeline,
                           const RSValue* at,
                           int has_at,
                           RSOutCollect* collect,
                           RSError* err) {
  if (!pipeline || pipeline->count == 0) {
    return 0;
  }
  return vm_exec_pipeline_from(vm, pipeline, 0, at, has_at, collect, err);
}

static void vm_value_take_from_collect(RSValue* out, RSOutCollect* collect) {
  if (!out || !collect) {
    return;
  }

  rs_value_free(out);
  if (collect->count == 0 || !collect->items) {
    rs_value_init_false(out);
    collect->items = 0;
    collect->count = 0;
    return;
  }

  if (collect->count == 1) {
    *out = collect->items[0];
    rs_value_init_false(&collect->items[0]);
    free(collect->items);
    collect->items = 0;
    collect->count = 0;
    return;
  }

  out->tag = RS_VAL_ARRAY;
  out->as.array.count = collect->count;
  out->as.array.items = collect->items;
  collect->items = 0;
  collect->count = 0;
}

static int vm_should_auto_print_pipeline(const RSPipeline* pipeline) {
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

static int vm_auto_print_value(RSVM* vm,
                               const RSValue* value,
                               int has_at,
                               RSError* err) {
  RSValue* expanded;
  unsigned short count;
  unsigned short i;
  if (!vm || !value || has_at) {
    return 0;
  }
  if (rs_pipe_expand_value(value, &expanded, &count) != 0) {
    vm_err(err, "out of memory");
    return -1;
  }
  for (i = 0; i < count; ++i) {
    if (rs_format_value(&expanded[i], rs_vm_line_buf, sizeof(rs_vm_line_buf)) < 0) {
      rs_pipe_free_items(expanded, count);
      vm_err(err, "print format overflow");
      return -1;
    }
    if (vm_write_line(vm, rs_vm_line_buf) != 0) {
      rs_pipe_free_items(expanded, count);
      vm_err(err, "write failed");
      return -1;
    }
  }
  rs_pipe_free_items(expanded, count);
  return 0;
}

static int vm_exec_stmt(RSVM* vm,
                        const RSStmt* stmt,
                        const RSValue* at,
                        int has_at,
                        RSValue* out_last,
                        int* out_has_last,
                        RSError* err) {
  RSOutCollect collect;
  RSValue tmp;
  const RSValue* stored;

  collect.items = 0;
  collect.count = 0;
  collect.enabled = 1;
  collect.stream_print = 0;
  collect.top_state_count = 0u;

  rs_value_init_false(&tmp);

  if (stmt->kind == RS_STMT_ASSIGN) {
    if (stmt->as.assign.rhs_is_pipeline) {
      if (vm_pipeline_run(vm, &stmt->as.assign.pipeline, at, has_at, &collect, err) != 0) {
        rs_pipe_free_items(collect.items, collect.count);
        rs_value_free(&tmp);
        return -1;
      }
      vm_value_take_from_collect(&tmp, &collect);
    } else {
      if (vm_eval_expr(vm, stmt->as.assign.expr, at, has_at, &tmp, err) != 0) {
        rs_value_free(&tmp);
        return -1;
      }
    }

    if (rs_vars_set_owned(&vm->vars, stmt->as.assign.name, &tmp) != 0) {
      rs_value_free(&tmp);
      vm_err(err, "variable table full");
      return -1;
    }

    stored = rs_vars_get(&vm->vars, stmt->as.assign.name);
    if (!stored) {
      vm_err(err, "variable set failed");
      return -1;
    }

    rs_value_free(out_last);
    if (rs_value_clone(out_last, stored) != 0) {
      rs_value_free(&tmp);
      vm_err(err, "out of memory");
      return -1;
    }
    *out_has_last = 1;
    rs_value_free(&tmp);
    return 0;
  }

  if (stmt->kind == RS_STMT_PIPELINE) {
    if (!has_at) {
      collect.enabled = 0;
      collect.stream_print = vm_should_auto_print_pipeline(&stmt->as.pipeline);
    }
    if (vm_pipeline_run(vm, &stmt->as.pipeline, at, has_at, &collect, err) != 0) {
      rs_pipe_free_items(collect.items, collect.count);
      rs_value_free(&tmp);
      return -1;
    }
    if (has_at) {
      if (collect.count == 0) {
        rs_pipe_free_items(collect.items, collect.count);
        rs_value_free(out_last);
        rs_value_init_false(out_last);
        *out_has_last = 0;
      } else {
        vm_value_take_from_collect(out_last, &collect);
        *out_has_last = 1;
      }
      rs_pipe_free_items(collect.items, collect.count);
    } else {
      rs_value_free(out_last);
      rs_value_init_false(out_last);
      *out_has_last = 0;
    }
    rs_value_free(&tmp);
    return 0;
  }

  if (stmt->kind == RS_STMT_EXPR) {
    if (vm_eval_expr(vm, stmt->as.expr, at, has_at, &tmp, err) != 0) {
      rs_value_free(&tmp);
      return -1;
    }
    if (vm_auto_print_value(vm, &tmp, has_at, err) != 0) {
      rs_value_free(&tmp);
      return -1;
    }
    rs_value_free(out_last);
    if (rs_value_clone(out_last, &tmp) != 0) {
      rs_value_free(&tmp);
      vm_err(err, "out of memory");
      return -1;
    }
    *out_has_last = 1;
    rs_value_free(&tmp);
    return 0;
  }

  rs_value_free(&tmp);
  vm_err(err, "unknown statement");
  return -1;
}

static int vm_exec_program_internal(RSVM* vm,
                                    const RSProgram* program,
                                    const RSValue* at,
                                    int has_at,
                                    RSValue* out_last,
                                    int* out_has_last,
                                    RSError* err) {
  unsigned short i;

  if (!vm || !program || !out_last || !out_has_last) {
    vm_err(err, "invalid program args");
    return -1;
  }

  *out_has_last = 0;
  rs_value_free(out_last);
  rs_value_init_false(out_last);

  for (i = 0; i < program->count; ++i) {
    if (vm_exec_stmt(vm, &program->stmts[i], at, has_at, out_last, out_has_last, err) != 0) {
      return -1;
    }
  }

  return 0;
}

int rs_vm_exec_program(RSVM* vm, const RSProgram* program, RSError* err) {
  RSValue last;
  int has_last;
  int rc;

  if (!vm || !program) {
    vm_err(err, "invalid program");
    return -1;
  }

  rs_error_init(err);
  rs_value_init_false(&last);
  has_last = 0;

#if RS_C64_OVERLAY_RUNTIME
  vm_overlay_enter();
  rc = vm_exec_program_internal(vm, program, 0, 0, &last, &has_last, err);
  vm_overlay_leave();
#else
  rc = vm_exec_program_internal(vm, program, 0, 0, &last, &has_last, err);
#endif
  if (rc != 0) {
    rs_value_free(&last);
    return -1;
  }

  rs_value_free(&last);
  return 0;
}

int rs_vm_exec_source(RSVM* vm, const char* source, RSError* err) {
  RSProgram program;
  int parse_rc;
  int rc;

  if (!vm || !source) {
    vm_err(err, "invalid source");
    return -1;
  }

#if RS_C64_OVERLAY_RUNTIME
  rs_overlay_debug_mark('a');
  if (rs_overlay_prepare_parse() != 0) {
    vm_err(err, "overlay parse load");
    return -1;
  }
  rs_overlay_debug_mark('b');
  if (!rs_overlay_is_phase_ready(RS_OVERLAY_PHASE_PARSE)) {
    vm_err(err, "overlay parse state");
    return -1;
  }
#endif

  rs_overlay_debug_mark('c');
  parse_rc = vm_parse_source_guarded(source, &program, err);
  if (parse_rc != 0) {
    return -1;
  }
  rs_overlay_debug_mark('d');

#if RS_C64_OVERLAY_RUNTIME
  rs_overlay_debug_mark('e');
  if (rs_overlay_prepare_exec() != 0) {
    if (rs_overlay_prepare_parse() == 0) {
      vm_program_free_guarded(&program);
    }
    vm_err(err, "overlay exec load");
    return -1;
  }
  rs_overlay_debug_mark('f');
  if (!rs_overlay_is_phase_ready(RS_OVERLAY_PHASE_EXEC)) {
    if (rs_overlay_prepare_parse() == 0) {
      vm_program_free_guarded(&program);
    }
    vm_err(err, "overlay exec state");
    return -1;
  }
#endif

  rs_overlay_debug_mark('g');
  rc = rs_vm_exec_program(vm, &program, err);
  rs_overlay_debug_mark('h');

#if RS_C64_OVERLAY_RUNTIME
  rs_overlay_debug_mark('i');
  if (rs_overlay_prepare_parse() != 0) {
    vm_err(err, "overlay parse restore");
    return -1;
  }
  rs_overlay_debug_mark('j');
  if (!rs_overlay_is_phase_ready(RS_OVERLAY_PHASE_PARSE)) {
    vm_err(err, "overlay parse state");
    return -1;
  }
#endif
  vm_program_free_guarded(&program);
  rs_overlay_debug_mark('k');
  return rc;
}
