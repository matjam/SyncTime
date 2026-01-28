# SyncTime

An AmigaOS 3.0+ commodity that synchronizes the system clock via SNTP (Simple Network Time Protocol).

## Requirements

- AmigaOS 3.0 (Kickstart 39) or later
- A TCP/IP stack with bsdsocket.library (Roadshow, AmiTCP, Miami, etc.)
- Network connectivity to an NTP server

## Installation

1. Copy `SyncTime` to your `SYS:WBStartup/` or `SYS:Tools/Commodities/` drawer
2. Optionally add an icon with tooltypes (see below)
3. Run SyncTime or reboot if placed in WBStartup

## Configuration

Configuration is stored in `ENV:SyncTime.prefs` and `ENVARC:SyncTime.prefs`.

Default settings:
- Server: `pool.ntp.org`
- Interval: `3600` seconds (1 hour)
- Timezone: `-8` (UTC-8, Pacific Time)
- DST: Enabled

You can modify settings via the configuration window (accessible through Exchange or the hotkey).

## Tooltypes

SyncTime supports the standard Commodities Exchange tooltypes:

| Tooltype | Default | Description |
|----------|---------|-------------|
| `CX_PRIORITY` | `0` | Commodity priority (-128 to 127). Higher priority commodities receive input events first. |
| `CX_POPUP` | `NO` | Open configuration window on startup. Set to `YES` to show window immediately. |
| `CX_POPKEY` | `ctrl alt t` | Hotkey to toggle the configuration window. |

### Example Icon Tooltypes

```
CX_PRIORITY=0
CX_POPUP=NO
CX_POPKEY=ctrl alt t
```

### Hotkey Format

The `CX_POPKEY` uses standard Commodities hotkey syntax:
- Qualifiers: `ctrl`, `alt`, `shift`, `lalt`, `ralt`, `lshift`, `rshift`, `lcommand`, `rcommand`
- Keys: Any key name (`a`-`z`, `0`-`9`, `f1`-`f10`, `help`, `del`, etc.)

Examples:
- `ctrl alt t` - Control + Alt + T
- `lcommand help` - Left Amiga + Help
- `ctrl shift s` - Control + Shift + S

## Usage

### From Workbench

Double-click the SyncTime icon. The commodity will start in the background and perform an initial time sync.

### From CLI

```
SyncTime
```

To run with tooltypes from CLI, you can use:
```
SyncTime CX_POPUP=YES
```

### Exchange Control

Once running, SyncTime appears in the Commodities Exchange:
- **Show**: Opens the configuration window
- **Hide**: Closes the configuration window
- **Remove**: Quits SyncTime

### Configuration Window

The window displays:
- **Status**: Current sync state (Idle, Syncing, Synchronized, or error messages)
- **Last sync**: Time of the last successful synchronization
- **Next sync**: Scheduled time for the next sync

Editable settings:
- **Server**: NTP server hostname
- **Interval**: Seconds between sync attempts (60-86400)
- **Timezone**: UTC offset (-12 to +14)
- **DST**: Daylight Saving Time adjustment (+1 hour when enabled)

Buttons:
- **Sync Now**: Immediately perform a time synchronization
- **Save**: Apply changes and write to ENV:/ENVARC:
- **Hide**: Close the configuration window

### Retry Behavior

SyncTime syncs immediately on startup. If the sync fails (network unavailable, DNS error, etc.), it will automatically retry every 30 seconds until successful. Once a sync succeeds, it switches to the configured interval (default: 1 hour).

## Signals

- **CTRL+C**: Cleanly exits SyncTime

## Troubleshooting

### "DNS failed"
- Verify your TCP/IP stack is running
- Check that DNS is configured correctly
- Try using an IP address instead of hostname

### "Send failed"
- Check network connectivity
- Verify UDP port 123 is not blocked

### "No response"
- The NTP server may be unreachable
- Try a different server (e.g., `time.nist.gov`, `time.google.com`)

### "Bad response"
- The server sent an invalid NTP packet
- Try a different NTP server

### "Clock set failed"
- This shouldn't happen under normal circumstances
- Check that timer.device is functioning

## Building from Source

Requires the m68k-amigaos-gcc cross-compiler.

```
make clean
make
```

The binary will be created as `SyncTime` in the project root.

## License

This software is provided as-is for the Amiga community.

## Version

See the embedded version string: `$VER: SyncTime 1.0.0`
