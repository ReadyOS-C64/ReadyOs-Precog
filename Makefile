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

# Flags for standard C64 programs (load at $0801)
# Note: -Osir causes cc65 optimizer crashes, disabled for now
CFLAGS = -t c64 -I$(LIB_DIR)

# Flags for apps (load at $1000 using custom config)
APP_CFLAGS = -t c64 -I$(LIB_DIR) -C $(CFG_DIR)/ready_app.cfg
EDITOR_CFLAGS = $(APP_CFLAGS) -Os
TASKLIST_CFLAGS = $(APP_CFLAGS) -Os
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
DISK1 = readyos.d71
DISK2 = readyos_2.d71
XFILECHK_DISK1 = $(HARNESS_OUT_DIR)/xfilechk.d71
XFILECHK_DISK2 = $(HARNESS_OUT_DIR)/xfilechk_2.d71
DISK = $(DISK1)
CATALOG_SRC = cfg/apps_catalog.txt
CATALOG_SEQ = $(OBJ_DIR)/apps_cfg_petscii.seq
EDITOR_HELP_SRC = cfg/editor_help.txt
EDITOR_HELP_SEQ = $(OBJ_DIR)/editor_help.seq
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
CLIP_COPY_SRC = $(LIB_DIR)/clipboard_copy.c
CLIP_PASTE_SRC = $(LIB_DIR)/clipboard_paste.c
CLIP_COUNT_SRC = $(LIB_DIR)/clipboard_count.c
CLIP_ADMIN_SRC = $(LIB_DIR)/clipboard_admin.c
RESUME_STATE_SRC = $(LIB_DIR)/resume_state.c
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
READYSHELL_OVERLAYSIZE ?= $(if $(filter 1,$(READYSHELL_PARSE_TRACE_DEBUG)),0x2480,0x2440)
READYSHELL_STACKSIZE ?= 0x0800
READYSHELL_OBJ_DIR = $(OBJ_DIR)/readyshell
READYSHELL_OVL1_PRG = $(READYSHELL).1
READYSHELL_OVL2_PRG = $(READYSHELL).2
READYSHELL_OVL3_PRG = $(READYSHELL).3
READYSHELL_OVL4_PRG = $(READYSHELL).4
READYSHELL_OVL5_PRG = $(READYSHELL).5
READYSHELL_OVL6_PRG = $(READYSHELL).6
READYSHELL_OVL7_PRG = $(READYSHELL).7
READYSHELL_OVL8_PRG = $(READYSHELL).8
READYSHELL_OVL9_PRG = $(READYSHELL).9
READYSHELL_OVL1_DISK = $(OBJ_DIR)/readyshell_ovl1.prg
READYSHELL_OVL2_DISK = $(OBJ_DIR)/readyshell_ovl2.prg
READYSHELL_OVL3_DISK = $(OBJ_DIR)/readyshell_ovl3.prg

READYSHELL_OVERLAY1_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_parse.c
READYSHELL_OVERLAY2_SRCS = \
	$(READYSHELL_CORE_DIR)/rs_vars.c \
	$(READYSHELL_CORE_DIR)/rs_value.c \
	$(READYSHELL_CORE_DIR)/rs_format.c \
	$(READYSHELL_CORE_DIR)/rs_cmd.c \
	$(READYSHELL_CORE_DIR)/rs_pipe.c
READYSHELL_OVERLAY3_SRCS = \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_script_ctl_c64.c
READYSHELL_RESIDENT_SRCS = \
	$(READYSHELL_DIR)/readyshellpoc.c \
	$(READYSHELL_CORE_DIR)/rs_token.c \
	$(READYSHELL_CORE_DIR)/rs_lexer.c \
	$(READYSHELL_CORE_DIR)/rs_bc.c \
	$(READYSHELL_CORE_DIR)/rs_errors.c \
	$(READYSHELL_CORE_DIR)/rs_vm_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_overlay_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_platform_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_fs_c64.c \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_screen_c64.c \
	$(TUI_NAV_SRC) \
	$(REU_DMA_SRC) \
	$(RESUME_STATE_SRC)
