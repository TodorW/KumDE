#include "kumde.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s [-h] [-v]\n", argv0);
    fprintf(stderr, "  -h  print this message\n");
    fprintf(stderr, "  -v  print version\n");
}

int main(int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "-v") == 0) {
            printf("kumde %s\n", KUM_VERSION);
            return 0;
        }
        fprintf(stderr, "unknown option: %s\n", argv[i]);
        usage(argv[0]);
        return 1;
    }

    struct kum_server server;
    kum_server_init(&server);
    kum_server_run(&server);
    kum_server_finish(&server);

    return 0;
}
