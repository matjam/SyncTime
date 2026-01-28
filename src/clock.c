/* clock.c - System clock and timer management for SyncTime */

#include "synctime.h"

BOOL clock_init(void) { return TRUE; }
void clock_cleanup(void) { }
BOOL clock_set_system_time(ULONG amiga_secs, ULONG amiga_micro) { return FALSE; }
BOOL clock_get_system_time(ULONG *amiga_secs, ULONG *amiga_micro) { return FALSE; }
void clock_format_time(ULONG amiga_secs, char *buf, ULONG buf_size) { }
BOOL clock_start_timer(ULONG seconds) { return FALSE; }
void clock_abort_timer(void) { }
ULONG clock_timer_signal(void) { return 0; }
