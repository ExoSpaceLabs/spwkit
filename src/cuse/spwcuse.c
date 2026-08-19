// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L
#define FUSE_USE_VERSION 35

#include "cuse/vspw_cuse_record.h"

#include <cuse_lowlevel.h>
#include <spwkit/spwkit.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPWCUSE_READ_QUEUE_DEPTH 16u
#define SPWCUSE_WRITE_QUEUE_DEPTH 16u
#define SPWCUSE_RECORD_QUEUE_DEPTH 16u
#define SPWCUSE_BACKEND_WAIT_US UINT64_C(20000)
#define SPWCUSE_TRANSFER_TIMEOUT_US UINT64_C(2000000)

typedef struct spwcuse_pending_read {
    fuse_req_t req;
    size_t size;
} spwcuse_pending_read_t;

typedef struct spwcuse_pending_write {
    fuse_req_t req;
    size_t size;
    uint8_t* record;
} spwcuse_pending_write_t;

typedef struct spwcuse_record {
    uint8_t* data;
    size_t size;
} spwcuse_record_t;

typedef struct spwcuse_state {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    pthread_t worker;
    bool worker_started;
    bool stopping;
    bool opened;

    spw_port_t* port;
    uint8_t rx_storage[VSPW_CUSE_RECORD_MAX_PAYLOAD];

    spwcuse_pending_read_t reads[SPWCUSE_READ_QUEUE_DEPTH];
    size_t read_head;
    size_t read_count;

    spwcuse_pending_write_t writes[SPWCUSE_WRITE_QUEUE_DEPTH];
    size_t write_head;
    size_t write_count;

    spwcuse_record_t records[SPWCUSE_RECORD_QUEUE_DEPTH];
    size_t record_head;
    size_t record_count;

    struct fuse_pollhandle* poll_handle;
} spwcuse_state_t;

static int result_to_errno(spw_result_t result) {
    switch (result) {
        case SPW_OK: return 0;
        case SPW_ERR_INVALID_ARGUMENT:
        case SPW_ERR_INVALID_PACKET: return EINVAL;
        case SPW_ERR_INVALID_STATE: return EBUSY;
        case SPW_ERR_TIMEOUT: return EAGAIN;
        case SPW_ERR_UNSUPPORTED: return EOPNOTSUPP;
        case SPW_ERR_RESOURCE_EXHAUSTED: return ENOBUFS;
        case SPW_ERR_LINK_UNAVAILABLE: return ENOLINK;
        case SPW_ERR_BUFFER_TOO_SMALL: return EMSGSIZE;
        case SPW_ERR_BACKEND:
        default: return EIO;
    }
}

static void notify_poll_handle(struct fuse_pollhandle* handle) {
    if (handle == NULL) {
        return;
    }
    (void)fuse_lowlevel_notify_poll(handle);
    fuse_pollhandle_destroy(handle);
}

static struct fuse_pollhandle* take_poll_handle_locked(spwcuse_state_t* state) {
    struct fuse_pollhandle* handle = state->poll_handle;
    state->poll_handle = NULL;
    return handle;
}

static unsigned current_poll_events_locked(const spwcuse_state_t* state) {
    unsigned events = 0u;
    if (state->record_count != 0u) {
        events |= (unsigned)(POLLIN | POLLRDNORM);
    }
    if (state->write_count < SPWCUSE_WRITE_QUEUE_DEPTH) {
        events |= (unsigned)(POLLOUT | POLLWRNORM);
    }
    return events;
}

static bool enqueue_read_locked(spwcuse_state_t* state,
                                fuse_req_t req,
                                size_t size) {
    size_t index;
    if (state->read_count >= SPWCUSE_READ_QUEUE_DEPTH) {
        return false;
    }
    index = (state->read_head + state->read_count) % SPWCUSE_READ_QUEUE_DEPTH;
    state->reads[index].req = req;
    state->reads[index].size = size;
    ++state->read_count;
    return true;
}

