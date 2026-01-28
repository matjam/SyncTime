/* window.c - GadTools configuration/status window for SyncTime */

#include "synctime.h"

BOOL window_open(struct Screen *screen) { return FALSE; }
void window_close(void) { }
BOOL window_is_open(void) { return FALSE; }
void window_handle_events(SyncConfig *cfg, SyncStatus *st) { }
ULONG window_signal(void) { return 0; }
void window_update_status(SyncStatus *st) { }
