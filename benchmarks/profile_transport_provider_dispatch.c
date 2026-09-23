// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/transport_provider.h"
#include "profiling/profile.h"

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SAMPLES 8192u

#if defined(_MSC_VER)
#define SPW_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define SPW_NOINLINE __attribute__((noinline))
#else
#define SPW_NOINLINE
#endif

typedef struct dispatch_context {
    volatile uint64_t sink;
} dispatch_context_t;

static uint64_t direct_samples[MAX_SAMPLES];
static uint64_t wrapper_samples[MAX_SAMPLES];
static int64_t paired_samples[MAX_SAMPLES];

static SPW_NOINLINE spw_result_t noop_start(void* context) {
    ((dispatch_context_t*)context)->sink += 1u;
    return SPW_OK;
}

static SPW_NOINLINE spw_result_t noop_stop(void* context) {
    ((dispatch_context_t*)context)->sink += 1u;
    return SPW_OK;
}

static SPW_NOINLINE spw_result_t noop_reset(void* context) {
    ((dispatch_context_t*)context)->sink += 1u;
    return SPW_OK;
}

static SPW_NOINLINE spw_result_t noop_send(
    void* context,
    const spw_transport_peer_id_t* peer,
    const uint8_t* message,
    size_t message_size,
    spw_timeout_us_t timeout_us) {
    dispatch_context_t* state = (dispatch_context_t*)context;
    (void)timeout_us;
    state->sink += (uint64_t)message_size + peer->size;
    if (message_size != 0u) {
        state->sink += message[0];
    }
    return SPW_OK;
}

static SPW_NOINLINE spw_result_t noop_receive(
    void* context,
    uint8_t* message,
    size_t message_capacity,
    size_t* out_message_size,
    spw_transport_peer_id_t* out_peer,
    spw_timeout_us_t timeout_us) {
    (void)context;
    (void)message;
    (void)message_capacity;
    (void)timeout_us;
    *out_message_size = 0u;
    *out_peer = (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
    return SPW_ERR_TIMEOUT;
}

static SPW_NOINLINE spw_result_t noop_wait(
    void* context,
    spw_transport_ready_t interests,
    spw_timeout_us_t timeout_us,
    spw_transport_ready_t* out_ready) {
    (void)context;
    (void)interests;
    (void)timeout_us;
    *out_ready = SPW_TRANSPORT_READY_TX;
    return SPW_OK;
}

static SPW_NOINLINE spw_result_t noop_get_mtu(
    const void* context,
    size_t* out_mtu) {
    (void)context;
    *out_mtu = 1500u;
    return SPW_OK;
}

static SPW_NOINLINE spw_result_t noop_get_state(
    const void* context,
    spw_transport_state_t* out_state) {
    (void)context;
    *out_state = SPW_TRANSPORT_STATE_UP;
    return SPW_OK;
}

static const spw_transport_provider_ops_t OPS = {
    noop_start,
    noop_stop,
    noop_reset,
    noop_send,
    noop_receive,
    noop_wait,
    noop_get_mtu,
    noop_get_state
};

static int compare_u64(const void* lhs, const void* rhs) {
    const uint64_t a = *(const uint64_t*)lhs;
    const uint64_t b = *(const uint64_t*)rhs;
    return (a > b) - (a < b);
}

static int compare_i64(const void* lhs, const void* rhs) {
    const int64_t a = *(const int64_t*)lhs;
    const int64_t b = *(const int64_t*)rhs;
    return (a > b) - (a < b);
}

static double median_u64(uint64_t* values, size_t count) {
    qsort(values, count, sizeof(values[0]), compare_u64);
    if ((count & 1u) != 0u) {
        return (double)values[count / 2u];
    }
    return ((double)values[count / 2u - 1u] +
            (double)values[count / 2u]) / 2.0;
}

static double median_i64(int64_t* values, size_t count) {
    qsort(values, count, sizeof(values[0]), compare_i64);
    if ((count & 1u) != 0u) {
        return (double)values[count / 2u];
    }
    return ((double)values[count / 2u - 1u] +
            (double)values[count / 2u]) / 2.0;
}

static int parse_size(const char* text,
                      size_t minimum,
                      size_t maximum,
                      size_t* out_value) {
    char* end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < minimum || value > maximum) {
        return 0;
    }
    *out_value = (size_t)value;
    return 1;
}

