// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "backends/ethernet/fragment_reassembler.h"
#include "backends/ethernet/vspw_tp.h"
#include "profiling/profile.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SPW_UDP_BREAKDOWN_MAX_SAMPLES 4096u
#define SPW_UDP_BREAKDOWN_MAX_PAYLOAD 4096u
#define SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD 1200u
#define SPW_UDP_BREAKDOWN_REASSEMBLY_CAPACITY (1024u * 1024u)
#define SPW_UDP_BREAKDOWN_MAX_FRAGMENTS 8u

#define COVERAGE_WORDS SPW_FRAGMENT_COVERAGE_WORDS(SPW_UDP_BREAKDOWN_REASSEMBLY_CAPACITY)

typedef struct breakdown_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} breakdown_statistics_t;

typedef struct socket_fixture {
    int tx_fd;
    int rx_fd;
    struct sockaddr_in destination;
    struct sockaddr_in source_template;
    char destination_text[INET_ADDRSTRLEN];
} socket_fixture_t;

typedef struct fragment_fixture {
    spw_fragment_reassembler_t reassembler;
    spw_vspw_tp_header_t headers[SPW_UDP_BREAKDOWN_MAX_FRAGMENTS];
    size_t fragment_count;
} fragment_fixture_t;

static uint64_t g_samples[SPW_UDP_BREAKDOWN_MAX_SAMPLES];
static uint8_t g_payload[SPW_UDP_BREAKDOWN_MAX_PAYLOAD];
static uint8_t g_copy_a[SPW_UDP_BREAKDOWN_MAX_PAYLOAD];
static uint8_t g_copy_b[SPW_UDP_BREAKDOWN_MAX_PAYLOAD];
static uint8_t g_datagram[SPW_VSPW_TP_HEADER_SIZE + SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD];
static uint8_t g_ack_datagram[SPW_VSPW_TP_HEADER_SIZE + SPW_VSPW_TP_ACK_PAYLOAD_SIZE];
static uint8_t g_rx_datagram[SPW_UDP_BREAKDOWN_MAX_PAYLOAD + SPW_VSPW_TP_HEADER_SIZE];
static uint8_t g_reassembly_data[SPW_UDP_BREAKDOWN_REASSEMBLY_CAPACITY];
static uint64_t g_reassembly_coverage[COVERAGE_WORDS];
static uint8_t g_pending_packet[SPW_UDP_BREAKDOWN_MAX_PAYLOAD];
static uint8_t g_output_packet[SPW_UDP_BREAKDOWN_MAX_PAYLOAD];
static volatile uint64_t g_sink;

static int parse_size(const char* text, size_t minimum, size_t maximum,
                      size_t* out_value) {
    char* end = NULL;
    unsigned long long value;
    if (text == NULL || out_value == NULL || text[0] == '\0') {
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

static size_t percentile_index(size_t count, unsigned int percentile) {
    size_t rank = ((size_t)percentile * count + 99u) / 100u;
    if (rank == 0u) {
        rank = 1u;
    }
    if (rank > count) {
        rank = count;
    }
    return rank - 1u;
}

static breakdown_statistics_t calculate_statistics(uint64_t* samples,
                                                   size_t count) {
    breakdown_statistics_t statistics;
    double sum = 0.0;
    double variance_sum = 0.0;
    size_t i;
    memset(&statistics, 0, sizeof(statistics));
    sort_samples(samples, count);
    statistics.minimum = samples[0u];
    statistics.maximum = samples[count - 1u];
    statistics.p95 = samples[percentile_index(count, 95u)];
    statistics.p99 = samples[percentile_index(count, 99u)];
    statistics.median = (count & 1u) != 0u
                            ? (double)samples[count / 2u]
                            : ((double)samples[count / 2u - 1u] +
                               (double)samples[count / 2u]) / 2.0;
    for (i = 0u; i < count; ++i) {
        sum += (double)samples[i];
    }
    statistics.mean = sum / (double)count;
    for (i = 0u; i < count; ++i) {
        const double difference = (double)samples[i] - statistics.mean;
        variance_sum += difference * difference;
    }
    statistics.standard_deviation = sqrt(variance_sum / (double)count);
    return statistics;
}

static void print_statistics(const breakdown_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(const char* stage, const char* domain,
                         size_t payload_size, size_t fragment_count,
                         size_t warmup, size_t iterations,
                         const breakdown_statistics_t* statistics,
                         const char* model) {
    printf("{\"schema\":\"spwkit.profile.udp-breakdown.v1\"");
    printf(",\"measurement_domain\":\"diagnostic-component\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"stage\":\"%s\"", stage);
    printf(",\"domain\":\"%s\"", domain);
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"fragment_payload_bytes\":%u", SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD);
    printf(",\"fragment_count\":%zu", fragment_count);
    printf(",\"warmup_iterations\":%zu", warmup);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"diagnostic_only\":true");
    printf(",\"additive_to_api_interval\":false");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"model\":\"%s\"", model);
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"statistics\":");
    print_statistics(statistics);
    printf("}\n");
}

