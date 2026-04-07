;
; syscalls.s - Ready OS Syscall Implementations
; NOTE: Legacy shim module; production shim is generated from src/boot/boot_asm.s.
;

.export _sys_init_impl, _sys_suspend_impl, _sys_resume_impl, _sys_exit_impl
.export _sys_clip_copy_impl, _sys_clip_paste_impl
.export _sys_deeplink_impl, _sys_query_impl

.import _reu_stash, _reu_fetch
.import _context_save, _context_restore

; System constants
MAX_APPS = 24

; Memory layout constants
ZP_START     = $00
ZP_SIZE      = $100
STACK_START  = $0100
STACK_SIZE   = $100
SCREEN_START = $0400
SCREEN_SIZE  = 1000
COLOR_START  = $D800
COLOR_SIZE   = 1000
APP_START    = $1000
APP_SIZE     = $B600   ; $1000-$C5FF = 46,592 bytes

; REU bank layout for app state (per bank = 64KB)
REU_OFF_HEADER    = $0000  ; 32 bytes - app header + CPU state
REU_OFF_DEEPLINK  = $0020  ; 224 bytes - deep link params
REU_OFF_ZP        = $0100  ; 256 bytes - zero page
REU_OFF_STACK     = $0200  ; 256 bytes - hardware stack
REU_OFF_HWREGS    = $0300  ; 112 bytes - VIC/SID/CIA
REU_OFF_SCREEN    = $0400  ; 1024 bytes - screen memory
REU_OFF_COLOR     = $0800  ; 1024 bytes - color RAM
REU_OFF_APP       = $1000  ; 46592 bytes - app memory snapshot

.segment "DATA"

; App registry
app_count:      .byte 0
app_slots:      .res MAX_APPS, 0    ; 0=free, 1=active, 2=suspended
app_banks:      .res MAX_APPS, 0    ; REU bank for each app
current_app:    .byte $FF           ; Currently running app ID

; Temp storage for CPU state during context switch
saved_a:        .byte 0
saved_x:        .byte 0
saved_y:        .byte 0
saved_sp:       .byte 0
saved_status:   .byte 0

; Clipboard
clip_bank:      .byte 1             ; Clipboard uses bank 1
clip_type:      .byte 0
clip_size:      .word 0

.segment "CODE"

;-----------------------------------------------------------------------------
; _sys_init_impl - Register a new app
; Input: A/X = pointer to ReadyAppHeader
; Output: A = app_id (0-23) or $FF on error
;-----------------------------------------------------------------------------
.proc _sys_init_impl
        ; Store header pointer
        sta ptr1
        stx ptr1+1

        ; Find free slot
        ldx #0
@find_slot:
        lda app_slots,x
        beq @found_slot
        inx
        cpx #MAX_APPS
        bne @find_slot

        ; No free slots
        lda #$FF
        rts

@found_slot:
        ; Mark slot as active
        lda #1
        sta app_slots,x

        ; Assign REU bank (bank 2 + slot number)
        txa
        clc
        adc #2          ; Banks 2-17 for apps
        sta app_banks,x

        ; Set as current app
        stx current_app

        ; Increment app count
        inc app_count

        ; Return app ID in A
        txa
        rts

ptr1:   .word 0
.endproc

;-----------------------------------------------------------------------------
; _sys_suspend_impl - Suspend current app and show switcher
;-----------------------------------------------------------------------------
.proc _sys_suspend_impl
        ; Get current app
        ldx current_app
        bmi @no_app         ; No app running

        ; Save CPU state
        php
        pla
        sta saved_status
        stx saved_x
        sty saved_y
        tsx
        stx saved_sp

        ; Mark app as suspended
        lda #2
        sta app_slots,x

        ; Get REU bank for this app
        lda app_banks,x
        sta temp_bank

        ; Save context to REU
        jsr _context_save

        ; Clear current app
        lda #$FF
        sta current_app

        ; Show app switcher (will be implemented in switcher.c)
        jsr show_switcher

@no_app:
        rts

temp_bank:
        .byte 0
.endproc

;-----------------------------------------------------------------------------
; _sys_resume_impl - Resume a suspended app
; Input: A = app_id to resume
;-----------------------------------------------------------------------------
.proc _sys_resume_impl
        ; Validate app ID
        cmp #MAX_APPS
        bcs @invalid

        ; Check if app is suspended
        tax
        lda app_slots,x
        cmp #2              ; 2 = suspended
        bne @invalid

        ; Get REU bank
        lda app_banks,x
        sta temp_bank

        ; Restore context from REU
        jsr _context_restore

        ; Mark app as active
        lda #1
        sta app_slots,x

        ; Set as current
        stx current_app

        ; Restore CPU state
        ldx saved_sp
        txs
        lda saved_status
        pha
        lda saved_a
        ldx saved_x
        ldy saved_y
        plp
        rts

@invalid:
        lda #$FF
        rts

temp_bank:
        .byte 0
.endproc

;-----------------------------------------------------------------------------
; _sys_exit_impl - Terminate current app
; Input: A = exit code
;-----------------------------------------------------------------------------
.proc _sys_exit_impl
        ; Get current app
        ldx current_app
        bmi @done           ; No app running

        ; Free the slot
        lda #0
        sta app_slots,x

        ; Decrement app count
        dec app_count

        ; Clear current app
        lda #$FF
        sta current_app

        ; Show switcher
        jsr show_switcher

@done:
        rts
.endproc

