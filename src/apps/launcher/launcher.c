/*
 * launcher.c - Ready OS Launcher (Home Screen)
 * Loads at $1000, uses shim at $C800 for app loading
 *
 * For Commodore 64, compiled with CC65
 */

#include "../../lib/tui.h"
#include "../../lib/resume_state.h"
#include "../../generated/build_version.h"
#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

/*---------------------------------------------------------------------------
 * Shim Interface (shim is at $C800-$C9FF, 512 bytes)
 *---------------------------------------------------------------------------*/

/* Shim jump table addresses (at $C800) */
#define SHIM_LOAD_DISK_RUN   0xC800   /* Load from disk, run */
#define SHIM_LOAD_REU_RUN    0xC803   /* Fetch from REU, run */
#define SHIM_RUN_APP         0xC806   /* Just run app at $1000 */
#define SHIM_PRELOAD_TO_REU  0xC809   /* Preload to REU, return to launcher */
#define SHIM_RETURN_LAUNCHER 0xC80C   /* Return to launcher */
#define SHIM_SWITCH_APP      0xC80F   /* Switch to another app */

/* Shim data addresses (at $C820) */
#define SHIM_APP_BANK   ((unsigned char*)0xC820)   /* Target bank for loading */
#define SHIM_APP_NAMELEN ((unsigned char*)0xC821)
#define SHIM_APP_SIZE   ((unsigned int*)0xC822)
#define SHIM_APP_NAME   ((char*)0xC824)
#define SHIM_CURRENT_BANK ((unsigned char*)0xC834) /* Currently running app's bank */
#define SHIM_LAST_SAVED   ((unsigned char*)0xC835) /* Last app saved by return_to_launcher */
#define SHIM_REU_BITMAP_LO ((unsigned char*)0xC836) /* Bitmap bits 0-7 */
#define SHIM_REU_BITMAP_HI ((unsigned char*)0xC837) /* Bitmap bits 8-15 */
#define SHIM_REU_BITMAP_XHI ((unsigned char*)0xC838) /* Bitmap bits 16-23 */
#define SHIM_LOAD_DISK_DEV_IMM ((unsigned char*)0xC84D) /* A2 xx at $C84C */
#define SHIM_PRELOAD_DEV_IMM   ((unsigned char*)0xC89C) /* A2 xx at $C89B */

/* REU registers for direct access */
#define REU_COMMAND  (*(unsigned char*)0xDF01)
#define REU_C64_LO   (*(unsigned char*)0xDF02)
#define REU_C64_HI   (*(unsigned char*)0xDF03)
#define REU_REU_LO   (*(unsigned char*)0xDF04)
#define REU_REU_HI   (*(unsigned char*)0xDF05)
#define REU_REU_BANK (*(unsigned char*)0xDF06)
#define REU_LEN_LO   (*(unsigned char*)0xDF07)
#define REU_LEN_HI   (*(unsigned char*)0xDF08)

#define REU_CMD_STASH 0x90
#define REU_CMD_FETCH 0x91

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

#define TITLE_Y      0
#define APPS_START_Y 4
#define APPS_HEIGHT  12
#define STATUS_Y     18
#define HELP_Y       22
#define APP_MENU_WIDTH 37
#define APP_NAME_WIDTH 22
#define APP_BIND_LABEL_LEN 8
#define VARIANT_MAX_LEN 31
#define LAUNCHER_NOTICE_LEN 38

/* REU indicator character */
#define REU_INDICATOR 0x2A  /* '*' in PETSCII screen code */

/* App catalog limits */
#define MAX_APPS 24          /* slot 0 + up to 23 app slots */
#define MAX_FILE_LEN 12      /* shim filename buffer is 12 bytes */
#define MAX_NAME_LEN 31
#define MAX_DESC_LEN 38
#define DEFAULT_DRIVE 8
#define APP_CFG_LFN 12
#define APP_CFG_OPEN_SPEC "apps.cfg,s,r"

#ifndef LAUNCHER_CFG_VERBOSE
#define LAUNCHER_CFG_VERBOSE 0
#endif

#define CFG_ERR_OPEN         1
#define CFG_ERR_FORMAT       2
#define CFG_ERR_MISSING_DESC 3
#define CFG_ERR_TOO_MANY     4
#define CFG_ERR_DRIVE        5
#define CFG_ERR_PRG          6
#define CFG_ERR_LABEL        7
#define CFG_ERR_EMPTY        8
#define CFG_ERR_COUNT        9
#define CFG_ERR_PRG_EXT     10
#define CFG_ERR_HOTKEY      11

#define CFG_ERR_PHASE_PARSE    1
#define CFG_ERR_PHASE_VALIDATE 2

#if LAUNCHER_CFG_VERBOSE
#define CFG_TITLE_TEXT "LAUNCHER CONFIG ERROR"
#define CFG_FAIL_TEXT "APP CATALOG FAILED VALIDATION."
#define CFG_CHECK_TEXT "CHECK APPS.CFG ON DISK 8."
#define CFG_PRESS_TEXT "PRESS ANY KEY TO RESET"
#define CFG_PHASE_PARSE_TEXT "PARSE PHASE"
#define CFG_PHASE_VALIDATE_TEXT "VALIDATE PHASE"
#define CFG_SHOW_REASON 1
#define CFG_REASON_OPEN_BASE "OPEN/BASE SLOT ERROR"
#define CFG_REASON_FORMAT "FORMAT/SLOT ERROR"
#define CFG_REASON_DESC "DESCRIPTION/SLOT ERROR"
#define CFG_REASON_CAPACITY "CAPACITY/DUPLICATE ERROR"
#define CFG_REASON_DRIVE "DRIVE FIELD ERROR"
#define CFG_REASON_PRG "PRG FIELD ERROR"
#define CFG_REASON_LABEL "DISPLAY NAME ERROR"
#define CFG_REASON_EMPTY "NO APP ENTRIES"
#define CFG_REASON_COUNT "APP COUNT INVALID"
#define CFG_REASON_PRG_EXT "REMOVE .PRG EXTENSION"
#define CFG_MSG_PRG_EMPTY "PRG NAME IS EMPTY"
#define CFG_MSG_PRG_COMMA "COMMA SUFFIX NOT ALLOWED"
#define CFG_MSG_PRG_EXT "DO NOT USE .PRG EXTENSION"
#define CFG_MSG_PRG_LEN "PRG NAME LENGTH INVALID"
#define CFG_MSG_PRG_CHAR "INVALID CHAR IN PRG NAME"
#define CFG_MSG_MISSING_COLON "MISSING ':' FIELD DELIMITERS"
#define CFG_MSG_DRIVE_EMPTY "DRIVE FIELD IS EMPTY"
#define CFG_MSG_DRIVE_NUMERIC "DRIVE MUST BE NUMERIC"
#define CFG_MSG_DRIVE_RANGE "DRIVE MUST BE 8..11"
#define CFG_MSG_LABEL_EMPTY "DISPLAY NAME IS EMPTY"
#define CFG_MSG_TOO_MANY "TOO MANY APPS IN CATALOG"
#define CFG_MSG_LABEL_LONG "DISPLAY NAME TOO LONG"
#define CFG_MSG_OPEN_FAIL "CANNOT OPEN APPS.CFG ON DRIVE 8"
#define CFG_MSG_DESC_MISSING "MISSING DESCRIPTION LINE"
#define CFG_MSG_NO_APPS "NO APPS FOUND IN APPS.CFG"
#define CFG_MSG_COUNT_RANGE "APP COUNT OUT OF RANGE"
#define CFG_MSG_SLOT0 "SLOT 0 MUST BE LAUNCHER"
#define CFG_MSG_BANK_RANGE "REU BANK OUT OF RANGE"
#define CFG_MSG_FILENAME_EMPTY "EMPTY APP FILENAME SLOT"
#define CFG_MSG_APP_DRIVE_RANGE "APP DRIVE OUT OF RANGE"
#define CFG_MSG_DUP_BANK "DUPLICATE REU BANK"
#define CFG_MSG_HOTKEY_EMPTY "HOTKEY SLOT IS EMPTY"
#define CFG_MSG_HOTKEY_NUMERIC "HOTKEY SLOT MUST BE NUMERIC"
#define CFG_MSG_HOTKEY_RANGE "HOTKEY SLOT MUST BE 1..9"
#define CFG_MSG_HOTKEY_EXTRA "TOO MANY ':' FIELDS"
#else
#define CFG_TITLE_TEXT "CFG ERROR"
#define CFG_FAIL_TEXT "CATALOG VALIDATION FAIL"
#define CFG_CHECK_TEXT "CHECK APPS.CFG D8."
#define CFG_PRESS_TEXT "PRESS KEY TO RESET"
#define CFG_PHASE_PARSE_TEXT "PARSE"
#define CFG_PHASE_VALIDATE_TEXT "VALIDATE"
#define CFG_SHOW_REASON 0
#endif

/* REU bank assignments */
#define REU_BANK_LAUNCHER  0   /* Bank 0 reserved for launcher self-save by shim */
#define LAUNCHER_RESUME_SCHEMA 3

