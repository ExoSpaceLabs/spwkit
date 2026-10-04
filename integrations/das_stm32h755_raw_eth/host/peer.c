// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE

#include <spwkit/raw_ethernet.h>
#include <spwkit/spwkit.h>

#include <arpa/inet.h>
#include <errno.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PEER_FRAME_CAPACITY 1518u
#define PEER_MAX_PACKET 4096u
#define PEER_MAX_ITERATIONS 4096u
#define PEER_LINK_ID UINT32_C(0x44534153)

typedef struct packet_context {
    char interface_name[IFNAMSIZ];
    int fd;
    int ifindex;
    uint8_t mac[SPW_RAW_ETHERNET_MAC_SIZE];
    bool started;
    uint64_t tx_frames;
    uint64_t tx_bytes;
    uint64_t rx_frames;
    uint64_t rx_bytes;
} packet_context_t;

static const uint8_t BOARD_MAC[SPW_RAW_ETHERNET_MAC_SIZE] = {
    0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u
};
static const uint8_t DONE_PACKET[8] = {
    'S', 'P', 'W', 'D', 'O', 'N', 'E', '1'
};
static uint8_t g_tx[PEER_MAX_PACKET];
static uint8_t g_rx[PEER_MAX_PACKET];
static uint64_t g_samples[PEER_MAX_ITERATIONS];

static int timeout_ms(spw_timeout_us_t timeout_us) {
    uint64_t rounded;
    if (timeout_us == SPW_TIMEOUT_INFINITE) return -1;
    rounded = (timeout_us + 999u) / 1000u;
    return rounded > INT32_MAX ? INT32_MAX : (int)rounded;
}

static int wait_fd(int fd, short events, spw_timeout_us_t timeout_us) {
    struct pollfd descriptor = {fd, events, 0};
    int result;
    do {
        result = poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & events) != 0;
}

static int query_interface(packet_context_t* context) {
    struct ifreq request;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return 0;

    memset(&request, 0, sizeof(request));
    (void)snprintf(request.ifr_name, sizeof(request.ifr_name), "%s",
                   context->interface_name);
    if (ioctl(fd, SIOCGIFINDEX, &request) != 0) {
        close(fd);
        return 0;
    }
    context->ifindex = request.ifr_ifindex;
    if (ioctl(fd, SIOCGIFHWADDR, &request) != 0) {
        close(fd);
        return 0;
    }
    memcpy(context->mac, request.ifr_hwaddr.sa_data,
           SPW_RAW_ETHERNET_MAC_SIZE);
    close(fd);
    return 1;
}

static spw_result_t io_start(void* raw) {
    packet_context_t* context = (packet_context_t*)raw;
    struct sockaddr_ll address;

    if (context == NULL) return SPW_ERR_INVALID_ARGUMENT;
    if (context->fd >= 0) {
        context->started = true;
        return SPW_OK;
    }

    context->fd = socket(AF_PACKET, SOCK_RAW,
                         htons(SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE));
    if (context->fd < 0) return SPW_ERR_BACKEND;

    memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_protocol =
        htons(SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE);
    address.sll_ifindex = context->ifindex;
    if (bind(context->fd, (const struct sockaddr*)&address,
             sizeof(address)) != 0) {
        close(context->fd);
        context->fd = -1;
        return SPW_ERR_BACKEND;
    }
    context->started = true;
    return SPW_OK;
}

static spw_result_t io_stop(void* raw) {
    packet_context_t* context = (packet_context_t*)raw;
    if (context == NULL) return SPW_ERR_INVALID_ARGUMENT;
    if (context->fd >= 0) close(context->fd);
    context->fd = -1;
    context->started = false;
    return SPW_OK;
}

static spw_result_t io_reset(void* raw) {
    return io_stop(raw);
}

