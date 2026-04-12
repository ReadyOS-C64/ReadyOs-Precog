# Ready OS Makefile
# Build system for Ready OS and demo applications

# Tools
CC = cl65
AS = ca65
LD = ld65
CC65 = cc65
C1541 = c1541
PETCAT = petcat
PYTHON = python3
CLANG ?= clang
PWSH ?= pwsh

# Directories
SRC_DIR = src
OBJ_DIR = obj
CFG_DIR = cfg
LIB_DIR = src/lib
SHIM_DIR = src/shim
APPS_DIR = src/apps
BOOT_DIR = src/boot
GEN_DIR = src/generated
BUILD_SUPPORT_DIR ?= build_support
VICE_DEBUG_TOOLS_DIR ?= ../agenticdevharness/tools
PROFILE ?= precog-dual-d71
PROFILE_CATALOG_SRC = $(shell $(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py catalog-source --profile $(PROFILE))

# Flags for standard C64 programs (load at $0801)
# Note: -Osir causes cc65 optimizer crashes, disabled for now
CFLAGS = -t c64 -I$(LIB_DIR)

# Flags for apps (load at $1000 using custom config)
APP_CFLAGS = -t c64 -I$(LIB_DIR) -C $(CFG_DIR)/ready_app.cfg
EDITOR_CFLAGS = $(APP_CFLAGS) -Os
TASKLIST_CFLAGS = $(APP_CFLAGS) -Os
SIMPLEFILES_CFLAGS = -t c64 -I$(LIB_DIR) -C $(CFG_DIR)/ready_app_simplefiles.cfg -Os
SIMPLECELLS_CFLAGS = -t c64 -I$(LIB_DIR) -C $(CFG_DIR)/ready_app_simplecells.cfg -Os
CAL26_CFLAGS = $(APP_CFLAGS) -Os
LAUNCHER_CFG_VERBOSE ?= 0
LAUNCHER_CFLAGS = $(APP_CFLAGS) -DLAUNCHER_CFG_VERBOSE=$(LAUNCHER_CFG_VERBOSE)

# Output files
BOOT = boot.prg
PREBOOT = preboot.prg
SETD71 = setd71.prg
SHOWCFG = showcfg.prg
TEST_REU = test_reu.prg
LAUNCHER = launcher.prg
EDITOR = editor.prg
QUICKNOTES = quicknotes.prg
CALCPLUS = calcplus.prg
HEXVIEW = hexview.prg
CLIPMGR = clipmgr.prg
REUVIEWER = reuviewer.prg
TASKLIST = tasklist.prg
SIMPLEFILES = simplefiles.prg
SIMPLECELLS = simplecells.prg
GAME2048 = game2048.prg
DEMINER = deminer.prg
CAL26 = cal26.prg
DIZZY = dizzy.prg
READMEAPP = readme.prg
READYSHELL = readyshell.prg
VERSION_HEADER = $(GEN_DIR)/build_version.h
VERSION_ASM_INC = $(GEN_DIR)/msg_version.inc
VARIANT_ASM_INC = $(GEN_DIR)/msg_variant.inc
README_DATA_C = $(GEN_DIR)/readme_pages.c
README_DATA_H = $(GEN_DIR)/readme_pages.h
XRELCHK = xrelchk.prg
HARNESS_OUT_DIR = artifacts/dev_harness/xfilechk
XFILECHK_BOOT = $(HARNESS_OUT_DIR)/xfilechk_boot.prg
XFILECHK = $(HARNESS_OUT_DIR)/xfilechk.prg
XRELCHK_CASE ?= 0
XRELCHK_STOP_AFTER ?= 0
XFILECHK_CASE ?= 0
XREL_DATA_SA ?= 2
XREL_POS_SEND_MODE ?= 1
XREL_POS_CMD_MODE ?= 1
XREL_POS_CH_MODE ?= 1
XREL_POS_REC_BASE_MODE ?= 0
XREL_POS_ORDER_MODE ?= 0
XREL_POS_POS_MODE ?= 0
XREL_POS_CR_MODE ?= 1
XFILECHK_DISK1 = $(HARNESS_OUT_DIR)/xfilechk.d71
XFILECHK_DISK2 = $(HARNESS_OUT_DIR)/xfilechk_2.d71
READYOS_CONFIG_SRC ?= $(PROFILE_CATALOG_SRC)
READYOS_CONFIG_LOAD_ALL ?=
READYOS_CONFIG_RUN_FIRST ?=
CATALOG_SRC = $(READYOS_CONFIG_SRC)
CATALOG_SEQ = $(OBJ_DIR)/apps_cfg_petscii.seq
EDITOR_HELP_SRC = cfg/editor_help.txt
EDITOR_HELP_SEQ = $(OBJ_DIR)/editor_help.seq
TASKLIST_SAMPLE_SRC = cfg/tasklist_sample.txt
TASKLIST_SAMPLE_SEQ = $(OBJ_DIR)/tasklist_sample.seq
XFILECHK_SRC8_TXT = cfg/xfilechk_src8.txt
XFILECHK_SRC8_SEQ = $(OBJ_DIR)/xfilechk_src8.seq
XFILECHK_TESTA_TXT = cfg/xfilechk_testa.txt
XFILECHK_TESTA_SEQ = $(OBJ_DIR)/xfilechk_testa.seq
REL_SEED_D71 ?=
REL_SEED_D71_CANDIDATES := readyos0-1-5.d71 ../readyos0-1-5.d71 ../../readyos0-1-5.d71
ifeq ($(strip $(REL_SEED_D71)),)
REL_SEED_D71 := $(firstword $(wildcard $(REL_SEED_D71_CANDIDATES)))
endif

# Library sources
TUI_CORE_SRC = $(LIB_DIR)/tui_core.c
TUI_WINDOW_SRC = $(LIB_DIR)/tui_window.c
TUI_MENU_SRC = $(LIB_DIR)/tui_menu.c
TUI_INPUT_SRC = $(LIB_DIR)/tui_input.c
TUI_NAV_SRC = $(LIB_DIR)/tui_nav.c
TUI_HOTKEY_SRC = $(LIB_DIR)/tui_hotkeys.c
TUI_MISC_SRC = $(LIB_DIR)/tui_misc.c
REU_INIT_SRC = $(LIB_DIR)/reu_mgr_init.c
REU_ALLOC_SRC = $(LIB_DIR)/reu_mgr_alloc.c
REU_DMA_SRC = $(LIB_DIR)/reu_mgr_dma.c
REU_STATS_SRC = $(LIB_DIR)/reu_mgr_stats.c
STORAGE_DEVICE_SRC = $(LIB_DIR)/storage_device.c
FILE_BROWSER_SRC = $(LIB_DIR)/file_browser.c
DIR_PAGE_SRC = $(LIB_DIR)/dir_page.c
FILE_DIALOG_SRC = $(LIB_DIR)/file_dialog.c
CLIP_COPY_SRC = $(LIB_DIR)/clipboard_copy.c
CLIP_PASTE_SRC = $(LIB_DIR)/clipboard_paste.c
CLIP_COUNT_SRC = $(LIB_DIR)/clipboard_count.c
CLIP_ADMIN_SRC = $(LIB_DIR)/clipboard_admin.c
RESUME_STATE_CTX_SRC = $(LIB_DIR)/resume_state_ctx.c
RESUME_STATE_CORE_SRC = $(LIB_DIR)/resume_state_core.c
RESUME_STATE_SEGMENTS_SRC = $(LIB_DIR)/resume_state_segments.c
RESUME_STATE_INVALIDATE_SRC = $(LIB_DIR)/resume_state_invalidate.c
RESUME_STATE_SIMPLE_SRCS = $(RESUME_STATE_CTX_SRC) $(RESUME_STATE_CORE_SRC)
RESUME_STATE_SEGMENT_SRCS = $(RESUME_STATE_SIMPLE_SRCS) $(RESUME_STATE_SEGMENTS_SRC)
RESUME_STATE_ALL_SRCS = $(RESUME_STATE_SEGMENT_SRCS) $(RESUME_STATE_INVALIDATE_SRC)
READYSHELL_DIR = $(APPS_DIR)/readyshellpoc
READYSHELL_CORE_DIR = $(READYSHELL_DIR)/core
READYSHELL_PLATFORM_DIR = $(READYSHELL_DIR)/platform
READYSHELL_PLATFORM_C64_DIR = $(READYSHELL_PLATFORM_DIR)/c64
READYSHELL_PARSE_TRACE_DEBUG ?= 0
READYSHELL_PARSE_TRACE_CFLAG = $(if $(filter 1,$(READYSHELL_PARSE_TRACE_DEBUG)),-DRS_PARSE_TRACE_DEBUG=1,)
READYSHELL_PARSE_TRACE_ASFLAG = $(if $(filter 1,$(READYSHELL_PARSE_TRACE_DEBUG)),-D RS_PARSE_TRACE_DEBUG=1,)
READYSHELL_CCFLAGS = -t c64 -Os -I$(LIB_DIR) -I$(READYSHELL_CORE_DIR) -I$(READYSHELL_PLATFORM_DIR) -I$(READYSHELL_PLATFORM_C64_DIR) -DRS_C64_OVERLAY_RUNTIME=1 $(READYSHELL_PARSE_TRACE_CFLAG)
READYSHELL_OVL1_CFLAGS = $(READYSHELL_CCFLAGS) --code-name OVERLAY1 --rodata-name OVERLAY1 --bss-name OVERLAY1
READYSHELL_OVL2_CFLAGS = $(READYSHELL_CCFLAGS) --code-name OVERLAY2 --rodata-name OVERLAY2 --bss-name OVERLAY2
READYSHELL_OVL3_CFLAGS = $(READYSHELL_CCFLAGS) --code-name OVERLAY3 --rodata-name OVERLAY3 --bss-name OVERLAY3
READYSHELL_OVL4_CFLAGS = $(READYSHELL_CCFLAGS) --code-name OVERLAY4 --rodata-name OVERLAY4 --bss-name OVERLAY4
READYSHELL_OVL5_CFLAGS = $(READYSHELL_CCFLAGS) --code-name OVERLAY5 --rodata-name OVERLAY5 --bss-name OVERLAY5
READYSHELL_OVL6_CFLAGS = $(READYSHELL_CCFLAGS) --code-name OVERLAY6 --rodata-name OVERLAY6 --bss-name OVERLAY6
READYSHELL_OVERLAYSIZE ?= $(if $(filter 1,$(READYSHELL_PARSE_TRACE_DEBUG)),0x3B00,0x3800)
READYSHELL_STACKSIZE ?= 0x0800
READYSHELL_OBJ_DIR = $(OBJ_DIR)/readyshell
READYSHELL_OVL1_PRG = rsparser.prg
READYSHELL_OVL2_PRG = rsvm.prg
READYSHELL_OVL3_PRG = rsdrvilst.prg
READYSHELL_OVL4_PRG = rsldv.prg
READYSHELL_OVL5_PRG = rsstv.prg
READYSHELL_OVL1_DISK = $(OBJ_DIR)/rsparser.prg
READYSHELL_OVL2_DISK = $(OBJ_DIR)/rsvm.prg
READYSHELL_OVL3_DISK = $(OBJ_DIR)/rsdrvilst.prg
READYSHELL_OVL4_DISK = $(OBJ_DIR)/rsldv.prg
READYSHELL_OVL5_DISK = $(OBJ_DIR)/rsstv.prg

READYSHELL_OVERLAY1_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_lexer.c \
	$(READYSHELL_CORE_DIR)/rs_parse.c \
	$(READYSHELL_CORE_DIR)/rs_parse_support.c \
	$(READYSHELL_CORE_DIR)/rs_parse_free.c
READYSHELL_OVERLAY2_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_vars.c \
	$(READYSHELL_CORE_DIR)/rs_value.c \
	$(READYSHELL_CORE_DIR)/rs_format.c \
	$(READYSHELL_CORE_DIR)/rs_cmd.c \
	$(READYSHELL_CORE_DIR)/rs_pipe.c
READYSHELL_OVERLAY3_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_cmd_lst_c64.c \
	$(READYSHELL_CORE_DIR)/rs_cmd_drvi_c64.c
READYSHELL_OVERLAY4_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_cmd_ldv_c64.c
READYSHELL_OVERLAY5_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_cmd_stv_c64.c
READYSHELL_OVERLAY6_SRCS = \

READYSHELL_RESIDENT_SRCS = \
	$(READYSHELL_DIR)/readyshellpoc.c \
	$(READYSHELL_CORE_DIR)/rs_token.c \
	$(READYSHELL_CORE_DIR)/rs_bc.c \
	$(READYSHELL_CORE_DIR)/rs_errors.c \
	$(READYSHELL_CORE_DIR)/rs_cmd_registry.c \
	$(READYSHELL_CORE_DIR)/rs_vm_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_overlay_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_platform_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_screen_c64.c \
	$(TUI_NAV_SRC) \
	$(REU_DMA_SRC) \
	$(RESUME_STATE_SIMPLE_SRCS)
READYSHELL_RESIDENT_ASM_SRCS = \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_runtime_c64.s
READYSHELL_RESIDENT_C_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/resident/%.o,$(READYSHELL_RESIDENT_SRCS))
READYSHELL_RESIDENT_ASM_OBJS = $(patsubst %.s,$(READYSHELL_OBJ_DIR)/resident/%.o,$(READYSHELL_RESIDENT_ASM_SRCS))
READYSHELL_RESIDENT_OBJS = $(READYSHELL_RESIDENT_C_OBJS) $(READYSHELL_RESIDENT_ASM_OBJS)
READYSHELL_OVERLAY1_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay1/%.o,$(READYSHELL_OVERLAY1_SRCS))
READYSHELL_OVERLAY2_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay2/%.o,$(READYSHELL_OVERLAY2_SRCS))
READYSHELL_OVERLAY3_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay3/%.o,$(READYSHELL_OVERLAY3_SRCS))
READYSHELL_OVERLAY4_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay4/%.o,$(READYSHELL_OVERLAY4_SRCS))
READYSHELL_OVERLAY5_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay5/%.o,$(READYSHELL_OVERLAY5_SRCS))
READYSHELL_OVERLAY6_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay6/%.o,$(READYSHELL_OVERLAY6_SRCS))