/* App save size - must include code + data + BSS */
#define APP_SAVE_SIZE 0xB600  /* $1000-$C5FF (46KB) */
/* Valid app load range from cfg/ready_app.cfg: $1000-$C5FF */
#define APP_LOAD_START    0x1000
#define APP_LOAD_END_EXCL 0xC600

/*---------------------------------------------------------------------------
 * Static variables
 *---------------------------------------------------------------------------*/

static const char *app_names[MAX_APPS];
static const char *app_descs[MAX_APPS];
static const char *app_files[MAX_APPS];
static unsigned char app_banks[MAX_APPS];
static unsigned char app_drives[MAX_APPS];
static unsigned char app_default_slots[MAX_APPS];
static char app_name_buf[MAX_APPS][MAX_NAME_LEN + 1];
static char app_desc_buf[MAX_APPS][MAX_DESC_LEN + 1];
static char app_file_buf[MAX_APPS][MAX_FILE_LEN + 1];
static unsigned char app_count;

typedef struct {
    unsigned char selected;
    unsigned char scroll_offset;
    unsigned char reserved1;
    unsigned char reserved2;
} LauncherResumeV1;

/* Track loaded apps */
static unsigned char apps_loaded[MAX_APPS];
static unsigned int app_sizes[MAX_APPS];
static LauncherResumeV1 launcher_resume_blob;
static unsigned char resume_ready;
static unsigned char launcher_cfg_load_all_to_reu;
static char launcher_variant_name[VARIANT_MAX_LEN + 1];
static char launcher_variant_boot_name[VARIANT_MAX_LEN + 1];
static char launcher_runappfirst_prg[MAX_FILE_LEN + 1];
static char launcher_notice[LAUNCHER_NOTICE_LEN + 1];
static unsigned char launcher_notice_color = TUI_COLOR_GRAY3;

/* Menu state */
static TuiMenu menu;
static unsigned char running;
static unsigned char slot_contract_ok = 1;
static unsigned char cfg_err_phase = 0;
#if LAUNCHER_CFG_VERBOSE
static char cfg_err_line[39];
static char cfg_err_prg[16];
static char cfg_err_reason[39];
#endif

/* Forward declarations for shared draw helpers */
static void draw_drive_field(unsigned int screen_offset, unsigned char drive);
static void draw_drive_prefixed_name(unsigned char x,
                                     unsigned char y,
                                     unsigned char index,
                                     unsigned char name_color,
                                     unsigned char name_maxlen);
static void launcher_sync_visible_window(void);
static unsigned char validate_slot_contract(unsigned char *detail_a,
                                            unsigned char *detail_b,
                                            unsigned char *detail_c);
static unsigned char load_all_to_reu_internal(unsigned char interactive);
static void launch_app(unsigned char index);

/* Launcher does not use F2/F4 global app cycling, but tui_hotkeys.c expects
 * these entry points when linked. Keep tiny local stubs instead of pulling in
 * the full nav micromodule. */
unsigned char tui_get_next_app(unsigned char current_bank) {
    (void)current_bank;
    return 0;
}

unsigned char tui_get_prev_app(unsigned char current_bank) {
    (void)current_bank;
    return 0;
}

/*---------------------------------------------------------------------------
 * Shim bitmap helpers ($C836-$C838)
 *---------------------------------------------------------------------------*/
static unsigned char shim_bitmap_has_bank(unsigned char bank) {
    if (bank < 8) {
        return (unsigned char)(*SHIM_REU_BITMAP_LO & (unsigned char)(1U << bank));
    }
    if (bank < 16) {
        return (unsigned char)(*SHIM_REU_BITMAP_HI & (unsigned char)(1U << (bank - 8)));
    }
    if (bank < 24) {
        return (unsigned char)(*SHIM_REU_BITMAP_XHI & (unsigned char)(1U << (bank - 16)));
    }
    return 0;
}

static void shim_bitmap_clear_bank(unsigned char bank) {
    if (bank < 8) {
        *SHIM_REU_BITMAP_LO &= (unsigned char)~(unsigned char)(1U << bank);
    } else if (bank < 16) {
        *SHIM_REU_BITMAP_HI &= (unsigned char)~(unsigned char)(1U << (bank - 8));
    } else if (bank < 24) {
        *SHIM_REU_BITMAP_XHI &= (unsigned char)~(unsigned char)(1U << (bank - 16));
    }
}

/*---------------------------------------------------------------------------
 * Sync apps_loaded[] from shim's reu_bitmap ($C836-$C838)
 * The shim updates reu_bitmap whenever an app is stashed to REU,
 * so this reflects the actual REU contents.
 *---------------------------------------------------------------------------*/
static void sync_from_reu_bitmap(void) {
    unsigned char i;
    unsigned char bank;
    unsigned char last_saved;

    /* Resilience: if return_to_launcher recorded a saved bank but bitmap
     * wasn't updated (e.g., interrupted path), heal bitmap from last_saved. */
    last_saved = *SHIM_LAST_SAVED;
    if (last_saved < 24) {
        if (last_saved < 8) {
            *SHIM_REU_BITMAP_LO |= (unsigned char)(1U << last_saved);
        } else if (last_saved < 16) {
            *SHIM_REU_BITMAP_HI |= (unsigned char)(1U << (last_saved - 8));
        } else {
            *SHIM_REU_BITMAP_XHI |= (unsigned char)(1U << (last_saved - 16));
        }
    }

    for (i = 1; i < app_count; ++i) {
        bank = app_banks[i];
        if (bank != 0) {
            if (shim_bitmap_has_bank(bank)) {
                apps_loaded[i] = 1;
                app_sizes[i] = APP_SAVE_SIZE;
            } else {
                apps_loaded[i] = 0;
                app_sizes[i] = 0;
            }
        }
    }

    /* Clear the last_saved flag - we've synced state */
    *SHIM_LAST_SAVED = 0xFF;
}

/*---------------------------------------------------------------------------
 * Slot contract helpers
 *---------------------------------------------------------------------------*/
static void compute_required_slot_bitmap(unsigned char *expected_lo,
                                         unsigned char *expected_hi,
                                         unsigned char *expected_xhi) {
    unsigned char i;
    unsigned char bank;
    unsigned char lo = 0;
    unsigned char hi = 0;
    unsigned char xhi = 0;

    for (i = 1; i < app_count; ++i) {
        bank = app_banks[i];
        if (bank < 8) {
            lo |= (unsigned char)(1U << bank);
        } else if (bank < 16) {
            hi |= (unsigned char)(1U << (bank - 8));
        } else if (bank < 24) {
            xhi |= (unsigned char)(1U << (bank - 16));
        }
    }

    *expected_lo = lo;
    *expected_hi = hi;
    *expected_xhi = xhi;
}

static unsigned char required_slots_loaded(void) {
    unsigned char expected_lo;
    unsigned char expected_hi;
    unsigned char expected_xhi;

    compute_required_slot_bitmap(&expected_lo, &expected_hi, &expected_xhi);
    return (unsigned char)(((*SHIM_REU_BITMAP_LO & expected_lo) == expected_lo) &&
                           ((*SHIM_REU_BITMAP_HI & expected_hi) == expected_hi) &&
                           ((*SHIM_REU_BITMAP_XHI & expected_xhi) == expected_xhi));
}

static unsigned char is_space_char(char ch) {
    return (unsigned char)(ch == ' ' || ch == '\t');
}

static void trim_in_place(char *s) {
    unsigned int start = 0;
    unsigned int len;

    while (s[start] != 0 && is_space_char(s[start])) {
        ++start;
    }
    if (start != 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }

    len = strlen(s);
    while (len > 0 && is_space_char(s[len - 1])) {
        --len;
    }
    s[len] = 0;
}

static void lowercase_in_place(char *s) {
    unsigned int i;
    for (i = 0; s[i] != 0; ++i) {
        if (s[i] >= 'A' && s[i] <= 'Z') {
            s[i] = (char)(s[i] + ('a' - 'A'));
        }
    }
}

static void copy_text_limit(char *dst, unsigned char cap, const char *src) {
    strncpy(dst, src, (unsigned int)(cap - 1));
    dst[cap - 1] = 0;
}

static unsigned char split_key_value(char *line, char **out_key, char **out_value) {
    char *eq = strchr(line, '=');
    if (eq == 0) {
        return 0;
    }
    *eq = 0;
    *out_key = line;
    *out_value = eq + 1;
    trim_in_place(*out_key);
    trim_in_place(*out_value);
    return 1;
}

static unsigned char is_blank_or_comment(const char *s) {
    return (unsigned char)(s[0] == 0 || s[0] == '#' || s[0] == ';');
}

static unsigned char cfg_read_line(char *out, unsigned char cap) {
    unsigned char ch;
    unsigned char raw;
    unsigned char len = 0;
    int n;

    while (1) {
        n = cbm_read(APP_CFG_LFN, &ch, 1);
        if (n <= 0) {
            if (len == 0) {
                out[0] = 0;
                return 0;
            }
            break;
        }

        raw = ch;
        ch &= 0x7F;
        if (raw == 0xA4 || ch == 0x5F) {
            ch = '_';
        }
        if (ch == 0x0A && len == 0) {
            continue;
        }
        if (ch == 0x0D || ch == 0x0A) {
            break;
        }
        if (ch == 0) {
            continue;
        }
        if (len < (unsigned char)(cap - 1)) {
            out[len++] = (char)ch;
        }
    }

    out[len] = 0;
    return 1;
}

