// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE

#include "vspwd/server.h"

#include "backends/device/vspw_device_protocol.h"
#include "spwkit/config.h"
#include "spwkit/port.h"
#include "spwkit/types.h"
#include "spwkit/udp.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

enum {
    VSPWD_PORT_COUNT = 2,
    VSPWD_CLIENT_COUNT = 4,
    VSPWD_PACKET_QUEUE_DEPTH = 2,
    VSPWD_TIME_CODE_QUEUE_DEPTH = 8,
    VSPWD_RESPONSE_MAX = VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE,
    VSPWD_FRAME_MAX = VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD
};

_Static_assert(VSPWD_PORT_COUNT <= 32,
               "monitor subscription mask supports at most 32 ports");

typedef struct vspwd_packet_slot {
    uint8_t data[VSPD_MAX_LOGICAL_PACKET];
    uint32_t length;
    uint32_t offset;
    uint32_t message_id;
    spw_terminator_t terminator;
} vspwd_packet_slot_t;

typedef struct vspwd_packet_queue {
    vspwd_packet_slot_t slots[VSPWD_PACKET_QUEUE_DEPTH];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} vspwd_packet_queue_t;

typedef struct vspwd_time_code_queue {
    spw_time_code_t slots[VSPWD_TIME_CODE_QUEUE_DEPTH];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} vspwd_time_code_queue_t;

typedef struct vspwd_port {
    int client_index;
    bool started;
    bool reset_latched;
    bool ever_attached;
    bool bridged;
    spw_link_state_t state;
    bool state_event_pending;
    uint32_t next_message_id;
    vspwd_packet_queue_t packets;
    vspwd_time_code_queue_t time_codes;
    spw_statistics_t statistics;
} vspwd_port_t;

typedef struct vspwd_reassembly {
    bool active;
    uint32_t request_id;
    uint32_t message_id;
    uint32_t total_size;
    uint32_t next_offset;
    spw_terminator_t terminator;
    uint8_t data[VSPD_MAX_LOGICAL_PACKET];
} vspwd_reassembly_t;

typedef struct vspwd_client {
    int fd;
    bool hello_done;
    int port_id;
    uint32_t monitor_mask;
    uint32_t monitor_pending_mask;
    bool response_pending;
    size_t response_size;
    uint8_t response[VSPWD_RESPONSE_MAX];
    vspwd_reassembly_t reassembly;
} vspwd_client_t;

typedef struct vspwd_udp_bridge {
    bool enabled;
    int port_id;
    spw_port_t* udp_port;
    spw_link_state_t state;
    uint8_t rx_packet[VSPD_MAX_LOGICAL_PACKET];
} vspwd_udp_bridge_t;

typedef struct vspwd_server {
    int listener_fd;
    char socket_path[sizeof(((struct sockaddr_un*)0)->sun_path)];
    vspwd_client_t clients[VSPWD_CLIENT_COUNT];
    vspwd_port_t ports[VSPWD_PORT_COUNT];
    vspwd_udp_bridge_t bridge;
} vspwd_server_t;

static volatile sig_atomic_t vspwd_stop_requested = 0;

static void vspwd_mark_port_changed(vspwd_server_t* server, int port_id);

static void vspwd_signal_handler(int signal_number) {
    (void)signal_number;
    vspwd_stop_requested = 1;
}

static uint32_t vspwd_next_message_id(vspwd_port_t* port) {
    uint32_t result = port->next_message_id++;
    if (result == 0u) {
        result = port->next_message_id++;
    }
    if (port->next_message_id == 0u) {
        port->next_message_id = 1u;
    }
    return result;
}

static int vspwd_peer_port(int port_id) {
    return port_id == 0 ? 1 : 0;
}

static void vspwd_clear_packet_queue(vspwd_packet_queue_t* queue) {
    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
}

static void vspwd_clear_time_code_queue(vspwd_time_code_queue_t* queue) {
    queue->head = 0u;
    queue->tail = 0u;
    queue->count = 0u;
}

static void vspwd_clear_reassembly(vspwd_reassembly_t* reassembly) {
    reassembly->active = false;
    reassembly->request_id = 0u;
    reassembly->message_id = 0u;
    reassembly->total_size = 0u;
    reassembly->next_offset = 0u;
    reassembly->terminator = SPW_TERMINATOR_EOP;
}

static void vspwd_clear_port_queues(vspwd_port_t* port) {
    vspwd_clear_packet_queue(&port->packets);
    vspwd_clear_time_code_queue(&port->time_codes);
}

static spw_link_state_t vspwd_calculate_state(const vspwd_server_t* server,
                                               int port_id) {
    const vspwd_port_t* port = &server->ports[port_id];
    const vspwd_port_t* peer = &server->ports[vspwd_peer_port(port_id)];

    if (port->bridged) {
        return server->bridge.state;
    }
    if (port->client_index < 0) {
        return SPW_LINK_ERROR_RESET;
    }
    if (port->reset_latched) {
        return SPW_LINK_ERROR_RESET;
    }
    if (!port->started) {
        return SPW_LINK_READY;
    }
    if (peer->bridged) {
        if (peer->state == SPW_LINK_RUN) {
            return SPW_LINK_RUN;
        }
        if (peer->state == SPW_LINK_ERROR_WAIT) {
            return SPW_LINK_ERROR_WAIT;
        }
        return SPW_LINK_CONNECTING;
    }
    if (peer->client_index >= 0 && peer->started && !peer->reset_latched) {
        return SPW_LINK_RUN;
    }
    if (peer->client_index >= 0) {
        return SPW_LINK_CONNECTING;
    }
    return peer->ever_attached ? SPW_LINK_ERROR_WAIT : SPW_LINK_CONNECTING;
}