;-----------------------------------------------------------------------------
; _sys_clip_copy_impl - Copy data to clipboard
; Input: A/X = data pointer, Y = type, stack = size
; Output: A = 1 on success, 0 on failure
;-----------------------------------------------------------------------------
.proc _sys_clip_copy_impl
        ; Store params
        sta src_ptr
        stx src_ptr+1
        sty clip_type

        ; Get size from stack (CC65 ABI)
        ldy #0
        lda (sp),y
        sta clip_size
        iny
        lda (sp),y
        sta clip_size+1

        ; Check size limit (max ~60KB)
        lda clip_size+1
        cmp #$F0
        bcs @too_big

        ; Stash to REU bank 1 (clipboard bank)
        ; Setup REU transfer
        lda src_ptr
        sta $DF02           ; C64 addr low
        lda src_ptr+1
        sta $DF03           ; C64 addr high
        lda #1              ; Bank 1
        sta $DF06
        lda #$10            ; Offset $0010 (after header)
        sta $DF04
        lda #$00
        sta $DF05
        lda clip_size
        sta $DF07           ; Length low
        lda clip_size+1
        sta $DF08           ; Length high
        lda #$90            ; STASH command
        sta $DF01

        ; Save clipboard header to REU
        ; (type at offset 0, size at offset 1-2)
        lda clip_type
        sta temp_header
        lda clip_size
        sta temp_header+1
        lda clip_size+1
        sta temp_header+2

        lda #<temp_header
        sta $DF02
        lda #>temp_header
        sta $DF03
        lda #$00
        sta $DF04
        sta $DF05
        lda #$03            ; 3 bytes
        sta $DF07
        lda #$00
        sta $DF08
        lda #$90
        sta $DF01

        lda #1
        rts

@too_big:
        lda #0
        rts

src_ptr:
        .word 0
temp_header:
        .res 3
.endproc

;-----------------------------------------------------------------------------
; _sys_clip_paste_impl - Paste from clipboard
; Input: A/X = buffer pointer, stack = max size
; Output: A/X = bytes copied (16-bit)
;-----------------------------------------------------------------------------
.proc _sys_clip_paste_impl
        ; Store destination
        sta dst_ptr
        stx dst_ptr+1

        ; Get max size from stack
        ldy #0
        lda (sp),y
        sta max_size
        iny
        lda (sp),y
        sta max_size+1

        ; First, fetch clipboard header from REU
        lda #<temp_header
        sta $DF02
        lda #>temp_header
        sta $DF03
        lda #1              ; Bank 1
        sta $DF06
        lda #$00
        sta $DF04
        sta $DF05
        lda #$03
        sta $DF07
        lda #$00
        sta $DF08
        lda #$91            ; FETCH command
        sta $DF01

        ; Check if clipboard empty
        lda temp_header     ; type
        beq @empty

        ; Get actual size (min of clip_size and max_size)
        lda temp_header+1
        sta copy_size
        lda temp_header+2
        sta copy_size+1

        ; Compare with max_size
        lda copy_size+1
        cmp max_size+1
        bcc @size_ok
        bne @use_max
        lda copy_size
        cmp max_size
        bcc @size_ok
@use_max:
        lda max_size
        sta copy_size
        lda max_size+1
        sta copy_size+1

@size_ok:
        ; Fetch clipboard data
        lda dst_ptr
        sta $DF02
        lda dst_ptr+1
        sta $DF03
        lda #$10            ; Offset past header
        sta $DF04
        lda #$00
        sta $DF05
        lda copy_size
        sta $DF07
        lda copy_size+1
        sta $DF08
        lda #$91
        sta $DF01

        ; Return bytes copied
        lda copy_size
        ldx copy_size+1
        rts

@empty:
        lda #0
        tax
        rts

dst_ptr:    .word 0
max_size:   .word 0
copy_size:  .word 0
temp_header: .res 3
.endproc

;-----------------------------------------------------------------------------
; _sys_deeplink_impl - Launch app with parameters
; Input: A/X = pointer to DeepLink struct
;-----------------------------------------------------------------------------
.proc _sys_deeplink_impl
        ; Store deeplink pointer
        sta dl_ptr
        stx dl_ptr+1

        ; For now, just acknowledge
        ; Full implementation will:
        ; 1. Suspend current app
        ; 2. Find/load target app
        ; 3. Pass parameters
        ; 4. Resume target

        lda #1
        rts

dl_ptr: .word 0
.endproc

;-----------------------------------------------------------------------------
; _sys_query_impl - Query system state
; Input: A = query type
;   0 = get app count
;   1 = get current app ID
;   2 = get free slots
;   3 = get REU status
; Output: A/X = result
;-----------------------------------------------------------------------------
.proc _sys_query_impl
        cmp #0
        beq @get_count
        cmp #1
        beq @get_current
        cmp #2
        beq @get_free
        cmp #3
        beq @get_reu

        ; Unknown query
        lda #0
        tax
        rts

@get_count:
        lda app_count
        ldx #0
        rts

@get_current:
        lda current_app
        ldx #0
        rts

@get_free:
        ; Count free slots
        lda #0
        ldx #0
@count_loop:
        ldy app_slots,x
        bne @not_free
        clc
        adc #1
@not_free:
        inx
        cpx #MAX_APPS
        bne @count_loop
        ldx #0
        rts

@get_reu:
        ; Return 1 if REU present
        ; (Already verified at init)
        lda #1
        ldx #0
        rts
.endproc

;-----------------------------------------------------------------------------
; show_switcher - Display app switcher UI
; (Stub - will call C implementation)
;-----------------------------------------------------------------------------
.proc show_switcher
        ; For now, just return
        ; Will call switcher.c implementation
        rts
.endproc

;-----------------------------------------------------------------------------
; Imports
;-----------------------------------------------------------------------------
.import sp      ; CC65 software stack pointer