static unsigned char is_valid_prg_char(unsigned char ch) {
    if (ch >= 'a' && ch <= 'z') return 1;
    if (ch >= '0' && ch <= '9') return 1;
    if (ch == '_' || ch == '-' || ch == '.') return 1;
    return 0;
}

#if LAUNCHER_CFG_VERBOSE
static void copy_text_cap(char *dst, unsigned char cap, const char *src) {
    strncpy(dst, src, (unsigned int)(cap - 1));
    dst[cap - 1] = 0;
}
#endif

static unsigned char ends_with_suffix(const char *s, const char *suffix) {
    unsigned int s_len = strlen(s);
    unsigned int suf_len = strlen(suffix);
    unsigned int i;

    if (s_len < suf_len) {
        return 0;
    }

    for (i = 0; i < suf_len; ++i) {
        if ((unsigned char)s[s_len - suf_len + i] != (unsigned char)suffix[i]) {
            return 0;
        }
    }
    return 1;
}

#if LAUNCHER_CFG_VERBOSE
static void clear_cfg_diag(void) {
    cfg_err_line[0] = 0;
    cfg_err_prg[0] = 0;
    cfg_err_reason[0] = 0;
}

static void set_cfg_reason(const char *msg) {
    copy_text_cap(cfg_err_reason, (unsigned char)sizeof(cfg_err_reason), msg);
}
#else
#define clear_cfg_diag() ((void)0)
#define set_cfg_reason(msg) ((void)0)
#endif

static unsigned char normalize_prg_field(char *field_prg,
                                         char *out_prg,
                                         unsigned char *out_detail) {
    unsigned char i;
    unsigned char prg_len;
    char *comma;

    *out_detail = 0;
#if LAUNCHER_CFG_VERBOSE
    cfg_err_prg[0] = 0;
#endif

    trim_in_place(field_prg);
#if LAUNCHER_CFG_VERBOSE
    copy_text_cap(cfg_err_prg, (unsigned char)sizeof(cfg_err_prg), field_prg);
#endif
    prg_len = (unsigned char)strlen(field_prg);
    if (prg_len == 0) {
        set_cfg_reason(CFG_MSG_PRG_EMPTY);
        return CFG_ERR_PRG;
    }

    comma = strchr(field_prg, ',');
    if (comma != 0) {
        *out_detail = (unsigned char)comma[1];
        set_cfg_reason(CFG_MSG_PRG_COMMA);
        return CFG_ERR_PRG;
    }

    if (ends_with_suffix(field_prg, ".prg")) {
        set_cfg_reason(CFG_MSG_PRG_EXT);
        return CFG_ERR_PRG_EXT;
    }

    prg_len = (unsigned char)strlen(field_prg);
    if (prg_len == 0 || prg_len > MAX_FILE_LEN) {
        *out_detail = prg_len;
        set_cfg_reason(CFG_MSG_PRG_LEN);
        return CFG_ERR_PRG;
    }

    for (i = 0; i < prg_len; ++i) {
        if (!is_valid_prg_char((unsigned char)field_prg[i])) {
            *out_detail = (unsigned char)field_prg[i];
            set_cfg_reason(CFG_MSG_PRG_CHAR);
            return CFG_ERR_PRG;
        }
        out_prg[i] = field_prg[i];
    }
    out_prg[prg_len] = 0;
#if LAUNCHER_CFG_VERBOSE
    copy_text_cap(cfg_err_prg, (unsigned char)sizeof(cfg_err_prg), out_prg);
#endif
    return 0;
}

static void catalog_init_defaults(void) {
    unsigned char i;

    for (i = 0; i < MAX_APPS; ++i) {
        apps_loaded[i] = 0;
        app_sizes[i] = 0;
        app_banks[i] = 0;
        app_drives[i] = DEFAULT_DRIVE;
        app_default_slots[i] = 0;
        app_name_buf[i][0] = 0;
        app_desc_buf[i][0] = 0;
        app_file_buf[i][0] = 0;
    }

    launcher_cfg_load_all_to_reu = 0;
    launcher_variant_name[0] = 0;
    launcher_variant_boot_name[0] = 0;
    launcher_runappfirst_prg[0] = 0;
    launcher_notice[0] = 0;
    launcher_notice_color = TUI_COLOR_GRAY3;
    copy_text_limit(launcher_variant_name, sizeof(launcher_variant_name), "readyos");
    strcpy(app_name_buf[0], "LOAD ALL TO REU");
    strcpy(app_desc_buf[0], "Load all apps from disk into REU");
    app_count = 1;
    cfg_err_phase = 0;
    clear_cfg_diag();
}

static void catalog_rebind_views(void) {
    unsigned char i;

    for (i = 0; i < MAX_APPS; ++i) {
        app_names[i] = app_name_buf[i];
        app_descs[i] = app_desc_buf[i];
        app_files[i] = app_file_buf[i];
    }
}

static void launcher_resume_save(unsigned char selected, unsigned char scroll_offset) {
    if (!resume_ready) {
        return;
    }

    launcher_resume_blob.selected = selected;
    launcher_resume_blob.scroll_offset = scroll_offset;
    launcher_resume_blob.reserved1 = 0;
    launcher_resume_blob.reserved2 = 0;
    (void)resume_save(&launcher_resume_blob, sizeof(launcher_resume_blob));
}

static unsigned char launcher_resume_restore(unsigned char *out_selected,
                                             unsigned char *out_scroll_offset) {
    unsigned int payload_len = 0;
    if (!resume_ready) {
        return 0;
    }
    if (!resume_try_load(&launcher_resume_blob, sizeof(launcher_resume_blob), &payload_len)) {
        return 0;
    }
    if (payload_len != sizeof(launcher_resume_blob)) {
        return 0;
    }

    if (out_selected != 0 && launcher_resume_blob.selected < app_count) {
        *out_selected = launcher_resume_blob.selected;
    }
    if (out_scroll_offset != 0) {
        *out_scroll_offset = launcher_resume_blob.scroll_offset;
    }
    return 1;
}

static unsigned char parse_catalog_entry_line(char *line,
                                              unsigned char *out_drive,
                                              char *out_prg,
                                              char *out_label,
                                              unsigned char *out_default_slot,
                                              unsigned char *out_detail) {
    char *first_colon;
    char *second_colon;
    char *third_colon;
    char *field_drive;
    char *field_prg;
    char *field_label;
    char *field_slot = 0;
    unsigned char i;
    unsigned int drive_val = 0;

    first_colon = strchr(line, ':');
    if (first_colon == 0) {
        set_cfg_reason(CFG_MSG_MISSING_COLON);
        return CFG_ERR_FORMAT;
    }
    *first_colon = 0;

    second_colon = strchr(first_colon + 1, ':');
    if (second_colon == 0) {
        set_cfg_reason(CFG_MSG_MISSING_COLON);
        return CFG_ERR_FORMAT;
    }
    *second_colon = 0;

    third_colon = strchr(second_colon + 1, ':');
    if (third_colon != 0) {
        *third_colon = 0;
        field_slot = third_colon + 1;
        if (strchr(field_slot, ':') != 0) {
            set_cfg_reason(CFG_MSG_HOTKEY_EXTRA);
            return CFG_ERR_HOTKEY;
        }
    }

    field_drive = line;
    field_prg = first_colon + 1;
    field_label = second_colon + 1;

    trim_in_place(field_drive);
    trim_in_place(field_prg);
    trim_in_place(field_label);
    if (field_slot != 0) {
        trim_in_place(field_slot);
    }

    if (field_drive[0] == 0) {
        set_cfg_reason(CFG_MSG_DRIVE_EMPTY);
        return CFG_ERR_DRIVE;
    }

    for (i = 0; field_drive[i] != 0; ++i) {
        if (field_drive[i] < '0' || field_drive[i] > '9') {
            *out_detail = (unsigned char)field_drive[i];
            set_cfg_reason(CFG_MSG_DRIVE_NUMERIC);
            return CFG_ERR_DRIVE;
        }
        drive_val = drive_val * 10U + (unsigned int)(field_drive[i] - '0');
    }

    if (drive_val < 8U || drive_val > 11U) {
        *out_detail = (unsigned char)drive_val;
        set_cfg_reason(CFG_MSG_DRIVE_RANGE);
        return CFG_ERR_DRIVE;
    }
    *out_drive = (unsigned char)drive_val;

    {
        unsigned char norm_detail = 0;
        unsigned char norm_rc = normalize_prg_field(field_prg, out_prg, &norm_detail);
        if (norm_rc != 0) {
            *out_detail = norm_detail;
            return norm_rc;
        }
    }

    if (field_label[0] == 0) {
        set_cfg_reason(CFG_MSG_LABEL_EMPTY);
        return CFG_ERR_LABEL;
    }
    strncpy(out_label, field_label, MAX_NAME_LEN);
    out_label[MAX_NAME_LEN] = 0;
    *out_default_slot = 0;

    if (field_slot != 0) {
        if (field_slot[0] == 0) {
            set_cfg_reason(CFG_MSG_HOTKEY_EMPTY);
            return CFG_ERR_HOTKEY;
        }
        if (field_slot[1] != 0) {
            *out_detail = (unsigned char)field_slot[1];
            set_cfg_reason(CFG_MSG_HOTKEY_NUMERIC);
            return CFG_ERR_HOTKEY;
        }
        if (field_slot[0] < '0' || field_slot[0] > '9') {
            *out_detail = (unsigned char)field_slot[0];
            set_cfg_reason(CFG_MSG_HOTKEY_NUMERIC);
            return CFG_ERR_HOTKEY;
        }
        if (field_slot[0] == '0') {
            *out_detail = 0;
            set_cfg_reason(CFG_MSG_HOTKEY_RANGE);
            return CFG_ERR_HOTKEY;
        }
        *out_default_slot = (unsigned char)(field_slot[0] - '0');
    }

    return 0;
}

