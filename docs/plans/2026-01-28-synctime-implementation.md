# SyncTime Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build an AmigaOS 3.0+ commodity that synchronizes the system clock via SNTP.

**Architecture:** Modular C program following Solitude conventions: one .c per subsystem, single shared header, static module state with init/cleanup pairs, cross-compiled with m68k-amigaos-gcc.

**Tech Stack:** AmigaOS 3.0 (v39), commodities.library, bsdsocket.library, gadtools.library, timer.device, m68k-amigaos-gcc with -noixemul.

---

### Task 1: Project Skeleton

**Files:**
- Create: `Makefile`
- Create: `version.txt`
- Create: `.gitignore`
- Create: `include/synctime.h`
- Create: `src/main.c` (stub)
- Create: `src/config.c` (stub)
- Create: `src/network.c` (stub)
- Create: `src/sntp.c` (stub)
- Create: `src/clock.c` (stub)
- Create: `src/window.c` (stub)

**Step 1: Initialize git repo**

```bash
cd /home/matjam/src/synctime
git init
```

**Step 2: Create version.txt**

```
1.0.0
```

**Step 3: Create .gitignore**

```
*.o
SyncTime
```

**Step 4: Create Makefile**

Match Solitude conventions exactly:

```makefile
# Makefile for SyncTime - Amiga NTP Clock Synchronizer
# Usage: make / make clean
# Override: make PREFIX=/opt/amiga

PREFIX ?= /opt/amiga
CC      = $(PREFIX)/bin/m68k-amigaos-gcc

VERSION := $(shell cat version.txt | tr -d '\n')
HASH    := $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
STAMP   := $(shell date '+%Y-%m-%d %H:%M')

CFLAGS  ?= -O2 -Wall -Wno-pointer-sign
CFLAGS  += '-DVERSION_STRING="$(VERSION)"' \
           '-DCOMMIT_HASH="$(HASH)"' \
           '-DBUILD_DATE="$(STAMP)"'
LDFLAGS  = -noixemul
INCLUDES = -Iinclude
LIBS     = -lamiga

SRCDIR = src
SRCS   = $(SRCDIR)/main.c \
         $(SRCDIR)/config.c \
         $(SRCDIR)/network.c \
         $(SRCDIR)/sntp.c \
         $(SRCDIR)/clock.c \
         $(SRCDIR)/window.c

OBJS = $(SRCS:.c=.o)
OUT  = SyncTime

.PHONY: all clean

all: $(OUT)

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c include/synctime.h
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(OBJS) $(OUT)
```

**Step 5: Create include/synctime.h**

Shared header with all types, constants, and prototypes. See Task 2 for full content.

**Step 6: Create source stubs**

Each .c file starts with `#include "synctime.h"` and empty init/cleanup functions that return TRUE/do nothing, so the project compiles from the start.

**Step 7: Verify build compiles**

```bash
make clean && make
```

Expected: Compiles with no errors. Executable won't do anything useful yet.

**Step 8: Commit**

```bash
git add -A
git commit -m "feat: initial project skeleton with build system"
```

---

### Task 2: Header File (include/synctime.h)

**Files:**
- Create: `include/synctime.h`

**Step 1: Write the complete header**

```c
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

#include <string.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define LIB_VERSION 39  /* AmigaOS 3.0 minimum */

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
#define DEFAULT_TIMEZONE   -8
#define DEFAULT_DST        TRUE
#define SERVER_NAME_MAX    128
#define MIN_INTERVAL       60
#define MAX_INTERVAL       86400
#define MIN_TIMEZONE       -12
#define MAX_TIMEZONE       14

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
    LONG  timezone;     /* hours offset from UTC (-12 to +14) */
    BOOL  dst;          /* daylight saving time enabled */
} SyncConfig;

typedef struct {
    int   status;              /* STATUS_* */
    ULONG last_sync_secs;      /* Amiga time of last successful sync */
    ULONG next_sync_secs;      /* Amiga time of next scheduled sync */
    char  status_text[64];     /* Human-readable status */
    char  last_sync_text[32];  /* Formatted last sync time */
    char  next_sync_text[32];  /* Formatted next sync time */
} SyncStatus;

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
void        config_set_timezone(LONG tz);
void        config_set_dst(BOOL enabled);

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
ULONG sntp_ntp_to_amiga(ULONG ntp_secs, LONG tz_offset, BOOL dst);

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

/* =========================================================================
 * window.c
 * ========================================================================= */

BOOL  window_open(struct Screen *screen);
void  window_close(void);
BOOL  window_is_open(void);
void  window_handle_events(SyncConfig *cfg, SyncStatus *st);
ULONG window_signal(void);
void  window_update_status(SyncStatus *st);

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

#endif /* SYNCTIME_H */
```

