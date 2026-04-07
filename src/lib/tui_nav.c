/*
 * tui_nav.c - App switching and launcher return via shim
 */

#include "tui.h"

#define SHIM_TARGET_BANK ((unsigned char*)0xC820)
#define SHIM_REU_BITMAP_LO ((unsigned char*)0xC836)
#define SHIM_REU_BITMAP_HI ((unsigned char*)0xC837)
#define SHIM_REU_BITMAP_XHI ((unsigned char*)0xC838)

#define APP_BANK_EDITOR   1
#define APP_BANK_MAX      23

static unsigned char tui_bank_loaded(unsigned char bank) {
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

void tui_return_to_launcher(void) {
    __asm__("jmp $C80C");
}

void tui_switch_to_app(unsigned char bank) {
    *SHIM_TARGET_BANK = bank;
    __asm__("jmp $C80F");
}

unsigned char tui_get_next_app(unsigned char current_bank) {
    unsigned char next = current_bank;
    unsigned char tries = 0;

    if (current_bank < 1 || current_bank > APP_BANK_MAX) {
        current_bank = APP_BANK_EDITOR;
        next = current_bank;
    }

    while (tries < APP_BANK_MAX) {
        ++next;
        if (next > APP_BANK_MAX) {
            next = APP_BANK_EDITOR;
        }
        if (next != current_bank && tui_bank_loaded(next)) {
            return next;
        }
        ++tries;
    }

    return 0;
}

unsigned char tui_get_prev_app(unsigned char current_bank) {
    unsigned char prev = current_bank;
    unsigned char tries = 0;

    if (current_bank < 1 || current_bank > APP_BANK_MAX) {
        current_bank = APP_BANK_EDITOR;
        prev = current_bank;
    }

    while (tries < APP_BANK_MAX) {
        if (prev <= APP_BANK_EDITOR) {
            prev = APP_BANK_MAX;
        } else {
            --prev;
        }
        if (prev != current_bank && tui_bank_loaded(prev)) {
            return prev;
        }
        ++tries;
    }

    return 0;
}