static unsigned char add_catalog_entry(unsigned char drive,
                                       const char *prg,
                                       const char *label,
                                       const char *desc,
                                       unsigned char default_slot) {
    unsigned char idx;

    if (app_count >= MAX_APPS) {
        set_cfg_reason(CFG_MSG_TOO_MANY);
        return CFG_ERR_TOO_MANY;
    }

    idx = app_count;
    app_banks[idx] = idx;
    app_drives[idx] = drive;
    app_default_slots[idx] = default_slot;

    strncpy(app_file_buf[idx], prg, MAX_FILE_LEN);
    app_file_buf[idx][MAX_FILE_LEN] = 0;

    strncpy(app_name_buf[idx], label, MAX_NAME_LEN);
    app_name_buf[idx][MAX_NAME_LEN] = 0;

    strncpy(app_desc_buf[idx], desc, MAX_DESC_LEN);
    app_desc_buf[idx][MAX_DESC_LEN] = 0;

    ++app_count;
    return 0;
}

static unsigned char load_catalog_from_disk(unsigned char *detail_a,
                                            unsigned char *detail_b,
                                            unsigned char *detail_c) {
    char line[96];
    char *key;
    char *value;
    char pending_prg[MAX_FILE_LEN + 1];
    char pending_label[MAX_NAME_LEN + 1];
    unsigned char pending_drive = 0;
    unsigned char pending_slot = 0;
    unsigned char entry_index = 1;
    unsigned char err;
    unsigned char parse_detail;
    unsigned char section = 0;
    unsigned char pending_desc = 0;

    cfg_err_phase = CFG_ERR_PHASE_PARSE;
    if (cbm_open(APP_CFG_LFN, DEFAULT_DRIVE, 2, APP_CFG_OPEN_SPEC) != 0) {
        *detail_a = 0;
        *detail_b = 0;
        *detail_c = 0;
        set_cfg_reason(CFG_MSG_OPEN_FAIL);
        return CFG_ERR_OPEN;
    }

    while (cfg_read_line(line, sizeof(line))) {
        trim_in_place(line);
        lowercase_in_place(line);
        if (is_blank_or_comment(line)) {
            continue;
        }

#if LAUNCHER_CFG_VERBOSE
        copy_text_cap(cfg_err_line, (unsigned char)sizeof(cfg_err_line), line);
#endif

        if (line[0] == '[') {
            if (pending_desc) {
                cbm_close(APP_CFG_LFN);
                *detail_a = entry_index;
                *detail_b = pending_drive;
                *detail_c = 0;
                set_cfg_reason(CFG_MSG_DESC_MISSING);
                return CFG_ERR_MISSING_DESC;
            }

            if (strcmp(line, "[system]") == 0) {
                section = 1;
            } else if (strcmp(line, "[launcher]") == 0) {
                section = 2;
            } else if (strcmp(line, "[apps]") == 0) {
                section = 3;
            } else {
                section = 0;
            }
            continue;
        }

        if (section == 1 || section == 2) {
            if (!split_key_value(line, &key, &value)) {
                continue;
            }
            if (section == 1) {
                if (strcmp(key, "variant_name") == 0) {
                    if (value[0] != 0) {
                        copy_text_limit(launcher_variant_name,
                                        sizeof(launcher_variant_name), value);
                    }
                } else if (strcmp(key, "variant_boot_name") == 0) {
                    copy_text_limit(launcher_variant_boot_name,
                                    sizeof(launcher_variant_boot_name), value);
                }
            } else {
                if (strcmp(key, "load_all_to_reu") == 0) {
                    launcher_cfg_load_all_to_reu = (unsigned char)(strcmp(value, "1") == 0);
                } else if (strcmp(key, "runappfirst") == 0) {
                    if (value[0] != 0) {
                        parse_detail = 0;
                        err = normalize_prg_field(value, launcher_runappfirst_prg,
                                                  &parse_detail);
                        if (err != 0) {
                            cbm_close(APP_CFG_LFN);
                            *detail_a = err;
                            *detail_b = 0;
                            *detail_c = parse_detail;
                            return err;
                        }
                    }
                }
            }
            continue;
        }

        if (section != 3) {
            continue;
        }

        if (!pending_desc) {
            parse_detail = 0;
            pending_slot = 0;
            err = parse_catalog_entry_line(line, &pending_drive, pending_prg,
                                           pending_label, &pending_slot,
                                           &parse_detail);
            if (err != 0) {
                cbm_close(APP_CFG_LFN);
                *detail_a = err;
                *detail_b = entry_index;
                *detail_c = parse_detail;
                return err;
            }
            pending_desc = 1;
            continue;
        }

        err = add_catalog_entry(pending_drive, pending_prg, pending_label, line,
                                pending_slot);
        if (err != 0) {
            cbm_close(APP_CFG_LFN);
            *detail_a = entry_index;
            *detail_b = err;
            *detail_c = app_count;
            return err;
        }

        pending_desc = 0;
        ++entry_index;
    }

    cbm_close(APP_CFG_LFN);

    if (pending_desc) {
        *detail_a = entry_index;
        *detail_b = pending_drive;
        *detail_c = 0;
        set_cfg_reason(CFG_MSG_DESC_MISSING);
        return CFG_ERR_MISSING_DESC;
    }

    if (launcher_variant_name[0] == 0) {
        copy_text_limit(launcher_variant_name, sizeof(launcher_variant_name), "readyos");
    }

    if (app_count <= 1) {
        *detail_a = 0;
        *detail_b = 0;
        *detail_c = 0;
        set_cfg_reason(CFG_MSG_NO_APPS);
        return CFG_ERR_EMPTY;
    }

    return 0;
}

static unsigned char validate_slot_contract(unsigned char *detail_a,
                                            unsigned char *detail_b,
                                            unsigned char *detail_c) {
    unsigned char i;
    unsigned char j;
    unsigned char bank_i;

    cfg_err_phase = CFG_ERR_PHASE_VALIDATE;
    if (app_count <= 1 || app_count > MAX_APPS) {
        *detail_a = app_count;
        *detail_b = 0;
        *detail_c = 0;
        set_cfg_reason(CFG_MSG_COUNT_RANGE);
        return CFG_ERR_COUNT;
    }

    if (app_banks[0] != 0 || app_files[0][0] != 0) {
        *detail_a = app_banks[0];
        *detail_b = (unsigned char)app_files[0][0];
        *detail_c = 0;
        set_cfg_reason(CFG_MSG_SLOT0);
        return CFG_ERR_OPEN;
    }

    for (i = 1; i < app_count; ++i) {
        bank_i = app_banks[i];
        if (bank_i == 0 || bank_i >= MAX_APPS) {
            *detail_a = i;
            *detail_b = bank_i;
            *detail_c = 0;
            set_cfg_reason(CFG_MSG_BANK_RANGE);
            return CFG_ERR_FORMAT;
        }
        if (app_files[i][0] == 0) {
            *detail_a = i;
            *detail_b = bank_i;
            *detail_c = 0;
            set_cfg_reason(CFG_MSG_FILENAME_EMPTY);
            return CFG_ERR_MISSING_DESC;
        }
        if (app_drives[i] < 8 || app_drives[i] > 11) {
            *detail_a = i;
            *detail_b = app_drives[i];
            *detail_c = 0;
            set_cfg_reason(CFG_MSG_APP_DRIVE_RANGE);
            return CFG_ERR_DRIVE;
        }
        if (app_default_slots[i] > TUI_HOTKEY_SLOT_COUNT) {
            *detail_a = i;
            *detail_b = app_default_slots[i];
            *detail_c = 0;
            set_cfg_reason(CFG_MSG_HOTKEY_RANGE);
            return CFG_ERR_HOTKEY;
        }
        for (j = (unsigned char)(i + 1); j < app_count; ++j) {
            if (app_banks[j] == bank_i) {
                *detail_a = i;
                *detail_b = j;
                *detail_c = bank_i;
                set_cfg_reason(CFG_MSG_DUP_BANK);
                return CFG_ERR_TOO_MANY;
            }
        }
    }

    return 0;
}

