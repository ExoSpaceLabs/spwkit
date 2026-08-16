// SPDX-License-Identifier: Apache-2.0

#include "vspwd/server.h"

#include <stdio.h>
#include <string.h>

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH]\n"
            "\n"
            "Runs a two-port virtual SpaceWire daemon using VSPD over\n"
            "AF_UNIX/SOCK_SEQPACKET. Ports 0 and 1 are equal peers.\n",
            program);
}

int main(int argc, char** argv) {
    const char* socket_path = VSPWD_DEFAULT_SOCKET_PATH;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 ||
                   strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    return vspwd_run(socket_path);
}
