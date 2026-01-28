/* main.c - SyncTime entry point and commodity event loop
 *
 * Ties together all modules: config, network, sntp, clock, window.
 * Implements the commodity broker, hotkey handling, periodic sync,
 * and the main event loop.
 */

#include "synctime.h"

/* =========================================================================
 * Static state & globals
 * ========================================================================= */

/* Request 16KB stack */
LONG __stack = 16384;

/* AmigaOS version string */
const char verstag[] =
    "\0$VER: SyncTime " VERSION_STRING " (" BUILD_DATE ") " COMMIT_HASH;

/* Library bases (extern'd in synctime.h) */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *GadToolsBase  = NULL;
struct Library       *CxBase        = NULL;
struct Library       *UtilityBase   = NULL;
struct Library       *SocketBase    = NULL;
struct Device        *TimerBase     = NULL;

/* Commodity state */
static CxObj *broker    = NULL;
static struct MsgPort *broker_port = NULL;
static BOOL running     = TRUE;
static BOOL cx_enabled  = TRUE;

/* Sync state */
static SyncStatus sync_status;

/* Custom event ID for hotkey */
#define EVT_HOTKEY 1

/* =========================================================================
 * Forward declarations
 * ========================================================================= */

static BOOL open_libraries(void);
static void close_libraries(void);
static BOOL setup_commodity(int argc, char **argv);
static void cleanup_commodity(void);
static void perform_sync(void);
static void event_loop(void);

/* =========================================================================
 * open_libraries / close_libraries
 * ========================================================================= */

static BOOL open_libraries(void)
{
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library",
                                                        LIB_VERSION);
    if (IntuitionBase == NULL)
        return FALSE;

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", LIB_VERSION);
    if (GfxBase == NULL)
        return FALSE;

    GadToolsBase = OpenLibrary("gadtools.library", LIB_VERSION);
    if (GadToolsBase == NULL)
        return FALSE;

    CxBase = OpenLibrary("commodities.library", LIB_VERSION);
    if (CxBase == NULL)
        return FALSE;

    UtilityBase = OpenLibrary("utility.library", LIB_VERSION);
    if (UtilityBase == NULL)
        return FALSE;

    /* bsdsocket.library is opened by network_init() */
    /* timer.device is opened by clock_init() */

    return TRUE;
}

