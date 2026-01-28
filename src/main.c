/* main.c - SyncTime entry point and commodity event loop */

#include "synctime.h"

/* Request 16KB stack */
LONG __stack = 16384;

/* AmigaOS version string */
const char verstag[] =
    "\0$VER: SyncTime " VERSION_STRING " (" BUILD_DATE ") " COMMIT_HASH;

/* Library bases */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
struct Library       *GadToolsBase  = NULL;
struct Library       *CxBase        = NULL;
struct Library       *UtilityBase   = NULL;
struct Library       *SocketBase    = NULL;
struct Device        *TimerBase     = NULL;

int main(int argc, char **argv)
{
    return 0;
}
