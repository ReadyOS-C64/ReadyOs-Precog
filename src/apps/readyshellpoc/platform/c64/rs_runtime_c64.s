.setcpu "6502"

.export _rs_set_c_stack_top
.export _rs_memcfg_push_ram_under_basic
.export _rs_memcfg_pop
.export _rs_parse_source_overlay_call
.export _rs_heap_bss_run
.export _rs_heap_bss_size
.export _rs_heap_overlay_loadaddr
.importzp sp
.import _rs_parse_source
.import __BSS_RUN__
.import __BSS_SIZE__
.import __OVERLAY_LOADADDR__
.ifdef RS_PARSE_TRACE_DEBUG
.import _rs_overlay_debug_mark
.import _rs_parse_entry_cookie
.macro rs_dbgmark_lit c
    lda #c
    jsr _rs_overlay_debug_mark
.endmacro
.macro rs_dbgmark_ax c
    pha
    txa
    pha
    lda #c
    jsr _rs_overlay_debug_mark
    pla
    tax
    pla
.endmacro
.endif

.segment "BSS"
memcfg_depth:
    .res 1
memcfg_saved:
    .res 4
memcfg_irq_saved:
    .res 4

.segment "CODE"

_rs_heap_bss_run = __BSS_RUN__
_rs_heap_bss_size = __BSS_SIZE__
_rs_heap_overlay_loadaddr = __OVERLAY_LOADADDR__

_rs_set_c_stack_top:
    lda #<$0FF0
    ldx #>$0FF0
    sta sp
    stx sp+1
    rts

_rs_memcfg_push_ram_under_basic:
    lda memcfg_depth
    cmp #5
    bcc push_depth_ok
    lda #0
    sta memcfg_depth
push_depth_ok:
    lda memcfg_depth
    cmp #4
    bcc push_use_depth
    lda #3
push_use_depth:
    tax
    php
    pla
    and #$04
    sta memcfg_irq_saved,x
    sei
    lda $01
    sta memcfg_saved,x
    and #$FE        ; Clear LORAM only (keep KERNAL visible).
    sta $01
    lda memcfg_depth
    cmp #4
    bcs push_done
    inc memcfg_depth
push_done:
    rts

_rs_memcfg_pop:
    lda memcfg_depth
    beq pop_done
    cmp #5
    bcc pop_depth_ok
    lda #0
    sta memcfg_depth
    lda #$37
    sta $01
    rts
pop_depth_ok:
    dec memcfg_depth
    ldx memcfg_depth
    lda memcfg_saved,x
    sta $01
    lda memcfg_irq_saved,x
    beq pop_restore_irq_enabled
    sei
    rts
pop_restore_irq_enabled:
    cli
pop_done:
    rts

; Call rs_parse_source() while RAM under BASIC is mapped and restore mapping/IRQ state.
; Signature matches C:
;   int rs_parse_source_overlay_call(const char* source, RSProgram* out_program, RSError* err);
_rs_parse_source_overlay_call:
.ifdef RS_PARSE_TRACE_DEBUG
    rs_dbgmark_ax 'u'
.endif
    ; cc65 fastcall: rightmost arg (err pointer) arrives in A/X.
    ; Preserve it across mapping setup before calling parse.
    pha
    txa
    pha
.ifdef RS_PARSE_TRACE_DEBUG
    rs_dbgmark_lit 'v'
.endif
    jsr _rs_memcfg_push_ram_under_basic
.ifdef RS_PARSE_TRACE_DEBUG
    rs_dbgmark_lit 'w'
.endif
    pla
    tax
    pla
.ifdef RS_PARSE_TRACE_DEBUG
    lda _rs_parse_entry_cookie
    cmp #$A5
    bne parse_sig_fail
    lda _rs_parse_entry_cookie+1
    cmp #$5A
    bne parse_sig_fail
    lda _rs_parse_entry_cookie+2
    cmp #$C3
    bne parse_sig_fail
    lda _rs_parse_entry_cookie+3
    cmp #$3C
    bne parse_sig_fail
    rs_dbgmark_ax 'x'
.endif
    jsr _rs_parse_source
.ifdef RS_PARSE_TRACE_DEBUG
    rs_dbgmark_ax 'y'
.endif
    ; Preserve parse return A/X across mapping restore.
    pha
    txa
    pha
    jsr _rs_memcfg_pop
.ifdef RS_PARSE_TRACE_DEBUG
    rs_dbgmark_lit 'z'
.endif
    pla
    tax
    pla
    rts
.ifdef RS_PARSE_TRACE_DEBUG
parse_sig_fail:
    rs_dbgmark_lit 'S'
    jsr _rs_memcfg_pop
    ldx #$FF
    txa
    rts
.endif
