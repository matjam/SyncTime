/* config.c - Configuration management for SyncTime */

#include "synctime.h"

static SyncConfig current_config;

BOOL config_init(void) { return TRUE; }
void config_cleanup(void) { }
BOOL config_load(void) { return FALSE; }
BOOL config_save(void) { return FALSE; }
SyncConfig *config_get(void) { return &current_config; }
void config_set_server(const char *server) { }
void config_set_interval(LONG interval) { }
void config_set_timezone(LONG tz) { }
void config_set_dst(BOOL enabled) { }
