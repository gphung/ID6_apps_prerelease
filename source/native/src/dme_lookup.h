#ifndef DME_LOOKUP_H
#define DME_LOOKUP_H
/* DME fault descriptions by profile: the reference 340i table, or the F87 M2 N55
 * (SGBD mevd172y) table selected by EngineCodes/profile.txt holding "n55". */
#include "dme_descriptions.h"
#include "dme_descriptions_n55.h"

static inline const char *dme_description(unsigned code, int n55) {
    const struct DmeDescription *table = n55 ? kN55DmeDescriptions : kDmeDescriptions;
    unsigned count = n55 ? sizeof kN55DmeDescriptions / sizeof kN55DmeDescriptions[0]
                         : sizeof kDmeDescriptions / sizeof kDmeDescriptions[0];
    unsigned i;
    for (i = 0; i < count; ++i) if (table[i].code == code) return table[i].text;
    return 0;
}
#endif
