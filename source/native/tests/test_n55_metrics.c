#include "n55_metrics.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double decode(unsigned id, const char *hex) {
    unsigned char p[64];
    size_t n = 0;
    double x = 0;
    while (hex[2 * n] && n < sizeof p) { unsigned b; sscanf(hex + 2 * n, "%2x", &b); p[n++] = (unsigned char)b; }
    assert(nm_decode(id, p, n, &x));
    return x;
}
static int decodes(unsigned id, const char *hex) {
    unsigned char p[64];
    size_t n = 0;
    double x;
    while (hex[2 * n] && n < sizeof p) { unsigned b; sscanf(hex + 2 * n, "%2x", &b); p[n++] = (unsigned char)b; }
    return nm_decode(id, p, n, &x);
}
#define NEAR(a, b, eps) assert(fabs((a) - (b)) < (eps))

/* Replies read on the car, 2026-11-19 (warm idle, then engine off with loads on). */
static void test_car_replies(void) {
    NEAR(decode(NM_TIMING, "ffbc"), -6.8, 1e-9);
    NEAR(decode(NM_TIMING3, "ffb4"), -7.6, 1e-9);
    NEAR(decode(NM_BATTERY, "3ab5"), 15.029, 1e-9);
    NEAR(decode(NM_BATTERY_AMPS, "2308"), 20.64, 1e-6);     /* charging shows positive */
    NEAR(decode(NM_BATTERY_AMPS, "2908"), -10.08, 1e-6);    /* engine off: discharging */
    NEAR(decode(NM_BATTERY_PCT, "5b803a8445000500000000212222222222" "3a3c3e3b3d394133000000" "86"), 58, 1e-9);
    NEAR(decode(NM_START_MARGIN, "79c0"), 24.672, 1e-6);
    NEAR(decode(NM_EXHAUST, "55e3"), 242.170, 1e-3);
    NEAR(decode(NM_AMBIENT, "4d"), 9.75, 1e-9);
    NEAR(decode(NM_FUEL_TRIM, "830f"), 2.398, 1e-3);
    NEAR(decode(NM_AFR_TARGET, "1009"), 14.732, 1e-3);
    NEAR(decode(NM_LPFP, "1a90"), 6.8, 1e-9);
    NEAR(decode(NM_HPFP_TARGET, "3bd9"), 76.605, 1e-9);
    NEAR(decode(NM_TORQUE, "fff5"), -5.5, 1e-9);
    NEAR(decode(NM_RPM, "0a2e"), 651.5, 1e-9);
    NEAR(decode(NM_LOAD, "16"), 16.5, 1e-9);
    NEAR(decode(NM_MISFIRE1, "0001"), 1, 1e-9);
    NEAR(decode(NM_THROTTLE, "0000"), 0, 1e-9);
    NEAR(decode(NM_COOLANT, "7c"), 84, 1e-9);               /* OBD 01 05: A - 40 */
    NEAR(decode(NM_OIL_TEMP, "0300"), 76.8, 1e-9);
    {
        unsigned char a[] = {0x64, 0xa5}, t[] = {0x64, 0xac}, wg[] = {0xfc, 0xcd}, o[] = {0x56, 0x0c}, c[] = {0xf9, 0x76};
        double ambient, target, gauge, mm, open, closed, pct;
        assert(nm_decode_ambient(a, 2, &ambient));
        NEAR(ambient, 1006.445, 1e-3);
        target = decode(NM_BOOST_TARGET, "64ac");
        assert(nm_gauge(NM_BOOST_TARGET, target, ambient, &gauge));
        NEAR(gauge, 0.00027, 1e-5);                          /* idle: target is ambient */
        assert(nm_decode(NM_WASTEGATE, wg, 2, &mm) && nm_decode_stop(o, 2, &open) && nm_decode_stop(c, 2, &closed));
        assert(nm_wastegate_pct(mm, open, closed, &pct));
        NEAR(pct, 3.6, 0.1);
        assert(!nm_wastegate_pct(mm, 0, 0, &pct));           /* stops not learned */
        (void)t;
    }
}

