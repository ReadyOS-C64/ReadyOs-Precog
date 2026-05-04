#include "sysinfo_uci.h"

#define UCI_ID_VALUE   0xC9
#define UCI_STAT_DATA  0x80
#define UCI_STAT_STAT  0x40
#define UCI_STATE_MASK 0x30
#define UCI_STATE_IDLE 0x00
#define UCI_STATE_BUSY 0x10
#define UCI_STATE_LAST 0x20
#define UCI_STATE_MORE 0x30
#define UCI_STAT_ERROR 0x08

#define UCI_WAIT_SHORT  1200u
#define UCI_WAIT_LONG   6000u

static unsigned char uci_wait_idle(void) {
    unsigned int tries;
    unsigned char st;

    for (tries = 0u; tries < UCI_WAIT_LONG; ++tries) {
        st = sysinfo_uci_asm_status();
        if ((st & UCI_STAT_ERROR) != 0u) {
            sysinfo_uci_asm_clear_error();
        }
        if ((st & UCI_STATE_MASK) == UCI_STATE_IDLE && (st & 0x01u) == 0u) {
            return 1u;
        }
    }
    return 0u;
}

static unsigned char uci_wait_data_state(void) {
    unsigned int tries;
    unsigned char st;

    for (tries = 0u; tries < UCI_WAIT_LONG; ++tries) {
        st = sysinfo_uci_asm_status();
        if ((st & UCI_STAT_ERROR) != 0u) {
            return 0u;
        }
        if ((st & UCI_STATE_MASK) == UCI_STATE_LAST ||
            (st & UCI_STATE_MASK) == UCI_STATE_MORE) {
            return 1u;
        }
        if ((st & UCI_STATE_MASK) == UCI_STATE_IDLE) {
            return 1u;
        }
    }
    return 0u;
}

unsigned char sysinfo_uci_detect(void) {
    return (unsigned char)(sysinfo_uci_asm_id() == UCI_ID_VALUE);
}

unsigned char sysinfo_uci_command(const unsigned char *cmd,
                                  unsigned char cmd_len,
                                  unsigned char *data,
                                  unsigned char data_cap,
                                  unsigned char *data_len,
                                  unsigned char *stat,
                                  unsigned char stat_cap,
                                  unsigned char *stat_len) {
    unsigned char i;
    unsigned char st;
    unsigned char state;
    unsigned char data_pos;
    unsigned char stat_pos;
    unsigned int drain_guard;

    data_pos = 0u;
    stat_pos = 0u;
    if (data_len != 0) {
        *data_len = 0u;
    }
    if (stat_len != 0) {
        *stat_len = 0u;
    }
    if (!sysinfo_uci_detect() || cmd == 0 || cmd_len == 0u) {
        return 0u;
    }

    if (!uci_wait_idle()) {
        sysinfo_uci_asm_abort();
        if (!uci_wait_idle()) {
            return 0u;
        }
    }

    for (i = 0u; i < cmd_len; ++i) {
        sysinfo_uci_asm_write_cmd(cmd[i]);
    }
    sysinfo_uci_asm_push_cmd();

    if (!uci_wait_data_state()) {
        sysinfo_uci_asm_abort();
        return 0u;
    }

    for (;;) {
        st = sysinfo_uci_asm_status();
        state = (unsigned char)(st & UCI_STATE_MASK);
        if (state == UCI_STATE_IDLE) {
            break;
        }
        if (state != UCI_STATE_LAST && state != UCI_STATE_MORE) {
            if (!uci_wait_data_state()) {
                sysinfo_uci_asm_abort();
                return 0u;
            }
            continue;
        }

        drain_guard = 0u;
        while (drain_guard < UCI_WAIT_SHORT) {
            st = sysinfo_uci_asm_status();
            if ((st & UCI_STAT_DATA) != 0u) {
                if (data_pos < data_cap && data != 0) {
                    data[data_pos] = sysinfo_uci_asm_read_data();
                    ++data_pos;
                } else {
                    (void)sysinfo_uci_asm_read_data();
                }
                drain_guard = 0u;
                continue;
            }
            if ((st & UCI_STAT_STAT) != 0u) {
                if (stat_pos < stat_cap && stat != 0) {
                    stat[stat_pos] = sysinfo_uci_asm_read_stat();
                    ++stat_pos;
                } else {
                    (void)sysinfo_uci_asm_read_stat();
                }
                drain_guard = 0u;
                continue;
            }
            ++drain_guard;
        }

        sysinfo_uci_asm_accept_data();
        if (state == UCI_STATE_LAST) {
            (void)uci_wait_idle();
            break;
        }
        if (!uci_wait_data_state()) {
            sysinfo_uci_asm_abort();
            return 0u;
        }
    }

    if (data_len != 0) {
        *data_len = data_pos;
    }
    if (stat_len != 0) {
        *stat_len = stat_pos;
    }
    return 1u;
}