TUI_BASE = $(TUI_CORE_SRC) $(TUI_WINDOW_SRC)
TUI_BASE_MENU = $(TUI_BASE) $(TUI_MENU_SRC)
TUI_BASE_INPUT = $(TUI_BASE) $(TUI_INPUT_SRC)
TUI_BASE_NAV = $(TUI_BASE) $(TUI_NAV_SRC)
TUI_BASE_MISC = $(TUI_BASE) $(TUI_MISC_SRC)
TUI_BASE_MENU_INPUT = $(TUI_BASE) $(TUI_MENU_SRC) $(TUI_INPUT_SRC)
TUI_BASE_NAV_MISC = $(TUI_BASE) $(TUI_NAV_SRC) $(TUI_MISC_SRC)
TUI_BASE_MENU_MISC = $(TUI_BASE) $(TUI_MENU_SRC) $(TUI_MISC_SRC)
TUI_BASE_MENU_INPUT_NAV = $(TUI_BASE) $(TUI_MENU_SRC) $(TUI_INPUT_SRC) $(TUI_NAV_SRC)
TUI_BASE_INPUT_NAV = $(TUI_BASE) $(TUI_INPUT_SRC) $(TUI_NAV_SRC)
TUI_BASE_INPUT_NAV_MISC = $(TUI_BASE) $(TUI_INPUT_SRC) $(TUI_NAV_SRC) $(TUI_MISC_SRC)
TUI_BASE_MENU_INPUT_NAV_MISC = $(TUI_BASE) $(TUI_MENU_SRC) $(TUI_INPUT_SRC) $(TUI_NAV_SRC) $(TUI_MISC_SRC)

