#ifndef DID_PAIR_H
#define DID_PAIR_H
/* Two DIDs in one UDS 22 request. The F87 M2 N55 DME (mevd172y) answers two per request in the
 * time of one; three or more are refused. The reply is 62, then each DID followed by its data. */
#include "diag_link.h"

/* 1 both read, -2 refused (negative reply), -1 link lost, -3 this request failed but the link is kept,
 * 0 a reply that does not match the requested DIDs and sizes. p1 and p2 point into reply. */
static int read_did_pair(struct diag_link *link, unsigned ecu, unsigned d1, unsigned n1,
                         unsigned d2, unsigned n2, unsigned char *reply, unsigned cap,
                         const unsigned char **p1, const unsigned char **p2) {
    const unsigned char request[] = {0x22, (unsigned char)(d1 >> 8), (unsigned char)d1,
                                     (unsigned char)(d2 >> 8), (unsigned char)d2};
    const unsigned char prefix[] = {0x62, request[1], request[2]};
    unsigned negative;
    int n = diag_link_request(link, ecu, request, sizeof request, prefix, sizeof prefix,
                              reply, cap, 500, &negative);
    if (n == -2) return -2;
    if (n == -1 || n == -3) return n;
    if (n != (int)(3 + n1 + 2 + n2) || reply[3 + n1] != request[3] || reply[4 + n1] != request[4])
        return 0;
    *p1 = reply + 3;
    *p2 = reply + 5 + n1;
    return 1;
}
#endif
