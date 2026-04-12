#include "../rs_overlay.h"
#include "../rs_platform.h"
#include "../rs_memcfg.h"
#include "../../core/rs_cmd_overlay.h"

#ifndef RS_C64_OVERLAY_RUNTIME
#define RS_C64_OVERLAY_RUNTIME 0
#endif

#if RS_C64_OVERLAY_RUNTIME

#include <cbm.h>
#include <device.h>
#include <errno.h>
#include <string.h>

#define RS_OVERLAY_UNIT 8u
#define RS_REU_OVERLAY1_OFF 0x400000ul
#define RS_REU_OVERLAY2_OFF 0x410000ul
#define RS_REU_DBG_HEAD_OFF 0x43F000ul
#define RS_REU_DBG_DATA_OFF 0x43F010ul
#define RS_REU_DBG_DATA_LEN 0x0200u
#define RS_RAM_DBG_HEAD      (*(unsigned char*)0xC7F0)
#define RS_RAM_DBG_BASE      ((unsigned char*)0xC7A0)
#define RS_RAM_DBG_LEN       0x40u
#define RS_OVL_RC_NOT_BOOTED 0xE3u
#define RS_OVL_RC_REU_PARSE  0xE4u
#define RS_OVL_RC_REU_EXEC   0xE5u
#define RS_OVL_RC_REU_REQUIRED 0xE9u
#define RS_OVL_RC_REU_CACHE    0xEAu
#define RS_OVL_RC_REU_CMD      0xEBu

static unsigned short g_overlay1_size = 0u;
static unsigned short g_overlay2_size = 0u;
static unsigned short g_overlay3_size = 0u;
static unsigned short g_overlay4_size = 0u;
static unsigned short g_overlay5_size = 0u;
static unsigned short g_overlay6_size = 0u;
static int g_overlay_loaded = 0;
static int g_overlay_cached_reu = 0;
static unsigned char g_overlay_last_rc = 0u;
static unsigned char g_overlay_active_phase = RS_OVERLAY_PHASE_NONE;
static unsigned short g_dbg_pos = 0u;
static unsigned char g_ram_dbg_pos = 0u;
static unsigned char g_reu_verify_buf[128];
/* 0 = unknown, 1 = disabled (no REU), 2 = enabled */
static unsigned char g_dbg_state = 0u;

static void rs_overlay_progress_tick(RSOverlayProgressFn progress,
                                     void* user,
                                     unsigned char stage) {
  if (progress) {
    progress(stage, user);
  }
}

static void rs_overlay_clear_phase(void) {
  g_overlay_active_phase = RS_OVERLAY_PHASE_NONE;
}

static void rs_overlay_set_phase(unsigned char phase) {
  g_overlay_active_phase = phase;
  g_overlay_last_rc = 0u;
}

static void rs_overlay_dbg_reset(void) {
  unsigned char head[2];
  unsigned char i;
  g_dbg_pos = 0u;
  g_ram_dbg_pos = 0u;
  head[0] = 0u;
  head[1] = 0u;
  (void)rs_reu_write(RS_REU_DBG_HEAD_OFF, head, 2u);
  for (i = 0u; i < RS_RAM_DBG_LEN; ++i) {
    RS_RAM_DBG_BASE[i] = 0u;
  }
  RS_RAM_DBG_HEAD = 0u;
}