LIB_LAUNCHER = $(TUI_BASE_MENU_MISC) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SEGMENT_SRCS)
LIB_REU_BASE = $(REU_INIT_SRC)
LIB_REU_DMA = $(REU_INIT_SRC) $(REU_ALLOC_SRC) $(REU_DMA_SRC)
LIB_REU_STATS = $(REU_INIT_SRC) $(REU_STATS_SRC)
LIB_REU_DMA_STATS = $(REU_INIT_SRC) $(REU_ALLOC_SRC) $(REU_DMA_SRC) $(REU_STATS_SRC)
LIB_CLIP_COPY = $(CLIP_COPY_SRC)
LIB_CLIP_PASTE = $(CLIP_PASTE_SRC)
LIB_CLIP_COUNT = $(CLIP_COUNT_SRC)
LIB_CLIP_PASTE_COUNT = $(CLIP_PASTE_SRC) $(CLIP_COUNT_SRC)
LIB_CLIP_COPY_PASTE_COUNT = $(CLIP_COPY_SRC) $(CLIP_PASTE_SRC) $(CLIP_COUNT_SRC)
LIB_EDITOR = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SEGMENT_SRCS) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC) $(FILE_DIALOG_SRC)
LIB_QUICKNOTES = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA_STATS) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_ALL_SRCS) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC) $(FILE_DIALOG_SRC)
LIB_CALCPLUS = $(TUI_BASE_NAV) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SEGMENT_SRCS)
LIB_HEXVIEW = $(TUI_BASE_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY) $(RESUME_STATE_SIMPLE_SRCS)
LIB_CLIPMGR = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA_STATS) $(LIB_CLIP_PASTE_COUNT) $(CLIP_ADMIN_SRC) $(RESUME_STATE_SIMPLE_SRCS) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_REUVIEWER = $(TUI_BASE_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA_STATS) $(RESUME_STATE_SIMPLE_SRCS)
LIB_TASKLIST = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_ALL_SRCS) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_SIMPLEFILES = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SIMPLE_SRCS) $(STORAGE_DEVICE_SRC) $(FILE_BROWSER_SRC)
LIB_SIMPLECELLS = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(LIB_REU_DMA) $(RESUME_STATE_SEGMENT_SRCS) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_GAME2048 = $(TUI_BASE_NAV) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SIMPLE_SRCS)
LIB_DEMINER = $(TUI_BASE_NAV) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SIMPLE_SRCS)
LIB_CAL26 = $(TUI_BASE_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SEGMENT_SRCS)
LIB_DIZZY = $(TUI_BASE_INPUT_NAV) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SEGMENT_SRCS)
LIB_README = $(TUI_BASE_NAV_MISC) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SIMPLE_SRCS)
LIB_READYSHELL = $(REU_DMA_SRC) $(RESUME_STATE_SIMPLE_SRCS)

