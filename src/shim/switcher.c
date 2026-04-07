/*
 * switcher.c - App Switcher UI for Ready OS Shim
 * Minimal text-based app switcher displayed when suspending
 *
 * For Commodore 64, compiled with CC65
 */

#include <c64.h>
#include <cbm.h>
#include <conio.h>
#include <string.h>

/* Screen memory */
#define SCREEN ((unsigned char*)0x0400)
#define COLOR_RAM ((unsigned char*)0xD800)

/* Screen dimensions */
#define SCREEN_WIDTH  40
#define SCREEN_HEIGHT 25

/* Colors */
#define COLOR_BG        COLOR_BLUE
#define COLOR_BORDER    COLOR_LIGHTBLUE
#define COLOR_TEXT      COLOR_WHITE
#define COLOR_HIGHLIGHT COLOR_CYAN
#define COLOR_TITLE     COLOR_YELLOW
#define COLOR_STATUS    COLOR_GRAY3

/* Key codes */
#define KEY_UP      145
#define KEY_DOWN    17
#define KEY_RETURN  13
#define KEY_F1      133
#define KEY_F3      134
#define KEY_F5      135

/* Status values */
#define STATUS_FREE      0
#define STATUS_RUNNING   1
#define STATUS_SUSPENDED 2

/* Max apps */
#define MAX_APPS 24

/*---------------------------------------------------------------------------
 * External functions from registry.c
 *---------------------------------------------------------------------------*/
extern unsigned char registry_get_count(void);
extern unsigned char registry_get_current(void);
extern unsigned char registry_get_slot(unsigned char index, char *name, unsigned char *status);
extern unsigned char registry_get_status(unsigned char app_id);
extern const char *registry_get_name(unsigned char app_id);

/*---------------------------------------------------------------------------
 * Static variables
 *---------------------------------------------------------------------------*/
static unsigned char selected;
static unsigned char app_list[MAX_APPS];  /* Maps menu index to app slot */
static unsigned char app_count;
static unsigned char i, j;
static unsigned int screen_offset;

/*---------------------------------------------------------------------------
 * Helper functions
 *---------------------------------------------------------------------------*/

/* Convert ASCII to screen code */
static unsigned char ascii_to_screen(unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 1;
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 1;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 48;
    }
    if (c == ' ') return 32;
    if (c == ':') return 58;
    if (c == '-') return 45;
    if (c == '[') return 27;
    if (c == ']') return 29;
    if (c == '>') return 62;
    return 32;
}

/* Draw string at position */
static void draw_string(unsigned char x, unsigned char y, const char *str, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;
    for (i = 0; str[i] != 0 && (x + i) < SCREEN_WIDTH; ++i) {
        SCREEN[screen_offset + i] = ascii_to_screen(str[i]);
        COLOR_RAM[screen_offset + i] = color;
    }
}

/* Draw horizontal line using PETSCII */
static void draw_hline(unsigned char x, unsigned char y, unsigned char len, unsigned char color) {
    screen_offset = (unsigned int)y * 40 + x;
    for (i = 0; i < len; ++i) {
        SCREEN[screen_offset + i] = 0x40;  /* Horizontal line */
        COLOR_RAM[screen_offset + i] = color;
    }
}

/* Draw box */
static void draw_box(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char color) {
    unsigned char x2 = x + w - 1;
    unsigned char y2 = y + h - 1;

    /* Corners */
    screen_offset = (unsigned int)y * 40 + x;
    SCREEN[screen_offset] = 0x70;  /* Top-left */
    COLOR_RAM[screen_offset] = color;

    screen_offset = (unsigned int)y * 40 + x2;
    SCREEN[screen_offset] = 0x6E;  /* Top-right */
    COLOR_RAM[screen_offset] = color;

    screen_offset = (unsigned int)y2 * 40 + x;
    SCREEN[screen_offset] = 0x6D;  /* Bottom-left */
    COLOR_RAM[screen_offset] = color;

    screen_offset = (unsigned int)y2 * 40 + x2;
    SCREEN[screen_offset] = 0x7D;  /* Bottom-right */
    COLOR_RAM[screen_offset] = color;

    /* Top and bottom lines */
    draw_hline(x + 1, y, w - 2, color);
    draw_hline(x + 1, y2, w - 2, color);

    /* Left and right lines */
    for (i = 1; i < h - 1; ++i) {
        screen_offset = (unsigned int)(y + i) * 40 + x;
        SCREEN[screen_offset] = 0x5D;  /* Vertical line */
        COLOR_RAM[screen_offset] = color;

        screen_offset = (unsigned int)(y + i) * 40 + x2;
        SCREEN[screen_offset] = 0x5D;  /* Vertical line */
        COLOR_RAM[screen_offset] = color;
    }
}

