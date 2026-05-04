.setcpu "6502"

.export easyflash_shim_copy_done
.export easyflash_preload_verify_done
.export easyflash_after_kernal_init
.export easyflash_after_launcher_restore
.export easyflash_before_launcher_jump

.segment "STARTUP"

CART_BANK    = $DE00
CART_CONTROL_EX = $DE01
CART_CONTROL = $DE02
KERNAL_CINT  = $FF81
KERNAL_IOINIT = $FF84
KERNAL_RESTOR = $FF8A
KERNAL_CLRCHN = $FFCC
KERNAL_BSOUT = $FFD2
KERNAL_GETIN = $FFE4
BASIC_COLD_START = $FCE2
KERNAL_KEYLOG = $0289
KERNAL_DELAY = $028B
KERNAL_KOUNT = $028C
KERNAL_SHFLAG = $028D
KERNAL_SFDX = $CB
KERNAL_BLNSW = $CC
KERNAL_BLNCT = $CD
KERNAL_GDBLN = $CE
KERNAL_BLNON = $CF
KERNAL_PNTR = $D3
KERNAL_HIBASE = $0288
KERNAL_COLOR = $0286
KERNAL_CURSOR_COLOR = $0287
KERNAL_INSW = $0291
KERNAL_AUTODN = $0292

REU_COMMAND  = $DF01
REU_C64_LO   = $DF02
REU_C64_HI   = $DF03
REU_REU_LO   = $DF04
REU_REU_HI   = $DF05
REU_REU_BANK = $DF06
REU_LEN_LO   = $DF07
REU_LEN_HI   = $DF08
VIC_CTRL1    = $D011
VIC_CTRL2    = $D016
VIC_MEM      = $D018
VIC_SPR_EN   = $D015
VIC_IRQ_EN   = $D01A
VIC_IRQ_ACK  = $D019
CIA2_PRA     = $DD00
CIA2_DDRA    = $DD02
CIA1_ICR     = $DC0D
CIA2_ICR     = $DD0D
CH_FONT_LOWER = $0E
VIC_MEM_LOWERCASE = $16

REU_CMD_STASH = $90
REU_CMD_FETCH = $91

APP_LOAD_START    = $1000
APP_WINDOW_LEN    = $B600
VERIFY_BUF        = $4000
VERIFY_CHUNK_LEN  = $2000
OVL_STAGE_LEN     = $3800
OVL_META_RAM      = $C7F0
SCREEN_RAM        = $0400
BOOT_LINE0        = SCREEN_RAM + (40 * 0)
BOOT_LINE1        = SCREEN_RAM + (40 * 1)
BOOT_STATUS_LINE  = SCREEN_RAM + (40 * 3)
BOOT_DETAIL_LINE  = SCREEN_RAM + (40 * 5)
BOOT_PROMPT_LINE  = SCREEN_RAM + (40 * 6)
BOOT_BG_COLOR     = $06
BOOT_BORDER_LOADER = $0E
BOOT_BORDER_SHIM   = $05
BOOT_BORDER_CART   = $07
BOOT_BORDER_REU    = $08
BOOT_BORDER_HANDOFF = $0D
BOOT_BORDER_ERROR  = $02
REU_MAGIC_VALUE   = $A5
SHIM_STORAGE_DRIVE = $C839
READYSHELL_OVL_META_VERSION = 2
READYSHELL_OVL_META_VALID_MASK = $FF
READYSHELL_CACHE_BANK = $40
READYSHELL_CACHE_BANK2 = $41
DBG_RING_BASE = $C7A0
DBG_RING_HEAD = $C7DF
DIAG_BASE = $CA00
DIAG_RECORDS = $CA20
DIAG_CURSOR_LO = $CA1E
DIAG_CURSOR_HI = $CA1F
DIAG_TOTAL = $CA04
DIAG_FAILS = $CA05
DIAG_STAGE = $CA06
DIAG_KIND = $CA08
DIAG_INDEX = $CA09
DIAG_STATUS = $CA0A
DIAG_EXPECT_LO = $CA0B
DIAG_EXPECT_HI = $CA0C
DIAG_ACTUAL_LO = $CA0D
DIAG_ACTUAL_HI = $CA0E
DIAG_MISMATCH_LO = $CA0F
DIAG_MISMATCH_HI = $CA10
REU_PROBE_SRC = $CA11
REU_PROBE_DST = $CA12
REU_PROBE_BANK = $FE
APP_TABLE_RAM = $CC00
OVERLAY_TABLE_RAM = $CC90
APP_TABLE_BYTES = EASYFLASH_APP_COUNT * 9
OVERLAY_TABLE_BYTES = EASYFLASH_OVERLAY_COUNT * 12