# Primary binaries shared across profiles
PROGRAMS = $(BOOT) $(PREBOOT) $(SETD71) $(SHOWCFG) $(TEST_REU) $(LAUNCHER) $(EDITOR) $(QUICKNOTES) $(CALCPLUS) $(HEXVIEW) $(CLIPMGR) $(REUVIEWER) $(TASKLIST) $(SIMPLEFILES) $(SIMPLECELLS) $(GAME2048) $(DEMINER) $(CAL26) $(DIZZY) $(READMEAPP) $(READYSHELL)

# Default target
all: profile
	@echo ""
	@echo "=== Build complete ==="
	@VERSION_TEXT=$$($(PYTHON) $(BUILD_SUPPORT_DIR)/update_build_version.py --current); \
	$(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py resolve --profile "$(PROFILE)" --version "$$VERSION_TEXT"

# Boot loader (assembly version for size control)
$(BOOT): $(BOOT_DIR)/boot_asm.s $(VERSION_ASM_INC) $(VARIANT_ASM_INC) $(CATALOG_SEQ)
	$(AS) -o obj/boot.o $<
	$(LD) -C $(CFG_DIR)/boot_asm.cfg -o $@ obj/boot.o

# Variant asm include is generated as a side effect of apps.cfg generation.
$(VARIANT_ASM_INC): $(CATALOG_SEQ)
	@test -f $@

# C64 BASIC preboot loader is profile-sensitive.
$(PREBOOT): FORCE $(BUILD_SUPPORT_DIR)/readyos_profiles.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py write-preboot --profile "$(PROFILE)" --output $@

# C64 BASIC helper to issue U0>M1 and run boot
$(SETD71): $(BOOT_DIR)/setd71.bas
	$(PETCAT) -w2 -o $@ $<

# C64 BASIC harness boot for standalone IEC file checks
$(XFILECHK_BOOT): $(BOOT_DIR)/xfilechk_boot.bas
	@mkdir -p "$(dir $@)"
	$(PETCAT) -w2 -o $@ $<

# C64 BASIC apps.cfg inspector (read-only diagnostics)
$(SHOWCFG): $(BOOT_DIR)/showcfg.bas
	$(PETCAT) -w2 -o $@ $<

# Build apps.cfg payload from sectioned config source
$(CATALOG_SEQ): FORCE $(CATALOG_SRC) $(BUILD_SUPPORT_DIR)/build_apps_catalog_petscii.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_apps_catalog_petscii.py --input $(CATALOG_SRC) --output $@ \
		--variant-asm-output $(VARIANT_ASM_INC) \
		$(if $(strip $(READYOS_CONFIG_LOAD_ALL)),--override-load-all $(READYOS_CONFIG_LOAD_ALL),) \
		$(if $(strip $(READYOS_CONFIG_RUN_FIRST)),--override-run-first $(READYOS_CONFIG_RUN_FIRST),)

# Build plain-text lowercase PETASCII SEQ payloads
$(EDITOR_HELP_SEQ): $(EDITOR_HELP_SRC) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(EDITOR_HELP_SRC) --output $@

$(XFILECHK_SRC8_SEQ): $(XFILECHK_SRC8_TXT) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(XFILECHK_SRC8_TXT) --output $@

$(XFILECHK_TESTA_SEQ): $(XFILECHK_TESTA_TXT) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(XFILECHK_TESTA_TXT) --output $@

$(TASKLIST_SAMPLE_SEQ): $(TASKLIST_SAMPLE_SRC) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(TASKLIST_SAMPLE_SRC) --output $@

# Test REU program (standalone)
$(TEST_REU): $(SRC_DIR)/test_reu.c
	$(CC) $(CFLAGS) -o $@ $<

# Launcher app (loads at $1000)
$(LAUNCHER): $(APPS_DIR)/launcher/launcher.c $(LIB_LAUNCHER) $(VERSION_HEADER)
	$(CC) $(LAUNCHER_CFLAGS) -m $(OBJ_DIR)/launcher.map -o $@ $(APPS_DIR)/launcher/launcher.c $(LIB_LAUNCHER)

# Editor app (loads at $1000)
$(EDITOR): $(APPS_DIR)/editor/editor.c $(LIB_EDITOR)
	$(CC) $(EDITOR_CFLAGS) -m $(OBJ_DIR)/editor.map -o $@ $^

# Quicknotes app (loads at $1000)
$(QUICKNOTES): $(APPS_DIR)/quicknotes/quicknotes.c $(LIB_QUICKNOTES)
	$(CC) $(EDITOR_CFLAGS) -m $(OBJ_DIR)/quicknotes.map -o $@ $^

# Calculator Plus app (loads at $1000)
$(CALCPLUS): $(APPS_DIR)/calcplus/calcplus.c $(APPS_DIR)/calcplus/rom_float.s $(LIB_CALCPLUS)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/calcplus.map -o $@ $^

# Hex Viewer app (loads at $1000)
$(HEXVIEW): $(APPS_DIR)/hexview/hexview.c $(LIB_HEXVIEW)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/hexview.map -o $@ $^

# Clipboard Manager app (loads at $1000)
$(CLIPMGR): $(APPS_DIR)/clipmgr/clipmgr.c $(LIB_CLIPMGR)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/clipmgr.map -o $@ $^

# REU Viewer app (loads at $1000)
$(REUVIEWER): $(APPS_DIR)/reuviewer/reuviewer.c $(LIB_REUVIEWER)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/reuviewer.map -o $@ $^

# Task List app (loads at $1000)
$(TASKLIST): $(APPS_DIR)/tasklist/tasklist.c $(LIB_TASKLIST)
	$(CC) $(TASKLIST_CFLAGS) -m $(OBJ_DIR)/tasklist.map -o $@ $^

# Simple Files app (loads at $1000)
$(SIMPLEFILES): $(APPS_DIR)/simplefiles/simplefiles.c $(LIB_SIMPLEFILES) $(CFG_DIR)/ready_app_simplefiles.cfg
	$(CC) $(SIMPLEFILES_CFLAGS) -m $(OBJ_DIR)/simplefiles.map -o $@ $(APPS_DIR)/simplefiles/simplefiles.c $(LIB_SIMPLEFILES)

# Simple Cells app (loads at $1000)
$(SIMPLECELLS): $(APPS_DIR)/simplecells/simplecells.c $(APPS_DIR)/calcplus/rom_float.s $(LIB_SIMPLECELLS)
	$(CC) $(SIMPLECELLS_CFLAGS) -m $(OBJ_DIR)/simplecells.map -o $@ $^

# 2048 game app (loads at $1000)
$(GAME2048): $(APPS_DIR)/game2048/game2048.c $(LIB_GAME2048)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/game2048.map -o $@ $^

# Deminer game app (loads at $1000)
$(DEMINER): $(APPS_DIR)/deminer/deminer.c $(LIB_DEMINER)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/deminer.map -o $@ $^

# Calendar 2026 app (loads at $1000)
$(CAL26): $(APPS_DIR)/cal26/cal26.c $(LIB_CAL26)
	$(CC) $(CAL26_CFLAGS) -m $(OBJ_DIR)/cal26.map -o $@ $^

# Dizzy app (loads at $1000)
$(DIZZY): $(APPS_DIR)/dizzy/dizzy.c $(LIB_DIZZY)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/dizzy.map -o $@ $^

# Generate README app page assets from markdown-lite source
$(README_DATA_C): $(BUILD_SUPPORT_DIR)/build_readme_app_assets.py $(APPS_DIR)/readme/readme_lite.md
	python3 $(BUILD_SUPPORT_DIR)/build_readme_app_assets.py \
		--input $(APPS_DIR)/readme/readme_lite.md \
		--out-h $(README_DATA_H) \
		--out-c $(README_DATA_C)

$(README_DATA_H): $(README_DATA_C)

# README app (loads at $1000)
$(READMEAPP): $(APPS_DIR)/readme/readme.c $(README_DATA_C) $(README_DATA_H) $(LIB_README)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/readme.map -o $@ $(APPS_DIR)/readme/readme.c $(README_DATA_C) $(LIB_README)

# ReadyShell resident objects
$(READYSHELL_OBJ_DIR)/resident/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_CCFLAGS) -c -o $@ $<