static void vspwd_update_states(vspwd_server_t* server) {
    int port_id;
    for (port_id = 0; port_id < VSPWD_PORT_COUNT; ++port_id) {
        vspwd_port_t* port = &server->ports[port_id];
        spw_link_state_t next_state;
        if (port->client_index < 0 && !port->bridged) {
            port->state = SPW_LINK_ERROR_RESET;
            port->state_event_pending = false;
            continue;
        }
        next_state = vspwd_calculate_state(server, port_id);
        if (next_state != port->state) {
            if (next_state == SPW_LINK_ERROR_WAIT) {
                ++port->statistics.link_errors;
            }
            port->state = next_state;
            port->state_event_pending = true;
            vspwd_mark_port_changed(server, port_id);
        }
    }
}

static void vspwd_init_server(vspwd_server_t* server) {
    int i;
    memset(server, 0, sizeof(*server));
    server->listener_fd = -1;
    server->bridge.port_id = -1;
    server->bridge.udp_port = NULL;
    server->bridge.state = SPW_LINK_ERROR_RESET;
    for (i = 0; i < VSPWD_CLIENT_COUNT; ++i) {
        server->clients[i].fd = -1;
        server->clients[i].port_id = -1;
    }
    for (i = 0; i < VSPWD_PORT_COUNT; ++i) {
        server->ports[i].client_index = -1;
        server->ports[i].state = SPW_LINK_ERROR_RESET;
        server->ports[i].next_message_id = 1u;
    }
}

static bool vspwd_send_record(int fd, const uint8_t* data, size_t size) {
    ssize_t sent = send(fd, data, size, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (sent == (ssize_t)size) {
        return true;
    }
    if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return false;
    }
    return false;
}

static bool vspwd_queue_response(vspwd_client_t* client,
                                 const vspd_header_t* request,
                                 int32_t status,
                                 const uint8_t* payload,
                                 uint32_t payload_size) {
    vspd_header_t response;

    if (client->response_pending || payload_size > VSPD_STATISTICS_PAYLOAD_SIZE) {
        return false;
    }
    memset(&response, 0, sizeof(response));
    response.magic = VSPD_MAGIC;
    response.version_major = VSPD_VERSION_MAJOR;
    response.version_minor = VSPD_VERSION_MINOR;
    response.type = request->type;
    response.flags = VSPD_FLAG_RESPONSE;
    response.header_size = VSPD_HEADER_SIZE;
    response.payload_size = status == VSPD_STATUS_OK ? payload_size : 0u;
    response.request_id = request->request_id;
    response.port_id = request->port_id;
    response.status = status;

    if (vspd_encode_header(&response, client->response) != VSPD_CODEC_OK) {
        return false;
    }
    if (response.payload_size != 0u) {
        if (payload == NULL) {
            return false;
        }
        memcpy(client->response + VSPD_HEADER_SIZE, payload, response.payload_size);
    }
    client->response_size = VSPD_HEADER_SIZE + response.payload_size;
    client->response_pending = true;
    return true;
}

static bool vspwd_flush_response(vspwd_client_t* client) {
    if (!client->response_pending) {
        return true;
    }
    if (!vspwd_send_record(client->fd, client->response, client->response_size)) {
        return false;
    }
    client->response_pending = false;
    client->response_size = 0u;
    return true;
}

static int32_t vspwd_validate_attached_request(const vspwd_client_t* client,
                                               const vspd_header_t* header) {
    if (!client->hello_done || client->port_id < 0) {
        return VSPD_STATUS_INVALID_STATE;
    }
    if (header->port_id != (uint32_t)client->port_id) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    return VSPD_STATUS_OK;
}

static int32_t vspwd_validate_management_request(
    const vspwd_client_t* client,
    const vspd_header_t* header,
    bool requires_port) {
    if (!client->hello_done || client->port_id >= 0) {
        return VSPD_STATUS_INVALID_STATE;
    }
    if (requires_port && header->port_id >= VSPWD_PORT_COUNT) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    return VSPD_STATUS_OK;
}

static void vspwd_statistics_to_wire(const spw_statistics_t* source,
                                     vspd_statistics_payload_t* destination) {
    memset(destination, 0, sizeof(*destination));
    destination->tx_packets = source->tx_packets;
    destination->rx_packets = source->rx_packets;
    destination->tx_bytes = source->tx_bytes;
    destination->rx_bytes = source->rx_bytes;
    destination->tx_time_codes = source->tx_time_codes;
    destination->rx_time_codes = source->rx_time_codes;
    destination->eep_packets = source->eep_packets;
    destination->link_errors = source->link_errors;
    destination->dropped_packets = source->dropped_packets;
}

static void vspwd_port_info_to_wire(const vspwd_port_t* port,
                                    vspd_port_info_payload_t* info) {
    memset(info, 0, sizeof(*info));
    if (port->client_index >= 0) {
        info->flags |= VSPD_PORT_INFO_ATTACHED;
    }
    if (port->started) {
        info->flags |= VSPD_PORT_INFO_STARTED;
    }
    if (port->reset_latched) {
        info->flags |= VSPD_PORT_INFO_RESET_LATCHED;
    }
    if (port->ever_attached) {
        info->flags |= VSPD_PORT_INFO_EVER_ATTACHED;
    }
    if (port->bridged) {
        info->flags |= VSPD_PORT_INFO_BRIDGED;
    }
    info->link_state = port->state;
    info->packet_queue_count = port->packets.count;
    info->time_code_queue_count = port->time_codes.count;
}

static void vspwd_port_snapshot_to_wire(
    const vspwd_port_t* port,
    vspd_port_snapshot_payload_t* snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    vspwd_port_info_to_wire(port, &snapshot->info);
    vspwd_statistics_to_wire(&port->statistics, &snapshot->statistics);
}

static void vspwd_mark_port_changed(vspwd_server_t* server, int port_id) {
    uint32_t bit;
    int i;
    if (port_id < 0 || port_id >= VSPWD_PORT_COUNT) {
        return;
    }
    bit = UINT32_C(1) << (uint32_t)port_id;
    for (i = 0; i < VSPWD_CLIENT_COUNT; ++i) {
        vspwd_client_t* client = &server->clients[i];
        if (client->fd >= 0 && client->hello_done && client->port_id < 0 &&
            (client->monitor_mask & bit) != 0u) {
            client->monitor_pending_mask |= bit;
        }
    }
}