static void rs_overlay_dbg_put(unsigned char code) {
  unsigned char b;
  unsigned char head[2];
  RS_RAM_DBG_BASE[g_ram_dbg_pos] = code;
  ++g_ram_dbg_pos;
  if (g_ram_dbg_pos >= RS_RAM_DBG_LEN) {
    g_ram_dbg_pos = 0u;
  }
  RS_RAM_DBG_HEAD = g_ram_dbg_pos;

  if (g_dbg_state == 0u) {
    g_dbg_state = rs_reu_available() ? 2u : 1u;
    if (g_dbg_state == 2u) {
      rs_overlay_dbg_reset();
      RS_RAM_DBG_BASE[g_ram_dbg_pos] = code;
      ++g_ram_dbg_pos;
      if (g_ram_dbg_pos >= RS_RAM_DBG_LEN) {
        g_ram_dbg_pos = 0u;
      }
      RS_RAM_DBG_HEAD = g_ram_dbg_pos;
    }
  }
  if (g_dbg_state != 2u) {
    return;
  }
  b = code;
  (void)rs_reu_write((unsigned long)(RS_REU_DBG_DATA_OFF + (unsigned long)g_dbg_pos), &b, 1u);
  ++g_dbg_pos;
  if (g_dbg_pos >= RS_REU_DBG_DATA_LEN) {
    g_dbg_pos = 0u;
  }
  head[0] = (unsigned char)(g_dbg_pos & 0xFFu);
  head[1] = (unsigned char)((g_dbg_pos >> 8u) & 0xFFu);
  (void)rs_reu_write(RS_REU_DBG_HEAD_OFF, head, 2u);
}

void rs_overlay_debug_mark(unsigned char code) {
  rs_overlay_dbg_put(code);
}

/* Overlay payload lives under BASIC ROM window ($A000-$BFFF): expose RAM while touching it. */
static void rs_overlay_window_enter(void) {
  rs_memcfg_push_ram_under_basic();
  rs_overlay_dbg_put('<');
}

static void rs_overlay_window_leave(void) {
  rs_overlay_dbg_put('>');
  rs_memcfg_pop();
}

extern unsigned char _OVERLAY1_LOAD__[];
extern unsigned char _OVERLAY1_SIZE__[];
extern unsigned char _OVERLAY2_LOAD__[];
extern unsigned char _OVERLAY2_SIZE__[];
extern unsigned char _OVERLAY3_LOAD__[];
extern unsigned char _OVERLAY3_SIZE__[];
extern unsigned char _OVERLAY4_LOAD__[];
extern unsigned char _OVERLAY4_SIZE__[];
extern unsigned char _OVERLAY5_LOAD__[];
extern unsigned char _OVERLAY5_SIZE__[];
extern unsigned char _OVERLAY6_LOAD__[];
extern unsigned char _OVERLAY6_SIZE__[];

extern int rs_vmovl_cmd_drvi(RSCommandFrame* frame);
extern int rs_vmovl_cmd_lst(RSCommandFrame* frame);
extern int rs_vmovl_cmd_ldv(RSCommandFrame* frame);
extern int rs_vmovl_cmd_stv(RSCommandFrame* frame);

static int rs_overlay_name_normalized(const char* path, char* out, unsigned short max) {
  unsigned short i;
  unsigned short n;
  int has_meta;

  if (!path || !out || max < 8u) {
    return -1;
  }

  has_meta = 0;
  n = (unsigned short)strlen(path);
  for (i = 0u; i < n; ++i) {
    if (path[i] == ':' || path[i] == ',') {
      has_meta = 1;
      break;
    }
  }

  if (has_meta) {
    if (n + 1u > max) {
      return -1;
    }
    memcpy(out, path, n + 1u);
    return 0;
  }

  if ((unsigned long)n + 5ul > (unsigned long)max) {
    return -1;
  }
  out[0] = '0';
  out[1] = ':';
  memcpy(out + 2u, path, n);
  out[2u + n] = ',';
  out[3u + n] = 'p';
  out[4u + n] = '\0';
  return 0;
}

static int rs_overlay_try_load(const char* name,
                               unsigned char* dst,
                               unsigned short size,
                               unsigned char unit) {
  char namebuf[40];
  unsigned int loaded;
  if (!name || !dst || size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return 1;
  }
  if (rs_overlay_name_normalized(name, namebuf, sizeof(namebuf)) != 0) {
    g_overlay_last_rc = 0xE2u;
    return 1;
  }
  loaded = cbm_load(namebuf, unit, 0);
  /* Restore default channels after KERNAL load path. */
  cbm_k_clrch();
  cbm_k_clall();
  if (loaded == 0u) {
    g_overlay_last_rc = _oserror ? (unsigned char)_oserror : 0xE6u;
    return 1;
  }
  g_overlay_last_rc = 0u;
  return 0;
}