$(READYSHELL_OBJ_DIR)/resident/%.o: %.s
	@mkdir -p "$(dir $@)"
	$(AS) $(READYSHELL_PARSE_TRACE_ASFLAG) -o $@ $<

# ReadyShell overlay objects
$(READYSHELL_OBJ_DIR)/overlay1/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_OVL1_CFLAGS) -c -o $@ $<

$(READYSHELL_OBJ_DIR)/overlay2/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_OVL2_CFLAGS) -c -o $@ $<

$(READYSHELL_OBJ_DIR)/overlay3/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_OVL3_CFLAGS) -c -o $@ $<

$(READYSHELL_OBJ_DIR)/overlay4/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_OVL4_CFLAGS) -c -o $@ $<

$(READYSHELL_OBJ_DIR)/overlay5/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_OVL5_CFLAGS) -c -o $@ $<

$(READYSHELL_OBJ_DIR)/overlay6/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(CC) $(READYSHELL_OVL6_CFLAGS) -c -o $@ $<

# ReadyShell app (loads at $1000) + overlay sidecars
$(READYSHELL): $(READYSHELL_RESIDENT_OBJS) $(READYSHELL_OVERLAY1_OBJS) $(READYSHELL_OVERLAY2_OBJS) $(READYSHELL_OVERLAY3_OBJS) $(READYSHELL_OVERLAY4_OBJS) $(READYSHELL_OVERLAY5_OBJS) $(READYSHELL_OVERLAY6_OBJS) $(CFG_DIR)/ready_app_overlay.cfg
	$(CC) -t c64 -C $(CFG_DIR)/ready_app_overlay.cfg \
		-Wl -D,__OVERLAYSIZE__=$(READYSHELL_OVERLAYSIZE) \
		-Wl -D,__STACKSIZE__=$(READYSHELL_STACKSIZE) \
		-m $(OBJ_DIR)/readyshell.map -o $@ \
		$(READYSHELL_RESIDENT_OBJS) $(READYSHELL_OVERLAY1_OBJS) $(READYSHELL_OVERLAY2_OBJS) $(READYSHELL_OVERLAY3_OBJS) $(READYSHELL_OVERLAY4_OBJS) $(READYSHELL_OVERLAY5_OBJS) $(READYSHELL_OVERLAY6_OBJS)
	cp -f $(READYSHELL_OVL1_PRG) $(READYSHELL_OVL1_DISK)
	cp -f $(READYSHELL_OVL2_PRG) $(READYSHELL_OVL2_DISK)
	cp -f $(READYSHELL_OVL3_PRG) $(READYSHELL_OVL3_DISK)
	cp -f $(READYSHELL_OVL4_PRG) $(READYSHELL_OVL4_DISK)
	cp -f $(READYSHELL_OVL5_PRG) $(READYSHELL_OVL5_DISK)