static spw_result_t io_send(void* raw,
                            const uint8_t* frame,
                            size_t frame_size,
                            spw_timeout_us_t timeout_us) {
    packet_context_t* context = (packet_context_t*)raw;
    struct sockaddr_ll destination;
    ssize_t sent;

    if (context == NULL || context->fd < 0 || !context->started ||
        frame == NULL || frame_size < ETH_HLEN ||
        frame_size > PEER_FRAME_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!wait_fd(context->fd, POLLOUT, timeout_us)) {
        return SPW_ERR_TIMEOUT;
    }

    memset(&destination, 0, sizeof(destination));
    destination.sll_family = AF_PACKET;
    destination.sll_protocol =
        htons(SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE);
    destination.sll_ifindex = context->ifindex;
    destination.sll_halen = ETH_ALEN;
    memcpy(destination.sll_addr, frame, ETH_ALEN);

    do {
        sent = sendto(context->fd, frame, frame_size, 0,
                      (const struct sockaddr*)&destination,
                      sizeof(destination));
    } while (sent < 0 && errno == EINTR);
    if (sent == (ssize_t)frame_size) {
        ++context->tx_frames;
        context->tx_bytes += frame_size;
        return SPW_OK;
    }
    return SPW_ERR_BACKEND;
}

static spw_result_t io_receive(void* raw,
                               uint8_t* frame,
                               size_t frame_capacity,
                               size_t* out_frame_size,
                               spw_timeout_us_t timeout_us) {
    packet_context_t* context = (packet_context_t*)raw;
    ssize_t received;

    if (context == NULL || context->fd < 0 || !context->started ||
        frame == NULL || out_frame_size == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = 0u;
    if (!wait_fd(context->fd, POLLIN, timeout_us)) {
        return SPW_ERR_TIMEOUT;
    }
    do {
        received = recvfrom(context->fd, frame, frame_capacity, 0, NULL, NULL);
    } while (received < 0 && errno == EINTR);
    if (received < 0) return SPW_ERR_BACKEND;
    *out_frame_size = (size_t)received;
    ++context->rx_frames;
    context->rx_bytes += (uint64_t)received;
    return SPW_OK;
}

static spw_result_t io_wait(void* raw,
                            spw_raw_ethernet_ready_t interests,
                            spw_timeout_us_t timeout_us,
                            spw_raw_ethernet_ready_t* out_ready) {
    packet_context_t* context = (packet_context_t*)raw;
    short events = 0;
    struct pollfd descriptor;
    int result;

    if (context == NULL || context->fd < 0 || !context->started ||
        out_ready == NULL || interests == SPW_RAW_ETHERNET_READY_NONE) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if ((interests & SPW_RAW_ETHERNET_READY_RX) != 0u) events |= POLLIN;
    if ((interests & SPW_RAW_ETHERNET_READY_TX) != 0u) events |= POLLOUT;

    descriptor = (struct pollfd){context->fd, events, 0};
    do {
        result = poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (result < 0 && errno == EINTR);

    *out_ready = SPW_RAW_ETHERNET_READY_NONE;
    if (result == 0) return SPW_ERR_TIMEOUT;
    if (result < 0) return SPW_ERR_BACKEND;
    if ((descriptor.revents & POLLIN) != 0) {
        *out_ready |= SPW_RAW_ETHERNET_READY_RX;
    }
    if ((descriptor.revents & POLLOUT) != 0) {
        *out_ready |= SPW_RAW_ETHERNET_READY_TX;
    }
    return *out_ready == SPW_RAW_ETHERNET_READY_NONE
               ? SPW_ERR_BACKEND : SPW_OK;
}

static spw_result_t io_mtu(const void* raw, size_t* out_frame_size) {
    (void)raw;
    if (out_frame_size == NULL) return SPW_ERR_INVALID_ARGUMENT;
    *out_frame_size = PEER_FRAME_CAPACITY;
    return SPW_OK;
}

static spw_result_t io_link(const void* raw, bool* out_link_up) {
    const packet_context_t* context = (const packet_context_t*)raw;
    struct ifreq request;
    int fd;

    if (context == NULL || out_link_up == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return SPW_ERR_BACKEND;
    memset(&request, 0, sizeof(request));
    (void)snprintf(request.ifr_name, sizeof(request.ifr_name), "%s",
                   context->interface_name);
    if (ioctl(fd, SIOCGIFFLAGS, &request) != 0) {
        close(fd);
        return SPW_ERR_BACKEND;
    }
    close(fd);
    *out_link_up =
        (request.ifr_flags & IFF_UP) != 0 &&
        (request.ifr_flags & IFF_RUNNING) != 0;
    return SPW_OK;
}

static uint64_t runtime_now_us(const void* context) {
    struct timespec now;
    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static spw_result_t runtime_delay(void* context,
                                  uint64_t delay_us,
                                  spw_timeout_us_t timeout_us) {
    struct timespec request;
    struct timespec remaining;
    (void)context;

    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    request.tv_sec = (time_t)(delay_us / UINT64_C(1000000));
    request.tv_nsec =
        (long)((delay_us % UINT64_C(1000000)) * UINT64_C(1000));
    while (nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) return SPW_ERR_BACKEND;
        request = remaining;
    }
    return SPW_OK;
}

static const spw_raw_ethernet_io_ops_t IO_OPS = {
    sizeof(spw_raw_ethernet_io_ops_t),
    SPW_RAW_ETHERNET_IO_OPS_VERSION,
    io_start, io_stop, io_reset,
    io_send, io_receive, io_wait, io_mtu, io_link
};

static const spw_runtime_ops_t RUNTIME_OPS = {
    sizeof(spw_runtime_ops_t),
    SPW_RUNTIME_OPS_VERSION,
    runtime_now_us, runtime_delay
};

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static int compare_u64(const void* lhs, const void* rhs) {
    const uint64_t a = *(const uint64_t*)lhs;
    const uint64_t b = *(const uint64_t*)rhs;
    return a < b ? -1 : a > b ? 1 : 0;
}

static uint64_t percentile(const uint64_t* sorted,
                           size_t count,
                           unsigned percent) {
    size_t rank = ((size_t)percent * count + 99u) / 100u;
    if (rank == 0u) rank = 1u;
    if (rank > count) rank = count;
    return sorted[rank - 1u];
}

static void fill_payload(size_t size, uint32_t sequence) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        g_tx[i] = (uint8_t)((sequence * 17u + (uint32_t)i * 13u) & 0xffu);
    }
}

static int wait_run(spw_port_t* port) {
    unsigned attempt;
    for (attempt = 0u; attempt < 20000u; ++attempt) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) return 0;
        if (state == SPW_LINK_RUN) return 1;
        {
            struct timespec delay = {0, 1000000L};
            (void)nanosleep(&delay, NULL);
        }
    }
    return 0;
}