dst_lo       = $F0
dst_hi       = $F1
rem_lo       = $F2
rem_hi       = $F3
src_lo       = $F4
src_hi       = $F5
chunk_lo     = $F6
chunk_hi     = $F7
cart_bank_zp = $F8
table_lo     = $F9
table_hi     = $FA
reu_bank_zp  = $FB
reu_off_lo   = $FC
reu_off_hi   = $FD
pages_left   = $FE
loop_index   = $FF

start:
    sei
    cld
    ldx #$FF
    txs
    lda #$37
    sta $01
    lda #$2F
    sta $00
    lda #$00
    sta CART_BANK
    lda #$40
    sta CART_CONTROL_EX
    jsr cart_disable_for_reu
    jsr set_boot_background
    jsr set_stage_border_loader
    jsr copy_loader_tail_from_cart
    lda #'S'
    jsr dbg_put
    jsr clear_screen
    jsr draw_boot_header
    jsr reu_probe_present
    bcc @reu_ok
    jmp reu_required_exit_to_basic
@reu_ok:
    lda #'C'
    jsr dbg_put
    jsr show_stage_shim
    jsr copy_shim
    lda #'H'
    jsr dbg_put
easyflash_shim_copy_done:
    jsr init_reu_state
    lda #'R'
    jsr dbg_put
    jsr copy_layout_tables
    jsr show_stage_launcher
    jsr preload_launcher_bank0
    lda #'L'
    jsr dbg_put
    jsr preload_app_banks
    lda #'A'
    jsr dbg_put
    jsr preload_overlay_banks
    lda #'O'
    jsr dbg_put
    jsr show_stage_overlay_meta
    jsr write_overlay_meta
    lda #'M'
    jsr dbg_put
    jsr show_stage_verify
    lda #'V'
    jsr dbg_put
easyflash_preload_verify_done:
    jsr set_stage_border_loader
    jsr cart_disable_for_reu
    lda #$37
    sta $01
    lda #$2F
    sta $00
    jsr KERNAL_IOINIT
    jsr KERNAL_RESTOR
    ; CINT clears the active screen. Cartridge startup can leave VIC banking
    ; pointed somewhere nonstandard, so force the baseline screen at $0400
    ; before calling into the KERNAL editor init or it can wipe page 2/3
    ; vectors instead of screen RAM.
    lda CIA2_DDRA
    ora #$03
    sta CIA2_DDRA
    lda CIA2_PRA
    and #$FC
    ora #$03
    sta CIA2_PRA
    lda #$1B
    sta VIC_CTRL1
    lda #$08
    sta VIC_CTRL2
    lda #$14
    sta VIC_MEM
    lda #$04
    sta KERNAL_HIBASE
    ; Restore the text-mode/editor state expected by the launcher baseline.
    ; EasyFlash startup may leave the active output channel detached from the
    ; screen, which makes CHR$(14) a no-op. Reset channels first, then force
    ; the lowercase charset mapping directly as a hard guard before launcher.
    jsr KERNAL_CINT
    jsr KERNAL_CLRCHN
    lda #VIC_MEM_LOWERCASE
    sta VIC_MEM
    lda #CH_FONT_LOWER
    jsr KERNAL_BSOUT
    ; CINT should now be safe, but restore vectors afterward as a hard guard
    ; for keyboard IRQ scanning before the launcher enables interrupts.
    jsr KERNAL_RESTOR
    jsr restore_kernal_keyboard_state