static void vspwd_detach_client(vspwd_server_t* server, int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    if (client->port_id >= 0 && client->port_id < VSPWD_PORT_COUNT) {
        vspwd_port_t* port = &server->ports[client->port_id];
        if (port->client_index == client_index) {
            port->client_index = -1;
            port->started = false;
            port->reset_latched = false;
            port->state = SPW_LINK_ERROR_RESET;
            port->state_event_pending = false;
            vspwd_clear_port_queues(port);
            vspwd_mark_port_changed(server, client->port_id);
        }
    }
    client->port_id = -1;
    vspwd_clear_reassembly(&client->reassembly);
    vspwd_update_states(server);
}

static void vspwd_close_client(vspwd_server_t* server, int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    vspwd_detach_client(server, client_index);
    if (client->fd >= 0) {
        close(client->fd);
    }
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->port_id = -1;
}

static bool vspwd_enqueue_packet(vspwd_server_t* server,
                                 int source_port_id,
                                 const vspwd_reassembly_t* reassembly) {
    vspwd_port_t* source = &server->ports[source_port_id];
    vspwd_port_t* destination = &server->ports[vspwd_peer_port(source_port_id)];
    vspwd_packet_slot_t* slot;

    if (source->state != SPW_LINK_RUN || destination->state != SPW_LINK_RUN) {
        return false;
    }
    if (destination->packets.count >= VSPWD_PACKET_QUEUE_DEPTH) {
        return false;
    }

    slot = &destination->packets.slots[destination->packets.tail];
    if (reassembly->total_size != 0u) {
        memcpy(slot->data, reassembly->data, reassembly->total_size);
    }
    slot->length = reassembly->total_size;
    slot->offset = 0u;
    slot->terminator = reassembly->terminator;
    slot->message_id = vspwd_next_message_id(destination);
    destination->packets.tail =
        (destination->packets.tail + 1u) % VSPWD_PACKET_QUEUE_DEPTH;
    ++destination->packets.count;

    ++source->statistics.tx_packets;
    source->statistics.tx_bytes += reassembly->total_size;
    if (reassembly->terminator == SPW_TERMINATOR_EEP) {
        ++source->statistics.eep_packets;
    }
    vspwd_mark_port_changed(server, source_port_id);
    vspwd_mark_port_changed(server, vspwd_peer_port(source_port_id));
    return true;
}

static int32_t vspwd_accept_data_fragment(vspwd_server_t* server,
                                          vspwd_client_t* client,
                                          const vspd_header_t* header,
                                          const uint8_t* payload) {
    vspwd_reassembly_t* reassembly = &client->reassembly;
    bool start = (header->flags & VSPD_FLAG_FRAGMENT_START) != 0u;
    bool end = (header->flags & VSPD_FLAG_FRAGMENT_END) != 0u;

    if (!reassembly->active) {
        if (!start || header->fragment_offset != 0u) {
            return VSPD_STATUS_INVALID_PACKET;
        }
        reassembly->active = true;
        reassembly->request_id = header->request_id;
        reassembly->message_id = header->message_id;
        reassembly->total_size = header->total_size;
        reassembly->next_offset = 0u;
    } else if (start ||
               reassembly->request_id != header->request_id ||
               reassembly->message_id != header->message_id ||
               reassembly->total_size != header->total_size ||
               reassembly->next_offset != header->fragment_offset) {
        return VSPD_STATUS_INVALID_PACKET;
    }

    if (header->payload_size != 0u) {
        memcpy(reassembly->data + header->fragment_offset,
               payload,
               header->payload_size);
    }
    reassembly->next_offset = header->fragment_offset + header->payload_size;

    if (!end) {
        return VSPD_STATUS_OK;
    }
    if (reassembly->next_offset != reassembly->total_size) {
        return VSPD_STATUS_INVALID_PACKET;
    }
    reassembly->terminator =
        (header->flags & VSPD_FLAG_EEP) != 0u ? SPW_TERMINATOR_EEP
                                             : SPW_TERMINATOR_EOP;

    if (server->ports[client->port_id].state != SPW_LINK_RUN) {
        return VSPD_STATUS_LINK_UNAVAILABLE;
    }
    if (server->ports[vspwd_peer_port(client->port_id)].packets.count >=
        VSPWD_PACKET_QUEUE_DEPTH) {
        ++server->ports[client->port_id].statistics.dropped_packets;
        vspwd_mark_port_changed(server, client->port_id);
        return VSPD_STATUS_RESOURCE_EXHAUSTED;
    }
    if (!vspwd_enqueue_packet(server, client->port_id, reassembly)) {
        return VSPD_STATUS_LINK_UNAVAILABLE;
    }
    return VSPD_STATUS_OK;
}

static bool vspwd_enqueue_time_code(vspwd_server_t* server,
                                    int source_port_id,
                                    const spw_time_code_t* time_code) {
    vspwd_port_t* source = &server->ports[source_port_id];
    vspwd_port_t* destination = &server->ports[vspwd_peer_port(source_port_id)];
    if (source->state != SPW_LINK_RUN || destination->state != SPW_LINK_RUN) {
        return false;
    }
    if (destination->time_codes.count >= VSPWD_TIME_CODE_QUEUE_DEPTH) {
        return false;
    }
    destination->time_codes.slots[destination->time_codes.tail] = *time_code;
    destination->time_codes.tail =
        (destination->time_codes.tail + 1u) % VSPWD_TIME_CODE_QUEUE_DEPTH;
    ++destination->time_codes.count;
    ++source->statistics.tx_time_codes;
    vspwd_mark_port_changed(server, source_port_id);
    vspwd_mark_port_changed(server, vspwd_peer_port(source_port_id));
    return true;
}