READYSHELL_RESIDENT_ASM_SRCS = \
	$(READYSHELL_PLATFORM_C64_DIR)/rs_runtime_c64.s
READYSHELL_RESIDENT_C_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/resident/%.o,$(READYSHELL_RESIDENT_SRCS))
READYSHELL_RESIDENT_ASM_OBJS = $(patsubst %.s,$(READYSHELL_OBJ_DIR)/resident/%.o,$(READYSHELL_RESIDENT_ASM_SRCS))
READYSHELL_RESIDENT_OBJS = $(READYSHELL_RESIDENT_C_OBJS) $(READYSHELL_RESIDENT_ASM_OBJS)
READYSHELL_OVERLAY1_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay1/%.o,$(READYSHELL_OVERLAY1_SRCS))
READYSHELL_OVERLAY2_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay2/%.o,$(READYSHELL_OVERLAY2_SRCS))
READYSHELL_OVERLAY3_OBJS = $(patsubst %.c,$(READYSHELL_OBJ_DIR)/overlay3/%.o,$(READYSHELL_OVERLAY3_SRCS))

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

LIB_LAUNCHER = $(TUI_BASE_MENU_MISC) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SRC)
LIB_REU_BASE = $(REU_INIT_SRC)
LIB_REU_DMA = $(REU_INIT_SRC) $(REU_ALLOC_SRC) $(REU_DMA_SRC)
LIB_REU_STATS = $(REU_INIT_SRC) $(REU_STATS_SRC)
LIB_REU_DMA_STATS = $(REU_INIT_SRC) $(REU_ALLOC_SRC) $(REU_DMA_SRC) $(REU_STATS_SRC)
LIB_CLIP_COPY = $(CLIP_COPY_SRC)
LIB_CLIP_PASTE = $(CLIP_PASTE_SRC)
LIB_CLIP_COUNT = $(CLIP_COUNT_SRC)
LIB_CLIP_PASTE_COUNT = $(CLIP_PASTE_SRC) $(CLIP_COUNT_SRC)
LIB_CLIP_COPY_PASTE_COUNT = $(CLIP_COPY_SRC) $(CLIP_PASTE_SRC) $(CLIP_COUNT_SRC)
LIB_EDITOR = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SRC) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_CALCPLUS = $(TUI_BASE_NAV) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SRC)
LIB_HEXVIEW = $(TUI_BASE_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY) $(RESUME_STATE_SRC)
LIB_CLIPMGR = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA_STATS) $(LIB_CLIP_PASTE_COUNT) $(CLIP_ADMIN_SRC) $(RESUME_STATE_SRC) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_REUVIEWER = $(TUI_BASE_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA_STATS) $(RESUME_STATE_SRC)
LIB_TASKLIST = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SRC) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_SIMPLEFILES = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SRC) $(STORAGE_DEVICE_SRC) $(FILE_BROWSER_SRC)
LIB_SIMPLECELLS = $(TUI_BASE_MENU_INPUT_NAV_MISC) $(LIB_REU_DMA) $(RESUME_STATE_SRC) $(STORAGE_DEVICE_SRC) $(DIR_PAGE_SRC)
LIB_GAME2048 = $(TUI_BASE_NAV) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SRC)
LIB_DEMINER = $(TUI_BASE_NAV) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SRC)
LIB_CAL26 = $(TUI_BASE_INPUT_NAV_MISC) $(TUI_HOTKEY_SRC) $(LIB_REU_DMA) $(LIB_CLIP_COPY_PASTE_COUNT) $(RESUME_STATE_SRC)
LIB_DIZZY = $(TUI_BASE_INPUT_NAV) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SRC)
LIB_README = $(TUI_BASE_NAV_MISC) $(TUI_HOTKEY_SRC) $(REU_DMA_SRC) $(RESUME_STATE_SRC)
LIB_READYSHELL = $(REU_DMA_SRC) $(RESUME_STATE_SRC)