easyflash_after_kernal_init:
    lda #'T'
    jsr dbg_put
    jsr restore_launcher_ram
easyflash_after_launcher_restore:
    lda #'J'
    jsr dbg_put
    jsr show_stage_handoff
    lda #$04
    sta CART_CONTROL
    lda #'K'
    jsr dbg_put
    lda #$37
    sta $01
    lda #$2F
    sta $00
    lda #$00
    sta VIC_IRQ_EN
    sta VIC_SPR_EN
    lda #$FF
    sta VIC_IRQ_ACK
    jsr set_stage_border_handoff
    ; KERNAL init can leave a CIA interrupt pending; clear the latches so
    ; CLI cannot vector away before the launcher gets its first instruction.
    lda CIA1_ICR
    lda CIA2_ICR
easyflash_before_launcher_jump:
    jmp APP_LOAD_START

set_boot_background:
    lda #BOOT_BG_COLOR
    sta $D021
    rts

set_stage_border_loader:
    lda #BOOT_BORDER_LOADER
    sta $D020
    rts

set_stage_border_shim:
    lda #BOOT_BORDER_SHIM
    sta $D020
    rts

set_stage_border_cart:
    lda #BOOT_BORDER_CART
    sta $D020
    rts

set_stage_border_reu:
    lda #BOOT_BORDER_REU
    sta $D020
    rts

set_stage_border_handoff:
    lda #BOOT_BORDER_HANDOFF
    sta $D020
    rts

set_stage_border_error:
    lda #BOOT_BORDER_ERROR
    sta $D020
    rts

dbg_put:
    ldx DBG_RING_HEAD
    sta DBG_RING_BASE,x
    inx
    cpx #$3F
    bcc :+
    ldx #$00
:
    stx DBG_RING_HEAD
    rts

cart_enable_for_copy:
    lda #$07
    sta CART_CONTROL
    rts

cart_disable_for_reu:
    lda #$04
    sta CART_CONTROL
    rts

restore_kernal_keyboard_state:
    lda #$0A
    sta KERNAL_KEYLOG
    sta KERNAL_KOUNT
    lda #$04
    sta KERNAL_DELAY
    lda #$0C
    sta KERNAL_BLNCT
    sta KERNAL_BLNSW
    lda #$00
    sta KERNAL_INSW
    sta KERNAL_BLNON
    sta KERNAL_SHFLAG
    sta KERNAL_SFDX
    sta KERNAL_AUTODN
    lda #$0E
    sta KERNAL_COLOR
    sta KERNAL_CURSOR_COLOR
    lda #$00
    sta KERNAL_PNTR
    rts

clear_screen:
    lda #$20
    ldx #$00
@screen_loop:
    sta $0400,x
    sta $0500,x
    sta $0600,x
    sta $06E8,x
    pha
    lda #$01
    sta $D800,x
    sta $D900,x
    sta $DA00,x
    sta $DAE8,x
    pla
    inx
    bne @screen_loop
    rts

clear_row:
    ldy #$00
    lda #$20
@clear_row_loop:
    sta (dst_lo),y
    iny
    cpy #40
    bcc @clear_row_loop
    rts

ascii_to_screen:
    cmp #'A'
    bcc @done
    cmp #'Z'+1
    bcc @upper
    cmp #'a'
    bcc @done
    cmp #'z'+1
    bcs @done
    sec
    sbc #'a'-1
    rts
@upper:
    sec
    sbc #'A'-1
@done:
    rts

print_row_from_src:
    jsr clear_row
    ldy #$00
@print_row_loop:
    lda (src_lo),y
    beq @done
    jsr ascii_to_screen
    sta (dst_lo),y
    iny
    cpy #40
    bcc @print_row_loop
@done:
    rts