static void vspwd_bridge_refresh_state(vspwd_server_t* server) {
    spw_link_state_t state = SPW_LINK_ERROR_WAIT;
    spw_result_t result;
    if (!server->bridge.enabled || server->bridge.udp_port == NULL) {
        return;
    }
    result = spw_port_get_link_state(server->bridge.udp_port, &state);
    if (result != SPW_OK) {
        state = SPW_LINK_ERROR_WAIT;
    }
    if (state != server->bridge.state) {
        vspwd_port_t* bridge_port = &server->ports[server->bridge.port_id];
        server->bridge.state = state;
        if (bridge_port->state != state) {
            if (state == SPW_LINK_ERROR_WAIT) {
                ++bridge_port->statistics.link_errors;
            }
            bridge_port->state = state;
            vspwd_mark_port_changed(server, server->bridge.port_id);
        }
        /* Publish the bridge endpoint first so the paired local port is
         * recalculated from the new remote state in this same pass. */
        vspwd_update_states(server);
    }
}

static void vspwd_bridge_pop_packet(vspwd_server_t* server) {
    vspwd_port_t* bridge_port = &server->ports[server->bridge.port_id];
    vspwd_packet_slot_t* slot;
    spw_packet_t packet;
    spw_result_t result;
    if (bridge_port->packets.count == 0u || server->bridge.state != SPW_LINK_RUN) {
        return;
    }
    slot = &bridge_port->packets.slots[bridge_port->packets.head];
    packet.data = slot->data;
    packet.length = slot->length;
    packet.capacity = slot->length;
    packet.terminator = slot->terminator;
    result = spw_port_send(server->bridge.udp_port, &packet, SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }
    ++bridge_port->statistics.rx_packets;
    bridge_port->statistics.rx_bytes += slot->length;
    bridge_port->packets.head =
        (bridge_port->packets.head + 1u) % VSPWD_PACKET_QUEUE_DEPTH;
    --bridge_port->packets.count;
    vspwd_mark_port_changed(server, server->bridge.port_id);
}

static void vspwd_bridge_pop_time_code(vspwd_server_t* server) {
    vspwd_port_t* bridge_port = &server->ports[server->bridge.port_id];
    const spw_time_code_t* time_code;
    spw_result_t result;
    if (bridge_port->time_codes.count == 0u || server->bridge.state != SPW_LINK_RUN) {
        return;
    }
    time_code = &bridge_port->time_codes.slots[bridge_port->time_codes.head];
    result = spw_port_send_time_code(server->bridge.udp_port,
                                     time_code,
                                     SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }
    ++bridge_port->statistics.rx_time_codes;
    bridge_port->time_codes.head =
        (bridge_port->time_codes.head + 1u) % VSPWD_TIME_CODE_QUEUE_DEPTH;
    --bridge_port->time_codes.count;
    vspwd_mark_port_changed(server, server->bridge.port_id);
}

static void vspwd_bridge_push_packet(vspwd_server_t* server) {
    const int bridge_id = server->bridge.port_id;
    const int local_id = vspwd_peer_port(bridge_id);
    vspwd_port_t* bridge_port = &server->ports[bridge_id];
    vspwd_port_t* local_port = &server->ports[local_id];
    vspwd_packet_slot_t* slot;
    spw_packet_t packet;
    spw_result_t result;

    if (server->bridge.state != SPW_LINK_RUN || local_port->state != SPW_LINK_RUN ||
        local_port->packets.count >= VSPWD_PACKET_QUEUE_DEPTH) {
        return;
    }
    packet.data = server->bridge.rx_packet;
    packet.length = 0u;
    packet.capacity = sizeof(server->bridge.rx_packet);
    packet.terminator = SPW_TERMINATOR_EOP;
    result = spw_port_receive(server->bridge.udp_port, &packet, SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }

    slot = &local_port->packets.slots[local_port->packets.tail];
    if (packet.length != 0u) {
        memcpy(slot->data, packet.data, packet.length);
    }
    slot->length = (uint32_t)packet.length;
    slot->offset = 0u;
    slot->terminator = packet.terminator;
    slot->message_id = vspwd_next_message_id(local_port);
    local_port->packets.tail =
        (local_port->packets.tail + 1u) % VSPWD_PACKET_QUEUE_DEPTH;
    ++local_port->packets.count;
    ++bridge_port->statistics.tx_packets;
    bridge_port->statistics.tx_bytes += packet.length;
    if (packet.terminator == SPW_TERMINATOR_EEP) {
        ++bridge_port->statistics.eep_packets;
    }
    vspwd_mark_port_changed(server, bridge_id);
    vspwd_mark_port_changed(server, local_id);
}

static void vspwd_bridge_push_time_code(vspwd_server_t* server) {
    const int bridge_id = server->bridge.port_id;
    const int local_id = vspwd_peer_port(bridge_id);
    vspwd_port_t* bridge_port = &server->ports[bridge_id];
    vspwd_port_t* local_port = &server->ports[local_id];
    spw_time_code_t time_code;
    spw_result_t result;

    if (server->bridge.state != SPW_LINK_RUN || local_port->state != SPW_LINK_RUN ||
        local_port->time_codes.count >= VSPWD_TIME_CODE_QUEUE_DEPTH) {
        return;
    }
    result = spw_port_receive_time_code(server->bridge.udp_port,
                                        &time_code,
                                        SPW_TIMEOUT_IMMEDIATE);
    if (result != SPW_OK) {
        return;
    }
    local_port->time_codes.slots[local_port->time_codes.tail] = time_code;
    local_port->time_codes.tail =
        (local_port->time_codes.tail + 1u) % VSPWD_TIME_CODE_QUEUE_DEPTH;
    ++local_port->time_codes.count;
    ++bridge_port->statistics.tx_time_codes;
    vspwd_mark_port_changed(server, bridge_id);
    vspwd_mark_port_changed(server, local_id);
}

static void vspwd_service_bridge(vspwd_server_t* server) {
    if (!server->bridge.enabled) {
        return;
    }
    vspwd_bridge_refresh_state(server);
    if (server->bridge.state != SPW_LINK_RUN) {
        return;
    }
    vspwd_bridge_pop_packet(server);
    vspwd_bridge_pop_time_code(server);
    vspwd_bridge_push_packet(server);
    vspwd_bridge_push_time_code(server);
    vspwd_bridge_refresh_state(server);
}

