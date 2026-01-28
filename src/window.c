/* window.c - Reaction configuration/status window for SyncTime
 *
 * Uses Reaction (BOOPSI) GUI toolkit for AmigaOS 3.2+
 * Opens a WindowObject with nested LayoutObjects showing sync status
 * and editable configuration fields.
 * Opened via Exchange "Show" or the commodity hotkey.
 *
 * Note: We use explicit NewObject calls with tag arrays instead of
 * the convenience macros, for cross-compiler compatibility.
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

/* Log system */
#define LOG_MAX_ENTRIES 50
#define LOG_LINE_LEN    64

/* Maximum regions for chooser */
#define MAX_REGIONS     20

/* =========================================================================
 * Static module state
 * ========================================================================= */

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
static Object *gad_log       = NULL;
static Object *gad_log_toggle = NULL;

/* Layout objects */
static Object *layout_root   = NULL;
static Object *layout_log    = NULL;

/* Log visibility state */
static BOOL log_visible = FALSE;

/* Log panel height for window resizing */
#define LOG_PANEL_HEIGHT 120

/* Window height without log (stored when window opens) */
static UWORD base_window_height = 0;

/* Timezone selection state */
static ULONG current_region_idx = 0;
static ULONG current_city_idx = 0;
static const TZEntry **current_cities = NULL;
static ULONG current_city_count = 0;

/* Chooser list for regions */
static struct List region_chooser_list;
static BOOL region_list_initialized = FALSE;

/* ListBrowser list for cities */
static struct List city_browser_list;
static BOOL city_list_initialized = FALSE;

/* ListBrowser list for log */
static struct List log_browser_list;
static BOOL log_list_initialized = FALSE;
static LONG log_count = 0;

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
        LAYOUT_AddChild, (ULONG)label,
        CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)gadget,
        TAG_DONE);
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

    (void)screen;  /* Not used -- we open on default public screen */

    /* Initialize log list if needed */
    init_log_list();

    if (window_obj)
        return TRUE;   /* Already open */

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

    /* Create city row with minimum height */
    row = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild, (ULONG)create_label("City:"),
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
        CHILD_WeightedHeight, 0,  /* Region row: fixed height */
        LAYOUT_AddChild, (ULONG)row,
        CHILD_MinHeight, 80,      /* City row: minimum 80, but can grow */
        LAYOUT_AddChild, (ULONG)gad_tz_info,
        CHILD_WeightedHeight, 0,  /* TZ info: fixed height */
        TAG_DONE);

    if (!timezone_group)
        goto cleanup;

    /* Create buttons */
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

    /* Create log listbrowser (always present, hidden by window size) */
    gad_log = NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID, GID_LOG,
        GA_ReadOnly, TRUE,
        LISTBROWSER_Labels, (ULONG)&log_browser_list,
        LISTBROWSER_AutoFit, TRUE,
        LISTBROWSER_VertSeparators, FALSE,
        TAG_DONE);

    if (!gad_log)
        goto cleanup;

    /* Create log panel layout */
    layout_log = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle, BVS_GROUP,
        LAYOUT_Label, (ULONG)"Log",
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_AddChild, (ULONG)gad_log,
        TAG_DONE);

    if (!layout_log)
        goto cleanup;

    /* Create root layout (includes log panel - hidden by window size initially) */
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
        LAYOUT_AddChild, (ULONG)layout_log,
        CHILD_MinHeight, LOG_PANEL_HEIGHT,
        TAG_DONE);

    if (!layout_root)
        goto cleanup;

    /* Create window object */
    window_obj = NewObject(WINDOW_GetClass(), NULL,
        WA_Title, (ULONG)"SyncTime",
        WA_DragBar, TRUE,
        WA_CloseGadget, TRUE,
        WA_DepthGadget, TRUE,
        WA_Activate, TRUE,
        WA_SizeGadget, TRUE,
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

    /* Store base height and shrink to hide log panel initially */
    base_window_height = win->Height - LOG_PANEL_HEIGHT;
    ChangeWindowBox(win, win->LeftEdge, win->TopEdge,
        win->Width, base_window_height);

    log_visible = FALSE;

    return TRUE;

cleanup:
    /* Clean up any partially created objects.
     * Note: Once an object is added to a layout, the layout owns it.
     * Disposing the parent disposes all children.
     * Only dispose objects that weren't added to a parent yet.
     */
    if (layout_root) {
        /* layout_root owns everything added to it */
        DisposeObject(layout_root);
    } else if (layout_log) {
        /* layout_log owns gad_log */
        DisposeObject(layout_log);
    } else if (gad_log) {
        DisposeObject(gad_log);
    }
    /* Note: status_group, settings_group, timezone_group, button_row
     * would be owned by layout_root if it was created */
    layout_root = layout_log = NULL;
    gad_status = gad_last_sync = gad_next_sync = NULL;
    gad_server = gad_interval = NULL;
    gad_region = gad_city = gad_tz_info = NULL;
    gad_log = gad_log_toggle = NULL;
    return FALSE;
}

