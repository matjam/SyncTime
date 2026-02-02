/* window.c - Reaction configuration/status window for SyncTime
 *
 * Uses Reaction (BOOPSI) GUI toolkit for AmigaOS 3.2+
 * Opens a WindowObject with nested LayoutObjects showing sync status
 * and editable configuration fields.
 * Opened via Exchange "Show" or the commodity hotkey.
 *
 * Log is displayed in a separate window.
 */

#include "synctime.h"

/* Reaction includes - need the gadget headers for tag definitions */
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/integer.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>
#include <images/bevel.h>
#include <classes/window.h>

/* Reaction class protos for GetClass functions and utilities */
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/label.h>
#include <proto/window.h>

/* For DoMethod */
#include <clib/alib_protos.h>

/* =========================================================================
 * Gadget IDs
 * ========================================================================= */

#define GID_STATUS      1
#define GID_LAST_SYNC   2
#define GID_NEXT_SYNC   3
#define GID_SERVER      4
#define GID_INTERVAL    5
#define GID_REGION      6
#define GID_CITY        7
#define GID_TZ_INFO     8
#define GID_SYNC        9
#define GID_SAVE        10
#define GID_HIDE        11
#define GID_LOG_TOGGLE  12
#define GID_LOG         13

/* Log system - circular buffer with 2KB limit
 * With max 80 chars per line, 2048/80 = ~25 entries max */
#define LOG_MAX_BYTES   2048
#define LOG_LINE_LEN    80
#define LOG_MAX_ENTRIES (LOG_MAX_BYTES / LOG_LINE_LEN)

/* Maximum regions for chooser */
#define MAX_REGIONS     20

/* =========================================================================
 * Static module state - Main window
 * ========================================================================= */

static struct Screen *pub_screen = NULL;  /* Locked public screen for font settings */
static BOOL pub_screen_locked = FALSE;    /* TRUE only if we LockPubScreen() */
static Object *window_obj = NULL;
static struct Window *win = NULL;

/* Gadget object pointers */
static Object *gad_status    = NULL;
static Object *gad_last_sync = NULL;
static Object *gad_next_sync = NULL;
static Object *gad_server    = NULL;
static Object *gad_interval  = NULL;
static Object *gad_region    = NULL;
static Object *gad_city      = NULL;
static Object *gad_tz_info   = NULL;
static Object *gad_log_toggle = NULL;

/* Layout objects */
static Object *layout_root   = NULL;

/* Timezone selection state */
static ULONG current_region_idx = 0;
static ULONG current_city_idx = 0;
static const TZEntry **current_cities = NULL;
static ULONG current_city_count = 0;

/* =========================================================================
 * Static module state - Log window
 * ========================================================================= */

static Object *log_window_obj = NULL;
static struct Window *log_win = NULL;
static Object *gad_log = NULL;

/* Chooser list for regions */
static struct List region_chooser_list;
static BOOL region_list_initialized = FALSE;

/* ListBrowser list for cities */
static struct List city_browser_list;
static BOOL city_list_initialized = FALSE;

/* ListBrowser list for log - circular buffer */
static struct List log_browser_list;
static BOOL log_list_initialized = FALSE;
static LONG log_count = 0;
static LONG log_bytes_used = 0;  /* Track total bytes for 2KB limit */

/* Buffer for text displays */
static char status_buf[64] = "Idle";
static char last_sync_buf[32] = "Never";
static char next_sync_buf[32] = "Pending";
static char tz_info_buf[64] = "UTC";

/* =========================================================================
 * Helper functions for Chooser/ListBrowser list management
 * ========================================================================= */

/* Free all nodes from a chooser list */
static void free_chooser_list(struct List *list)
{
    struct Node *node;
    while ((node = RemHead(list)) != NULL) {
        FreeChooserNode(node);
    }
}

/* Free all nodes from a listbrowser list */
static void free_listbrowser_list(struct List *list)
{
    struct Node *node;
    while ((node = RemHead(list)) != NULL) {
        FreeListBrowserNode(node);
    }
}