# Default target
all: $(BOOT) $(PREBOOT) $(SETD71) $(SHOWCFG) $(TEST_REU) $(LAUNCHER) $(EDITOR) $(CALCPLUS) $(HEXVIEW) $(CLIPMGR) $(REUVIEWER) $(TASKLIST) $(SIMPLEFILES) $(SIMPLECELLS) $(GAME2048) $(DEMINER) $(CAL26) $(DIZZY) $(READMEAPP) $(READYSHELL) $(DISK1) $(DISK2)
	@echo ""
	@echo "=== Build complete ==="
	@ls -la *.prg $(DISK1) $(DISK2)

# Boot loader (assembly version for size control)
$(BOOT): $(BOOT_DIR)/boot_asm.s $(VERSION_ASM_INC)
	$(AS) -o obj/boot.o $<
	$(LD) -C $(CFG_DIR)/boot_asm.cfg -o $@ obj/boot.o

# C64 BASIC preboot loader (sets D71 mode then chains to boot)
$(PREBOOT): $(BOOT_DIR)/preboot.bas
	$(PETCAT) -w2 -o $@ $<

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

# Build apps.cfg payload in strict lowercase PETASCII
$(CATALOG_SEQ): $(CATALOG_SRC) $(BUILD_SUPPORT_DIR)/build_apps_catalog_petscii.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_apps_catalog_petscii.py --input $(CATALOG_SRC) --output $@

# Build plain-text lowercase PETASCII SEQ payloads
$(EDITOR_HELP_SEQ): $(EDITOR_HELP_SRC) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(EDITOR_HELP_SRC) --output $@

$(XFILECHK_SRC8_SEQ): $(XFILECHK_SRC8_TXT) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(XFILECHK_SRC8_TXT) --output $@

$(XFILECHK_TESTA_SEQ): $(XFILECHK_TESTA_TXT) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py
	$(PYTHON) $(BUILD_SUPPORT_DIR)/build_petscii_lower_seq.py --input $(XFILECHK_TESTA_TXT) --output $@

# Test REU program (standalone)
$(TEST_REU): $(SRC_DIR)/test_reu.c
	$(CC) $(CFLAGS) -o $@ $<

# Launcher app (loads at $1000)
$(LAUNCHER): $(APPS_DIR)/launcher/launcher.c $(LIB_LAUNCHER) $(VERSION_HEADER)
	$(CC) $(LAUNCHER_CFLAGS) -m $(OBJ_DIR)/launcher.map -o $@ $(APPS_DIR)/launcher/launcher.c $(LIB_LAUNCHER)

# Editor app (loads at $1000)
$(EDITOR): $(APPS_DIR)/editor/editor.c $(LIB_EDITOR)
	$(CC) $(EDITOR_CFLAGS) -m $(OBJ_DIR)/editor.map -o $@ $^

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
$(SIMPLEFILES): $(APPS_DIR)/simplefiles/simplefiles.c $(LIB_SIMPLEFILES)
	$(CC) $(APP_CFLAGS) -m $(OBJ_DIR)/simplefiles.map -o $@ $^

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

# ReadyShell app (loads at $1000) + overlay sidecars
$(READYSHELL): $(READYSHELL_RESIDENT_OBJS) $(READYSHELL_OVERLAY1_OBJS) $(READYSHELL_OVERLAY2_OBJS) $(READYSHELL_OVERLAY3_OBJS) $(CFG_DIR)/ready_app_overlay.cfg
	$(CC) -t c64 -C $(CFG_DIR)/ready_app_overlay.cfg \
		-Wl -D,__OVERLAYSIZE__=$(READYSHELL_OVERLAYSIZE) \
		-Wl -D,__STACKSIZE__=$(READYSHELL_STACKSIZE) \
		-m $(OBJ_DIR)/readyshell.map -o $@ \
		$(READYSHELL_RESIDENT_OBJS) $(READYSHELL_OVERLAY1_OBJS) $(READYSHELL_OVERLAY2_OBJS) $(READYSHELL_OVERLAY3_OBJS)
	cp -f $(READYSHELL_OVL1_PRG) $(READYSHELL_OVL1_DISK)
	cp -f $(READYSHELL_OVL2_PRG) $(READYSHELL_OVL2_DISK)
	cp -f $(READYSHELL_OVL3_PRG) $(READYSHELL_OVL3_DISK)

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