static void show_slot_contract_error(unsigned char err,
                                     unsigned char detail_a,
                                     unsigned char detail_b,
                                     unsigned char detail_c) {
    const char *phase_msg;
#if CFG_SHOW_REASON
    const char *fallback_reason;
#else
    (void)err;
#endif

    phase_msg = (cfg_err_phase == CFG_ERR_PHASE_PARSE) ? CFG_PHASE_PARSE_TEXT : CFG_PHASE_VALIDATE_TEXT;
#if CFG_SHOW_REASON
    fallback_reason = "UNKNOWN";
    if (err == CFG_ERR_OPEN) fallback_reason = CFG_REASON_OPEN_BASE;
    else if (err == CFG_ERR_FORMAT) fallback_reason = CFG_REASON_FORMAT;
    else if (err == CFG_ERR_MISSING_DESC) fallback_reason = CFG_REASON_DESC;
    else if (err == CFG_ERR_TOO_MANY) fallback_reason = CFG_REASON_CAPACITY;
    else if (err == CFG_ERR_DRIVE) fallback_reason = CFG_REASON_DRIVE;
    else if (err == CFG_ERR_PRG) fallback_reason = CFG_REASON_PRG;
    else if (err == CFG_ERR_LABEL) fallback_reason = CFG_REASON_LABEL;
    else if (err == CFG_ERR_EMPTY) fallback_reason = CFG_REASON_EMPTY;
    else if (err == CFG_ERR_COUNT) fallback_reason = CFG_REASON_COUNT;
    else if (err == CFG_ERR_PRG_EXT) fallback_reason = CFG_REASON_PRG_EXT;
    else if (err == CFG_ERR_HOTKEY) fallback_reason = "HOTKEY SLOT ERROR";
#endif

    tui_clear(TUI_COLOR_BLUE);
    {
        TuiRect title_box = {1, 0, 38, 3};
        tui_window_title(&title_box, CFG_TITLE_TEXT,
                         TUI_COLOR_LIGHTRED, TUI_COLOR_YELLOW);
    }
    tui_puts(2, 5, CFG_FAIL_TEXT, TUI_COLOR_WHITE);
    tui_puts(2, 7, "ERR:", TUI_COLOR_LIGHTRED);
    tui_print_uint(7, 7, err, TUI_COLOR_LIGHTRED);
    tui_puts(2, 9, "A:", TUI_COLOR_WHITE);
    tui_print_uint(5, 9, detail_a, TUI_COLOR_WHITE);
    tui_puts(11, 9, "B:", TUI_COLOR_WHITE);
    tui_print_uint(14, 9, detail_b, TUI_COLOR_WHITE);
    tui_puts(20, 9, "C:", TUI_COLOR_WHITE);
    tui_print_uint(23, 9, detail_c, TUI_COLOR_WHITE);

    tui_puts(2, 11, phase_msg, TUI_COLOR_YELLOW);
#if CFG_SHOW_REASON
    tui_puts_n(2, 13, "WHY:", 4, TUI_COLOR_LIGHTRED);
    if (cfg_err_reason[0] != 0) {
        tui_puts_n(7, 13, cfg_err_reason, 31, TUI_COLOR_WHITE);
    } else {
        tui_puts_n(7, 13, fallback_reason, 31, TUI_COLOR_WHITE);
    }

    #if LAUNCHER_CFG_VERBOSE
    if (cfg_err_phase == CFG_ERR_PHASE_PARSE) {
        tui_puts_n(2, 15, "LINE:", 5, TUI_COLOR_LIGHTRED);
        tui_puts_n(8, 15, cfg_err_line, 30, TUI_COLOR_WHITE);
        tui_puts_n(2, 16, "PRG:", 4, TUI_COLOR_LIGHTRED);
        tui_puts_n(7, 16, cfg_err_prg, 31, TUI_COLOR_WHITE);
    }
#endif
#endif

    tui_puts(2, 18, CFG_CHECK_TEXT, TUI_COLOR_LIGHTRED);
    tui_puts(2, 22, CFG_PRESS_TEXT, TUI_COLOR_WHITE);
    tui_getkey();
}

/*---------------------------------------------------------------------------
 * Helper: Set shim app name
 *---------------------------------------------------------------------------*/
static void set_shim_name(const char *name) {
    unsigned char len = 0;
    /* Shim filename buffer is $C824-$C82F (12 bytes). */
    while (name[len] && len < 12) {
        SHIM_APP_NAME[len] = name[len];
        ++len;
    }
    *SHIM_APP_NAMELEN = len;
}

/*---------------------------------------------------------------------------
 * Helper: Set shim REU params
 *---------------------------------------------------------------------------*/
static void set_shim_reu(unsigned char bank, unsigned int size) {
    *SHIM_APP_BANK = bank;
    *SHIM_APP_SIZE = size;
}

/*---------------------------------------------------------------------------
 * Helper: Patch shim load/preload device for per-app drive
 *---------------------------------------------------------------------------*/
static void set_shim_drive(unsigned char drive) {
    *SHIM_LOAD_DISK_DEV_IMM = drive;
    *SHIM_PRELOAD_DEV_IMM = drive;
}


/*---------------------------------------------------------------------------
 * Save launcher state to REU bank 0
 *---------------------------------------------------------------------------*/
static void save_launcher_to_reu(void) {
    REU_C64_LO = 0x00;
    REU_C64_HI = 0x10;
    REU_REU_LO = 0x00;
    REU_REU_HI = 0x00;
    REU_REU_BANK = REU_BANK_LAUNCHER;
    REU_LEN_LO = 0x00;
    REU_LEN_HI = 0xB6;  /* $B600 bytes */
    REU_COMMAND = REU_CMD_STASH;
}

/*---------------------------------------------------------------------------
 * Load single app from disk to REU
 * Uses shim's preload routine at $C809 which:
 * 1. Stashes launcher to REU bank 0
 * 2. Loads app from disk to $1000
 * 3. Stashes app to target REU bank
 * 4. Fetches launcher back from REU bank 0
 * 5. Returns via RTS
 *---------------------------------------------------------------------------*/
static unsigned int load_app_to_reu(unsigned char index) {
    const char *filename;
    unsigned char bank;
    unsigned char loaded_in_bitmap;
    unsigned int end_addr;
    unsigned int file_size;

    if (index == 0 || index >= app_count || app_banks[index] == 0) {
        return 0;
    }

    filename = app_files[index];
    bank = app_banks[index];

    /* Set target bank in shim data area */
    *SHIM_APP_BANK = bank;

    /* Set filename in shim data area */
    set_shim_name(filename);

    set_shim_drive(app_drives[index]);

    /* Call shim's preload routine - it handles everything and returns */
    __asm__("jsr $C809");

    /* Read actual file size from shim data area.
     * Shim saves KERNAL LOAD end address at $C830-$C831.
     * If this value is invalid, treat load as failure. */
    end_addr = ((unsigned int)(*(unsigned char*)0xC831) << 8)
             | (*(unsigned char*)0xC830);
    loaded_in_bitmap = shim_bitmap_has_bank(bank);

    if (end_addr <= APP_LOAD_START || end_addr > APP_LOAD_END_EXCL) {
        /* Some preload paths can leave end_addr invalid while still stashing app. */
        if (loaded_in_bitmap) {
            apps_loaded[index] = 1;
            app_sizes[index] = APP_SAVE_SIZE;
            return APP_SAVE_SIZE;
        }
        apps_loaded[index] = 0;
        app_sizes[index] = 0;
        shim_bitmap_clear_bank(bank);
        return 0;
    }

    file_size = end_addr - APP_LOAD_START;
    if (file_size > APP_SAVE_SIZE) {
        if (loaded_in_bitmap) {
            apps_loaded[index] = 1;
            app_sizes[index] = APP_SAVE_SIZE;
            return APP_SAVE_SIZE;
        }
        apps_loaded[index] = 0;
        app_sizes[index] = 0;
        shim_bitmap_clear_bank(bank);
        return 0;
    }

    /* Treat bitmap as authoritative: only mark loaded if target bit is set. */
    loaded_in_bitmap = shim_bitmap_has_bank(bank);
    if (!loaded_in_bitmap) {
        apps_loaded[index] = 0;
        app_sizes[index] = 0;
        return 0;
    }

    /* On return, launcher is back in memory and app is valid in REU. */
    apps_loaded[index] = 1;
    app_sizes[index] = file_size;
    return file_size;
}

static unsigned char missing_list_contains(const unsigned char *list,
                                           unsigned char count,
                                           unsigned char index) {
    unsigned char i;
    for (i = 0; i < count; ++i) {
        if (list[i] == index) {
            return 1;
        }
    }
    return 0;
}

static const char *launcher_resolved_variant_title(void) {
    if (launcher_variant_boot_name[0] != 0) {
        return launcher_variant_boot_name;
    }
    if (launcher_variant_name[0] != 0) {
        return launcher_variant_name;
    }
    return "readyos";
}

static void launcher_set_notice(const char *msg, unsigned char color) {
    copy_text_limit(launcher_notice, sizeof(launcher_notice), msg);
    launcher_notice_color = color;
}

static unsigned char launcher_find_app_by_prg(const char *prg) {
    unsigned char i;

    for (i = 1; i < app_count; ++i) {
        if (strcmp(app_files[i], prg) == 0) {
            return i;
        }
    }
    return 0;
}