static void close_libraries(void)
{
    /* Close in reverse order; NULL-safe */
    if (UtilityBase != NULL) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
    }
    if (CxBase != NULL) {
        CloseLibrary(CxBase);
        CxBase = NULL;
    }
    if (GadToolsBase != NULL) {
        CloseLibrary(GadToolsBase);
        GadToolsBase = NULL;
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

/* =========================================================================
 * setup_commodity / cleanup_commodity
 * ========================================================================= */

static BOOL setup_commodity(int argc, char **argv)
{
    LONG priority;
    STRPTR popup;
    STRPTR popkey;
    STRPTR *ttypes;
    CxObj *filter;
    struct NewBroker nb;

    /* Create message port for broker */
    broker_port = CreateMsgPort();
    if (broker_port == NULL)
        return FALSE;

    /* Parse tooltypes / CLI args for commodity overrides.
     * ArgArrayInit requires valid argc/argv for CLI launch.
     * When argc > 0, it uses argv directly for tooltype parsing.
     * When argc == 0, it assumes Workbench launch and uses argv
     * as a WBStartup message pointer (handled by C startup code).
     */
    ttypes = ArgArrayInit(argc, (CONST_STRPTR *)argv);

    priority = ArgInt((CONST_STRPTR *)ttypes, "CX_PRIORITY", CX_DEFAULT_PRI);
    popup    = ArgString((CONST_STRPTR *)ttypes, "CX_POPUP", "NO");
    popkey   = ArgString((CONST_STRPTR *)ttypes, "CX_POPKEY", CX_DEFAULT_POPKEY);

    /* Build the NewBroker structure */
    nb.nb_Version = NB_VERSION;
    nb.nb_Name    = CX_NAME;
    nb.nb_Title   = CX_TITLE;
    nb.nb_Descr   = CX_DESCR;
    nb.nb_Unique  = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Flags   = 0;
    nb.nb_Pri     = priority;
    nb.nb_Port    = broker_port;
    nb.nb_ReservedChannel = 0;

    broker = CxBroker(&nb, NULL);

    if (broker != NULL) {
        /* Create hotkey filter chain */
        filter = CxFilter(popkey);
        if (filter != NULL) {
            AttachCxObj(filter, CxSender(broker_port, EVT_HOTKEY));
            AttachCxObj(filter, CxTranslate(NULL));
            AttachCxObj(broker, filter);
        }

        /* Activate the broker */
        ActivateCxObj(broker, TRUE);

        /* Check if we should open window on startup */
        if (Stricmp(popup, "YES") == 0) {
            window_open(NULL);
        }
    }

    ArgArrayDone();

    return (broker != NULL);
}

static void cleanup_commodity(void)
{
    struct Message *msg;

    if (broker != NULL) {
        DeleteCxObjAll(broker);
        broker = NULL;
    }

    if (broker_port != NULL) {
        /* Drain any remaining messages */
        while ((msg = GetMsg(broker_port)) != NULL) {
            ReplyMsg(msg);
        }
        DeleteMsgPort(broker_port);
        broker_port = NULL;
    }
}

/* =========================================================================
 * perform_sync - Execute a single NTP synchronization
 * ========================================================================= */

static void perform_sync(void)
{
    SyncConfig *cfg;
    ULONG ip_addr;
    UBYTE packet[NTP_PACKET_SIZE];
    ULONG ntp_secs;
    ULONG ntp_frac;
    LONG bytes;
    ULONG amiga_secs;

    /* Indicate syncing in progress */
    sync_status.status = STATUS_SYNCING;
    strcpy(sync_status.status_text, "Syncing...");
    if (window_is_open())
        window_update_status(&sync_status);

    /* Get current configuration */
    cfg = config_get();

    /* Step 1: Resolve server hostname */
    if (!network_resolve(cfg->server, &ip_addr)) {
        sync_status.status = STATUS_ERROR;
        strcpy(sync_status.status_text, "DNS failed");
        if (window_is_open())
            window_update_status(&sync_status);
        return;
    }

    /* Step 2: Build SNTP request packet */
    sntp_build_request(packet);

    /* Step 3: Send UDP packet */
    if (!network_send_udp(ip_addr, NTP_PORT, packet, NTP_PACKET_SIZE)) {
        sync_status.status = STATUS_ERROR;
        strcpy(sync_status.status_text, "Send failed");
        if (window_is_open())
            window_update_status(&sync_status);
        return;
    }

    /* Step 4: Receive response */
    bytes = network_recv_udp(packet, NTP_PACKET_SIZE, 5);
    if (bytes < NTP_PACKET_SIZE) {
        sync_status.status = STATUS_ERROR;
        strcpy(sync_status.status_text, "No response");
        if (window_is_open())
            window_update_status(&sync_status);
        return;
    }

    /* Step 5: Parse SNTP response */
    if (!sntp_parse_response(packet, &ntp_secs, &ntp_frac)) {
        sync_status.status = STATUS_ERROR;
        strcpy(sync_status.status_text, "Bad response");
        if (window_is_open())
            window_update_status(&sync_status);
        return;
    }

    /* Step 6: Convert NTP time to Amiga time */
    amiga_secs = sntp_ntp_to_amiga(ntp_secs, cfg->timezone, cfg->dst);

    /* Step 7: Set the system clock */
    if (!clock_set_system_time(amiga_secs, 0)) {
        sync_status.status = STATUS_ERROR;
        strcpy(sync_status.status_text, "Clock set failed");
        if (window_is_open())
            window_update_status(&sync_status);
        return;
    }

    /* Success: update sync status */
    sync_status.status = STATUS_OK;
    strcpy(sync_status.status_text, "Synchronized");
    sync_status.last_sync_secs = amiga_secs;
    clock_format_time(amiga_secs, sync_status.last_sync_text,
                      sizeof(sync_status.last_sync_text));
    sync_status.next_sync_secs = amiga_secs + cfg->interval;
    clock_format_time(sync_status.next_sync_secs, sync_status.next_sync_text,
                      sizeof(sync_status.next_sync_text));
    if (window_is_open())
        window_update_status(&sync_status);
}

/* =========================================================================
 * get_next_interval - Return timer interval based on last sync status
 *
 * If the last sync failed, return RETRY_INTERVAL (30s) for quick retry.
 * If the last sync succeeded, return the configured interval.
 * ========================================================================= */

static ULONG get_next_interval(void)
{
    if (sync_status.status == STATUS_OK) {
        return (ULONG)config_get()->interval;
    }
    return RETRY_INTERVAL;
}

/* =========================================================================
 * event_loop - Main commodity event loop
 * ========================================================================= */

static void event_loop(void)
{
    ULONG broker_sig = 1UL << broker_port->mp_SigBit;
    ULONG timer_sig, win_sig;
    ULONG signals;
    CxMsg *cxmsg;

    while (running) {
        timer_sig = clock_timer_signal();
        win_sig = window_signal();

        signals = Wait(broker_sig | timer_sig | win_sig | SIGBREAKF_CTRL_C);

        /* CTRL+C: exit */
        if (signals & SIGBREAKF_CTRL_C)
            break;

        /* Timer fired: sync and restart timer */
        if (signals & timer_sig) {
            /* The timer IORequest completed - acknowledge it */
            perform_sync();
            if (cx_enabled) {
                /* Use retry interval (30s) if sync failed, otherwise configured interval */
                clock_start_timer(get_next_interval());
            }
        }

        /* Commodity messages */
        if (signals & broker_sig) {
            while ((cxmsg = (CxMsg *)GetMsg(broker_port)) != NULL) {
                ULONG msg_id = CxMsgID(cxmsg);
                ULONG msg_type = CxMsgType(cxmsg);
                ReplyMsg((struct Message *)cxmsg);

                switch (msg_type) {
                    case CXM_IEVENT:
                        if (msg_id == EVT_HOTKEY) {
                            if (window_is_open())
                                window_close();
                            else
                                window_open(NULL);
                        }
                        break;

                    case CXM_COMMAND:
                        switch (msg_id) {
                            case CXCMD_DISABLE:
                                ActivateCxObj(broker, FALSE);
                                cx_enabled = FALSE;
                                clock_abort_timer();
                                break;
                            case CXCMD_ENABLE:
                                ActivateCxObj(broker, TRUE);
                                cx_enabled = TRUE;
                                perform_sync();
                                clock_start_timer(get_next_interval());
                                break;
                            case CXCMD_KILL:
                                running = FALSE;
                                break;
                            case CXCMD_UNIQUE:
                                /* Another instance tried to start */
                                if (window_is_open())
                                    window_close();
                                else
                                    window_open(NULL);
                                break;
                            case CXCMD_APPEAR:
                                window_open(NULL);
                                break;
                            case CXCMD_DISAPPEAR:
                                window_close();
                                break;
                        }
                        break;
                }
            }
        }

        /* Window events */
        if ((signals & win_sig) && window_is_open()) {
            SyncConfig *cfg = config_get();
            LONG old_interval = cfg->interval;
            BOOL sync_now = window_handle_events(cfg, &sync_status);

            /* Handle "Sync Now" button */
            if (sync_now && cx_enabled) {
                clock_abort_timer();
                perform_sync();
                clock_start_timer(get_next_interval());
            }
            /* If interval changed, restart timer */
            else if (cfg->interval != old_interval && cx_enabled) {
                clock_abort_timer();
                clock_start_timer(get_next_interval());
            }
        }
    }
}

/* =========================================================================
 * main - Entry point
 * ========================================================================= */

int main(int argc, char **argv)
{
    int result = 20;  /* RETURN_FAIL */

    memset(&sync_status, 0, sizeof(sync_status));
    strcpy(sync_status.status_text, "Starting...");
    strcpy(sync_status.last_sync_text, "Never");
    strcpy(sync_status.next_sync_text, "Pending");

    if (!open_libraries())
        goto cleanup;

    if (!config_init())
        goto cleanup;

    if (!network_init())
        goto cleanup;

    if (!clock_init())
        goto cleanup;

    if (!setup_commodity(argc, argv))
        goto cleanup;

    /* Perform initial sync */
    perform_sync();

    /* Start periodic timer: use retry interval if initial sync failed */
    if (cx_enabled) {
        clock_start_timer(get_next_interval());
    }

    /* Run event loop */
    event_loop();

    result = 0;  /* RETURN_OK */

cleanup:
    window_close();
    clock_abort_timer();
    cleanup_commodity();
    clock_cleanup();
    network_cleanup();
    config_cleanup();
    close_libraries();

    return result;
}