static void test_unavailable(void) {
    assert(!decodes(NM_TIMING, "ffff"));                     /* ambiguous FFFF */
    assert(!decodes(NM_HPFP, "ffff"));
    assert(!decodes(NM_IAT, "ff"));
    assert(!decodes(NM_AFR, "0000"));                        /* lambda 0: engine off */
    assert(!decodes(NM_BATTERY, "00"));                      /* wrong length */
    assert(!decodes(NM_BATTERY_PCT, "5b80"));                /* block too short */
    assert(!decodes(NM_KNOCK, "02"));
    assert(decodes(NM_KNOCK, "01"));
}

static void test_tiles(void) {
    int tiles[NM_TILES];
    char text[256];
    const char saved[] = "boost timing1 knock - battery timing throttle misfire3\n";
    assert(!nm_parse_tiles("", 0, tiles) && !memcmp(tiles, nm_default_tiles, sizeof tiles));
    assert(nm_parse_tiles(saved, strlen(saved), tiles));
    assert(tiles[1] == NM_TIMING1 && tiles[3] == NM_BLANK && tiles[5] == NM_TIMING);
    assert(nm_format_tiles(tiles, text, sizeof text) > 0 && !strcmp(text, saved));
    assert(!nm_parse_tiles("boost boost - - - - - -", 23, tiles) && tiles[0] == NM_BOOST && tiles[1] == NM_AFR);
    assert(!nm_parse_tiles("boost nope - - - - - -", 22, tiles));
    assert(!nm_parse_tiles("- - - - - - -", 13, tiles));
    assert(nm_parse_tiles("- - - - - - - -", 15, tiles));
    /* Picking a metric another tile shows swaps the two. */
    memcpy(tiles, nm_default_tiles, sizeof tiles);
    nm_assign(tiles, 0, NM_COOLANT);
    assert(tiles[0] == NM_COOLANT && tiles[7] == NM_BOOST);
    nm_assign(tiles, 3, NM_BLANK);
    assert(tiles[3] == NM_BLANK);
    nm_assign(tiles, 2, NM_THROTTLE);
    assert(tiles[2] == NM_THROTTLE);
}

static void test_sources(void) {
    int tiles[NM_TILES] = {NM_TIMING, NM_TIMING1, NM_BOOST, NM_BATTERY, NM_WASTEGATE, NM_BLANK, NM_OIL_PRESS, NM_KNOCK};
    int wanted[NM_MAX_FRAME];
    unsigned rates[NM_MAX_FRAME];
    nm_source s[NM_MAX_SOURCES];
    unsigned n = nm_wanted(tiles, wanted, rates), k, i, timing = 0, ambient = 0;
    assert(n == 9);                                          /* 7 tiles + oil temperature + RPM */
    assert(wanted[8] == NM_RPM && rates[8] == NM_HELPER_MS && rates[0] == 0 && rates[7] == 0);
    k = nm_sources(wanted, rates, n, s);
    for (i = 0; i < k; ++i) if (s[i].did == 0x4807) assert(s[i].rate_ms == NM_HELPER_MS);
    for (i = 0; i < k; ++i) {
        if (s[i].did == 0x4A49) ++timing;
        if (s[i].did == NM_AMBIENT_DID) { ++ambient; assert(s[i].rate_ms == NM_AMBIENT_MS); }
    }
    assert(timing == 1 && ambient == 1);                     /* Timing and Timing 1 read once */
    assert(k == 11);                                         /* 8 DIDs + ambient + two end stops */
    {
        int all[NM_TILES] = {NM_TIMING1, NM_TIMING2, NM_TIMING3, NM_TIMING4, NM_TIMING5, NM_TIMING6, NM_THROTTLE, NM_KNOCK};
        int rpm_tile[NM_TILES] = {NM_BATTERY, NM_RPM, NM_BLANK, NM_BLANK, NM_BLANK, NM_BLANK, NM_BLANK, NM_BLANK};
        n = nm_wanted(all, wanted, rates);
        assert(nm_sources(wanted, rates, n, s) == 9);
        n = nm_wanted(rpm_tile, wanted, rates);              /* an RPM tile keeps every scan */
        assert(n == 3 && rates[1] == 0);
        k = nm_sources(wanted, rates, n, s);
        for (i = 0; i < k; ++i) if (s[i].did == 0x4807) assert(s[i].rate_ms == 0);
    }
}