draw_boot_header:
    lda #<msg_banner
    sta src_lo
    lda #>msg_banner
    sta src_hi
    lda #<BOOT_LINE0
    sta dst_lo
    lda #>BOOT_LINE0
    sta dst_hi
    jsr print_row_from_src
    lda #<msg_subtitle
    sta src_lo
    lda #>msg_subtitle
    sta src_hi
    lda #<BOOT_LINE1
    sta dst_lo
    lda #>BOOT_LINE1
    sta dst_hi
    jmp print_row_from_src

show_status_literal:
    lda #<BOOT_STATUS_LINE
    sta dst_lo
    lda #>BOOT_STATUS_LINE
    sta dst_hi
    jsr print_row_from_src
    lda #<BOOT_DETAIL_LINE
    sta dst_lo
    lda #>BOOT_DETAIL_LINE
    sta dst_hi
    jmp clear_row

show_stage_shim:
    jsr set_stage_border_shim
    lda #<msg_stage_shim
    sta src_lo
    lda #>msg_stage_shim
    sta src_hi
    jmp show_status_literal

show_stage_launcher:
    jsr set_stage_border_loader
    lda #<msg_stage_launcher
    sta src_lo
    lda #>msg_stage_launcher
    sta src_hi
    jmp show_status_literal

show_stage_overlay_meta:
    jsr set_stage_border_loader
    lda #<msg_stage_overlay_meta
    sta src_lo
    lda #>msg_stage_overlay_meta
    sta src_hi
    jmp show_status_literal

show_stage_verify:
    jsr set_stage_border_loader
    lda #<msg_stage_verify
    sta src_lo
    lda #>msg_stage_verify
    sta src_hi
    jmp show_status_literal

show_stage_handoff:
    jsr set_stage_border_handoff
    lda #<msg_stage_handoff
    sta src_lo
    lda #>msg_stage_handoff
    sta src_hi
    jmp show_status_literal

show_reu_required_message:
    jsr set_stage_border_error
    lda #<msg_stage_reu_missing
    sta src_lo
    lda #>msg_stage_reu_missing
    sta src_hi
    lda #<BOOT_STATUS_LINE
    sta dst_lo
    lda #>BOOT_STATUS_LINE
    sta dst_hi
    jsr print_row_from_src
    lda #<msg_detail_reu_missing
    sta src_lo
    lda #>msg_detail_reu_missing
    sta src_hi
    lda #<BOOT_DETAIL_LINE
    sta dst_lo
    lda #>BOOT_DETAIL_LINE
    sta dst_hi
    jsr print_row_from_src
    lda #<msg_detail_reu_prompt
    sta src_lo
    lda #>msg_detail_reu_prompt
    sta src_hi
    lda #<BOOT_PROMPT_LINE
    sta dst_lo
    lda #>BOOT_PROMPT_LINE
    sta dst_hi
    jmp print_row_from_src

write_two_digits_at:
    sty chunk_hi
    ldx #'0'
@digit_tens:
    cmp #10
    bcc @digit_store
    sec
    sbc #10
    inx
    bne @digit_tens
@digit_store:
    pha
    txa
    jsr ascii_to_screen
    ldy chunk_hi
    sta (dst_lo),y
    iny
    pla
    clc
    adc #'0'
    jsr ascii_to_screen
    sta (dst_lo),y
    rts

show_app_progress:
    jsr set_stage_border_loader
    lda #<msg_stage_apps
    sta src_lo
    lda #>msg_stage_apps
    sta src_hi
    jsr show_status_literal
    lda #<msg_app_template
    sta src_lo
    lda #>msg_app_template
    sta src_hi
    lda #<BOOT_DETAIL_LINE
    sta dst_lo
    lda #>BOOT_DETAIL_LINE
    sta dst_hi
    jsr print_row_from_src
    lda #EASYFLASH_APP_COUNT + 1
    sec
    sbc loop_index
    ldy #4
    jsr write_two_digits_at
    lda #EASYFLASH_APP_COUNT
    ldy #7
    jsr write_two_digits_at
    lda reu_bank_zp
    ldy #14
    jmp write_two_digits_at

