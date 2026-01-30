# SyncTime

An AmigaOS 3.2+ commodity that synchronizes the system clock via SNTP with full timezone and DST support.

## Description

SyncTime is a commodity that synchronizes your Amiga's clock with internet
time servers using the SNTP protocol. It features a Reaction-based GUI for
configuration and includes a comprehensive timezone database.

You may ask why I developed this while there are many other tools to do the
same thing. And that's fair. Basically, it was an exercise for me to see
how to do it, and to see if Claude could help me with it. And I also was
kind of unhappy with the choices; they were either too simple or didn't
make the configuration easy enough. I wanted it to be a drag and drop thing
with no magic tooltip configuration, just find your region and it works.

Please drop me a mail if you end up using this, and like it!

## Features

- SNTP time synchronization from configurable NTP servers
- Full IANA timezone database with 400+ locations
- Region/city timezone picker with automatic DST handling
- Sets TZ and TZONE environment variables
- Reaction-based GUI for easy configuration
- Scrollable activity log
- Standard commodity with Exchange integration
- Runs quietly in the background

## Requirements

- AmigaOS 3.2 or later
- TCP/IP stack with bsdsocket.library (Roadshow, AmiTCP, Miami, etc.)
- Network connectivity

## Installation

Copy SyncTime to SYS:WBStartup/ or SYS:Tools/Commodities/.

Configuration is stored in ENVARC:SyncTime.prefs.

## Usage

SyncTime runs as a standard Amiga commodity. Use Exchange to show/hide
the window, or press the hotkey (default: ctrl alt s).

From the configuration window you can:

- View sync status and last/next sync times
- Configure the NTP server (default: pool.ntp.org)
- Set the sync interval (900-86400 seconds)
- Select your timezone by region and city
- View the activity log
- Trigger an immediate sync

## Tooltypes

- **CX_PRIORITY=n** - Commodity priority (default: 0)
- **CX_POPUP=YES|NO** - Open window on startup (default: NO)
- **CX_POPKEY=key** - Hotkey to toggle window (default: ctrl alt s)
- **DONOTWAIT** - Workbench won't wait for exit (recommended for WBStartup)

## History

- **1.0.3** - Retry sync every 1 second at startup until first success; gracefully handle network not ready
- **1.0.2** - Delay initial sync by 60 seconds to allow network stack to start
- **1.0.1** - Build system improvements
- **1.0.0** - Initial release with SNTP, Reaction GUI, IANA timezone database

## Building

Requires m68k-amigaos-gcc cross-compiler and Python 3.

```
make clean && make
```

## License

MIT License. See LICENSE file.

## Author

Nathan Ollerenshaw <chrome@stupendous.net>

https://github.com/matjam/synctime
