/*
 * storage_device.h - Shared D8/D9 storage-drive selector
 *
 * The selected drive lives in the shim data area at $C839. That byte is now
 * part of the shim-global contract for app file dialogs, rather than app-local
 * scratch state. Apps using this helper therefore share one persistent default
 * drive across app switches.
 *
 * Current policy is intentionally narrow: only drives 8 and 9 are valid here.
 * Invalid values are sanitized back to drive 8.
 */

#ifndef STORAGE_DEVICE_H
#define STORAGE_DEVICE_H

#define SHIM_STORAGE_DRIVE      ((unsigned char*)0xC839)
#define STORAGE_DEVICE_DRIVE_8  8
#define STORAGE_DEVICE_DRIVE_9  9

unsigned char storage_device_get_default(void);
void storage_device_set_default(unsigned char drive);
unsigned char storage_device_toggle_8_9(unsigned char drive);
unsigned char storage_device_is_valid_8_11(unsigned char drive);
unsigned char storage_device_normalize_8_11(unsigned char drive,
                                            unsigned char fallback);

#endif /* STORAGE_DEVICE_H */