# Create disk 1 (drive 8): boot chain + launcher + utilities + cal26 + dizzy + readyshell + catalog
ifeq ($(READYOS_USE_PWSH),1)
$(DISK1): FORCE $(BOOT) $(PREBOOT) $(SETD71) $(SHOWCFG) $(LAUNCHER) $(DEMINER) $(CAL26) $(DIZZY) $(READYSHELL) $(READYSHELL_OVL1_DISK) $(READYSHELL_OVL2_DISK) $(READYSHELL_OVL3_DISK) $(CATALOG_SEQ) $(EDITOR_HELP_SEQ)
	$(PWSH) -NoLogo -NoProfile -File $(BUILD_SUPPORT_DIR)/rebuild_disk.ps1 -Mode disk1 -DiskPath $@ -BuildSupportDir $(BUILD_SUPPORT_DIR) -RelSeedD71 "$(REL_SEED_D71)"
else
$(DISK1): FORCE $(BOOT) $(PREBOOT) $(SETD71) $(SHOWCFG) $(LAUNCHER) $(DEMINER) $(CAL26) $(DIZZY) $(READYSHELL) $(READYSHELL_OVL1_DISK) $(READYSHELL_OVL2_DISK) $(READYSHELL_OVL3_DISK) $(CATALOG_SEQ) $(EDITOR_HELP_SEQ)
	@set -e; \
	PRESERVE_DIR=$$(mktemp -d /tmp/readyos_preserve.XXXXXX); \
	if [ -f $@ ]; then \
		$(BUILD_SUPPORT_DIR)/preserve_d71_user_data.sh backup $@ $$PRESERVE_DIR; \
	fi; \
	$(C1541) -format "readyos,ro" d71 $@ \
		-write $(PREBOOT) preboot \
		-write $(SETD71) setd71 \
		-write $(SHOWCFG) showcfg \
		-write $(BOOT) boot \
		-write $(LAUNCHER) launcher \
		-write $(DEMINER) deminer \
		-write $(CAL26) cal26 \
		-write $(DIZZY) dizzy \
		-write $(READYSHELL) readyshell \
		-write $(READYSHELL_OVL1_DISK) rsovl1 \
		-write $(READYSHELL_OVL2_DISK) rsovl2 \
			-write $(READYSHELL_OVL3_DISK) rsovl3 \
			-write $(CATALOG_SEQ) "apps.cfg,s" \
			-write $(EDITOR_HELP_SEQ) "editor help,s"; \
	echo ""; \
	echo "Seeding CAL26 REL files:"; \
	python3 $(BUILD_SUPPORT_DIR)/seed_cal26_rel.py --disk $@; \
	if [ -s $$PRESERVE_DIR/manifest.tsv ]; then \
		$(BUILD_SUPPORT_DIR)/preserve_d71_user_data.sh restore $@ $$PRESERVE_DIR/manifest.tsv; \
	fi; \
	if [ -n "$(REL_SEED_D71)" ] && [ -f "$(REL_SEED_D71)" ]; then \
		echo "Restoring REL files from $(REL_SEED_D71) ..."; \
		$(BUILD_SUPPORT_DIR)/recover_cal26_rel_from_d71.sh "$(REL_SEED_D71)" "$@"; \
	elif [ -n "$(REL_SEED_D71)" ]; then \
		echo "warning: REL donor disk not found: $(REL_SEED_D71)"; \
	else \
		echo "note: no REL donor disk configured; skipping REL restore"; \
	fi; \
	rm -rf $$PRESERVE_DIR
	@echo ""
	@echo "Disk contents:"
	@$(C1541) $@ -list
endif

# Create disk 2 (drive 9): remaining apps
ifeq ($(READYOS_USE_PWSH),1)
$(DISK2): FORCE $(EDITOR) $(CALCPLUS) $(HEXVIEW) $(CLIPMGR) $(REUVIEWER) $(TASKLIST) $(SIMPLEFILES) $(SIMPLECELLS) $(GAME2048) $(READMEAPP)
	$(PWSH) -NoLogo -NoProfile -File $(BUILD_SUPPORT_DIR)/rebuild_disk.ps1 -Mode disk2 -DiskPath $@ -BuildSupportDir $(BUILD_SUPPORT_DIR)