**Step 2: Commit**

```bash
git add include/synctime.h
git commit -m "feat: add shared header with all types and prototypes"
```

---

### Task 3: Configuration Module (src/config.c)

**Files:**
- Create: `src/config.c`

**Step 1: Implement config.c**

Follow Solitude's prefs.c pattern exactly: static module state, set_defaults(),
parse_line(), load from ENV:, save to both ENV: and ENVARC:, accessor functions.

Key implementation details:
- `set_defaults()`: SERVER=pool.ntp.org, INTERVAL=3600, TIMEZONE=-8, DST=1
- `parse_line()`: Parse SERVER=, INTERVAL=, TIMEZONE=, DST= with strncmp
- Integer parsing: manual digit loop (no atoi, matching Solitude pattern)
- TIMEZONE is signed: handle leading '-' in parse
- Validation on load: clamp interval to 60-86400, timezone to -12..+14
- `config_init()`: call set_defaults(), then config_load() (keeps defaults on fail)
- If no prefs file found on init, write defaults to both ENV: and ENVARC:
- `save_to_path()` helper: write key=value lines using Open/FPuts/Close
- Integer-to-string conversion: manual (matching Solitude, no sprintf)

```c
/* config.c - Configuration management for SyncTime */

#include "synctime.h"

static SyncConfig current_config;

static void set_defaults(void) { /* ... */ }
static BOOL parse_line(const char *line) { /* ... */ }
static BOOL save_to_path(const char *path) { /* ... */ }

BOOL config_init(void) {
    set_defaults();
    if (!config_load()) {
        config_save();  /* Write defaults if no prefs exist */
    }
    return TRUE;
}
void config_cleanup(void) { }
/* ... accessors ... */
```

**Step 2: Verify build**

```bash
make clean && make
```

**Step 3: Commit**

```bash
git add src/config.c
git commit -m "feat: add configuration module with ENV/ENVARC persistence"
```

---

### Task 4: Network Module (src/network.c)

**Files:**
- Create: `src/network.c`

**Step 1: Implement network.c**

This module wraps bsdsocket.library for UDP communication.

Key implementation details:
- `network_init()`: Open bsdsocket.library (version 0 - any version).
  This library is special: it's opened with OpenLibrary() but provides
  Berkeley socket API functions. Store base in SocketBase global.
- `network_cleanup()`: Close bsdsocket.library
- `network_resolve()`: Use gethostbyname() to resolve hostname to IP.
  Returns the first IPv4 address as a ULONG in network byte order.
- `network_send_udp()`: Create UDP socket (socket(AF_INET, SOCK_DGRAM, 0)),
  fill sockaddr_in with ip_addr and port, sendto() the data, close socket.
  Actually, keep socket open between send and recv. Use a static socket fd.
- `network_recv_udp()`: recv() with SO_RCVTIMEO timeout. Returns bytes read
  or -1 on error/timeout.
- Socket is created in send, used in recv, closed after recv returns.
- Timeout: use setsockopt(SO_RCVTIMEO) with 5 second timeout.

Important: bsdsocket.library on AmigaOS uses the same BSD socket API but
accessed through library calls. The includes are:
```c
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <proto/socket.h>
```

Note: With m68k-amigaos-gcc and -noixemul, bsdsocket functions are called
through the library base. The proto/socket.h header provides the stubs.

**Step 2: Verify build**

```bash
make clean && make
```

**Step 3: Commit**

```bash
git add src/network.c
git commit -m "feat: add network module wrapping bsdsocket.library UDP"
```

---

### Task 5: SNTP Module (src/sntp.c)

**Files:**
- Create: `src/sntp.c`

**Step 1: Implement sntp.c**

Pure data transformation - no I/O, no library calls (except memset/memcpy).

Key implementation details:
- `sntp_build_request()`: Zero 48 bytes. Set byte 0 to (NTP_VERSION << 3) | NTP_MODE_CLIENT = (3 << 3) | 3 = 0x1B. That's it.
- `sntp_parse_response()`: Validate response (byte 0 mode should be 4 = server). Extract transmit timestamp from bytes 40-43 (seconds, big-endian) and bytes 44-47 (fraction, big-endian). Convert from network byte order using manual shift: `(b[40] << 24) | (b[41] << 16) | (b[42] << 8) | b[43]`.
- `sntp_ntp_to_amiga()`: Subtract NTP_TO_AMIGA_EPOCH from ntp_secs. Add timezone offset: `+ (tz_offset * 3600)`. Add DST: `+ (dst ? 3600 : 0)`. Return as ULONG. Must handle the subtraction carefully since NTP seconds is larger than Amiga epoch offset (NTP time in 2026 is ~3,979,000,000 which minus 2,461,449,600 = ~1,517,550,400 which fits in ULONG).

