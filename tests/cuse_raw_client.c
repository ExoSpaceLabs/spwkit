// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include "cuse/vspw_cuse_record.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RECORD_BUFFER_SIZE 256u

static int write_record(int fd,
                        uint8_t type,
                        uint8_t flags,
                        const uint8_t* payload,
                        size_t payload_size) {
    uint8_t buffer[RECORD_BUFFER_SIZE];
    vspw_cuse_record_header_t header;
    size_t total_size;
    ssize_t written;

    if (payload_size > sizeof(buffer) - VSPW_CUSE_RECORD_HEADER_SIZE) {
        return 0;
    }
    header.type = type;
    header.flags = flags;
    header.payload_size = (uint32_t)payload_size;
    if (vspw_cuse_record_encode_header(&header, buffer) != VSPW_CUSE_RECORD_OK) {
        return 0;
    }
    if (payload_size != 0u) {
        memcpy(buffer + VSPW_CUSE_RECORD_HEADER_SIZE, payload, payload_size);
    }
    total_size = VSPW_CUSE_RECORD_HEADER_SIZE + payload_size;
    written = write(fd, buffer, total_size);
    return written == (ssize_t)total_size;
}

static int parse_record(const uint8_t* buffer,
                        size_t size,
                        uint8_t expected_type,
                        uint8_t expected_flags,
                        const uint8_t* expected_payload,
                        size_t expected_payload_size) {
    vspw_cuse_record_header_t header;
    const uint8_t* payload;

    if (size < VSPW_CUSE_RECORD_HEADER_SIZE ||
        vspw_cuse_record_decode_header(buffer, &header) != VSPW_CUSE_RECORD_OK) {
        return 0;
    }
    payload = buffer + VSPW_CUSE_RECORD_HEADER_SIZE;
    if (header.type != expected_type || header.flags != expected_flags ||
        header.payload_size != expected_payload_size ||
        size != VSPW_CUSE_RECORD_HEADER_SIZE + expected_payload_size ||
        vspw_cuse_record_validate_payload(
            &header,
            expected_payload_size == 0u ? NULL : payload,
            expected_payload_size) != VSPW_CUSE_RECORD_OK) {
        return 0;
    }
    return expected_payload_size == 0u ||
           memcmp(payload, expected_payload, expected_payload_size) == 0;
}

static double monotonic_seconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0.0;
    }
    return (double)now.tv_sec + (double)now.tv_nsec / 1000000000.0;
}

int main(int argc, char** argv) {
    static const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    static const uint8_t world[] = {'w', 'o', 'r', 'l', 'd'};
    static const uint8_t time_code[] = {7u, 0u};
    uint8_t buffer[RECORD_BUFFER_SIZE];
    struct pollfd descriptor;
    int fd = -1;
    int second_fd;
    int flags;
    ssize_t received;
    double before;
    double after;

    if (argc != 2) {
        fprintf(stderr, "usage: %s /dev/vspwX\n", argv[0]);
        return 2;
    }

    fd = open(argv[1], O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open CUSE device");
        return 1;
    }

    errno = 0;
    second_fd = open(argv[1], O_RDWR | O_NONBLOCK);
    if (second_fd >= 0 || errno != EBUSY) {
        fprintf(stderr, "second CUSE open did not fail with EBUSY\n");
        if (second_fd >= 0) {
            close(second_fd);
        }
        close(fd);
        return 1;
    }

    errno = 0;
    received = read(fd, buffer, sizeof(buffer));
    if (received >= 0 || errno != EAGAIN) {
        fprintf(stderr, "empty nonblocking read did not return EAGAIN\n");
        close(fd);
        return 1;
    }

    if (!write_record(fd,
                      VSPW_CUSE_RECORD_DATA,
                      0u,
                      hello,
                      sizeof(hello)) ||
        !write_record(fd,
                      VSPW_CUSE_RECORD_TIME_CODE,
                      0u,
                      time_code,
                      sizeof(time_code)) ||
        !write_record(fd,
                      VSPW_CUSE_RECORD_DATA,
                      VSPW_CUSE_RECORD_FLAG_EEP,
                      NULL,
                      0u)) {
        fprintf(stderr, "failed to write outbound CUSE records\n");
        close(fd);
        return 1;
    }

    descriptor.fd = fd;
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    if (poll(&descriptor, 1u, 3000) != 1 ||
        (descriptor.revents & POLLIN) == 0) {
        fprintf(stderr, "CUSE poll did not report inbound record readiness\n");
        close(fd);
        return 1;
    }

    errno = 0;
    received = read(fd, buffer, 8u);
    if (received >= 0 || errno != EMSGSIZE) {
        fprintf(stderr, "short CUSE read did not return EMSGSIZE\n");
        close(fd);
        return 1;
    }

    descriptor.revents = 0;
    if (poll(&descriptor, 1u, 0) != 1 ||
        (descriptor.revents & POLLIN) == 0) {
        fprintf(stderr, "short read consumed poll readiness\n");
        close(fd);
        return 1;
    }

    received = read(fd, buffer, sizeof(buffer));
    if (received < 0 ||
        !parse_record(buffer,
                      (size_t)received,
                      VSPW_CUSE_RECORD_DATA,
                      VSPW_CUSE_RECORD_FLAG_EEP,
                      world,
                      sizeof(world))) {
        fprintf(stderr, "failed to receive expected EEP DATA record\n");
        close(fd);
        return 1;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        perror("clear O_NONBLOCK");
        close(fd);
        return 1;
    }

    before = monotonic_seconds();
    received = read(fd, buffer, sizeof(buffer));
    after = monotonic_seconds();
    if (received < 0 ||
        !parse_record(buffer,
                      (size_t)received,
                      VSPW_CUSE_RECORD_DATA,
                      0u,
                      NULL,
                      0u)) {
        fprintf(stderr, "failed to receive zero-length EOP DATA record\n");
        close(fd);
        return 1;
    }
    if (after - before < 0.10) {
        fprintf(stderr, "blocking CUSE read returned before delayed peer DATA\n");
        close(fd);
        return 1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        perror("restore O_NONBLOCK");
        close(fd);
        return 1;
    }
    errno = 0;
    received = read(fd, buffer, sizeof(buffer));
    if (received >= 0 || errno != EAGAIN) {
        fprintf(stderr, "post-consume nonblocking read did not return EAGAIN\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