static int vspwd_open_bridge(vspwd_server_t* server,
                             const vspwd_udp_bridge_config_t* config) {
    spw_port_config_t port_config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    spw_result_t result;
    vspwd_port_t* port;
    if (!config->enabled) {
        return 0;
    }
    if (config->port_id >= VSPWD_PORT_COUNT) {
        return -1;
    }
    port_config.backend_config = &config->udp;
    port_config.backend_config_size = sizeof(config->udp);
    result = spw_port_open(&port_config, &server->bridge.udp_port);
    if (result != SPW_OK || server->bridge.udp_port == NULL) {
        fprintf(stderr, "vspwd: failed to open UDP bridge backend: %d\n", (int)result);
        return -1;
    }
    result = spw_port_start(server->bridge.udp_port);
    if (result != SPW_OK) {
        fprintf(stderr, "vspwd: failed to start UDP bridge backend: %d\n", (int)result);
        (void)spw_port_close(server->bridge.udp_port);
        server->bridge.udp_port = NULL;
        return -1;
    }
    server->bridge.enabled = true;
    server->bridge.port_id = (int)config->port_id;
    server->bridge.state = SPW_LINK_CONNECTING;
    port = &server->ports[server->bridge.port_id];
    port->bridged = true;
    port->started = true;
    port->reset_latched = false;
    port->state = SPW_LINK_CONNECTING;
    port->next_message_id = 1u;
    vspwd_bridge_refresh_state(server);
    return 0;
}

