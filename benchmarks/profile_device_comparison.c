// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <spwkit/spwkit.h>

#include "backends/device/vspw_device_protocol.h"
#include "profiling/profile.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define SPW_DEVICE_BENCH_MAX_SAMPLES 4096u
#define SPW_DEVICE_BENCH_MAX_PAYLOAD 4096u
#define SPW_DEVICE_BENCH_WORKSPACE_SIZE (2u * 1024u * 1024u)
#define SPW_DEVICE_BENCH_TIMEOUT_MS 5000
#define SPW_DEVICE_BENCH_TIMEOUT_US UINT64_C(5000000)

typedef enum device_direction {
    DEVICE_DIRECTION_TX = 0,
    DEVICE_DIRECTION_RX = 1
} device_direction_t;

typedef union device_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_DEVICE_BENCH_WORKSPACE_SIZE];
} device_workspace_t;

typedef struct device_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} device_statistics_t;

typedef struct raw_client {
    int fd;
    uint32_t port_id;
    uint32_t next_request_id;
    uint32_t next_message_id;
    uint32_t link_state;
    bool link_state_valid;
    bool rx_active;
    bool rx_ready;
    uint32_t rx_message_id;
    uint32_t rx_total_size;
    uint32_t rx_next_offset;
    uint8_t rx_terminator_flags;
    uint8_t rx_data[SPW_DEVICE_BENCH_MAX_PAYLOAD];
} raw_client_t;

typedef struct device_fixture {
    raw_client_t raw;
    spw_port_t* port;
    device_workspace_t workspace;
} device_fixture_t;

static uint64_t g_native_samples[SPW_DEVICE_BENCH_MAX_SAMPLES];
static uint64_t g_spwkit_samples[SPW_DEVICE_BENCH_MAX_SAMPLES];
static uint8_t g_payload[SPW_DEVICE_BENCH_MAX_PAYLOAD];
static uint8_t g_receive[SPW_DEVICE_BENCH_MAX_PAYLOAD];
static volatile uint64_t g_sink;

static int parse_size(const char* text, size_t minimum, size_t maximum,
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

static double square_root(double value) {
    double estimate;
    unsigned int i;
    if (value <= 0.0) {
        return 0.0;
    }
    estimate = value > 1.0 ? value : 1.0;
    for (i = 0u; i < 32u; ++i) {
        estimate = 0.5 * (estimate + value / estimate);
    }
    return estimate;
}

static device_statistics_t calculate_statistics(uint64_t* samples, size_t count) {
    device_statistics_t statistics;
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
        statistics.median = ((double)samples[count / 2u - 1u] +
                             (double)samples[count / 2u]) / 2.0;
    }
    for (i = 0u; i < count; ++i) {
        sum += (double)samples[i];
    }
    statistics.mean = sum / (double)count;
    for (i = 0u; i < count; ++i) {
        const double difference = (double)samples[i] - statistics.mean;
        variance_sum += difference * difference;
    }
    statistics.standard_deviation = square_root(variance_sum / (double)count);
    return statistics;
}

static const char* architecture_name(void) {
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#elif defined(__aarch64__)
    return "aarch64";
#else
    return "unknown";
#endif
}

static const char* compiler_name(void) {
#if defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}

static bool wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd descriptor;
    int result;
    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1u, timeout_ms);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & events) != 0;
}

static bool raw_send_record(int fd, const uint8_t* data, size_t size) {
    for (;;) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent == (ssize_t)size) {
            return true;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            if (!wait_fd(fd, POLLOUT, SPW_DEVICE_BENCH_TIMEOUT_MS)) {
                return false;
            }
            continue;
        }
        return false;
    }
}