show_overlay_progress:
    jsr set_stage_border_loader
    lda #<msg_stage_overlays
    sta src_lo
    lda #>msg_stage_overlays
    sta src_hi
    jsr show_status_literal
    lda #<msg_overlay_template
    sta src_lo
    lda #>msg_overlay_template
    sta src_hi
    lda #<BOOT_DETAIL_LINE
    sta dst_lo
    lda #>BOOT_DETAIL_LINE
    sta dst_hi
    jsr print_row_from_src
    lda #EASYFLASH_OVERLAY_COUNT + 1
    sec
    sbc loop_index
    ldy #4
    jsr write_two_digits_at
    lda #EASYFLASH_OVERLAY_COUNT
    ldy #7
    jsr write_two_digits_at
    lda reu_bank_zp
    ldy #14
    jmp write_two_digits_at

copy_shim:
    ldx #$00
@shim_loop:
    lda shim_data,x
    sta $C800,x
    lda shim_data+$0100,x
    sta $C900,x
    inx
    bne @shim_loop
    rts

copy_layout_tables:
    ldx #$00
@app_table_loop:
    lda easyflash_app_table,x
    sta APP_TABLE_RAM,x
    inx
    cpx #APP_TABLE_BYTES
    bne @app_table_loop
    ldx #$00
@overlay_table_loop:
    lda easyflash_overlay_table,x
    sta OVERLAY_TABLE_RAM,x
    inx
    cpx #OVERLAY_TABLE_BYTES
    bne @overlay_table_loop
    rts

copy_loader_tail_from_cart:
    jsr cart_enable_for_copy
    lda #$00
    sta CART_BANK
    lda #$00
    sta src_lo
    lda #$88
    sta src_hi
    lda #$00
    sta dst_lo
    lda #$10
    sta dst_hi
    lda #$18
    sta pages_left
@tail_page_loop:
    ldy #$00
@tail_byte_loop:
    lda (src_lo),y
    sta (dst_lo),y
    iny
    bne @tail_byte_loop
    inc src_hi
    inc dst_hi
    dec pages_left
    bne @tail_page_loop
    jmp cart_disable_for_reu

reu_probe_present:
    lda #$A5
    sta REU_PROBE_SRC
    lda #$00
    sta REU_PROBE_DST
    lda #<REU_PROBE_SRC
    sta dst_lo
    lda #>REU_PROBE_SRC
    sta dst_hi
    lda #REU_PROBE_BANK
    sta reu_bank_zp
    lda #$00
    sta reu_off_lo
    sta reu_off_hi
    lda #$01
    sta rem_lo
    lda #$00
    sta rem_hi
    jsr reu_stash_window
    lda #<REU_PROBE_DST
    sta dst_lo
    lda #>REU_PROBE_DST
    sta dst_hi
    lda #REU_PROBE_BANK
    sta reu_bank_zp
    lda #$00
    sta reu_off_lo
    sta reu_off_hi
    lda #$01
    sta rem_lo
    lda #$00
    sta rem_hi
    jsr reu_fetch_window
    lda REU_PROBE_DST
    cmp REU_PROBE_SRC
    beq @present
    sec
    rts
@present:
    clc
    rts

reu_required_exit_to_basic:
    jsr cart_disable_for_reu
    lda #$37
    sta $01
    lda #$2F
    sta $00
    jsr KERNAL_IOINIT
    jsr KERNAL_RESTOR
    lda CIA2_DDRA
    ora #$03
    sta CIA2_DDRA
    lda CIA2_PRA
    and #$FC
    ora #$03
    sta CIA2_PRA
    lda #$1B
    sta VIC_CTRL1
    lda #$08
    sta VIC_CTRL2
    lda #$14
    sta VIC_MEM
    lda #$04
    sta KERNAL_HIBASE
    jsr KERNAL_CINT
    jsr KERNAL_CLRCHN
    lda #VIC_MEM_LOWERCASE
    sta VIC_MEM
    lda #CH_FONT_LOWER
    jsr KERNAL_BSOUT
    jsr KERNAL_RESTOR
    jsr restore_kernal_keyboard_state
    jsr clear_screen
    jsr draw_boot_header
    jsr show_reu_required_message
    lda CIA1_ICR
    lda CIA2_ICR
    cli