/* Build the region chooser list */
static void build_region_chooser_list(void)
{
    const char **regions;
    ULONG region_count, i;
    struct Node *node;

    if (!region_list_initialized) {
        NewList(&region_chooser_list);
        region_list_initialized = TRUE;
    } else {
        free_chooser_list(&region_chooser_list);
    }

    regions = tz_get_regions(&region_count);
    for (i = 0; i < region_count && i < MAX_REGIONS; i++) {
        node = AllocChooserNode(CNA_Text, (ULONG)regions[i], TAG_DONE);
        if (node) {
            AddTail(&region_chooser_list, node);
        }
    }
}

/* Build the city listbrowser list for a given region */
static void build_city_browser_list(const char *region)
{
    ULONG i;
    struct Node *node;

    if (!city_list_initialized) {
        NewList(&city_browser_list);
        city_list_initialized = TRUE;
    } else {
        free_listbrowser_list(&city_browser_list);
    }

    current_cities = tz_get_cities_for_region(region, &current_city_count);
    for (i = 0; i < current_city_count; i++) {
        node = AllocListBrowserNode(1,
            LBNA_Column, 0,
            LBNCA_Text, (ULONG)current_cities[i]->city,
            TAG_DONE);
        if (node) {
            AddTail(&city_browser_list, node);
        }
    }
}

/* Initialize log list */
static void init_log_list(void)
{
    if (!log_list_initialized) {
        NewList(&log_browser_list);
        log_list_initialized = TRUE;
        log_count = 0;
        log_bytes_used = 0;
    }
}

/* Format timezone info string for display */
static void format_tz_info(const TZEntry *tz)
{
    LONG offset_mins_rem, offset_hrs;
    char sign;
    char *p;

    if (tz == NULL) {
        strcpy(tz_info_buf, "UTC");
        return;
    }

    offset_mins_rem = tz->std_offset_mins;
    sign = (offset_mins_rem >= 0) ? '+' : '-';
    if (offset_mins_rem < 0) offset_mins_rem = -offset_mins_rem;
    offset_hrs = offset_mins_rem / 60;
    offset_mins_rem = offset_mins_rem % 60;

    p = tz_info_buf;

    /* Build "UTC+X" or "UTC+X:MM" */
    *p++ = 'U'; *p++ = 'T'; *p++ = 'C'; *p++ = sign;

    /* Convert hours to string */
    if (offset_hrs >= 10) {
        *p++ = '0' + (offset_hrs / 10);
    }
    *p++ = '0' + (offset_hrs % 10);

    if (offset_mins_rem > 0) {
        *p++ = ':';
        *p++ = '0' + (offset_mins_rem / 10);
        *p++ = '0' + (offset_mins_rem % 10);
    }

    /* Add DST info */
    if (tz->dst_offset_mins > 0) {
        strcpy(p, ", DST active seasonally");
    } else {
        strcpy(p, " (no DST)");
    }
}

/* =========================================================================
 * Helper: Create a label object
 * ========================================================================= */
static Object *create_label(const char *text)
{
    return NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)text,
        TAG_DONE);
}

/* =========================================================================
 * Helper: Create a read-only string gadget for display
 * ========================================================================= */
static Object *create_display_string(ULONG id, const char *text)
{
    return NewObject(STRING_GetClass(), NULL,
        GA_ID, id,
        GA_ReadOnly, TRUE,
        STRINGA_TextVal, (ULONG)text,
        TAG_DONE);
}

/* =========================================================================
 * Helper: Create a horizontal row with label and gadget
 * ========================================================================= */
static Object *create_label_row(const char *label_text, Object *gadget)
{
    Object *label = create_label(label_text);
    if (!label) return NULL;

    return NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddImage, (ULONG)label,  /* Labels are images, not gadgets */
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)gadget,
        TAG_DONE);
}

/* =========================================================================
 * Log window functions
 * ========================================================================= */

