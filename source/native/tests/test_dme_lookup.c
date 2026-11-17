/* EngineCodes description tables: reference 340i and F87 M2 N55 (mevd172y). */
#include "dme_lookup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    unsigned i, n55 = sizeof kN55DmeDescriptions / sizeof kN55DmeDescriptions[0];
    assert(n55 == 1378);
    for (i = 0; i < n55; ++i) {
        assert(kN55DmeDescriptions[i].code <= 0xFFFFFF);
        assert(kN55DmeDescriptions[i].text && *kN55DmeDescriptions[i].text);
        assert(strlen(kN55DmeDescriptions[i].text) <= 70);
        if (i) assert(kN55DmeDescriptions[i].code > kN55DmeDescriptions[i-1].code);
        assert(dme_description(kN55DmeDescriptions[i].code, 1) == kN55DmeDescriptions[i].text);
    }
    /* N55-only code: Valvetronic (the B58 has none), absent from the 340i table. */
    assert(!strcmp(dme_description(0x133010, 1), "Valvetronic actuator motor, control: malfunction"));
    assert(dme_description(0x133010, 0) == 0);
    /* Shared code: both tables describe knock sensor 1 (same German source text). */
    assert(dme_description(0x168A30, 0) != 0 && dme_description(0x168A30, 1) != 0);
    /* 340i-only code stays out of the N55 table. */
    assert(dme_description(0x100213, 0) != 0 && dme_description(0x100213, 1) == 0);
    /* Unknown codes have no text in either table. */
    assert(dme_description(0xABCDEF, 0) == 0 && dme_description(0xABCDEF, 1) == 0);
    puts("DME descriptions: N55 mevd172y table and profile lookup passed.");
    return 0;
}
