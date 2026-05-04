;
; sysinfo_uci_asm.s - tiny UCI register accessors for System Info
;

        .export _sysinfo_uci_asm_write_cmd
        .export _sysinfo_uci_asm_id
        .export _sysinfo_uci_asm_status
        .export _sysinfo_uci_asm_read_data
        .export _sysinfo_uci_asm_read_stat
        .export _sysinfo_uci_asm_push_cmd
        .export _sysinfo_uci_asm_accept_data
        .export _sysinfo_uci_asm_abort
        .export _sysinfo_uci_asm_clear_error

UCI_CONTROL = $DF1C
UCI_STATUS  = $DF1C
UCI_COMMAND = $DF1D
UCI_ID      = $DF1D
UCI_DATA    = $DF1E
UCI_STAT    = $DF1F

_sysinfo_uci_asm_write_cmd:
        sta UCI_COMMAND
        rts

_sysinfo_uci_asm_id:
        lda UCI_ID
        rts

_sysinfo_uci_asm_status:
        lda UCI_STATUS
        rts

_sysinfo_uci_asm_read_data:
        lda UCI_DATA
        rts

_sysinfo_uci_asm_read_stat:
        lda UCI_STAT
        rts

_sysinfo_uci_asm_push_cmd:
        lda #$01
        sta UCI_CONTROL
        rts

_sysinfo_uci_asm_accept_data:
        lda #$02
        sta UCI_CONTROL
        rts

_sysinfo_uci_asm_abort:
        lda #$04
        sta UCI_CONTROL
        rts

_sysinfo_uci_asm_clear_error:
        lda #$08
        sta UCI_CONTROL
        rts