static ssize_t raw_receive_record(int fd, uint8_t* frame, size_t capacity) {
    for (;;) {
        ssize_t received;
        if (!wait_fd(fd, POLLIN, SPW_DEVICE_BENCH_TIMEOUT_MS)) {
            return -1;
        }
        received = recv(fd, frame, capacity, MSG_DONTWAIT);
        if (received >= 0) {
            return received;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        return -1;
    }
}

static void raw_clear_rx(raw_client_t* client) {
    client->rx_active = false;
    client->rx_ready = false;
    client->rx_message_id = 0u;
    client->rx_total_size = 0u;
    client->rx_next_offset = 0u;
    client->rx_terminator_flags = 0u;
}

static bool raw_process_event(raw_client_t* client,
                              const vspd_header_t* header,
                              const uint8_t* payload) {
    if (header->type == VSPD_MSG_LINK_STATE_EVENT) {
        client->link_state = vspd_decode_u32_payload(payload);
        client->link_state_valid = true;
        return true;
    }
    if (header->type != VSPD_MSG_DATA_RX) {
        return false;
    }

    {
        const bool start = (header->flags & VSPD_FLAG_FRAGMENT_START) != 0u;
        const bool end = (header->flags & VSPD_FLAG_FRAGMENT_END) != 0u;
        if (start) {
            if (client->rx_active || client->rx_ready ||
                header->fragment_offset != 0u ||
                header->total_size > sizeof(client->rx_data)) {
                return false;
            }
            client->rx_active = true;
            client->rx_message_id = header->message_id;
            client->rx_total_size = header->total_size;
            client->rx_next_offset = 0u;
        } else if (!client->rx_active ||
                   client->rx_message_id != header->message_id ||
                   client->rx_total_size != header->total_size) {
            return false;
        }
        if (header->fragment_offset != client->rx_next_offset ||
            header->payload_size > client->rx_total_size - client->rx_next_offset) {
            raw_clear_rx(client);
            return false;
        }
        if (header->payload_size != 0u) {
            memcpy(client->rx_data + client->rx_next_offset,
                   payload,
                   header->payload_size);
        }
        client->rx_next_offset += header->payload_size;
        if (end) {
            if (client->rx_next_offset != client->rx_total_size) {
                raw_clear_rx(client);
                return false;
            }
            client->rx_terminator_flags =
                header->flags & (VSPD_FLAG_EOP | VSPD_FLAG_EEP);
            client->rx_active = false;
            client->rx_ready = true;
        }
    }
    return true;
}

static bool raw_wait_response(raw_client_t* client,
                              uint8_t type,
                              uint32_t request_id,
                              int32_t* out_status,
                              uint8_t* response_payload,
                              uint32_t response_capacity,
                              uint32_t* out_response_size) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    for (;;) {
        vspd_header_t header;
        const uint8_t* payload;
        ssize_t received = raw_receive_record(client->fd, frame, sizeof(frame));
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK) {
            return false;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        if ((header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            if (header.type != type || header.request_id != request_id ||
                header.payload_size > response_capacity) {
                return false;
            }
            if (response_payload != NULL && header.payload_size != 0u) {
                memcpy(response_payload, payload, header.payload_size);
            }
            if (out_response_size != NULL) {
                *out_response_size = header.payload_size;
            }
            if (out_status != NULL) {
                *out_status = header.status;
            }
            return true;
        }
        if (!raw_process_event(client, &header, payload)) {
            return false;
        }
    }
}

static bool raw_request(raw_client_t* client,
                        uint8_t type,
                        const uint8_t* payload,
                        uint32_t payload_size,
                        int32_t* out_status,
                        uint8_t* response_payload,
                        uint32_t response_capacity,
                        uint32_t* out_response_size) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];
    vspd_header_t header;
    uint32_t request_id = client->next_request_id++;
    if (payload_size > VSPD_STATISTICS_PAYLOAD_SIZE) {
        return false;
    }
    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = type;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = payload_size;
    header.request_id = request_id;
    header.port_id = client->port_id;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return false;
    }
    if (payload_size != 0u) {
        memcpy(frame + VSPD_HEADER_SIZE, payload, payload_size);
    }
    if (!raw_send_record(client->fd, frame, VSPD_HEADER_SIZE + payload_size)) {
        return false;
    }
    return raw_wait_response(client, type, request_id, out_status,
                             response_payload, response_capacity,
                             out_response_size);
}

