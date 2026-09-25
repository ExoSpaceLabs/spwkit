// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/raw_ethernet_transport_provider.h"
#include "backends/ethernet/vspw_tp.h"

#include <string.h>

static uint16_t read_be16(const uint8_t* source) {
    return (uint16_t)(((uint16_t)source[0] << 8u) |
                      (uint16_t)source[1]);
}

static void write_be16(uint8_t* destination, uint16_t value) {
    destination[0] = (uint8_t)(value >> 8u);
    destination[1] = (uint8_t)value;
}

static spw_timeout_us_t remaining_timeout(
    const spw_raw_ethernet_transport_t* transport,
    uint64_t start_us,
    spw_timeout_us_t timeout_us) {
    uint64_t now;
    uint64_t elapsed;

    if (timeout_us == SPW_TIMEOUT_INFINITE ||
        timeout_us == SPW_TIMEOUT_IMMEDIATE) {
        return timeout_us;
    }
    now = spw_vspw_runtime_now_us(&transport->runtime);
    if (now < start_us) {
        return SPW_TIMEOUT_IMMEDIATE;
    }
    elapsed = now - start_us;
    return elapsed >= timeout_us ? SPW_TIMEOUT_IMMEDIATE
                                 : timeout_us - elapsed;
}

static bool frame_matches(const spw_raw_ethernet_transport_t* transport,
                          const uint8_t* frame,
                          size_t frame_size) {
    size_t declared_size;
    if (frame_size < SPW_RAW_ETHERNET_CARRIER_OVERHEAD) {
        return false;
    }
    declared_size =
        read_be16(frame + SPW_RAW_ETHERNET_HEADER_SIZE + 4u);
    return memcmp(frame, transport->config.local_mac,
                  SPW_RAW_ETHERNET_MAC_SIZE) == 0 &&
           read_be16(frame + 12u) == transport->config.ether_type &&
           read_be16(frame + SPW_RAW_ETHERNET_HEADER_SIZE) ==
               transport->config.protocol_subtype &&
           frame[SPW_RAW_ETHERNET_HEADER_SIZE + 2u] ==
               SPW_RAW_ETHERNET_PROTOCOL_VERSION_MAJOR &&
           frame[SPW_RAW_ETHERNET_HEADER_SIZE + 3u] ==
               SPW_RAW_ETHERNET_PROTOCOL_VERSION_MINOR &&
           declared_size >= SPW_VSPW_TP_HEADER_SIZE &&
           declared_size <=
               frame_size - SPW_RAW_ETHERNET_CARRIER_OVERHEAD;
}

