#ifndef RS_OVERLAY_H
#define RS_OVERLAY_H

typedef void (*RSOverlayProgressFn)(unsigned char stage, void* user);

#define RS_OVERLAY_PHASE_NONE   0u
#define RS_OVERLAY_PHASE_PARSE  1u
#define RS_OVERLAY_PHASE_EXEC   2u
#define RS_OVERLAY_PHASE_CMD3   3u
#define RS_OVERLAY_PHASE_CMD4   4u
#define RS_OVERLAY_PHASE_CMD5   5u
#define RS_OVERLAY_PHASE_CMD6   6u

int rs_overlay_boot(void);
int rs_overlay_boot_with_progress(RSOverlayProgressFn progress, void* user);
int rs_overlay_prepare_parse(void);
int rs_overlay_prepare_exec(void);
int rs_overlay_active(void);
int rs_overlay_is_phase_ready(unsigned char phase);
unsigned char rs_overlay_last_rc(void);
void rs_overlay_debug_mark(unsigned char code);

#endif