static bool enqueue_write_locked(spwcuse_state_t* state,
                                 fuse_req_t req,
                                 uint8_t* record,
                                 size_t size) {
    size_t index;
    if (state->write_count >= SPWCUSE_WRITE_QUEUE_DEPTH) {
        return false;
    }
    index = (state->write_head + state->write_count) % SPWCUSE_WRITE_QUEUE_DEPTH;
    state->writes[index].req = req;
    state->writes[index].record = record;
    state->writes[index].size = size;
    ++state->write_count;
    return true;
}

static bool pop_write_locked(spwcuse_state_t* state,
                             spwcuse_pending_write_t* out) {
    if (state->write_count == 0u) {
        return false;
    }
    *out = state->writes[state->write_head];
    state->writes[state->write_head] = (spwcuse_pending_write_t){0};
    state->write_head = (state->write_head + 1u) % SPWCUSE_WRITE_QUEUE_DEPTH;
    --state->write_count;
    return true;
}

static bool enqueue_record_locked(spwcuse_state_t* state,
                                  uint8_t type,
                                  uint8_t flags,
                                  const uint8_t* payload,
                                  size_t payload_size) {
    vspw_cuse_record_header_t header;
    spwcuse_record_t* slot;
    uint8_t* record;
    size_t record_size;
    size_t index;

    if (state->record_count >= SPWCUSE_RECORD_QUEUE_DEPTH ||
        payload_size > VSPW_CUSE_RECORD_MAX_PAYLOAD) {
        return false;
    }
    if (payload_size > SIZE_MAX - VSPW_CUSE_RECORD_HEADER_SIZE) {
        return false;
    }
    record_size = VSPW_CUSE_RECORD_HEADER_SIZE + payload_size;
    record = (uint8_t*)malloc(record_size == 0u ? 1u : record_size);
    if (record == NULL) {
        return false;
    }

    header.type = type;
    header.flags = flags;
    header.payload_size = (uint32_t)payload_size;
    if (vspw_cuse_record_encode_header(&header, record) != VSPW_CUSE_RECORD_OK) {
        free(record);
        return false;
    }
    if (payload_size != 0u) {
        memcpy(record + VSPW_CUSE_RECORD_HEADER_SIZE, payload, payload_size);
    }

    index = (state->record_head + state->record_count) % SPWCUSE_RECORD_QUEUE_DEPTH;
    slot = &state->records[index];
    slot->data = record;
    slot->size = record_size;
    ++state->record_count;
    return true;
}

static bool service_one_read(spwcuse_state_t* state) {
    spwcuse_pending_read_t pending;
    spwcuse_record_t record = {0};
    bool consume_record = false;

    pthread_mutex_lock(&state->mutex);
    if (state->read_count == 0u || state->record_count == 0u) {
        pthread_mutex_unlock(&state->mutex);
        return false;
    }

    pending = state->reads[state->read_head];
    state->reads[state->read_head] = (spwcuse_pending_read_t){0};
    state->read_head = (state->read_head + 1u) % SPWCUSE_READ_QUEUE_DEPTH;
    --state->read_count;

    record = state->records[state->record_head];
    if (pending.size >= record.size) {
        state->records[state->record_head] = (spwcuse_record_t){0};
        state->record_head = (state->record_head + 1u) % SPWCUSE_RECORD_QUEUE_DEPTH;
        --state->record_count;
        consume_record = true;
    }
    pthread_mutex_unlock(&state->mutex);

    if (!consume_record) {
        (void)fuse_reply_err(pending.req, EMSGSIZE);
        return true;
    }

    (void)fuse_reply_buf(pending.req, (const char*)record.data, record.size);
    free(record.data);
    return true;
}

static int validate_user_record(const char* data,
                                size_t size,
                                vspw_cuse_record_header_t* out_header) {
    const uint8_t* bytes = (const uint8_t*)data;
    size_t payload_size;

    if (data == NULL || out_header == NULL ||
        size < VSPW_CUSE_RECORD_HEADER_SIZE) {
        return EINVAL;
    }
    if (vspw_cuse_record_decode_header(bytes, out_header) != VSPW_CUSE_RECORD_OK) {
        return EINVAL;
    }
    payload_size = size - VSPW_CUSE_RECORD_HEADER_SIZE;
    if (payload_size != out_header->payload_size) {
        return EMSGSIZE;
    }
    if (vspw_cuse_record_validate_payload(
            out_header,
            payload_size == 0u ? NULL : bytes + VSPW_CUSE_RECORD_HEADER_SIZE,
            payload_size) != VSPW_CUSE_RECORD_OK) {
        return EINVAL;
    }
    return 0;
}