static bool vspwd_handle_request(vspwd_server_t* server,
                                 int client_index,
                                 const uint8_t* frame,
                                 size_t frame_size) {
    vspwd_client_t* client = &server->clients[client_index];
    vspd_header_t header;
    const uint8_t* payload;
    uint8_t response_payload[VSPD_STATISTICS_PAYLOAD_SIZE];
    int32_t status = VSPD_STATUS_OK;

    if (client->response_pending) {
        return false;
    }
    if (vspd_validate_frame(frame, frame_size, &header) != VSPD_CODEC_OK) {
        return false;
    }
    if ((header.flags & VSPD_FLAG_RESPONSE) != 0u ||
        header.type == VSPD_MSG_DATA_RX ||
        header.type == VSPD_MSG_TIME_CODE_RX ||
        header.type == VSPD_MSG_LINK_STATE_EVENT ||
        header.type == VSPD_MSG_PORT_SNAPSHOT_EVENT) {
        return false;
    }
    payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;

    if (!client->hello_done && header.type != VSPD_MSG_HELLO) {
        return false;
    }

    switch (header.type) {
        case VSPD_MSG_HELLO: {
            static const uint8_t hello[VSPD_HELLO_PAYLOAD_SIZE] = {
                VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
            if (client->port_id >= 0) {
                status = VSPD_STATUS_INVALID_STATE;
            } else {
                client->hello_done = true;
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        hello,
                                        VSPD_HELLO_PAYLOAD_SIZE);
        }

        case VSPD_MSG_ATTACH: {
            int port_id = (int)header.port_id;
            if (!client->hello_done || client->port_id >= 0) {
                status = VSPD_STATUS_INVALID_STATE;
            } else if (port_id < 0 || port_id >= VSPWD_PORT_COUNT) {
                status = VSPD_STATUS_INVALID_ARGUMENT;
            } else if (server->ports[port_id].bridged) {
                status = VSPD_STATUS_RESOURCE_EXHAUSTED;
            } else if (server->ports[port_id].client_index >= 0) {
                status = VSPD_STATUS_RESOURCE_EXHAUSTED;
            } else {
                vspwd_port_t* port = &server->ports[port_id];
                client->port_id = port_id;
                port->client_index = client_index;
                port->started = false;
                port->reset_latched = false;
                port->ever_attached = true;
                port->state = SPW_LINK_ERROR_RESET;
                port->state_event_pending = false;
                port->next_message_id = 1u;
                vspwd_clear_port_queues(port);
                memset(&port->statistics, 0, sizeof(port->statistics));
                vspwd_update_states(server);
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);
        }

        case VSPD_MSG_DETACH:
            status = vspwd_validate_attached_request(client, &header);
            if (!vspwd_queue_response(client, &header, status, NULL, 0u)) {
                return false;
            }
            if (status == VSPD_STATUS_OK) {
                vspwd_detach_client(server, client_index);
            }
            return true;

        case VSPD_MSG_START:
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK) {
                vspwd_port_t* port = &server->ports[client->port_id];
                port->started = true;
                port->reset_latched = false;
                vspwd_update_states(server);
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        case VSPD_MSG_STOP:
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK) {
                vspwd_port_t* port = &server->ports[client->port_id];
                port->started = false;
                port->reset_latched = false;
                vspwd_update_states(server);
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        case VSPD_MSG_RESET:
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK) {
                vspwd_port_t* port = &server->ports[client->port_id];
                port->started = false;
                port->reset_latched = true;
                vspwd_clear_port_queues(port);
                vspwd_clear_reassembly(&client->reassembly);
                vspwd_update_states(server);
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        case VSPD_MSG_GET_LINK_STATE:
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK) {
                vspd_encode_u32_payload(server->ports[client->port_id].state,
                                        response_payload);
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        response_payload,
                                        VSPD_LINK_STATE_PAYLOAD_SIZE);

        case VSPD_MSG_GET_CAPABILITIES: {
            vspd_capabilities_payload_t capabilities;
            status = vspwd_validate_attached_request(client, &header);
            memset(&capabilities, 0, sizeof(capabilities));
            if (status == VSPD_STATUS_OK) {
                capabilities.bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                                    SPW_CAP_LINK_CONTROL | SPW_CAP_STATISTICS;
                capabilities.max_packet_size = VSPD_MAX_LOGICAL_PACKET;
                capabilities.tx_queue_depth = VSPWD_PACKET_QUEUE_DEPTH;
                capabilities.rx_queue_depth = VSPWD_PACKET_QUEUE_DEPTH;
                capabilities.buffer_alignment = 1u;
                vspd_encode_capabilities(&capabilities, response_payload);
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        response_payload,
                                        VSPD_CAPABILITIES_PAYLOAD_SIZE);
        }

        case VSPD_MSG_DATA_TX: {
            bool end = (header.flags & VSPD_FLAG_FRAGMENT_END) != 0u;
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK) {
                status = vspwd_accept_data_fragment(server, client, &header, payload);
            }
            if (status == VSPD_STATUS_INVALID_PACKET) {
                vspwd_clear_reassembly(&client->reassembly);
                return false;
            }
            if (!end) {
                return status == VSPD_STATUS_OK;
            }
            vspwd_clear_reassembly(&client->reassembly);
            return vspwd_queue_response(client, &header, status, NULL, 0u);
        }

        case VSPD_MSG_TIME_CODE_TX: {
            spw_time_code_t time_code;
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK &&
                server->ports[client->port_id].state != SPW_LINK_RUN) {
                status = VSPD_STATUS_LINK_UNAVAILABLE;
            }
            if (status == VSPD_STATUS_OK) {
                time_code.time_count = payload[0];
                time_code.control_flags = payload[1];
                if (server->ports[vspwd_peer_port(client->port_id)].time_codes.count >=
                    VSPWD_TIME_CODE_QUEUE_DEPTH) {
                    ++server->ports[client->port_id].statistics.dropped_packets;
                    vspwd_mark_port_changed(server, client->port_id);
                    status = VSPD_STATUS_RESOURCE_EXHAUSTED;
                } else if (!vspwd_enqueue_time_code(server,
                                                    client->port_id,
                                                    &time_code)) {
                    status = VSPD_STATUS_LINK_UNAVAILABLE;
                }
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);
        }

        case VSPD_MSG_GET_STATISTICS: {
            vspd_statistics_payload_t statistics;
            status = vspwd_validate_attached_request(client, &header);
            memset(&statistics, 0, sizeof(statistics));
            if (status == VSPD_STATUS_OK) {
                vspwd_statistics_to_wire(
                    &server->ports[client->port_id].statistics, &statistics);
                vspd_encode_statistics(&statistics, response_payload);
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        response_payload,
                                        VSPD_STATISTICS_PAYLOAD_SIZE);
        }

        case VSPD_MSG_CLEAR_STATISTICS:
            status = vspwd_validate_attached_request(client, &header);
            if (status == VSPD_STATUS_OK) {
                memset(&server->ports[client->port_id].statistics,
                       0,
                       sizeof(server->ports[client->port_id].statistics));
                vspwd_mark_port_changed(server, client->port_id);
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        case VSPD_MSG_GET_SERVER_INFO: {
            vspd_server_info_payload_t info;
            status = vspwd_validate_management_request(client, &header, false);
            memset(&info, 0, sizeof(info));
            if (status == VSPD_STATUS_OK) {
                info.port_count = VSPWD_PORT_COUNT;
                info.client_capacity = VSPWD_CLIENT_COUNT;
                info.packet_queue_depth = VSPWD_PACKET_QUEUE_DEPTH;
                info.time_code_queue_depth = VSPWD_TIME_CODE_QUEUE_DEPTH;
                info.max_logical_packet = VSPD_MAX_LOGICAL_PACKET;
                vspd_encode_server_info(&info, response_payload);
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        response_payload,
                                        VSPD_SERVER_INFO_PAYLOAD_SIZE);
        }

        case VSPD_MSG_GET_PORT_INFO: {
            vspd_port_info_payload_t info;
            const vspwd_port_t* port = NULL;
            status = vspwd_validate_management_request(client, &header, true);
            memset(&info, 0, sizeof(info));
            if (status == VSPD_STATUS_OK) {
                port = &server->ports[header.port_id];
                vspwd_port_info_to_wire(port, &info);
                vspd_encode_port_info(&info, response_payload);
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        response_payload,
                                        VSPD_PORT_INFO_PAYLOAD_SIZE);
        }

        case VSPD_MSG_GET_PORT_STATISTICS: {
            vspd_statistics_payload_t statistics;
            status = vspwd_validate_management_request(client, &header, true);
            memset(&statistics, 0, sizeof(statistics));
            if (status == VSPD_STATUS_OK) {
                vspwd_statistics_to_wire(
                    &server->ports[header.port_id].statistics, &statistics);
                vspd_encode_statistics(&statistics, response_payload);
            }
            return vspwd_queue_response(client,
                                        &header,
                                        status,
                                        response_payload,
                                        VSPD_STATISTICS_PAYLOAD_SIZE);
        }

        case VSPD_MSG_CLEAR_PORT_STATISTICS:
            status = vspwd_validate_management_request(client, &header, true);
            if (status == VSPD_STATUS_OK) {
                memset(&server->ports[header.port_id].statistics,
                       0,
                       sizeof(server->ports[header.port_id].statistics));
                vspwd_mark_port_changed(server, (int)header.port_id);
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        case VSPD_MSG_SUBSCRIBE_PORT:
            status = vspwd_validate_management_request(client, &header, true);
            if (status == VSPD_STATUS_OK) {
                uint32_t bit = UINT32_C(1) << header.port_id;
                client->monitor_mask |= bit;
                client->monitor_pending_mask |= bit;
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        case VSPD_MSG_UNSUBSCRIBE_PORT:
            status = vspwd_validate_management_request(client, &header, true);
            if (status == VSPD_STATUS_OK) {
                uint32_t bit = UINT32_C(1) << header.port_id;
                client->monitor_mask &= ~bit;
                client->monitor_pending_mask &= ~bit;
            }
            return vspwd_queue_response(client, &header, status, NULL, 0u);

        default:
            return false;
    }
}

static bool vspwd_flush_state_event(vspwd_server_t* server,
                                    int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    vspwd_port_t* port;
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_LINK_STATE_PAYLOAD_SIZE];
    vspd_header_t header;

    if (client->port_id < 0) {
        return true;
    }
    port = &server->ports[client->port_id];
    if (!port->state_event_pending) {
        return true;
    }

    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = VSPD_MSG_LINK_STATE_EVENT;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = VSPD_LINK_STATE_PAYLOAD_SIZE;
    header.port_id = (uint32_t)client->port_id;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return false;
    }
    vspd_encode_u32_payload(port->state, frame + VSPD_HEADER_SIZE);
    if (!vspwd_send_record(client->fd, frame, sizeof(frame))) {
        return false;
    }
    port->state_event_pending = false;
    return true;
}

