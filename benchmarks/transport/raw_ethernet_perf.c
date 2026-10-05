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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define PERF_FRAME_CAPACITY 1518u
#define PERF_MAX_PAYLOAD (1024u * 1024u)
#define PERF_TIMEOUT_US UINT64_C(5000000)
#define PERF_STATE_TIMEOUT_MS 15000u

typedef enum perf_role {
    PERF_ROLE_SOURCE = 0,
    PERF_ROLE_SINK = 1
} perf_role_t;

typedef struct packet_context {
    char interface_name[IFNAMSIZ];
    int fd;
    int ifindex;
    uint8_t mac[SPW_RAW_ETHERNET_MAC_SIZE];
    uint16_t ether_type;
    bool started;
    uint64_t tx_frames;
    uint64_t tx_bytes;
    uint64_t rx_frames;
    uint64_t rx_bytes;
} packet_context_t;

typedef struct perf_options {
    perf_role_t role;
    char interface_name[IFNAMSIZ];
    uint8_t remote_mac[SPW_RAW_ETHERNET_MAC_SIZE];
    uint16_t ether_type;
    uint32_t link_id;
    size_t payload_size;
    uint64_t total_bytes;
    uint32_t seed;
} perf_options_t;

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
    context->fd = socket(AF_PACKET, SOCK_RAW, htons(context->ether_type));
    if (context->fd < 0) return SPW_ERR_BACKEND;
    memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(context->ether_type);
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

static spw_result_t io_send(void* raw, const uint8_t* frame,
                            size_t frame_size,
                            spw_timeout_us_t timeout_us) {
    packet_context_t* context = (packet_context_t*)raw;
    struct sockaddr_ll destination;
    ssize_t sent;
    if (context == NULL || context->fd < 0 || !context->started ||
        frame == NULL || frame_size < ETH_HLEN ||
        frame_size > PERF_FRAME_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!wait_fd(context->fd, POLLOUT, timeout_us)) return SPW_ERR_TIMEOUT;
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
    if (sent != (ssize_t)frame_size) return SPW_ERR_BACKEND;
    ++context->tx_frames;
    context->tx_bytes += frame_size;
    return SPW_OK;
}

static spw_result_t io_receive(void* raw, uint8_t* frame,
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
    if (!wait_fd(context->fd, POLLIN, timeout_us)) return SPW_ERR_TIMEOUT;
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
    if ((descriptor.revents & POLLIN) != 0) *out_ready |= SPW_RAW_ETHERNET_READY_RX;
    if ((descriptor.revents & POLLOUT) != 0) *out_ready |= SPW_RAW_ETHERNET_READY_TX;
    return *out_ready == SPW_RAW_ETHERNET_READY_NONE
               ? SPW_ERR_BACKEND : SPW_OK;
}

static spw_result_t io_mtu(const void* raw, size_t* out_frame_size) {
    (void)raw;
    if (out_frame_size == NULL) return SPW_ERR_INVALID_ARGUMENT;
    *out_frame_size = PERF_FRAME_CAPACITY;
    return SPW_OK;
}