static void log_window_open(void)
{
    Object *log_layout;
    WORD log_left, log_top, log_width, log_height;

    if (log_window_obj)
        return;  /* Already open */

    init_log_list();

    /* Calculate log window position and size based on main window */
    if (win) {
        log_left = win->LeftEdge;
        log_top = win->TopEdge + win->Height;  /* Position below main window */
        log_width = win->Width;
        log_height = 120;  /* Fixed height for log display */
    } else {
        /* Fallback if main window not open */
        log_left = 100;
        log_top = 200;
        log_width = 320;
        log_height = 120;
    }

    /* Create log listbrowser */
    gad_log = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GID_LOG,
        GA_ReadOnly, TRUE,
        LISTBROWSER_Labels, (ULONG)&log_browser_list,
        LISTBROWSER_AutoFit, TRUE,
        TAG_DONE);

    if (!gad_log)
        return;

    /* Create log layout */
    log_layout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)gad_log,
        TAG_DONE);

    if (!log_layout) {
        DisposeObject(gad_log);
        gad_log = NULL;
        return;
    }

    /* Create log window - positioned beneath main config window */
    log_window_obj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"SyncTime Log",
        WA_PubScreen, (ULONG)pub_screen,
        WA_Left, log_left,
        WA_Top, log_top,
        WA_Width, log_width,
        WA_Height, log_height,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_SizeGadget, TRUE,
        WA_Activate, FALSE,
        WINDOW_ParentGroup, (ULONG)log_layout,
        TAG_DONE);

    if (!log_window_obj) {
        DisposeObject(log_layout);
        gad_log = NULL;
        return;
    }

    /* Open the window */
    log_win = (struct Window *)DoMethod(log_window_obj, WM_OPEN, NULL);
    if (!log_win) {
        DisposeObject(log_window_obj);
        log_window_obj = NULL;
        gad_log = NULL;
        return;
    }

    /* Scroll to bottom */
    if (log_count > 0) {
        SetGadgetAttrs((struct Gadget *)gad_log, log_win, NULL,
            LISTBROWSER_MakeVisible, log_count - 1,
            TAG_DONE);
    }

    /* Update main window button text */
    if (win && gad_log_toggle) {
        SetGadgetAttrs((struct Gadget *)gad_log_toggle, win, NULL,
            GA_Text, (ULONG)"Hide Log",
            TAG_DONE);
    }
}

static void log_window_close(void)
{
    if (log_win) {
        /* Detach list before closing */
        if (gad_log) {
            SetGadgetAttrs((struct Gadget *)gad_log, log_win, NULL,
                LISTBROWSER_Labels, (ULONG)~0,
                TAG_DONE);
        }
        DoMethod(log_window_obj, WM_CLOSE, NULL);
        log_win = NULL;
    }
    if (log_window_obj) {
        DisposeObject(log_window_obj);
        log_window_obj = NULL;
    }
    gad_log = NULL;

    /* Update main window button text */
    if (win && gad_log_toggle) {
        SetGadgetAttrs((struct Gadget *)gad_log_toggle, win, NULL,
            GA_Text, (ULONG)"Show Log",
            TAG_DONE);
    }
}

static BOOL log_window_is_open(void)
{
    return (log_win != NULL);
}

static void toggle_log_window(void)
{
    if (log_window_is_open()) {
        log_window_close();
    } else {
        log_window_open();
    }
}