static int rs_overlay_read_from_reu(unsigned long off, void* dst, unsigned short size) {
  int rc;
  if (!dst || size == 0u) {
    return -1;
  }
  rs_overlay_window_enter();
  rc = rs_reu_read(off, dst, size);
  rs_overlay_window_leave();
  return rc == 0 ? 0 : -1;
}

static int rs_overlay_cache_to_reu(unsigned long off, const void* src, unsigned short size) {
  int rc;
  if (!src || size == 0u) {
    return -1;
  }
  rs_overlay_window_enter();
  rc = rs_reu_write(off, src, size);
  rs_overlay_window_leave();
  return rc == 0 ? 0 : -1;
}

static int rs_overlay_verify_reu(unsigned long off, const unsigned char* src, unsigned short size) {
  unsigned short pos;
  unsigned short n;
  if (!src || size == 0u) {
    return -1;
  }
  rs_overlay_window_enter();
  pos = 0u;
  while (pos < size) {
    n = (unsigned short)(size - pos);
    if (n > (unsigned short)sizeof(g_reu_verify_buf)) {
      n = (unsigned short)sizeof(g_reu_verify_buf);
    }
    if (rs_reu_read(off + (unsigned long)pos, g_reu_verify_buf, n) != 0) {
      rs_overlay_window_leave();
      return -1;
    }
    if (memcmp(src + pos, g_reu_verify_buf, n) != 0) {
      rs_overlay_window_leave();
      return -1;
    }
    pos = (unsigned short)(pos + n);
  }
  rs_overlay_window_leave();
  return 0;
}

static int rs_overlay_load1_disk(void) {
  rs_overlay_dbg_put('1');
  cbm_k_clall();
  if (rs_overlay_try_load("0:rsparser,p", _OVERLAY1_LOAD__, g_overlay1_size, RS_OVERLAY_UNIT) == 0) {
    rs_overlay_dbg_put('a');
    return 0;
  }
  rs_overlay_dbg_put('!');
  return -1;
}

static int rs_overlay_load2_disk(void) {
  rs_overlay_dbg_put('2');
  cbm_k_clall();
  if (rs_overlay_try_load("0:rsvm,p", _OVERLAY2_LOAD__, g_overlay2_size, RS_OVERLAY_UNIT) == 0) {
    rs_overlay_dbg_put('b');
    return 0;
  }
  rs_overlay_dbg_put('!');
  return -1;
}