static bool vspwd_flush_packet_event(vspwd_server_t* server,
                                     int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    vspwd_port_t* port;
    vspwd_packet_slot_t* slot;
    uint8_t frame[VSPWD_FRAME_MAX];
    vspd_header_t header;
    uint32_t remaining;
    uint32_t chunk;
    bool final_fragment;

    if (client->port_id < 0) {
        return true;
    }
    port = &server->ports[client->port_id];
    if (port->packets.count == 0u) {
        return true;
    }
    slot = &port->packets.slots[port->packets.head];

    remaining = slot->length - slot->offset;
    chunk = remaining > VSPD_MAX_FRAME_PAYLOAD ? VSPD_MAX_FRAME_PAYLOAD : remaining;
    final_fragment = slot->length == 0u || slot->offset + chunk == slot->length;

    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = VSPD_MSG_DATA_RX;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = chunk;
    header.port_id = (uint32_t)client->port_id;
    header.message_id = slot->message_id;
    header.fragment_offset = slot->offset;
    header.total_size = slot->length;
    if (slot->offset == 0u) {
        header.flags |= VSPD_FLAG_FRAGMENT_START;
    }
    if (final_fragment) {
        header.flags |= VSPD_FLAG_FRAGMENT_END;
        header.flags |= slot->terminator == SPW_TERMINATOR_EEP ? VSPD_FLAG_EEP
                                                               : VSPD_FLAG_EOP;
    }

    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return false;
    }
    if (chunk != 0u) {
        memcpy(frame + VSPD_HEADER_SIZE, slot->data + slot->offset, chunk);
    }
    if (!vspwd_send_record(client->fd, frame, VSPD_HEADER_SIZE + chunk)) {
        return false;
    }

    if (!final_fragment) {
        slot->offset += chunk;
        return true;
    }

    ++port->statistics.rx_packets;
    port->statistics.rx_bytes += slot->length;
    port->packets.head = (port->packets.head + 1u) % VSPWD_PACKET_QUEUE_DEPTH;
    --port->packets.count;
    vspwd_mark_port_changed(server, client->port_id);
    return true;
}

static bool vspwd_flush_time_code_event(vspwd_server_t* server,
                                        int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    vspwd_port_t* port;
    const spw_time_code_t* time_code;
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_TIME_CODE_PAYLOAD_SIZE];
    vspd_header_t header;

    if (client->port_id < 0) {
        return true;
    }
    port = &server->ports[client->port_id];
    if (port->time_codes.count == 0u) {
        return true;
    }
    time_code = &port->time_codes.slots[port->time_codes.head];

    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = VSPD_MSG_TIME_CODE_RX;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = VSPD_TIME_CODE_PAYLOAD_SIZE;
    header.port_id = (uint32_t)client->port_id;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return false;
    }
    frame[VSPD_HEADER_SIZE] = time_code->time_count;
    frame[VSPD_HEADER_SIZE + 1u] = time_code->control_flags;
    if (!vspwd_send_record(client->fd, frame, sizeof(frame))) {
        return false;
    }

    port->time_codes.head =
        (port->time_codes.head + 1u) % VSPWD_TIME_CODE_QUEUE_DEPTH;
    --port->time_codes.count;
    ++port->statistics.rx_time_codes;
    vspwd_mark_port_changed(server, client->port_id);
    return true;
}

static bool vspwd_flush_monitor_event(vspwd_server_t* server,
                                      int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE];
    vspd_port_snapshot_payload_t snapshot;
    vspd_header_t header;
    uint32_t port_id;

    if (client->port_id >= 0 || client->monitor_pending_mask == 0u) {
        return true;
    }
    for (port_id = 0u; port_id < VSPWD_PORT_COUNT; ++port_id) {
        uint32_t bit = UINT32_C(1) << port_id;
        if ((client->monitor_pending_mask & bit) != 0u) {
            memset(&header, 0, sizeof(header));
            header.magic = VSPD_MAGIC;
            header.version_major = VSPD_VERSION_MAJOR;
            header.version_minor = VSPD_VERSION_MINOR;
            header.type = VSPD_MSG_PORT_SNAPSHOT_EVENT;
            header.header_size = VSPD_HEADER_SIZE;
            header.payload_size = VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE;
            header.port_id = port_id;
            if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
                return false;
            }
            vspwd_port_snapshot_to_wire(&server->ports[port_id], &snapshot);
            vspd_encode_port_snapshot(&snapshot, frame + VSPD_HEADER_SIZE);
            if (!vspwd_send_record(client->fd, frame, sizeof(frame))) {
                return false;
            }
            client->monitor_pending_mask &= ~bit;
            return true;
        }
    }
    return true;
}

static bool vspwd_client_has_output(const vspwd_server_t* server,
                                    int client_index) {
    const vspwd_client_t* client = &server->clients[client_index];
    const vspwd_port_t* port;
    if (client->response_pending) {
        return true;
    }
    if (client->port_id < 0) {
        return client->monitor_pending_mask != 0u;
    }
    port = &server->ports[client->port_id];
    return port->state_event_pending || port->packets.count != 0u ||
           port->time_codes.count != 0u;
}

static bool vspwd_flush_client(vspwd_server_t* server, int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    if (client->response_pending) {
        return vspwd_flush_response(client);
    }
    if (client->port_id < 0) {
        return vspwd_flush_monitor_event(server, client_index);
    }
    if (!vspwd_flush_state_event(server, client_index)) {
        return false;
    }
    if (client->port_id >= 0 &&
        server->ports[client->port_id].state_event_pending) {
        return true;
    }
    if (!vspwd_flush_packet_event(server, client_index)) {
        return false;
    }
    if (!vspwd_flush_time_code_event(server, client_index)) {
        return false;
    }
    return true;
}

