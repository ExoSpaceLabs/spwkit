// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

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

#define RAW_BENCH_MAX_SAMPLES 4096u
#define RAW_BENCH_MAX_PAYLOAD 4096u
#define RAW_BENCH_WORKSPACE_SIZE (4u * 1024u * 1024u)
#define RAW_BENCH_FRAME_CAPACITY 1514u
#define RAW_BENCH_DIRECT_ETHERTYPE 0x88B6u
#define RAW_BENCH_DIRECT_HEADER_SIZE 18u
#define RAW_BENCH_FRAGMENT_PAYLOAD 1400u

typedef enum raw_direction {
    RAW_DIRECTION_TX = 0,
    RAW_DIRECTION_RX = 1
} raw_direction_t;

typedef union raw_workspace {
    max_align_t alignment;
    uint8_t bytes[RAW_BENCH_WORKSPACE_SIZE];
} raw_workspace_t;

typedef struct raw_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} raw_statistics_t;

typedef struct packet_context {
    char interface_name[IFNAMSIZ];
    uint16_t protocol;
    int fd;
    int ifindex;
    uint8_t mac[SPW_RAW_ETHERNET_MAC_SIZE];
    bool started;
} packet_context_t;

typedef struct raw_fixture {
    spw_port_t* a;
    spw_port_t* b;
    raw_workspace_t workspace_a;
    raw_workspace_t workspace_b;
    packet_context_t spw_a;
    packet_context_t spw_b;
    packet_context_t native_a;
    packet_context_t native_b;
} raw_fixture_t;

static uint64_t g_native_samples[RAW_BENCH_MAX_SAMPLES];
static uint64_t g_spwkit_samples[RAW_BENCH_MAX_SAMPLES];
static uint8_t g_payload[RAW_BENCH_MAX_PAYLOAD];
static uint8_t g_receive[RAW_BENCH_MAX_PAYLOAD];
static uint8_t g_frame[RAW_BENCH_FRAME_CAPACITY];

static int parse_size(const char* text,
                      size_t minimum,
                      size_t maximum,
                      size_t* out_value) {
    char* end = NULL;
    unsigned long long value;
    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return 0;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < minimum || value > maximum) {
        return 0;
    }
    *out_value = (size_t)value;
    return 1;
}

static int timeout_ms(spw_timeout_us_t timeout_us) {
    uint64_t rounded;
    if (timeout_us == SPW_TIMEOUT_INFINITE) {
        return -1;
    }
    rounded = (timeout_us + 999u) / 1000u;
    return rounded > 2147483647u ? 2147483647 : (int)rounded;
}

static int wait_fd(int fd, short events, spw_timeout_us_t timeout_us) {
    struct pollfd descriptor;
    int result;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = fd;
    descriptor.events = events;
    do {
        result = poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & events) != 0;
}

