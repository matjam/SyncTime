# TZDB Integration Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace simple UTC offset with full IANA timezone database support, including proper DST handling and a region/city picker UI.

**Architecture:** Python build-time script parses tzdb files and generates a C table of ~312 timezone entries. New tz.c module provides lookup and DST calculation. Expanded preferences window lets users select region then city.

**Tech Stack:** Python 3 for code generation, C for AmigaOS, GadTools for UI

---

## Task 1: Add TZEntry Structure and Prototypes

**Files:**
- Modify: `include/synctime.h`

**Step 1: Add TZEntry typedef after SyncStatus**

```c
/* Timezone entry from generated tz_table.c */
typedef struct {
    const char *name;       /* Full name: "America/Los_Angeles" */
    const char *region;     /* Region: "America" */
    const char *city;       /* City: "Los_Angeles" */
    WORD std_offset_mins;   /* Standard offset from UTC in minutes */
    WORD dst_offset_mins;   /* Additional DST offset (0 if no DST) */
    UBYTE dst_start_month;  /* 1-12, 0 = no DST */
    UBYTE dst_start_week;   /* 1-5, which occurrence of dow */
    UBYTE dst_start_dow;    /* 0=Sun, 1=Mon, ..., 6=Sat */
    UBYTE dst_start_hour;   /* Local hour of transition */
    UBYTE dst_end_month;
    UBYTE dst_end_week;
    UBYTE dst_end_dow;
    UBYTE dst_end_hour;
} TZEntry;
```

**Step 2: Update SyncConfig to use tz_name**

Replace:
```c
    LONG  timezone;     /* hours offset from UTC (-12 to +14) */
    BOOL  dst;          /* daylight saving time enabled */
```

With:
```c
    char  tz_name[48];  /* IANA timezone name, e.g. "America/Los_Angeles" */
```

**Step 3: Add tz.c function prototypes**

```c
/* =========================================================================
 * tz.c - Timezone database functions
 * ========================================================================= */

extern const TZEntry tz_table[];
extern const ULONG tz_table_count;

const TZEntry *tz_find_by_name(const char *name);
const char   **tz_get_regions(ULONG *count);
const TZEntry **tz_get_cities_for_region(const char *region, ULONG *count);
BOOL           tz_is_dst_active(const TZEntry *tz, ULONG utc_secs);
LONG           tz_get_offset_mins(const TZEntry *tz, ULONG utc_secs);
```

**Step 4: Update sntp.c prototype**

Replace:
```c
ULONG sntp_ntp_to_amiga(ULONG ntp_secs, LONG tz_offset, BOOL dst);
```

With:
```c
ULONG sntp_ntp_to_amiga(ULONG ntp_secs, const TZEntry *tz);
```

**Step 5: Remove obsolete constants**

Remove these lines:
```c
#define DEFAULT_TIMEZONE   -8
#define DEFAULT_DST        TRUE
#define MIN_TIMEZONE       -12
#define MAX_TIMEZONE       14
```

Add:
```c
#define DEFAULT_TIMEZONE   "America/Los_Angeles"
```

**Step 6: Commit**

```bash
git add include/synctime.h
git commit -m "$(cat <<'EOF'
feat: add TZEntry structure and tz.c prototypes

Prepare for IANA timezone database integration with proper
DST handling. SyncConfig now stores timezone by name instead
of numeric offset.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Create Python tzdb Parser Script

**Files:**
- Create: `scripts/gen_tz_table.py`

**Step 1: Create scripts directory and parser**

```python
#!/usr/bin/env python3
"""
Generate tz_table.c from IANA tzdb source files.

Usage: python3 gen_tz_table.py <tzdata_dir> > src/tz_table.c
"""

import sys
import os
import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

@dataclass
class Rule:
    name: str
    from_year: int
    to_year: int  # 9999 for "max"
    month: int    # 1-12
    week: int     # 1-5, or 0 for specific day
    dow: int      # 0=Sun, 6=Sat, or -1 for specific day
    day: int      # specific day if week==0
    hour: int
    save_mins: int
    letter: str

@dataclass
class Zone:
    name: str
    std_offset_mins: int
    rule_name: str  # "-" means no DST