/* Handle log window events - returns TRUE if window was closed */
static BOOL log_window_handle_events(void)
{
    ULONG result;
    UWORD code;

    if (!log_window_obj || !log_win)
        return FALSE;

    while ((result = DoMethod(log_window_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        if ((result & WMHI_CLASSMASK) == WMHI_CLOSEWINDOW) {
            log_window_close();
            return TRUE;
        }
    }

    return FALSE;
}

/* =========================================================================
 * window_open -- create and display the Reaction configuration window
 * ========================================================================= */

BOOL window_open(struct Screen *screen)
{
    SyncConfig *cfg;
    const char **regions;
    ULONG region_count, i;
    const TZEntry *tz;
    Object *status_group, *settings_group, *timezone_group, *button_row;
    Object *row;

    /* Initialize log list if needed */
    init_log_list();

    if (window_obj)
        return TRUE;   /* Already open */

    /* Lock the public screen for proper font settings */
    /* Lock the public screen for proper font settings */
    pub_screen_locked = FALSE;

    if (screen) {
        pub_screen = screen;               /* not locked by us */
    } else {
        pub_screen = LockPubScreen(NULL);  /* Lock default (Workbench) screen */
        if (!pub_screen)
            return FALSE;
        pub_screen_locked = TRUE;
    }

    /* Read current config so gadgets reflect live values */
    cfg = config_get();

    /* Find current timezone in table and set up region/city indices */
    regions = tz_get_regions(&region_count);
    tz = tz_find_by_name(cfg->tz_name);

    if (tz) {
        /* Find region index */
        for (i = 0; i < region_count; i++) {
            if (strcmp(regions[i], tz->region) == 0) {
                current_region_idx = i;
                break;
            }
        }
        /* Build city list and find city index */
        build_city_browser_list(tz->region);
        for (i = 0; i < current_city_count; i++) {
            if (strcmp(current_cities[i]->name, cfg->tz_name) == 0) {
                current_city_idx = i;
                break;
            }
        }
        format_tz_info(tz);
    } else {
        current_region_idx = 0;
        if (region_count > 0) {
            build_city_browser_list(regions[0]);
        }
        current_city_idx = 0;
        format_tz_info(NULL);
    }

    /* Build chooser list for regions */
    build_region_chooser_list();

    /* Create status display gadgets */
    gad_status = create_display_string(GID_STATUS, status_buf);
    gad_last_sync = create_display_string(GID_LAST_SYNC, last_sync_buf);
    gad_next_sync = create_display_string(GID_NEXT_SYNC, next_sync_buf);

    if (!gad_status || !gad_last_sync || !gad_next_sync)
        goto cleanup;

    /* Create status group */
    status_group = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, (ULONG)"Status",
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)create_label_row("Status:", gad_status),
        LAYOUT_AddChild, (ULONG)create_label_row("Last sync:", gad_last_sync),
        LAYOUT_AddChild, (ULONG)create_label_row("Next sync:", gad_next_sync),
        TAG_DONE);

    if (!status_group)
        goto cleanup;

    /* Create server and interval gadgets */
    gad_server = NewObject(STRING_GetClass(), NULL,
        GA_ID, GID_SERVER,
        STRINGA_TextVal, (ULONG)cfg->server,
        STRINGA_MaxChars, SERVER_NAME_MAX - 1,
        TAG_DONE);

    gad_interval = NewObject(INTEGER_GetClass(), NULL,
        GA_ID, GID_INTERVAL,
        INTEGER_Number, cfg->interval,
        INTEGER_MaxChars, 6,
        INTEGER_Minimum, MIN_INTERVAL,
        INTEGER_Maximum, MAX_INTERVAL,
        TAG_DONE);

    if (!gad_server || !gad_interval)
        goto cleanup;

    /* Create settings group */
    settings_group = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, (ULONG)"Settings",
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)create_label_row("Server:", gad_server),
        LAYOUT_AddChild, (ULONG)create_label_row("Interval (sec):", gad_interval),
        TAG_DONE);

    if (!settings_group)
        goto cleanup;

    /* Create region chooser */
    gad_region = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID, GID_REGION,
        GA_RelVerify, TRUE,
        CHOOSER_Labels, (ULONG)&region_chooser_list,
        CHOOSER_Selected, current_region_idx,
        TAG_DONE);

    /* Create city listbrowser */
    gad_city = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GID_CITY,
        GA_RelVerify, TRUE,
        LISTBROWSER_Labels, (ULONG)&city_browser_list,
        LISTBROWSER_Selected, current_city_idx,
        LISTBROWSER_ShowSelected, TRUE,
        LISTBROWSER_AutoFit, TRUE,
        TAG_DONE);

    /* Create TZ info display */
    gad_tz_info = create_display_string(GID_TZ_INFO, tz_info_buf);

    if (!gad_region || !gad_city || !gad_tz_info)
        goto cleanup;

    /* Create city row */
    row = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddImage, (ULONG)create_label("City:"),
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)gad_city,
        TAG_DONE);

    /* Create timezone group */
    timezone_group = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, (ULONG)"Timezone",
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)create_label_row("Region:", gad_region),
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)row,
        CHILD_MinHeight, 80,
        LAYOUT_AddChild, (ULONG)gad_tz_info,
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    if (!timezone_group)
        goto cleanup;

    /* Create log toggle button */
    gad_log_toggle = NewObject(BUTTON_GetClass(), NULL,
        GA_ID, GID_LOG_TOGGLE,
        GA_RelVerify, TRUE,
        GA_Text, (ULONG)"Show Log",
        TAG_DONE);

    /* Create button row */
    button_row = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_EvenSize, TRUE,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID, GID_SYNC,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)"Sync Now",
            TAG_DONE),
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID, GID_SAVE,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)"Save",
            TAG_DONE),
        LAYOUT_AddChild, (ULONG)gad_log_toggle,
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
            GA_ID, GID_HIDE,
            GA_RelVerify, TRUE,
            GA_Text, (ULONG)"Hide",
            TAG_DONE),
        TAG_DONE);

    if (!button_row)
        goto cleanup;

    /* Create root layout */
    layout_root = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_BevelStyle, BVS_THIN,
        LAYOUT_AddChild, (ULONG)status_group,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)settings_group,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)timezone_group,
        LAYOUT_AddChild, (ULONG)button_row,
        CHILD_WeightedHeight, 0,
        TAG_DONE);

    if (!layout_root)
        goto cleanup;

    /* Create window object on the public screen for proper font settings */
    window_obj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"SyncTime",
        WA_PubScreen, (ULONG)pub_screen,
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, (ULONG)layout_root,
        TAG_DONE);

    if (!window_obj)
        goto cleanup;

    /* Open the window */
    win = (struct Window *)DoMethod(window_obj, WM_OPEN, NULL);
    if (!win) {
        DisposeObject(window_obj);
        window_obj = NULL;
        return FALSE;
    }

    /* Scroll city list to show selected item near top (with 1 item of context) */
    if (gad_city && current_city_idx > 0) {
        LONG top_idx = (LONG)current_city_idx - 1;
        if (top_idx < 0) top_idx = 0;
        SetGadgetAttrs((struct Gadget *)gad_city, win, NULL,
            LISTBROWSER_Top, top_idx,
            TAG_DONE);
    }

    return TRUE;

