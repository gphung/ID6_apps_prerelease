#ifndef N55_METRICS_H
#define N55_METRICS_H
/* F87 M2 N55 (DME MEVD17.2.G, SGBD mevd172y) BoostDeck metrics: what each tile reads, how it is
 * decoded, how often it is read, and how it is shown. Shared by the collector and the plugin, so
 * plain C with no allocation. Scalings are the SGBD's SG_FUNKTIONEN entries (SIGNALS.md); live
 * values were checked on the car on 2026-11-18 and 2026-11-19. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum nm_unit { NU_NONE, NU_TEMP, NU_PRESS, NU_PCT, NU_DEG, NU_VOLT, NU_AMP, NU_KGH, NU_NM, NU_RPM, NU_KMH };
enum nm_bar { NB_LEFT, NB_ZERO };          /* fill from the left, or from the zero mark */
enum nm_rule { NR_NONE, NR_OIL_BAND, NR_KNOCK, NR_MISFIRE, NR_BATTERY_V, NR_BATTERY_A, NR_BATTERY_PCT,
               NR_START_MARGIN };
enum nm_flag {
    NF_SIGNED = 1, NF_AMBIENT = 2,      /* absolute hPa: minus ambient (4201), shown as gauge bar */
    NF_LAMBDA = 4,                      /* lambda x 14.7: gasoline-equivalent AFR */
    NF_TRIM = 8,                        /* controller factor: (x - 1) x 100 % */
    NF_NEGATE = 16,                     /* battery current: the IBS reports charging as negative */
    NF_WASTEGATE = 32,                  /* mm between the learned end stops: % open */
    NF_NO_FFFF = 64,                    /* FFFF is ambiguous (signed -0.1 or unavailable): unavailable */
    NF_OBD = 128                        /* did is an OBD mode 01 PID */
};

typedef struct {
    const char *key, *caption;
    unsigned did;
    unsigned char size, offset;         /* reply data bytes; offset of a 1-byte value in a block */
    unsigned char flags, unit, bar, rule;
    unsigned short rate_ms;             /* 0: every scan */
    double mul, add;                    /* raw x mul + add */
    double lo, hi;                      /* bar range, in the base unit (C, gauge bar, ...) */
    double vlo, vhi;                    /* plausible decoded values; outside is unavailable */
    unsigned char decimals, decimals_alt; /* text: C or psi, F or bar */
} nm_metric;

enum {
    NM_BOOST, NM_BOOST_TARGET, NM_AFR, NM_AFR_TARGET, NM_FUEL_TRIM, NM_THROTTLE, NM_TIMING,
    NM_TIMING1, NM_TIMING2, NM_TIMING3, NM_TIMING4, NM_TIMING5, NM_TIMING6, NM_KNOCK,
    NM_MISFIRE1, NM_MISFIRE2, NM_MISFIRE3, NM_MISFIRE4, NM_MISFIRE5, NM_MISFIRE6,
    NM_IAT, NM_OIL_TEMP, NM_COOLANT, NM_HPFP, NM_HPFP_TARGET, NM_LPFP, NM_OIL_PRESS, NM_WASTEGATE,
    NM_LOAD, NM_AIR_MASS, NM_TORQUE, NM_RPM, NM_SPEED, NM_BATTERY, NM_BATTERY_AMPS, NM_BATTERY_PCT,
    NM_START_MARGIN, NM_BATTERY_TEMP, NM_AMBIENT, NM_FUEL_TEMP, NM_EXHAUST,
    NM_COUNT
};
enum { NM_TILES = 8, NM_BLANK = -1,
       NM_MAX_FRAME = 12,               /* 8 tiles + oil temperature + RPM, deduplicated */
       NM_MAX_PACKET = 640,
       NM_AMBIENT_DID = 0x4201, NM_WG_OPEN_DID = 0x576A, NM_WG_CLOSED_DID = 0x576B,
       NM_STOPS_MS = 30000, NM_AMBIENT_MS = 1000 };

extern const nm_metric nm_metrics[NM_COUNT];
extern const int nm_default_tiles[NM_TILES];

