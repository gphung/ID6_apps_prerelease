#ifndef FAULT_TEXT_H
#define FAULT_TEXT_H
/* Engine Fault Codes text: the short list row and the per-code detail page. Plain C so host tests can
 * cover it. Text is Latin-1, as the HMI's Richtext models expect. */
#include <stdio.h>
#include <string.h>

/* Characters that fit, measured on the car: the list's description column shows about 39, a
 * full-width detail line about 52. */
enum { FT_ROW_CHARS = 38, FT_LINE_CHARS = 50 };

static inline const char *ft_status_word(unsigned status) {
    return (status & 0x01u) ? "Active" : (status & 0x04u) ? "Pending" : "Stored";
}

/* UDS DTC status bits in plain words, most important first. */
static inline void ft_status_detail(unsigned status, char *out, size_t size) {
    static const struct { unsigned bit; const char *text; } bits[] = {
        {0x01u, "failing now"}, {0x08u, "confirmed"}, {0x04u, "pending"},
        {0x02u, "failed this drive"}, {0x20u, "failed since last clear"}, {0x80u, "warning light requested"},
    };
    size_t used = 0, i;
    out[0] = 0;
    for (i = 0; i < sizeof bits / sizeof bits[0]; ++i) {
        int n;
        if (!(status & bits[i].bit)) continue;
        n = snprintf(out + used, size - used, "%s%s", used ? ", " : "", bits[i].text);
        if (n < 0 || (size_t)n >= size - used) break;
        used += (size_t)n;
    }
    if (!used) snprintf(out, size, "stored, not failing now");
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
}

/* text in at most max characters, cut at a word boundary and ending in "..." when it is longer. */
static inline void ft_shorten(const char *text, size_t max, char *out, size_t size) {
    size_t len = strlen(text), cut;
    if (len <= max || max < 4) { snprintf(out, size, "%.*s", (int)(len < max ? len : max), text); return; }
    cut = max - 3;
    while (cut > max / 2 && text[cut] != ' ') --cut;   /* the last space that leaves room for "..." */
    if (text[cut] != ' ') cut = max - 3;
    while (cut && (text[cut - 1] == ' ' || text[cut - 1] == ',' || text[cut - 1] == ':')) --cut;
    snprintf(out, size, "%.*s...", (int)cut, text);
}

/* text over two lines of at most width characters, split at a space; the second is shortened. */
static inline void ft_wrap(const char *text, size_t width, char *first, size_t first_size, char *second, size_t second_size) {
    size_t len = strlen(text), cut;
    second[0] = 0;
    if (len <= width) { snprintf(first, first_size, "%s", text); return; }
    cut = width;
    while (cut && text[cut] != ' ') --cut;
    if (!cut) cut = width;
    snprintf(first, first_size, "%.*s", (int)cut, text);
    while (text[cut] == ' ') ++cut;
    ft_shorten(text + cut, width, second, second_size);
}

/* A worker line "CCCCCC  status SS": 1 with code and status, otherwise 0. */
static inline int ft_parse(const char *line, int length, unsigned *code, unsigned *status) {
    int used = 0;
    return length > 0 && sscanf(line, "%6x  status %2x%n", code, status, &used) == 2 && used == length;
}
#endif