# Temporary REL diagnostics app (standalone at $0801)
$(XRELCHK): $(APPS_DIR)/xrelchk/xrelchk.c
	$(CC) $(CFLAGS) \
		-D XRELCHK_CASE=$(XRELCHK_CASE) \
		-D XRELCHK_STOP_AFTER=$(XRELCHK_STOP_AFTER) \
		-D XREL_DATA_SA=$(XREL_DATA_SA) \
		-D XREL_POS_SEND_MODE=$(XREL_POS_SEND_MODE) \
		-D XREL_POS_CMD_MODE=$(XREL_POS_CMD_MODE) \
		-D XREL_POS_CH_MODE=$(XREL_POS_CH_MODE) \
		-D XREL_POS_REC_BASE_MODE=$(XREL_POS_REC_BASE_MODE) \
		-D XREL_POS_ORDER_MODE=$(XREL_POS_ORDER_MODE) \
		-D XREL_POS_POS_MODE=$(XREL_POS_POS_MODE) \
		-D XREL_POS_CR_MODE=$(XREL_POS_CR_MODE) \
		-m $(OBJ_DIR)/xrelchk.map -o $@ $<

# Standalone IEC file-operation harness ($0801)
$(XFILECHK): $(APPS_DIR)/xfilechk/xfilechk.c
	@mkdir -p "$(dir $@)"
	$(CC) $(CFLAGS) \
		-D XFILECHK_CASE=$(XFILECHK_CASE) \
		-m $(OBJ_DIR)/xfilechk.map -o $@ $<

$(XFILECHK_DISK1): FORCE $(XFILECHK_BOOT) $(XFILECHK) $(XFILECHK_SRC8_SEQ)
	@mkdir -p "$(dir $@)"
	$(C1541) -format "xfilechk8,ro" d71 $@ \
		-write $(XFILECHK_BOOT) xfilechkboot \
		-write $(XFILECHK) xfilechk \
		-write $(XFILECHK_SRC8_SEQ) "src8,s"

$(XFILECHK_DISK2): FORCE $(XFILECHK_TESTA_SEQ)
	@mkdir -p "$(dir $@)"
	$(C1541) -format "xfilechk9,ro" d71 $@ \
		-write $(XFILECHK_TESTA_SEQ) "testa,s"

# Version preparation shared by direct make and run scripts.
prepare-version:
ifeq ($(strip $(READYOS_VERSION_TEXT)),)
	@$(PYTHON) $(BUILD_SUPPORT_DIR)/update_build_version.py --next >/dev/null
else
	@$(PYTHON) $(BUILD_SUPPORT_DIR)/update_build_version.py --write "$(READYOS_VERSION_TEXT)" >/dev/null
endif

programs: prepare-version $(PROGRAMS)