int nm_find(const char *key, size_t length);     /* metric index, or -1 */
/* Stale after this long without a new value. */
unsigned nm_stale_ms(unsigned id);
/* Raw reply data (after 62 DID) to the base unit, before the ambient and wastegate steps.
 * 1 decoded, 0 unavailable. */
int nm_decode(unsigned id, const unsigned char *p, size_t n, double *out);
/* Wastegate mm between the end stops (open, closed) as % open; 0 when the stops are not usable. */
int nm_wastegate_pct(double mm, double open_mm, double closed_mm, double *out);
/* Ambient pressure 4201 in hPa, and a wastegate end stop (576A, 576B) in mm. */
int nm_decode_ambient(const unsigned char *p, size_t n, double *hpa);
int nm_decode_stop(const unsigned char *p, size_t n, double *mm);
/* An NF_AMBIENT metric's absolute hPa minus ambient hPa, as gauge bar within its plausible range. */
int nm_gauge(unsigned id, double hpa, double ambient_hpa, double *out);

/* tiles.txt: eight keys separated by spaces, "-" for a blank tile. Unknown keys, duplicates and a
 * short or long list give the default layout. 1 parsed, 0 default used. */
int nm_parse_tiles(const char *text, size_t length, int tiles[NM_TILES]);
int nm_format_tiles(const int tiles[NM_TILES], char *out, size_t size);
/* Put metric at position, swapping with the tile that already shows it. metric may be NM_BLANK. */
void nm_assign(int tiles[NM_TILES], unsigned position, int metric);

/* The metrics the collector delivers: each tile's metric once, oil temperature for the ENGINE
 * panel, and RPM when a battery tile needs to know whether the engine runs. Returns the count. */
unsigned nm_wanted(const int tiles[NM_TILES], int wanted[NM_MAX_FRAME]);

/* Read sources: one per distinct DID (or OBD PID), at the fastest rate any wanted metric needs,
 * plus ambient pressure and the wastegate end stops when needed. */
typedef struct { unsigned did; unsigned char size, flags; unsigned short rate_ms; } nm_source;
enum { NM_MAX_SOURCES = NM_MAX_FRAME + 3 };
unsigned nm_sources(const int *wanted, unsigned count, nm_source out[NM_MAX_SOURCES]);

/* Knock events and misfire totals, from each sample in order. */
typedef struct { double last[NM_COUNT], carried[NM_COUNT]; int seen[NM_COUNT]; unsigned knocks; } nm_tracker;
void nm_tracker_init(nm_tracker *t);
/* The second number a tile shows: knock events seen, or a cylinder's misfires this drive. */
double nm_track(nm_tracker *t, unsigned id, double value);

/* Collector to plugin over loopback UDP:
 * "NBN1 <seq> <sent_ms> <count>" then count times " <metric> <value> <aux> <age_ms>". */
typedef struct { unsigned id; double value, aux; unsigned age_ms; } nm_entry;
typedef struct { uint32_t seq; uint64_t sent_ms; unsigned count; nm_entry entry[NM_MAX_FRAME]; } nm_frame;
int nm_encode(const nm_frame *f, char *out, size_t size);
int nm_parse(const char *data, size_t length, nm_frame *out);

/* Text for a tile in the chosen units, Latin-1. */
void nm_format_value(unsigned id, double value, double aux, int celsius, int bar, char *out, size_t size);
/* Bar step 0..steps. For NB_ZERO bars also the zero mark's step. */
unsigned nm_bar_step(unsigned id, double value, unsigned steps);
unsigned nm_zero_step(unsigned id, unsigned steps);

/* Knock bar: full when a knock was just seen, empty NM_KNOCK_FADE_MS later. */
enum { NM_KNOCK_FADE_MS = 1000, NM_DISCHARGE_MS = 15000 };
double nm_knock_fraction(uint64_t since_ms);
/* Battery warnings. running: RPM above 400 and fresh. */
int nm_battery_v_red(double volts, int running);
int nm_battery_pct_red(double pct);
int nm_start_margin_red(double pct);
/* Battery current shown with charging positive: red after NM_DISCHARGE_MS of more than 10 A out
 * while running. *since is when the current discharge began (0: not discharging). */
int nm_battery_a_red(double amps, int running, uint64_t now_ms, uint64_t *since);

#ifdef __cplusplus
}
#endif
#endif