cleanup:
    if (layout_root) {
        DisposeObject(layout_root);
    }
    if (pub_screen_locked && pub_screen) {
        UnlockPubScreen(NULL, pub_screen);
    }
    pub_screen = NULL;
    pub_screen_locked = FALSE;
    layout_root = NULL;
    gad_status = gad_last_sync = gad_next_sync = NULL;
    gad_server = gad_interval = NULL;
    gad_region = gad_city = gad_tz_info = NULL;
    gad_log_toggle = NULL;
    return FALSE;
}

/* =========================================================================
 * window_close -- tear down window and free all Reaction resources
 * ========================================================================= */

void window_close(void)
{
    /* Close log window first */
    log_window_close();

    /* Detach lists from gadgets BEFORE disposing (prevents crash) */
    if (win) {
        if (gad_region) {
            SetGadgetAttrs((struct Gadget *)gad_region, win, NULL,
                CHOOSER_Labels, (ULONG)~0,
                TAG_DONE);
        }
        if (gad_city) {
            SetGadgetAttrs((struct Gadget *)gad_city, win, NULL,
                LISTBROWSER_Labels, (ULONG)~0,
                TAG_DONE);
        }
    }

    if (win) {
        DoMethod(window_obj, WM_CLOSE, NULL);
        win = NULL;
    }
    if (window_obj) {
        DisposeObject(window_obj);
        window_obj = NULL;
    }

    /* Free list nodes (safe now that gadgets are gone) */
    if (region_list_initialized) {
        free_chooser_list(&region_chooser_list);
        region_list_initialized = FALSE;
    }
    if (city_list_initialized) {
        free_listbrowser_list(&city_browser_list);
        city_list_initialized = FALSE;
    }
    /* Note: log list is preserved across window open/close */

    /* Unlock the public screen */
    if (pub_screen_locked && pub_screen) {
        UnlockPubScreen(NULL, pub_screen);
    }
    pub_screen = NULL;
    pub_screen_locked = FALSE;

    /* Reset object pointers */
    layout_root = NULL;
    gad_status = gad_last_sync = gad_next_sync = NULL;
    gad_server = gad_interval = NULL;
    gad_region = gad_city = gad_tz_info = NULL;
    gad_log_toggle = NULL;
}

