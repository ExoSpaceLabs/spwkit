// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

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
#include <unistd.h>

#define DEVICE_READINESS_MAX_SAMPLES 4096u
#define DEVICE_READINESS_MAX_PAYLOAD 4096u
#define DEVICE_READINESS_TIMEOUT_MS 5000

typedef enum readiness_direction {
    READINESS_TX = 0,
    READINESS_RX = 1
} readiness_direction_t;

typedef struct readiness_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
} readiness_statistics_t;

static uint64_t g_poll_first_samples[DEVICE_READINESS_MAX_SAMPLES];
static uint64_t g_optimistic_samples[DEVICE_READINESS_MAX_SAMPLES];
static uint8_t g_payload[DEVICE_READINESS_MAX_PAYLOAD];
static uint8_t g_drain[DEVICE_READINESS_MAX_PAYLOAD];
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

static readiness_statistics_t calculate_statistics(uint64_t* samples,
                                                    size_t count) {
    readiness_statistics_t statistics;
    double sum = 0.0;
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

static bool wait_ready(int fd, short events) {
    struct pollfd descriptor;
    int result;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = fd;
    descriptor.events = events;
    do {
        result = poll(&descriptor, 1u, DEVICE_READINESS_TIMEOUT_MS);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & events) != 0;
}

static bool send_optimistic(int fd, const uint8_t* data, size_t size) {
    for (;;) {
        const ssize_t sent = send(fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent == (ssize_t)size) {
            return true;
        }
        if (sent < 0 && errno == EINTR) {
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_ready(fd, POLLOUT)) {
                return false;
            }
            continue;
        }
        return false;
    }
}

static ssize_t receive_optimistic(int fd, uint8_t* data, size_t capacity) {
    for (;;) {
        const ssize_t received = recv(fd, data, capacity, MSG_DONTWAIT);
        if (received >= 0) {
            return received;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!wait_ready(fd, POLLIN)) {
                return -1;
            }
            continue;
        }
        return -1;
    }
}

static bool drain_record(int fd, size_t expected_size) {
    const ssize_t received = recv(fd, g_drain, sizeof(g_drain), 0);
    return received == (ssize_t)expected_size;
}

static bool preload_record(int fd, size_t size) {
    const ssize_t sent = send(fd, g_payload, size, MSG_NOSIGNAL);
    return sent == (ssize_t)size;
}