static spw_result_t io_link(const void* raw, bool* out_link_up) {
    const packet_context_t* context = (const packet_context_t*)raw;
    struct ifreq request;
    int fd;
    if (context == NULL || out_link_up == NULL) return SPW_ERR_INVALID_ARGUMENT;
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
    *out_link_up = (request.ifr_flags & IFF_UP) != 0 &&
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

static spw_result_t runtime_delay(void* context, uint64_t delay_us,
                                  spw_timeout_us_t timeout_us) {
    struct timespec request;
    struct timespec remaining;
    (void)context;
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    request.tv_sec = (time_t)(delay_us / UINT64_C(1000000));
    request.tv_nsec = (long)((delay_us % UINT64_C(1000000)) * UINT64_C(1000));
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

static void sleep_ms(unsigned ms) {
    struct timespec delay = {
        (time_t)(ms / 1000u),
        (long)(ms % 1000u) * 1000000L
    };
    (void)nanosleep(&delay, NULL);
}

static int parse_u64(const char* text, uint64_t* value) {
    char* end = NULL;
    unsigned long long parsed;
    if (text == NULL || text[0] == '\0' || value == NULL) return 0;
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int parse_mac(const char* text, uint8_t mac[SPW_RAW_ETHERNET_MAC_SIZE]) {
    unsigned values[SPW_RAW_ETHERNET_MAC_SIZE];
    if (sscanf(text, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) != 6) {
        return 0;
    }
    for (size_t i = 0u; i < SPW_RAW_ETHERNET_MAC_SIZE; ++i) {
        if (values[i] > 0xffu) return 0;
        mac[i] = (uint8_t)values[i];
    }
    return (mac[0] & 1u) == 0u;
}

static void usage(const char* program) {
    fprintf(stderr,
            "usage: sudo %s --role source|sink --interface IFACE "
            "--remote-mac MAC [options]\n"
            "  --link-id N          default 264\n"
            "  --ether-type N       default 0x88B5; use a distinct value per concurrent raw link\n"
            "  --payload-size N     default 4096, max 1048576\n"
            "  --total-bytes N      default 1073741824 (1 GiB)\n"
            "  --seed N             default 264\n",
            program);
}

static int parse_options(int argc, char** argv, perf_options_t* options) {
    int role_set = 0;
    int remote_set = 0;
    int i;
    memset(options, 0, sizeof(*options));
    options->link_id = 264u;
    options->ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
    options->payload_size = 4096u;
    options->total_bytes = UINT64_C(1073741824);
    options->seed = 264u;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            const char* role = argv[++i];
            if (strcmp(role, "source") == 0) options->role = PERF_ROLE_SOURCE;
            else if (strcmp(role, "sink") == 0) options->role = PERF_ROLE_SINK;
            else return 0;
            role_set = 1;
        } else if (strcmp(argv[i], "--interface") == 0 && i + 1 < argc) {
            if (snprintf(options->interface_name,
                         sizeof(options->interface_name),
                         "%s", argv[++i]) >=
                (int)sizeof(options->interface_name)) return 0;
        } else if (strcmp(argv[i], "--remote-mac") == 0 && i + 1 < argc) {
            if (!parse_mac(argv[++i], options->remote_mac)) return 0;
            remote_set = 1;
        } else if (strcmp(argv[i], "--link-id") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) ||
                value == 0u || value > UINT32_MAX) return 0;
            options->link_id = (uint32_t)value;
        } else if (strcmp(argv[i], "--ether-type") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) ||
                value < 0x0600u || value > UINT16_MAX) return 0;
            options->ether_type = (uint16_t)value;
        } else if (strcmp(argv[i], "--payload-size") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) ||
                value == 0u || value > PERF_MAX_PAYLOAD) return 0;
            options->payload_size = (size_t)value;
        } else if (strcmp(argv[i], "--total-bytes") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->total_bytes) ||
                options->total_bytes == 0u) return 0;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) || value > UINT32_MAX) return 0;
            options->seed = (uint32_t)value;
        } else {
            return 0;
        }
    }
    return role_set && remote_set && options->interface_name[0] != '\0';
}

static uint8_t pattern_byte(uint32_t seed, uint64_t sequence, size_t index) {
    uint64_t value = (uint64_t)seed * UINT64_C(0x9e3779b1) +
                     sequence * UINT64_C(0x85ebca6b) +
                     (uint64_t)index * UINT64_C(0xc2b2ae35);
    value ^= value >> 33u;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 29u;
    return (uint8_t)value;
}

static void fill_payload(uint8_t* payload, size_t size,
                         uint32_t seed, uint64_t sequence) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        payload[i] = pattern_byte(seed, sequence, i);
    }
}

static int verify_payload(const uint8_t* payload, size_t size,
                          uint32_t seed, uint64_t sequence) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        if (payload[i] != pattern_byte(seed, sequence, i)) return 0;
    }
    return 1;
}

static int wait_run(spw_port_t* port) {
    unsigned i;
    for (i = 0u; i <= PERF_STATE_TIMEOUT_MS; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) return 0;
        if (state == SPW_LINK_RUN) return 1;
        sleep_ms(1u);
    }
    return 0;
}