profiles:
	@$(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py list-ids

profile: programs
	@VERSION_TEXT=$$($(PYTHON) $(BUILD_SUPPORT_DIR)/update_build_version.py --current); \
	$(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py build-release \
		--profile "$(PROFILE)" \
		--version "$$VERSION_TEXT" \
		--catalog-source "$(READYOS_CONFIG_SRC)" \
		$(if $(strip $(READYOS_CONFIG_LOAD_ALL)),--override-load-all "$(READYOS_CONFIG_LOAD_ALL)",) \
		$(if $(strip $(READYOS_CONFIG_RUN_FIRST)),--override-run-first "$(READYOS_CONFIG_RUN_FIRST)",)

release-all: prepare-version
	@VERSION_TEXT=$$($(PYTHON) $(BUILD_SUPPORT_DIR)/update_build_version.py --current); \
	for profile in precog-dual-d71 $$($(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py list-ids | grep -v '^precog-dual-d71$$'); do \
		RS_PARSE_TRACE_DEBUG=$$($(PYTHON) $(BUILD_SUPPORT_DIR)/readyos_profiles.py readyshell-parse-trace-debug --profile "$$profile"); \
		echo "==> $$profile ($$VERSION_TEXT, READYSHELL_PARSE_TRACE_DEBUG=$$RS_PARSE_TRACE_DEBUG)"; \
		$(MAKE) PROFILE="$$profile" READYOS_VERSION_TEXT="$$VERSION_TEXT" READYSHELL_PARSE_TRACE_DEBUG="$$RS_PARSE_TRACE_DEBUG" profile; \
	done

audit-profile-assets:
	$(PYTHON) $(BUILD_SUPPORT_DIR)/audit_release_seq_rel.py --profile "$(PROFILE)"

audit-release-assets:
	$(PYTHON) $(BUILD_SUPPORT_DIR)/audit_release_seq_rel.py

# Clean
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f $(OBJ_DIR)/*.map
	rm -rf $(READYSHELL_OBJ_DIR)
	rm -f $(CATALOG_SEQ)
	rm -f $(VARIANT_ASM_INC)
	rm -f $(EDITOR_HELP_SEQ)
	rm -f $(TASKLIST_SAMPLE_SEQ)
	rm -f *.prg
	rm -f $(READYSHELL_OVL1_PRG) $(READYSHELL_OVL2_PRG) $(READYSHELL_OVL3_PRG) \
		$(READYSHELL_OVL4_PRG) $(READYSHELL_OVL5_PRG) $(READYSHELL_OVL6_PRG) \
		$(READYSHELL).1 $(READYSHELL).2 $(READYSHELL).3 $(READYSHELL).4 \
		$(READYSHELL).5 $(READYSHELL).6 $(READYSHELL).7 $(READYSHELL).8 $(READYSHELL).9
	rm -f $(READYSHELL_OVL1_DISK) $(READYSHELL_OVL2_DISK) $(READYSHELL_OVL3_DISK) \
		$(READYSHELL_OVL4_DISK) $(READYSHELL_OVL5_DISK) $(READYSHELL_OVL6_DISK) \
		$(OBJ_DIR)/readyshell_unused_ovl7.prg \
		$(OBJ_DIR)/readyshell_unused_ovl8.prg $(OBJ_DIR)/readyshell_unused_ovl9.prg
	rm -f *.d64
	rm -f *.d71
	rm -f *.d81
	rm -rf release

# Verify all generated binaries and memory layout constraints
verify: profile
	python3 verify.py --profile "$(PROFILE)"
	python3 $(BUILD_SUPPORT_DIR)/audit_release_seq_rel.py --profile "$(PROFILE)"
	python3 $(BUILD_SUPPORT_DIR)/editor_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/tasklist_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/simplefiles_host_smoke.py
	$(MAKE) readyshell-vm-smoke-host
	python3 $(BUILD_SUPPORT_DIR)/verify_resume_contract.py
	python3 $(BUILD_SUPPORT_DIR)/verify_memory_map.py

# Full rebuild + deep verification
fullcheck: clean verify

# Warm-resume contract checks only
verify-resume:
	python3 $(BUILD_SUPPORT_DIR)/verify_resume_contract.py

# Fast host-side parser smoke checks (no VICE, no C64 memory mapping).
readyshell-parse-smoke-host:
	$(CLANG) -std=c99 -Wall -Wextra -I. -I$(READYSHELL_CORE_DIR) \
		$(BUILD_SUPPORT_DIR)/readyshell_parse_smoke.c \
		$(READYSHELL_CORE_DIR)/rs_parse.c \
		$(READYSHELL_CORE_DIR)/rs_parse_support.c \
		$(READYSHELL_CORE_DIR)/rs_parse_free.c \
		$(READYSHELL_CORE_DIR)/rs_lexer.c \
		$(READYSHELL_CORE_DIR)/rs_token.c \
		$(READYSHELL_CORE_DIR)/rs_errors.c \
		-o /tmp/readyshell_parse_smoke
	/tmp/readyshell_parse_smoke

readyshell-vm-smoke-host:
	$(CLANG) -std=c99 -Wall -Wextra -I. -I$(READYSHELL_CORE_DIR) \
		$(BUILD_SUPPORT_DIR)/readyshell_reu_host.c \
		$(BUILD_SUPPORT_DIR)/readyshell_vm_smoke.c \
		$(READYSHELL_CORE_DIR)/rs_vm.c \
		$(READYSHELL_CORE_DIR)/rs_parse.c \
		$(READYSHELL_CORE_DIR)/rs_parse_support.c \
		$(READYSHELL_CORE_DIR)/rs_parse_free.c \
		$(READYSHELL_CORE_DIR)/rs_lexer.c \
		$(READYSHELL_CORE_DIR)/rs_token.c \
		$(READYSHELL_CORE_DIR)/rs_errors.c \
		$(READYSHELL_CORE_DIR)/rs_value.c \
		$(READYSHELL_CORE_DIR)/rs_vars.c \
		$(READYSHELL_CORE_DIR)/rs_cmd.c \
		$(READYSHELL_CORE_DIR)/rs_pipe.c \
		$(READYSHELL_CORE_DIR)/rs_format.c \
		$(READYSHELL_CORE_DIR)/rs_serialize.c \
		-o /tmp/readyshell_vm_smoke
	/tmp/readyshell_vm_smoke
	$(CLANG) -std=c99 -Wall -Wextra -DREADYSHELL_VM_SMOKE_OVERLAY=1 -I. -I$(READYSHELL_CORE_DIR) \
		-I$(READYSHELL_PLATFORM_DIR) -I$(READYSHELL_PLATFORM_C64_DIR) \
		$(BUILD_SUPPORT_DIR)/readyshell_reu_host.c \
		$(BUILD_SUPPORT_DIR)/readyshell_vm_smoke.c \
		$(BUILD_SUPPORT_DIR)/readyshell_overlay_host_stub.c \
		$(READYSHELL_CORE_DIR)/rs_vm_c64.c \
		$(READYSHELL_CORE_DIR)/rs_cmd_registry.c \
		$(READYSHELL_CORE_DIR)/rs_parse.c \
		$(READYSHELL_CORE_DIR)/rs_parse_support.c \
		$(READYSHELL_CORE_DIR)/rs_parse_free.c \
		$(READYSHELL_CORE_DIR)/rs_lexer.c \
		$(READYSHELL_CORE_DIR)/rs_token.c \
		$(READYSHELL_CORE_DIR)/rs_errors.c \
		$(READYSHELL_CORE_DIR)/rs_value.c \
		$(READYSHELL_CORE_DIR)/rs_vars.c \
		$(READYSHELL_CORE_DIR)/rs_cmd.c \
		$(READYSHELL_CORE_DIR)/rs_pipe.c \
		$(READYSHELL_CORE_DIR)/rs_format.c \
		$(READYSHELL_CORE_DIR)/rs_serialize.c \
		-o /tmp/readyshell_vm_smoke_c64
	/tmp/readyshell_vm_smoke_c64

readyshell-reu-tests-host:
	$(CLANG) -std=c99 -Wall -Wextra -I. -I$(READYSHELL_CORE_DIR) \
		$(BUILD_SUPPORT_DIR)/readyshell_reu_host.c \
		$(BUILD_SUPPORT_DIR)/readyshell_reu_tests.c \
		$(READYSHELL_CORE_DIR)/rs_value.c \
		$(READYSHELL_CORE_DIR)/rs_format.c \
		$(READYSHELL_CORE_DIR)/rs_serialize.c \
		$(READYSHELL_CORE_DIR)/rs_token.c \
		-o /tmp/readyshell_reu_tests
	/tmp/readyshell_reu_tests

editor-smoke-host:
	python3 $(BUILD_SUPPORT_DIR)/editor_host_smoke.py

tasklist-smoke-host:
	python3 $(BUILD_SUPPORT_DIR)/tasklist_host_smoke.py

file-dialog-memory-report:
	python3 $(BUILD_SUPPORT_DIR)/file_dialog_memory_report.py

readyshell-overlay-report: profile
	python3 $(BUILD_SUPPORT_DIR)/readyshell_overlay_report.py --profile "$(PROFILE)"

# Run Ready OS (boot loader)
run:
	bash ./run.sh --profile "$(PROFILE)"

# Run test_reu standalone
run-test: $(TEST_REU)
	x64sc -reu -reusize 16384 -autostartprgmode 1 $<

# Help
help:
	@echo "Ready OS Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build the selected profile release package (default)"
	@echo "  profiles    - List available build/run profiles"
	@echo "  profile     - Build one profile under releases/<version>/<profile>"
	@echo "                Use PROFILE=<id> (default: $(PROFILE))"
	@echo "  release-all - Build every release profile with one version stamp"
	@echo "  clean       - Remove built files"
	@echo "  verify      - Build and run deep binary verification"
	@echo "                (includes hard memory-map gate)"
	@echo "  verify-resume - Run warm-resume contract verification"
	@echo "  fullcheck   - Clean rebuild and deep binary verification"
	@echo "  editor-smoke-host - Run Editor host-side smoke checks"
	@echo "  tasklist-smoke-host - Run Tasklist host-side smoke checks"
	@echo "  readyshell-vm-smoke-host - Run ReadyShell VM/pipeline host smoke checks"
	@echo "  readyshell-reu-tests-host - Run ReadyShell REU heap/value host tests"
	@echo "  readyshell-overlay-report - Generate ReadyShell overlay Markdown + HTML docs"
	@echo "  seed-cal26  - Seed CAL26 REL files on the latest built precog-dual-d71 drive 8 image"
	@echo "  launcher-verbose - Rebuild launcher with verbose config diagnostics"
	@echo "  run         - Run Ready OS through run.sh for PROFILE=<id>"
	@echo "  run-test    - Run REU test in VICE"
	@echo "  $(XFILECHK) - Build standalone IEC file-operation harness"
	@echo "  $(XFILECHK_DISK1) - Build standalone IEC harness boot/program disk"
	@echo "  $(XFILECHK_DISK2) - Build standalone IEC harness fixture disk"
	@echo ""
	@echo "Programs built:"
	@echo "  preboot.prg  - BASIC preboot (sets D71 mode, runs boot)"
	@echo "  setd71.prg   - BASIC helper (sends U0>M1 only)"
	@echo "  showcfg.prg  - BASIC APPS.CFG inspector (shows text + byte codes)"
	@echo "  boot.prg     - Boot loader (installs shim, loads launcher)"
	@echo "  launcher.prg - App launcher (loads at \$$1000)"
	@echo "  editor.prg   - Text editor (loads at \$$1000)"
	@echo "  quicknotes.prg - REU-backed note editor (loads at \$$1000)"
	@echo "  calcplus.prg - Calculator Plus (loads at \$$1000)"
	@echo "  hexview.prg  - Hex memory viewer (loads at \$$1000)"
	@echo "  clipmgr.prg  - Clipboard manager (loads at \$$1000)"
	@echo "  reuviewer.prg- REU memory viewer (loads at \$$1000)"
	@echo "  game2048.prg - 2048 puzzle game (loads at \$$1000)"
	@echo "  deminer.prg  - PETSCII minesweeper game (loads at \$$1000)"
	@echo "  cal26.prg    - Calendar 2026 app (loads at \$$1000)"
	@echo "  dizzy.prg    - Kanban task board app (loads at \$$1000)"
	@echo "  readme.prg   - Project README app (loads at \$$1000)"
	@echo "  readyshell.prg - ReadyShell app (loads at \$$1000, overlays rsparser/rsvm/rsdrvilst/rsldv/rsstv on disk 1)"
	@echo "  $(XFILECHK) - Standalone IEC file-operation harness (loads at \$$0801)"
	@echo "  releases/<version>/<profile>/readyos-v<version>-<kind>[_n].<ext>"
	@echo "              - Versioned disk images for the selected profile"
	@echo "  releases/<version>/<profile>/helpme.md"
	@echo "              - Profile-specific run instructions"
	@echo "  $(XFILECHK_DISK1) - Standalone harness drive 8 disk (boot+harness+src fixture)"
	@echo "  $(XFILECHK_DISK2) - Standalone harness drive 9 disk (test fixture)"

FORCE:

# Seed CAL26 REL files with valid initial data and 2 sample events
seed-cal26:
	python3 $(BUILD_SUPPORT_DIR)/seed_cal26_rel.py

# Headless VICE REL probe (xrelchk + monitor log parse)
probe-rel:
	$(VICE_DEBUG_TOOLS_DIR)/vice_headless_rel_probe.sh

# Build launcher with expanded on-screen config diagnostics
launcher-verbose:
	$(MAKE) LAUNCHER_CFG_VERBOSE=1 $(LAUNCHER)

.PHONY: all clean verify verify-resume fullcheck help run run-test seed-cal26 probe-rel launcher-verbose readyshell-parse-smoke-host readyshell-vm-smoke-host readyshell-reu-tests-host editor-smoke-host tasklist-smoke-host programs prepare-version profile profiles release-all FORCE