static uint64_t measure_direct(dispatch_context_t* context,
                               const spw_transport_peer_id_t* peer,
                               const uint8_t* payload,
                               size_t payload_size) {
    uint64_t start;
    uint64_t end;
    spw_profile_counter_prepare();
    start = spw_profile_counter_read();
    (void)noop_send(context, peer, payload, payload_size,
                    SPW_TIMEOUT_IMMEDIATE);
    end = spw_profile_counter_read();
    return spw_profile_counter_delta(start, end);
}

static uint64_t measure_wrapper(spw_transport_provider_t* provider,
                                const spw_transport_peer_id_t* peer,
                                const uint8_t* payload,
                                size_t payload_size) {
    uint64_t start;
    uint64_t end;
    spw_profile_counter_prepare();
    start = spw_profile_counter_read();
    (void)spw_transport_provider_send(provider, peer, payload, payload_size,
                                      SPW_TIMEOUT_IMMEDIATE);
    end = spw_profile_counter_read();
    return spw_profile_counter_delta(start, end);
}

int main(int argc, char** argv) {
    size_t warmup = 1024u;
    size_t iterations = 4096u;
    dispatch_context_t context = {0u};
    spw_transport_provider_t provider = {&OPS, &context};
    spw_transport_peer_id_t peer = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    const uint8_t peer_byte = 0x42u;
    uint8_t payload[64];
    size_t i;
    double direct_median;
    double wrapper_median;
    double paired_median;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup)) {
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, MAX_SAMPLES, &iterations)) {
                return 2;
            }
        } else {
            return 2;
        }
    }

    memset(payload, 0x5au, sizeof(payload));
    if (!spw_transport_peer_id_set(&peer, &peer_byte, sizeof(peer_byte)) ||
        !spw_transport_provider_valid(&provider)) {
        return 1;
    }

    for (i = 0u; i < warmup; ++i) {
        (void)noop_send(&context, &peer, payload, sizeof(payload),
                        SPW_TIMEOUT_IMMEDIATE);
        (void)spw_transport_provider_send(
            &provider, &peer, payload, sizeof(payload), SPW_TIMEOUT_IMMEDIATE);
    }

    for (i = 0u; i < iterations; ++i) {
        if ((i & 1u) == 0u) {
            direct_samples[i] =
                measure_direct(&context, &peer, payload, sizeof(payload));
            wrapper_samples[i] =
                measure_wrapper(&provider, &peer, payload, sizeof(payload));
        } else {
            wrapper_samples[i] =
                measure_wrapper(&provider, &peer, payload, sizeof(payload));
            direct_samples[i] =
                measure_direct(&context, &peer, payload, sizeof(payload));
        }
        paired_samples[i] =
            (int64_t)wrapper_samples[i] - (int64_t)direct_samples[i];
    }

    direct_median = median_u64(direct_samples, iterations);
    wrapper_median = median_u64(wrapper_samples, iterations);
    paired_median = median_i64(paired_samples, iterations);

    printf("schema=spwkit.transport-provider-dispatch.v1\n");
    printf("counter=%s\n", spw_profile_counter_kind());
    printf("counter_width_bits=%u\n", spw_profile_counter_width_bits());
    printf("iterations=%zu\n", iterations);
    printf("payload_bytes=%zu\n", sizeof(payload));
    printf("direct_send_median_ticks=%.1f\n", direct_median);
    printf("provider_send_median_ticks=%.1f\n", wrapper_median);
    printf("paired_provider_minus_direct_median_ticks=%.1f\n", paired_median);
    printf("sink=%" PRIu64 "\n", context.sink);
    return 0;
}
