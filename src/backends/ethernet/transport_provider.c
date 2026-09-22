// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/transport_provider.h"

#include <string.h>

bool spw_transport_peer_id_set(spw_transport_peer_id_t* peer,
                               const void* bytes,
                               size_t size) {
    if (peer == NULL || size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        (size != 0u && bytes == NULL)) {
        return false;
    }
    memset(peer, 0, sizeof(*peer));
    if (size != 0u) {
        memcpy(peer->bytes, bytes, size);
    }
    peer->size = (uint8_t)size;
    return true;
}

bool spw_transport_peer_id_equal(const spw_transport_peer_id_t* lhs,
                                 const spw_transport_peer_id_t* rhs) {
    if (lhs == NULL || rhs == NULL ||
        lhs->size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        rhs->size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        lhs->size != rhs->size) {
        return false;
    }
    return lhs->size == 0u || memcmp(lhs->bytes, rhs->bytes, lhs->size) == 0;
}

bool spw_transport_provider_valid(const spw_transport_provider_t* provider) {
    const spw_transport_provider_ops_t* ops;
    if (provider == NULL || provider->context == NULL || provider->ops == NULL) {
        return false;
    }
    ops = provider->ops;
    return ops->start != NULL && ops->stop != NULL && ops->reset != NULL &&
           ops->send != NULL && ops->receive != NULL && ops->wait != NULL &&
           ops->get_mtu != NULL && ops->get_state != NULL;
}

static spw_result_t validate_provider(const spw_transport_provider_t* provider) {
    return spw_transport_provider_valid(provider) ? SPW_OK
                                                  : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_transport_provider_start(spw_transport_provider_t* provider) {
    spw_result_t result = validate_provider(provider);
    return result == SPW_OK ? provider->ops->start(provider->context) : result;
}

spw_result_t spw_transport_provider_stop(spw_transport_provider_t* provider) {
    spw_result_t result = validate_provider(provider);
    return result == SPW_OK ? provider->ops->stop(provider->context) : result;
}

spw_result_t spw_transport_provider_reset(spw_transport_provider_t* provider) {
    spw_result_t result = validate_provider(provider);
    return result == SPW_OK ? provider->ops->reset(provider->context) : result;
}

spw_result_t spw_transport_provider_send(
    spw_transport_provider_t* provider,
    const spw_transport_peer_id_t* peer,
    const uint8_t* message,
    size_t message_size,
    spw_timeout_us_t timeout_us) {
    spw_result_t result = validate_provider(provider);
    if (result != SPW_OK || peer == NULL ||
        peer->size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        (message_size != 0u && message == NULL)) {
        return result != SPW_OK ? result : SPW_ERR_INVALID_ARGUMENT;
    }
    return provider->ops->send(provider->context, peer, message, message_size,
                               timeout_us);
}

spw_result_t spw_transport_provider_receive(
    spw_transport_provider_t* provider,
    uint8_t* message,
    size_t message_capacity,
    size_t* out_message_size,
    spw_transport_peer_id_t* out_peer,
    spw_timeout_us_t timeout_us) {
    spw_result_t result = validate_provider(provider);
    if (result != SPW_OK || out_message_size == NULL || out_peer == NULL ||
        (message_capacity != 0u && message == NULL)) {
        return result != SPW_OK ? result : SPW_ERR_INVALID_ARGUMENT;
    }
    return provider->ops->receive(provider->context, message, message_capacity,
                                  out_message_size, out_peer, timeout_us);
}

spw_result_t spw_transport_provider_wait(
    spw_transport_provider_t* provider,
    spw_transport_ready_t interests,
    spw_timeout_us_t timeout_us,
    spw_transport_ready_t* out_ready) {
    spw_result_t result = validate_provider(provider);
    if (result != SPW_OK || out_ready == NULL ||
        interests == SPW_TRANSPORT_READY_NONE ||
        (interests & (uint8_t)~SPW_TRANSPORT_READY_ALL) != 0u) {
        return result != SPW_OK ? result : SPW_ERR_INVALID_ARGUMENT;
    }
    return provider->ops->wait(provider->context, interests, timeout_us,
                               out_ready);
}

spw_result_t spw_transport_provider_get_mtu(
    const spw_transport_provider_t* provider,
    size_t* out_mtu) {
    spw_result_t result = validate_provider(provider);
    if (result != SPW_OK || out_mtu == NULL) {
        return result != SPW_OK ? result : SPW_ERR_INVALID_ARGUMENT;
    }
    return provider->ops->get_mtu(provider->context, out_mtu);
}

spw_result_t spw_transport_provider_get_state(
    const spw_transport_provider_t* provider,
    spw_transport_state_t* out_state) {
    spw_result_t result = validate_provider(provider);
    if (result != SPW_OK || out_state == NULL) {
        return result != SPW_OK ? result : SPW_ERR_INVALID_ARGUMENT;
    }
    return provider->ops->get_state(provider->context, out_state);
}
