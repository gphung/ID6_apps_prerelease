/* Prints the metric table as the plugin sees it, for tests/test_n55_tiles.py. */
#include "n55_metrics.h"
#include <stdio.h>

int main(void) {
    unsigned i;
    for (i = 0; i < NM_COUNT; ++i) {
        char name[32];
        nm_display_name(i, name, sizeof name);
        printf("%s\t%s\t%s\t%d\t%u\n", nm_metrics[i].key, nm_metrics[i].caption, name, nm_zero_set(i),
               nm_zero_step(i, NM_STEPS));
    }
    return 0;
}
