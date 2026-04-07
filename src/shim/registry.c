/*
 * registry.c - App Registry Management for Ready OS Shim
 * Manages app slots, registration, and status tracking
 *
 * For Commodore 64, compiled with CC65
 */

#include <string.h>

/* Maximum number of apps */
#define MAX_APPS 24

/* App status values */
#define STATUS_FREE      0
#define STATUS_RUNNING   1
#define STATUS_SUSPENDED 2

/* REU bank allocation */
#define REU_BANK_SYSTEM    0
#define REU_BANK_CLIPBOARD 1
#define REU_BANK_APP_BASE  2

/*---------------------------------------------------------------------------
 * Registry Data Structures
 *---------------------------------------------------------------------------*/

/* App slot information */
typedef struct {
    unsigned char status;    /* STATUS_* */
    unsigned char reu_bank;  /* Assigned REU bank */
    char name[8];            /* App name */
    void *entry;             /* Entry point */
    unsigned char flags;     /* App flags */
} AppSlot;

/* Global registry state */
static AppSlot slots[MAX_APPS];
static unsigned char app_count;
static unsigned char current_app;

/*---------------------------------------------------------------------------
 * Registry Functions
 *---------------------------------------------------------------------------*/

/*
 * registry_init - Initialize the app registry
 * Called once at shim startup
 */
void registry_init(void) {
    unsigned char i;

    /* Clear all slots */
    for (i = 0; i < MAX_APPS; ++i) {
        slots[i].status = STATUS_FREE;
        slots[i].reu_bank = REU_BANK_APP_BASE + i;
        slots[i].name[0] = 0;
        slots[i].entry = 0;
        slots[i].flags = 0;
    }

    app_count = 0;
    current_app = 0xFF;  /* No app running */
}

/*
 * registry_find_free - Find a free app slot
 * Returns: slot index (0-23) or 0xFF if none free
 */
unsigned char registry_find_free(void) {
    unsigned char i;

    for (i = 0; i < MAX_APPS; ++i) {
        if (slots[i].status == STATUS_FREE) {
            return i;
        }
    }
    return 0xFF;
}

/*
 * registry_register - Register a new app
 *
 * name: App name (max 7 chars + null)
 * entry: Entry point function
 * flags: App flags
 *
 * Returns: app ID (0-23) or 0xFF on failure
 */
unsigned char registry_register(const char *name, void *entry, unsigned char flags) {
    unsigned char slot;

    /* Find free slot */
    slot = registry_find_free();
    if (slot == 0xFF) {
        return 0xFF;  /* No free slots */
    }

    /* Initialize slot */
    slots[slot].status = STATUS_RUNNING;
    strncpy(slots[slot].name, name, 7);
    slots[slot].name[7] = 0;
    slots[slot].entry = entry;
    slots[slot].flags = flags;

    /* Increment app count */
    ++app_count;

    /* Set as current app */
    current_app = slot;

    return slot;
}

/*
 * registry_unregister - Remove an app from registry
 *
 * app_id: App slot to free
 */
void registry_unregister(unsigned char app_id) {
    if (app_id >= MAX_APPS) {
        return;
    }

    if (slots[app_id].status != STATUS_FREE) {
        slots[app_id].status = STATUS_FREE;
        slots[app_id].name[0] = 0;
        slots[app_id].entry = 0;

        if (app_count > 0) {
            --app_count;
        }

        if (current_app == app_id) {
            current_app = 0xFF;
        }
    }
}

/*
 * registry_set_status - Set app status
 *
 * app_id: App slot
 * status: New status (STATUS_*)
 */
void registry_set_status(unsigned char app_id, unsigned char status) {
    if (app_id < MAX_APPS) {
        slots[app_id].status = status;
    }
}

/*
 * registry_get_status - Get app status
 *
 * app_id: App slot
 *
 * Returns: STATUS_* value
 */
unsigned char registry_get_status(unsigned char app_id) {
    if (app_id < MAX_APPS) {
        return slots[app_id].status;
    }
    return STATUS_FREE;
}

/*
 * registry_get_bank - Get REU bank for app
 *
 * app_id: App slot
 *
 * Returns: REU bank number
 */
unsigned char registry_get_bank(unsigned char app_id) {
    if (app_id < MAX_APPS) {
        return slots[app_id].reu_bank;
    }
    return 0;
}

/*
 * registry_get_name - Get app name
 *
 * app_id: App slot
 *
 * Returns: Pointer to name string
 */
const char *registry_get_name(unsigned char app_id) {
    if (app_id < MAX_APPS && slots[app_id].status != STATUS_FREE) {
        return slots[app_id].name;
    }
    return "";
}

/*
 * registry_get_entry - Get app entry point
 *
 * app_id: App slot
 *
 * Returns: Entry point function pointer
 */
void *registry_get_entry(unsigned char app_id) {
    if (app_id < MAX_APPS) {
        return slots[app_id].entry;
    }
    return 0;
}

/*
 * registry_find_by_name - Find app by name
 *
 * name: App name to search for
 *
 * Returns: app ID (0-23) or 0xFF if not found
 */
unsigned char registry_find_by_name(const char *name) {
    unsigned char i;

    for (i = 0; i < MAX_APPS; ++i) {
        if (slots[i].status != STATUS_FREE) {
            if (strcmp(slots[i].name, name) == 0) {
                return i;
            }
        }
    }
    return 0xFF;
}

/*
 * registry_get_count - Get number of registered apps
 *
 * Returns: Number of apps (0-16)
 */
unsigned char registry_get_count(void) {
    return app_count;
}

/*
 * registry_get_current - Get current app ID
 *
 * Returns: Current app ID or 0xFF if none
 */
unsigned char registry_get_current(void) {
    return current_app;
}

/*
 * registry_set_current - Set current app
 *
 * app_id: App to make current
 */
void registry_set_current(unsigned char app_id) {
    if (app_id < MAX_APPS && slots[app_id].status != STATUS_FREE) {
        current_app = app_id;
    } else {
        current_app = 0xFF;
    }
}

/*
 * registry_get_slot - Get slot info for enumeration
 *
 * index: Slot index (0-23)
 * name: Output buffer for name (8 bytes)
 * status: Output for status
 *
 * Returns: 1 if slot is valid, 0 if empty
 */
unsigned char registry_get_slot(unsigned char index, char *name, unsigned char *status) {
    if (index >= MAX_APPS) {
        return 0;
    }

    *status = slots[index].status;

    if (slots[index].status != STATUS_FREE) {
        strcpy(name, slots[index].name);
        return 1;
    }

    name[0] = 0;
    return 0;
}