static int vspwd_receive_one(vspwd_server_t* server, int client_index) {
    vspwd_client_t* client = &server->clients[client_index];
    uint8_t frame[VSPWD_FRAME_MAX];
    struct iovec iov;
    struct msghdr message;
    ssize_t received;

    memset(&message, 0, sizeof(message));
    iov.iov_base = frame;
    iov.iov_len = sizeof(frame);
    message.msg_iov = &iov;
    message.msg_iovlen = 1u;

    received = recvmsg(client->fd, &message, MSG_DONTWAIT | MSG_TRUNC);
    if (received == 0) {
        return -1;
    }
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        return -1;
    }
    if ((message.msg_flags & MSG_TRUNC) != 0 ||
        (size_t)received > sizeof(frame)) {
        return -1;
    }
    return vspwd_handle_request(server,
                                client_index,
                                frame,
                                (size_t)received)
               ? 1
               : -1;
}

static void vspwd_accept_clients(vspwd_server_t* server) {
    for (;;) {
        int fd = accept4(server->listener_fd,
                         NULL,
                         NULL,
                         SOCK_NONBLOCK | SOCK_CLOEXEC);
        int i;
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            return;
        }
        for (i = 0; i < VSPWD_CLIENT_COUNT; ++i) {
            if (server->clients[i].fd < 0) {
                memset(&server->clients[i], 0, sizeof(server->clients[i]));
                server->clients[i].fd = fd;
                server->clients[i].port_id = -1;
                fd = -1;
                break;
            }
        }
        if (fd >= 0) {
            close(fd);
        }
    }
}

static int vspwd_open_listener(vspwd_server_t* server, const char* socket_path) {
    struct sockaddr_un address;
    size_t path_length;

    if (socket_path == NULL || socket_path[0] == '\0') {
        return -1;
    }
    path_length = strlen(socket_path);
    if (path_length >= sizeof(address.sun_path)) {
        return -1;
    }

    server->listener_fd = socket(AF_UNIX,
                                 SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                 0);
    if (server->listener_fd < 0) {
        return -1;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_length + 1u);
    memcpy(server->socket_path, socket_path, path_length + 1u);

    (void)unlink(socket_path);
    if (bind(server->listener_fd,
             (const struct sockaddr*)&address,
             sizeof(address)) != 0) {
        return -1;
    }
    if (chmod(socket_path, S_IRUSR | S_IWUSR) != 0) {
        return -1;
    }
    if (listen(server->listener_fd, VSPWD_CLIENT_COUNT) != 0) {
        return -1;
    }
    return 0;
}

static void vspwd_cleanup(vspwd_server_t* server) {
    int i;
    if (server->bridge.udp_port != NULL) {
        (void)spw_port_close(server->bridge.udp_port);
        server->bridge.udp_port = NULL;
    }
    for (i = 0; i < VSPWD_CLIENT_COUNT; ++i) {
        if (server->clients[i].fd >= 0) {
            close(server->clients[i].fd);
        }
    }
    if (server->listener_fd >= 0) {
        close(server->listener_fd);
    }
    if (server->socket_path[0] != '\0') {
        (void)unlink(server->socket_path);
    }
}

int vspwd_run_config(const vspwd_config_t* config) {
    vspwd_server_t* server;
    struct sigaction action;
    int result = 0;

    if (config == NULL || config->socket_path == NULL) {
        return 1;
    }
    server = (vspwd_server_t*)calloc(1u, sizeof(vspwd_server_t));
    if (server == NULL) {
        return 1;
    }
    vspwd_init_server(server);
    if (vspwd_open_bridge(server, &config->udp_bridge) != 0) {
        vspwd_cleanup(server);
        free(server);
        return 1;
    }
    if (vspwd_open_listener(server, config->socket_path) != 0) {
        vspwd_cleanup(server);
        free(server);
        return 1;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = vspwd_signal_handler;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    (void)signal(SIGPIPE, SIG_IGN);
    vspwd_stop_requested = 0;

    while (!vspwd_stop_requested) {
        struct pollfd descriptors[1 + VSPWD_CLIENT_COUNT];
        vspwd_service_bridge(server);
        int descriptor_clients[VSPWD_CLIENT_COUNT];
        nfds_t count = 1u;
        int i;
        int poll_result;

        memset(descriptors, 0, sizeof(descriptors));
        descriptors[0].fd = server->listener_fd;
        descriptors[0].events = POLLIN;

        for (i = 0; i < VSPWD_CLIENT_COUNT; ++i) {
            descriptor_clients[i] = -1;
            if (server->clients[i].fd >= 0) {
                descriptor_clients[count - 1u] = i;
                descriptors[count].fd = server->clients[i].fd;
                descriptors[count].events = POLLIN;
                if (vspwd_client_has_output(server, i)) {
                    descriptors[count].events |= POLLOUT;
                }
                ++count;
            }
        }

        poll_result = poll(descriptors, count, server->bridge.enabled ? 10 : 100);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            result = 1;
            break;
        }
        if (poll_result == 0) {
            continue;
        }

        if ((descriptors[0].revents & POLLIN) != 0) {
            vspwd_accept_clients(server);
        }

        for (i = 1; i < (int)count; ++i) {
            int client_index = descriptor_clients[i - 1];
            short revents = descriptors[i].revents;
            bool close_client = false;
            if (client_index < 0 || server->clients[client_index].fd < 0) {
                continue;
            }
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                close_client = true;
            }
            if (!close_client && (revents & POLLIN) != 0) {
                if (vspwd_receive_one(server, client_index) < 0) {
                    close_client = true;
                }
            }
            if (!close_client && (revents & POLLOUT) != 0) {
                if (!vspwd_flush_client(server, client_index) &&
                    errno != EAGAIN && errno != EWOULDBLOCK) {
                    close_client = true;
                }
            }
            if (close_client) {
                vspwd_close_client(server, client_index);
            }
        }
    }

    vspwd_cleanup(server);
    free(server);
    return result;
}

int vspwd_run(const char* socket_path) {
    vspwd_config_t config = VSPWD_CONFIG_INITIALIZER;
    config.socket_path = socket_path;
    return vspwd_run_config(&config);
}