static int exchange(spw_port_t* port, size_t size, uint32_t sequence) {
    spw_packet_t tx;
    spw_packet_t rx;

    fill_payload(size, sequence);
    tx = (spw_packet_t){g_tx, size, size, SPW_TERMINATOR_EOP};
    rx = (spw_packet_t){g_rx, 0u, sizeof(g_rx), SPW_TERMINATOR_EEP};

    if (spw_port_send(port, &tx, UINT64_C(500000)) != SPW_OK ||
        spw_port_receive(port, &rx, UINT64_C(500000)) != SPW_OK ||
        rx.length != size || rx.terminator != SPW_TERMINATOR_EOP ||
        (size != 0u && memcmp(g_tx, g_rx, size) != 0)) {
        return 0;
    }
    return 1;
}

static int run_size(spw_port_t* port,
                    size_t payload_size,
                    size_t warmup,
                    size_t iterations,
                    uint32_t* sequence) {
    size_t i;
    long double sum = 0.0L;

    for (i = 0u; i < warmup; ++i) {
        if (!exchange(port, payload_size, (*sequence)++)) return 0;
    }
    for (i = 0u; i < iterations; ++i) {
        const uint64_t start = monotonic_ns();
        if (!exchange(port, payload_size, (*sequence)++)) return 0;
        g_samples[i] = monotonic_ns() - start;
        sum += (long double)g_samples[i];
    }

    qsort(g_samples, iterations, sizeof(g_samples[0]), compare_u64);
    printf("{\"schema\":\"spwkit.embedded.raw-ethernet-rtt.v1\""
           ",\"payload_bytes\":%zu"
           ",\"warmup\":%zu"
           ",\"iterations\":%zu"
           ",\"median_ns\":%llu"
           ",\"mean_ns\":%.3Lf"
           ",\"p95_ns\":%llu"
           ",\"p99_ns\":%llu"
           ",\"min_ns\":%llu"
           ",\"max_ns\":%llu}\n",
           payload_size, warmup, iterations,
           (unsigned long long)percentile(g_samples, iterations, 50u),
           sum / (long double)iterations,
           (unsigned long long)percentile(g_samples, iterations, 95u),
           (unsigned long long)percentile(g_samples, iterations, 99u),
           (unsigned long long)g_samples[0],
           (unsigned long long)g_samples[iterations - 1u]);
    fflush(stdout);
    return 1;
}

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s --interface IFACE [--iterations N] [--warmup N]\n",
            program);
}

