// gateway-ip.txt: one to four IPv4 addresses, in order, as one comma-separated --host value.
#include "diagnostic_gateway.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char* host_from(const char* body, char* path, size_t size) {
    snprintf(path, size, "/tmp/id6-gateway-test-%d.txt", static_cast<int>(getpid()));
    FILE* f = fopen(path, "w");
    assert(f);
    fputs(body, f);
    fclose(f);
    return path;
}

int main() {
    char path[64];
    struct { const char* body; const char* host; } cases[] = {
        {"169.254.10.10\n", "169.254.10.10"},
        {"160.48.199.97 169.254.10.10\n", "160.48.199.97,169.254.10.10"},
        {"160.48.199.97\n169.254.10.10\nnot-an-ip 10.0.0.1 10.0.0.2 10.0.0.3\n",
         "160.48.199.97,169.254.10.10,10.0.0.1,10.0.0.2"},
        {"junk\n", "169.254.21.3"},
        {"", "169.254.21.3"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const DiagnosticGateway gateway(host_from(cases[i].body, path, sizeof path));
        assert(!strcmp(gateway.host(), cases[i].host));
        unlink(path);
    }
    assert(!strcmp(DiagnosticGateway("/nonexistent/gateway-ip.txt").host(), "169.254.21.3"));
    puts("Diagnostic gateway: address list and fallback passed.");
    return 0;
}
