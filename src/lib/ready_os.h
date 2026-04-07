/*
 * ready_os.h - Ready OS API for Applications
 * Syscall wrappers and data structures for app development
 *
 * For Commodore 64, compiled with CC65
 */

#ifndef READY_OS_H
#define READY_OS_H

/*---------------------------------------------------------------------------
 * Constants
 *---------------------------------------------------------------------------*/

/* Syscall addresses (shim jump table at $C800) */
#define SYSCALL_INIT       0xC800
#define SYSCALL_SUSPEND    0xC803
#define SYSCALL_RESUME     0xC806
#define SYSCALL_EXIT       0xC809
#define SYSCALL_CLIP_COPY  0xC80C
#define SYSCALL_CLIP_PASTE 0xC80F
#define SYSCALL_DEEPLINK   0xC812
#define SYSCALL_QUERY      0xC815

/* App flags */
#define APP_FLAG_NONE       0x00
#define APP_FLAG_SINGLETON  0x01  /* Only one instance allowed */
#define APP_FLAG_BACKGROUND 0x02  /* Can run in background */
#define APP_FLAG_RESIDENT   0x04  /* Don't swap out completely */

/* App status values */
#define APP_STATUS_FREE      0
#define APP_STATUS_RUNNING   1
#define APP_STATUS_SUSPENDED 2

/* Clipboard types */
#define CLIP_TYPE_EMPTY  0
#define CLIP_TYPE_TEXT   1
#define CLIP_TYPE_BINARY 2
#define CLIP_TYPE_SCREEN 3  /* Screen region */

/* Deep link action codes */
#define DL_ACTION_LAUNCH    0   /* Normal launch */
#define DL_ACTION_OPEN_FILE 1   /* Open file (params = filename) */
#define DL_ACTION_VIEW_ADDR 2   /* View address (params = "$XXXX") */
#define DL_ACTION_RETURN    3   /* Return data to caller */

/* Query types for ready_query() */
#define QUERY_APP_COUNT     0   /* Number of registered apps */
#define QUERY_CURRENT_APP   1   /* Current app ID */
#define QUERY_FREE_SLOTS    2   /* Number of free app slots */
#define QUERY_REU_STATUS    3   /* REU present (1) or not (0) */
#define QUERY_CLIP_SIZE     4   /* Clipboard data size */
#define QUERY_CLIP_TYPE     5   /* Clipboard data type */

/* Maximum values */
#define MAX_APP_NAME     8
#define MAX_DEEPLINK_PARAMS 64
#define MAX_APPS         24

/*---------------------------------------------------------------------------
 * Data Structures
 *---------------------------------------------------------------------------*/

/*
 * Application Header - placed at start of app memory
 * This is how apps register with the shim
 */
typedef struct {
    char magic[6];           /* "RDYAPP" magic identifier */
    char name[MAX_APP_NAME]; /* Application name (null-terminated) */
    void (*entry)(void);     /* Entry point function */
    unsigned char flags;     /* Capability flags */
    unsigned char version;   /* App version */
} ReadyAppHeader;

/*
 * Deep Link structure - for launching apps with parameters
 */
typedef struct {
    char target_app[MAX_APP_NAME]; /* Target application name */
    unsigned char action;          /* Action code (DL_ACTION_*) */
    unsigned char param_len;       /* Length of params data */
    char params[MAX_DEEPLINK_PARAMS]; /* Action-specific data */
} DeepLink;

/*
 * App Info structure - returned by ready_app_info()
 */
typedef struct {
    unsigned char id;            /* App slot ID (0-23) */
    unsigned char status;        /* APP_STATUS_* */
    char name[MAX_APP_NAME];     /* App name */
} AppInfo;

/*---------------------------------------------------------------------------
 * Syscall Wrapper Functions
 *---------------------------------------------------------------------------*/

/*
 * ready_init - Register app with Ready OS
 * Call this at app startup with a pointer to your ReadyAppHeader
 *
 * Returns: app_id (0-23) on success, 0xFF on failure
 */
unsigned char ready_init(ReadyAppHeader *header);

/*
 * ready_suspend - Suspend current app and return to switcher
 * Saves all app state to REU, shows app switcher
 * When resumed, returns from this function
 */
void ready_suspend(void);

/*
 * ready_exit - Terminate app and return to switcher
 * Frees app slot and REU resources
 *
 * code: Exit code (0 = success, non-zero = error)
 */
void ready_exit(unsigned char code);

/*
 * ready_clip_copy - Copy data to system clipboard
 *
 * type: CLIP_TYPE_TEXT, CLIP_TYPE_BINARY, or CLIP_TYPE_SCREEN
 * data: Pointer to data to copy
 * size: Size of data in bytes
 *
 * Returns: 1 on success, 0 on failure
 */
unsigned char ready_clip_copy(unsigned char type, void *data, unsigned int size);

/*
 * ready_clip_paste - Paste data from system clipboard
 *
 * buffer: Destination buffer
 * maxsize: Maximum bytes to copy
 *
 * Returns: Number of bytes copied (0 if clipboard empty)
 */
unsigned int ready_clip_paste(void *buffer, unsigned int maxsize);

/*
 * ready_clip_avail - Check clipboard contents
 *
 * Returns: Size of clipboard data (0 if empty)
 */
unsigned int ready_clip_avail(void);

/*
 * ready_clip_type - Get clipboard data type
 *
 * Returns: CLIP_TYPE_* value
 */
unsigned char ready_clip_type(void);

/*
 * ready_deeplink - Launch another app with parameters
 * Current app is suspended, target app is launched
 * If target supports DL_ACTION_RETURN, will return to caller
 *
 * link: Pointer to DeepLink structure
 */
void ready_deeplink(DeepLink *link);

/*
 * ready_query - Query system state
 *
 * query_type: QUERY_* constant
 *
 * Returns: Query result value
 */
unsigned int ready_query(unsigned char query_type);

/*---------------------------------------------------------------------------
 * Convenience Functions
 *---------------------------------------------------------------------------*/

/*
 * ready_app_count - Get number of registered apps
 */
unsigned char ready_app_count(void);

/*
 * ready_current_app - Get current app ID
 *
 * Returns: App ID (0-23) or 0xFF if no app running
 */
unsigned char ready_current_app(void);

/*
 * ready_has_reu - Check if REU is available
 *
 * Returns: 1 if REU present, 0 if not
 */
unsigned char ready_has_reu(void);

/*
 * ready_launch - Simple app launch (no parameters)
 *
 * app_name: Name of app to launch
 */
void ready_launch(const char *app_name);

/*
 * ready_open_file - Launch app to open a file
 *
 * app_name: Name of app to launch
 * filename: File to open
 */
void ready_open_file(const char *app_name, const char *filename);

/*---------------------------------------------------------------------------
 * App Header Macro
 *---------------------------------------------------------------------------*/

/*
 * READY_APP_HEADER - Declare app header at correct memory location
 * Place this at the top of your main app source file
 *
 * Usage:
 *   READY_APP_HEADER("MYAPP", main_entry, APP_FLAG_NONE, 1);
 */
#define READY_APP_HEADER(name, entry, flags, ver) \
    __attribute__((section("APPHEADER"))) \
    const ReadyAppHeader _app_header = { \
        "RDYAPP", \
        name, \
        entry, \
        flags, \
        ver \
    }

#endif /* READY_OS_H */
