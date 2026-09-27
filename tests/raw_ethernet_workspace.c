// SPDX-License-Identifier: Apache-2.0
#include <spwkit/raw_ethernet.h>
#include <spwkit/spwkit.h>

#include <stdio.h>
#include <stdlib.h>

static spw_result_t io_start(void* context) { (void)context; return SPW_OK; }
static spw_result_t io_stop(void* context) { (void)context; return SPW_OK; }
static spw_result_t io_reset(void* context) { (void)context; return SPW_OK; }

static spw_result_t io_send(void* context, const uint8_t* frame,
                            size_t size, spw_timeout_us_t timeout) {
    (void)context; (void)frame; (void)size; (void)timeout;
    return SPW_OK;
}

static spw_result_t io_receive(void* context, uint8_t* frame,
                               size_t capacity, size_t* out_size,
                               spw_timeout_us_t timeout) {
    (void)context; (void)frame; (void)capacity; (void)timeout;
    if (out_size == NULL) return SPW_ERR_INVALID_ARGUMENT;
    *out_size = 0u;
    return SPW_ERR_TIMEOUT;
}

static spw_result_t io_wait(void* context,
                            spw_raw_ethernet_ready_t interests,
                            spw_timeout_us_t timeout,
                            spw_raw_ethernet_ready_t* out_ready) {
    (void)context; (void)interests; (void)timeout;
    if (out_ready == NULL) return SPW_ERR_INVALID_ARGUMENT;
    *out_ready = SPW_RAW_ETHERNET_READY_NONE;
    return SPW_ERR_TIMEOUT;
}

static spw_result_t io_mtu(const void* context, size_t* out_size) {
    (void)context;
    if (out_size == NULL) return SPW_ERR_INVALID_ARGUMENT;
    *out_size = 1518u;
    return SPW_OK;
}

static spw_result_t io_link(const void* context, bool* out_up) {
    (void)context;
    if (out_up == NULL) return SPW_ERR_INVALID_ARGUMENT;
    *out_up = true;
    return SPW_OK;
}

static uint64_t now_us(const void* context) {
    (void)context;
    return 1u;
}

static spw_result_t delay_us(void* context, uint64_t delay,
                             spw_timeout_us_t timeout) {
    (void)context; (void)delay; (void)timeout;
    return SPW_OK;
}

static const spw_raw_ethernet_io_ops_t IO_OPS = {
    sizeof(spw_raw_ethernet_io_ops_t),
    SPW_RAW_ETHERNET_IO_OPS_VERSION,
    io_start, io_stop, io_reset,
    io_send, io_receive, io_wait, io_mtu, io_link
};

static const spw_runtime_ops_t RUNTIME_OPS = {
    sizeof(spw_runtime_ops_t),
    SPW_RUNTIME_OPS_VERSION,
    now_us, delay_us
};

int main(int argc, char** argv) {
    spw_raw_ethernet_config_t raw =
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &IO_OPS, NULL, &RUNTIME_OPS, NULL, UINT32_C(0x454d4244));
    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    unsigned long long maximum = 0u;

    raw.local_mac[0] = 0x02u;
    raw.local_mac[5] = 0x01u;
    raw.remote_mac[0] = 0x02u;
    raw.remote_mac[5] = 0x02u;
    raw.ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
    raw.fragment_payload_size = 1400u;
    config.backend_config = &raw;
    config.backend_config_size = sizeof(raw);

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK) {
        return 1;
    }
    printf("raw_ethernet_workspace_bytes=%zu\n", requirements.size);
    printf("raw_ethernet_workspace_alignment=%zu\n", requirements.alignment);

    if (argc == 2) {
        char* end = NULL;
        maximum = strtoull(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' ||
            requirements.size > (size_t)maximum) {
            fprintf(stderr, "workspace exceeds limit: %zu > %llu\n",
                    requirements.size, maximum);
            return 2;
        }
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [MAX_WORKSPACE_BYTES]\n", argv[0]);
        return 2;
    }

    return 0;
}