/* Clear rectangle */
static void clear_rect(unsigned char x, unsigned char y, unsigned char w, unsigned char h, unsigned char color) {
    for (j = 0; j < h; ++j) {
        screen_offset = (unsigned int)(y + j) * 40 + x;
        for (i = 0; i < w; ++i) {
            SCREEN[screen_offset + i] = 32;
            COLOR_RAM[screen_offset + i] = color;
        }
    }
}

/*---------------------------------------------------------------------------
 * Switcher UI
 *---------------------------------------------------------------------------*/

/* Build list of active apps */
static void build_app_list(void) {
    char name[8];
    unsigned char status;

    app_count = 0;

    for (i = 0; i < MAX_APPS; ++i) {
        if (registry_get_slot(i, name, &status)) {
            app_list[app_count] = i;
            ++app_count;
        }
    }
}

/* Draw the switcher screen */
static void draw_switcher(void) {
    char name[8];
    unsigned char status;
    unsigned char color;
    unsigned char y;

    /* Clear screen */
    VIC.bordercolor = COLOR_BG;
    VIC.bgcolor0 = COLOR_BG;
    clrscr();

    /* Draw main box */
    draw_box(0, 0, 40, 20, COLOR_BORDER);

    /* Title */
    draw_string(13, 0, " READY OS v1.0 ", COLOR_TITLE);

    /* Separator line */
    draw_hline(1, 2, 38, COLOR_BORDER);

    /* "APPLICATIONS:" label */
    draw_string(2, 3, "APPLICATIONS:", COLOR_TEXT);

    /* Draw app list */
    y = 5;
    for (i = 0; i < app_count && i < 12; ++i) {
        unsigned char slot = app_list[i];

        registry_get_slot(slot, name, &status);

        /* Determine color */
        if (i == selected) {
            color = COLOR_HIGHLIGHT;
            draw_string(2, y, ">", color);
        } else {
            color = COLOR_TEXT;
            draw_string(2, y, " ", color);
        }

        /* Draw app name */
        draw_string(4, y, name, color);

        /* Draw status */
        if (status == STATUS_RUNNING) {
            draw_string(20, y, "[RUNNING]", COLOR_STATUS);
        } else if (status == STATUS_SUSPENDED) {
            draw_string(20, y, "[SUSPENDED]", COLOR_STATUS);
        }

        ++y;
    }

    /* If no apps */
    if (app_count == 0) {
        draw_string(4, 5, "(NO APPLICATIONS)", COLOR_STATUS);
    }

    /* Separator */
    draw_hline(1, 17, 38, COLOR_BORDER);

    /* Status line */
    draw_string(2, 18, "APPS:", COLOR_TEXT);
    /* Draw app count */
    SCREEN[18 * 40 + 7] = 48 + app_count;
    COLOR_RAM[18 * 40 + 7] = COLOR_TEXT;

    draw_string(12, 18, "REU: 16MB", COLOR_TEXT);

    /* Help line */
    draw_string(2, 21, "UP/DOWN:Select  RETURN:Resume", COLOR_STATUS);
    draw_string(2, 22, "F1:Launch  F5:Terminate", COLOR_STATUS);
}

/* Handle switcher input */
static unsigned char handle_input(void) {
    unsigned char key;

    key = cgetc();

    switch (key) {
        case KEY_UP:
            if (selected > 0) {
                --selected;
            }
            return 0;  /* Redraw */

        case KEY_DOWN:
            if (selected < app_count - 1) {
                ++selected;
            }
            return 0;  /* Redraw */

        case KEY_RETURN:
        case KEY_F3:
            /* Resume selected app */
            if (app_count > 0) {
                return app_list[selected] + 1;  /* Return 1-based app ID */
            }
            return 0;

        case KEY_F5:
            /* Terminate selected app */
            if (app_count > 0) {
                return 0x80 | app_list[selected];  /* Return with terminate flag */
            }
            return 0;
    }

    return 0;
}

/*---------------------------------------------------------------------------
 * Public Interface
 *---------------------------------------------------------------------------*/

/*
 * switcher_show - Display app switcher and wait for selection
 *
 * Returns: App ID to resume (1-16), or 0 to stay in switcher,
 *          or 0x80 | app_id to terminate an app
 */
unsigned char switcher_show(void) {
    unsigned char result;

    /* Initialize */
    selected = 0;
    (void)kbrepeat(KBREPEAT_NONE);

    /* Build list of apps */
    build_app_list();

    /* Find current app in list */
    {
        unsigned char current = registry_get_current();
        for (i = 0; i < app_count; ++i) {
            if (app_list[i] == current) {
                selected = i;
                break;
            }
        }
    }

    /* Main loop */
    while (1) {
        draw_switcher();
        result = handle_input();

        if (result != 0) {
            return result;
        }
    }
}

/*
 * switcher_quick - Quick switcher without loop
 * Just draws and returns immediately
 */
void switcher_quick(void) {
    selected = 0;
    build_app_list();
    draw_switcher();
}
