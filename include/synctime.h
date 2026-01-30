#ifndef SYNCTIME_H
#define SYNCTIME_H

/* AmigaOS system includes */
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/datetime.h>
#include <intuition/intuition.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <libraries/commodities.h>
#include <devices/timer.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/commodities.h>
#include <proto/timer.h>
#include <proto/utility.h>
#include <clib/alib_protos.h>

/* Reaction includes */
#include <classes/window.h>
#include <gadgets/layout.h>
#include <gadgets/button.h>
#include <gadgets/string.h>
#include <gadgets/integer.h>
#include <gadgets/chooser.h>
#include <gadgets/listbrowser.h>
#include <images/label.h>

#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>
#include <proto/string.h>
#include <proto/integer.h>
#include <proto/chooser.h>
#include <proto/listbrowser.h>
#include <proto/label.h>

#include <string.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define LIB_VERSION 44  /* AmigaOS 3.2 minimum for Reaction */

/* NTP constants */
#define NTP_PORT           123
#define NTP_PACKET_SIZE    48
#define NTP_VERSION        3
#define NTP_MODE_CLIENT    3

/* Epoch offset: seconds from Jan 1 1900 (NTP) to Jan 1 1978 (Amiga) */
#define NTP_TO_AMIGA_EPOCH 2461449600UL

/* Configuration defaults */
#define DEFAULT_SERVER     "pool.ntp.org"
#define DEFAULT_INTERVAL   3600
#define DEFAULT_TIMEZONE   "America/Los_Angeles"
#define SERVER_NAME_MAX    128
#define MIN_INTERVAL       60
#define MAX_INTERVAL       86400
#define RETRY_INTERVAL     30      /* Seconds between retries on failure */
#define INITIAL_SYNC_DELAY 60      /* Seconds to wait before first sync */

/* Prefs file paths */
#define PREFS_ENV_PATH     "ENV:SyncTime.prefs"
#define PREFS_ENVARC_PATH  "ENVARC:SyncTime.prefs"

/* Commodity */
#define CX_NAME            "SyncTime"
#define CX_TITLE           "SyncTime - NTP Clock Synchronizer"
#define CX_DESCR           "Synchronizes system clock via SNTP"
#define CX_DEFAULT_POPKEY  "ctrl alt t"
#define CX_DEFAULT_PRI     0

/* Sync status */
#define STATUS_IDLE        0
#define STATUS_SYNCING     1
#define STATUS_OK          2
#define STATUS_ERROR       3

/* =========================================================================
 * Types
 * ========================================================================= */

typedef struct {
    char  server[SERVER_NAME_MAX];
    LONG  interval;     /* seconds between syncs */
    char  tz_name[48];  /* IANA timezone name, e.g. "America/Los_Angeles" */
} SyncConfig;

typedef struct {
    int   status;              /* STATUS_* */
    ULONG last_sync_secs;      /* Amiga time of last successful sync */
    ULONG next_sync_secs;      /* Amiga time of next scheduled sync */
    char  status_text[64];     /* Human-readable status */
    char  last_sync_text[32];  /* Formatted last sync time */
    char  next_sync_text[32];  /* Formatted next sync time */
} SyncStatus;

/* Timezone entry from generated tz_table.c */
typedef struct {
    const char *name;       /* Full name: "America/Los_Angeles" */
    const char *region;     /* Region: "America" */
    const char *city;       /* City: "Los_Angeles" */
    WORD std_offset_mins;   /* Standard offset from UTC in minutes */
    WORD dst_offset_mins;   /* Additional DST offset (0 if no DST) */
    UBYTE dst_start_month;  /* 1-12, 0 = no DST */
    UBYTE dst_start_week;   /* 1-5, which occurrence of dow */
    UBYTE dst_start_dow;    /* 0=Sun, 1=Mon, ..., 6=Sat */
    UBYTE dst_start_hour;   /* Local hour of transition */
    UBYTE dst_end_month;
    UBYTE dst_end_week;
    UBYTE dst_end_dow;
    UBYTE dst_end_hour;
} TZEntry;

/* =========================================================================
 * config.c
 * ========================================================================= */

BOOL        config_init(void);
void        config_cleanup(void);
BOOL        config_load(void);
BOOL        config_save(void);
SyncConfig *config_get(void);
void        config_set_server(const char *server);
void        config_set_interval(LONG interval);
void        config_set_tz_name(const char *name);

/* =========================================================================
 * network.c
 * ========================================================================= */

BOOL network_init(void);
void network_cleanup(void);
BOOL network_resolve(const char *hostname, ULONG *ip_addr);
BOOL network_send_udp(ULONG ip_addr, UWORD port,
                      const UBYTE *data, ULONG len);
LONG network_recv_udp(UBYTE *buf, ULONG buf_size, ULONG timeout_secs);

/* =========================================================================
 * sntp.c
 * ========================================================================= */

void sntp_build_request(UBYTE *packet);
BOOL sntp_parse_response(const UBYTE *packet, ULONG *ntp_secs,
                         ULONG *ntp_frac);
ULONG sntp_ntp_to_amiga(ULONG ntp_secs, const TZEntry *tz);

/* =========================================================================
 * tz.c - Timezone database functions
 * ========================================================================= */

extern const TZEntry tz_table[];
extern const ULONG tz_table_count;

const TZEntry *tz_find_by_name(const char *name);
const char   **tz_get_regions(ULONG *count);
const TZEntry **tz_get_cities_for_region(const char *region, ULONG *count);
BOOL           tz_is_dst_active(const TZEntry *tz, ULONG utc_secs);
LONG           tz_get_offset_mins(const TZEntry *tz, ULONG utc_secs);
BOOL           tz_set_env(const TZEntry *tz);

/* =========================================================================
 * clock.c
 * ========================================================================= */

BOOL clock_init(void);
void clock_cleanup(void);
BOOL clock_set_system_time(ULONG amiga_secs, ULONG amiga_micro);
BOOL clock_get_system_time(ULONG *amiga_secs, ULONG *amiga_micro);
void clock_format_time(ULONG amiga_secs, char *buf, ULONG buf_size);

/* Timer for periodic sync */
BOOL  clock_start_timer(ULONG seconds);
void  clock_abort_timer(void);
ULONG clock_timer_signal(void);
BOOL  clock_check_timer(void);  /* Check if timer fired and acknowledge it */

/* =========================================================================
 * window.c
 * ========================================================================= */

BOOL  window_open(struct Screen *screen);
void  window_close(void);
BOOL  window_is_open(void);
BOOL  window_handle_events(SyncConfig *cfg, SyncStatus *st);  /* Returns TRUE if "Sync Now" requested */
ULONG window_signal(void);
void  window_update_status(SyncStatus *st);
void  window_log(const char *message);  /* Add entry to scrollable log */

/* =========================================================================
 * Globals (main.c)
 * ========================================================================= */

extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
extern struct Library       *GadToolsBase;
extern struct Library       *CxBase;
extern struct Library       *UtilityBase;
extern struct Library       *SocketBase;
extern struct Device        *TimerBase;

/* Reaction class library bases */
extern struct Library       *WindowBase;
extern struct Library       *LayoutBase;
extern struct Library       *ButtonBase;
extern struct Library       *StringBase;
extern struct Library       *IntegerBase;
extern struct Library       *ChooserBase;
extern struct Library       *ListBrowserBase;
extern struct Library       *LabelBase;

#endif /* SYNCTIME_H */