else
$(DISK2): FORCE $(EDITOR) $(CALCPLUS) $(HEXVIEW) $(CLIPMGR) $(REUVIEWER) $(TASKLIST) $(SIMPLEFILES) $(SIMPLECELLS) $(GAME2048) $(READMEAPP)
	@set -e; \
	PRESERVE_DIR=$$(mktemp -d /tmp/readyos2_preserve.XXXXXX); \
	if [ -f $@ ]; then \
		$(BUILD_SUPPORT_DIR)/preserve_d71_user_data.sh backup $@ $$PRESERVE_DIR; \
	fi; \
	$(C1541) -format "readyos2,ro" d71 $@ \
		-write $(EDITOR) editor \
		-write $(CALCPLUS) calcplus \
		-write $(HEXVIEW) hexview \
		-write $(CLIPMGR) clipmgr \
		-write $(REUVIEWER) reuviewer \
			-write $(TASKLIST) tasklist \
			-write $(SIMPLEFILES) simplefiles \
			-write $(SIMPLECELLS) simplecells \
			-write $(GAME2048) game2048 \
			-write $(READMEAPP) readme; \
	if [ -s $$PRESERVE_DIR/manifest.tsv ]; then \
		$(BUILD_SUPPORT_DIR)/preserve_d71_user_data.sh restore $@ $$PRESERVE_DIR/manifest.tsv; \
	fi; \
	rm -rf $$PRESERVE_DIR
	@echo ""
	@echo "Disk contents:"
	@$(C1541) $@ -list
endif

