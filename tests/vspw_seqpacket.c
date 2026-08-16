// SPDX-License-Identifier: Apache-2.0

#include "backends/device/vspw_device_protocol.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static vspd_header_t data_header(uint32_t payload_size) {
    vspd_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = VSPD_MSG_DATA_TX;
    header.flags = VSPD_FLAG_FRAGMENT_START | VSPD_FLAG_FRAGMENT_END | VSPD_FLAG_EOP;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = payload_size;
    header.request_id = 1u;
    header.port_id = 0u;
    header.message_id = 1u;
    header.total_size = payload_size;
    return header;
}

int main(void) {
    int sockets[2] = {-1, -1};
    struct pollfd descriptor;
    static uint8_t tx[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    static uint8_t rx[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    vspd_header_t header = data_header(VSPD_MAX_FRAME_PAYLOAD);
    vspd_header_t decoded;
    ssize_t count;
    int flags;
    size_t i;

    assert(socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets) == 0);

    flags = fcntl(sockets[1], F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(sockets[1], F_SETFL, flags | O_NONBLOCK) == 0);

    errno = 0;
    count = recv(sockets[1], rx, sizeof(rx), 0);
    assert(count == -1);
    assert(errno == EAGAIN || errno == EWOULDBLOCK);

    assert(vspd_encode_header(&header, tx) == VSPD_CODEC_OK);
    for (i = 0u; i < VSPD_MAX_FRAME_PAYLOAD; ++i) {
        tx[VSPD_HEADER_SIZE + i] = (uint8_t)(i & 0xffu);
    }

    assert(send(sockets[0], tx, sizeof(tx), MSG_NOSIGNAL) == (ssize_t)sizeof(tx));

    descriptor.fd = sockets[1];
    descriptor.events = POLLIN;
    descriptor.revents = 0;
    assert(poll(&descriptor, 1u, 1000) == 1);
    assert((descriptor.revents & POLLIN) != 0);

    count = recv(sockets[1], rx, sizeof(rx), 0);
    assert(count == (ssize_t)sizeof(rx));
    assert(vspd_validate_frame(rx, (size_t)count, &decoded) == VSPD_CODEC_OK);
    assert(decoded.payload_size == VSPD_MAX_FRAME_PAYLOAD);
    assert(memcmp(tx + VSPD_HEADER_SIZE,
                  rx + VSPD_HEADER_SIZE,
                  VSPD_MAX_FRAME_PAYLOAD) == 0);

    /* SOCK_SEQPACKET must preserve independent VSPD records. */
    header = data_header(1u);
    header.request_id = 2u;
    header.message_id = 2u;
    assert(vspd_encode_header(&header, tx) == VSPD_CODEC_OK);
    tx[VSPD_HEADER_SIZE] = 0xa5u;
    assert(send(sockets[0], tx, VSPD_HEADER_SIZE + 1u, MSG_NOSIGNAL) ==
           (ssize_t)(VSPD_HEADER_SIZE + 1u));

    header.request_id = 3u;
    header.message_id = 3u;
    assert(vspd_encode_header(&header, tx) == VSPD_CODEC_OK);
    tx[VSPD_HEADER_SIZE] = 0x5au;
    assert(send(sockets[0], tx, VSPD_HEADER_SIZE + 1u, MSG_NOSIGNAL) ==
           (ssize_t)(VSPD_HEADER_SIZE + 1u));

    count = recv(sockets[1], rx, sizeof(rx), 0);
    assert(count == (ssize_t)(VSPD_HEADER_SIZE + 1u));
    assert(vspd_validate_frame(rx, (size_t)count, &decoded) == VSPD_CODEC_OK);
    assert(decoded.request_id == 2u);
    assert(rx[VSPD_HEADER_SIZE] == 0xa5u);

    count = recv(sockets[1], rx, sizeof(rx), 0);
    assert(count == (ssize_t)(VSPD_HEADER_SIZE + 1u));
    assert(vspd_validate_frame(rx, (size_t)count, &decoded) == VSPD_CODEC_OK);
    assert(decoded.request_id == 3u);
    assert(rx[VSPD_HEADER_SIZE] == 0x5au);

    close(sockets[0]);
    sockets[0] = -1;

    descriptor.fd = sockets[1];
    descriptor.events = POLLIN | POLLHUP;
    descriptor.revents = 0;
    assert(poll(&descriptor, 1u, 1000) == 1);
    assert((descriptor.revents & (POLLIN | POLLHUP)) != 0);
    count = recv(sockets[1], rx, sizeof(rx), 0);
    assert(count == 0);

    close(sockets[1]);
    return 0;
}