@wait_key:
    jsr KERNAL_GETIN
    beq @wait_key
    sei
    lda CIA1_ICR
    lda CIA2_ICR
    jmp BASIC_COLD_START

init_reu_state:
    ldx #$00
    lda #$00
@reu_loop:
    sta $C600,x
    sta $C700,x
    inx
    bne @reu_loop
    lda #REU_MAGIC_VALUE
    sta $C700
    lda #$08
    sta SHIM_STORAGE_DRIVE
    lda #$FF
    sta $C835
    rts

preload_launcher_bank0:
    jsr clear_app_window
    lda #EASYFLASH_LAUNCHER_BANK
    sta cart_bank_zp
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #<EASYFLASH_LAUNCHER_PAYLOAD_LEN
    sta rem_lo
    lda #>EASYFLASH_LAUNCHER_PAYLOAD_LEN
    sta rem_hi
    jsr copy_payload_from_cart
    lda #$00
    sta reu_bank_zp
    lda #$00
    sta reu_off_lo
    sta reu_off_hi
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #<APP_WINDOW_LEN
    sta rem_lo
    lda #>APP_WINDOW_LEN
    sta rem_hi
    jsr reu_stash_window
    rts

restore_launcher_ram:
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #$00
    sta reu_bank_zp
    sta reu_off_lo
    sta reu_off_hi
    lda #<APP_WINDOW_LEN
    sta rem_lo
    lda #>APP_WINDOW_LEN
    sta rem_hi
    jmp reu_fetch_window

preload_app_banks:
    lda #<APP_TABLE_RAM
    sta table_lo
    lda #>APP_TABLE_RAM
    sta table_hi
    lda #EASYFLASH_APP_COUNT
    sta loop_index
@app_loop:
    lda loop_index
    beq @done
    jsr clear_app_window
    ldy #$00
    lda (table_lo),y
    sta reu_bank_zp
    iny
    lda (table_lo),y
    sta cart_bank_zp
    iny
    iny
    lda (table_lo),y
    sta dst_lo
    iny
    lda (table_lo),y
    sta dst_hi
    iny
    lda (table_lo),y
    sta rem_lo
    iny
    lda (table_lo),y
    sta rem_hi
    jsr show_app_progress
    ldy #$03
    lda (table_lo),y
    sta dst_lo
    iny
    lda (table_lo),y
    sta dst_hi
    jsr copy_payload_from_cart
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #$00
    sta reu_off_lo
    sta reu_off_hi
    lda #<APP_WINDOW_LEN
    sta rem_lo
    lda #>APP_WINDOW_LEN
    sta rem_hi
    jsr reu_stash_window
    lda reu_bank_zp
    jsr set_bitmap
    clc
    lda table_lo
    adc #9
    sta table_lo
    bcc @no_carry
    inc table_hi
@no_carry:
    dec loop_index
    jmp @app_loop
@done:
    rts

preload_overlay_banks:
    lda #<OVERLAY_TABLE_RAM
    sta table_lo
    lda #>OVERLAY_TABLE_RAM
    sta table_hi
    lda #EASYFLASH_OVERLAY_COUNT
    sta loop_index
@ovl_loop:
    lda loop_index
    beq @done
    jsr clear_overlay_stage
    ldy #$00
    lda (table_lo),y
    sta cart_bank_zp
    iny
    iny
    iny
    iny
    lda (table_lo),y
    sta rem_lo
    iny
    lda (table_lo),y
    sta rem_hi
    iny
    iny
    iny
    lda (table_lo),y
    sta reu_bank_zp
    iny
    lda (table_lo),y
    sta reu_off_lo
    iny
    lda (table_lo),y
    sta reu_off_hi
    jsr show_overlay_progress
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    jsr copy_payload_from_cart
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #<OVL_STAGE_LEN
    sta rem_lo
    lda #>OVL_STAGE_LEN
    sta rem_hi
    jsr reu_stash_window
    clc
    lda table_lo
    adc #12
    sta table_lo
    bcc @ovl_no_carry
    inc table_hi