static bool raw_open(raw_client_t* client, const char* socket_path, uint32_t port_id) {
    struct sockaddr_un address;
    uint8_t hello[VSPD_HELLO_PAYLOAD_SIZE] = {
        VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    uint8_t response[VSPD_HELLO_PAYLOAD_SIZE];
    uint32_t response_size = 0u;
    int32_t status = VSPD_STATUS_BACKEND;
    size_t path_length = strlen(socket_path);

    if (path_length >= sizeof(address.sun_path)) {
        return false;
    }
    memset(client, 0, sizeof(*client));
    client->fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (client->fd < 0) {
        return false;
    }
    client->port_id = port_id;
    client->next_request_id = 1u;
    client->next_message_id = 1u;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_length + 1u);
    if (connect(client->fd, (const struct sockaddr*)&address, sizeof(address)) != 0) {
        close(client->fd);
        client->fd = -1;
        return false;
    }
    if (!raw_request(client, VSPD_MSG_HELLO, hello, sizeof(hello),
                     &status, response, sizeof(response), &response_size) ||
        status != VSPD_STATUS_OK || response_size != sizeof(hello) ||
        memcmp(response, hello, sizeof(hello)) != 0 ||
        !raw_request(client, VSPD_MSG_ATTACH, NULL, 0u,
                     &status, NULL, 0u, NULL) || status != VSPD_STATUS_OK ||
        !raw_request(client, VSPD_MSG_START, NULL, 0u,
                     &status, NULL, 0u, NULL) || status != VSPD_STATUS_OK) {
        close(client->fd);
        client->fd = -1;
        return false;
    }
    return true;
}

static void raw_close(raw_client_t* client) {
    if (client->fd >= 0) {
        close(client->fd);
        client->fd = -1;
    }
}

static bool raw_query_state(raw_client_t* client, uint32_t* state) {
    uint8_t response[VSPD_LINK_STATE_PAYLOAD_SIZE];
    uint32_t response_size = 0u;
    int32_t status = VSPD_STATUS_BACKEND;
    if (!raw_request(client, VSPD_MSG_GET_LINK_STATE, NULL, 0u,
                     &status, response, sizeof(response), &response_size) ||
        status != VSPD_STATUS_OK || response_size != VSPD_LINK_STATE_PAYLOAD_SIZE) {
        return false;
    }
    *state = vspd_decode_u32_payload(response);
    return true;
}

static bool raw_wait_run(raw_client_t* client) {
    unsigned int i;
    for (i = 0u; i < 500u; ++i) {
        uint32_t state = 0u;
        if (raw_query_state(client, &state) && state == (uint32_t)SPW_LINK_RUN) {
            return true;
        }
        {
            struct timespec delay = {0, 2 * 1000 * 1000};
            nanosleep(&delay, NULL);
        }
    }
    return false;
}

static bool spw_wait_run(spw_port_t* port) {
    unsigned int i;
    for (i = 0u; i < 500u; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) == SPW_OK && state == SPW_LINK_RUN) {
            return true;
        }
        {
            struct timespec delay = {0, 2 * 1000 * 1000};
            nanosleep(&delay, NULL);
        }
    }
    return false;
}