# Day of week mapping
DOW_MAP = {'Sun': 0, 'Mon': 1, 'Tue': 2, 'Wed': 3, 'Thu': 4, 'Fri': 5, 'Sat': 6}

# Month mapping
MONTH_MAP = {'Jan': 1, 'Feb': 2, 'Mar': 3, 'Apr': 4, 'May': 5, 'Jun': 6,
             'Jul': 7, 'Aug': 8, 'Sep': 9, 'Oct': 10, 'Nov': 11, 'Dec': 12}

def parse_offset(s: str) -> int:
    """Parse offset like '-8:00' or '5:30' to minutes."""
    if not s or s == '-':
        return 0
    negative = s.startswith('-')
    s = s.lstrip('-')
    parts = s.split(':')
    hours = int(parts[0])
    mins = int(parts[1]) if len(parts) > 1 else 0
    total = hours * 60 + mins
    return -total if negative else total

def parse_time(s: str) -> int:
    """Parse time like '2:00' to hour."""
    if not s:
        return 2
    s = s.rstrip('uswg')  # Remove suffixes
    parts = s.split(':')
    return int(parts[0])

def parse_on(s: str) -> Tuple[int, int, int]:
    """Parse ON field like 'lastSun', 'Sun>=8', '15' -> (week, dow, day)."""
    if s.startswith('last'):
        dow = DOW_MAP[s[4:]]
        return (5, dow, 0)  # week 5 means "last"

    m = re.match(r'(\w+)>=(\d+)', s)
    if m:
        dow = DOW_MAP[m.group(1)]
        day = int(m.group(2))
        # Convert "Sun>=8" to week number (8 means second week)
        week = (day + 6) // 7
        return (week, dow, 0)

    m = re.match(r'(\w+)<=(\d+)', s)
    if m:
        dow = DOW_MAP[m.group(1)]
        day = int(m.group(2))
        week = (day + 6) // 7
        return (week, dow, 0)

    # Specific day number
    return (0, -1, int(s))

def parse_save(s: str) -> int:
    """Parse SAVE field like '1:00' or '0' to minutes."""
    if s == '0' or s == '-':
        return 0
    return parse_offset(s)

def parse_rules(lines: List[str]) -> Dict[str, List[Rule]]:
    """Parse all Rule lines, return dict of rule_name -> [Rule]."""
    rules: Dict[str, List[Rule]] = {}

    for line in lines:
        if not line.startswith('Rule'):
            continue
        parts = line.split()
        if len(parts) < 10:
            continue

        name = parts[1]
        from_year = int(parts[2]) if parts[2] != 'min' else 1900
        to_year = 9999 if parts[3] == 'max' else int(parts[3]) if parts[3] != 'only' else from_year
        month = MONTH_MAP.get(parts[5], 0)
        week, dow, day = parse_on(parts[6])
        hour = parse_time(parts[7])
        save_mins = parse_save(parts[8])
        letter = parts[9] if len(parts) > 9 else ''

        rule = Rule(name, from_year, to_year, month, week, dow, day, hour, save_mins, letter)

        if name not in rules:
            rules[name] = []
        rules[name].append(rule)

    return rules

def parse_zones(lines: List[str]) -> List[Zone]:
    """Parse Zone lines, extracting only current (last) rule for each zone."""
    zones = []
    current_zone = None

    for line in lines:
        if line.startswith('Zone'):
            parts = line.split()
            if len(parts) >= 4:
                current_zone = parts[1]
                # Check if this line has an UNTIL field (not current)
                # Format: Zone NAME STDOFF RULES FORMAT [UNTIL...]
                offset = parse_offset(parts[2])
                rule = parts[3]
                # If there are 5+ parts after FORMAT, there's an UNTIL
                if len(parts) <= 5:
                    # No UNTIL, this is current
                    zones.append(Zone(current_zone, offset, rule))
                    current_zone = None
        elif current_zone and line.startswith('\t\t'):
            # Continuation line
            parts = line.split()
            if len(parts) >= 2:
                offset = parse_offset(parts[0])
                rule = parts[1]
                # If 3 or fewer parts, no UNTIL - this is current
                if len(parts) <= 3:
                    zones.append(Zone(current_zone, offset, rule))
                    current_zone = None

    return zones