@ovl_no_carry:
    dec loop_index
    jmp @ovl_loop
@done:
    rts

write_overlay_meta:
    lda #'O'
    sta OVL_META_RAM+0
    lda #'V'
    sta OVL_META_RAM+1
    lda #READYSHELL_OVL_META_VERSION
    sta OVL_META_RAM+2
    lda #READYSHELL_OVL_META_VALID_MASK
    sta OVL_META_RAM+3
    lda #READYSHELL_CACHE_BANK
    sta OVL_META_RAM+4
    lda #READYSHELL_CACHE_BANK2
    sta OVL_META_RAM+5
    lda #<OVL_STAGE_LEN
    sta OVL_META_RAM+6
    lda #>OVL_STAGE_LEN
    sta OVL_META_RAM+7
    lda #$00
    sta OVL_META_RAM+8
    sta OVL_META_RAM+9
    sta OVL_META_RAM+10
    sta OVL_META_RAM+11

    lda #$48
    sta reu_bank_zp
    lda #<$80F0
    sta reu_off_lo
    lda #>$80F0
    sta reu_off_hi
    lda #<OVL_META_RAM
    sta dst_lo
    lda #>OVL_META_RAM
    sta dst_hi
    lda #12
    sta rem_lo
    lda #$00
    sta rem_hi
    jsr reu_stash_window
    rts

clear_app_window:
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #<APP_WINDOW_LEN
    sta rem_lo
    lda #>APP_WINDOW_LEN
    sta rem_hi
    jmp clear_region

clear_overlay_stage:
    lda #<APP_LOAD_START
    sta dst_lo
    lda #>APP_LOAD_START
    sta dst_hi
    lda #<OVL_STAGE_LEN
    sta rem_lo
    lda #>OVL_STAGE_LEN
    sta rem_hi
    jmp clear_region

clear_region:
    lda rem_lo
    ora rem_hi
    beq @done
    lda #$00
    sta chunk_lo
    sta chunk_hi
@page_loop:
    lda rem_hi
    beq @tail
    ldy #$00
    lda #$00
@byte_loop:
    sta (dst_lo),y
    iny
    bne @byte_loop
    inc dst_hi
    dec rem_hi
    bne @more
    lda rem_lo
    beq @done
@tail:
    ldy #$00
    lda #$00
@tail_loop:
    cpy rem_lo
    beq @done
    sta (dst_lo),y
    iny
    bne @tail_loop
@done:
    rts
@more:
    lda rem_hi
    ora rem_lo
    bne @page_loop
    rts

copy_payload_from_cart:
    jsr set_stage_border_cart
    jsr cart_enable_for_copy
@bank_loop:
    lda rem_lo
    ora rem_hi
    beq @done
    lda cart_bank_zp
    sta CART_BANK
    lda #<$8000
    sta src_lo
    lda #>$8000
    sta src_hi
    jsr copy_cart_half
    lda rem_lo
    ora rem_hi
    beq @advance_bank
    lda #<$A000
    sta src_lo
    lda #>$A000
    sta src_hi
    jsr copy_cart_half
@advance_bank:
    inc cart_bank_zp
    jmp @bank_loop
@done:
    jsr cart_disable_for_reu
    rts

copy_cart_half:
    lda rem_hi
    cmp #$20
    bcc @short_chunk
    bne @full_chunk
    lda rem_lo
    beq @full_chunk
@full_chunk:
    lda #$00
    sta chunk_lo
    lda #$20
    sta chunk_hi
    jmp copy_chunk
@short_chunk:
    lda rem_lo
    sta chunk_lo
    lda rem_hi
    sta chunk_hi
    jmp copy_chunk

copy_chunk:
    lda chunk_hi
    sta pages_left
@page_loop:
    lda pages_left
    beq @tail
    ldy #$00
@copy_page:
    lda (src_lo),y
    sta (dst_lo),y
    iny
    bne @copy_page
    inc src_hi
    inc dst_hi
    dec pages_left
    jmp @page_loop