static void process_write(spwcuse_state_t* state,
                          spwcuse_pending_write_t* pending) {
    vspw_cuse_record_header_t header;
    const uint8_t* payload;
    spw_result_t result;
    int error;

    error = validate_user_record((const char*)pending->record,
                                 pending->size,
                                 &header);
    if (error != 0) {
        (void)fuse_reply_err(pending->req, error);
        free(pending->record);
        return;
    }

    payload = pending->record + VSPW_CUSE_RECORD_HEADER_SIZE;
    if (header.type == VSPW_CUSE_RECORD_DATA) {
        spw_packet_t packet;
        packet.data = (uint8_t*)payload;
        packet.length = header.payload_size;
        packet.capacity = header.payload_size;
        packet.terminator = (header.flags & VSPW_CUSE_RECORD_FLAG_EEP) != 0u
                                ? SPW_TERMINATOR_EEP
                                : SPW_TERMINATOR_EOP;
        result = spw_port_send(state->port, &packet, SPWCUSE_TRANSFER_TIMEOUT_US);
    } else {
        spw_time_code_t time_code;
        time_code.time_count = payload[0];
        time_code.control_flags = payload[1];
        result = spw_port_send_time_code(
            state->port, &time_code, SPWCUSE_TRANSFER_TIMEOUT_US);
    }

    if (result == SPW_OK) {
        (void)fuse_reply_write(pending->req, pending->size);
    } else {
        (void)fuse_reply_err(pending->req, result_to_errno(result));
    }
    free(pending->record);
}

static bool receive_one_ready_record(spwcuse_state_t* state,
                                     spw_ready_events_t ready) {
    struct fuse_pollhandle* poll_handle = NULL;
    bool queued = false;

    if ((ready & SPW_READY_RX_PACKET) != 0u) {
        spw_packet_t packet = {
            state->rx_storage,
            0u,
            sizeof(state->rx_storage),
            SPW_TERMINATOR_EOP};
        spw_result_t result = spw_port_receive(
            state->port, &packet, SPW_TIMEOUT_IMMEDIATE);
        if (result == SPW_OK) {
            uint8_t flags = packet.terminator == SPW_TERMINATOR_EEP
                                ? VSPW_CUSE_RECORD_FLAG_EEP
                                : 0u;
            pthread_mutex_lock(&state->mutex);
            queued = enqueue_record_locked(
                state, VSPW_CUSE_RECORD_DATA, flags, packet.data, packet.length);
            if (queued) {
                poll_handle = take_poll_handle_locked(state);
            }
            pthread_mutex_unlock(&state->mutex);
            notify_poll_handle(poll_handle);
            return queued;
        }
    }

    if ((ready & SPW_READY_RX_TIME_CODE) != 0u) {
        spw_time_code_t time_code;
        spw_result_t result = spw_port_receive_time_code(
            state->port, &time_code, SPW_TIMEOUT_IMMEDIATE);
        if (result == SPW_OK) {
            uint8_t payload[2] = {time_code.time_count, time_code.control_flags};
            pthread_mutex_lock(&state->mutex);
            queued = enqueue_record_locked(
                state, VSPW_CUSE_RECORD_TIME_CODE, 0u, payload, sizeof(payload));
            if (queued) {
                poll_handle = take_poll_handle_locked(state);
            }
            pthread_mutex_unlock(&state->mutex);
            notify_poll_handle(poll_handle);
            return queued;
        }
    }
    return false;
}

