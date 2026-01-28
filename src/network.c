/* network.c - BSD socket networking for SyncTime */

#include "synctime.h"

BOOL network_init(void) { return TRUE; }
void network_cleanup(void) { }
BOOL network_resolve(const char *hostname, ULONG *ip_addr) { return FALSE; }
BOOL network_send_udp(ULONG ip_addr, UWORD port, const UBYTE *data, ULONG len) { return FALSE; }
LONG network_recv_udp(UBYTE *buf, ULONG buf_size, ULONG timeout_secs) { return -1; }