int main(int argc, char** argv) {
    const char* interface_name = NULL;
    size_t iterations = 128u;
    size_t warmup = 16u;
    packet_context_t io;
    spw_raw_ethernet_config_t raw;
    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_t* port = NULL;
    uint32_t sequence = 1u;
    static const size_t sizes[] = {64u, 256u, 1024u, 4096u};
    size_t index;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
            interface_name = argv[++i];
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            iterations = (size_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = (size_t)strtoul(argv[++i], NULL, 10);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (interface_name == NULL || iterations == 0u ||
        iterations > PEER_MAX_ITERATIONS || warmup > PEER_MAX_ITERATIONS) {
        usage(argv[0]);
        return 2;
    }

    memset(&io, 0, sizeof(io));
    io.fd = -1;
    if (strlen(interface_name) >= sizeof(io.interface_name)) return 2;
    (void)snprintf(io.interface_name, sizeof(io.interface_name), "%s",
                   interface_name);
    if (!query_interface(&io)) {
        fprintf(stderr, "failed to query interface %s\n", interface_name);
        return 1;
    }

    raw = (spw_raw_ethernet_config_t)
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &IO_OPS, &io, &RUNTIME_OPS, NULL, PEER_LINK_ID);
    memcpy(raw.local_mac, io.mac, sizeof(raw.local_mac));
    memcpy(raw.remote_mac, BOARD_MAC, sizeof(raw.remote_mac));
    raw.ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
    raw.fragment_payload_size = 1400u;
    raw.ack_timeout_ms = 20u;
    raw.max_retries = 3u;
    raw.keepalive_interval_ms = 100u;
    raw.peer_timeout_ms = 1000u;
    config.backend_config = &raw;
    config.backend_config_size = sizeof(raw);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK || !wait_run(port)) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (port != NULL) {
            (void)spw_port_get_link_state(port, &state);
        }
        fprintf(stderr,
                "failed to establish VSPW raw-Ethernet RUN state "
                "(state=%d tx_frames=%llu tx_bytes=%llu "
                "rx_frames=%llu rx_bytes=%llu)\n",
                (int)state,
                (unsigned long long)io.tx_frames,
                (unsigned long long)io.tx_bytes,
                (unsigned long long)io.rx_frames,
                (unsigned long long)io.rx_bytes);
        if (port != NULL) (void)spw_port_close(port);
        return 1;
    }

    for (index = 0u; index < sizeof(sizes) / sizeof(sizes[0]); ++index) {
        if (!run_size(port, sizes[index], warmup, iterations, &sequence)) {
            fprintf(stderr, "exchange failed at payload %zu\n", sizes[index]);
            (void)spw_port_close(port);
            return 1;
        }
    }

    {
        spw_packet_t done = {
            (uint8_t*)DONE_PACKET,
            sizeof(DONE_PACKET),
            sizeof(DONE_PACKET),
            SPW_TERMINATOR_EOP
        };
        if (spw_port_send(port, &done, UINT64_C(500000)) != SPW_OK) {
            fprintf(stderr, "failed to send completion marker\n");
            (void)spw_port_close(port);
            return 1;
        }
    }

    printf("HOST_RESULT: PASS\n");
    (void)spw_port_close(port);
    return 0;
}