static int socket_fixture_open(socket_fixture_t* fixture) {
    struct sockaddr_in rx_address;
    socklen_t address_size = sizeof(rx_address);
    memset(fixture, 0, sizeof(*fixture));
    fixture->tx_fd = -1;
    fixture->rx_fd = -1;
    fixture->tx_fd = socket(AF_INET, SOCK_DGRAM, 0);
    fixture->rx_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fixture->tx_fd < 0 || fixture->rx_fd < 0) {
        return 0;
    }
    memset(&rx_address, 0, sizeof(rx_address));
    rx_address.sin_family = AF_INET;
    rx_address.sin_port = 0;
    rx_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fixture->rx_fd, (const struct sockaddr*)&rx_address,
             sizeof(rx_address)) != 0 ||
        getsockname(fixture->rx_fd, (struct sockaddr*)&rx_address,
                    &address_size) != 0) {
        return 0;
    }
    fixture->destination = rx_address;
    strcpy(fixture->destination_text, "127.0.0.1");
    fixture->source_template.sin_family = AF_INET;
    fixture->source_template.sin_port = htons(54321u);
    fixture->source_template.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return 1;
}

static void socket_fixture_close(socket_fixture_t* fixture) {
    if (fixture->tx_fd >= 0) {
        close(fixture->tx_fd);
    }
    if (fixture->rx_fd >= 0) {
        close(fixture->rx_fd);
    }
}

static int drain_datagram(const socket_fixture_t* fixture, size_t expected) {
    const ssize_t received = recvfrom(fixture->rx_fd, g_rx_datagram,
                                      sizeof(g_rx_datagram), 0, NULL, NULL);
    return received >= 0 && (size_t)received == expected;
}

static int queue_datagram(const socket_fixture_t* fixture, size_t payload_size) {
    const ssize_t sent = sendto(fixture->tx_fd, g_payload, payload_size, 0,
                                (const struct sockaddr*)&fixture->destination,
                                sizeof(fixture->destination));
    return sent >= 0 && (size_t)sent == payload_size;
}

static int poll_writable(int fd) {
    struct pollfd descriptor;
    int ready;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = fd;
    descriptor.events = POLLOUT;
    ready = poll(&descriptor, 1, 500);
    return ready > 0 && (descriptor.revents & POLLOUT) != 0;
}

static int poll_readable(int fd) {
    struct pollfd descriptor;
    int ready;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = fd;
    descriptor.events = POLLIN;
    ready = poll(&descriptor, 1, 500);
    return ready > 0 && (descriptor.revents & POLLIN) != 0;
}

typedef int (*sample_fn_t)(void* context, size_t payload_size, uint64_t* delta);

static int run_stage(const char* stage, const char* domain, const char* model,
                     void* context, sample_fn_t sample, size_t payload_size,
                     size_t fragment_count, size_t warmup, size_t iterations) {
    breakdown_statistics_t statistics;
    size_t i;
    uint64_t delta;
    for (i = 0u; i < warmup; ++i) {
        if (!sample(context, payload_size, &delta)) {
            fprintf(stderr, "stage warmup failed: %s\n", stage);
            return 0;
        }
        g_sink ^= delta;
    }
    for (i = 0u; i < iterations; ++i) {
        if (!sample(context, payload_size, &g_samples[i])) {
            fprintf(stderr, "stage sample failed: %s at %zu\n", stage, i);
            return 0;
        }
    }
    statistics = calculate_statistics(g_samples, iterations);
    print_result(stage, domain, payload_size, fragment_count, warmup, iterations,
                 &statistics, model);
    return 1;
}

