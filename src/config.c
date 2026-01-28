/* config.c - Configuration management for SyncTime
 *
 * Follows Solitude's prefs.c pattern: static module state, set_defaults(),
 * parse_line() with strncmp-based key=value parsing, manual integer
 * conversion (no atoi/sprintf), FGets/FPuts file I/O, save to both
 * ENV: and ENVARC:.
 */

#include "synctime.h"

/* =========================================================================
 * Static module state
 * ========================================================================= */

static SyncConfig current_config;

/* =========================================================================
 * Helper: set all config fields to compiled-in defaults
 * ========================================================================= */

static void set_defaults(void)
{
    LONG i;
    const char *src = DEFAULT_SERVER;
    const char *tz_src = DEFAULT_TIMEZONE;  /* "America/Los_Angeles" */

    for (i = 0; i < SERVER_NAME_MAX - 1 && src[i] != '\0'; i++)
        current_config.server[i] = src[i];
    current_config.server[i] = '\0';

    current_config.interval = DEFAULT_INTERVAL;

    for (i = 0; i < (LONG)sizeof(current_config.tz_name) - 1 && tz_src[i] != '\0'; i++)
        current_config.tz_name[i] = tz_src[i];
    current_config.tz_name[i] = '\0';
}

/* =========================================================================
 * Helper: parse a signed integer from a string (manual digit loop)
 *
 * Handles optional leading '-' for negative values.
 * Returns the parsed value; sets *ok to TRUE on success, FALSE if no
 * digits were found.
 * ========================================================================= */

static LONG parse_int(const char *s, BOOL *ok)
{
    LONG val = 0;
    BOOL negative = FALSE;
    BOOL found = FALSE;

    if (*s == '-') {
        negative = TRUE;
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        found = TRUE;
        s++;
    }

    if (ok)
        *ok = found;

    return negative ? -val : val;
}

/* =========================================================================
 * Helper: convert an integer to a string (manual, no sprintf)
 *
 * Writes into buf (must be large enough -- 16 bytes is plenty).
 * Handles negative numbers by writing '-' then the absolute value.
 * ========================================================================= */

static void int_to_str(LONG val, char *buf)
{
    char tmp[16];
    LONG i = 0;
    BOOL negative = FALSE;
    ULONG uval;

    if (val < 0) {
        negative = TRUE;
        uval = (ULONG)(-(val + 1)) + 1;  /* safe negate avoiding overflow */
    } else {
        uval = (ULONG)val;
    }

    if (uval == 0) {
        tmp[i++] = '0';
    } else {
        while (uval > 0) {
            tmp[i++] = '0' + (char)(uval % 10);
            uval /= 10;
        }
    }

    /* Build output: optional '-' then digits in reverse order */
    {
        LONG out = 0;
        if (negative)
            buf[out++] = '-';
        while (i > 0)
            buf[out++] = tmp[--i];
        buf[out] = '\0';
    }
}

/* =========================================================================
 * Helper: parse a single key=value line and apply to current_config
 *
 * Uses strncmp to match keys. Ignores unknown keys silently.
 * ========================================================================= */