def get_current_dst_rules(rule_name: str, rules: Dict[str, List[Rule]]) -> Tuple[Optional[Rule], Optional[Rule]]:
    """Get current DST start and end rules for a rule name."""
    if rule_name == '-' or rule_name not in rules:
        return None, None

    rule_list = rules[rule_name]

    # Find rules active in 2025 (current year)
    current_year = 2025
    active = [r for r in rule_list if r.from_year <= current_year <= r.to_year]

    if not active:
        return None, None

    # Separate into DST start (save > 0) and end (save == 0)
    starts = [r for r in active if r.save_mins > 0]
    ends = [r for r in active if r.save_mins == 0]

    start = starts[-1] if starts else None
    end = ends[-1] if ends else None

    return start, end

def generate_c_table(zones: List[Zone], rules: Dict[str, List[Rule]]) -> str:
    """Generate C source code for tz_table.c."""

    # Sort zones by region, then city
    def sort_key(z):
        parts = z.name.split('/', 1)
        region = parts[0]
        city = parts[1] if len(parts) > 1 else ''
        return (region, city)

    zones.sort(key=sort_key)

    lines = [
        '/* tz_table.c - Generated timezone table from IANA tzdb */',
        '/* DO NOT EDIT - Generated by scripts/gen_tz_table.py */',
        '',
        '#include "synctime.h"',
        '',
        'const TZEntry tz_table[] = {',
    ]

    for zone in zones:
        parts = zone.name.split('/', 1)
        region = parts[0]
        city = parts[1].replace('_', ' ') if len(parts) > 1 else parts[0]
        city_raw = parts[1] if len(parts) > 1 else parts[0]

        start, end = get_current_dst_rules(zone.rule_name, rules)

        if start and end:
            dst_offset = start.save_mins
            entry = (f'    {{"{zone.name}", "{region}", "{city_raw}", '
                    f'{zone.std_offset_mins}, {dst_offset}, '
                    f'{start.month}, {start.week}, {start.dow}, {start.hour}, '
                    f'{end.month}, {end.week}, {end.dow}, {end.hour}}},')
        else:
            entry = (f'    {{"{zone.name}", "{region}", "{city_raw}", '
                    f'{zone.std_offset_mins}, 0, '
                    f'0, 0, 0, 0, 0, 0, 0, 0}},')

        lines.append(entry)

    lines.append('};')
    lines.append('')
    lines.append(f'const ULONG tz_table_count = {len(zones)};')
    lines.append('')

    return '\n'.join(lines)

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <tzdata_dir>", file=sys.stderr)
        sys.exit(1)

    tzdata_dir = sys.argv[1]

    # Read all tzdb source files
    all_lines = []
    for fname in ['africa', 'antarctica', 'asia', 'australasia', 'europe',
                  'northamerica', 'southamerica', 'etcetera']:
        fpath = os.path.join(tzdata_dir, fname)
        if os.path.exists(fpath):
            with open(fpath, 'r') as f:
                for line in f:
                    line = line.rstrip()
                    # Skip comments and empty lines
                    if line and not line.startswith('#'):
                        all_lines.append(line)

    rules = parse_rules(all_lines)
    zones = parse_zones(all_lines)

    # Filter out legacy/link zones we don't want
    zones = [z for z in zones if '/' in z.name and not z.name.startswith('Etc/')]

    print(generate_c_table(zones, rules))

if __name__ == '__main__':
    main()
```

**Step 2: Make script executable**

```bash
chmod +x scripts/gen_tz_table.py
```

**Step 3: Test the script locally**

```bash
python3 scripts/gen_tz_table.py tzdb-2025c > /tmp/test_tz_table.c
head -50 /tmp/test_tz_table.c
```

**Step 4: Commit**

```bash
git add scripts/gen_tz_table.py
git commit -m "$(cat <<'EOF'
feat: add Python script to generate timezone table from tzdb

Parses IANA tzdb Zone and Rule definitions to extract current
timezone offsets and DST transition rules. Outputs tz_table.c
for compilation into SyncTime.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Update Makefile for tzdb Download and Generation

**Files:**
- Modify: `Makefile`

**Step 1: Add tzdb variables and targets**

Add after existing variables:

```makefile
# Timezone database
TZDB_VERSION = 2025c
TZDB_URL = https://data.iana.org/time-zones/releases/tzdata$(TZDB_VERSION).tar.gz
TZDB_DIR = tzdata

# Generated sources
GEN_SRCS = src/tz_table.c
```