static void* broker_main(void* opaque) {
    spwcuse_state_t* state = (spwcuse_state_t*)opaque;

    for (;;) {
        spwcuse_pending_write_t pending_write = {0};
        struct fuse_pollhandle* poll_handle = NULL;
        bool should_wait_backend;
        bool have_write;

        while (service_one_read(state)) {
        }

        pthread_mutex_lock(&state->mutex);
        if (state->stopping) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }
        have_write = pop_write_locked(state, &pending_write);
        if (have_write) {
            poll_handle = take_poll_handle_locked(state);
        }
        should_wait_backend =
            !have_write && state->record_count < SPWCUSE_RECORD_QUEUE_DEPTH &&
            (state->read_count != 0u || state->poll_handle != NULL);

        if (!have_write && !should_wait_backend) {
            (void)pthread_cond_wait(&state->cond, &state->mutex);
            pthread_mutex_unlock(&state->mutex);
            continue;
        }
        pthread_mutex_unlock(&state->mutex);

        notify_poll_handle(poll_handle);

        if (have_write) {
            process_write(state, &pending_write);
            continue;
        }

        if (should_wait_backend) {
            spw_ready_events_t ready = SPW_READY_NONE;
            spw_result_t result = spw_port_wait(
                state->port,
                SPW_READY_ALL,
                SPWCUSE_BACKEND_WAIT_US,
                &ready);
            if (result == SPW_OK && ready != SPW_READY_NONE) {
                while (receive_one_ready_record(state, ready)) {
                    ready = SPW_READY_NONE;
                    result = spw_port_wait(
                        state->port,
                        SPW_READY_ALL,
                        SPW_TIMEOUT_IMMEDIATE,
                        &ready);
                    if (result != SPW_OK || ready == SPW_READY_NONE) {
                        break;
                    }
                    pthread_mutex_lock(&state->mutex);
                    if (state->record_count >= SPWCUSE_RECORD_QUEUE_DEPTH) {
                        pthread_mutex_unlock(&state->mutex);
                        break;
                    }
                    pthread_mutex_unlock(&state->mutex);
                }
            }
        }
    }
    return NULL;
}

static void cuse_open(fuse_req_t req, struct fuse_file_info* fi) {
    spwcuse_state_t* state = (spwcuse_state_t*)fuse_req_userdata(req);
    bool busy;

    pthread_mutex_lock(&state->mutex);
    busy = state->opened;
    if (!busy) {
        state->opened = true;
    }
    pthread_mutex_unlock(&state->mutex);

    if (busy) {
        (void)fuse_reply_err(req, EBUSY);
        return;
    }
    fi->direct_io = 1u;
    fi->nonseekable = 1u;
    fi->fh = 1u;
    (void)fuse_reply_open(req, fi);
}

static void cuse_read(fuse_req_t req,
                      size_t size,
                      off_t off,
                      struct fuse_file_info* fi) {
    spwcuse_state_t* state = (spwcuse_state_t*)fuse_req_userdata(req);
    bool nonblocking = (fi->flags & O_NONBLOCK) != 0;
    bool queued = false;
    bool have_record;

    (void)off;
    pthread_mutex_lock(&state->mutex);
    have_record = state->record_count != 0u;
    if (have_record || !nonblocking) {
        queued = enqueue_read_locked(state, req, size);
        if (queued) {
            (void)pthread_cond_signal(&state->cond);
        }
    }
    pthread_mutex_unlock(&state->mutex);

    if (queued) {
        return;
    }
    (void)fuse_reply_err(req, have_record || !nonblocking ? ENOBUFS : EAGAIN);
}

static void cuse_write(fuse_req_t req,
                       const char* buf,
                       size_t size,
                       off_t off,
                       struct fuse_file_info* fi) {
    spwcuse_state_t* state = (spwcuse_state_t*)fuse_req_userdata(req);
    vspw_cuse_record_header_t header;
    uint8_t* copy;
    bool queued;
    int error;

    (void)off;
    (void)fi;
    error = validate_user_record(buf, size, &header);
    if (error != 0) {
        (void)fuse_reply_err(req, error);
        return;
    }

    copy = (uint8_t*)malloc(size == 0u ? 1u : size);
    if (copy == NULL) {
        (void)fuse_reply_err(req, ENOMEM);
        return;
    }
    memcpy(copy, buf, size);

    pthread_mutex_lock(&state->mutex);
    queued = enqueue_write_locked(state, req, copy, size);
    if (queued) {
        (void)pthread_cond_signal(&state->cond);
    }
    pthread_mutex_unlock(&state->mutex);

    if (!queued) {
        free(copy);
        (void)fuse_reply_err(req, (fi->flags & O_NONBLOCK) != 0 ? EAGAIN : ENOBUFS);
    }
}