static void parse_line(const char *line)
{
    BOOL ok;
    LONG val;

    if (strncmp(line, "SERVER=", 7) == 0) {
        const char *src = line + 7;
        LONG i;

        for (i = 0; i < SERVER_NAME_MAX - 1 && src[i] != '\0'; i++)
            current_config.server[i] = src[i];
        current_config.server[i] = '\0';

        /* Strip trailing whitespace and newlines */
        while (i > 0 &&
               (current_config.server[i - 1] == '\n' ||
                current_config.server[i - 1] == '\r' ||
                current_config.server[i - 1] == ' '  ||
                current_config.server[i - 1] == '\t')) {
            i--;
            current_config.server[i] = '\0';
        }

    } else if (strncmp(line, "INTERVAL=", 9) == 0) {
        val = parse_int(line + 9, &ok);
        if (ok) {
            if (val < MIN_INTERVAL) val = MIN_INTERVAL;
            if (val > MAX_INTERVAL) val = MAX_INTERVAL;
            current_config.interval = val;
        }

    } else if (strncmp(line, "TIMEZONE=", 9) == 0) {
        const char *src = line + 9;
        LONG i;

        for (i = 0; i < (LONG)sizeof(current_config.tz_name) - 1 && src[i] != '\0'; i++)
            current_config.tz_name[i] = src[i];
        current_config.tz_name[i] = '\0';

        /* Strip trailing whitespace and newlines */
        while (i > 0 &&
               (current_config.tz_name[i - 1] == '\n' ||
                current_config.tz_name[i - 1] == '\r' ||
                current_config.tz_name[i - 1] == ' '  ||
                current_config.tz_name[i - 1] == '\t')) {
            i--;
            current_config.tz_name[i] = '\0';
        }
    }
}

/* =========================================================================
 * Helper: save current config to a single file path
 *
 * Uses Open/FPuts/Close (AmigaOS dos.library).
 * Returns TRUE on success.
 * ========================================================================= */

static BOOL save_to_path(const char *path)
{
    BPTR fh;
    char buf[16];

    fh = Open(path, MODE_NEWFILE);
    if (!fh)
        return FALSE;

    /* SERVER= */
    FPuts(fh, "SERVER=");
    FPuts(fh, current_config.server);
    FPuts(fh, "\n");

    /* INTERVAL= */
    FPuts(fh, "INTERVAL=");
    int_to_str(current_config.interval, buf);
    FPuts(fh, buf);
    FPuts(fh, "\n");

    /* TIMEZONE= */
    FPuts(fh, "TIMEZONE=");
    FPuts(fh, current_config.tz_name);
    FPuts(fh, "\n");

    Close(fh);
    return TRUE;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

/* config_init: set defaults, attempt load, write defaults if no file */
BOOL config_init(void)
{
    set_defaults();
    if (!config_load()) {
        /* No prefs file found -- write defaults to both locations */
        config_save();
    }
    return TRUE;
}

/* config_cleanup: nothing to free (all static) */
void config_cleanup(void)
{
}

/* config_load: read and parse ENV:SyncTime.prefs line by line */
BOOL config_load(void)
{
    BPTR fh;
    char line[256];

    fh = Open(PREFS_ENV_PATH, MODE_OLDFILE);
    if (!fh)
        return FALSE;

    while (FGets(fh, line, sizeof(line))) {
        parse_line(line);
    }

    Close(fh);
    return TRUE;
}

/* config_save: write to both ENV: and ENVARC: */
BOOL config_save(void)
{
    BOOL ok;

    ok  = save_to_path(PREFS_ENV_PATH);
    ok &= save_to_path(PREFS_ENVARC_PATH);

    return ok;
}

/* config_get: return pointer to static config struct */
SyncConfig *config_get(void)
{
    return &current_config;
}

/* config_set_server: copy server name with length limit */
void config_set_server(const char *server)
{
    LONG i;

    if (!server)
        return;

    for (i = 0; i < SERVER_NAME_MAX - 1 && server[i] != '\0'; i++)
        current_config.server[i] = server[i];
    current_config.server[i] = '\0';
}

/* config_set_interval: set with clamping */
void config_set_interval(LONG interval)
{
    if (interval < MIN_INTERVAL) interval = MIN_INTERVAL;
    if (interval > MAX_INTERVAL) interval = MAX_INTERVAL;
    current_config.interval = interval;
}

/* config_set_tz_name: set IANA timezone name */
void config_set_tz_name(const char *name)
{
    LONG i;

    if (!name)
        return;

    for (i = 0; i < (LONG)sizeof(current_config.tz_name) - 1 && name[i] != '\0'; i++)
        current_config.tz_name[i] = name[i];
    current_config.tz_name[i] = '\0';
}
