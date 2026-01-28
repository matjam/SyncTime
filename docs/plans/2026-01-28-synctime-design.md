# SyncTime Design

## Overview

SyncTime is an AmigaOS 3.0+ commodity that periodically synchronizes the
system clock via SNTP (RFC 4330). It runs silently in the background,
appearing in the Exchange commodity list. When activated through Exchange
(or via hotkey), it opens a configuration/status window.

**Target:** AmigaOS 3.0 (Kickstart 39+)
**Network:** bsdsocket.library (compatible with Roadshow, AmiTCP, Miami)
**Protocol:** SNTP (Simple Network Time Protocol) over UDP port 123

## Defaults

- Server: pool.ntp.org
- Sync interval: 3600 seconds
- Timezone: UTC-8
- DST: enabled

## Module Breakdown

| File | Responsibility |
|------|---------------|
| `src/main.c` | Entry point, commodity setup, event loop, signal dispatch |
| `src/sntp.c` | SNTP packet build/parse, NTP-to-Amiga epoch conversion |
| `src/network.c` | bsdsocket.library open/close, DNS lookup, UDP send/recv |
| `src/config.c` | Load/save prefs from ENV:/ENVARC:, defaults, validation |
| `src/clock.c` | timer.device open/close, read/set system time |
| `src/window.c` | GadTools config/status window |
| `include/synctime.h` | Shared types, prototypes, constants |

## SNTP Protocol

1. Build 48-byte NTP request packet: version=3, mode=3 (client), rest zeroed
2. Send via UDP to server:123
3. Receive 48-byte response
4. Extract server transmit timestamp (bytes 40-47, 32-bit seconds + 32-bit fraction)
5. Apply timezone: `utc_seconds + (timezone * 3600) + (dst ? 3600 : 0)`
6. Convert NTP epoch (Jan 1 1900) to Amiga epoch (Jan 1 1978): subtract 2,461,449,600
7. Set system clock via timer.device TR_SETSYSTIME

## Event Loop

The main loop uses `Wait()` on three signal sources simultaneously:

| Signal | Source | Purpose |
|--------|--------|---------|
| Commodity port | commodities.library | Enable/disable/show/hide from Exchange |
| Window port | intuition.library | Gadget events when config window is open |
| Timer signal | timer.device | Fires every N seconds to trigger sync |

No use of `Delay()` -- all timing via timer.device IORequests so commodity
messages are never blocked.

DNS resolution uses `gethostbyname()` from bsdsocket.library before each
sync to handle pool rotation.

## Configuration

**Files:** `ENV:SyncTime.prefs` and `ENVARC:SyncTime.prefs`

**Format** (key=value text, matching Solitude conventions):

```
SERVER=pool.ntp.org
INTERVAL=3600
TIMEZONE=-8
DST=1
```

**Startup:**
1. Try to load `ENV:SyncTime.prefs`
2. If not found, write defaults to both `ENV:` and `ENVARC:`
3. Validate: interval clamped to 60-86400, timezone clamped to -12 to +14

**Save from window:** Writes both `ENV:` and `ENVARC:`, takes effect
immediately (next sync uses new settings, timer restarted with new interval).

## Config/Status Window

Opened via Exchange CX_POPUP or hotkey (default: ctrl alt t). GadTools
window on the default public screen. Closing hides the window; commodity
continues running.

```
+--[ SyncTime ]---------------------------+
|                                          |
|  Status:  Synchronized                   |
|  Last sync:  28-Jan-2026 10:32:15        |
|  Next sync:  28-Jan-2026 11:32:15        |
|                                          |
|  Server:   [pool.ntp.org___________]     |
|  Interval: [3600____] seconds            |
|  Timezone: [ UTC-8        |v]            |
|  DST:      [x]                           |
|                                          |
|       [ Save ]          [ Hide ]         |
+------------------------------------------+
```

**Gadgets:**
- 3x TEXT_KIND: status, last sync, next sync (read-only)
- STRING_KIND: server hostname
- INTEGER_KIND: sync interval in seconds
- CYCLE_KIND: timezone (UTC-12 through UTC+14)
- CHECKBOX_KIND: DST on/off
- 2x BUTTON_KIND: Save, Hide

## Commodity

- CX_NAME: SyncTime
- CX_TITLE: SyncTime - NTP Clock Synchronizer
- CX_DESCR: Synchronizes system clock via SNTP
- CX_PRIORITY: 0 (default)
- CX_POPUP: NO (start hidden)
- CX_POPKEY: ctrl alt t

Supports standard commodity messages: CXCMD_DISABLE, CXCMD_ENABLE,
CXCMD_KILL, CXCMD_UNIQUE, CXCMD_APPEAR, CXCMD_DISAPPEAR.

## Libraries Required (all opened at runtime)

- commodities.library v39+
- bsdsocket.library (no minimum version)
- intuition.library v39+
- gadtools.library v39+
- graphics.library v39+
- dos.library v39+
- utility.library v39+

## Build System

Cross-compiled with `m68k-amigaos-gcc`:

- CFLAGS: `-O2 -Wall -Wno-pointer-sign`
- LDFLAGS: `-noixemul`
- LIBS: `-lamiga`
- Version from `version.txt`, git hash and build date via -D flags
- `$VER:` string embedded for AmigaOS Version command

## Clock Setting

AmigaOS epoch: Jan 1, 1978 00:00:00
NTP epoch: Jan 1, 1900 00:00:00
Offset: 2,461,449,600 seconds (NTP - Amiga)

To set the clock:
1. Open timer.device (UNIT_VBLANK)
2. Build timerequest with TR_SETSYSTIME
3. Set tv_secs to converted Amiga time, tv_micro to fractional part
4. DoIO() to set immediately

The periodic sync timer uses a separate timerequest on the same
timer.device unit, sent with SendIO() and waited on via signal.