static int sample_sendto(void* context, size_t payload_size, uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    uint64_t start = spw_profile_counter_read();
    const ssize_t sent = sendto(fixture->tx_fd, g_payload, payload_size, 0,
                                (const struct sockaddr*)&fixture->destination,
                                sizeof(fixture->destination));
    uint64_t end = spw_profile_counter_read();
    if (sent < 0 || (size_t)sent != payload_size ||
        !drain_datagram(fixture, payload_size)) {
        return 0;
    }
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_poll_sendto(void* context, size_t payload_size, uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    ssize_t sent;
    uint64_t start = spw_profile_counter_read();
    if (!poll_writable(fixture->tx_fd)) {
        return 0;
    }
    sent = sendto(fixture->tx_fd, g_payload, payload_size, 0,
                  (const struct sockaddr*)&fixture->destination,
                  sizeof(fixture->destination));
    {
        uint64_t end = spw_profile_counter_read();
        if (sent < 0 || (size_t)sent != payload_size ||
            !drain_datagram(fixture, payload_size)) {
            return 0;
        }
        *delta = spw_profile_counter_delta(start, end);
    }
    return 1;
}

static int sample_prepare_poll_sendto(void* context, size_t payload_size,
                                      uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    struct sockaddr_in remote;
    ssize_t sent;
    uint64_t start = spw_profile_counter_read();
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = fixture->destination.sin_port;
    if (inet_pton(AF_INET, fixture->destination_text, &remote.sin_addr) != 1 ||
        !poll_writable(fixture->tx_fd)) {
        return 0;
    }
    sent = sendto(fixture->tx_fd, g_payload, payload_size, 0,
                  (const struct sockaddr*)&remote, sizeof(remote));
    {
        uint64_t end = spw_profile_counter_read();
        if (sent < 0 || (size_t)sent != payload_size ||
            !drain_datagram(fixture, payload_size)) {
            return 0;
        }
        *delta = spw_profile_counter_delta(start, end);
    }
    return 1;
}

static int sample_recvfrom(void* context, size_t payload_size, uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    ssize_t received;
    uint64_t start;
    uint64_t end;
    if (!queue_datagram(fixture, payload_size)) {
        return 0;
    }
    start = spw_profile_counter_read();
    received = recvfrom(fixture->rx_fd, g_rx_datagram, sizeof(g_rx_datagram),
                        0, NULL, NULL);
    end = spw_profile_counter_read();
    if (received < 0 || (size_t)received != payload_size) {
        return 0;
    }
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_poll_recvfrom(void* context, size_t payload_size,
                                uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    ssize_t received;
    uint64_t start;
    uint64_t end;
    if (!queue_datagram(fixture, payload_size)) {
        return 0;
    }
    start = spw_profile_counter_read();
    if (!poll_readable(fixture->rx_fd)) {
        return 0;
    }
    received = recvfrom(fixture->rx_fd, g_rx_datagram, sizeof(g_rx_datagram),
                        0, NULL, NULL);
    end = spw_profile_counter_read();
    if (received < 0 || (size_t)received != payload_size) {
        return 0;
    }
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_source_validate(void* context, size_t payload_size,
                                  uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    struct in_addr expected;
    volatile int valid;
    uint64_t start;
    uint64_t end;
    (void)payload_size;
    start = spw_profile_counter_read();
    memset(&expected, 0, sizeof(expected));
    valid = fixture->source_template.sin_family == AF_INET &&
            fixture->source_template.sin_port == htons(54321u) &&
            inet_pton(AF_INET, fixture->destination_text, &expected) == 1 &&
            fixture->source_template.sin_addr.s_addr == expected.s_addr;
    end = spw_profile_counter_read();
    if (!valid) {
        return 0;
    }
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static spw_vspw_tp_header_t make_data_header(size_t payload_size) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    const size_t fragment = payload_size > SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD
                                ? SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD
                                : payload_size;
    header.type = SPW_VSPW_TP_DATA;
    header.flags = SPW_VSPW_TP_FLAG_EOP | SPW_VSPW_TP_FLAG_ACK_REQUIRED;
    if (payload_size > SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD) {
        header.flags |= SPW_VSPW_TP_FLAG_FRAGMENT_START;
    }
    header.payload_size = (uint16_t)fragment;
    header.link_id = UINT32_C(0x42575031);
    header.session_id = UINT64_C(0x1234567812345678);
    header.sequence = 1u;
    header.message_id = 1u;
    header.fragment_offset = 0u;
    header.total_size = (uint32_t)payload_size;
    return header;
}

static int sample_header_encode(void* context, size_t payload_size,
                                uint64_t* delta) {
    spw_vspw_tp_header_t header = make_data_header(payload_size);
    uint64_t start;
    uint64_t end;
    (void)context;
    start = spw_profile_counter_read();
    if (!spw_vspw_tp_encode_header(&header, g_datagram, sizeof(g_datagram))) {
        return 0;
    }
    end = spw_profile_counter_read();
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_header_decode(void* context, size_t payload_size,
                                uint64_t* delta) {
    spw_vspw_tp_header_t header = make_data_header(payload_size);
    spw_vspw_tp_header_t decoded = SPW_VSPW_TP_HEADER_INITIALIZER;
    uint64_t start;
    uint64_t end;
    (void)context;
    if (!spw_vspw_tp_encode_header(&header, g_datagram, sizeof(g_datagram))) {
        return 0;
    }
    start = spw_profile_counter_read();
    if (spw_vspw_tp_decode_header(g_datagram,
                                  SPW_VSPW_TP_HEADER_SIZE + header.payload_size,
                                  &decoded) != SPW_VSPW_TP_DECODE_OK) {
        return 0;
    }
    end = spw_profile_counter_read();
    g_sink ^= decoded.sequence;
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_clock_gettime(void* context, size_t payload_size,
                                uint64_t* delta) {
    struct timespec now;
    uint64_t start;
    uint64_t end;
    (void)context;
    (void)payload_size;
    start = spw_profile_counter_read();
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    end = spw_profile_counter_read();
    g_sink ^= (uint64_t)now.tv_nsec;
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_copy_once(void* context, size_t payload_size, uint64_t* delta) {
    uint64_t start;
    uint64_t end;
    (void)context;
    start = spw_profile_counter_read();
    if (payload_size != 0u) {
        memcpy(g_copy_a, g_payload, payload_size);
    }
    end = spw_profile_counter_read();
    g_sink ^= g_copy_a[payload_size == 0u ? 0u : payload_size - 1u];
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_copy_twice(void* context, size_t payload_size, uint64_t* delta) {
    uint64_t start;
    uint64_t end;
    (void)context;
    start = spw_profile_counter_read();
    if (payload_size != 0u) {
        memcpy(g_copy_a, g_payload, payload_size);
        memcpy(g_copy_b, g_copy_a, payload_size);
    }
    end = spw_profile_counter_read();
    g_sink ^= g_copy_b[payload_size == 0u ? 0u : payload_size - 1u];
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_ack_encode_validate_send(void* context, size_t payload_size,
                                           uint64_t* delta) {
    socket_fixture_t* fixture = (socket_fixture_t*)context;
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    spw_vspw_tp_header_t decoded = SPW_VSPW_TP_HEADER_INITIALIZER;
    struct sockaddr_in remote;
    ssize_t sent;
    uint64_t start;
    uint64_t end;
    (void)payload_size;
    header.type = SPW_VSPW_TP_ACK;
    header.payload_size = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;
    header.link_id = UINT32_C(0x42575031);
    header.session_id = UINT64_C(0x1234567812345678);
    header.sequence = 2u;
    header.message_id = 1u;
    header.total_size = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;
    start = spw_profile_counter_read();
    if (!spw_vspw_tp_encode_header(&header, g_ack_datagram,
                                   sizeof(g_ack_datagram)) ||
        !spw_vspw_tp_encode_ack_payload(
            UINT64_C(0x8877665544332211),
            g_ack_datagram + SPW_VSPW_TP_HEADER_SIZE,
            SPW_VSPW_TP_ACK_PAYLOAD_SIZE) ||
        spw_vspw_tp_decode_header(g_ack_datagram, sizeof(g_ack_datagram),
                                  &decoded) != SPW_VSPW_TP_DECODE_OK) {
        return 0;
    }
    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = fixture->destination.sin_port;
    if (inet_pton(AF_INET, fixture->destination_text, &remote.sin_addr) != 1 ||
        !poll_writable(fixture->tx_fd)) {
        return 0;
    }
    sent = sendto(fixture->tx_fd, g_ack_datagram, sizeof(g_ack_datagram), 0,
                  (const struct sockaddr*)&remote, sizeof(remote));
    end = spw_profile_counter_read();
    if (sent != (ssize_t)sizeof(g_ack_datagram) ||
        !drain_datagram(fixture, sizeof(g_ack_datagram))) {
        return 0;
    }
    g_sink ^= decoded.type;
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int fragment_fixture_prepare(fragment_fixture_t* fixture,
                                    size_t payload_size) {
    size_t offset = 0u;
    size_t index = 0u;
    memset(fixture, 0, sizeof(*fixture));
    if (payload_size <= SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD) {
        return 0;
    }
    spw_fragment_reassembler_init(
        &fixture->reassembler, g_reassembly_data,
        SPW_UDP_BREAKDOWN_REASSEMBLY_CAPACITY, g_reassembly_coverage,
        COVERAGE_WORDS);
    while (offset < payload_size) {
        const size_t remaining = payload_size - offset;
        const size_t size = remaining > SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD
                                ? SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD
                                : remaining;
        spw_vspw_tp_header_t* header;
        if (index >= SPW_UDP_BREAKDOWN_MAX_FRAGMENTS) {
            return 0;
        }
        header = &fixture->headers[index];
        *header = (spw_vspw_tp_header_t)SPW_VSPW_TP_HEADER_INITIALIZER;
        header->type = SPW_VSPW_TP_DATA;
        header->flags = SPW_VSPW_TP_FLAG_EOP | SPW_VSPW_TP_FLAG_ACK_REQUIRED;
        if (offset == 0u) {
            header->flags |= SPW_VSPW_TP_FLAG_FRAGMENT_START;
        }
        if (offset + size == payload_size) {
            header->flags |= SPW_VSPW_TP_FLAG_FRAGMENT_END;
        }
        header->payload_size = (uint16_t)size;
        header->link_id = UINT32_C(0x42575031);
        header->session_id = UINT64_C(0x1234567812345678);
        header->sequence = (uint32_t)(index + 1u);
        header->message_id = 1u;
        header->fragment_offset = (uint32_t)offset;
        header->total_size = (uint32_t)payload_size;
        offset += size;
        ++index;
    }
    fixture->fragment_count = index;
    return 1;
}

static int push_all_fragments(fragment_fixture_t* fixture, size_t payload_size) {
    size_t i;
    for (i = 0u; i < fixture->fragment_count; ++i) {
        const spw_vspw_tp_header_t* header = &fixture->headers[i];
        const spw_reassembly_result_t result = spw_fragment_reassembler_push(
            &fixture->reassembler, header,
            g_payload + header->fragment_offset);
        if (i + 1u == fixture->fragment_count) {
            if (result != SPW_REASSEMBLY_COMPLETE) {
                return 0;
            }
        } else if (result != SPW_REASSEMBLY_ACCEPTED) {
            return 0;
        }
    }
    return fixture->reassembler.total_size == payload_size;
}

static int sample_reassembly_push(void* context, size_t payload_size,
                                  uint64_t* delta) {
    fragment_fixture_t* fixture = (fragment_fixture_t*)context;
    uint64_t start;
    uint64_t end;
    spw_fragment_reassembler_reset(&fixture->reassembler);
    start = spw_profile_counter_read();
    if (!push_all_fragments(fixture, payload_size)) {
        return 0;
    }
    end = spw_profile_counter_read();
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_reassembly_reset(void* context, size_t payload_size,
                                   uint64_t* delta) {
    fragment_fixture_t* fixture = (fragment_fixture_t*)context;
    uint64_t start;
    uint64_t end;
    (void)payload_size;
    g_reassembly_coverage[0] ^= UINT64_C(0xffffffffffffffff);
    start = spw_profile_counter_read();
    spw_fragment_reassembler_reset(&fixture->reassembler);
    end = spw_profile_counter_read();
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_reassembly_delivery(void* context, size_t payload_size,
                                      uint64_t* delta) {
    fragment_fixture_t* fixture = (fragment_fixture_t*)context;
    uint64_t start;
    uint64_t end;
    spw_fragment_reassembler_reset(&fixture->reassembler);
    start = spw_profile_counter_read();
    if (!push_all_fragments(fixture, payload_size)) {
        return 0;
    }
    memcpy(g_pending_packet, fixture->reassembler.data, payload_size);
    spw_fragment_reassembler_reset(&fixture->reassembler);
    memcpy(g_output_packet, g_pending_packet, payload_size);
    end = spw_profile_counter_read();
    g_sink ^= g_output_packet[payload_size - 1u];
    *delta = spw_profile_counter_delta(start, end);
    return 1;
}

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s --payload N [--warmup N] [--iterations N]\n",
            program);
}

int main(int argc, char** argv) {
    size_t payload_size = 0u;
    size_t warmup = 256u;
    size_t iterations = 1024u;
    int have_payload = 0;
    socket_fixture_t sockets;
    fragment_fixture_t fragments;
    size_t fragment_count;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--payload") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_UDP_BREAKDOWN_MAX_PAYLOAD,
                            &payload_size)) {
                print_usage(argv[0]);
                return 2;
            }
            have_payload = 1;
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_UDP_BREAKDOWN_MAX_SAMPLES,
                            &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (!have_payload) {
        print_usage(argv[0]);
        return 2;
    }

    for (i = 0u; i < sizeof(g_payload); ++i) {
        g_payload[i] = (uint8_t)((i * 37u + 11u) & 0xffu);
    }
    spw_profile_prepare();
    if (!socket_fixture_open(&sockets)) {
        fprintf(stderr, "failed to create UDP loopback fixture\n");
        return 1;
    }

    fragment_count = payload_size == 0u
                         ? 1u
                         : (payload_size + SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD - 1u) /
                               SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD;

    if (!run_stage("sendto", "tx", "pre-resolved sendto",
                   &sockets, sample_sendto, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("poll_sendto", "tx", "poll(POLLOUT)+pre-resolved sendto",
                   &sockets, sample_poll_sendto, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("prepare_poll_sendto", "tx",
                   "sockaddr memset+inet_pton+poll(POLLOUT)+sendto",
                   &sockets, sample_prepare_poll_sendto, payload_size,
                   fragment_count, warmup, iterations) ||
        !run_stage("header_encode", "protocol", "VSPW-TP DATA header encode/validate",
                   NULL, sample_header_encode, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("header_decode", "protocol", "VSPW-TP DATA header decode/validate",
                   NULL, sample_header_decode, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("recvfrom", "rx", "queued recvfrom",
                   &sockets, sample_recvfrom, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("poll_recvfrom", "rx", "poll(POLLIN)+queued recvfrom",
                   &sockets, sample_poll_recvfrom, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("source_validate", "rx", "sockaddr checks+inet_pton+address compare",
                   &sockets, sample_source_validate, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("clock_gettime", "common", "clock_gettime(CLOCK_MONOTONIC)",
                   NULL, sample_clock_gettime, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("copy_once", "copy", "one payload memcpy",
                   NULL, sample_copy_once, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("copy_twice", "copy", "two serial payload memcpy operations",
                   NULL, sample_copy_twice, payload_size, fragment_count,
                   warmup, iterations) ||
        !run_stage("ack_encode_validate_send", "rx-ack",
                   "ACK encode+payload+decode validation+address prepare+poll+sendto",
                   &sockets, sample_ack_encode_validate_send, payload_size,
                   fragment_count, warmup, iterations)) {
        socket_fixture_close(&sockets);
        return 1;
    }

    if (payload_size > SPW_UDP_BREAKDOWN_FRAGMENT_PAYLOAD) {
        if (!fragment_fixture_prepare(&fragments, payload_size) ||
            !run_stage("reassembly_push", "fragmentation",
                       "all fragment_reassembler_push calls; reset outside interval",
                       &fragments, sample_reassembly_push, payload_size,
                       fragments.fragment_count, warmup, iterations) ||
            !run_stage("reassembly_reset_1m", "fragmentation",
                       "reset 1MiB-capacity reassembler (128KiB coverage bitmap)",
                       &fragments, sample_reassembly_reset, payload_size,
                       fragments.fragment_count, warmup, iterations) ||
            !run_stage("reassembly_delivery", "fragmentation",
                       "reassembly push+reassembly-to-pending copy+128KiB reset+pending-to-caller copy",
                       &fragments, sample_reassembly_delivery, payload_size,
                       fragments.fragment_count, warmup, iterations)) {
            socket_fixture_close(&sockets);
            return 1;
        }
    }

    socket_fixture_close(&sockets);
    return g_sink == UINT64_MAX ? 1 : 0;
}