int rs_overlay_boot_with_progress(RSOverlayProgressFn progress, void* user) {
  unsigned overlay_size;
  int reu_ok;

  /* Clear any stale logical files/channels left by autostart. */
  cbm_k_clall();
  g_dbg_state = 0u;
  rs_overlay_dbg_put('B');
  rs_overlay_progress_tick(progress, user, 1u);

  overlay_size = (unsigned)_OVERLAY1_SIZE__;
  if (overlay_size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return -1;
  }
  g_overlay1_size = (unsigned short)overlay_size;
  overlay_size = (unsigned)_OVERLAY2_SIZE__;
  if (overlay_size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return -1;
  }
  g_overlay2_size = (unsigned short)overlay_size;
  overlay_size = (unsigned)_OVERLAY3_SIZE__;
  if (overlay_size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return -1;
  }
  g_overlay3_size = (unsigned short)overlay_size;
  overlay_size = (unsigned)_OVERLAY4_SIZE__;
  if (overlay_size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return -1;
  }
  g_overlay4_size = (unsigned short)overlay_size;
  overlay_size = (unsigned)_OVERLAY5_SIZE__;
  if (overlay_size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return -1;
  }
  g_overlay5_size = (unsigned short)overlay_size;
  overlay_size = (unsigned)_OVERLAY6_SIZE__;
  if (overlay_size == 0u) {
    g_overlay_last_rc = 0xE1u;
    return -1;
  }
  g_overlay6_size = (unsigned short)overlay_size;
  g_overlay_loaded = 0;
  rs_overlay_clear_phase();
  g_overlay_cached_reu = 0;

  reu_ok = rs_reu_available();
  rs_overlay_dbg_put(reu_ok ? 'Q' : 'q');
  if (!reu_ok) {
    g_overlay_last_rc = RS_OVL_RC_REU_REQUIRED;
    return -1;
  }

  /* Keep C64 filenames short and lowercase on disk images. */
  rs_overlay_progress_tick(progress, user, 2u);
  if (rs_overlay_load1_disk() != 0) {
    g_overlay_loaded = 0;
    rs_overlay_clear_phase();
    return -1;
  }
  rs_overlay_progress_tick(progress, user, 5u);
  rs_overlay_dbg_put('C');
  if (rs_overlay_cache_to_reu(RS_REU_OVERLAY1_OFF, _OVERLAY1_LOAD__, g_overlay1_size) != 0 ||
      rs_overlay_verify_reu(RS_REU_OVERLAY1_OFF, _OVERLAY1_LOAD__, g_overlay1_size) != 0) {
    g_overlay_last_rc = RS_OVL_RC_REU_CACHE;
    rs_overlay_dbg_put('!');
    rs_overlay_dbg_put('Y');
    return -1;
  }
  rs_overlay_progress_tick(progress, user, 3u);
  if (rs_overlay_load2_disk() != 0) {
    g_overlay_loaded = 0;
    rs_overlay_clear_phase();
    return -1;
  }
  if (rs_overlay_cache_to_reu(RS_REU_OVERLAY2_OFF, _OVERLAY2_LOAD__, g_overlay2_size) != 0 ||
      rs_overlay_verify_reu(RS_REU_OVERLAY2_OFF, _OVERLAY2_LOAD__, g_overlay2_size) != 0) {
    g_overlay_last_rc = RS_OVL_RC_REU_CACHE;
    rs_overlay_dbg_put('!');
    rs_overlay_dbg_put('Y');
    return -1;
  }
  rs_overlay_set_phase(RS_OVERLAY_PHASE_EXEC);
  g_overlay_loaded = 1;
  g_overlay_cached_reu = 1;
  rs_overlay_dbg_put('c');
  rs_overlay_progress_tick(progress, user, 6u);
  return 0;
}

int rs_overlay_boot(void) {
  return rs_overlay_boot_with_progress(0, 0);
}

int rs_overlay_prepare_parse(void) {
  rs_overlay_dbg_put('P');
  if (!g_overlay_loaded) {
    g_overlay_last_rc = RS_OVL_RC_NOT_BOOTED;
    rs_overlay_clear_phase();
    rs_overlay_dbg_put('!');
    return -1;
  }
  if (!g_overlay_cached_reu) {
    g_overlay_last_rc = RS_OVL_RC_REU_REQUIRED;
    rs_overlay_clear_phase();
    rs_overlay_dbg_put('!');
    return -1;
  }
  rs_overlay_dbg_put('R');
  if (rs_overlay_read_from_reu(RS_REU_OVERLAY1_OFF, _OVERLAY1_LOAD__, g_overlay1_size) == 0) {
    rs_overlay_set_phase(RS_OVERLAY_PHASE_PARSE);
    rs_overlay_dbg_put('p');
    return 0;
  }
  g_overlay_last_rc = RS_OVL_RC_REU_PARSE;
  rs_overlay_dbg_put('!');
  return -1;
}

int rs_overlay_prepare_exec(void) {
  rs_overlay_dbg_put('E');
  if (!g_overlay_loaded) {
    g_overlay_last_rc = RS_OVL_RC_NOT_BOOTED;
    rs_overlay_clear_phase();
    rs_overlay_dbg_put('!');
    return -1;
  }
  if (!g_overlay_cached_reu) {
    g_overlay_last_rc = RS_OVL_RC_REU_REQUIRED;
    rs_overlay_clear_phase();
    rs_overlay_dbg_put('!');
    return -1;
  }
  rs_overlay_dbg_put('R');
  if (rs_overlay_read_from_reu(RS_REU_OVERLAY2_OFF, _OVERLAY2_LOAD__, g_overlay2_size) == 0) {
    rs_overlay_set_phase(RS_OVERLAY_PHASE_EXEC);
    rs_overlay_dbg_put('e');
    return 0;
  }
  g_overlay_last_rc = RS_OVL_RC_REU_EXEC;
  rs_overlay_dbg_put('!');
  return -1;
}

