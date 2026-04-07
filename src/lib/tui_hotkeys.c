/*
 * tui_hotkeys.c - Shared direct app hotkeys backed by shim-safe RAM
 */

#include "tui.h"

#define SHFLAG (*(unsigned char*)0x028D)
#define SHIM_REU_BITMAP_LO ((unsigned char*)0xC836)
#define SHIM_REU_BITMAP_HI ((unsigned char*)0xC837)
#define SHIM_REU_BITMAP_XHI ((unsigned char*)0xC838)
#define APP_BANK_EDITOR 1
#define APP_BANK_MAX 23

/* PETSCII/control bytes produced by Ctrl/C= digit chords on C64 keyboards. */
#define HOTKEY_CTRL_1 144
#define HOTKEY_CTRL_2   5
#define HOTKEY_CTRL_3  28
#define HOTKEY_CTRL_4 159
#define HOTKEY_CTRL_5 156
#define HOTKEY_CTRL_6  30
#define HOTKEY_CTRL_7  31
#define HOTKEY_CTRL_8 158
#define HOTKEY_CTRL_9  18

#define HOTKEY_CBM_1 129
#define HOTKEY_CBM_2 149
#define HOTKEY_CBM_3 150
#define HOTKEY_CBM_4 151
#define HOTKEY_CBM_5 152
#define HOTKEY_CBM_6 153
#define HOTKEY_CBM_7 154
#define HOTKEY_CBM_8 155

static unsigned char hotkey_slot_from_shifted(unsigned char key) {
    switch (key) {
        case '!': return 1;
        case '"': return 2;
        case '#': return 3;
        case '$': return 4;
        case '%': return 5;
        case '&': return 6;
        case '\'': return 7;
        case '(': return 8;
        case ')': return 9;
        default: return 0;
    }
}

static unsigned char hotkey_slot_from_unshifted(unsigned char key,
                                                unsigned char modifiers) {
    if (key >= '1' && key <= '9') {
        return (unsigned char)(key - '0');
    }

    if ((modifiers & TUI_MOD_CBM) != 0) {
        switch (key) {
            case HOTKEY_CBM_1: return 1;
            case HOTKEY_CBM_2: return 2;
            case HOTKEY_CBM_3: return 3;
            case HOTKEY_CBM_4: return 4;
            case HOTKEY_CBM_5: return 5;
            case HOTKEY_CBM_6: return 6;
            case HOTKEY_CBM_7: return 7;
            case HOTKEY_CBM_8: return 8;
            default: break;
        }
    }

    if ((modifiers & TUI_MOD_CTRL) != 0) {
        switch (key) {
            case HOTKEY_CTRL_1: return 1;
            case HOTKEY_CTRL_2: return 2;
            case HOTKEY_CTRL_3: return 3;
            case HOTKEY_CTRL_4: return 4;
            case HOTKEY_CTRL_5: return 5;
            case HOTKEY_CTRL_6: return 6;
            case HOTKEY_CTRL_7: return 7;
            case HOTKEY_CTRL_8: return 8;
            case HOTKEY_CTRL_9: return 9;
            default: break;
        }
    }

    return 0;
}

static unsigned char hotkey_bank_loaded(unsigned char bank) {
    if (bank < 8) {
        return (unsigned char)((*SHIM_REU_BITMAP_LO & (unsigned char)(1U << bank)) != 0);
    }
    if (bank < 16) {
        return (unsigned char)((*SHIM_REU_BITMAP_HI & (unsigned char)(1U << (bank - 8))) != 0);
    }
    if (bank < 24) {
        return (unsigned char)((*SHIM_REU_BITMAP_XHI & (unsigned char)(1U << (bank - 16))) != 0);
    }
    return 0;
}

unsigned char tui_get_modifiers(void) {
    return SHFLAG;
}

unsigned char tui_is_back_pressed(void) {
    return (unsigned char)((SHFLAG & TUI_MOD_CTRL) != 0);
}

unsigned char tui_handle_global_hotkey(unsigned char key,
                                       unsigned char current_bank,
                                       unsigned char allow_bind) {
    unsigned char modifiers;
    unsigned char slot;
    unsigned char target;

    if (key == 2) {
        return TUI_HOTKEY_LAUNCHER;
    }
    if (key == TUI_KEY_NEXT_APP) {
        return tui_get_next_app(current_bank);
    }
    if (key == TUI_KEY_PREV_APP) {
        return tui_get_prev_app(current_bank);
    }

    modifiers = SHFLAG;
    if ((modifiers & (unsigned char)(TUI_MOD_CTRL | TUI_MOD_CBM)) == 0) {
        return TUI_HOTKEY_NONE;
    }

    slot = hotkey_slot_from_unshifted(key, modifiers);
    if (slot != 0) {
        if (allow_bind && (modifiers & TUI_MOD_SHIFT) != 0) {
            if (current_bank >= APP_BANK_EDITOR && current_bank <= APP_BANK_MAX) {
                TUI_HOTKEY_BINDINGS[(unsigned char)(slot - 1)] = current_bank;
            }
            return TUI_HOTKEY_BIND_ONLY;
        }
    } else {
        slot = hotkey_slot_from_shifted(key);
        if (slot == 0 || !allow_bind) {
            return TUI_HOTKEY_NONE;
        }
        if (current_bank >= APP_BANK_EDITOR && current_bank <= APP_BANK_MAX) {
            TUI_HOTKEY_BINDINGS[(unsigned char)(slot - 1)] = current_bank;
        }
        return TUI_HOTKEY_BIND_ONLY;
    }

    target = TUI_HOTKEY_BINDINGS[(unsigned char)(slot - 1)];
    if (target < APP_BANK_EDITOR || target > APP_BANK_MAX) {
        return TUI_HOTKEY_NONE;
    }
    if (target == current_bank) {
        return TUI_HOTKEY_BIND_ONLY;
    }
    if (current_bank != 0 && !hotkey_bank_loaded(target)) {
        return TUI_HOTKEY_BIND_ONLY;
    }

    return target;
}
