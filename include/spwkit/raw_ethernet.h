// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_RAW_ETHERNET_H
#define SPWKIT_RAW_ETHERNET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "spwkit/runtime.h"
#include "spwkit/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_RAW_ETHERNET_IO_OPS_VERSION 1u
#define SPW_RAW_ETHERNET_CONFIG_VERSION 1u
#define SPW_RAW_ETHERNET_MAC_SIZE 6u
#define SPW_RAW_ETHERNET_MAX_FRAME_SIZE 2048u

/*
 * IEEE Local Experimental EtherType 1.
 *
 * This value is for protocol development on a privately administered network.
 * It is not a permanent product EtherType. Wider/commercial deployment must
 * use an appropriate IEEE-assigned identifier or another standards-compliant
 * organization-specific scheme.
 */
#define SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE ((uint16_t)0x88B5u)

/* SpWKit development subtype carried immediately after EtherType. */
#define SPW_RAW_ETHERNET_PROTOCOL_SUBTYPE ((uint16_t)0x5357u)
#define SPW_RAW_ETHERNET_PROTOCOL_VERSION_MAJOR ((uint8_t)2u)
#define SPW_RAW_ETHERNET_PROTOCOL_VERSION_MINOR ((uint8_t)0u)

#define SPW_RAW_ETHERNET_DEFAULT_FRAGMENT_PAYLOAD 1400u
#define SPW_RAW_ETHERNET_DEFAULT_ACK_TIMEOUT_MS 100u
#define SPW_RAW_ETHERNET_DEFAULT_MAX_RETRIES 5u
#define SPW_RAW_ETHERNET_DEFAULT_KEEPALIVE_INTERVAL_MS 1000u
#define SPW_RAW_ETHERNET_DEFAULT_PEER_TIMEOUT_MS 3000u
#define SPW_RAW_ETHERNET_DEFAULT_VIRTUAL_LINK_BPS 0ull
#define SPW_RAW_ETHERNET_DEFAULT_VIRTUAL_LATENCY_US 0u

typedef uint8_t spw_raw_ethernet_ready_t;
#define SPW_RAW_ETHERNET_READY_NONE ((spw_raw_ethernet_ready_t)0u)
#define SPW_RAW_ETHERNET_READY_RX   ((spw_raw_ethernet_ready_t)(1u << 0))
#define SPW_RAW_ETHERNET_READY_TX   ((spw_raw_ethernet_ready_t)(1u << 1))
#define SPW_RAW_ETHERNET_READY_ALL \
    ((spw_raw_ethernet_ready_t)(SPW_RAW_ETHERNET_READY_RX | \
                                SPW_RAW_ETHERNET_READY_TX))

/**
 * Raw Ethernet frame-I/O binding.
 *
 * Frames include destination/source MAC addresses and EtherType, but exclude
 * preamble/SFD and FCS. SpWKit's development framing also carries an explicit
 * VSPW-frame length so Ethernet minimum-frame padding is never interpreted as
 * protocol data. The provider owns no MAC/DMA/PHY details.
 *
 * The platform implementation and io_context remain caller-owned for the
 * lifetime of the SpWKit port. This contract is suitable for bindings to
 * AF_PACKET/libpcap-like host facilities or to an embedded Ethernet
 * MAC/DMA driver.
 *
 * wait() is level-triggered and non-consuming. IRQ-driven embedded bindings
 * may implement it by blocking on an RTOS event/semaphore that is signalled by
 * DMA/IRQ completion.
 */
typedef struct spw_raw_ethernet_io_ops {
    uint32_t struct_size;
    uint32_t version;

    spw_result_t (*start)(void* io_context);
    spw_result_t (*stop)(void* io_context);
    spw_result_t (*reset)(void* io_context);

    spw_result_t (*send_frame)(void* io_context,
                               const uint8_t* frame,
                               size_t frame_size,
                               spw_timeout_us_t timeout_us);
    spw_result_t (*receive_frame)(void* io_context,
                                  uint8_t* frame,
                                  size_t frame_capacity,
                                  size_t* out_frame_size,
                                  spw_timeout_us_t timeout_us);
    spw_result_t (*wait)(void* io_context,
                         spw_raw_ethernet_ready_t interests,
                         spw_timeout_us_t timeout_us,
                         spw_raw_ethernet_ready_t* out_ready);

    spw_result_t (*get_max_frame_size)(const void* io_context,
                                       size_t* out_frame_size);
    spw_result_t (*get_link_up)(const void* io_context,
                                bool* out_link_up);
} spw_raw_ethernet_io_ops_t;

#define SPW_RAW_ETHERNET_IO_OPS_MIN_SIZE \
    (offsetof(spw_raw_ethernet_io_ops_t, get_link_up) + \
     sizeof(((spw_raw_ethernet_io_ops_t*)0)->get_link_up))

typedef struct spw_raw_ethernet_config {
    uint32_t struct_size;
    uint32_t version;

    const spw_raw_ethernet_io_ops_t* io_ops;
    void* io_context;

    const spw_runtime_ops_t* runtime_ops;
    void* runtime_context;

    uint8_t local_mac[SPW_RAW_ETHERNET_MAC_SIZE];
    uint8_t remote_mac[SPW_RAW_ETHERNET_MAC_SIZE];

    uint16_t ether_type;
    uint16_t protocol_subtype;

    uint32_t link_id;
    uint16_t fragment_payload_size;
    uint16_t max_retries;
    uint32_t ack_timeout_ms;
    uint32_t keepalive_interval_ms;
    uint32_t peer_timeout_ms;
    uint64_t virtual_link_bps;
    uint32_t virtual_latency_us;
    uint32_t reserved;
} spw_raw_ethernet_config_t;

#define SPW_RAW_ETHERNET_CONFIG_MIN_SIZE \
    (offsetof(spw_raw_ethernet_config_t, reserved) + \
     sizeof(((spw_raw_ethernet_config_t*)0)->reserved))

#define SPW_RAW_ETHERNET_IO_OPS_INITIALIZER \
    { sizeof(spw_raw_ethernet_io_ops_t), SPW_RAW_ETHERNET_IO_OPS_VERSION }

#define SPW_RAW_ETHERNET_CONFIG_INITIALIZER(io_ops_, io_context_, \
                                            runtime_ops_, runtime_context_, \
                                            link_id_) \
    { sizeof(spw_raw_ethernet_config_t), SPW_RAW_ETHERNET_CONFIG_VERSION, \
      (io_ops_), (io_context_), (runtime_ops_), (runtime_context_), \
      {0u, 0u, 0u, 0u, 0u, 0u}, {0u, 0u, 0u, 0u, 0u, 0u}, \
      0u, SPW_RAW_ETHERNET_PROTOCOL_SUBTYPE, (uint32_t)(link_id_), \
      (uint16_t)SPW_RAW_ETHERNET_DEFAULT_FRAGMENT_PAYLOAD, \
      (uint16_t)SPW_RAW_ETHERNET_DEFAULT_MAX_RETRIES, \
      (uint32_t)SPW_RAW_ETHERNET_DEFAULT_ACK_TIMEOUT_MS, \
      (uint32_t)SPW_RAW_ETHERNET_DEFAULT_KEEPALIVE_INTERVAL_MS, \
      (uint32_t)SPW_RAW_ETHERNET_DEFAULT_PEER_TIMEOUT_MS, \
      (uint64_t)SPW_RAW_ETHERNET_DEFAULT_VIRTUAL_LINK_BPS, \
      (uint32_t)SPW_RAW_ETHERNET_DEFAULT_VIRTUAL_LATENCY_US, 0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_RAW_ETHERNET_H */
