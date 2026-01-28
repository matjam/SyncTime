/* window.c - GadTools configuration/status window for SyncTime
 *
 * Opens a GadTools window on the default public screen (Workbench)
 * showing sync status and editable configuration fields.
 * Opened via Exchange "Show" or the commodity hotkey.
 */

#include "synctime.h"

/* =========================================================================
 * Gadget IDs
 * ========================================================================= */

#define GID_STATUS     0
#define GID_LAST_SYNC  1
#define GID_NEXT_SYNC  2
#define GID_LOG        3
#define GID_SERVER     4
#define GID_INTERVAL   5
#define GID_TIMEZONE   6
#define GID_DST        7
#define GID_SYNC       8
#define GID_SAVE       9
#define GID_HIDE       10

/* Log system */
#define LOG_MAX_ENTRIES 50
#define LOG_LINE_LEN    64

/* =========================================================================
 * Timezone cycle labels (NULL-terminated for GadTools CYCLE_KIND)
 * ========================================================================= */

static const char *tz_labels[] = {
    "UTC-12", "UTC-11", "UTC-10", "UTC-9", "UTC-8", "UTC-7",
    "UTC-6",  "UTC-5",  "UTC-4",  "UTC-3", "UTC-2", "UTC-1",
    "UTC+0",  "UTC+1",  "UTC+2",  "UTC+3", "UTC+4", "UTC+5",
    "UTC+6",  "UTC+7",  "UTC+8",  "UTC+9", "UTC+10","UTC+11",
    "UTC+12", "UTC+13", "UTC+14", NULL
};

/* =========================================================================
 * Static module state
 * ========================================================================= */

static struct Window *win   = NULL;
static struct Gadget *glist = NULL;
static APTR vi              = NULL;   /* VisualInfo */

/* Individual gadget pointers for updating */
static struct Gadget *gad_status    = NULL;
static struct Gadget *gad_last_sync = NULL;
static struct Gadget *gad_next_sync = NULL;
static struct Gadget *gad_log       = NULL;
static struct Gadget *gad_server    = NULL;
static struct Gadget *gad_interval  = NULL;
static struct Gadget *gad_timezone  = NULL;
static struct Gadget *gad_dst       = NULL;

/* Local edit state */
static LONG local_tz_index  = 0;
static BOOL local_dst       = FALSE;
static BOOL config_changed  = FALSE;

/* Log entries stored as Exec List of Node structures */
static struct List log_list;
static LONG log_count = 0;

/* Log node structure - Node followed by text buffer */
struct LogNode {
    struct Node node;
    char text[LOG_LINE_LEN];
};

static struct LogNode log_nodes[LOG_MAX_ENTRIES];
static LONG log_next_slot = 0;

/* =========================================================================
 * window_open -- create and display the GadTools configuration window
 * ========================================================================= */

/* Initialize log list (called once) */
static BOOL log_initialized = FALSE;
static void init_log_list(void)
{
    LONG i;
    if (log_initialized)
        return;
    NewList(&log_list);
    for (i = 0; i < LOG_MAX_ENTRIES; i++) {
        log_nodes[i].node.ln_Succ = NULL;
        log_nodes[i].node.ln_Pred = NULL;
        log_nodes[i].text[0] = '\0';
    }
    log_next_slot = 0;
    log_count = 0;
    log_initialized = TRUE;
}

