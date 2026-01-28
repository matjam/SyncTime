/* sntp.c - SNTP protocol for SyncTime */

#include "synctime.h"

void sntp_build_request(UBYTE *packet) { }
BOOL sntp_parse_response(const UBYTE *packet, ULONG *ntp_secs, ULONG *ntp_frac) { return FALSE; }
ULONG sntp_ntp_to_amiga(ULONG ntp_secs, LONG tz_offset, BOOL dst) { return 0; }
