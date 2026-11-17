#ifndef ID6_DIAGNOSTIC_GATEWAY_H
#define ID6_DIAGNOSTIC_GATEWAY_H

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

// gateway-ip.txt holds one to four IPv4 addresses, separated by spaces or new lines, tried in order
// (for example the internal gateway address, then its ENET link-local address). The workers take them
// as one comma-separated --host value. Invalid entries are skipped; with none, the reference address.
class DiagnosticGateway {
    char host_[4 * 16];

public:
    explicit DiagnosticGateway(const char* settings) {
        strcpy(host_, "169.254.21.3");
        FILE* file = fopen(settings, "r");
        if (!file) return;
        char list[sizeof host_] = "", candidate[16];
        in_addr address;
        for (unsigned count = 0; count < 4 && fscanf(file, "%15s", candidate) == 1;) {
            if (inet_pton(AF_INET, candidate, &address) != 1) continue;
            if (count++) strcat(list, ",");
            strcat(list, candidate);
        }
        if (*list) strcpy(host_, list);
        fclose(file);
    }

    const char* host() const { return host_; }
};
#endif