static bool raw_send_packet(raw_client_t* client,
                            const uint8_t* data,
                            uint32_t length,
                            uint8_t terminator_flag) {
    uint32_t request_id = client->next_request_id++;
    uint32_t message_id = client->next_message_id++;
    uint32_t offset = 0u;
    bool sent_zero = false;
    int32_t status = VSPD_STATUS_BACKEND;

    do {
        uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
        vspd_header_t header;
        uint32_t remaining = length - offset;
        uint32_t chunk = remaining > VSPD_MAX_FRAME_PAYLOAD
                             ? VSPD_MAX_FRAME_PAYLOAD
                             : remaining;
        bool final_fragment = length == 0u || offset + chunk == length;
        memset(&header, 0, sizeof(header));
        header.magic = VSPD_MAGIC;
        header.version_major = VSPD_VERSION_MAJOR;
        header.version_minor = VSPD_VERSION_MINOR;
        header.type = VSPD_MSG_DATA_TX;
        header.header_size = VSPD_HEADER_SIZE;
        header.payload_size = chunk;
        header.request_id = request_id;
        header.port_id = client->port_id;
        header.message_id = message_id;
        header.fragment_offset = offset;
        header.total_size = length;
        if (offset == 0u) {
            header.flags |= VSPD_FLAG_FRAGMENT_START;
        }
        if (final_fragment) {
            header.flags |= VSPD_FLAG_FRAGMENT_END | terminator_flag;
        }
        if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
            return false;
        }
        if (chunk != 0u) {
            memcpy(frame + VSPD_HEADER_SIZE, data + offset, chunk);
        }
        if (!raw_send_record(client->fd, frame, VSPD_HEADER_SIZE + chunk)) {
            return false;
        }
        offset += chunk;
        sent_zero = true;
    } while (offset < length || !sent_zero);

    return raw_wait_response(client, VSPD_MSG_DATA_TX, request_id,
                             &status, NULL, 0u, NULL) &&
           status == VSPD_STATUS_OK;
}

static bool raw_receive_packet(raw_client_t* client,
                               const uint8_t* expected,
                               uint32_t expected_size) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    raw_clear_rx(client);
    while (!client->rx_ready) {
        vspd_header_t header;
        const uint8_t* payload;
        ssize_t received = raw_receive_record(client->fd, frame, sizeof(frame));
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK ||
            (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            return false;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        if (!raw_process_event(client, &header, payload)) {
            return false;
        }
    }
    if (client->rx_total_size != expected_size ||
        client->rx_terminator_flags != VSPD_FLAG_EOP) {
        return false;
    }
    return expected_size == 0u || memcmp(client->rx_data, expected, expected_size) == 0;
}

static bool fixture_open(device_fixture_t* fixture, const char* socket_path) {
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(1u);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_workspace_requirements_t requirements;
    size_t path_length = strlen(socket_path);

    memset(fixture, 0, sizeof(*fixture));
    fixture->raw.fd = -1;
    if (!raw_open(&fixture->raw, socket_path, 0u)) {
        return false;
    }
    if (path_length >= sizeof(device.endpoint)) {
        raw_close(&fixture->raw);
        return false;
    }
    memcpy(device.endpoint, socket_path, path_length + 1u);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);
    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(fixture->workspace.bytes) ||
        spw_port_open_in_place(&config,
                               fixture->workspace.bytes,
                               sizeof(fixture->workspace.bytes),
                               &fixture->port) != SPW_OK ||
        fixture->port == NULL ||
        spw_port_start(fixture->port) != SPW_OK ||
        !raw_wait_run(&fixture->raw) ||
        !spw_wait_run(fixture->port)) {
        if (fixture->port != NULL) {
            (void)spw_port_close(fixture->port);
            fixture->port = NULL;
        }
        raw_close(&fixture->raw);
        return false;
    }
    return true;
}

static void fixture_close(device_fixture_t* fixture) {
    if (fixture->port != NULL) {
        (void)spw_port_close(fixture->port);
        fixture->port = NULL;
    }
    raw_close(&fixture->raw);
}

static bool spw_send_packet(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    packet.data = g_payload;
    packet.length = payload_size;
    packet.capacity = payload_size;
    packet.terminator = SPW_TERMINATOR_EOP;
    return spw_port_send(port, &packet, SPW_DEVICE_BENCH_TIMEOUT_US) == SPW_OK;
}

static bool spw_receive_packet(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    packet.data = g_receive;
    packet.length = 0u;
    packet.capacity = sizeof(g_receive);
    packet.terminator = SPW_TERMINATOR_EOP;
    if (spw_port_receive(port, &packet, SPW_DEVICE_BENCH_TIMEOUT_US) != SPW_OK ||
        packet.length != payload_size || packet.terminator != SPW_TERMINATOR_EOP) {
        return false;
    }
    return payload_size == 0u || memcmp(packet.data, g_payload, payload_size) == 0;
}