static bool sample_poll_first_tx(int sender, int receiver, size_t payload_size,
                                 uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    ssize_t sent;
    start = spw_profile_counter_read();
    if (!wait_ready(sender, POLLOUT)) {
        return false;
    }
    sent = send(sender, g_payload, payload_size, MSG_NOSIGNAL | MSG_DONTWAIT);
    end = spw_profile_counter_read();
    if (sent != (ssize_t)payload_size || !drain_record(receiver, payload_size)) {
        return false;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool sample_optimistic_tx(int sender, int receiver, size_t payload_size,
                                 uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    start = spw_profile_counter_read();
    if (!send_optimistic(sender, g_payload, payload_size)) {
        return false;
    }
    end = spw_profile_counter_read();
    if (!drain_record(receiver, payload_size)) {
        return false;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool sample_poll_first_rx(int receiver, int sender, size_t payload_size,
                                 uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    ssize_t received;
    if (!preload_record(sender, payload_size)) {
        return false;
    }
    start = spw_profile_counter_read();
    if (!wait_ready(receiver, POLLIN)) {
        return false;
    }
    received = recv(receiver, g_drain, sizeof(g_drain), MSG_DONTWAIT);
    end = spw_profile_counter_read();
    if (received != (ssize_t)payload_size) {
        return false;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool sample_optimistic_rx(int receiver, int sender, size_t payload_size,
                                 uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    ssize_t received;
    if (!preload_record(sender, payload_size)) {
        return false;
    }
    start = spw_profile_counter_read();
    received = receive_optimistic(receiver, g_drain, sizeof(g_drain));
    end = spw_profile_counter_read();
    if (received != (ssize_t)payload_size) {
        return false;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return true;
}

static bool run_pair(int first_fd, int second_fd,
                     readiness_direction_t direction,
                     size_t payload_size,
                     size_t iteration,
                     uint64_t* poll_first_delta,
                     uint64_t* optimistic_delta) {
    bool poll_first_ok;
    bool optimistic_ok;
    if ((iteration & 1u) == 0u) {
        poll_first_ok = direction == READINESS_TX
                            ? sample_poll_first_tx(first_fd, second_fd,
                                                   payload_size, poll_first_delta)
                            : sample_poll_first_rx(first_fd, second_fd,
                                                   payload_size, poll_first_delta);
        optimistic_ok = direction == READINESS_TX
                            ? sample_optimistic_tx(first_fd, second_fd,
                                                   payload_size, optimistic_delta)
                            : sample_optimistic_rx(first_fd, second_fd,
                                                   payload_size, optimistic_delta);
    } else {
        optimistic_ok = direction == READINESS_TX
                            ? sample_optimistic_tx(first_fd, second_fd,
                                                   payload_size, optimistic_delta)
                            : sample_optimistic_rx(first_fd, second_fd,
                                                   payload_size, optimistic_delta);
        poll_first_ok = direction == READINESS_TX
                            ? sample_poll_first_tx(first_fd, second_fd,
                                                   payload_size, poll_first_delta)
                            : sample_poll_first_rx(first_fd, second_fd,
                                                   payload_size, poll_first_delta);
    }
    return poll_first_ok && optimistic_ok;
}

static void print_statistics(const readiness_statistics_t* statistics) {
    printf("{\"min\":%llu,\"median\":%.3f,\"mean\":%.3f,"
           "\"p95\":%llu,\"p99\":%llu,\"max\":%llu}",
           (unsigned long long)statistics->minimum,
           statistics->median,
           statistics->mean,
           (unsigned long long)statistics->p95,
           (unsigned long long)statistics->p99,
           (unsigned long long)statistics->maximum);
}

static void print_result(readiness_direction_t direction,
                         size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const readiness_statistics_t* poll_first,
                         const readiness_statistics_t* optimistic) {
    const double median_delta = optimistic->median - poll_first->median;
    const double mean_delta = optimistic->mean - poll_first->mean;
    const long long p95_delta =
        (long long)optimistic->p95 - (long long)poll_first->p95;
    const char* direction_name = direction == READINESS_TX ? "tx" : "rx";
    printf("{\"schema\":\"spwkit.profile.readiness.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"direction\":\"%s\"", direction_name);
    printf(",\"backend\":\"device\"");
    printf(",\"carrier\":\"af-unix-seqpacket\"");
    printf(",\"boundary\":\"ready-record-operation\"");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\"}", compiler_name());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"poll_first_statistics\":");
    print_statistics(poll_first);
    printf(",\"optimistic_statistics\":");
    print_statistics(optimistic);
    printf(",\"delta\":{\"definition\":\"optimistic-minus-poll-first\"");
    printf(",\"median_ticks\":%.3f", median_delta);
    printf(",\"mean_ticks\":%.3f", mean_delta);
    printf(",\"p95_ticks\":%lld", p95_delta);
    if (poll_first->median != 0.0) {
        printf(",\"median_percent\":%.3f",
               100.0 * median_delta / poll_first->median);
    } else {
        printf(",\"median_percent\":null");
    }
    printf("}}\n");
}

static void usage(const char* program) {
    fprintf(stderr,
            "Usage: %s --direction tx|rx [--payload N] [--warmup N] [--iterations N]\n",
            program);
}

int main(int argc, char** argv) {
    readiness_direction_t direction = READINESS_TX;
    int have_direction = 0;
    size_t payload_size = 64u;
    size_t warmup_iterations = 64u;
    size_t iterations = 256u;
    int sockets[2] = {-1, -1};
    readiness_statistics_t poll_first_statistics;
    readiness_statistics_t optimistic_statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--direction") == 0 && i + 1u < (size_t)argc) {
            ++i;
            if (strcmp(argv[i], "tx") == 0) {
                direction = READINESS_TX;
            } else if (strcmp(argv[i], "rx") == 0) {
                direction = READINESS_RX;
            } else {
                usage(argv[0]);
                return 2;
            }
            have_direction = 1;
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, DEVICE_READINESS_MAX_PAYLOAD,
                            &payload_size)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, DEVICE_READINESS_MAX_SAMPLES,
                            &warmup_iterations)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, DEVICE_READINESS_MAX_SAMPLES,
                            &iterations)) {
                usage(argv[0]);
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!have_direction || socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sockets) != 0) {
        return 2;
    }
    for (i = 0u; i < payload_size; ++i) {
        g_payload[i] = (uint8_t)(0x31u + (uint8_t)(i * 29u));
    }

    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t poll_first_delta;
        uint64_t optimistic_delta;
        if (!run_pair(sockets[0], sockets[1], direction, payload_size, i,
                      &poll_first_delta, &optimistic_delta)) {
            close(sockets[0]);
            close(sockets[1]);
            return 1;
        }
        g_sink ^= poll_first_delta ^ optimistic_delta;
    }

    for (i = 0u; i < iterations; ++i) {
        if (!run_pair(sockets[0], sockets[1], direction, payload_size, i,
                      &g_poll_first_samples[i], &g_optimistic_samples[i])) {
            close(sockets[0]);
            close(sockets[1]);
            return 1;
        }
    }

    poll_first_statistics = calculate_statistics(g_poll_first_samples, iterations);
    optimistic_statistics = calculate_statistics(g_optimistic_samples, iterations);
    print_result(direction, payload_size, warmup_iterations, iterations,
                 &poll_first_statistics, &optimistic_statistics);

    close(sockets[0]);
    close(sockets[1]);
    return g_sink == UINT64_MAX ? 1 : 0;
}