# Clean
clean:
	rm -f $(OBJ_DIR)/*.o
	rm -f $(OBJ_DIR)/*.map
	rm -rf $(READYSHELL_OBJ_DIR)
	rm -f $(CATALOG_SEQ)
	rm -f $(EDITOR_HELP_SEQ)
	rm -f *.prg
	rm -f $(READYSHELL_OVL1_PRG) $(READYSHELL_OVL2_PRG) $(READYSHELL_OVL3_PRG) \
		$(READYSHELL_OVL4_PRG) $(READYSHELL_OVL5_PRG) $(READYSHELL_OVL6_PRG) \
		$(READYSHELL_OVL7_PRG) $(READYSHELL_OVL8_PRG) $(READYSHELL_OVL9_PRG)
	rm -f $(READYSHELL_OVL1_DISK) $(READYSHELL_OVL2_DISK) $(READYSHELL_OVL3_DISK)
	rm -f *.d64
	rm -f *.d71

# Verify all generated binaries and memory layout constraints
verify: all
	python3 verify.py
	python3 $(BUILD_SUPPORT_DIR)/editor_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/tasklist_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/simplefiles_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/verify_resume_contract.py
	python3 $(BUILD_SUPPORT_DIR)/verify_memory_map.py

# Full rebuild + deep verification
fullcheck: clean all
	python3 verify.py
	python3 $(BUILD_SUPPORT_DIR)/editor_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/tasklist_host_smoke.py
	python3 $(BUILD_SUPPORT_DIR)/verify_resume_contract.py
	python3 $(BUILD_SUPPORT_DIR)/verify_memory_map.py

# Warm-resume contract checks only
verify-resume:
	python3 $(BUILD_SUPPORT_DIR)/verify_resume_contract.py

# Fast host-side parser smoke checks (no VICE, no C64 memory mapping).
readyshell-parse-smoke-host:
	$(CLANG) -std=c99 -Wall -Wextra -I. -I$(READYSHELL_CORE_DIR) \
		$(BUILD_SUPPORT_DIR)/readyshell_parse_smoke.c \
		$(READYSHELL_CORE_DIR)/rs_parse.c \
		$(READYSHELL_CORE_DIR)/rs_lexer.c \
		$(READYSHELL_CORE_DIR)/rs_token.c \
		$(READYSHELL_CORE_DIR)/rs_errors.c \
		-o /tmp/readyshell_parse_smoke
	/tmp/readyshell_parse_smoke

editor-smoke-host:
	python3 $(BUILD_SUPPORT_DIR)/editor_host_smoke.py

tasklist-smoke-host:
	python3 $(BUILD_SUPPORT_DIR)/tasklist_host_smoke.py

# Run Ready OS (boot loader)
run: $(DISK1) $(DISK2)
	x64sc -reu -reusize 16384 -drive8type 1571 -drive8truedrive -devicebackend8 0 +busdevice8 -drive9type 1571 -drive9truedrive -devicebackend9 0 +busdevice9 -8 $(DISK1) -9 $(DISK2) -autostartprgmode 1 $(PREBOOT)

# Run test_reu standalone
run-test: $(TEST_REU)
	x64sc -reu -reusize 16384 -autostartprgmode 1 $<

# Help
help:
	@echo "Ready OS Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build all programs and both disk images (default)"
	@echo "  clean       - Remove built files"
	@echo "  verify      - Build and run deep binary verification"
	@echo "                (includes hard memory-map gate)"
	@echo "  verify-resume - Run warm-resume contract verification"
	@echo "  fullcheck   - Clean rebuild and deep binary verification"
	@echo "  editor-smoke-host - Run Editor host-side smoke checks"
	@echo "  tasklist-smoke-host - Run Tasklist host-side smoke checks"
	@echo "  seed-cal26  - Seed CAL26 REL files on readyos.d71 with sample events"
	@echo "  launcher-verbose - Rebuild launcher with verbose config diagnostics"
	@echo "  run         - Run Ready OS in VICE"
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
	@echo "  calcplus.prg - Calculator Plus (loads at \$$1000)"
	@echo "  hexview.prg  - Hex memory viewer (loads at \$$1000)"
	@echo "  clipmgr.prg  - Clipboard manager (loads at \$$1000)"
	@echo "  reuviewer.prg- REU memory viewer (loads at \$$1000)"
	@echo "  game2048.prg - 2048 puzzle game (loads at \$$1000)"
	@echo "  deminer.prg  - PETSCII minesweeper game (loads at \$$1000)"
	@echo "  cal26.prg    - Calendar 2026 app (loads at \$$1000)"
	@echo "  dizzy.prg    - Kanban task board app (loads at \$$1000)"
	@echo "  readme.prg   - Project README app (loads at \$$1000)"
	@echo "  readyshell.prg - ReadyShell app (loads at \$$1000, overlays rsovl1/2/3 on disk 1)"
	@echo "  $(XFILECHK) - Standalone IEC file-operation harness (loads at \$$0801)"
	@echo "  readyos.d71   - Disk 1 (boot/launcher/showcfg/deminer/cal26/dizzy/readyshell/rsovl1-3/apps.cfg/editor help)"
	@echo "  readyos_2.d71 - Disk 2 (editor/calcplus/hexview/clipmgr/reuviewer/tasklist/simplefiles/simplecells/2048/readme)"
	@echo "  $(XFILECHK_DISK1) - Standalone harness drive 8 disk (boot+harness+src fixture)"
	@echo "  $(XFILECHK_DISK2) - Standalone harness drive 9 disk (test fixture)"

FORCE:

# Seed CAL26 REL files with valid initial data and 2 sample events
seed-cal26: $(DISK1)
	python3 $(BUILD_SUPPORT_DIR)/seed_cal26_rel.py --disk $(DISK1)

# Headless VICE REL probe (xrelchk + monitor log parse)
probe-rel:
	$(VICE_DEBUG_TOOLS_DIR)/vice_headless_rel_probe.sh

# Build launcher with expanded on-screen config diagnostics
launcher-verbose:
	$(MAKE) LAUNCHER_CFG_VERBOSE=1 $(LAUNCHER)

.PHONY: all clean verify verify-resume fullcheck help run run-test seed-cal26 probe-rel launcher-verbose readyshell-parse-smoke-host editor-smoke-host tasklist-smoke-host FORCE
