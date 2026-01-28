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
 *
 * Logs each step to the scrollable log and updates status on completion.
 * ========================================================================= */

/* Re-entrancy guard */
static BOOL sync_in_progress = FALSE;

/* Helper to update main status field */
static void set_status(int status_code, const char *text)
{
    sync_status.status = status_code;
    strcpy(sync_status.status_text, text);
    if (window_is_open())
        window_update_status(&sync_status);
}

/* Helper to format IP address into buffer */
static void format_ip(ULONG ip_addr, char *buf)
{
    UBYTE *ip = (UBYTE *)&ip_addr;
    int i, val, pos = 0;

    for (i = 0; i < 4; i++) {
        val = ip[i];
        if (val >= 100) { buf[pos++] = '0' + (val / 100); val %= 100; }
        if (val >= 10 || ip[i] >= 100) { buf[pos++] = '0' + (val / 10); val %= 10; }
        buf[pos++] = '0' + val;
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

static void perform_sync(void)
{
    SyncConfig *cfg;
    ULONG ip_addr;
    UBYTE packet[NTP_PACKET_SIZE];
    ULONG ntp_secs;
    ULONG ntp_frac;
    LONG bytes;
    ULONG amiga_secs;
    char msg[64];

    /* Prevent re-entrancy */
    if (sync_in_progress) {
        window_log("Sync already in progress, skipping");
        return;
    }
    sync_in_progress = TRUE;

    /* Get current configuration */
    cfg = config_get();

    /* Step 1: Resolve server hostname */
    set_status(STATUS_SYNCING, "Syncing...");
    strcpy(msg, "Resolving ");
    {
        int i;
        for (i = 0; i < 40 && cfg->server[i]; i++)
            msg[10 + i] = cfg->server[i];
        msg[10 + i] = '\0';
    }
    window_log(msg);

    if (!network_resolve(cfg->server, &ip_addr)) {
        window_log("ERROR: DNS lookup failed");
        set_status(STATUS_ERROR, "DNS failed");
        sync_in_progress = FALSE;
        return;
    }

    /* Log resolved IP */
    strcpy(msg, "Resolved to ");
    format_ip(ip_addr, msg + 12);
    window_log(msg);

    /* Step 2: Build and send SNTP request packet */
    window_log("Sending NTP request to port 123...");
    sntp_build_request(packet);
    if (!network_send_udp(ip_addr, NTP_PORT, packet, NTP_PACKET_SIZE)) {
        window_log("ERROR: Failed to send UDP packet");
        set_status(STATUS_ERROR, "Send failed");
        sync_in_progress = FALSE;
        return;
    }
    window_log("Request sent, waiting for response...");

    /* Step 3: Wait for response (5 second timeout) */
    bytes = network_recv_udp(packet, NTP_PACKET_SIZE, 5);
    if (bytes < 0) {
        window_log("ERROR: Timeout waiting for response");
        set_status(STATUS_ERROR, "Timeout");
        sync_in_progress = FALSE;
        return;
    }
    if (bytes < NTP_PACKET_SIZE) {
        window_log("ERROR: Response too short");
        set_status(STATUS_ERROR, "Bad response");
        sync_in_progress = FALSE;
        return;
    }
    window_log("Received 48-byte response");

    /* Step 4: Parse SNTP response */
    window_log("Parsing NTP response...");
    if (!sntp_parse_response(packet, &ntp_secs, &ntp_frac)) {
        window_log("ERROR: Invalid NTP packet format");
        set_status(STATUS_ERROR, "Invalid response");
        sync_in_progress = FALSE;
        return;
    }
    window_log("Response valid, extracting time...");

    /* Step 5: Convert NTP time to Amiga time */
    amiga_secs = sntp_ntp_to_amiga(ntp_secs, cfg->timezone, cfg->dst);

    /* Step 6: Set the system clock */
    window_log("Setting system clock...");
    if (!clock_set_system_time(amiga_secs, 0)) {
        window_log("ERROR: Failed to set system time");
        set_status(STATUS_ERROR, "Clock set failed");
        sync_in_progress = FALSE;
        return;
    }

    /* Success! */
    window_log("Clock synchronized successfully!");

    /* Update sync status with timestamps */
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

    sync_in_progress = FALSE;
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
        if ((signals & timer_sig) && clock_check_timer()) {
            /* Timer actually completed - acknowledged by clock_check_timer() */
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
