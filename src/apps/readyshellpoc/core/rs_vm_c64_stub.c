#include "rs_vm.h"

void rs_vm_init(RSVM* vm) {
  if (!vm) {
    return;
  }
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
  return RS_VM_OUTPUT_RENDER;
}

int rs_vm_exec_program(RSVM* vm, const RSProgram* program, RSError* err) {
  (void)vm;
  (void)program;
  if (err) {
    err->code = RS_ERR_EXEC;
    err->message = "vm not enabled on c64 scaffold";
    err->offset = 0;
    err->line = 1;
    err->column = 1;
  }
  return -1;
}

int rs_vm_exec_source(RSVM* vm, const char* source, RSError* err) {
  (void)vm;
  (void)source;
  if (err) {
    err->code = RS_ERR_EXEC;
    err->message = "vm not enabled on c64 scaffold";
    err->offset = 0;
    err->line = 1;
    err->column = 1;
  }
  return -1;
}