static int query_interface(packet_context_t* context) {
    struct ifreq request;
    int fd;
    if (context == NULL || context->interface_name[0] == '\0') {
        return 0;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }
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

static int packet_open(packet_context_t* context) {
    struct sockaddr_ll address;
    if (!query_interface(context)) {
        return 0;
    }
    context->fd = socket(AF_PACKET, SOCK_RAW, htons(context->protocol));
    if (context->fd < 0) {
        return 0;
    }
    memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_protocol = htons(context->protocol);
    address.sll_ifindex = context->ifindex;
    if (bind(context->fd, (const struct sockaddr*)&address,
             sizeof(address)) != 0) {
        close(context->fd);
        context->fd = -1;
        return 0;
    }
    return 1;
}

static void packet_close(packet_context_t* context) {
    if (context != NULL && context->fd >= 0) {
        close(context->fd);
        context->fd = -1;
    }
    if (context != NULL) {
        context->started = false;
    }
}

static int packet_prepare(packet_context_t* context,
                          const char* interface_name,
                          uint16_t protocol) {
    if (context == NULL || interface_name == NULL ||
        strlen(interface_name) >= sizeof(context->interface_name)) {
        return 0;
    }
    memset(context, 0, sizeof(*context));
    context->fd = -1;
    context->protocol = protocol;
    (void)snprintf(context->interface_name,
                   sizeof(context->interface_name), "%s", interface_name);
    return query_interface(context);
}

static spw_result_t packet_send(packet_context_t* context,
                                const uint8_t* frame,
                                size_t frame_size,
                                spw_timeout_us_t timeout_us) {
    struct sockaddr_ll destination;
    ssize_t sent;
    if (context == NULL || context->fd < 0 || !context->started ||
        frame == NULL || frame_size < ETH_HLEN ||
        frame_size > RAW_BENCH_FRAME_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!wait_fd(context->fd, POLLOUT, timeout_us)) {
        return SPW_ERR_TIMEOUT;
    }
    memset(&destination, 0, sizeof(destination));
    destination.sll_family = AF_PACKET;
    destination.sll_protocol = htons(context->protocol);
    destination.sll_ifindex = context->ifindex;
    destination.sll_halen = ETH_ALEN;
    memcpy(destination.sll_addr, frame, ETH_ALEN);
    do {
        sent = sendto(context->fd, frame, frame_size, 0,
                      (const struct sockaddr*)&destination,
                      sizeof(destination));
    } while (sent < 0 && errno == EINTR);
    return sent == (ssize_t)frame_size ? SPW_OK : SPW_ERR_BACKEND;
}

static spw_result_t packet_receive(packet_context_t* context,
                                   uint8_t* frame,
                                   size_t frame_capacity,
                                   size_t* out_frame_size,
                                   spw_timeout_us_t timeout_us) {
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
    if (received < 0) {
        return SPW_ERR_BACKEND;
    }
    *out_frame_size = (size_t)received;
    return SPW_OK;
}

static spw_result_t io_start(void* io_context) {
    packet_context_t* context = (packet_context_t*)io_context;
    if (context == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (context->fd < 0 && !packet_open(context)) {
        return SPW_ERR_BACKEND;
    }
    context->started = true;
    return SPW_OK;
}

static spw_result_t io_stop(void* io_context) {
    packet_context_t* context = (packet_context_t*)io_context;
    if (context == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    packet_close(context);
    return SPW_OK;
}

static spw_result_t io_reset(void* io_context) {
    return io_stop(io_context);
}

static spw_result_t io_send_frame(void* io_context,
                                  const uint8_t* frame,
                                  size_t frame_size,
                                  spw_timeout_us_t timeout_us) {
    return packet_send((packet_context_t*)io_context, frame, frame_size,
                       timeout_us);
}

static spw_result_t io_receive_frame(void* io_context,
                                     uint8_t* frame,
                                     size_t frame_capacity,
                                     size_t* out_frame_size,
                                     spw_timeout_us_t timeout_us) {
    return packet_receive((packet_context_t*)io_context, frame, frame_capacity,
                          out_frame_size, timeout_us);
}

static spw_result_t io_wait(void* io_context,
                            spw_raw_ethernet_ready_t interests,
                            spw_timeout_us_t timeout_us,
                            spw_raw_ethernet_ready_t* out_ready) {
    packet_context_t* context = (packet_context_t*)io_context;
    short events = 0;
    struct pollfd descriptor;
    int result;
    if (context == NULL || context->fd < 0 || !context->started ||
        out_ready == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if ((interests & SPW_RAW_ETHERNET_READY_RX) != 0u) {
        events |= POLLIN;
    }
    if ((interests & SPW_RAW_ETHERNET_READY_TX) != 0u) {
        events |= POLLOUT;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = context->fd;
    descriptor.events = events;
    do {
        result = poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (result < 0 && errno == EINTR);
    *out_ready = SPW_RAW_ETHERNET_READY_NONE;
    if (result == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (result < 0) {
        return SPW_ERR_BACKEND;
    }
    if ((descriptor.revents & POLLIN) != 0) {
        *out_ready |= SPW_RAW_ETHERNET_READY_RX;
    }
    if ((descriptor.revents & POLLOUT) != 0) {
        *out_ready |= SPW_RAW_ETHERNET_READY_TX;
    }
    return *out_ready == SPW_RAW_ETHERNET_READY_NONE
               ? SPW_ERR_BACKEND
               : SPW_OK;
}

static spw_result_t io_get_max_frame_size(const void* io_context,
                                          size_t* out_frame_size) {
    (void)io_context;
    if (out_frame_size == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = RAW_BENCH_FRAME_CAPACITY;
    return SPW_OK;
}

static spw_result_t io_get_link_up(const void* io_context,
                                   bool* out_link_up) {
    const packet_context_t* context =
        (const packet_context_t*)io_context;
    struct ifreq request;
    int fd;
    if (context == NULL || out_link_up == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return SPW_ERR_BACKEND;
    }
    memset(&request, 0, sizeof(request));
    (void)snprintf(request.ifr_name, sizeof(request.ifr_name), "%s",
                   context->interface_name);
    if (ioctl(fd, SIOCGIFFLAGS, &request) != 0) {
        close(fd);
        return SPW_ERR_BACKEND;
    }
    close(fd);
    *out_link_up = (request.ifr_flags & IFF_UP) != 0;
    return SPW_OK;
}

static uint64_t runtime_now_us(const void* runtime_context) {
    struct timespec now;
    (void)runtime_context;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static spw_result_t runtime_delay_us(void* runtime_context,
                                     uint64_t delay_us,
                                     spw_timeout_us_t timeout_us) {
    struct timespec request;
    struct timespec remaining;
    (void)runtime_context;
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    request.tv_sec = (time_t)(delay_us / UINT64_C(1000000));
    request.tv_nsec =
        (long)((delay_us % UINT64_C(1000000)) * UINT64_C(1000));
    while (nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) {
            return SPW_ERR_BACKEND;
        }
        request = remaining;
    }
    return SPW_OK;
}

static const spw_raw_ethernet_io_ops_t IO_OPS = {
    sizeof(spw_raw_ethernet_io_ops_t),
    SPW_RAW_ETHERNET_IO_OPS_VERSION,
    io_start,
    io_stop,
    io_reset,
    io_send_frame,
    io_receive_frame,
    io_wait,
    io_get_max_frame_size,
    io_get_link_up
};

static const spw_runtime_ops_t RUNTIME_OPS = {
    sizeof(spw_runtime_ops_t),
    SPW_RUNTIME_OPS_VERSION,
    runtime_now_us,
    runtime_delay_us
};

static void sort_samples(uint64_t* samples, size_t count) {
    size_t gap;
    for (gap = count / 2u; gap > 0u; gap /= 2u) {
        size_t i;
        for (i = gap; i < count; ++i) {
            const uint64_t value = samples[i];
            size_t j = i;
            while (j >= gap && samples[j - gap] > value) {
                samples[j] = samples[j - gap];
                j -= gap;
            }
            samples[j] = value;
        }
    }
}

static size_t percentile_index(size_t count, unsigned percentile) {
    size_t rank = ((size_t)percentile * count + 99u) / 100u;
    if (rank == 0u) {
        rank = 1u;
    }
    if (rank > count) {
        rank = count;
    }
    return rank - 1u;
}

static double square_root(double value) {
    double estimate;
    unsigned i;
    if (value <= 0.0) {
        return 0.0;
    }
    estimate = value > 1.0 ? value : 1.0;
    for (i = 0u; i < 32u; ++i) {
        estimate = 0.5 * (estimate + value / estimate);
    }
    return estimate;
}

static raw_statistics_t calculate_statistics(uint64_t* samples,
                                             size_t count) {
    raw_statistics_t statistics;
    double sum = 0.0;
    double variance_sum = 0.0;
    size_t i;
    memset(&statistics, 0, sizeof(statistics));
    sort_samples(samples, count);
    statistics.minimum = samples[0u];
    statistics.maximum = samples[count - 1u];
    statistics.p95 = samples[percentile_index(count, 95u)];
    statistics.p99 = samples[percentile_index(count, 99u)];
    if ((count & 1u) != 0u) {
        statistics.median = (double)samples[count / 2u];
    } else {
        statistics.median =
            ((double)samples[count / 2u - 1u] +
             (double)samples[count / 2u]) / 2.0;
    }
    for (i = 0u; i < count; ++i) {
        sum += (double)samples[i];
    }
    statistics.mean = sum / (double)count;
    for (i = 0u; i < count; ++i) {
        const double difference =
            (double)samples[i] - statistics.mean;
        variance_sum += difference * difference;
    }
    statistics.standard_deviation =
        square_root(variance_sum / (double)count);
    return statistics;
}

static int open_spw_raw(packet_context_t* local_io,
                        const uint8_t remote_mac[SPW_RAW_ETHERNET_MAC_SIZE],
                        uint32_t link_id,
                        raw_workspace_t* workspace,
                        spw_port_t** out_port) {
    spw_raw_ethernet_config_t raw =
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &IO_OPS, local_io, &RUNTIME_OPS, NULL, link_id);
    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_workspace_requirements_t requirements;

    memcpy(raw.local_mac, local_io->mac, SPW_RAW_ETHERNET_MAC_SIZE);
    memcpy(raw.remote_mac, remote_mac, SPW_RAW_ETHERNET_MAC_SIZE);
    raw.ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
    raw.fragment_payload_size = RAW_BENCH_FRAGMENT_PAYLOAD;
    raw.ack_timeout_ms = 20u;
    raw.max_retries = 3u;
    raw.keepalive_interval_ms = 1000u;
    raw.peer_timeout_ms = 5000u;

    config.backend_config = &raw;
    config.backend_config_size = sizeof(raw);
    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(workspace->bytes) ||
        requirements.alignment > _Alignof(raw_workspace_t)) {
        return 0;
    }
    return spw_port_open_in_place(
               &config, workspace->bytes, sizeof(workspace->bytes),
               out_port) == SPW_OK;
}

static int wait_for_run(spw_port_t* a, spw_port_t* b) {
    unsigned attempt;
    for (attempt = 0u; attempt < 2500u; ++attempt) {
        spw_link_state_t state_a = SPW_LINK_ERROR_RESET;
        spw_link_state_t state_b = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(a, &state_a) != SPW_OK ||
            spw_port_get_link_state(b, &state_b) != SPW_OK) {
            return 0;
        }
        if (state_a == SPW_LINK_RUN && state_b == SPW_LINK_RUN) {
            return 1;
        }
        {
            struct timespec delay = {0, 1000000L};
            (void)nanosleep(&delay, NULL);
        }
    }
    return 0;
}

static int fixture_open(raw_fixture_t* fixture,
                        const char* interface_a,
                        const char* interface_b) {
    const uint32_t link_id = UINT32_C(0x52574157);
    memset(fixture, 0, sizeof(*fixture));
    fixture->spw_a.fd = -1;
    fixture->spw_b.fd = -1;
    fixture->native_a.fd = -1;
    fixture->native_b.fd = -1;

    if (!packet_prepare(&fixture->spw_a, interface_a,
                        SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE) ||
        !packet_prepare(&fixture->spw_b, interface_b,
                        SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE) ||
        !packet_prepare(&fixture->native_a, interface_a,
                        RAW_BENCH_DIRECT_ETHERTYPE) ||
        !packet_prepare(&fixture->native_b, interface_b,
                        RAW_BENCH_DIRECT_ETHERTYPE)) {
        return 0;
    }

    /* Bind both AF_PACKET sockets before either VSPW endpoint starts so
     * the first KEEPALIVE cannot disappear simply because the peer socket
     * does not exist yet. io_start() will only mark them active. */
    if (!packet_open(&fixture->spw_a) ||
        !packet_open(&fixture->spw_b)) {
        return 0;
    }

    if (!open_spw_raw(&fixture->spw_a, fixture->spw_b.mac, link_id,
                      &fixture->workspace_a, &fixture->a) ||
        !open_spw_raw(&fixture->spw_b, fixture->spw_a.mac, link_id,
                      &fixture->workspace_b, &fixture->b)) {
        return 0;
    }
    if (spw_port_start(fixture->a) != SPW_OK ||
        spw_port_start(fixture->b) != SPW_OK ||
        !wait_for_run(fixture->a, fixture->b)) {
        return 0;
    }

    if (!packet_open(&fixture->native_a) ||
        !packet_open(&fixture->native_b)) {
        return 0;
    }
    fixture->native_a.started = true;
    fixture->native_b.started = true;
    return 1;
}

static void fixture_close(raw_fixture_t* fixture) {
    if (fixture->a != NULL) {
        (void)spw_port_stop(fixture->a);
        (void)spw_port_close(fixture->a);
        fixture->a = NULL;
    }
    if (fixture->b != NULL) {
        (void)spw_port_stop(fixture->b);
        (void)spw_port_close(fixture->b);
        fixture->b = NULL;
    }
    packet_close(&fixture->spw_a);
    packet_close(&fixture->spw_b);
    packet_close(&fixture->native_a);
    packet_close(&fixture->native_b);
}

static int service_sender_ack(spw_port_t* sender) {
    unsigned attempt;
    for (attempt = 0u; attempt < 8u; ++attempt) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(sender, &state) != SPW_OK ||
            state != SPW_LINK_RUN) {
            return 0;
        }
    }
    return 1;
}

static int spw_send(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = g_payload;
    packet.length = payload_size;
    packet.capacity = payload_size;
    packet.terminator = SPW_TERMINATOR_EOP;
    return spw_port_send(port, &packet, 500000u) == SPW_OK;
}

static int spw_receive(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = g_receive;
    packet.capacity = sizeof(g_receive);
    if (spw_port_receive(port, &packet, 500000u) != SPW_OK ||
        packet.length != payload_size ||
        packet.terminator != SPW_TERMINATOR_EOP) {
        return 0;
    }
    return payload_size == 0u ||
           memcmp(g_receive, g_payload, payload_size) == 0;
}

static size_t native_fragment_count(size_t payload_size) {
    return payload_size == 0u
               ? 1u
               : (payload_size + RAW_BENCH_FRAGMENT_PAYLOAD - 1u) /
                     RAW_BENCH_FRAGMENT_PAYLOAD;
}

static int native_send(raw_fixture_t* fixture, size_t payload_size) {
    const size_t count = native_fragment_count(payload_size);
    size_t offset = 0u;
    size_t index;
    for (index = 0u; index < count; ++index) {
        const size_t remaining = payload_size - offset;
        const size_t chunk =
            payload_size == 0u
                ? 0u
                : (remaining < RAW_BENCH_FRAGMENT_PAYLOAD
                       ? remaining
                       : RAW_BENCH_FRAGMENT_PAYLOAD);
        const size_t frame_size = RAW_BENCH_DIRECT_HEADER_SIZE + chunk;
        memset(g_frame, 0, frame_size);
        memcpy(g_frame, fixture->native_b.mac, ETH_ALEN);
        memcpy(g_frame + ETH_ALEN, fixture->native_a.mac, ETH_ALEN);
        g_frame[12] = (uint8_t)(RAW_BENCH_DIRECT_ETHERTYPE >> 8u);
        g_frame[13] = (uint8_t)RAW_BENCH_DIRECT_ETHERTYPE;
        g_frame[14] = (uint8_t)(index >> 8u);
        g_frame[15] = (uint8_t)index;
        g_frame[16] = (uint8_t)(count >> 8u);
        g_frame[17] = (uint8_t)count;
        if (chunk != 0u) {
            memcpy(g_frame + RAW_BENCH_DIRECT_HEADER_SIZE,
                   g_payload + offset, chunk);
        }
        if (packet_send(&fixture->native_a, g_frame, frame_size,
                        500000u) != SPW_OK) {
            return 0;
        }
        offset += chunk;
    }
    return 1;
}

static int native_receive(raw_fixture_t* fixture, size_t payload_size) {
    const size_t count = native_fragment_count(payload_size);
    size_t offset = 0u;
    size_t index;
    memset(g_receive, 0, sizeof(g_receive));
    for (index = 0u; index < count; ++index) {
        size_t frame_size = 0u;
        size_t expected_chunk;
        const size_t remaining = payload_size - offset;
        expected_chunk =
            payload_size == 0u
                ? 0u
                : (remaining < RAW_BENCH_FRAGMENT_PAYLOAD
                       ? remaining
                       : RAW_BENCH_FRAGMENT_PAYLOAD);
        if (packet_receive(&fixture->native_b, g_frame, sizeof(g_frame),
                           &frame_size, 500000u) != SPW_OK ||
            frame_size < RAW_BENCH_DIRECT_HEADER_SIZE + expected_chunk ||
            memcmp(g_frame, fixture->native_b.mac, ETH_ALEN) != 0 ||
            memcmp(g_frame + ETH_ALEN, fixture->native_a.mac, ETH_ALEN) != 0 ||
            g_frame[12] !=
                (uint8_t)(RAW_BENCH_DIRECT_ETHERTYPE >> 8u) ||
            g_frame[13] != (uint8_t)RAW_BENCH_DIRECT_ETHERTYPE ||
            (((size_t)g_frame[14] << 8u) | g_frame[15]) != index ||
            (((size_t)g_frame[16] << 8u) | g_frame[17]) != count) {
            return 0;
        }
        if (expected_chunk != 0u) {
            memcpy(g_receive + offset,
                   g_frame + RAW_BENCH_DIRECT_HEADER_SIZE,
                   expected_chunk);
        }
        offset += expected_chunk;
    }
    return payload_size == 0u ||
           memcmp(g_receive, g_payload, payload_size) == 0;
}

static int sample_native_tx(raw_fixture_t* fixture,
                            size_t payload_size,
                            uint64_t* out_delta) {
    const uint64_t start = spw_profile_counter_read();
    const int ok = native_send(fixture, payload_size);
    const uint64_t end = spw_profile_counter_read();
    if (!ok || !native_receive(fixture, payload_size)) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_spw_tx(raw_fixture_t* fixture,
                         size_t payload_size,
                         uint64_t* out_delta) {
    const uint64_t start = spw_profile_counter_read();
    const int ok = spw_send(fixture->a, payload_size);
    const uint64_t end = spw_profile_counter_read();
    if (!ok || !spw_receive(fixture->b, payload_size) ||
        !service_sender_ack(fixture->a)) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_native_rx(raw_fixture_t* fixture,
                            size_t payload_size,
                            uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    int ok;
    if (!native_send(fixture, payload_size)) {
        return 0;
    }
    start = spw_profile_counter_read();
    ok = native_receive(fixture, payload_size);
    end = spw_profile_counter_read();
    if (!ok) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_spw_rx(raw_fixture_t* fixture,
                         size_t payload_size,
                         uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    int ok;
    if (!spw_send(fixture->a, payload_size)) {
        return 0;
    }
    start = spw_profile_counter_read();
    ok = spw_receive(fixture->b, payload_size);
    end = spw_profile_counter_read();
    if (!ok || !service_sender_ack(fixture->a)) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int run_pair(raw_fixture_t* fixture,
                    raw_direction_t direction,
                    size_t payload_size,
                    size_t iteration,
                    uint64_t* native_delta,
                    uint64_t* spwkit_delta) {
    int native_ok;
    int spwkit_ok;
    if ((iteration & 1u) == 0u) {
        native_ok = direction == RAW_DIRECTION_TX
                        ? sample_native_tx(fixture, payload_size, native_delta)
                        : sample_native_rx(fixture, payload_size, native_delta);
        spwkit_ok = direction == RAW_DIRECTION_TX
                        ? sample_spw_tx(fixture, payload_size, spwkit_delta)
                        : sample_spw_rx(fixture, payload_size, spwkit_delta);
    } else {
        spwkit_ok = direction == RAW_DIRECTION_TX
                        ? sample_spw_tx(fixture, payload_size, spwkit_delta)
                        : sample_spw_rx(fixture, payload_size, spwkit_delta);
        native_ok = direction == RAW_DIRECTION_TX
                        ? sample_native_tx(fixture, payload_size, native_delta)
                        : sample_native_rx(fixture, payload_size, native_delta);
    }
    return native_ok && spwkit_ok;
}

static void print_statistics(const raw_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(raw_direction_t direction,
                         size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const raw_statistics_t* native_statistics,
                         const raw_statistics_t* spwkit_statistics) {
    const double median_delta =
        spwkit_statistics->median - native_statistics->median;
    const double mean_delta =
        spwkit_statistics->mean - native_statistics->mean;
    const long long p95_delta =
        (long long)spwkit_statistics->p95 -
        (long long)native_statistics->p95;
    const long long p99_delta =
        (long long)spwkit_statistics->p99 -
        (long long)native_statistics->p99;
    const char* direction_name =
        direction == RAW_DIRECTION_TX ? "tx" : "rx";

    printf("{\"schema\":\"spwkit.profile.comparison.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"direction\":\"%s\"", direction_name);
    printf(",\"backend\":\"raw-ethernet\"");
    printf(",\"spwkit_path\":\"vspw-tp\"");
    printf(",\"native_path\":\"direct-af-packet\"");
    printf(",\"boundary\":\"complete-public-api-operation\"");
    printf(",\"provider_fixture\":\"linux-af-packet-veth\"");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"carrier_frames\":%zu", native_fragment_count(payload_size));
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,"
           "\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"native_statistics\":");
    print_statistics(native_statistics);
    printf(",\"spwkit_statistics\":");
    print_statistics(spwkit_statistics);
    printf(",\"delta\":{\"definition\":\"spwkit-minus-native\"");
    printf(",\"median_ticks\":%.3f", median_delta);
    printf(",\"mean_ticks\":%.3f", mean_delta);
    printf(",\"p95_ticks\":%lld", p95_delta);
    printf(",\"p99_ticks\":%lld", p99_delta);
    if (native_statistics->median != 0.0) {
        printf(",\"median_percent\":%.3f",
               100.0 * median_delta / native_statistics->median);
    } else {
        printf(",\"median_percent\":null");
    }
    printf("}}\n");
}

static void usage(const char* program) {
    fprintf(stderr,
            "Usage: %s --interface-a IFACE --interface-b IFACE "
            "--direction tx|rx [--warmup N] [--iterations N] [--payload N]\n",
            program);
}

int main(int argc, char** argv) {
    const char* interface_a = NULL;
    const char* interface_b = NULL;
    raw_direction_t direction = RAW_DIRECTION_TX;
    bool have_direction = false;
    size_t warmup_iterations = 32u;
    size_t iterations = 128u;
    size_t payload_size = 64u;
    static raw_fixture_t fixture;
    raw_statistics_t native_statistics;
    raw_statistics_t spwkit_statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--interface-a") == 0 &&
            i + 1u < (size_t)argc) {
            interface_a = argv[++i];
        } else if (strcmp(argv[i], "--interface-b") == 0 &&
                   i + 1u < (size_t)argc) {
            interface_b = argv[++i];
        } else if (strcmp(argv[i], "--direction") == 0 &&
                   i + 1u < (size_t)argc) {
            const char* value = argv[++i];
            if (strcmp(value, "tx") == 0) {
                direction = RAW_DIRECTION_TX;
            } else if (strcmp(value, "rx") == 0) {
                direction = RAW_DIRECTION_RX;
            } else {
                usage(argv[0]);
                return 2;
            }
            have_direction = true;
        } else if (strcmp(argv[i], "--warmup") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, RAW_BENCH_MAX_SAMPLES,
                            &iterations)) {
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, RAW_BENCH_MAX_PAYLOAD,
                            &payload_size)) {
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (interface_a == NULL || interface_b == NULL || !have_direction) {
        usage(argv[0]);
        return 2;
    }

    for (i = 0u; i < payload_size; ++i) {
        g_payload[i] = (uint8_t)((i * 17u + 3u) & 0xffu);
    }
    memset(g_receive, 0, sizeof(g_receive));

    if (!fixture_open(&fixture, interface_a, interface_b)) {
        fprintf(stderr, "failed to open AF_PACKET raw-Ethernet fixture\n");
        fixture_close(&fixture);
        return 1;
    }

    spw_profile_prepare();
    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t native_delta;
        uint64_t spwkit_delta;
        if (!run_pair(&fixture, direction, payload_size, i,
                      &native_delta, &spwkit_delta)) {
            fprintf(stderr, "raw-Ethernet warmup failed at %zu\n", i);
            fixture_close(&fixture);
            return 1;
        }
    }
    for (i = 0u; i < iterations; ++i) {
        if (!run_pair(&fixture, direction, payload_size, i,
                      &g_native_samples[i], &g_spwkit_samples[i])) {
            fprintf(stderr, "raw-Ethernet measurement failed at %zu\n", i);
            fixture_close(&fixture);
            return 1;
        }
    }

    fixture_close(&fixture);
    native_statistics =
        calculate_statistics(g_native_samples, iterations);
    spwkit_statistics =
        calculate_statistics(g_spwkit_samples, iterations);
    print_result(direction, payload_size, warmup_iterations, iterations,
                 &native_statistics, &spwkit_statistics);
    return 0;
}
