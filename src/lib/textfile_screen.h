#ifndef READYOS_TEXTFILE_SCREEN_H
#define READYOS_TEXTFILE_SCREEN_H

#include "tui.h"

static unsigned char textfile_byte_to_screen(unsigned char ch) {
    if (ch == 124u || ch == CH_VLINE) {
        return TUI_VLINE;
    }
    return tui_ascii_to_screen(ch);
}

#endif