@tail:
    ldy #$00
@tail_loop:
    cpy chunk_lo
    beq @advance
    lda (src_lo),y
    sta (dst_lo),y
    iny
    bne @tail_loop
@advance:
    clc
    lda src_lo
    adc chunk_lo
    sta src_lo
    bcc @src_ok
    inc src_hi
@src_ok:
    clc
    lda dst_lo
    adc chunk_lo
    sta dst_lo
    bcc @dst_ok
    inc dst_hi
@dst_ok:
    sec
    lda rem_lo
    sbc chunk_lo
    sta rem_lo
    lda rem_hi
    sbc chunk_hi
    sta rem_hi
    rts

reu_stash_window:
    jsr set_stage_border_reu
    jsr cart_disable_for_reu
    lda $01
    pha
    and #$FE
    sta $01
    lda dst_lo
    sta REU_C64_LO
    lda dst_hi
    sta REU_C64_HI
    lda reu_off_lo
    sta REU_REU_LO
    lda reu_off_hi
    sta REU_REU_HI
    lda reu_bank_zp
    sta REU_REU_BANK
    lda rem_lo
    sta REU_LEN_LO
    lda rem_hi
    sta REU_LEN_HI
    lda #REU_CMD_STASH
    sta REU_COMMAND
    pla
    sta $01
    rts

reu_fetch_window:
    jsr set_stage_border_reu
    jsr cart_disable_for_reu
    lda $01
    pha
    and #$FE
    sta $01
    lda dst_lo
    sta REU_C64_LO
    lda dst_hi
    sta REU_C64_HI
    lda reu_off_lo
    sta REU_REU_LO
    lda reu_off_hi
    sta REU_REU_HI
    lda reu_bank_zp
    sta REU_REU_BANK
    lda rem_lo
    sta REU_LEN_LO
    lda rem_hi
    sta REU_LEN_HI
    lda #REU_CMD_FETCH
    sta REU_COMMAND
    pla
    sta $01
    rts

set_bitmap:
    cmp #8
    bcc @lo
    cmp #16
    bcc @hi
    cmp #24
    bcs @done
    sec
    sbc #16
    tax
    lda bit_masks,x
    ora $C838
    sta $C838
    rts
@hi:
    sec
    sbc #8
    tax
    lda bit_masks,x
    ora $C837
    sta $C837
    rts
@lo:
    tax
    lda bit_masks,x
    ora $C836
    sta $C836
@done:
    rts

bit_masks:
    .byte $01, $02, $04, $08, $10, $20, $40, $80

.segment "RODATA"

msg_banner:
    .asciiz "READYOS EASYFLASH BOOT"
msg_subtitle:
    .asciiz "BOOTER PRELOADING REU SNAPSHOTS"
msg_stage_shim:
    .asciiz "INSTALLING HISTORICAL SHIM"
msg_stage_launcher:
    .asciiz "PRELOADING LAUNCHER SNAPSHOT"
msg_stage_apps:
    .asciiz "PRELOADING APPLICATION SNAPSHOTS"
msg_stage_overlays:
    .asciiz "PRELOADING OVERLAY SNAPSHOTS"
msg_stage_overlay_meta:
    .asciiz "WRITING OVERLAY CACHE META"
msg_stage_verify:
    .asciiz "VERIFYING PRELOADS"
msg_stage_handoff:
    .asciiz "HANDOFF TO LAUNCHER"
msg_stage_reu_missing:
    .asciiz "REU NOT DETECTED"
msg_detail_reu_missing:
    .asciiz "EASYFLASH REQUIRES REU"
msg_detail_reu_prompt:
    .asciiz "PRESS ANY KEY TO RETURN TO BASIC"
msg_app_template:
    .asciiz "APP 00/00 REU 00"
msg_overlay_template:
    .asciiz "OVL 00/00 REU 00"

.include "../generated/easyflash_layout.inc"

shim_data:
    .incbin "../../bin/easyflash_shim.bin"
