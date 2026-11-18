/* Engine Fault Codes list rows and detail pages. */
#include "fault_text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char a[96], b[96];
    unsigned code, status;
    const char *line = "213901  status 2F\nnext";
    assert(ft_parse(line, 17, &code, &status) && code == 0x213901 && status == 0x2F);
    assert(!ft_parse("Engine: 1 code", 14, &code, &status));
    assert(!strcmp(ft_status_word(0x2F), "Active") && !strcmp(ft_status_word(0x0C), "Pending") &&
           !strcmp(ft_status_word(0x08), "Stored"));
    ft_status_detail(0x2F, a, sizeof a);   /* the car's 213901 */
    assert(!strcmp(a, "Failing now, confirmed, pending, failed this drive, failed since last clear"));
    ft_status_detail(0x00, a, sizeof a);
    assert(!strcmp(a, "Stored, not failing now"));
    ft_status_detail(0xFF, a, 24);         /* stops before overflowing */
    assert(strlen(a) < 24);
    /* The list summary is cut at a word with "..." instead of mid-word, as the car showed. */
    ft_shorten("Active \267 Power management: consumer reduction active", FT_ROW_CHARS, a, sizeof a);
    assert(!strcmp(a, "Active \267 Power management: consumer...") && strlen(a) <= FT_ROW_CHARS);
    ft_shorten("Short text", FT_ROW_CHARS, a, sizeof a);
    assert(!strcmp(a, "Short text"));
    ft_shorten("Averyveryveryverylongwordwithoutanyspacesatallinit", 20, a, sizeof a);
    assert(strlen(a) == 20 && !strcmp(a + 17, "..."));
    /* Detail lines: split at a space, nothing lost for N55 texts up to 70 characters. */
    ft_wrap("Power management: consumer reduction active", FT_LINE_CHARS, a, sizeof a, b, sizeof b);
    assert(!strcmp(a, "Power management: consumer reduction active") && !b[0]);
    ft_wrap("Valvetronic actuator motor, position sensors, plausibility: signals implausible",
            FT_LINE_CHARS, a, sizeof a, b, sizeof b);
    assert(strlen(a) <= FT_LINE_CHARS && a[strlen(a) - 1] != ' ' && b[0] != ' ');
    assert(!strcmp(a, "Valvetronic actuator motor, position sensors,") && !strcmp(b, "plausibility: signals implausible"));
    puts("Fault text: rows, status words, shortening and wrapping passed.");
    return 0;
}
