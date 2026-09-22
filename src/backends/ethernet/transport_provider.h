// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_TRANSPORT_PROVIDER_H
#define SPWKIT_TRANSPORT_PROVIDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spwkit/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPW_TRANSPORT_PEER_ID_MAX_SIZE = 32u
};

typedef struct spw_transport_peer_id {
    uint8_t bytes[SPW_TRANSPORT_PEER_ID_MAX_SIZE];
    uint8_t size;
} spw_transport_peer_id_t;

#define SPW_TRANSPORT_PEER_ID_INITIALIZER {{0}, 0u}

typedef uint8_t spw_transport_state_t;
enum {
    SPW_TRANSPORT_STATE_DOWN = 0u,
    SPW_TRANSPORT_STATE_UP = 1u
};

typedef uint8_t spw_transport_ready_t;
enum {
    SPW_TRANSPORT_READY_NONE = 0u,
    SPW_TRANSPORT_READY_RX = (1u << 0),
    SPW_TRANSPORT_READY_TX = (1u << 1),
    SPW_TRANSPORT_READY_ALL = SPW_TRANSPORT_READY_RX | SPW_TRANSPORT_READY_TX
};

typedef struct spw_transport_provider_ops {
    spw_result_t (*start)(void* context);
    spw_result_t (*stop)(void* context);
    spw_result_t (*reset)(void* context);

    spw_result_t (*send)(void* context,
                         const spw_transport_peer_id_t* peer,
                         const uint8_t* message,
                         size_t message_size,
                         spw_timeout_us_t timeout_us);
    spw_result_t (*receive)(void* context,
                            uint8_t* message,
                            size_t message_capacity,
                            size_t* out_message_size,
                            spw_transport_peer_id_t* out_peer,
                            spw_timeout_us_t timeout_us);
    spw_result_t (*wait)(void* context,
                         spw_transport_ready_t interests,
                         spw_timeout_us_t timeout_us,
                         spw_transport_ready_t* out_ready);

    spw_result_t (*get_mtu)(const void* context, size_t* out_mtu);
    spw_result_t (*get_state)(const void* context,
                              spw_transport_state_t* out_state);
} spw_transport_provider_ops_t;

typedef struct spw_transport_provider {
    const spw_transport_provider_ops_t* ops;
    void* context;
} spw_transport_provider_t;

#define SPW_TRANSPORT_PROVIDER_INITIALIZER {NULL, NULL}

bool spw_transport_peer_id_set(spw_transport_peer_id_t* peer,
                               const void* bytes,
                               size_t size);
bool spw_transport_peer_id_equal(const spw_transport_peer_id_t* lhs,
                                 const spw_transport_peer_id_t* rhs);
bool spw_transport_provider_valid(const spw_transport_provider_t* provider);

spw_result_t spw_transport_provider_start(spw_transport_provider_t* provider);
spw_result_t spw_transport_provider_stop(spw_transport_provider_t* provider);
spw_result_t spw_transport_provider_reset(spw_transport_provider_t* provider);
spw_result_t spw_transport_provider_send(
    spw_transport_provider_t* provider,
    const spw_transport_peer_id_t* peer,
    const uint8_t* message,
    size_t message_size,
    spw_timeout_us_t timeout_us);
spw_result_t spw_transport_provider_receive(
    spw_transport_provider_t* provider,
    uint8_t* message,
    size_t message_capacity,
    size_t* out_message_size,
    spw_transport_peer_id_t* out_peer,
    spw_timeout_us_t timeout_us);
spw_result_t spw_transport_provider_wait(
    spw_transport_provider_t* provider,
    spw_transport_ready_t interests,
    spw_timeout_us_t timeout_us,
    spw_transport_ready_t* out_ready);
spw_result_t spw_transport_provider_get_mtu(
    const spw_transport_provider_t* provider,
    size_t* out_mtu);
spw_result_t spw_transport_provider_get_state(
    const spw_transport_provider_t* provider,
    spw_transport_state_t* out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_TRANSPORT_PROVIDER_H */