/* =========================================================================
 * window_close -- tear down window and free all Reaction resources
 * ========================================================================= */

void window_close(void)
{
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
        if (gad_log) {
            SetGadgetAttrs((struct Gadget *)gad_log, win, NULL,
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

    /* Reset object pointers */
    layout_root = layout_log = NULL;
    gad_status = gad_last_sync = gad_next_sync = NULL;
    gad_server = gad_interval = NULL;
    gad_region = gad_city = gad_tz_info = NULL;
    gad_log = gad_log_toggle = NULL;
    log_visible = FALSE;
    base_window_height = 0;
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
    if (win)
        return 1UL << win->UserPort->mp_SigBit;
    return 0;
}

/* =========================================================================
 * Helper: toggle_log_panel -- show or hide the log panel
 * ========================================================================= */

static void toggle_log_panel(void)
{
    if (!win || !base_window_height)
        return;

    if (!log_visible) {
        /* Show log: expand window to reveal log panel */
        ChangeWindowBox(win, win->LeftEdge, win->TopEdge,
            win->Width, base_window_height + LOG_PANEL_HEIGHT);

        /* Update button text */
        SetGadgetAttrs((struct Gadget *)gad_log_toggle, win, NULL,
            GA_Text, (ULONG)"Hide Log",
            TAG_DONE);

        /* Scroll log to bottom if there are entries */
        if (gad_log && log_count > 0) {
            SetGadgetAttrs((struct Gadget *)gad_log, win, NULL,
                LISTBROWSER_MakeVisible, log_count - 1,
                TAG_DONE);
        }

        log_visible = TRUE;
    } else {
        /* Hide log: shrink window to clip log panel */
        ChangeWindowBox(win, win->LeftEdge, win->TopEdge,
            win->Width, base_window_height);

        /* Update button text */
        SetGadgetAttrs((struct Gadget *)gad_log_toggle, win, NULL,
            GA_Text, (ULONG)"Show Log",
            TAG_DONE);

        log_visible = FALSE;
    }
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
                        toggle_log_panel();
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
 * window_log -- add an entry to the scrollable log
 * ========================================================================= */

void window_log(const char *message)
{
    struct Node *node;
    char *text_copy;
    LONG i, len;

    /* Initialize log list if needed (may be called before window_open) */
    init_log_list();

    /* Calculate message length */
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

    /* If we're at max entries, remove oldest */
    if (log_count >= LOG_MAX_ENTRIES) {
        struct Node *old = RemHead(&log_browser_list);
        if (old) {
            FreeListBrowserNode(old);
            log_count--;
        }
    }

    /* Add to end of list */
    AddTail(&log_browser_list, node);
    log_count++;

    /* Update listbrowser if visible */
    if (win && gad_log && log_visible) {
        SetGadgetAttrs((struct Gadget *)gad_log, win, NULL,
            LISTBROWSER_Labels, (ULONG)&log_browser_list,
            LISTBROWSER_MakeVisible, log_count - 1,  /* Auto-scroll to bottom */
            TAG_DONE);
    }
}