int main(int argc, char** argv) {
    perf_options_t options;
    packet_context_t io;
    spw_raw_ethernet_config_t raw;
    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_t* port = NULL;
    uint8_t* payload = NULL;
    uint64_t transferred = 0u;
    uint64_t packets = 0u;
    uint64_t sequence = 0u;
    uint64_t start_ns;
    spw_result_t result;
    spw_statistics_t stats = {0};

    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    payload = (uint8_t*)malloc(options.payload_size);
    if (payload == NULL) return EXIT_FAILURE;

    memset(&io, 0, sizeof(io));
    io.fd = -1;
    io.ether_type = options.ether_type;
    (void)snprintf(io.interface_name, sizeof(io.interface_name),
                   "%s", options.interface_name);
    if (!query_interface(&io)) {
        fprintf(stderr, "failed to query interface %s\n",
                options.interface_name);
        free(payload);
        return EXIT_FAILURE;
    }

    raw = (spw_raw_ethernet_config_t)
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &IO_OPS, &io, &RUNTIME_OPS, NULL, options.link_id);
    memcpy(raw.local_mac, io.mac, sizeof(raw.local_mac));
    memcpy(raw.remote_mac, options.remote_mac, sizeof(raw.remote_mac));
    raw.ether_type = options.ether_type;
    raw.fragment_payload_size = 1400u;
    raw.ack_timeout_ms = 20u;
    raw.max_retries = 5u;
    raw.keepalive_interval_ms = 100u;
    raw.peer_timeout_ms = 1000u;

    config.backend_config = &raw;
    config.backend_config_size = sizeof(raw);
    result = spw_port_open(&config, &port);
    if (result != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK || !wait_run(port)) {
        fprintf(stderr, "failed to establish raw-Ethernet VSPW RUN\n");
        if (port != NULL) (void)spw_port_close(port);
        free(payload);
        return EXIT_FAILURE;
    }

    start_ns = monotonic_ns();
    while (transferred < options.total_bytes) {
        const uint64_t remaining = options.total_bytes - transferred;
        const size_t size = remaining < (uint64_t)options.payload_size
                                ? (size_t)remaining
                                : options.payload_size;
        if (options.role == PERF_ROLE_SOURCE) {
            spw_packet_t packet;
            fill_payload(payload, size, options.seed, sequence);
            packet = (spw_packet_t){payload, size, size, SPW_TERMINATOR_EOP};
            result = spw_port_send(port, &packet, PERF_TIMEOUT_US);
            if (result != SPW_OK) {
                fprintf(stderr, "send failed at sequence %llu: %d\n",
                        (unsigned long long)sequence, (int)result);
                (void)spw_port_close(port);
                free(payload);
                return EXIT_FAILURE;
            }
        } else {
            spw_packet_t packet = {
                payload, 0u, options.payload_size, SPW_TERMINATOR_EEP
            };
            result = spw_port_receive(port, &packet, PERF_TIMEOUT_US);
            {
                const int payload_ok =
                    result == SPW_OK && packet.length == size &&
                    verify_payload(payload, size, options.seed, sequence);
                if (result != SPW_OK || packet.length != size ||
                    packet.terminator != SPW_TERMINATOR_EOP ||
                    !payload_ok) {
                    fprintf(stderr,
                            "receive verification failed at sequence %llu: "
                            "result=%d length=%zu expected=%zu terminator=%d "
                            "payload_match=%s link_id=%u ether_type=0x%04x\n",
                            (unsigned long long)sequence, (int)result,
                            packet.length, size, (int)packet.terminator,
                            payload_ok ? "yes" : "no",
                            options.link_id, (unsigned)options.ether_type);
                    (void)spw_port_close(port);
                    free(payload);
                    return EXIT_FAILURE;
                }
            }
        }
        transferred += size;
        ++packets;
        ++sequence;
    }

    {
        const uint64_t elapsed_ns = monotonic_ns() - start_ns;
        const long double seconds =
            (long double)elapsed_ns / 1000000000.0L;
        const long double mbps =
            seconds > 0.0L
                ? ((long double)transferred * 8.0L / 1000000.0L) / seconds
                : 0.0L;
        result = spw_port_get_statistics(port, &stats);
        if (result != SPW_OK) {
            (void)spw_port_close(port);
            free(payload);
            return EXIT_FAILURE;
        }
        printf("{\"schema\":\"spwkit.transport.raw-ethernet-throughput.v1\""
               ",\"role\":\"%s\""
               ",\"link_id\":%u"
               ",\"ether_type\":%u"
               ",\"payload_bytes\":%zu"
               ",\"total_bytes\":%llu"
               ",\"packets\":%llu"
               ",\"elapsed_ns\":%llu"
               ",\"payload_mbps\":%.6Lf"
               ",\"carrier_tx_frames\":%llu"
               ",\"carrier_tx_bytes\":%llu"
               ",\"carrier_rx_frames\":%llu"
               ",\"carrier_rx_bytes\":%llu"
               ",\"link_errors\":%llu}\n",
               options.role == PERF_ROLE_SOURCE ? "source" : "sink",
               options.link_id,
               (unsigned)options.ether_type,
               options.payload_size,
               (unsigned long long)transferred,
               (unsigned long long)packets,
               (unsigned long long)elapsed_ns,
               mbps,
               (unsigned long long)io.tx_frames,
               (unsigned long long)io.tx_bytes,
               (unsigned long long)io.rx_frames,
               (unsigned long long)io.rx_bytes,
               (unsigned long long)stats.link_errors);
    }

    (void)spw_port_close(port);
    free(payload);
    return EXIT_SUCCESS;
}