**Step 2: Add download and generation rules**

```makefile
# Download and extract tzdb
$(TZDB_DIR)/.downloaded:
	@echo "Downloading tzdata $(TZDB_VERSION)..."
	mkdir -p $(TZDB_DIR)
	curl -sL $(TZDB_URL) | tar xz -C $(TZDB_DIR)
	touch $@

# Generate timezone table
src/tz_table.c: $(TZDB_DIR)/.downloaded scripts/gen_tz_table.py
	@echo "Generating timezone table..."
	python3 scripts/gen_tz_table.py $(TZDB_DIR) > $@

# Add tz_table.c to SRCS
SRCS += src/tz_table.c
```

**Step 3: Add clean target for generated files**

```makefile
clean-generated:
	rm -f $(GEN_SRCS)
	rm -rf $(TZDB_DIR)

clean: clean-generated
```

**Step 4: Add .gitignore entries**

```bash
echo "tzdata/" >> .gitignore
echo "src/tz_table.c" >> .gitignore
```

**Step 5: Commit**

```bash
git add Makefile .gitignore
git commit -m "$(cat <<'EOF'
build: add tzdb download and tz_table.c generation

Makefile now downloads IANA tzdata on first build and generates
src/tz_table.c using the Python parser script.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Implement tz.c Module

**Files:**
- Create: `src/tz.c`

**Step 1: Create tz.c with lookup functions**

```c
/* tz.c - Timezone database functions for SyncTime */

#include "synctime.h"

/* tz_table[] and tz_table_count are defined in generated tz_table.c */

/* --------------------------------------------------------------------------
 * tz_find_by_name - Find timezone entry by full name
 * -------------------------------------------------------------------------- */

const TZEntry *tz_find_by_name(const char *name)
{
    ULONG i;

    if (name == NULL)
        return NULL;

    for (i = 0; i < tz_table_count; i++) {
        if (strcmp(tz_table[i].name, name) == 0)
            return &tz_table[i];
    }

    return NULL;
}

/* --------------------------------------------------------------------------
 * tz_get_regions - Get list of unique region names
 *
 * Returns static array of region strings. Caller must not free.
 * -------------------------------------------------------------------------- */

static const char *region_list[20];
static ULONG region_count = 0;

const char **tz_get_regions(ULONG *count)
{
    ULONG i, j;
    const char *region;
    BOOL found;

    /* Build list on first call */
    if (region_count == 0) {
        for (i = 0; i < tz_table_count && region_count < 20; i++) {
            region = tz_table[i].region;

            /* Check if already in list */
            found = FALSE;
            for (j = 0; j < region_count; j++) {
                if (strcmp(region_list[j], region) == 0) {
                    found = TRUE;
                    break;
                }
            }

            if (!found) {
                region_list[region_count++] = region;
            }
        }
    }

    if (count)
        *count = region_count;

    return region_list;
}

/* --------------------------------------------------------------------------
 * tz_get_cities_for_region - Get timezone entries for a region
 *
 * Returns static array of TZEntry pointers. Caller must not free.
 * -------------------------------------------------------------------------- */

static const TZEntry *city_list[100];
static ULONG city_count = 0;

const TZEntry **tz_get_cities_for_region(const char *region, ULONG *count)
{
    ULONG i;

    city_count = 0;

    if (region == NULL) {
        if (count)
            *count = 0;
        return city_list;
    }

    for (i = 0; i < tz_table_count && city_count < 100; i++) {
        if (strcmp(tz_table[i].region, region) == 0) {
            city_list[city_count++] = &tz_table[i];
        }
    }

    if (count)
        *count = city_count;

    return city_list;
}

/* --------------------------------------------------------------------------
 * Helper: Calculate day of week for a given date
 *
 * Uses Zeller's congruence. Returns 0=Sun, 1=Mon, ..., 6=Sat
 * -------------------------------------------------------------------------- */