static void cuse_poll(fuse_req_t req,
                      struct fuse_file_info* fi,
                      struct fuse_pollhandle* ph) {
    spwcuse_state_t* state = (spwcuse_state_t*)fuse_req_userdata(req);
    struct fuse_pollhandle* discard = NULL;
    unsigned events;

    (void)fi;
    pthread_mutex_lock(&state->mutex);
    events = current_poll_events_locked(state);
    if (ph != NULL) {
        if (state->poll_handle == NULL) {
            state->poll_handle = ph;
        } else {
            discard = ph;
        }
        (void)pthread_cond_signal(&state->cond);
    }
    pthread_mutex_unlock(&state->mutex);

    if (discard != NULL) {
        fuse_pollhandle_destroy(discard);
    }
    (void)fuse_reply_poll(req, events);
}

static void cuse_release(fuse_req_t req, struct fuse_file_info* fi) {
    spwcuse_state_t* state = (spwcuse_state_t*)fuse_req_userdata(req);
    fuse_req_t cancelled[SPWCUSE_READ_QUEUE_DEPTH];
    struct fuse_pollhandle* poll_handle;
    size_t cancelled_count = 0u;
    size_t i;

    (void)fi;
    pthread_mutex_lock(&state->mutex);
    state->opened = false;
    while (state->read_count != 0u && cancelled_count < SPWCUSE_READ_QUEUE_DEPTH) {
        cancelled[cancelled_count++] = state->reads[state->read_head].req;
        state->reads[state->read_head] = (spwcuse_pending_read_t){0};
        state->read_head = (state->read_head + 1u) % SPWCUSE_READ_QUEUE_DEPTH;
        --state->read_count;
    }
    poll_handle = take_poll_handle_locked(state);
    pthread_mutex_unlock(&state->mutex);

    for (i = 0u; i < cancelled_count; ++i) {
        (void)fuse_reply_err(cancelled[i], EINTR);
    }
    if (poll_handle != NULL) {
        fuse_pollhandle_destroy(poll_handle);
    }
    (void)fuse_reply_err(req, 0);
}

static const struct cuse_lowlevel_ops cuse_ops = {
    .open = cuse_open,
    .read = cuse_read,
    .write = cuse_write,
    .poll = cuse_poll,
    .release = cuse_release,
};

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH] [--port ID] [--device NAME]\n"
            "       %s --api-check\n\n"
            "Creates /dev/NAME as a packet-record SpaceWire CUSE device.\n"
            "The presenter owns one VSPD port for its lifetime; a second VSPD\n"
            "application cannot attach to that same port concurrently.\n",
            program,
            program);
}

