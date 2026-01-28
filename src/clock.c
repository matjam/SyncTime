/* clock.c - System clock and timer management for SyncTime */

#include "synctime.h"

/* --------------------------------------------------------------------------
 * Static state
 * -------------------------------------------------------------------------- */

/* Main timerequest: used for get/set system time (synchronous ops) */
static struct MsgPort     *main_port     = NULL;
static struct timerequest *main_treq     = NULL;

/* Periodic timerequest: used for async sync scheduling timer */
static struct MsgPort     *periodic_port = NULL;
static struct timerequest *periodic_treq = NULL;

/* Tracks whether an asynchronous timer request is outstanding */
static BOOL timer_pending = FALSE;

/* --------------------------------------------------------------------------
 * clock_init - Open timer.device and set up both timerequests
 * -------------------------------------------------------------------------- */

BOOL clock_init(void)
{
    LONG err;

    /* 1. Create main message port */
    main_port = CreateMsgPort();
    if (!main_port)
        goto fail;

    /* 2. Create main timerequest */
    main_treq = (struct timerequest *)
        CreateIORequest(main_port, sizeof(struct timerequest));
    if (!main_treq)
        goto fail;

    /* 3. Open timer.device on UNIT_VBLANK */
    err = OpenDevice("timer.device", UNIT_VBLANK,
                     (struct IORequest *)main_treq, 0);
    if (err != 0) {
        /* OpenDevice failed -- mark treq so cleanup won't CloseDevice */
        main_treq->tr_node.io_Device = NULL;
        goto fail;
    }

    /* 4. Set TimerBase so proto/timer.h functions work */
    TimerBase = (struct Device *)main_treq->tr_node.io_Device;

    /* 5. Create periodic message port */
    periodic_port = CreateMsgPort();
    if (!periodic_port)
        goto fail;

    /* 6. Create periodic timerequest */
    periodic_treq = (struct timerequest *)
        CreateIORequest(periodic_port, sizeof(struct timerequest));
    if (!periodic_treq)
        goto fail;

    /* 7. Copy device info from main treq so both share the device */
    periodic_treq->tr_node.io_Device = main_treq->tr_node.io_Device;
    periodic_treq->tr_node.io_Unit   = main_treq->tr_node.io_Unit;

    return TRUE;

fail:
    clock_cleanup();
    return FALSE;
}

/* --------------------------------------------------------------------------
 * clock_cleanup - Close device and free all resources
 * -------------------------------------------------------------------------- */

void clock_cleanup(void)
{
    /* 1. If a timer is pending, abort and wait for it */
    if (timer_pending && periodic_treq) {
        AbortIO((struct IORequest *)periodic_treq);
        WaitIO((struct IORequest *)periodic_treq);
        timer_pending = FALSE;
    }

    /* 2. Close the device (only once via main_treq) */
    if (main_treq && main_treq->tr_node.io_Device) {
        CloseDevice((struct IORequest *)main_treq);
        main_treq->tr_node.io_Device = NULL;
    }

    /* 3. Free periodic timerequest and port */
    if (periodic_treq) {
        DeleteIORequest((struct IORequest *)periodic_treq);
        periodic_treq = NULL;
    }
    if (periodic_port) {
        DeleteMsgPort(periodic_port);
        periodic_port = NULL;
    }

    /* 4. Free main timerequest and port */
    if (main_treq) {
        DeleteIORequest((struct IORequest *)main_treq);
        main_treq = NULL;
    }
    if (main_port) {
        DeleteMsgPort(main_port);
        main_port = NULL;
    }

    /* 5. Clear TimerBase */
    TimerBase = NULL;
}

/* --------------------------------------------------------------------------
 * clock_set_system_time - Set the Amiga system clock (synchronous)
 * -------------------------------------------------------------------------- */

BOOL clock_set_system_time(ULONG amiga_secs, ULONG amiga_micro)
{
    if (!main_treq)
        return FALSE;

    main_treq->tr_node.io_Command = TR_SETSYSTIME;
    main_treq->tr_time.tv_secs    = amiga_secs;
    main_treq->tr_time.tv_micro   = amiga_micro;

    DoIO((struct IORequest *)main_treq);

    return (main_treq->tr_node.io_Error == 0) ? TRUE : FALSE;
}

