/*
 * storage_device.c - Shared D8/D9 storage-drive selector
 */

#include "storage_device.h"

static unsigned char storage_device_normalize(unsigned char drive) {
    if (drive == STORAGE_DEVICE_DRIVE_9) {
        return STORAGE_DEVICE_DRIVE_9;
    }
    return STORAGE_DEVICE_DRIVE_8;
}

unsigned char storage_device_get_default(void) {
    unsigned char drive;

    drive = storage_device_normalize(*SHIM_STORAGE_DRIVE);
    if (*SHIM_STORAGE_DRIVE != drive) {
        *SHIM_STORAGE_DRIVE = drive;
    }
    return drive;
}

void storage_device_set_default(unsigned char drive) {
    *SHIM_STORAGE_DRIVE = storage_device_normalize(drive);
}

unsigned char storage_device_toggle_8_9(unsigned char drive) {
    if (storage_device_normalize(drive) == STORAGE_DEVICE_DRIVE_8) {
        return STORAGE_DEVICE_DRIVE_9;
    }
    return STORAGE_DEVICE_DRIVE_8;
}