/* =========================================================================
 * window_is_open -- query whether the window is currently displayed
 * ========================================================================= */

BOOL window_is_open(void)
{
    return (win != NULL);
}

/* =========================================================================
 * window_signal -- return the signal mask for the window's message port
 * ========================================================================= */

ULONG window_signal(void)
{
    ULONG sig = 0;

    if (win)
        sig |= 1UL << win->UserPort->mp_SigBit;
    if (log_win)
        sig |= 1UL << log_win->UserPort->mp_SigBit;

    return sig;
}

/* =========================================================================
 * Helper: handle_region_change -- rebuild city list when region changes
 * ========================================================================= */

static void handle_region_change(ULONG new_region)
{
    const char **regions;
    ULONG region_count;

    regions = tz_get_regions(&region_count);
    if (new_region >= region_count)
        return;

    current_region_idx = new_region;

    /* Detach list from gadget before modifying */
    SetGadgetAttrs((struct Gadget *)gad_city, win, NULL,
        LISTBROWSER_Labels, (ULONG)~0,
        TAG_DONE);

    /* Rebuild city list */
    build_city_browser_list(regions[new_region]);
    current_city_idx = 0;

    /* Reattach list */
    SetGadgetAttrs((struct Gadget *)gad_city, win, NULL,
        LISTBROWSER_Labels, (ULONG)&city_browser_list,
        LISTBROWSER_Selected, 0,
        TAG_DONE);

    /* Update TZ info */
    if (current_city_count > 0) {
        format_tz_info(current_cities[0]);
        SetGadgetAttrs((struct Gadget *)gad_tz_info, win, NULL,
            STRINGA_TextVal, (ULONG)tz_info_buf,
            TAG_DONE);
    }
}

/* =========================================================================
 * Helper: handle_city_change -- update TZ info when city changes
 * ========================================================================= */

static void handle_city_change(ULONG new_city)
{
    if (new_city >= current_city_count)
        return;

    current_city_idx = new_city;
    format_tz_info(current_cities[new_city]);
    SetGadgetAttrs((struct Gadget *)gad_tz_info, win, NULL,
        STRINGA_TextVal, (ULONG)tz_info_buf,
        TAG_DONE);
}

/* =========================================================================
 * Helper: save_config_from_gadgets -- read gadget values and save config
 * ========================================================================= */

static void save_config_from_gadgets(void)
{
    STRPTR server_str = NULL;
    LONG interval_val = 0;

    /* Get server string */
    GetAttr(STRINGA_TextVal, gad_server, (ULONG *)&server_str);
    if (server_str) {
        config_set_server(server_str);
    }

    /* Get interval */
    GetAttr(INTEGER_Number, gad_interval, (ULONG *)&interval_val);
    config_set_interval(interval_val);

    /* Set timezone from current city selection */
    if (current_city_count > 0 && current_city_idx < current_city_count) {
        config_set_tz_name(current_cities[current_city_idx]->name);
        /* Update TZ/TZONE environment variables */
        tz_set_env(current_cities[current_city_idx]);
    }

    config_save();
}

/* =========================================================================
 * window_handle_events -- process all pending Reaction messages
 *
 * cfg: pointer to live config struct (updated on Save)
 * st:  pointer to sync status (currently unused here, reserved for future)
 *
 * Returns TRUE if "Sync Now" was pressed, FALSE otherwise.
 * ========================================================================= */