**Step 2: Verify build**

```bash
make clean && make
```

**Step 3: Commit**

```bash
git add src/sntp.c
git commit -m "feat: add SNTP packet build/parse and epoch conversion"
```

---

### Task 6: Clock Module (src/clock.c)

**Files:**
- Create: `src/clock.c`

**Step 1: Implement clock.c**

Manages timer.device for two purposes: setting system time and periodic timer.

Key implementation details:
- Two separate timerequest structures: one for setting time, one for periodic timer.
- `clock_init()`:
  - Create a MsgPort via CreateMsgPort()
  - Create timerequest via CreateIORequest(port, sizeof(struct timerequest))
  - Open timer.device with UNIT_VBLANK: OpenDevice("timer.device", UNIT_VBLANK, (struct IORequest *)treq, 0)
  - Set TimerBase = (struct Device *)treq->tr_node.io_Device
  - Create second timerequest for periodic timer: copy first one, create separate reply port
- `clock_cleanup()`: AbortIO on pending timer, CloseDevice, DeleteIORequest, DeleteMsgPort (reverse order)
- `clock_set_system_time()`:
  - Use the set-time timerequest
  - treq->tr_node.io_Command = TR_SETSYSTIME
  - treq->tr_time.tv_secs = amiga_secs
  - treq->tr_time.tv_micro = amiga_micro
  - DoIO((struct IORequest *)treq) - synchronous
- `clock_get_system_time()`:
  - treq->tr_node.io_Command = TR_GETSYSTIME
  - DoIO()
  - Read back tv_secs and tv_micro
- `clock_format_time()`: Use dos.library DateStamp conversion. Convert Amiga seconds to DateStamp (days since Jan 1 1978, minutes, ticks). Use DateToStr() from dos.library to format.
- `clock_start_timer()`: Use the periodic timerequest. TR_ADDREQUEST with tv_secs = seconds. SendIO() (async). On completion, the port's signal bit fires.
- `clock_abort_timer()`: AbortIO() + WaitIO() on periodic timer request.
- `clock_timer_signal()`: Return 1L << periodic_port->mp_SigBit.

**Step 2: Verify build**

```bash
make clean && make
```

**Step 3: Commit**

```bash
git add src/clock.c
git commit -m "feat: add clock module for system time and periodic timer"
```

---

### Task 7: Window Module (src/window.c)

**Files:**
- Create: `src/window.c`

**Step 1: Implement window.c**

GadTools window on default public screen for configuration and status display.

Key implementation details:
- Static state: Window pointer, GadgetList, VisualInfo, individual gadget pointers
- Gadget IDs: GID_STATUS=0, GID_LAST_SYNC=1, GID_NEXT_SYNC=2, GID_SERVER=3, GID_INTERVAL=4, GID_TIMEZONE=5, GID_DST=6, GID_SAVE=7, GID_HIDE=8
- `window_open()`:
  - Lock default public screen: LockPubScreen(NULL)
  - Get VisualInfo: GetVisualInfo(screen, TAG_DONE)
  - Create gadget list using CreateContext(&glist) + CreateGadget() chain
  - Timezone cycle: array of 27 strings "UTC-12" through "UTC+14"
  - Open window with OpenWindowTags:
    - WA_Title = "SyncTime"
    - WA_PubScreen = locked screen
    - WA_Gadgets = glist
    - WA_IDCMP = IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | BUTTONIDCMP | STRINGIDCMP | CYCLEIDCMP | CHECKBOXIDCMP
    - WA_Width ~= 340, WA_Height ~= 200 (font-relative using screen font)
    - WA_DragBar, WA_DepthGadget, WA_CloseGadget, WA_Activate
  - GT_RefreshWindow() after open
  - UnlockPubScreen()
- `window_close()`: CloseWindow, FreeGadgets, FreeVisualInfo
- `window_handle_events()`:
  - Process messages via GT_GetIMsg/GT_ReplyIMsg
  - IDCMP_CLOSEWINDOW -> window_close()
  - IDCMP_GADGETUP:
    - GID_SAVE: read gadget values, update config, config_save(), restart timer
    - GID_HIDE: window_close()
    - GID_TIMEZONE: update local cycle index
    - GID_DST: update local checkbox state
  - IDCMP_REFRESHWINDOW: GT_BeginRefresh/GT_EndRefresh
- `window_update_status()`: Use GT_SetGadgetAttrs to update the TEXT_KIND gadgets with status text
- `window_signal()`: Return 1L << window->UserPort->mp_SigBit (or 0 if window not open)

