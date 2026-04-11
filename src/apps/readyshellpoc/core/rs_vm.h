#ifndef RS_VM_H
#define RS_VM_H

#include "rs_errors.h"
#include "rs_parse.h"
#include "rs_vars.h"

typedef int (*RSVMWriteLineFn)(void* user, const char* line);
typedef int (*RSVMFileReadFn)(void* user,
                              const char* path,
                              unsigned char* dst,
                              unsigned short max,
                              unsigned short* out_len);
typedef int (*RSVMFileWriteFn)(void* user,
                               const char* path,
                               const unsigned char* src,
                               unsigned short len);
typedef int (*RSVMListDirFn)(void* user, unsigned char drive, RSValue* out_array);
typedef int (*RSVMDriveInfoFn)(void* user, unsigned char drive, RSValue* out_obj);

typedef struct RSVMPlatform {
  void* user;
  RSVMFileReadFn file_read;
  RSVMFileWriteFn file_write;
  RSVMListDirFn list_dir;
  RSVMDriveInfoFn drive_info;
} RSVMPlatform;

typedef enum {
  RS_VM_OUTPUT_RENDER = 0,
  RS_VM_OUTPUT_PRT = 1
} RSVMOutputKind;

#ifdef __CC65__
#define RS_VM_TAP_LOG_MAX 216
#else
#define RS_VM_TAP_LOG_MAX 8192
#endif

typedef struct RSVM {
  RSVarTable vars;
  RSVMPlatform platform;
  RSVMWriteLineFn write_line;
  void* write_user;
  char tap_log[RS_VM_TAP_LOG_MAX];
  unsigned short tap_len;
} RSVM;

void rs_vm_init(RSVM* vm);
void rs_vm_free(RSVM* vm);
void rs_vm_set_writer(RSVM* vm, RSVMWriteLineFn write_line, void* user);
void rs_vm_set_platform(RSVM* vm, const RSVMPlatform* platform);
void rs_vm_clear_tap_log(RSVM* vm);
const char* rs_vm_get_tap_log(const RSVM* vm);
RSVMOutputKind rs_vm_current_output_kind(void);

int rs_vm_exec_program(RSVM* vm, const RSProgram* program, RSError* err);
int rs_vm_exec_source(RSVM* vm, const char* source, RSError* err);

#endif