BOOL window_handle_events(SyncConfig *cfg, SyncStatus *st)
{
    ULONG result;
    UWORD code;
    BOOL sync_requested = FALSE;

    (void)cfg;  /* Config is accessed via config_get() */
    (void)st;   /* Status is updated via window_update_status */

    /* Handle log window events first */
    if (log_win) {
        log_window_handle_events();
    }

    if (!window_obj || !win)
        return FALSE;

    while ((result = DoMethod(window_obj, WM_HANDLEINPUT, &code)) != WMHI_LASTMSG) {
        switch (result & WMHI_CLASSMASK) {
            case WMHI_CLOSEWINDOW:
                window_close();
                return sync_requested;

            case WMHI_GADGETUP:
                switch (result & WMHI_GADGETMASK) {
                    case GID_SYNC:
                        sync_requested = TRUE;
                        break;

                    case GID_SAVE:
                        save_config_from_gadgets();
                        break;

                    case GID_HIDE:
                        window_close();
                        return sync_requested;

                    case GID_LOG_TOGGLE:
                        toggle_log_window();
                        break;

                    case GID_REGION:
                        handle_region_change(code);
                        break;

                    case GID_CITY:
                        handle_city_change(code);
                        break;
                }
                break;
        }
    }

    return sync_requested;
}

/* =========================================================================
 * window_update_status -- refresh the status display gadgets
 * ========================================================================= */

void window_update_status(SyncStatus *st)
{
    if (!win)
        return;

    /* Copy to static buffers */
    strcpy(status_buf, st->status_text);
    strcpy(last_sync_buf, st->last_sync_text);
    strcpy(next_sync_buf, st->next_sync_text);

    /* Update gadgets */
    if (gad_status) {
        SetGadgetAttrs((struct Gadget *)gad_status, win, NULL,
            STRINGA_TextVal, (ULONG)status_buf, TAG_DONE);
    }
    if (gad_last_sync) {
        SetGadgetAttrs((struct Gadget *)gad_last_sync, win, NULL,
            STRINGA_TextVal, (ULONG)last_sync_buf, TAG_DONE);
    }
    if (gad_next_sync) {
        SetGadgetAttrs((struct Gadget *)gad_next_sync, win, NULL,
            STRINGA_TextVal, (ULONG)next_sync_buf, TAG_DONE);
    }
}

/* =========================================================================
 * window_log -- add an entry to the scrollable log (2KB circular buffer)
 * ========================================================================= */

void window_log(const char *message)
{
    struct Node *node;
    char *text_copy;
    LONG i, len;

    /* Initialize log list if needed (may be called before window_open) */
    init_log_list();

    /* Calculate message length (capped at LOG_LINE_LEN - 1) */
    for (len = 0; message[len] != '\0' && len < LOG_LINE_LEN - 1; len++)
        ;

    /* Allocate text copy (node will own this memory) */
    text_copy = AllocVec(len + 1, MEMF_ANY);
    if (!text_copy)
        return;

    for (i = 0; i < len; i++)
        text_copy[i] = message[i];
    text_copy[len] = '\0';

    /* Create listbrowser node */
    node = AllocListBrowserNode(1,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)text_copy,
        TAG_DONE);

    FreeVec(text_copy);  /* Node made a copy */

    if (!node)
        return;

    /* Circular buffer: remove oldest entries to stay within 2KB limit
     * We track actual bytes used for accuracy, falling back to count limit */
    while ((log_bytes_used + len + 1 > LOG_MAX_BYTES || log_count >= LOG_MAX_ENTRIES)
           && log_count > 0) {
        struct Node *old = RemHead(&log_browser_list);
        if (old) {
            /* Use average line length estimate when removing old entries
             * (can't easily retrieve old text length from node) */
            log_bytes_used -= (LOG_LINE_LEN / 2);
            if (log_bytes_used < 0) log_bytes_used = 0;
            FreeListBrowserNode(old);
            log_count--;
        }
    }

    /* Add to end of list */
    AddTail(&log_browser_list, node);
    log_count++;
    log_bytes_used += len + 1;  /* +1 for null terminator */

    /* Update log window listbrowser if open */
    if (log_win && gad_log) {
        SetGadgetAttrs((struct Gadget *)gad_log, log_win, NULL,
            LISTBROWSER_Labels, (ULONG)&log_browser_list,
            LISTBROWSER_MakeVisible, log_count - 1,  /* Auto-scroll to bottom */
            TAG_DONE);
    }
}