static void test_tracker(void) {
    nm_tracker t;
    nm_tracker_init(&t);
    assert(nm_track(&t, NM_KNOCK, 0) == 0);
    assert(nm_track(&t, NM_KNOCK, 1) == 1);
    assert(nm_track(&t, NM_KNOCK, 1) == 1);                  /* one event, not one per sample */
    assert(nm_track(&t, NM_KNOCK, 0) == 1);
    assert(nm_track(&t, NM_KNOCK, 1) == 2);
    assert(nm_track(&t, NM_MISFIRE2, 3) == 3);
    assert(nm_track(&t, NM_MISFIRE2, 5) == 5);
    assert(nm_track(&t, NM_MISFIRE2, 0) == 5);               /* window ended */
    assert(nm_track(&t, NM_MISFIRE2, 2) == 7);
    assert(nm_track(&t, NM_BOOST, 1) == 0);
}

static void test_frames(void) {
    nm_frame f, g;
    char packet[NM_MAX_PACKET];
    unsigned i;
    memset(&f, 0, sizeof f);
    f.seq = 7; f.sent_ms = 123456789; f.count = NM_MAX_FRAME;
    for (i = 0; i < f.count; ++i) {
        f.entry[i].id = NM_COUNT - 1 - i;
        f.entry[i].value = -1234567.0 + i;
        f.entry[i].aux = 65534;
        f.entry[i].age_ms = 999999;
    }
    assert(nm_encode(&f, packet, sizeof packet) > 0);
    assert(nm_parse(packet, strlen(packet), &g));
    assert(g.count == f.count && g.seq == 7 && g.sent_ms == 123456789);
    NEAR(g.entry[3].value, f.entry[3].value, 1e-3);
    assert(!nm_parse("NBN1 1 2 1 99 0 0 0", 19, &g));        /* unknown metric */
    assert(!nm_parse("NBN1 1 2 13", 11, &g));                /* too many entries */
    assert(!nm_parse("NBN1 1 2 1 0 1.0 0", 18, &g));         /* short entry */
    assert(!nm_parse("NBN1 1 2 1 0 nan 0 0", 20, &g));
    assert(!nm_parse("NBN1 1 2 0 x", 12, &g));
    assert(nm_parse("NBN1 1 2 0\n", 11, &g) && g.count == 0);
    assert(!nm_parse("NBG4 1 2 0", 10, &g));
}

