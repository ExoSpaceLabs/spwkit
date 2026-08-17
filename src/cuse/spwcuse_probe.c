// SPDX-License-Identifier: Apache-2.0
#define FUSE_USE_VERSION 35

#include <cuse_lowlevel.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

static void probe_open(fuse_req_t req, struct fuse_file_info* fi) {
    fuse_reply_open(req, fi);
}

static void probe_read(fuse_req_t req,
                       size_t size,
                       off_t off,
                       struct fuse_file_info* fi) {
    (void)size;
    (void)off;
    (void)fi;
    fuse_reply_err(req, EOPNOTSUPP);
}

static void probe_write(fuse_req_t req,
                        const char* buf,
                        size_t size,
                        off_t off,
                        struct fuse_file_info* fi) {
    (void)buf;
    (void)size;
    (void)off;
    (void)fi;
    fuse_reply_err(req, EOPNOTSUPP);
}

static const struct cuse_lowlevel_ops probe_ops = {
    .open = probe_open,
    .read = probe_read,
    .write = probe_write,
};

static void usage(const char* program) {
    fprintf(stderr,
            "usage:\n"
            "  %s --api-check\n"
            "  %s --serve DEVICE_NAME\n\n"
            "--api-check validates the compiled libfuse3 CUSE surface without "
            "opening /dev/cuse.\n"
            "--serve creates a probe character device when the host provides "
            "/dev/cuse; read/write intentionally return EOPNOTSUPP.\n",
            program,
            program);
}

int main(int argc, char** argv) {
    struct cuse_info info;
    char dev_name[160];
    const char* dev_info_argv[1];
    char* cuse_argv[4];
    int written;

    if (argc == 2 && strcmp(argv[1], "--api-check") == 0) {
        printf("libfuse3 CUSE API probe OK (FUSE_USE_VERSION=%d)\n",
               FUSE_USE_VERSION);
        return 0;
    }
    if (argc != 3 || strcmp(argv[1], "--serve") != 0) {
        usage(argv[0]);
        return 2;
    }

    written = snprintf(dev_name, sizeof(dev_name), "DEVNAME=%s", argv[2]);
    if (written < 0 || (size_t)written >= sizeof(dev_name)) {
        fprintf(stderr, "device name is too long\n");
        return 2;
    }

    memset(&info, 0, sizeof(info));
    dev_info_argv[0] = dev_name;
    info.dev_info_argc = 1u;
    info.dev_info_argv = dev_info_argv;

    /*
     * Keep the probe single-threaded. A production presenter must not depend on
     * undocumented concurrent spw_port_* safety; it needs an explicit broker/
     * event-loop design before DATA callbacks are enabled.
     */
    cuse_argv[0] = argv[0];
    cuse_argv[1] = (char*)"-f";
    cuse_argv[2] = (char*)"-s";
    cuse_argv[3] = NULL;

    return cuse_lowlevel_main(3, cuse_argv, &info, &probe_ops, NULL);
}