static int rs_overlay_prepare_command(RSCommandId id) {
  unsigned char* load;
  unsigned short size;
  unsigned char phase;
  const char* name;

  rs_overlay_dbg_put('D');
  if (!g_overlay_loaded) {
    g_overlay_last_rc = RS_OVL_RC_NOT_BOOTED;
    rs_overlay_clear_phase();
    rs_overlay_dbg_put('!');
    return -1;
  }
  if (!g_overlay_cached_reu) {
    g_overlay_last_rc = RS_OVL_RC_REU_REQUIRED;
    rs_overlay_clear_phase();
    rs_overlay_dbg_put('!');
    return -1;
  }

  if (id == RS_CMD_DRVI) {
    name = "0:rsdrvi,p";
    load = _OVERLAY3_LOAD__;
    size = g_overlay3_size;
    phase = RS_OVERLAY_PHASE_CMD3;
  } else if (id == RS_CMD_LST) {
    name = "0:rslst,p";
    load = _OVERLAY4_LOAD__;
    size = g_overlay4_size;
    phase = RS_OVERLAY_PHASE_CMD4;
  } else if (id == RS_CMD_LDV) {
    name = "0:rsldv,p";
    load = _OVERLAY5_LOAD__;
    size = g_overlay5_size;
    phase = RS_OVERLAY_PHASE_CMD5;
  } else if (id == RS_CMD_STV) {
    name = "0:rsstv,p";
    load = _OVERLAY6_LOAD__;
    size = g_overlay6_size;
    phase = RS_OVERLAY_PHASE_CMD6;
  } else {
    g_overlay_last_rc = RS_OVL_RC_REU_CMD;
    rs_overlay_dbg_put('!');
    return -1;
  }

  cbm_k_clall();
  if (rs_overlay_try_load(name, load, size, RS_OVERLAY_UNIT) == 0) {
    rs_overlay_set_phase(phase);
    rs_overlay_dbg_put('d');
    return 0;
  }
  rs_overlay_dbg_put('!');
  return -1;
}

int rs_overlay_command_call(RSCommandId id, unsigned char op, RSCommandFrame* frame) {
  int rc;
  if (!frame) {
    return -1;
  }
  frame->id = id;
  frame->op = op;
  if (rs_overlay_prepare_command(id) != 0) {
    return -1;
  }
  if (id == RS_CMD_DRVI) {
    rc = rs_vmovl_cmd_drvi(frame);
  } else if (id == RS_CMD_LST) {
    rc = rs_vmovl_cmd_lst(frame);
  } else if (id == RS_CMD_LDV) {
    rc = rs_vmovl_cmd_ldv(frame);
  } else if (id == RS_CMD_STV) {
    rc = rs_vmovl_cmd_stv(frame);
  } else {
    rc = -1;
  }
  if (rs_overlay_prepare_exec() != 0) {
    return -1;
  }
  return rc;
}

int rs_overlay_active(void) {
  return g_overlay_loaded;
}

int rs_overlay_is_phase_ready(unsigned char phase) {
  if (!g_overlay_loaded) {
    return 0;
  }
  return g_overlay_active_phase == phase;
}

unsigned char rs_overlay_last_rc(void) {
  return g_overlay_last_rc;
}

#else

int rs_overlay_boot(void) {
  return 0;
}

int rs_overlay_boot_with_progress(RSOverlayProgressFn progress, void* user) {
  (void)progress;
  (void)user;
  return 0;
}

int rs_overlay_prepare_parse(void) {
  return 0;
}

int rs_overlay_prepare_exec(void) {
  return 0;
}

int rs_overlay_command_call(RSCommandId id, unsigned char op, RSCommandFrame* frame) {
  (void)id;
  (void)op;
  (void)frame;
  return -1;
}

int rs_overlay_active(void) {
  return 0;
}

int rs_overlay_is_phase_ready(unsigned char phase) {
  (void)phase;
  return 1;
}

unsigned char rs_overlay_last_rc(void) {
  return 0u;
}

void rs_overlay_debug_mark(unsigned char code) {
  (void)code;
}

#endif
