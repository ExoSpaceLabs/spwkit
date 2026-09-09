#!/usr/bin/env python3
from pathlib import Path

path = Path("src/backends/device/device_backend.c")
text = path.read_text()

old_send = r'''static spw_result_t send_record(device_context_t* context,
                                const uint8_t* data,
                                size_t size,
                                uint64_t deadline) {
    for (;;) {
        ssize_t sent;
        spw_result_t result = wait_fd(context, POLLOUT, deadline);
        if (result != SPW_OK) {
            return result;
        }
        sent = send(context->fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent == (ssize_t)size) {
            return SPW_OK;
        }
        if (sent >= 0) {
            mark_disconnected(context);
            return SPW_ERR_BACKEND;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
}
'''
new_send = r'''static spw_result_t send_record(device_context_t* context,
                                const uint8_t* data,
                                size_t size,
                                uint64_t deadline) {
    for (;;) {
        const ssize_t sent =
            send(context->fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent == (ssize_t)size) {
            return SPW_OK;
        }
        if (sent >= 0) {
            mark_disconnected(context);
            return SPW_ERR_BACKEND;
        }
        if (errno == EINTR) {
            if (deadline != UINT64_MAX && poll_timeout_ms(deadline) == 0) {
                return SPW_ERR_TIMEOUT;
            }
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            const spw_result_t result = wait_fd(context, POLLOUT, deadline);
            if (result != SPW_OK) {
                return result;
            }
            continue;
        }
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
}
'''

old_recv = r'''static spw_result_t receive_record(device_context_t* context,
                                   uint8_t* frame,
                                   size_t capacity,
                                   size_t* out_size,
                                   uint64_t deadline) {
    for (;;) {
        ssize_t received;
        spw_result_t result = wait_fd(context, POLLIN, deadline);
        if (result != SPW_OK) {
            return result;
        }
        received = recv(context->fd, frame, capacity, MSG_DONTWAIT);
        if (received > 0) {
            *out_size = (size_t)received;
            return SPW_OK;
        }
        if (received == 0) {
            mark_disconnected(context);
            return SPW_ERR_LINK_UNAVAILABLE;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
}
'''
new_recv = r'''static spw_result_t receive_record(device_context_t* context,
                                   uint8_t* frame,
                                   size_t capacity,
                                   size_t* out_size,
                                   uint64_t deadline) {
    for (;;) {
        const ssize_t received = recv(context->fd, frame, capacity, MSG_DONTWAIT);
        if (received > 0) {
            *out_size = (size_t)received;
            return SPW_OK;
        }
        if (received == 0) {
            mark_disconnected(context);
            return SPW_ERR_LINK_UNAVAILABLE;
        }
        if (errno == EINTR) {
            if (deadline != UINT64_MAX && poll_timeout_ms(deadline) == 0) {
                return SPW_ERR_TIMEOUT;
            }
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            const spw_result_t result = wait_fd(context, POLLIN, deadline);
            if (result != SPW_OK) {
                return result;
            }
            continue;
        }
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
}
'''

for label, old, new in (("send_record", old_send, new_send),
                        ("receive_record", old_recv, new_recv)):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    text = text.replace(old, new, 1)

path.write_text(text)
print("Applied guarded DEVICE readiness fast-path patch")