static bool sample_native_tx(device_fixture_t* fixture,
                             size_t payload_size,
                             uint64_t* out_delta) {
    uint64_t start = spw_profile_counter_read();
    bool ok = raw_send_packet(&fixture->raw, g_payload,
                              (uint32_t)payload_size, VSPD_FLAG_EOP);
    uint64_t end = spw_profile_counter_read();
    if (!ok || !spw_receive_packet(fixture->port, payload_size)) {
        return false;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool sample_spw_tx(device_fixture_t* fixture,
                          size_t payload_size,
                          uint64_t* out_delta) {
    uint64_t start = spw_profile_counter_read();
    bool ok = spw_send_packet(fixture->port, payload_size);
    uint64_t end = spw_profile_counter_read();
    if (!ok || !raw_receive_packet(&fixture->raw, g_payload, (uint32_t)payload_size)) {
        return false;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool sample_native_rx(device_fixture_t* fixture,
                             size_t payload_size,
                             uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    if (!spw_send_packet(fixture->port, payload_size)) {
        return false;
    }
    start = spw_profile_counter_read();
    if (!raw_receive_packet(&fixture->raw, g_payload, (uint32_t)payload_size)) {
        return false;
    }
    end = spw_profile_counter_read();
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool sample_spw_rx(device_fixture_t* fixture,
                          size_t payload_size,
                          uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    if (!raw_send_packet(&fixture->raw, g_payload,
                         (uint32_t)payload_size, VSPD_FLAG_EOP)) {
        return false;
    }
    start = spw_profile_counter_read();
    if (!spw_receive_packet(fixture->port, payload_size)) {
        return false;
    }
    end = spw_profile_counter_read();
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool run_pair(device_fixture_t* fixture,
                     device_direction_t direction,
                     size_t payload_size,
                     size_t iteration,
                     uint64_t* native_delta,
                     uint64_t* spwkit_delta) {
    bool native_ok;
    bool spwkit_ok;
    if ((iteration & 1u) == 0u) {
        native_ok = direction == DEVICE_DIRECTION_TX
                        ? sample_native_tx(fixture, payload_size, native_delta)
                        : sample_native_rx(fixture, payload_size, native_delta);
        spwkit_ok = direction == DEVICE_DIRECTION_TX
                        ? sample_spw_tx(fixture, payload_size, spwkit_delta)
                        : sample_spw_rx(fixture, payload_size, spwkit_delta);
    } else {
        spwkit_ok = direction == DEVICE_DIRECTION_TX
                        ? sample_spw_tx(fixture, payload_size, spwkit_delta)
                        : sample_spw_rx(fixture, payload_size, spwkit_delta);
        native_ok = direction == DEVICE_DIRECTION_TX
                        ? sample_native_tx(fixture, payload_size, native_delta)
                        : sample_native_rx(fixture, payload_size, native_delta);
    }
    return native_ok && spwkit_ok;
}

static void print_statistics(const device_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(device_direction_t direction,
                         size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const device_statistics_t* native_statistics,
                         const device_statistics_t* spwkit_statistics) {
    const double median_delta = spwkit_statistics->median - native_statistics->median;
    const double mean_delta = spwkit_statistics->mean - native_statistics->mean;
    const long long p95_delta = (long long)spwkit_statistics->p95 -
                                (long long)native_statistics->p95;
    const long long p99_delta = (long long)spwkit_statistics->p99 -
                                (long long)native_statistics->p99;
    const char* direction_name = direction == DEVICE_DIRECTION_TX ? "tx" : "rx";

    printf("{\"schema\":\"spwkit.profile.comparison.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"direction\":\"%s\"", direction_name);
    printf(",\"backend\":\"device\"");
    printf(",\"spwkit_path\":\"vspd\"");
    printf(",\"native_path\":\"direct-vspd-seqpacket\"");
    printf(",\"boundary\":\"complete-public-api-operation\"");
    printf(",\"provider_fixture\":\"same-vspwd-daemon-peer-pair\"");
    printf(",\"carrier\":\"af-unix-seqpacket\"");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\"}", compiler_name());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
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
    if (native_statistics->mean != 0.0) {
        printf(",\"mean_percent\":%.3f",
               100.0 * mean_delta / native_statistics->mean);
    } else {
        printf(",\"mean_percent\":null");
    }
    printf("}}\n");
}

static void usage(const char* program) {
    fprintf(stderr,
            "Usage: %s --socket PATH --direction tx|rx [--warmup N] [--iterations N] [--payload N] [--probe-smoke]\n",
            program);
}

int main(int argc, char** argv) {
    const char* socket_path = NULL;
    device_direction_t direction = DEVICE_DIRECTION_TX;
    int have_direction = 0;
    int probe_smoke = 0;
    size_t warmup_iterations = 64u;
    size_t iterations = 256u;
    size_t payload_size = 64u;
    device_fixture_t fixture;
    device_statistics_t native_statistics;
    device_statistics_t spwkit_statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1u < (size_t)argc) {
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--direction") == 0 && i + 1u < (size_t)argc) {
            const char* value = argv[++i];
            if (strcmp(value, "tx") == 0) {
                direction = DEVICE_DIRECTION_TX;
            } else if (strcmp(value, "rx") == 0) {
                direction = DEVICE_DIRECTION_RX;
            } else {
                usage(argv[0]);
                return 2;
            }
            have_direction = 1;
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_DEVICE_BENCH_MAX_SAMPLES, &iterations)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_DEVICE_BENCH_MAX_PAYLOAD, &payload_size)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--probe-smoke") == 0) {
            probe_smoke = 1;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!have_direction || socket_path == NULL) {
        usage(argv[0]);
        return 2;
    }

    for (i = 0u; i < payload_size; ++i) {
        g_payload[i] = (uint8_t)((i * 23u + 7u) & 0xffu);
    }
    memset(g_receive, 0, sizeof(g_receive));

    if (!fixture_open(&fixture, socket_path)) {
        fprintf(stderr, "failed to open DEVICE/VSPD benchmark fixture\n");
        return 1;
    }
    spw_profile_prepare();
    if (probe_smoke) {
        uint64_t ignored = 0u;
        const volatile spw_profile_sample_t* sample;
        spw_profile_reset();
        if (!(direction == DEVICE_DIRECTION_TX
                  ? sample_spw_tx(&fixture, payload_size, &ignored)
                  : sample_spw_rx(&fixture, payload_size, &ignored))) {
            fprintf(stderr, "DEVICE probe smoke operation failed\n");
            fixture_close(&fixture);
            return 1;
        }
        sample = spw_profile_last_sample();
        if (sample == NULL || sample->sequence != 1u) {
            fprintf(stderr, "DEVICE probe smoke expected sequence=1, got %u\n",
                    sample == NULL ? 0u : sample->sequence);
            fixture_close(&fixture);
            return 1;
        }
        printf("PROBE_SMOKE PASS backend=device direction=%s sequence=%u delta=%llu\n",
               direction == DEVICE_DIRECTION_TX ? "tx" : "rx", sample->sequence,
               (unsigned long long)sample->delta);
        fixture_close(&fixture);
        return 0;
    }

    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t native_delta;
        uint64_t spwkit_delta;
        if (!run_pair(&fixture, direction, payload_size, i,
                      &native_delta, &spwkit_delta)) {
            fprintf(stderr, "DEVICE warmup failed at iteration %zu\n", i);
            fixture_close(&fixture);
            return 1;
        }
        g_sink ^= native_delta ^ spwkit_delta;
    }

    for (i = 0u; i < iterations; ++i) {
        if (!run_pair(&fixture, direction, payload_size, i,
                      &g_native_samples[i], &g_spwkit_samples[i])) {
            fprintf(stderr, "DEVICE measurement failed at iteration %zu\n", i);
            fixture_close(&fixture);
            return 1;
        }
    }

    native_statistics = calculate_statistics(g_native_samples, iterations);
    spwkit_statistics = calculate_statistics(g_spwkit_samples, iterations);
    print_result(direction, payload_size, warmup_iterations, iterations,
                 &native_statistics, &spwkit_statistics);
    fixture_close(&fixture);
    return g_sink == UINT64_MAX ? 1 : 0;
}
