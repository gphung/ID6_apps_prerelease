#define _POSIX_C_SOURCE 200809L
#include "n55_metrics.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PSI_PER_BAR 14.5037738
#define F(c) (((c) - 32) / 1.8)
#define PSI(p) ((p) / PSI_PER_BAR)

/* key, caption, DID, bytes, block offset, flags, unit, bar, rule, rate ms, mul, add,
 * bar range, plausible range, decimals (C / psi), decimals (F / bar). Pressures are gauge bar,
 * temperatures C. Timing n and Misfire n are cylinder n. The timing DIDs are the DME's firing-order
 * array (1-5-3-6-2-4): [0] 4A49, [1] 4A4A, [2] 4A4C, [3] 4A4D, [4] 4A4E, [5] 4A4F, the same order
 * as its per-cylinder roughness and knock entries, which name their cylinders. */
#define M(k, c, d, n, o, fl, u, b, r, ms, mul, add, lo, hi, vlo, vhi, dec, alt) \
    {k, c, d, n, o, fl, u, b, r, ms, mul, add, lo, hi, vlo, vhi, dec, alt}
const nm_metric nm_metrics[NM_COUNT] = {
    M("boost", "BOOST", 0x4205, 2, 0, NF_AMBIENT, NU_PRESS, NB_LEFT, NR_NONE, 0, 0.078125, 0, 0, PSI(25), -1.2, 4, 1, 2),
    M("boost_target", "BOOST TARGET", 0x4AB0, 2, 0, NF_AMBIENT, NU_PRESS, NB_LEFT, NR_NONE, 0, 0.0390625, 0, 0, PSI(25), -1.2, 4, 1, 2),
    M("afr", "AFR", 0x5889, 2, 0, NF_LAMBDA, NU_NONE, NB_LEFT, NR_NONE, 0, 16.0 / 65535, 0, 9, 19, 7.35, 29.4, 1, 1),
    M("afr_target", "AFR TARGET", 0x4704, 2, 0, NF_LAMBDA, NU_NONE, NB_LEFT, NR_NONE, 500, 0.00024414, 0, 9, 19, 7.35, 29.4, 1, 1),
    M("fuel_trim", "FUEL TRIM", 0x4A81, 2, 0, NF_TRIM, NU_PCT, NB_ZERO, NR_NONE, 500, 0.00003052, 0, -25, 25, -50, 50, 1, 1),
    M("throttle", "THROTTLE", 0x480B, 2, 0, 0, NU_PCT, NB_LEFT, NR_NONE, 0, 0.0015259, 0, 0, 100, 0, 101, 0, 0),
    M("timing", "TIMING", 0x4A49, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("timing1", "TIMING 1", 0x4A49, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("timing2", "TIMING 2", 0x4A4E, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("timing3", "TIMING 3", 0x4A4C, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("timing4", "TIMING 4", 0x4A4F, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("timing5", "TIMING 5", 0x4A4A, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("timing6", "TIMING 6", 0x4A4D, 2, 0, NF_SIGNED | NF_NO_FFFF, NU_DEG, NB_ZERO, NR_NONE, 0, 0.1, 0, -20, 40, -64, 64, 1, 1),
    M("knock", "KNOCK", 0x4A36, 1, 0, 0, NU_NONE, NB_LEFT, NR_KNOCK, 0, 1, 0, 0, 1, 0, 1, 0, 0),
    M("misfire1", "MISFIRE 1", 0x4B23, 2, 0, 0, NU_NONE, NB_LEFT, NR_MISFIRE, 1000, 1, 0, 0, 20, 0, 65534, 0, 0),
    M("misfire2", "MISFIRE 2", 0x4B24, 2, 0, 0, NU_NONE, NB_LEFT, NR_MISFIRE, 1000, 1, 0, 0, 20, 0, 65534, 0, 0),
    M("misfire3", "MISFIRE 3", 0x4B25, 2, 0, 0, NU_NONE, NB_LEFT, NR_MISFIRE, 1000, 1, 0, 0, 20, 0, 65534, 0, 0),
    M("misfire4", "MISFIRE 4", 0x4B30, 2, 0, 0, NU_NONE, NB_LEFT, NR_MISFIRE, 1000, 1, 0, 0, 20, 0, 65534, 0, 0),
    M("misfire5", "MISFIRE 5", 0x4B31, 2, 0, 0, NU_NONE, NB_LEFT, NR_MISFIRE, 1000, 1, 0, 0, 20, 0, 65534, 0, 0),
    M("misfire6", "MISFIRE 6", 0x4B32, 2, 0, 0, NU_NONE, NB_LEFT, NR_MISFIRE, 1000, 1, 0, 0, 20, 0, 65534, 0, 0),
    M("iat", "INTAKE AIR", 0x580F, 1, 0, 0, NU_TEMP, NB_LEFT, NR_NONE, 2000, 0.75, -48, -20, 100, -48, 143.25, 1, 1),
    M("oil_temp", "OIL TEMP", 0x4408, 2, 0, NF_SIGNED, NU_TEMP, NB_LEFT, NR_OIL_BAND, 2000, 0.1, 0, F(100), F(300), -48, 170, 1, 1),
    M("coolant", "COOLANT", 0x05, 1, 0, NF_OBD, NU_TEMP, NB_LEFT, NR_NONE, 2000, 1, -40, 0, 130, -40, 150, 1, 1),
    M("hpfp", "HPFP", 0x58EF, 2, 0, 0, NU_PRESS, NB_LEFT, NR_NONE, 500, 0.005, 0, 0, 200, 0, 300, 0, 0),
    M("hpfp_target", "HPFP TARGET", 0x5837, 2, 0, 0, NU_PRESS, NB_LEFT, NR_NONE, 500, 0.005, 0, 0, 200, 0, 300, 0, 0),
    M("lpfp", "LPFP", 0x58F3, 2, 0, 0, NU_PRESS, NB_LEFT, NR_NONE, 1000, 0.001, 0, 0, 10, 0, 20, 0, 1),
    M("oil_pressure", "OIL PRESSURE", 0x586F, 2, 0, NF_AMBIENT, NU_PRESS, NB_LEFT, NR_NONE, 0, 1, 0, 0, 6, -1, 20, 1, 1),
    M("wastegate", "WASTEGATE", 0x4654, 2, 0, NF_SIGNED | NF_WASTEGATE, NU_PCT, NB_LEFT, NR_NONE, 0, 0.00097656, 0, 0, 100, -32, 32, 0, 0),
    M("load", "LOAD", 0x5813, 1, 0, 0, NU_PCT, NB_LEFT, NR_NONE, 0, 0.75, 0, 0, 200, 0, 191.25, 0, 0),
    M("air_mass", "AIR MASS", 0x4ABC, 2, 0, 0, NU_KGH, NB_LEFT, NR_NONE, 0, 0.1, 0, 0, 1000, 0, 3000, 0, 0),
    M("torque", "TORQUE", 0x5891, 2, 0, NF_SIGNED, NU_NM, NB_LEFT, NR_NONE, 0, 0.5, 0, 0, 700, -1000, 2000, 0, 0),
    M("rpm", "RPM", 0x4807, 2, 0, 0, NU_RPM, NB_LEFT, NR_NONE, 0, 0.25, 0, 0, 7000, 0, 16383, 0, 0),
    M("speed", "SPEED", 0x4AB1, 2, 0, 0, NU_KMH, NB_LEFT, NR_NONE, 0, 1.0 / 128, 0, 0, 260, 0, 400, 0, 0),
    M("battery", "BATTERY", 0x5853, 2, 0, 0, NU_VOLT, NB_LEFT, NR_BATTERY_V, 1000, 0.001, 0, 10, 16, 5, 20, 2, 2),
    M("battery_amps", "BATTERY AMPS", 0x5852, 2, 0, NF_NEGATE, NU_AMP, NB_ZERO, NR_BATTERY_A, 500, 0.02, -200, -60, 100, -250, 250, 1, 1),
    M("battery_pct", "BATTERY %", 0x4023, 18, 17, 0, NU_PCT, NB_LEFT, NR_BATTERY_PCT, 10000, 1, 0, 0, 100, 0, 100, 0, 0),
    M("start_margin", "START MARGIN", 0x460E, 2, 0, 0, NU_PCT, NB_LEFT, NR_START_MARGIN, 2000, 0.004, -100, 0, 60, -100, 100, 0, 0),
    M("battery_temp", "BATTERY TEMP", 0x5854, 1, 0, 0, NU_TEMP, NB_LEFT, NR_NONE, 5000, 0.75, -48, -30, 60, -48, 143.25, 1, 1),
    M("ambient", "AMBIENT", 0x5817, 1, 0, 0, NU_TEMP, NB_LEFT, NR_NONE, 5000, 0.75, -48, -30, 40, -48, 143.25, 1, 1),
    M("fuel_temp", "FUEL TEMP", 0x4AD5, 1, 0, 0, NU_TEMP, NB_LEFT, NR_NONE, 5000, 0.75, -48, 0, 80, -48, 143.25, 1, 1),
    M("exhaust", "EXHAUST TEMP", 0x5896, 2, 0, 0, NU_TEMP, NB_LEFT, NR_NONE, 1000, 0.0234375, -273.15, 200, 1000, -60, 1300, 0, 0),
};

/* The layout before tiles were configurable: top row Boost, Timing, Oil temp, Oil pressure;
 * bottom row AFR, Intake air, HPFP, Coolant. Position p is column p / 2, row p % 2. */
const int nm_default_tiles[NM_TILES] = {NM_BOOST, NM_AFR, NM_TIMING, NM_IAT, NM_OIL_TEMP, NM_HPFP,
                                        NM_OIL_PRESS, NM_COOLANT};

int nm_find(const char *key, size_t length) {
    unsigned i;
    for (i = 0; i < NM_COUNT; ++i)
        if (strlen(nm_metrics[i].key) == length && !memcmp(nm_metrics[i].key, key, length)) return (int)i;
    return -1;
}

unsigned nm_stale_ms(unsigned id) {
    const unsigned rate = id < NM_COUNT ? nm_metrics[id].rate_ms : 0;
    if (!rate) return 2500;
    return 2 * rate + 1000 < 3500 ? 3500 : 2 * rate + 1000;
}

int nm_decode(unsigned id, const unsigned char *p, size_t n, double *out) {
    const nm_metric *m;
    long raw;
    double x;
    if (id >= NM_COUNT || !p || !out) return 0;
    m = &nm_metrics[id];
    if (m->offset) {                       /* one byte of a block read */
        if (n <= m->offset || n > 64) return 0;
        raw = p[m->offset];
    } else {
        if (n != m->size) return 0;
        raw = m->size == 1 ? p[0] : ((long)p[0] << 8) | p[1];
        if (m->flags & NF_SIGNED) {
            if ((m->flags & NF_NO_FFFF) && raw == 0xFFFF) return 0;
            if (m->size == 2 && raw >= 0x8000) raw -= 0x10000;
            if (m->size == 1 && raw >= 0x80) raw -= 0x100;
        } else if (!(m->flags & NF_OBD) && raw == (m->size == 1 ? 0xFF : 0xFFFF) && m->rule != NR_KNOCK) {
            return 0;                      /* all ones: no value */
        }
    }
    x = raw * m->mul + m->add;
    if (m->flags & NF_LAMBDA) {
        if (x < 0.5 || x > 2) return 0;
        x *= 14.7;
    }
    if (m->flags & NF_TRIM) x = (x - 1) * 100;
    if (m->flags & NF_NEGATE) x = -x;
    if (!(m->flags & (NF_AMBIENT | NF_WASTEGATE)) && (x < m->vlo || x > m->vhi)) return 0;
    if (!isfinite(x)) return 0;
    *out = x;
    return 1;
}

int nm_wastegate_pct(double mm, double open_mm, double closed_mm, double *out) {
    const double span = open_mm - closed_mm;
    double x;
    if (!isfinite(mm) || !isfinite(span) || span < 2 || span > 40) return 0;
    x = (mm - closed_mm) / span * 100;
    if (x < -10 || x > 110) return 0;
    *out = x < 0 ? 0 : x > 100 ? 100 : x;
    return 1;
}

int nm_decode_ambient(const unsigned char *p, size_t n, double *hpa) {
    double x;
    if (!p || !hpa || n != 2) return 0;
    x = (((unsigned)p[0] << 8) | p[1]) * 0.0390625;
    if (x < 400 || x > 1150) return 0;
    *hpa = x;
    return 1;
}

int nm_decode_stop(const unsigned char *p, size_t n, double *mm) {
    long raw;
    if (!p || !mm || n != 2) return 0;
    raw = ((long)p[0] << 8) | p[1];
    if (raw >= 0x8000) raw -= 0x10000;
    *mm = raw * 0.00097656;
    return 1;
}

int nm_gauge(unsigned id, double hpa, double ambient_hpa, double *out) {
    double x;
    if (id >= NM_COUNT || !(nm_metrics[id].flags & NF_AMBIENT)) return 0;
    x = (hpa - ambient_hpa) / 1000;
    if (!isfinite(x) || x < nm_metrics[id].vlo || x > nm_metrics[id].vhi) return 0;
    *out = x;
    return 1;
}

int nm_parse_tiles(const char *text, size_t length, int tiles[NM_TILES]) {
    int parsed[NM_TILES];
    size_t i = 0;
    unsigned count = 0, j;
    memcpy(tiles, nm_default_tiles, sizeof parsed);
    if (!text || length > 512) return 0;
    while (i < length) {
        size_t start;
        int metric;
        while (i < length && isspace((unsigned char)text[i])) ++i;
        if (i == length) break;
        start = i;
        while (i < length && !isspace((unsigned char)text[i])) ++i;
        if (count == NM_TILES) return 0;
        if (i - start == 1 && text[start] == '-') metric = NM_BLANK;
        else if ((metric = nm_find(text + start, i - start)) < 0) return 0;
        for (j = 0; metric != NM_BLANK && j < count; ++j)
            if (parsed[j] == metric) return 0;
        parsed[count++] = metric;
    }
    if (count != NM_TILES) return 0;
    memcpy(tiles, parsed, sizeof parsed);
    return 1;
}

int nm_format_tiles(const int tiles[NM_TILES], char *out, size_t size) {
    size_t used = 0;
    unsigned i;
    for (i = 0; i < NM_TILES; ++i) {
        const char *key = tiles[i] >= 0 && tiles[i] < NM_COUNT ? nm_metrics[tiles[i]].key : "-";
        int n = snprintf(out + used, size - used, "%s%s", i ? " " : "", key);
        if (n < 0 || (size_t)n >= size - used) return -1;
        used += (size_t)n;
    }
    if (size - used < 2) return -1;
    out[used++] = '\n';
    out[used] = 0;
    return (int)used;
}

void nm_assign(int tiles[NM_TILES], unsigned position, int metric) {
    unsigned i;
    if (position >= NM_TILES || metric < NM_BLANK || metric >= NM_COUNT) return;
    for (i = 0; metric != NM_BLANK && i < NM_TILES; ++i)
        if (i != position && tiles[i] == metric) tiles[i] = tiles[position];
    tiles[position] = metric;
}

static unsigned add_unique(int *list, unsigned count, int metric) {
    unsigned i;
    for (i = 0; i < count; ++i) if (list[i] == metric) return count;
    list[count] = metric;
    return count + 1;
}

unsigned nm_wanted(const int tiles[NM_TILES], int wanted[NM_MAX_FRAME]) {
    unsigned i, count = 0;
    int battery = 0;
    for (i = 0; i < NM_TILES; ++i) {
        const int m = tiles[i];
        if (m < 0 || m >= NM_COUNT) continue;
        count = add_unique(wanted, count, m);
        if (nm_metrics[m].rule == NR_BATTERY_V || nm_metrics[m].rule == NR_BATTERY_A) battery = 1;
    }
    count = add_unique(wanted, count, NM_OIL_TEMP);
    if (battery) count = add_unique(wanted, count, NM_RPM);
    return count;
}

static unsigned add_source(nm_source *out, unsigned count, unsigned did, unsigned size, unsigned flags,
                           unsigned rate) {
    unsigned i;
    for (i = 0; i < count; ++i) {
        if (out[i].did == did && (out[i].flags & NF_OBD) == (flags & NF_OBD)) {
            if (rate < out[i].rate_ms) out[i].rate_ms = (unsigned short)rate;
            return count;
        }
    }
    if (count == NM_MAX_SOURCES) return count;
    out[count].did = did;
    out[count].size = (unsigned char)size;
    out[count].flags = (unsigned char)flags;
    out[count].rate_ms = (unsigned short)rate;
    return count + 1;
}

unsigned nm_sources(const int *wanted, unsigned count, nm_source out[NM_MAX_SOURCES]) {
    unsigned i, n = 0;
    int ambient = 0, wastegate = 0;
    for (i = 0; i < count; ++i) {
        if (wanted[i] < 0 || wanted[i] >= NM_COUNT) continue;
        if (nm_metrics[wanted[i]].flags & NF_AMBIENT) ambient = 1;
        if (nm_metrics[wanted[i]].flags & NF_WASTEGATE) wastegate = 1;
    }
    /* Helpers first, so the values that need them decode in the same scan. */
    if (ambient) n = add_source(out, n, NM_AMBIENT_DID, 2, 0, NM_AMBIENT_MS);
    if (wastegate) {
        n = add_source(out, n, NM_WG_OPEN_DID, 2, NF_SIGNED, NM_STOPS_MS);
        n = add_source(out, n, NM_WG_CLOSED_DID, 2, NF_SIGNED, NM_STOPS_MS);
    }
    for (i = 0; i < count; ++i) {
        const nm_metric *m;
        if (wanted[i] < 0 || wanted[i] >= NM_COUNT) continue;
        m = &nm_metrics[wanted[i]];
        n = add_source(out, n, m->did, m->offset ? 0 : m->size, m->flags & NF_OBD, m->rate_ms);
    }
    return n;
}

void nm_tracker_init(nm_tracker *t) { memset(t, 0, sizeof *t); }

double nm_track(nm_tracker *t, unsigned id, double value) {
    if (id >= NM_COUNT || !isfinite(value)) return 0;
    if (nm_metrics[id].rule == NR_KNOCK) {
        const int on = value >= 0.5;
        if (on && !(t->seen[id] && t->last[id] >= 0.5)) ++t->knocks;
        t->seen[id] = 1;
        t->last[id] = value;
        return t->knocks;
    }
    if (nm_metrics[id].rule == NR_MISFIRE) {
        /* The DME counts misfires in a window and starts again at 0: a drop adds the window's count. */
        if (t->seen[id] && value < t->last[id]) t->carried[id] += t->last[id];
        t->seen[id] = 1;
        t->last[id] = value;
        return t->carried[id] + value;
    }
    return 0;
}

int nm_encode(const nm_frame *f, char *out, size_t size) {
    size_t used;
    unsigned i;
    int n;
    if (!f || !out || f->count > NM_MAX_FRAME) return -1;
    n = snprintf(out, size, "NBN1 %lu %llu %u", (unsigned long)f->seq, (unsigned long long)f->sent_ms, f->count);
    if (n < 0 || (size_t)n >= size) return -1;
    used = (size_t)n;
    for (i = 0; i < f->count; ++i) {
        const nm_entry *e = &f->entry[i];
        n = snprintf(out + used, size - used, " %u %.4f %.0f %u", e->id, e->value, e->aux,
                     e->age_ms > 999999 ? 999999 : e->age_ms);
        if (n < 0 || (size_t)n >= size - used) return -1;
        used += (size_t)n;
    }
    if (size - used < 2) return -1;
    out[used++] = '\n';
    out[used] = 0;
    return (int)used;
}

static int parse_unsigned(const char **cursor, unsigned long long max, unsigned long long *out) {
    const char *p = *cursor;
    char *end;
    unsigned long long value;
    if (*p != ' ' || !isdigit((unsigned char)p[1])) return 0;
    errno = 0;
    value = strtoull(p + 1, &end, 10);
    if (errno == ERANGE || value > max || (*end && *end != ' ' && *end != '\n')) return 0;
    *cursor = end;
    *out = value;
    return 1;
}

static int parse_double(const char **cursor, double *out) {
    const char *p = *cursor;
    char *end;
    double value;
    if (*p != ' ' || !(isdigit((unsigned char)p[1]) || p[1] == '-')) return 0;
    errno = 0;
    value = strtod(p + 1, &end);
    if (errno == ERANGE || end == p + 1 || !isfinite(value) || fabs(value) > 1e7 ||
        (*end && *end != ' ' && *end != '\n')) return 0;
    *cursor = end;
    *out = value;
    return 1;
}

int nm_parse(const char *data, size_t length, nm_frame *out) {
    char b[NM_MAX_PACKET];
    const char *cursor;
    nm_frame f;
    unsigned long long x;
    unsigned i;
    if (!data || !out || length < 5 || length >= sizeof b || memchr(data, 0, length)) return 0;
    memcpy(b, data, length);
    b[length] = 0;
    if (memcmp(b, "NBN1", 4)) return 0;
    memset(&f, 0, sizeof f);
    cursor = b + 4;
    if (!parse_unsigned(&cursor, UINT32_MAX, &x)) return 0;
    f.seq = (uint32_t)x;
    if (!parse_unsigned(&cursor, UINT64_MAX, &x)) return 0;
    f.sent_ms = (uint64_t)x;
    if (!parse_unsigned(&cursor, NM_MAX_FRAME, &x)) return 0;
    f.count = (unsigned)x;
    for (i = 0; i < f.count; ++i) {
        if (!parse_unsigned(&cursor, NM_COUNT - 1, &x)) return 0;
        f.entry[i].id = (unsigned)x;
        if (!parse_double(&cursor, &f.entry[i].value) || !parse_double(&cursor, &f.entry[i].aux) ||
            !parse_unsigned(&cursor, 999999, &x)) return 0;
        f.entry[i].age_ms = (unsigned)x;
    }
    if (*cursor == '\n') ++cursor;
    if (*cursor) return 0;
    *out = f;
    return 1;
}

static const char *unit_text(const nm_metric *m, int celsius, int bar) {
    switch (m->unit) {
    case NU_TEMP: return celsius ? "\260C" : "\260F";
    case NU_PRESS: return bar ? "bar" : "psi";
    case NU_PCT: return "%";
    case NU_DEG: return "deg";
    case NU_VOLT: return "V";
    case NU_AMP: return "A";
    case NU_KGH: return "kg/h";
    case NU_NM: return "Nm";
    case NU_RPM: return "rpm";
    case NU_KMH: return "km/h";
    default: return "";
    }
}

void nm_format_value(unsigned id, double value, double aux, int celsius, int bar, char *out, size_t size) {
    const nm_metric *m;
    const char *unit;
    double x = value;
    int decimals;
    if (!out || !size) return;
    if (id >= NM_COUNT || !isfinite(value)) { snprintf(out, size, "--"); return; }
    m = &nm_metrics[id];
    if (m->rule == NR_KNOCK || m->rule == NR_MISFIRE) {
        snprintf(out, size, "%.0f (%.0f)", value, aux);
        return;
    }
    decimals = m->decimals;
    if (m->unit == NU_TEMP && !celsius) { x = x * 1.8 + 32; decimals = m->decimals_alt; }
    if (m->unit == NU_PRESS) {
        if (bar) decimals = m->decimals_alt;
        else x *= PSI_PER_BAR;
    }
    if (fabs(x) < 0.5 * pow(10, -decimals)) x = 0;   /* never "-0.0" */
    unit = unit_text(m, celsius, bar);
    snprintf(out, size, m->bar == NB_ZERO && m->unit != NU_DEG ? "%+.*f%s%s" : "%.*f%s%s",
             decimals, x, *unit ? " " : "", unit);
}

static double fraction(const nm_metric *m, double x) {
    double f = (x - m->lo) / (m->hi - m->lo);
    return !isfinite(f) || f < 0 ? 0 : f > 1 ? 1 : f;
}

unsigned nm_bar_step(unsigned id, double value, unsigned steps) {
    if (id >= NM_COUNT) return 0;
    return (unsigned)(fraction(&nm_metrics[id], value) * steps + .5);
}

unsigned nm_zero_step(unsigned id, unsigned steps) { return nm_bar_step(id, 0, steps); }

int nm_zero_set(unsigned id) {
    unsigned steps[NM_COUNT], n = 0, i, j, mine;
    if (id >= NM_COUNT || nm_metrics[id].bar != NB_ZERO) return -1;
    mine = nm_zero_step(id, NM_STEPS);
    for (i = 0; i < NM_COUNT; ++i) {
        unsigned z;
        int known = 0;
        if (nm_metrics[i].bar != NB_ZERO) continue;
        z = nm_zero_step(i, NM_STEPS);
        for (j = 0; j < n; ++j) if (steps[j] == z) known = 1;
        if (!known) steps[n++] = z;
    }
    for (i = 0, j = 0; i < n; ++i) if (steps[i] < mine) ++j;
    return (int)j;
}

void nm_display_name(unsigned id, char *out, size_t size) {
    static const char *const acronyms[] = {"AFR", "HPFP", "LPFP", "RPM"};
    const char *p;
    size_t used = 0;
    if (!out || !size) return;
    out[0] = 0;
    if (id >= NM_COUNT) return;
    p = nm_metrics[id].caption;
    while (*p && used + 1 < size) {
        const char *end = strchr(p, ' ');
        const size_t n = end ? (size_t)(end - p) : strlen(p);
        size_t k, a;
        int keep = 0;
        for (a = 0; a < sizeof acronyms / sizeof acronyms[0]; ++a)
            if (strlen(acronyms[a]) == n && !memcmp(acronyms[a], p, n)) keep = 1;
        for (k = 0; k < n && used + 1 < size; ++k) {
            const char ch = p[k];
            out[used++] = keep || k == 0 || ch < 'A' || ch > 'Z' ? ch : (char)(ch - 'A' + 'a');
        }
        if (!end) break;
        if (used + 1 < size) out[used++] = ' ';
        p = end + 1;
    }
    out[used] = 0;
}

double nm_knock_fraction(uint64_t since_ms) {
    return since_ms >= NM_KNOCK_FADE_MS ? 0 : 1 - (double)since_ms / NM_KNOCK_FADE_MS;
}

int nm_battery_v_red(double volts, int running) {
    return running ? volts < 12.0 || volts > 15.5 : volts < 12.0;
}
int nm_battery_pct_red(double pct) { return pct < 40; }
int nm_start_margin_red(double pct) { return pct < 5; }

int nm_battery_a_red(double amps, int running, uint64_t now_ms, uint64_t *since) {
    if (!running || amps >= -10) { *since = 0; return 0; }
    if (!*since) *since = now_ms ? now_ms : 1;
    return now_ms - *since >= NM_DISCHARGE_MS;
}