BOOL window_open(struct Screen *screen)
{
    struct Screen *pub;
    struct TextAttr *font;
    UWORD fonth, topoff, leftoff, label_width, gad_left, gad_width, win_width;
    UWORD spacing, y;
    struct Gadget *gad;
    struct NewGadget ng;
    SyncConfig *cfg;

    (void)screen;  /* Not used -- we lock the default public screen */

    /* Initialize log list if needed */
    init_log_list();

    if (win)
        return TRUE;   /* Already open */

    /* Read current config so gadgets reflect live values */
    cfg = config_get();
    local_tz_index = cfg->timezone + 12;
    local_dst      = cfg->dst;
    config_changed = FALSE;

    /* Lock default public screen (Workbench) */
    pub = LockPubScreen(NULL);
    if (!pub)
        return FALSE;

    vi = GetVisualInfo(pub, TAG_DONE);
    if (!vi) {
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    /* Font-relative sizing */
    font       = pub->Font;
    fonth      = font->ta_YSize;
    topoff     = pub->WBorTop + fonth + 1;
    leftoff    = pub->WBorLeft + 4;
    label_width = 80;
    gad_left   = leftoff + label_width;
    gad_width  = 220;
    win_width  = gad_left + gad_width + pub->WBorRight + 8;
    spacing    = fonth + 6;
    y          = topoff + 4;

    /* Create gadget context */
    gad = CreateContext(&glist);
    if (!gad) {
        FreeVisualInfo(vi);
        vi = NULL;
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    memset(&ng, 0, sizeof(ng));
    ng.ng_VisualInfo = vi;
    ng.ng_TextAttr   = font;

    /* ---- Status (TEXT_KIND) ---- */
    ng.ng_LeftEdge   = gad_left;
    ng.ng_TopEdge    = y;
    ng.ng_Width      = gad_width;
    ng.ng_Height     = fonth + 4;
    ng.ng_GadgetText = "Status:";
    ng.ng_GadgetID   = GID_STATUS;
    ng.ng_Flags      = PLACETEXT_LEFT;
    gad_status = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text,   (ULONG)"Idle",
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* ---- Last sync (TEXT_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Last sync:";
    ng.ng_GadgetID   = GID_LAST_SYNC;
    gad_last_sync = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text,   (ULONG)"Never",
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* ---- Next sync (TEXT_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Next sync:";
    ng.ng_GadgetID   = GID_NEXT_SYNC;
    gad_next_sync = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Text,   (ULONG)"Pending",
        GTTX_Border, TRUE,
        TAG_DONE);
    y += spacing;

    /* Extra gap before log */
    y += 4;

    /* ---- Log (LISTVIEW_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Log:";
    ng.ng_GadgetID   = GID_LOG;
    ng.ng_Height     = fonth * 5 + 4;  /* 5 lines visible */
    gad_log = gad = CreateGadget(LISTVIEW_KIND, gad, &ng,
        GTLV_Labels,     (ULONG)&log_list,
        GTLV_ReadOnly,   TRUE,
        GTLV_ScrollWidth, 16,
        TAG_DONE);
    y += ng.ng_Height + 4;

    /* Reset height for other gadgets */
    ng.ng_Height = fonth + 4;

    /* Extra gap before editable section */
    y += 4;

    /* ---- Server (STRING_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Server:";
    ng.ng_GadgetID   = GID_SERVER;
    gad_server = gad = CreateGadget(STRING_KIND, gad, &ng,
        GTST_String,   (ULONG)cfg->server,
        GTST_MaxChars, SERVER_NAME_MAX - 1,
        TAG_DONE);
    y += spacing;

    /* ---- Interval (INTEGER_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Interval:";
    ng.ng_GadgetID   = GID_INTERVAL;
    gad_interval = gad = CreateGadget(INTEGER_KIND, gad, &ng,
        GTIN_Number,   cfg->interval,
        GTIN_MaxChars, 6,
        TAG_DONE);
    y += spacing;

    /* ---- Timezone (CYCLE_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_GadgetText = "Timezone:";
    ng.ng_GadgetID   = GID_TIMEZONE;
    gad_timezone = gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, (ULONG)tz_labels,
        GTCY_Active, local_tz_index,
        TAG_DONE);
    y += spacing;

    /* ---- DST (CHECKBOX_KIND) ---- */
    ng.ng_TopEdge    = y;
    ng.ng_Width      = fonth + 4;   /* Checkbox is square */
    ng.ng_GadgetText = "DST:";
    ng.ng_GadgetID   = GID_DST;
    gad_dst = gad = CreateGadget(CHECKBOX_KIND, gad, &ng,
        GTCB_Checked, (LONG)local_dst,
        GTCB_Scaled,  TRUE,
        TAG_DONE);
    y += spacing;

    /* Extra gap before buttons */
    y += 10;

    /* Button row: Sync Now, Save, Hide (3 buttons with 5px gaps) */
    {
        UWORD btn_width = (gad_width - 10) / 3;  /* 3 buttons, 2 gaps of 5px */
        UWORD btn_gap = 5;

        ng.ng_TopEdge    = y;
        ng.ng_Height     = fonth + 6;
        ng.ng_Flags      = PLACETEXT_IN;

        /* ---- Sync Now button ---- */
        ng.ng_LeftEdge   = gad_left;
        ng.ng_Width      = btn_width;
        ng.ng_GadgetText = "Sync Now";
        ng.ng_GadgetID   = GID_SYNC;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

        /* ---- Save button ---- */
        ng.ng_LeftEdge   = gad_left + btn_width + btn_gap;
        ng.ng_GadgetText = "Save";
        ng.ng_GadgetID   = GID_SAVE;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

        /* ---- Hide button ---- */
        ng.ng_LeftEdge   = gad_left + 2 * (btn_width + btn_gap);
        ng.ng_GadgetText = "Hide";
        ng.ng_GadgetID   = GID_HIDE;
        gad = CreateGadget(BUTTON_KIND, gad, &ng, TAG_DONE);

        y += ng.ng_Height;
    }

    if (!gad) {
        /* One or more gadgets failed to create */
        FreeGadgets(glist);
        glist = NULL;
        FreeVisualInfo(vi);
        vi = NULL;
        gad_status = gad_last_sync = gad_next_sync = gad_log = NULL;
        gad_server = gad_interval = gad_timezone = gad_dst = NULL;
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    /* Open the window */
    win = OpenWindowTags(NULL,
        WA_Left,        100,
        WA_Top,         50,
        WA_Width,       win_width,
        WA_Height,      y + fonth + 8 + pub->WBorBottom,
        WA_Title,       (ULONG)"SyncTime",
        WA_PubScreen,   (ULONG)pub,
        WA_Gadgets,     (ULONG)glist,
        WA_IDCMP,       IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW |
                        BUTTONIDCMP | STRINGIDCMP | CYCLEIDCMP |
                        CHECKBOXIDCMP,
        WA_DragBar,     TRUE,
        WA_DepthGadget, TRUE,
        WA_CloseGadget, TRUE,
        WA_Activate,    TRUE,
        WA_RMBTrap,     TRUE,
        TAG_DONE);

    if (!win) {
        FreeGadgets(glist);
        glist = NULL;
        FreeVisualInfo(vi);
        vi = NULL;
        gad_status = gad_last_sync = gad_next_sync = gad_log = NULL;
        gad_server = gad_interval = gad_timezone = gad_dst = NULL;
        UnlockPubScreen(NULL, pub);
        return FALSE;
    }

    GT_RefreshWindow(win, NULL);
    UnlockPubScreen(NULL, pub);

    return TRUE;
}

/* =========================================================================
 * window_close -- tear down window and free all GadTools resources
 * ========================================================================= */

void window_close(void)
{
    if (win) {
        CloseWindow(win);
        win = NULL;
    }
    if (glist) {
        FreeGadgets(glist);
        glist = NULL;
    }
    if (vi) {
        FreeVisualInfo(vi);
        vi = NULL;
    }

    gad_status    = NULL;
    gad_last_sync = NULL;
    gad_next_sync = NULL;
    gad_log       = NULL;
    gad_server    = NULL;
    gad_interval  = NULL;
    gad_timezone  = NULL;
    gad_dst       = NULL;
}

/* =========================================================================
 * window_is_open -- query whether the window is currently displayed
 * ========================================================================= */

BOOL window_is_open(void)
{
    return (win != NULL);
}

/* =========================================================================
 * window_handle_events -- process all pending Intuition/GadTools messages
 *
 * cfg: pointer to live config struct (updated on Save)
 * st:  pointer to sync status (currently unused here, reserved for future)
 *
 * Returns TRUE if "Sync Now" was pressed, FALSE otherwise.
 * ========================================================================= */

BOOL window_handle_events(SyncConfig *cfg, SyncStatus *st)
{
    struct IntuiMessage *msg;
    BOOL sync_requested = FALSE;

    (void)st;  /* status is updated via window_update_status */

    if (!win)
        return FALSE;

    while ((msg = GT_GetIMsg(win->UserPort))) {
        ULONG class = msg->Class;
        UWORD code  = msg->Code;
        struct Gadget *gad = (struct Gadget *)msg->IAddress;

        GT_ReplyIMsg(msg);

        switch (class) {
            case IDCMP_CLOSEWINDOW:
                window_close();
                return sync_requested;  /* Window is gone -- stop processing */

            case IDCMP_REFRESHWINDOW:
                GT_BeginRefresh(win);
                GT_EndRefresh(win, TRUE);
                break;

            case IDCMP_GADGETUP:
                switch (gad->GadgetID) {
                    case GID_SYNC:
                        sync_requested = TRUE;
                        break;

                    case GID_SAVE: {
                        /* Read current gadget values and push into config */
                        char *srv;
                        LONG intv;

                        srv  = ((struct StringInfo *)gad_server->SpecialInfo)->Buffer;
                        intv = ((struct StringInfo *)gad_interval->SpecialInfo)->LongInt;

                        config_set_server(srv);
                        config_set_interval(intv);
                        config_set_timezone(local_tz_index - 12);
                        config_set_dst(local_dst);
                        config_save();
                        config_changed = TRUE;

                        /* cfg already points to the static config struct
                         * returned by config_get(), so it's already updated.
                         * No copy needed.
                         */
                        (void)cfg;
                        break;
                    }

                    case GID_HIDE:
                        window_close();
                        return sync_requested;

                    case GID_TIMEZONE:
                        local_tz_index = code;
                        break;

                    case GID_DST:
                        local_dst = !local_dst;
                        break;
                }
                break;
        }
    }

    return sync_requested;
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
 * window_update_status -- refresh the three read-only text gadgets
 * ========================================================================= */

void window_update_status(SyncStatus *st)
{
    if (!win)
        return;

    GT_SetGadgetAttrs(gad_status, win, NULL,
        GTTX_Text, (ULONG)st->status_text, TAG_DONE);
    GT_SetGadgetAttrs(gad_last_sync, win, NULL,
        GTTX_Text, (ULONG)st->last_sync_text, TAG_DONE);
    GT_SetGadgetAttrs(gad_next_sync, win, NULL,
        GTTX_Text, (ULONG)st->next_sync_text, TAG_DONE);
}

/* =========================================================================
 * window_log -- add an entry to the scrollable log
 * ========================================================================= */

void window_log(const char *message)
{
    struct LogNode *node;
    LONG i;

    /* Get next slot (circular buffer) */
    node = &log_nodes[log_next_slot];

    /* If this node is already in the list, remove it */
    if (node->node.ln_Succ != NULL) {
        Remove(&node->node);
    }

    /* Copy message text */
    for (i = 0; i < LOG_LINE_LEN - 1 && message[i] != '\0'; i++) {
        node->text[i] = message[i];
    }
    node->text[i] = '\0';

    /* Set up node */
    node->node.ln_Name = node->text;
    node->node.ln_Type = 0;
    node->node.ln_Pri = 0;

    /* Add to end of list */
    AddTail(&log_list, &node->node);

    /* Advance slot */
    log_next_slot = (log_next_slot + 1) % LOG_MAX_ENTRIES;
    if (log_count < LOG_MAX_ENTRIES)
        log_count++;

    /* Update listview if window is open */
    if (win && gad_log) {
        GT_SetGadgetAttrs(gad_log, win, NULL,
            GTLV_Labels, (ULONG)&log_list,
            GTLV_Top, log_count > 5 ? log_count - 5 : 0,  /* Auto-scroll to bottom */
            TAG_DONE);
    }
}