**Step 2: Verify build**

```bash
make clean && make
```

**Step 3: Commit**

```bash
git add src/window.c
git commit -m "feat: add GadTools config/status window"
```

---

### Task 8: Main Module (src/main.c)

**Files:**
- Create: `src/main.c`

**Step 1: Implement main.c**

Commodity setup, event loop with three signal sources.

Key implementation details:
- `LONG __stack = 16384;` (request 16KB stack)
- Version tag: `"\0$VER: SyncTime " VERSION_STRING " (" BUILD_DATE ") " COMMIT_HASH`
- Library bases: IntuitionBase, GfxBase, GadToolsBase, CxBase, UtilityBase (all v39). SocketBase and TimerBase set by their modules.
- `open_libraries()`: Open intuition, graphics, gadtools, commodities, utility (all v39). Return FALSE if any fail.
- `close_libraries()`: Close in reverse order (NULL-safe).
- Commodity setup:
  - Create broker message port: CreateMsgPort()
  - Parse tooltype args: ArgArrayInit() for Workbench startup support
  - Build CxBroker with NewBroker struct:
    - nb_Version = NB_VERSION
    - nb_Name = CX_NAME
    - nb_Title = CX_TITLE
    - nb_Descr = CX_DESCR
    - nb_Unique = NBU_UNIQUE | NBU_NOTIFY
    - nb_Port = broker_port
    - nb_Pri = CX_DEFAULT_PRI (overridden by tooltype)
  - Create hotkey filter: CxFilter(popkey_string)
  - Attach CxSender to filter -> sends EVT_HOTKEY to broker_port
  - AttachCxObj(broker, filter)
  - ActivateCxObj(broker, TRUE)
- `perform_sync()`:
  - Set status to STATUS_SYNCING
  - Resolve server hostname via network_resolve()
  - Build SNTP request via sntp_build_request()
  - Send via network_send_udp()
  - Receive via network_recv_udp() with 5s timeout
  - Parse via sntp_parse_response()
  - Convert via sntp_ntp_to_amiga() with current timezone/dst
  - Set clock via clock_set_system_time()
  - Update status to STATUS_OK with formatted times
  - On any failure: set STATUS_ERROR with error description
  - Restart periodic timer for next sync
- Event loop:
  - Build signal mask: broker_sig | timer_sig | window_sig | SIGBREAKF_CTRL_C
  - Wait(mask)
  - If SIGBREAKF_CTRL_C -> exit
  - If timer_sig -> perform_sync(), restart timer
  - If broker_sig -> process CxMsg:
    - CXM_IEVENT: if id == EVT_HOTKEY -> toggle window
    - CXM_COMMAND:
      - CXCMD_DISABLE -> ActivateCxObj(broker, FALSE), abort timer
      - CXCMD_ENABLE -> ActivateCxObj(broker, TRUE), restart timer
      - CXCMD_KILL -> exit
      - CXCMD_UNIQUE -> toggle window (another instance tried to start)
      - CXCMD_APPEAR -> open window
      - CXCMD_DISAPPEAR -> close window
  - If window_sig -> window_handle_events()
    - After handling: check if config changed, restart timer if needed
- main():
  - open_libraries()
  - config_init()
  - network_init()
  - clock_init()
  - Setup commodity broker
  - Perform initial sync
  - Start periodic timer
  - Enter event loop
  - Cleanup: window_close(), abort timer, DeleteCxObjAll(broker), drain & delete broker port, clock_cleanup(), network_cleanup(), config_cleanup(), close_libraries()
  - Return 0 on success, 20 on failure

**Step 2: Verify build**

```bash
make clean && make
```

Expected: Clean compile, linked binary.

**Step 3: Commit**

```bash
git add src/main.c
git commit -m "feat: add main module with commodity event loop and SNTP sync"
```

---

### Task 9: Final Integration & Polish

**Step 1: Review all modules compile cleanly**

```bash
make clean && make 2>&1
```

Fix any warnings or errors.

**Step 2: Verify commodity tooltypes work**

Ensure ArgArrayInit/ArgArrayDone are used so WBStartup tooltypes
(CX_PRIORITY, CX_POPUP, CX_POPKEY) are parsed correctly.

**Step 3: Verify cleanup is complete**

Walk through every resource opened and confirm it's freed:
- All libraries closed
- All ports deleted
- All IO requests deleted
- Timer device closed
- Socket closed
- Window closed, gadgets freed, visual info freed
- CxBroker deleted, broker port drained and deleted

**Step 4: Commit**

```bash
git add -A
git commit -m "chore: final integration review and polish"
```