/* --------------------------------------------------------------------------
 * clock_get_system_time - Read the current Amiga system clock (synchronous)
 * -------------------------------------------------------------------------- */

BOOL clock_get_system_time(ULONG *amiga_secs, ULONG *amiga_micro)
{
    if (!main_treq || !amiga_secs || !amiga_micro)
        return FALSE;

    main_treq->tr_node.io_Command = TR_GETSYSTIME;

    DoIO((struct IORequest *)main_treq);

    if (main_treq->tr_node.io_Error == 0) {
        *amiga_secs  = main_treq->tr_time.tv_secs;
        *amiga_micro = main_treq->tr_time.tv_micro;
        return TRUE;
    }

    return FALSE;
}

/* --------------------------------------------------------------------------
 * clock_format_time - Format Amiga time as human-readable "date time" string
 * -------------------------------------------------------------------------- */

void clock_format_time(ULONG amiga_secs, char *buf, ULONG buf_size)
{
    struct DateTime dt;
    char date_buf[LEN_DATSTRING];
    char time_buf[LEN_DATSTRING];

    if (!buf || buf_size == 0)
        return;

    /* Convert Amiga seconds to a DateStamp */
    memset(&dt, 0, sizeof(dt));
    dt.dat_Stamp.ds_Days   = amiga_secs / 86400;
    dt.dat_Stamp.ds_Minute = (amiga_secs % 86400) / 60;
    dt.dat_Stamp.ds_Tick   = ((amiga_secs % 86400) % 60) * TICKS_PER_SECOND;

    dt.dat_Format  = FORMAT_DOS;
    dt.dat_Flags   = 0;
    dt.dat_StrDay  = NULL;
    dt.dat_StrDate = date_buf;
    dt.dat_StrTime = time_buf;

    if (DateToStr(&dt)) {
        /* Compose "date time" into the output buffer */
        if (strlen(date_buf) + 1 + strlen(time_buf) + 1 <= buf_size) {
            strcpy(buf, date_buf);
            strcat(buf, " ");
            strcat(buf, time_buf);
        } else {
            /* Buffer too small */
            if (buf_size >= 8)
                strcpy(buf, "Unknown");
            else
                buf[0] = '\0';
        }
    } else {
        /* DateToStr failed */
        if (buf_size >= 8)
            strcpy(buf, "Unknown");
        else
            buf[0] = '\0';
    }
}

/* --------------------------------------------------------------------------
 * clock_start_timer - Start (or restart) the periodic async timer
 * -------------------------------------------------------------------------- */

BOOL clock_start_timer(ULONG seconds)
{
    if (!periodic_treq)
        return FALSE;

    /* If a timer is already pending, abort it first */
    if (timer_pending) {
        AbortIO((struct IORequest *)periodic_treq);
        WaitIO((struct IORequest *)periodic_treq);
        timer_pending = FALSE;
    }

    periodic_treq->tr_node.io_Command = TR_ADDREQUEST;
    periodic_treq->tr_time.tv_secs    = seconds;
    periodic_treq->tr_time.tv_micro   = 0;

    SendIO((struct IORequest *)periodic_treq);
    timer_pending = TRUE;

    return TRUE;
}

/* --------------------------------------------------------------------------
 * clock_abort_timer - Safely cancel a pending periodic timer
 * -------------------------------------------------------------------------- */

void clock_abort_timer(void)
{
    if (timer_pending && periodic_treq) {
        AbortIO((struct IORequest *)periodic_treq);
        WaitIO((struct IORequest *)periodic_treq);
        timer_pending = FALSE;
    }
}

/* --------------------------------------------------------------------------
 * clock_timer_signal - Return the signal mask for the periodic timer port
 * -------------------------------------------------------------------------- */

ULONG clock_timer_signal(void)
{
    if (periodic_port)
        return 1UL << periodic_port->mp_SigBit;

    return 0;
}
