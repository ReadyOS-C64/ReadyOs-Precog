;
; shim.s - Ready OS Shim Jump Table and Entry
; Resident kernel at $0800-$0FFF (2KB)
;
; Jump Table at $0800:
;   $0800: JMP SYS_INIT      - Register app, get ID
;   $0803: JMP SYS_SUSPEND   - Save state, show switcher
;   $0806: JMP SYS_RESUME    - Restore app from REU
;   $0809: JMP SYS_EXIT      - Terminate, free resources
;   $080C: JMP SYS_CLIP_COPY - Write to clipboard
;   $080F: JMP SYS_CLIP_PASTE- Read from clipboard
;   $0812: JMP SYS_DEEPLINK  - Launch app with params
;   $0815: JMP SYS_QUERY     - Query system state
;

.segment "JUMPTBL"

; Syscall jump table - must be at $0800
.export SYS_INIT, SYS_SUSPEND, SYS_RESUME, SYS_EXIT
.export SYS_CLIP_COPY, SYS_CLIP_PASTE, SYS_DEEPLINK, SYS_QUERY

SYS_INIT:       jmp _sys_init
SYS_SUSPEND:    jmp _sys_suspend
SYS_RESUME:     jmp _sys_resume
SYS_EXIT:       jmp _sys_exit
SYS_CLIP_COPY:  jmp _sys_clip_copy
SYS_CLIP_PASTE: jmp _sys_clip_paste
SYS_DEEPLINK:   jmp _sys_deeplink
SYS_QUERY:      jmp _sys_query

.segment "CODE"

; Import syscall implementations
.import _sys_init_impl, _sys_suspend_impl, _sys_resume_impl, _sys_exit_impl
.import _sys_clip_copy_impl, _sys_clip_paste_impl
.import _sys_deeplink_impl, _sys_query_impl

; Export for C code
.export _shim_start, _shim_version

;-----------------------------------------------------------------------------
; Shim version info
;-----------------------------------------------------------------------------
_shim_version:
        .byte "RDYOS"      ; Magic identifier
        .byte $01, $00     ; Version 1.0
        .byte $00          ; Reserved

;-----------------------------------------------------------------------------
; _shim_start - Initialize the shim on system boot
; Called once when Ready OS first loads
;-----------------------------------------------------------------------------
.proc _shim_start
        ; Disable interrupts during init
        sei

        ; Initialize system state
        jsr init_system_state

        ; Detect and initialize REU
        jsr _reu_detect
        beq @no_reu
        jsr init_reu_banks
        jmp @init_done

@no_reu:
        ; No REU found - display error
        jsr display_no_reu_error
        ; Fall through to halt

@init_done:
        ; Enable interrupts
        cli

        ; Return success
        lda #$01
        ldx #$00
        rts
.endproc

;-----------------------------------------------------------------------------
; init_system_state - Initialize shim internal state
;-----------------------------------------------------------------------------
.proc init_system_state
        ; Clear app registry
        ldx #23
        lda #$00
@clear_loop:
        sta app_status,x
        sta app_banks,x
        dex
        bpl @clear_loop

        ; Set current app to none
        lda #$FF
        sta current_app

        ; Clear clipboard
        lda #$00
        sta clip_size
        sta clip_size+1
        sta clip_type

        rts
.endproc

;-----------------------------------------------------------------------------
; init_reu_banks - Initialize REU bank allocation bitmap
;-----------------------------------------------------------------------------
.proc init_reu_banks
        ; Mark banks 0-1 as system (used)
        ; Banks 2-25 are app slots (initially free)
        ; Banks 26-255 are free pool

        ; For now just verify REU is working
        ; Store a test pattern
        lda #$AA
        sta $0400       ; Screen location as temp
        jsr test_reu_transfer
        rts
.endproc

;-----------------------------------------------------------------------------
; test_reu_transfer - Quick REU sanity check
;-----------------------------------------------------------------------------
.proc test_reu_transfer
        ; Will be implemented to verify REU DMA works
        rts
.endproc

;-----------------------------------------------------------------------------
; display_no_reu_error - Show error if REU not detected
;-----------------------------------------------------------------------------
.proc display_no_reu_error
        ; Write "NO REU!" to screen
        ldx #0
@loop:
        lda no_reu_msg,x
        beq @done
        sta $0400,x
        inx
        bne @loop
@done:
        rts

no_reu_msg:
        .byte 14, 15, 32, 18, 5, 21, 33, 0  ; "NO REU!" in screen codes
.endproc

;-----------------------------------------------------------------------------
; Syscall Implementations (stubs for now)
;-----------------------------------------------------------------------------

.proc _sys_init
        ; Register new app
        ; A = app header pointer low, X = high
        ; Returns: A = app_id (0-23) or $FF on error
        jmp _sys_init_impl
.endproc

.proc _sys_suspend
        ; Save current app state and show switcher
        jmp _sys_suspend_impl
.endproc

.proc _sys_resume
        ; A = app_id to resume
        jmp _sys_resume_impl
.endproc

.proc _sys_exit
        ; A = exit code
        jmp _sys_exit_impl
.endproc

.proc _sys_clip_copy
        ; Copy data to clipboard
        ; A/X = data pointer, Y = type, stack = size
        jmp _sys_clip_copy_impl
.endproc

.proc _sys_clip_paste
        ; Paste from clipboard
        ; A/X = buffer pointer, stack = max size
        ; Returns: A/X = bytes copied
        jmp _sys_clip_paste_impl
.endproc

.proc _sys_deeplink
        ; Launch app with parameters
        ; A/X = DeepLink struct pointer
        jmp _sys_deeplink_impl
.endproc

.proc _sys_query
        ; Query system state
        ; A = query type
        ; Returns: value in A/X
        jmp _sys_query_impl
.endproc

;-----------------------------------------------------------------------------
; Shim Data Section
;-----------------------------------------------------------------------------
.segment "DATA"

; Current running app (0-23, $FF = none)
current_app:    .byte $FF

; App status for each slot (0=free, 1=running, 2=suspended)
app_status:     .res 24, 0

; REU bank assignment for each app slot
app_banks:      .res 24, 0

; Clipboard state
clip_type:      .byte 0         ; 0=empty, 1=text, 2=binary
clip_size:      .word 0         ; Size in bytes

;-----------------------------------------------------------------------------
; Import from reu.s
;-----------------------------------------------------------------------------
.import _reu_detect