static int parse_u32(const char* text, uint32_t* out) {
    char* end = NULL;
    unsigned long value;
    if (text == NULL || out == NULL || *text == '\0') {
        return 0;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}

static const char* normalize_device_name(const char* value) {
    static const char prefix[] = "/dev/";
    if (value != NULL && strncmp(value, prefix, sizeof(prefix) - 1u) == 0) {
        return value + sizeof(prefix) - 1u;
    }
    return value;
}

static void cleanup_state(spwcuse_state_t* state) {
    size_t i;
    if (state == NULL) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    state->stopping = true;
    (void)pthread_cond_broadcast(&state->cond);
    pthread_mutex_unlock(&state->mutex);

    if (state->worker_started) {
        (void)pthread_join(state->worker, NULL);
    }
    if (state->port != NULL) {
        (void)spw_port_close(state->port);
        state->port = NULL;
    }
    if (state->poll_handle != NULL) {
        fuse_pollhandle_destroy(state->poll_handle);
        state->poll_handle = NULL;
    }
    for (i = 0u; i < SPWCUSE_WRITE_QUEUE_DEPTH; ++i) {
        free(state->writes[i].record);
    }
    for (i = 0u; i < SPWCUSE_RECORD_QUEUE_DEPTH; ++i) {
        free(state->records[i].data);
    }
    (void)pthread_cond_destroy(&state->cond);
    (void)pthread_mutex_destroy(&state->mutex);
    free(state);
}

int main(int argc, char** argv) {
    spwcuse_state_t* state = NULL;
    spw_device_config_t device_config;
    spw_port_config_t port_config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    struct cuse_info cuse_info;
    const char* socket_path = SPW_DEVICE_DEFAULT_ENDPOINT;
    const char* requested_device = NULL;
    const char* device_name;
    const char* dev_info_argv[1];
    char dev_info[192];
    char generated_device[48];
    char* cuse_argv[4];
    uint32_t port_id = 0u;
    size_t socket_length;
    int written;
    int i;
    int result = 1;

    if (argc == 2 && strcmp(argv[1], "--api-check") == 0) {
        printf("spwcuse CUSE API and packet-record codec available\n");
        return 0;
    }

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--socket") == 0 && i + 1 < argc) {
            socket_path = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &port_id)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            requested_device = normalize_device_name(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (requested_device == NULL) {
        written = snprintf(generated_device,
                           sizeof(generated_device),
                           "vspw%u",
                           (unsigned)port_id);
        if (written < 0 || (size_t)written >= sizeof(generated_device)) {
            return 2;
        }
        device_name = generated_device;
    } else {
        device_name = requested_device;
    }
    if (*device_name == '\0' || strchr(device_name, '/') != NULL) {
        fprintf(stderr, "spwcuse: device name must be a single /dev entry name\n");
        return 2;
    }

    state = (spwcuse_state_t*)calloc(1u, sizeof(*state));
    if (state == NULL) {
        perror("calloc");
        return 1;
    }
    if (pthread_mutex_init(&state->mutex, NULL) != 0 ||
        pthread_cond_init(&state->cond, NULL) != 0) {
        fprintf(stderr, "spwcuse: failed to initialize broker synchronization\n");
        free(state);
        return 1;
    }

    device_config = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER(port_id);
    socket_length = strlen(socket_path);
    if (socket_length >= sizeof(device_config.endpoint)) {
        fprintf(stderr, "spwcuse: VSPD socket path is too long\n");
        cleanup_state(state);
        return 2;
    }
    memcpy(device_config.endpoint, socket_path, socket_length + 1u);
    port_config.backend_config = &device_config;
    port_config.backend_config_size = sizeof(device_config);

    {
        spw_result_t open_result = spw_port_open(&port_config, &state->port);
        if (open_result != SPW_OK || state->port == NULL) {
            fprintf(stderr,
                    "spwcuse: failed to attach VSPD port %u (%d)\n",
                    (unsigned)port_id,
                    (int)open_result);
            cleanup_state(state);
            return 1;
        }
    }
    {
        spw_result_t start_result = spw_port_start(state->port);
        if (start_result != SPW_OK) {
            fprintf(stderr,
                    "spwcuse: failed to start VSPD port %u (%d)\n",
                    (unsigned)port_id,
                    (int)start_result);
            cleanup_state(state);
            return 1;
        }
    }

    if (pthread_create(&state->worker, NULL, broker_main, state) != 0) {
        fprintf(stderr, "spwcuse: failed to start broker thread\n");
        cleanup_state(state);
        return 1;
    }
    state->worker_started = true;

    written = snprintf(dev_info, sizeof(dev_info), "DEVNAME=%s", device_name);
    if (written < 0 || (size_t)written >= sizeof(dev_info)) {
        fprintf(stderr, "spwcuse: device name is too long\n");
        cleanup_state(state);
        return 2;
    }

    memset(&cuse_info, 0, sizeof(cuse_info));
    dev_info_argv[0] = dev_info;
    cuse_info.dev_info_argc = 1u;
    cuse_info.dev_info_argv = dev_info_argv;

    cuse_argv[0] = argv[0];
    cuse_argv[1] = (char*)"-f";
    cuse_argv[2] = (char*)"-s";
    cuse_argv[3] = NULL;

    fprintf(stderr,
            "spwcuse: presenting /dev/%s from VSPD port %u at %s\n",
            device_name,
            (unsigned)port_id,
            socket_path);
    result = cuse_lowlevel_main(3, cuse_argv, &cuse_info, &cuse_ops, state);
    cleanup_state(state);
    return result == 0 ? 0 : 1;
}