static spw_result_t raw_start(void* context) {
    spw_raw_ethernet_transport_t* transport =
        (spw_raw_ethernet_transport_t*)context;
    spw_result_t result;
    if (transport == NULL || transport->config.io_ops == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    result = transport->config.io_ops->start(transport->config.io_context);
    if (result == SPW_OK) {
        transport->started = true;
    }
    return result;
}

static spw_result_t raw_stop(void* context) {
    spw_raw_ethernet_transport_t* transport =
        (spw_raw_ethernet_transport_t*)context;
    spw_result_t result;
    if (transport == NULL || transport->config.io_ops == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    result = transport->config.io_ops->stop(transport->config.io_context);
    if (result == SPW_OK) {
        transport->started = false;
    }
    return result;
}

static spw_result_t raw_reset(void* context) {
    spw_raw_ethernet_transport_t* transport =
        (spw_raw_ethernet_transport_t*)context;
    spw_result_t result;
    if (transport == NULL || transport->config.io_ops == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    result = transport->config.io_ops->reset(transport->config.io_context);
    if (result == SPW_OK) {
        transport->started = false;
    }
    return result;
}

static spw_result_t raw_send(void* context,
                             const spw_transport_peer_id_t* peer,
                             const uint8_t* message,
                             size_t message_size,
                             spw_timeout_us_t timeout_us) {
    spw_raw_ethernet_transport_t* transport =
        (spw_raw_ethernet_transport_t*)context;
    size_t frame_size;

    if (transport == NULL || peer == NULL ||
        peer->size != SPW_RAW_ETHERNET_MAC_SIZE ||
        (message_size != 0u && message == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!transport->started) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!spw_transport_peer_id_equal(peer, &transport->remote_peer)) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (message_size > UINT16_MAX ||
        message_size >
            transport->max_frame_size - SPW_RAW_ETHERNET_CARRIER_OVERHEAD) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(transport->tx_frame, peer->bytes, SPW_RAW_ETHERNET_MAC_SIZE);
    memcpy(transport->tx_frame + 6u, transport->config.local_mac,
           SPW_RAW_ETHERNET_MAC_SIZE);
    write_be16(transport->tx_frame + 12u, transport->config.ether_type);
    write_be16(transport->tx_frame + SPW_RAW_ETHERNET_HEADER_SIZE,
               transport->config.protocol_subtype);
    transport->tx_frame[SPW_RAW_ETHERNET_HEADER_SIZE + 2u] =
        SPW_RAW_ETHERNET_PROTOCOL_VERSION_MAJOR;
    transport->tx_frame[SPW_RAW_ETHERNET_HEADER_SIZE + 3u] =
        SPW_RAW_ETHERNET_PROTOCOL_VERSION_MINOR;
    write_be16(transport->tx_frame + SPW_RAW_ETHERNET_HEADER_SIZE + 4u,
               (uint16_t)message_size);
    if (message_size != 0u) {
        memcpy(transport->tx_frame + SPW_RAW_ETHERNET_CARRIER_OVERHEAD,
               message, message_size);
    }
    frame_size = SPW_RAW_ETHERNET_CARRIER_OVERHEAD + message_size;
    return transport->config.io_ops->send_frame(
        transport->config.io_context, transport->tx_frame,
        frame_size, timeout_us);
}

static spw_result_t raw_receive(void* context,
                                uint8_t* message,
                                size_t message_capacity,
                                size_t* out_message_size,
                                spw_transport_peer_id_t* out_peer,
                                spw_timeout_us_t timeout_us) {
    spw_raw_ethernet_transport_t* transport =
        (spw_raw_ethernet_transport_t*)context;
    const uint64_t start_us =
        transport != NULL
            ? spw_vspw_runtime_now_us(&transport->runtime)
            : 0u;

    if (transport == NULL || out_message_size == NULL || out_peer == NULL ||
        (message_capacity != 0u && message == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_message_size = 0u;
    *out_peer = (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
    if (!transport->started) {
        return SPW_ERR_INVALID_STATE;
    }

    for (;;) {
        size_t frame_size = 0u;
        size_t payload_size;
        const spw_timeout_us_t remaining =
            remaining_timeout(transport, start_us, timeout_us);
        spw_result_t result = transport->config.io_ops->receive_frame(
            transport->config.io_context, transport->rx_frame,
            transport->max_frame_size, &frame_size, remaining);
        if (result != SPW_OK) {
            return result;
        }
        if (frame_size > transport->max_frame_size) {
            return SPW_ERR_BACKEND;
        }
        if (!frame_matches(transport, transport->rx_frame, frame_size)) {
            if (timeout_us != SPW_TIMEOUT_INFINITE &&
                remaining_timeout(transport, start_us, timeout_us) ==
                    SPW_TIMEOUT_IMMEDIATE) {
                return SPW_ERR_TIMEOUT;
            }
            continue;
        }

        payload_size =
            read_be16(transport->rx_frame +
                      SPW_RAW_ETHERNET_HEADER_SIZE + 4u);
        *out_message_size = payload_size;
        if (!spw_transport_peer_id_set(
                out_peer, transport->rx_frame + 6u,
                SPW_RAW_ETHERNET_MAC_SIZE)) {
            return SPW_ERR_BACKEND;
        }
        if (payload_size > message_capacity) {
            return SPW_ERR_BUFFER_TOO_SMALL;
        }
        if (payload_size != 0u) {
            memcpy(message,
                   transport->rx_frame + SPW_RAW_ETHERNET_CARRIER_OVERHEAD,
                   payload_size);
        }
        return SPW_OK;
    }
}

static spw_result_t raw_wait(void* context,
                             spw_transport_ready_t interests,
                             spw_timeout_us_t timeout_us,
                             spw_transport_ready_t* out_ready) {
    spw_raw_ethernet_transport_t* transport =
        (spw_raw_ethernet_transport_t*)context;
    spw_raw_ethernet_ready_t io_interests = SPW_RAW_ETHERNET_READY_NONE;
    spw_raw_ethernet_ready_t io_ready = SPW_RAW_ETHERNET_READY_NONE;
    spw_result_t result;

    if (transport == NULL || out_ready == NULL ||
        interests == SPW_TRANSPORT_READY_NONE ||
        (interests & (uint8_t)~SPW_TRANSPORT_READY_ALL) != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_ready = SPW_TRANSPORT_READY_NONE;
    if (!transport->started) {
        return SPW_ERR_INVALID_STATE;
    }

    if ((interests & SPW_TRANSPORT_READY_RX) != 0u) {
        io_interests |= SPW_RAW_ETHERNET_READY_RX;
    }
    if ((interests & SPW_TRANSPORT_READY_TX) != 0u) {
        io_interests |= SPW_RAW_ETHERNET_READY_TX;
    }

    result = transport->config.io_ops->wait(
        transport->config.io_context, io_interests, timeout_us, &io_ready);
    if (result != SPW_OK) {
        return result;
    }
    if ((io_ready & SPW_RAW_ETHERNET_READY_RX) != 0u) {
        *out_ready |= SPW_TRANSPORT_READY_RX;
    }
    if ((io_ready & SPW_RAW_ETHERNET_READY_TX) != 0u) {
        *out_ready |= SPW_TRANSPORT_READY_TX;
    }
    return *out_ready == SPW_TRANSPORT_READY_NONE ? SPW_ERR_BACKEND : SPW_OK;
}

static spw_result_t raw_get_mtu(const void* context, size_t* out_mtu) {
    const spw_raw_ethernet_transport_t* transport =
        (const spw_raw_ethernet_transport_t*)context;
    if (transport == NULL || out_mtu == NULL ||
        transport->max_frame_size < SPW_RAW_ETHERNET_CARRIER_OVERHEAD) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_mtu =
        transport->max_frame_size - SPW_RAW_ETHERNET_CARRIER_OVERHEAD;
    return SPW_OK;
}

static spw_result_t raw_get_state(const void* context,
                                  spw_transport_state_t* out_state) {
    const spw_raw_ethernet_transport_t* transport =
        (const spw_raw_ethernet_transport_t*)context;
    bool link_up = false;
    spw_result_t result;

    if (transport == NULL || out_state == NULL ||
        transport->config.io_ops == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!transport->started) {
        *out_state = SPW_TRANSPORT_STATE_DOWN;
        return SPW_OK;
    }
    result = transport->config.io_ops->get_link_up(
        transport->config.io_context, &link_up);
    if (result != SPW_OK) {
        return result;
    }
    *out_state = link_up ? SPW_TRANSPORT_STATE_UP
                         : SPW_TRANSPORT_STATE_DOWN;
    return SPW_OK;
}

static const spw_transport_provider_ops_t RAW_OPS = {
    raw_start,
    raw_stop,
    raw_reset,
    raw_send,
    raw_receive,
    raw_wait,
    raw_get_mtu,
    raw_get_state
};

spw_result_t spw_raw_ethernet_transport_init(
    spw_raw_ethernet_transport_t* transport,
    const spw_raw_ethernet_config_t* config,
    const spw_vspw_runtime_t* runtime) {
    size_t frame_size = 0u;
    if (transport == NULL || config == NULL || runtime == NULL ||
        config->io_ops == NULL || !spw_vspw_runtime_valid(runtime)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (config->io_ops->get_max_frame_size(
            config->io_context, &frame_size) != SPW_OK ||
        frame_size < SPW_RAW_ETHERNET_CARRIER_OVERHEAD +
                         SPW_VSPW_TP_HEADER_SIZE) {
        return SPW_ERR_UNSUPPORTED;
    }

    memset(transport, 0, sizeof(*transport));
    {
        const size_t config_copy_size =
            config->struct_size < sizeof(transport->config)
                ? config->struct_size
                : sizeof(transport->config);
        memcpy(&transport->config, config, config_copy_size);
    }
    transport->runtime = *runtime;
    transport->max_frame_size =
        frame_size < SPW_RAW_ETHERNET_MAX_FRAME_SIZE
            ? frame_size
            : SPW_RAW_ETHERNET_MAX_FRAME_SIZE;
    if (!spw_transport_peer_id_set(
            &transport->remote_peer, transport->config.remote_mac,
            SPW_RAW_ETHERNET_MAC_SIZE)) {
        return SPW_ERR_BACKEND;
    }
    return SPW_OK;
}

void spw_raw_ethernet_transport_provider(
    spw_raw_ethernet_transport_t* transport,
    spw_transport_provider_t* out_provider) {
    if (out_provider == NULL) {
        return;
    }
    out_provider->ops = transport == NULL ? NULL : &RAW_OPS;
    out_provider->context = transport;
}

spw_result_t spw_raw_ethernet_transport_remote_peer(
    const spw_raw_ethernet_transport_t* transport,
    spw_transport_peer_id_t* out_peer) {
    if (transport == NULL || out_peer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_peer = transport->remote_peer;
    return SPW_OK;
}