/*---------------------------------------------------------------------------
 * Load all apps to REU
 *---------------------------------------------------------------------------*/
static unsigned char load_all_to_reu_internal(unsigned char interactive) {
    unsigned char i;
    unsigned int size;
    unsigned char total_to_load;
    unsigned char loaded_count;
    unsigned char y;
    unsigned char bar_y;
    unsigned char counter_y;
    unsigned char loaded_ok;
    unsigned char retried;
    unsigned char missing_count;
    unsigned char bitmap_complete;
    unsigned char status_x;
    unsigned char done_x;
    unsigned char success;
    static unsigned char missing_slots[MAX_APPS];

    sync_from_reu_bitmap();

    /* Count how many apps need loading */
    total_to_load = 0;
    for (i = 1; i < app_count; ++i) {
        if (app_banks[i] != 0 && !apps_loaded[i]) {
            ++total_to_load;
        }
    }

    if (total_to_load == 0) {
        if (interactive) {
            tui_clear(TUI_COLOR_BLUE);
            tui_puts(4, 10, "ALL APPS ALREADY IN REU!", TUI_COLOR_LIGHTGREEN);
            tui_puts(13, 14, "PRESS ANY KEY...", TUI_COLOR_WHITE);
            tui_getkey();
        }
        return 1;
    }

    tui_clear(TUI_COLOR_BLUE);
    {
        TuiRect title_box = {1, 0, 38, 3};
        tui_window_title(&title_box, "LOADING APPS TO REU",
                         TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    }

    /* Keep full app list visible: counter on row 23, bar on row 24 */
    counter_y = 23;
    bar_y = 24;

    /* Draw empty unified progress bar */
    tui_progress_bar(4, bar_y, 32, 0, total_to_load,
                     TUI_COLOR_LIGHTGREEN, TUI_COLOR_GRAY2);
    tui_puts(4, counter_y, "0/", TUI_COLOR_WHITE);
    tui_print_uint(6, counter_y, total_to_load, TUI_COLOR_WHITE);

    status_x = 24;
    loaded_count = 0;
    missing_count = 0;
    for (i = 1; i < app_count; ++i) {
        if (app_banks[i] == 0) continue;
        if (apps_loaded[i]) continue;

        y = (unsigned char)(4 + loaded_count);

        /* Display app name with LOADING status */
        draw_drive_prefixed_name(4, y, i, TUI_COLOR_CYAN, 16);
        tui_puts(status_x, y, "LOADING...", TUI_COLOR_YELLOW);

        /* Load app; retry once if target bank bit was not set. */
        retried = 0;
        size = load_app_to_reu(i);
        sync_from_reu_bitmap();
        loaded_ok = apps_loaded[i];
        if (!loaded_ok) {
            size = load_app_to_reu(i);
            sync_from_reu_bitmap();
            loaded_ok = apps_loaded[i];
            retried = 1;
        }

        /* Update line status */
        tui_puts_n(status_x, y, "", 16, TUI_COLOR_WHITE);  /* Clear status area */
        draw_drive_prefixed_name(4, y, i, TUI_COLOR_CYAN, APP_NAME_WIDTH);

        if (loaded_ok) {
            tui_puts(30, y, "OK", TUI_COLOR_LIGHTGREEN);
            tui_print_uint(33, y, size / 1024, TUI_COLOR_GRAY3);
            tui_puts(37, y, "KB", TUI_COLOR_GRAY3);
            if (retried) {
                tui_puts(status_x, y, "RETRY", TUI_COLOR_YELLOW);
            }
        } else {
            tui_puts(30, y, "FAIL", TUI_COLOR_LIGHTRED);
            if (!missing_list_contains(missing_slots, missing_count, i)) {
                missing_slots[missing_count] = i;
                ++missing_count;
            }
        }

        ++loaded_count;

        /* Update unified progress bar */
        tui_progress_bar(4, bar_y, 32, loaded_count, total_to_load,
                         TUI_COLOR_LIGHTGREEN, TUI_COLOR_GRAY2);
        tui_puts_n(4, counter_y, "", 8, TUI_COLOR_WHITE);  /* Clear counter */
        tui_print_uint(4, counter_y, loaded_count, TUI_COLOR_WHITE);
        tui_puts(5, counter_y, "/", TUI_COLOR_WHITE);
        tui_print_uint(6, counter_y, total_to_load, TUI_COLOR_WHITE);
    }

    /* Final authoritative check: all catalog-assigned banks must be present. */
    sync_from_reu_bitmap();
    bitmap_complete = required_slots_loaded();

    for (i = 1; i < app_count; ++i) {
        if (!apps_loaded[i] && !missing_list_contains(missing_slots, missing_count, i)) {
            missing_slots[missing_count] = i;
            ++missing_count;
        }
    }

    tui_puts_n(2, counter_y, "", 38, TUI_COLOR_WHITE);
    success = (unsigned char)(missing_count == 0 && bitmap_complete);

    if (success) {
        done_x = 9;
        if (interactive) {
            tui_puts(done_x, counter_y, "DONE! PRESS ANY KEY...", TUI_COLOR_WHITE);
        }
    } else {
        done_x = 4;
        if (interactive) {
            tui_puts(done_x, counter_y, "INCOMPLETE LOAD. PRESS ANY KEY...", TUI_COLOR_LIGHTRED);
        }
    }

    if (interactive) {
        tui_getkey();
    }
    return success;
}

static void load_all_to_reu(void) {
    (void)load_all_to_reu_internal(1);
}

/*---------------------------------------------------------------------------
 * Load selected app to REU (F3)
 *---------------------------------------------------------------------------*/
static void load_selected_to_reu(unsigned char index) {
    unsigned int size;
    unsigned char loaded_ok;

    if (index == 0 || index >= app_count || app_banks[index] == 0) {
        return;
    }

    sync_from_reu_bitmap();
    if (apps_loaded[index]) {
        return;
    }

    tui_clear(TUI_COLOR_BLUE);
    {
        TuiRect title_box = {1, 0, 38, 3};
        tui_window_title(&title_box, "LOADING TO REU",
                         TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    }

    /* Display app name */
    draw_drive_prefixed_name(4, 5, index, TUI_COLOR_CYAN, 20);
    tui_puts(4, 7, "LOADING...", TUI_COLOR_YELLOW);

    /* Load the app (blocking) */
    size = load_app_to_reu(index);
    sync_from_reu_bitmap();
    loaded_ok = apps_loaded[index];

    /* Show result */
    tui_puts_n(4, 7, "", 16, TUI_COLOR_WHITE);

    if (loaded_ok) {
        tui_puts(4, 7, "OK", TUI_COLOR_LIGHTGREEN);
        tui_puts(8, 7, "-", TUI_COLOR_WHITE);
        tui_print_uint(10, 7, size / 1024, TUI_COLOR_GRAY3);
        tui_puts(15, 7, "KB", TUI_COLOR_GRAY3);
    } else {
        tui_puts(4, 7, "FAILED", TUI_COLOR_LIGHTRED);
    }

    tui_puts(4, 10, "PRESS ANY KEY...", TUI_COLOR_WHITE);
    tui_getkey();
}

/*---------------------------------------------------------------------------
 * Launch app from REU (fast)
 *---------------------------------------------------------------------------*/
static void launch_from_reu(unsigned char index) {
    unsigned char bank;
    unsigned int size;

    if (!apps_loaded[index]) return;

    bank = app_banks[index];
    size = app_sizes[index];

    tui_clear(TUI_COLOR_BLUE);
    tui_puts(8, 12, "LAUNCHING FROM REU...", TUI_COLOR_CYAN);

    launcher_resume_save(index, menu.scroll_offset);

    /* Save current launcher state to REU bank 0 first */
    save_launcher_to_reu();

    /* Set REU params in shim */
    set_shim_reu(bank, size);
    set_shim_drive(app_drives[index]);

    /* Set current app bank so apps can return to launcher */
    *SHIM_CURRENT_BANK = bank;

    /* Call shim to DMA from REU and run - jump table at $C803 */
    __asm__("jmp $C803");
}

/*---------------------------------------------------------------------------
 * Launch app from disk (slow)
 *---------------------------------------------------------------------------*/
static void launch_from_disk(unsigned char index) {
    const char *filename;

    if (app_files[index][0] == 0) return;

    filename = app_files[index];

    tui_clear(TUI_COLOR_BLUE);
    tui_puts(4, 5, "LOADING FROM DISK:", TUI_COLOR_WHITE);
    draw_drive_prefixed_name(23, 5, index, TUI_COLOR_CYAN, 12);
    tui_puts(4, 7, "PLEASE WAIT...", TUI_COLOR_YELLOW);

    /* Set filename in shim */
    set_shim_name(filename);
    set_shim_drive(app_drives[index]);

    launcher_resume_save(index, menu.scroll_offset);

    /* Save launcher to REU first so we can return to it */
    save_launcher_to_reu();

    /* Set current app bank so apps can return to launcher */
    *SHIM_CURRENT_BANK = app_banks[index];

    /* Call shim to load and run - use jump table entry at $C800 */
    __asm__("jmp $C800");
}

/*---------------------------------------------------------------------------
 * Launch app (from REU if available, else disk)
 *---------------------------------------------------------------------------*/
static void launch_app(unsigned char index) {
    if (!slot_contract_ok) {
        return;
    }

    if (index == 0) {
        load_all_to_reu();
        return;
    }

    if (index >= app_count || app_banks[index] == 0) {
        tui_puts(4, STATUS_Y + 2, "NOT AVAILABLE", TUI_COLOR_LIGHTRED);
        return;
    }

    /* Bitmap is authoritative for REU presence. */
    sync_from_reu_bitmap();

    if (apps_loaded[index]) {
        launch_from_reu(index);
    } else {
        launch_from_disk(index);
    }
}

/*---------------------------------------------------------------------------
 * Drawing
 *---------------------------------------------------------------------------*/

static void draw_header(void) {
    TuiRect box = {0, 0, 40, 3};
    const char *variant = launcher_resolved_variant_title();
    unsigned char len;
    unsigned char x;

    tui_window_title(&box, READYOS_TITLE_TEXT, TUI_COLOR_LIGHTBLUE, TUI_COLOR_YELLOW);
    len = (unsigned char)strlen(variant);
    if (len > 38) {
        len = 38;
    }
    x = (unsigned char)((40 - len) / 2);
    tui_puts_n(x, 1, variant, len, TUI_COLOR_LIGHTGREEN);
}

static void draw_status(void) {
    TuiRect box = {0, STATUS_Y, 40, 3};
    tui_window(&box, TUI_COLOR_LIGHTBLUE);

    tui_puts(2, STATUS_Y + 1, "REU: 16MB", TUI_COLOR_WHITE);

    /* Legend for REU indicator */
    tui_putc(20, STATUS_Y + 1, REU_INDICATOR, TUI_COLOR_LIGHTGREEN);
    tui_puts(21, STATUS_Y + 1, "=IN REU", TUI_COLOR_GRAY3);
}

static void draw_help(void) {
    tui_puts(1, HELP_Y, "RET:LAUNCH F3:LOAD  F1:LOAD ALL", TUI_COLOR_GRAY3);
    tui_puts(1, HELP_Y + 1, "F2:NEXT APP  F4:PREV  STOP:QUIT", TUI_COLOR_GRAY3);
}

static void draw_notice(void) {
    tui_puts_n(1, (unsigned char)(HELP_Y - 1), launcher_notice,
               LAUNCHER_NOTICE_LEN, launcher_notice_color);
}

static void draw_drive_field(unsigned int screen_offset, unsigned char drive) {
    unsigned char tens = 32;
    unsigned char ones;

    if (drive >= 10) {
        tens = (unsigned char)('0' + (drive / 10));
        ones = (unsigned char)('0' + (drive % 10));
    } else {
        ones = (unsigned char)('0' + drive);
    }

    TUI_SCREEN[screen_offset] = tens;
    TUI_COLOR_RAM[screen_offset] = TUI_COLOR_GRAY2;
    TUI_SCREEN[screen_offset + 1] = ones;
    TUI_COLOR_RAM[screen_offset + 1] = TUI_COLOR_GRAY2;
}

static void draw_drive_prefixed_name(unsigned char x,
                                     unsigned char y,
                                     unsigned char index,
                                     unsigned char name_color,
                                     unsigned char name_maxlen) {
    unsigned int screen_offset;

    if (index >= app_count) {
        tui_puts_n(x, y, "", name_maxlen, name_color);
        return;
    }

    if (app_banks[index] == 0) {
        tui_puts_n(x, y, app_names[index], name_maxlen, name_color);
        return;
    }

    screen_offset = (unsigned int)y * 40 + x;
    TUI_SCREEN[screen_offset] = 32;
    TUI_COLOR_RAM[screen_offset] = name_color;
    draw_drive_field(screen_offset + 1, app_drives[index]);
    TUI_SCREEN[screen_offset + 3] = 32;
    TUI_COLOR_RAM[screen_offset + 3] = name_color;
    tui_puts_n((unsigned char)(x + 4), y, app_names[index], name_maxlen, name_color);
}

static void clear_menu_span(unsigned int start, unsigned char len, unsigned char color) {
    unsigned char pos;

    for (pos = 0; pos < len; ++pos) {
        TUI_SCREEN[start + pos] = 32;
        TUI_COLOR_RAM[start + pos] = color;
    }
}

static unsigned char launcher_hotkey_slot_for_bank(unsigned char bank) {
    unsigned char slot;

    if (bank == 0) {
        return 0;
    }

    for (slot = 1; slot <= TUI_HOTKEY_SLOT_COUNT; ++slot) {
        if (TUI_HOTKEY_BINDINGS[(unsigned char)(slot - 1)] == bank) {
            return slot;
        }
    }

    return 0;
}

static void draw_binding_tag(unsigned int start, unsigned char slot, unsigned char color) {
    if (slot < 1 || slot > TUI_HOTKEY_SLOT_COUNT) {
        clear_menu_span(start, APP_BIND_LABEL_LEN, color);
        return;
    }

    TUI_SCREEN[start + 0] = tui_ascii_to_screen('(');
    TUI_SCREEN[start + 1] = tui_ascii_to_screen('C');
    TUI_SCREEN[start + 2] = tui_ascii_to_screen('T');
    TUI_SCREEN[start + 3] = tui_ascii_to_screen('R');
    TUI_SCREEN[start + 4] = tui_ascii_to_screen('L');
    TUI_SCREEN[start + 5] = tui_ascii_to_screen('+');
    TUI_SCREEN[start + 6] = tui_ascii_to_screen((unsigned char)('0' + slot));
    TUI_SCREEN[start + 7] = tui_ascii_to_screen(')');
    TUI_COLOR_RAM[start + 0] = color;
    TUI_COLOR_RAM[start + 1] = color;
    TUI_COLOR_RAM[start + 2] = color;
    TUI_COLOR_RAM[start + 3] = color;
    TUI_COLOR_RAM[start + 4] = color;
    TUI_COLOR_RAM[start + 5] = color;
    TUI_COLOR_RAM[start + 6] = color;
    TUI_COLOR_RAM[start + 7] = color;
}

static void draw_menu_item(unsigned char idx) {
    unsigned char row;
    unsigned char y;
    unsigned char color;
    unsigned char prefix;
    unsigned int screen_offset;
    const char *str;
    unsigned char pos;
    unsigned char name_len;
    unsigned char slot;
    unsigned int text_offset;
    unsigned int binding_offset;
    unsigned int reu_offset;

    if (idx < menu.scroll_offset || idx >= menu.count) {
        return;
    }
    row = (unsigned char)(idx - menu.scroll_offset);
    if (row >= menu.h) {
        return;
    }
    y = (unsigned char)(menu.y + row);

    if (idx == menu.selected) {
        color = menu.sel_color;
        prefix = 0x3E;  /* '>' in screen code */
    } else {
        color = menu.item_color;
        prefix = 32;    /* Space */
    }

    screen_offset = (unsigned int)y * 40 + menu.x;
    TUI_SCREEN[screen_offset] = prefix;
    TUI_COLOR_RAM[screen_offset] = color;
    TUI_SCREEN[screen_offset + 1] = 32;
    TUI_COLOR_RAM[screen_offset + 1] = color;
    if (idx > 0 && idx < app_count && app_banks[idx] != 0) {
        draw_drive_field(screen_offset + 2, app_drives[idx]);
    } else {
        TUI_SCREEN[screen_offset + 2] = 32;
        TUI_COLOR_RAM[screen_offset + 2] = color;
        TUI_SCREEN[screen_offset + 3] = 32;
        TUI_COLOR_RAM[screen_offset + 3] = color;
    }
    TUI_SCREEN[screen_offset + 4] = 32;
    TUI_COLOR_RAM[screen_offset + 4] = color;

    str = menu.items[idx];
    name_len = APP_NAME_WIDTH;
    text_offset = screen_offset + 5;
    for (pos = 0; str[pos] != 0 && pos < name_len; ++pos) {
        TUI_SCREEN[text_offset + pos] = tui_ascii_to_screen(str[pos]);
        TUI_COLOR_RAM[text_offset + pos] = color;
    }
    for (; pos < name_len; ++pos) {
        TUI_SCREEN[text_offset + pos] = 32;
        TUI_COLOR_RAM[text_offset + pos] = color;
    }

    reu_offset = screen_offset + menu.w - 1;
    binding_offset = reu_offset - (APP_BIND_LABEL_LEN + 1);
    slot = 0;
    if (idx > 0 && idx < app_count && app_banks[idx] != 0) {
        slot = launcher_hotkey_slot_for_bank(app_banks[idx]);
    }
    draw_binding_tag(binding_offset, slot, color);
    TUI_SCREEN[reu_offset - 1] = 32;
    TUI_COLOR_RAM[reu_offset - 1] = color;
    if (idx > 0 && idx < app_count && apps_loaded[idx] && app_banks[idx] != 0) {
        TUI_SCREEN[reu_offset] = REU_INDICATOR;
        TUI_COLOR_RAM[reu_offset] = TUI_COLOR_LIGHTGREEN;
    } else {
        TUI_SCREEN[reu_offset] = 32;
        TUI_COLOR_RAM[reu_offset] = color;
    }
}

static void draw_menu(void) {
    unsigned char row;
    unsigned char item_idx;

    for (row = 0; row < menu.h; ++row) {
        item_idx = menu.scroll_offset + row;
        if (item_idx < menu.count) {
            draw_menu_item(item_idx);
        } else {
            tui_puts_n(menu.x, (unsigned char)(menu.y + row), "", menu.w, menu.item_color);
        }
    }
}

static void draw_app_desc(void) {
    unsigned char sel = tui_menu_selected(&menu);
    static char launch_line[39];

    /* Overwrite both description lines in-place (no clear needed) */
    if (sel < app_count) {
        tui_puts_n(2, APPS_START_Y + APPS_HEIGHT, app_descs[sel], 38, TUI_COLOR_GRAY3);

        /* Show launch source */
        if (apps_loaded[sel] && app_banks[sel] != 0) {
            tui_puts_n(2, APPS_START_Y + APPS_HEIGHT + 1,
                       "LAUNCH FROM REU (INSTANT)", 38, TUI_COLOR_LIGHTGREEN);
        } else if (app_banks[sel] != 0) {
            if (app_drives[sel] == 8) {
                tui_puts_n(2, APPS_START_Y + APPS_HEIGHT + 1,
                           "LAUNCH FROM DISK", 38, TUI_COLOR_GRAY3);
            } else {
                strcpy(launch_line, "LAUNCH FROM DISK ");
                if (app_drives[sel] >= 10) {
                    launch_line[17] = (char)('0' + (app_drives[sel] / 10));
                    launch_line[18] = (char)('0' + (app_drives[sel] % 10));
                    launch_line[19] = 0;
                } else {
                    launch_line[17] = (char)('0' + app_drives[sel]);
                    launch_line[18] = 0;
                }
                tui_puts_n(2, APPS_START_Y + APPS_HEIGHT + 1, launch_line, 38, TUI_COLOR_GRAY3);
            }
        } else {
            tui_puts_n(2, APPS_START_Y + APPS_HEIGHT + 1, "", 38, TUI_COLOR_WHITE);
        }
    } else {
        tui_puts_n(2, APPS_START_Y + APPS_HEIGHT, "", 38, TUI_COLOR_WHITE);
        tui_puts_n(2, APPS_START_Y + APPS_HEIGHT + 1, "", 38, TUI_COLOR_WHITE);
    }
}

static void launcher_sync_visible_window(void) {
    unsigned char max_scroll = 0;

    if (menu.count == 0) {
        menu.selected = 0;
        menu.scroll_offset = 0;
        return;
    }

    if (menu.selected >= menu.count) {
        menu.selected = (unsigned char)(menu.count - 1);
    }

    if (menu.count > menu.h) {
        max_scroll = (unsigned char)(menu.count - menu.h);
    }
    if (menu.scroll_offset > max_scroll) {
        menu.scroll_offset = max_scroll;
    }

    if (menu.selected < menu.scroll_offset) {
        menu.scroll_offset = menu.selected;
    } else if (menu.selected >= (unsigned char)(menu.scroll_offset + menu.h)) {
        menu.scroll_offset = (unsigned char)(menu.selected - menu.h + 1);
    }
}

static void launcher_seed_default_hotkeys(void) {
    unsigned char i;
    unsigned char slot;

    for (i = 1; i < app_count; ++i) {
        slot = app_default_slots[i];
        if (slot == 0) {
            continue;
        }
        if (TUI_HOTKEY_BINDINGS[(unsigned char)(slot - 1)] == 0) {
            TUI_HOTKEY_BINDINGS[(unsigned char)(slot - 1)] = app_banks[i];
        }
    }
}

static unsigned char launcher_index_for_bank(unsigned char bank) {
    unsigned char i;

    for (i = 1; i < app_count; ++i) {
        if (app_banks[i] == bank) {
            return i;
        }
    }

    return 0;
}

static void launcher_apply_startup_actions(void) {
    unsigned char index;

    if (launcher_cfg_load_all_to_reu) {
        if (!load_all_to_reu_internal(0)) {
            launcher_set_notice("auto preload incomplete", TUI_COLOR_LIGHTRED);
            return;
        }

        if (launcher_runappfirst_prg[0] != 0) {
            index = launcher_find_app_by_prg(launcher_runappfirst_prg);
            if (index == 0) {
                launcher_set_notice("runappfirst app not found", TUI_COLOR_LIGHTRED);
                return;
            }
            launch_app(index);
            return;
        }
        return;
    }

    if (launcher_runappfirst_prg[0] != 0) {
        launcher_set_notice("runappfirst ignored without preload", TUI_COLOR_YELLOW);
    }
}

static void launcher_draw(void) {
    launcher_sync_visible_window();
    tui_clear(TUI_COLOR_BLUE);
    draw_header();
    tui_puts(2, APPS_START_Y - 1, "APPLICATIONS:", TUI_COLOR_WHITE);
    draw_menu();
    draw_app_desc();
    draw_status();
    draw_notice();
    draw_help();
}

/*---------------------------------------------------------------------------
 * Main
 *---------------------------------------------------------------------------*/

static void launcher_init(void) {
    unsigned char i;
    unsigned char saved_selected = 0;
    unsigned char saved_scroll_offset = 0;
    unsigned char err;
    unsigned char detail_a;
    unsigned char detail_b;
    unsigned char detail_c;

    tui_init();
    catalog_init_defaults();
    catalog_rebind_views();
    resume_ready = 0;

    err = load_catalog_from_disk(&detail_a, &detail_b, &detail_c);
    if (err != 0) {
        slot_contract_ok = 0;
        show_slot_contract_error(err, detail_a, detail_b, detail_c);
        running = 0;
        return;
    }

    err = validate_slot_contract(&detail_a, &detail_b, &detail_c);
    if (err != 0) {
        slot_contract_ok = 0;
        show_slot_contract_error(err, detail_a, detail_b, detail_c);
        running = 0;
        return;
    }
    slot_contract_ok = 1;

    resume_init_for_app(REU_BANK_LAUNCHER, REU_BANK_LAUNCHER,
                        LAUNCHER_RESUME_SCHEMA);
    resume_ready = 1;
    (void)launcher_resume_restore(&saved_selected, &saved_scroll_offset);

    /* Initialize menu */
    tui_menu_init(&menu, 2, APPS_START_Y, APP_MENU_WIDTH, APPS_HEIGHT, app_names, app_count);
    menu.item_color = TUI_COLOR_WHITE;
    menu.sel_color = TUI_COLOR_CYAN;

    if (saved_selected < app_count) {
        menu.selected = saved_selected;
    }
    menu.scroll_offset = saved_scroll_offset;
    launcher_sync_visible_window();

    /* ALWAYS sync apps_loaded from shim's reu_bitmap - this is the
     * authoritative source for what's actually in REU. Don't rely on
     * stale values from before the REU restore. */
    for (i = 0; i < app_count; ++i) {
        apps_loaded[i] = 0;
        app_sizes[i] = 0;
    }
    set_shim_drive(8);
    sync_from_reu_bitmap();
    launcher_seed_default_hotkeys();

    running = 1;
    launcher_apply_startup_actions();
}

static void launcher_loop(void) {
    unsigned char key;
    unsigned char result;
    unsigned char bank;
    unsigned char old_selected;
    unsigned char old_scroll_offset;

    /* apps_loaded[] is already synced from reu_bitmap in launcher_init() */
    if (!running) {
        return;
    }

    launcher_draw();

    while (running) {
        key = tui_getkey();

        if (key != 2 && key != TUI_KEY_NEXT_APP && key != TUI_KEY_PREV_APP) {
            bank = tui_handle_global_hotkey(key, REU_BANK_LAUNCHER, 0);
            if (bank >= 1 && bank < MAX_APPS) {
                result = launcher_index_for_bank(bank);
                if (result != 0) {
                    launch_app(result);
                    launcher_draw();
                    continue;
                }
            }
        }

        old_selected = menu.selected;
        old_scroll_offset = menu.scroll_offset;
        result = tui_menu_input(&menu, key);
        launcher_sync_visible_window();

        if (result != 255) {
            launch_app(result);
            launcher_draw();
            continue;
        }

        switch (key) {
            case TUI_KEY_F1:
                load_all_to_reu();
                launcher_draw();
                break;

            case TUI_KEY_F3:
                load_selected_to_reu(tui_menu_selected(&menu));
                launcher_draw();
                break;

            case TUI_KEY_RUNSTOP:
                running = 0;
                break;

            default:
                /* Only update if selection changed */
                if (old_selected != menu.selected) {
                    if (old_scroll_offset != menu.scroll_offset) {
                        draw_menu();
                    } else {
                        draw_menu_item(old_selected);
                        draw_menu_item(menu.selected);
                    }
                    draw_app_desc();
                }
                break;
        }
    }

    /* Reset on exit */
    __asm__("jmp $FCE2");
}

int main(void) {
    launcher_init();
    launcher_loop();
    return 0;
}
