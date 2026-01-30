/* network.c - BSD socket networking for SyncTime
 *
 * Wraps bsdsocket.library for UDP communication:
 * DNS resolve, send, receive with timeout.
 */

#include "synctime.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <proto/socket.h>

/* Static state: current socket file descriptor, -1 when not open */
static LONG sock_fd = -1;

/*
 * network_init - Initialize network subsystem
 *
 * Just initializes state. The actual library opening is deferred
 * to when we need it, so startup doesn't fail if network isn't ready.
 *
 * Always returns TRUE.
 */
BOOL network_init(void)
{
    sock_fd = -1;
    SocketBase = NULL;
    return TRUE;
}

/*
 * network_ensure_open - Open bsdsocket.library if not already open
 *
 * Opens any version of bsdsocket.library (version 0) since
 * different TCP/IP stacks (Roadshow, AmiTCP, Miami) may vary.
 *
 * Returns TRUE if library is open, FALSE if it cannot be opened.
 */
static BOOL network_ensure_open(void)
{
    if (SocketBase != NULL)
        return TRUE;

    SocketBase = OpenLibrary("bsdsocket.library", 0);
    return (SocketBase != NULL);
}

/*
 * network_cleanup - Close socket and bsdsocket.library
 *
 * Closes any open socket and releases bsdsocket.library.
 */
void network_cleanup(void)
{
    if (sock_fd >= 0) {
        CloseSocket(sock_fd);
        sock_fd = -1;
    }

    if (SocketBase) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

/*
 * network_resolve - Resolve hostname to IPv4 address
 *
 * Uses gethostbyname() from bsdsocket.library to resolve
 * the given hostname. The result is in network byte order.
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL network_resolve(const char *hostname, ULONG *ip_addr)
{
    struct hostent *h;

    if (!network_ensure_open())
        return FALSE;

    h = gethostbyname((STRPTR)hostname);
    if (h == NULL)
        return FALSE;

    memcpy(ip_addr, h->h_addr_list[0], sizeof(ULONG));
    return TRUE;
}

/*
 * network_send_udp - Send a UDP packet
 *
 * Creates a new UDP socket (closing any previous one), sets a
 * 5-second receive timeout via SO_RCVTIMEO, builds the destination
 * address, and sends the data.
 *
 * The socket is kept open after a successful send so that
 * network_recv_udp() can receive the reply.
 *
 * 68000 is big-endian, same as network byte order, so no
 * byte swapping is needed for port or address values.
 *
 * Returns TRUE on success, FALSE on failure.
 */
BOOL network_send_udp(ULONG ip_addr, UWORD port,
                      const UBYTE *data, ULONG len)
{
    struct timeval tv;
    struct sockaddr_in dest;
    LONG result;

    if (!network_ensure_open())
        return FALSE;

    /* Close any previously open socket */
    if (sock_fd >= 0) {
        CloseSocket(sock_fd);
        sock_fd = -1;
    }

    /* Create UDP socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
        return FALSE;

    /* Set receive timeout to 5 seconds */
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Build destination address */
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);    /* Convert to network byte order */
    dest.sin_addr.s_addr = ip_addr; /* already in network byte order */

    /* Send the packet */
    result = sendto(sock_fd, (UBYTE *)data, len, 0,
                    (struct sockaddr *)&dest, sizeof(dest));
    if (result < 0 || (ULONG)result != len) {
        CloseSocket(sock_fd);
        sock_fd = -1;
        return FALSE;
    }

    return TRUE;
}

/*
 * network_recv_udp - Receive a UDP packet with timeout
 *
 * Receives data on the currently open socket (opened by
 * network_send_udp). Uses WaitSelect() for timeout since
 * SO_RCVTIMEO is not supported by all Amiga TCP/IP stacks.
 *
 * The socket is closed after receive (success or failure).
 *
 * Returns number of bytes received, or -1 on error/timeout.
 */
LONG network_recv_udp(UBYTE *buf, ULONG buf_size, ULONG timeout_secs)
{
    fd_set read_fds;
    struct timeval tv;
    ULONG sigmask = 0;  /* No additional signals to wait on */
    LONG select_result;
    LONG result;

    if (sock_fd < 0)
        return -1;

    /* Set up the fd_set for select/WaitSelect */
    FD_ZERO(&read_fds);
    FD_SET(sock_fd, &read_fds);

    /* Set timeout */
    tv.tv_sec = timeout_secs;
    tv.tv_usec = 0;

    /* Wait for data with timeout using WaitSelect.
     * Pass &sigmask (zero) instead of NULL - some stacks need this.
     */
    select_result = WaitSelect(sock_fd + 1, &read_fds, NULL, NULL, &tv, &sigmask);

    if (select_result <= 0) {
        /* Timeout (0) or error (-1) */
        CloseSocket(sock_fd);
        sock_fd = -1;
        return -1;
    }

    /* Data is available, receive it */
    result = recvfrom(sock_fd, buf, buf_size, 0, NULL, NULL);

    /* Close the socket regardless of result */
    CloseSocket(sock_fd);
    sock_fd = -1;

    if (result < 0)
        return -1;

    return result;
}