static int day_of_week(int year, int month, int day)
{
    int q = day;
    int m = month;
    int k, j, h;

    if (m < 3) {
        m += 12;
        year--;
    }

    k = year % 100;
    j = year / 100;

    h = (q + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    /* Convert from Zeller (0=Sat) to our format (0=Sun) */
    return ((h + 6) % 7);
}

/* --------------------------------------------------------------------------
 * Helper: Get day of month for "Nth DOW of month"
 *
 * week: 1-4 for first through fourth, 5 for last
 * dow: 0=Sun, 1=Mon, ..., 6=Sat
 * -------------------------------------------------------------------------- */

static int nth_dow_of_month(int year, int month, int week, int dow)
{
    int first_dow, day, last_day;
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    /* Leap year adjustment */
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        days_in_month[2] = 29;

    last_day = days_in_month[month];

    if (week == 5) {
        /* Last occurrence of dow in month */
        day = last_day;
        while (day_of_week(year, month, day) != dow)
            day--;
        return day;
    }

    /* First occurrence */
    first_dow = day_of_week(year, month, 1);
    day = 1 + ((dow - first_dow + 7) % 7);

    /* Nth occurrence */
    day += (week - 1) * 7;

    return (day <= last_day) ? day : 0;
}

/* --------------------------------------------------------------------------
 * tz_is_dst_active - Check if DST is active for given UTC time
 * -------------------------------------------------------------------------- */

BOOL tz_is_dst_active(const TZEntry *tz, ULONG utc_secs)
{
    ULONG local_secs;
    int year, month, day, hour;
    int dst_start_day, dst_end_day;
    ULONG days, secs_in_day;
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m;

    /* No DST if start month is 0 */
    if (tz == NULL || tz->dst_start_month == 0)
        return FALSE;

    /* Convert to local standard time for comparison */
    local_secs = utc_secs + (tz->std_offset_mins * 60);

    /* Calculate year, month, day, hour from Amiga seconds */
    /* Amiga epoch: Jan 1, 1978 */
    days = local_secs / 86400;
    secs_in_day = local_secs % 86400;
    hour = secs_in_day / 3600;

    /* Calculate year and day of year */
    year = 1978;
    while (1) {
        int days_in_year = 365;
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
            days_in_year = 366;
        if (days < (ULONG)days_in_year)
            break;
        days -= days_in_year;
        year++;
    }

    /* Leap year adjustment */
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
        days_in_month[2] = 29;

    /* Calculate month and day */
    month = 1;
    for (m = 1; m <= 12; m++) {
        if (days < (ULONG)days_in_month[m]) {
            month = m;
            day = days + 1;
            break;
        }
        days -= days_in_month[m];
    }

    /* Calculate DST transition days */
    dst_start_day = nth_dow_of_month(year, tz->dst_start_month,
                                      tz->dst_start_week, tz->dst_start_dow);
    dst_end_day = nth_dow_of_month(year, tz->dst_end_month,
                                    tz->dst_end_week, tz->dst_end_dow);

    /* Northern hemisphere: DST from spring to fall */
    if (tz->dst_start_month < tz->dst_end_month) {
        /* Before DST start month */
        if (month < tz->dst_start_month)
            return FALSE;
        /* After DST end month */
        if (month > tz->dst_end_month)
            return FALSE;
        /* In DST start month */
        if (month == tz->dst_start_month) {
            if (day < dst_start_day)
                return FALSE;
            if (day == dst_start_day && hour < tz->dst_start_hour)
                return FALSE;
            return TRUE;
        }
        /* In DST end month */
        if (month == tz->dst_end_month) {
            if (day > dst_end_day)
                return FALSE;
            if (day == dst_end_day && hour >= tz->dst_end_hour)
                return FALSE;
            return TRUE;
        }
        /* Between start and end months */
        return TRUE;
    }
    /* Southern hemisphere: DST from fall to spring (wraps year) */
    else {
        /* In DST if after start OR before end */
        if (month > tz->dst_start_month || month < tz->dst_end_month)
            return TRUE;
        if (month == tz->dst_start_month) {
            if (day > dst_start_day)
                return TRUE;
            if (day == dst_start_day && hour >= tz->dst_start_hour)
                return TRUE;
            return FALSE;
        }
        if (month == tz->dst_end_month) {
            if (day < dst_end_day)
                return TRUE;
            if (day == dst_end_day && hour < tz->dst_end_hour)
                return TRUE;
            return FALSE;
        }
        return FALSE;
    }
}

/* --------------------------------------------------------------------------
 * tz_get_offset_mins - Get current offset from UTC in minutes
 * -------------------------------------------------------------------------- */

LONG tz_get_offset_mins(const TZEntry *tz, ULONG utc_secs)
{
    LONG offset;

    if (tz == NULL)
        return 0;

    offset = tz->std_offset_mins;

    if (tz_is_dst_active(tz, utc_secs))
        offset += tz->dst_offset_mins;

    return offset;
}
```

**Step 2: Commit**

```bash
git add src/tz.c
git commit -m "$(cat <<'EOF'
feat: implement tz.c timezone module

Provides timezone lookup by name, region/city enumeration,
and DST calculation with proper handling for both northern
and southern hemisphere transitions.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Update sntp.c to Use TZEntry

**Files:**
- Modify: `src/sntp.c`

**Step 1: Update sntp_ntp_to_amiga function signature and implementation**

Replace the existing function:

```c
/* --------------------------------------------------------------------------
 * sntp_ntp_to_amiga - Convert NTP timestamp to Amiga local time
 * -------------------------------------------------------------------------- */

ULONG sntp_ntp_to_amiga(ULONG ntp_secs, const TZEntry *tz)
{
    ULONG utc_secs;
    LONG offset_mins;

    /* Convert NTP epoch to Amiga epoch (both are UTC at this point) */
    utc_secs = ntp_secs - NTP_TO_AMIGA_EPOCH;

    /* Get timezone offset including DST if active */
    offset_mins = tz_get_offset_mins(tz, utc_secs);

    /* Apply offset (can be negative for western timezones) */
    return utc_secs + (offset_mins * 60);
}
```

**Step 2: Commit**

```bash
git add src/sntp.c
git commit -m "$(cat <<'EOF'
refactor: update sntp_ntp_to_amiga to use TZEntry

Uses tz_get_offset_mins() for proper DST-aware time conversion
instead of simple offset and boolean DST flag.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Update config.c for tz_name

**Files:**
- Modify: `src/config.c`

**Step 1: Update default config initialization**

Change the default timezone initialization to use tz_name:

```c
static void set_defaults(void)
{
    strcpy(cfg.server, DEFAULT_SERVER);
    cfg.interval = DEFAULT_INTERVAL;
    strcpy(cfg.tz_name, DEFAULT_TIMEZONE);
}
```

**Step 2: Update config_load to parse TIMEZONE as string**

Replace timezone/dst parsing with:

```c
        else if (strncmp(line, "TIMEZONE=", 9) == 0) {
            strncpy(cfg.tz_name, line + 9, sizeof(cfg.tz_name) - 1);
            cfg.tz_name[sizeof(cfg.tz_name) - 1] = '\0';
        }
```

Remove the DST= parsing.

**Step 3: Update config_save to write tz_name**

Replace timezone/dst writing with:

```c
    /* Write timezone */
    strcpy(line, "TIMEZONE=");
    strcat(line, cfg.tz_name);
    strcat(line, "\n");
    Write(fh, line, strlen(line));
```

Remove DST writing.

**Step 4: Remove config_set_timezone and config_set_dst functions**

Add new function:

```c
void config_set_tz_name(const char *name)
{
    if (name) {
        strncpy(cfg.tz_name, name, sizeof(cfg.tz_name) - 1);
        cfg.tz_name[sizeof(cfg.tz_name) - 1] = '\0';
    }
}
```

**Step 5: Commit**

```bash
git add src/config.c
git commit -m "$(cat <<'EOF'
refactor: update config.c to store timezone by name

Replaces numeric timezone offset and DST boolean with IANA
timezone name string (e.g. "America/Los_Angeles").

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Update main.c to Use TZEntry

**Files:**
- Modify: `src/main.c`

**Step 1: Add TZEntry lookup in perform_sync**

After getting the config, look up the timezone:

```c
    const TZEntry *tz;

    /* ... existing code ... */

    cfg = config_get();

    /* Look up timezone entry */
    tz = tz_find_by_name(cfg->tz_name);
    if (tz == NULL) {
        window_log("WARNING: Unknown timezone, using UTC");
        /* Fall through with NULL tz - tz_get_offset_mins handles this */
    }
```

**Step 2: Update sntp_ntp_to_amiga call**

Change:
```c
    amiga_secs = sntp_ntp_to_amiga(ntp_secs, cfg->timezone, cfg->dst);
```

To:
```c
    amiga_secs = sntp_ntp_to_amiga(ntp_secs, tz);
```

**Step 3: Commit**

```bash
git add src/main.c
git commit -m "$(cat <<'EOF'
refactor: update main.c to use TZEntry for time conversion

Looks up timezone by name from config and passes TZEntry to
sntp_ntp_to_amiga for proper DST-aware conversion.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Update window.c with Region/City UI

**Files:**
- Modify: `src/window.c`

**Step 1: Add new gadget IDs**

```c
#define GID_REGION     20
#define GID_CITY       21
#define GID_TZ_INFO    22
```

**Step 2: Add gadget pointers**

```c
static struct Gadget *gad_region  = NULL;
static struct Gadget *gad_city    = NULL;
static struct Gadget *gad_tz_info = NULL;
```

**Step 3: Add region/city state**

```c
static ULONG current_region_idx = 0;
static const char **region_labels = NULL;
static ULONG region_label_count = 0;

static struct List city_list_header;
static struct Node city_nodes[100];
static ULONG city_node_count = 0;
static const TZEntry **current_cities = NULL;
static ULONG current_city_count = 0;
```

**Step 4: Add helper to build city list for region**

```c
static void build_city_list(const char *region)
{
    ULONG i;

    current_cities = tz_get_cities_for_region(region, &current_city_count);

    NewList(&city_list_header);
    city_node_count = 0;

    for (i = 0; i < current_city_count && i < 100; i++) {
        city_nodes[i].ln_Name = (STRPTR)current_cities[i]->city;
        city_nodes[i].ln_Type = 0;
        city_nodes[i].ln_Pri = 0;
        AddTail(&city_list_header, &city_nodes[i]);
        city_node_count++;
    }
}
```

**Step 5: Add helper to format timezone info string**

```c
static void format_tz_info(const TZEntry *tz, char *buf, ULONG buf_size)
{
    LONG offset_hrs, offset_mins;
    char sign;

    if (tz == NULL) {
        strcpy(buf, "UTC");
        return;
    }

    offset_mins = tz->std_offset_mins;
    sign = (offset_mins >= 0) ? '+' : '-';
    if (offset_mins < 0) offset_mins = -offset_mins;
    offset_hrs = offset_mins / 60;
    offset_mins = offset_mins % 60;

    if (tz->dst_offset_mins > 0) {
        LONG dst_total = tz->std_offset_mins + tz->dst_offset_mins;
        LONG dst_hrs, dst_mins;
        char dst_sign = (dst_total >= 0) ? '+' : '-';
        if (dst_total < 0) dst_total = -dst_total;
        dst_hrs = dst_total / 60;
        dst_mins = dst_total % 60;

        if (offset_mins == 0 && dst_mins == 0) {
            sprintf(buf, "UTC%c%ld, DST: UTC%c%ld",
                    sign, offset_hrs, dst_sign, dst_hrs);
        } else {
            sprintf(buf, "UTC%c%ld:%02ld, DST: UTC%c%ld:%02ld",
                    sign, offset_hrs, offset_mins,
                    dst_sign, dst_hrs, dst_mins);
        }
    } else {
        if (offset_mins == 0) {
            sprintf(buf, "UTC%c%ld (no DST)", sign, offset_hrs);
        } else {
            sprintf(buf, "UTC%c%ld:%02ld (no DST)", sign, offset_hrs, offset_mins);
        }
    }
}
```

**Step 6: Update create_gadgets to add region cycle and city listview**

After the server gadget, add:

```c
    /* Region cycle */
    region_labels = tz_get_regions(&region_label_count);

    ng.ng_LeftEdge   = 100;
    ng.ng_TopEdge    = 45;
    ng.ng_Width      = 180;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = "Region:";
    ng.ng_GadgetID   = GID_REGION;
    ng.ng_Flags      = PLACETEXT_LEFT;

    gad_region = gad = CreateGadget(CYCLE_KIND, gad, &ng,
        GTCY_Labels, region_labels,
        GTCY_Active, current_region_idx,
        TAG_DONE);

    /* City listview */
    build_city_list(region_labels[current_region_idx]);

    ng.ng_TopEdge    = 65;
    ng.ng_Width      = 180;
    ng.ng_Height     = 80;
    ng.ng_GadgetText = "City:";
    ng.ng_GadgetID   = GID_CITY;

    gad_city = gad = CreateGadget(LISTVIEW_KIND, gad, &ng,
        GTLV_Labels, &city_list_header,
        GTLV_ShowSelected, NULL,
        GTLV_Selected, 0,
        TAG_DONE);

    /* Timezone info text */
    ng.ng_TopEdge    = 150;
    ng.ng_Width      = 250;
    ng.ng_Height     = 14;
    ng.ng_GadgetText = NULL;
    ng.ng_GadgetID   = GID_TZ_INFO;
    ng.ng_Flags      = 0;

    gad_tz_info = gad = CreateGadget(TEXT_KIND, gad, &ng,
        GTTX_Border, TRUE,
        TAG_DONE);
```

**Step 7: Update window dimensions**

Change window open call to use larger dimensions:

```c
    win = OpenWindowTags(NULL,
        WA_Left,        100,
        WA_Top,         50,
        WA_Width,       420,
        WA_Height,      340,
        /* ... rest of tags ... */
```

**Step 8: Adjust other gadget positions**

Move interval, status, last sync, next sync, log, and buttons down to accommodate the new timezone gadgets. Adjust TopEdge values accordingly.

**Step 9: Handle region cycle and city listview events in window_handle_events**

```c
            case GID_REGION:
                current_region_idx = msg_code;
                build_city_list(region_labels[current_region_idx]);
                GT_SetGadgetAttrs(gad_city, win, NULL,
                    GTLV_Labels, &city_list_header,
                    GTLV_Selected, 0,
                    TAG_DONE);
                /* Update tz_info display */
                if (current_city_count > 0) {
                    char info[64];
                    format_tz_info(current_cities[0], info, sizeof(info));
                    GT_SetGadgetAttrs(gad_tz_info, win, NULL,
                        GTTX_Text, info,
                        TAG_DONE);
                }
                break;

            case GID_CITY:
                if (msg_code < current_city_count) {
                    char info[64];
                    strcpy(cfg->tz_name, current_cities[msg_code]->name);
                    format_tz_info(current_cities[msg_code], info, sizeof(info));
                    GT_SetGadgetAttrs(gad_tz_info, win, NULL,
                        GTTX_Text, info,
                        TAG_DONE);
                }
                break;
```

**Step 10: Initialize region/city from config on window open**

Add logic to find the current timezone in the table and set the region cycle and city listview selection accordingly.

**Step 11: Remove old timezone and DST gadgets**

Remove GID_TIMEZONE integer gadget and GID_DST checkbox gadget definitions and creation.

**Step 12: Commit**

```bash
git add src/window.c
git commit -m "$(cat <<'EOF'
feat: add region/city timezone picker to preferences window

Replaces numeric timezone offset with IANA timezone selection.
Region cycle filters city listview. Displays current UTC offset
and DST information for selected timezone.

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Build and Test

**Step 1: Generate timezone table and build**

```bash
make clean
make
```

**Step 2: Verify tz_table.c was generated**

```bash
head -20 src/tz_table.c
wc -l src/tz_table.c
```

**Step 3: Test on Amiga or emulator**

- Launch SyncTime
- Open preferences window
- Verify region cycle shows regions (Africa, America, Asia, etc.)
- Select a region and verify city list updates
- Select a city and verify timezone info updates
- Click Sync Now and verify time is set correctly
- Save preferences and reopen - verify selection persists

**Step 4: Commit any final fixes**

```bash
git add -A
git commit -m "$(cat <<'EOF'
fix: final adjustments after testing tzdb integration

Co-Authored-By: Claude Opus 4.5 <noreply@anthropic.com>
EOF
)"
```

---

## Summary

| Task | Description |
|------|-------------|
| 1 | Add TZEntry structure and prototypes to synctime.h |
| 2 | Create Python tzdb parser script |
| 3 | Update Makefile for tzdb download and generation |
| 4 | Implement tz.c module with lookup and DST logic |
| 5 | Update sntp.c to use TZEntry |
| 6 | Update config.c for tz_name storage |
| 7 | Update main.c to use TZEntry for conversion |
| 8 | Update window.c with region/city picker UI |
| 9 | Build and test |