static void test_text_and_bars(void) {
    char s[64];
    nm_format_value(NM_BOOST, 1.0, 0, 1, 0, s, sizeof s);
    assert(!strcmp(s, "14.5 psi"));
    nm_format_value(NM_BOOST, -0.001, 0, 1, 1, s, sizeof s);
    assert(!strcmp(s, "0.00 bar"));
    nm_format_value(NM_OIL_TEMP, 100, 0, 0, 0, s, sizeof s);
    assert(!strcmp(s, "212.0 \260F"));
    nm_format_value(NM_TIMING2, -6.8, 0, 1, 0, s, sizeof s);
    assert(!strcmp(s, "-6.8 deg"));
    nm_format_value(NM_FUEL_TRIM, 2.4, 0, 1, 0, s, sizeof s);
    assert(!strcmp(s, "+2.4 %"));
    nm_format_value(NM_BATTERY_AMPS, -10.08, 0, 1, 0, s, sizeof s);
    assert(!strcmp(s, "-10.1 A"));
    nm_format_value(NM_KNOCK, 0, 3, 1, 0, s, sizeof s);
    assert(!strcmp(s, "0 (3)"));
    nm_format_value(NM_HPFP, 76.9, 0, 1, 0, s, sizeof s);
    assert(!strcmp(s, "1115 psi"));
    nm_format_value(NM_LPFP, 6.8, 0, 1, 1, s, sizeof s);
    assert(!strcmp(s, "6.8 bar"));
    nm_format_value(NM_BATTERY, 12.49, 0, 1, 0, s, sizeof s);
    assert(!strcmp(s, "12.49 V"));
    assert(nm_zero_step(NM_TIMING, 32) == 11);              /* -20..40: a third of the way */
    assert(nm_zero_step(NM_FUEL_TRIM, 32) == 16);
    assert(nm_zero_step(NM_BATTERY_AMPS, 32) == 12);
    assert(nm_bar_step(NM_TIMING, -30, 32) == 0 && nm_bar_step(NM_TIMING, 40, 32) == 32);
    assert(nm_bar_step(NM_THROTTLE, 50, 32) == 16);
    /* Image sets in ascending zero step, as tools/n55_table.py zero_sets(): 11, 12, 16. */
    assert(nm_zero_set(NM_TIMING) == 0 && nm_zero_set(NM_TIMING6) == 0);
    assert(nm_zero_set(NM_BATTERY_AMPS) == 1 && nm_zero_set(NM_FUEL_TRIM) == 2 && nm_zero_set(NM_BOOST) == -1);
    nm_display_name(NM_HPFP_TARGET, s, sizeof s);
    assert(!strcmp(s, "HPFP Target"));
    nm_display_name(NM_BATTERY_PCT, s, sizeof s);
    assert(!strcmp(s, "Battery %"));
    nm_display_name(NM_TIMING3, s, sizeof s);
    assert(!strcmp(s, "Timing 3"));
    nm_display_name(NM_AFR_TARGET, s, 4);
    assert(!strcmp(s, "AFR"));
}

static void test_rules(void) {
    uint64_t since = 0;
    assert(nm_battery_v_red(15.6, 1) && !nm_battery_v_red(15.14, 1) && nm_battery_v_red(11.9, 1));
    assert(!nm_battery_v_red(12.49, 0) && nm_battery_v_red(11.99, 0));
    assert(nm_battery_pct_red(39) && !nm_battery_pct_red(58));
    assert(nm_start_margin_red(4) && !nm_start_margin_red(24.7));
    assert(!nm_battery_a_red(-12, 1, 1000, &since) && since == 1000);
    assert(!nm_battery_a_red(-12, 1, 15999, &since));
    assert(nm_battery_a_red(-12, 1, 16000, &since));
    assert(!nm_battery_a_red(-9, 1, 17000, &since) && since == 0);     /* back under 10 A */
    assert(!nm_battery_a_red(-30, 0, 40000, &since));                  /* engine off: expected */
    NEAR(nm_knock_fraction(0), 1, 1e-9);
    NEAR(nm_knock_fraction(250), .75, 1e-9);
    assert(nm_knock_fraction(1000) == 0);
}

static void test_table(void) {
    unsigned i, j;
    for (i = 0; i < NM_COUNT; ++i) {
        const nm_metric *m = &nm_metrics[i];
        assert(m->key && m->caption && strlen(m->caption) <= 13 && m->hi > m->lo);
        assert(nm_find(m->key, strlen(m->key)) == (int)i);
        for (j = 0; m->caption[j]; ++j) assert(m->caption[j] == ' ' || m->caption[j] == '%' ||
                                               (m->caption[j] >= 'A' && m->caption[j] <= 'Z') ||
                                               (m->caption[j] >= '0' && m->caption[j] <= '9'));
        if (m->bar == NB_ZERO) assert(m->lo < 0 && m->hi > 0);
    }
}

int main(void) {
    test_car_replies();
    test_unavailable();
    test_tiles();
    test_sources();
    test_tracker();
    test_frames();
    test_text_and_bars();
    test_rules();
    test_table();
    puts("n55 metrics tests passed");
    return 0;
}
