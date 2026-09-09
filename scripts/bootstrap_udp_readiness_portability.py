#!/usr/bin/env python3
from pathlib import Path

path = Path("src/backends/ethernet/udp_backend.c")
text = path.read_text()

old_send = r'''static spw_result_t send_datagram_raw(spw_udp_backend_t* backend,
                                      const uint8_t* bytes,
                                      size_t size,
                                      spw_timeout_us_t timeout_us) {
    struct sockaddr_in remote;
    spw_udp_deadline_t deadline;

    if (backend->socket_fd < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(backend->config.remote_port);
    if (inet_pton(AF_INET, backend->config.remote_address, &remote.sin_addr) != 1) {
        return SPW_ERR_BACKEND;
    }

    deadline = deadline_make(timeout_us);
    for (;;) {
        const ssize_t sent = sendto(backend->socket_fd, bytes, size, MSG_DONTWAIT,
                                    (const struct sockaddr*)&remote,
                                    sizeof(remote));
        if (sent == (ssize_t)size) {
            return SPW_OK;
        }
        if (sent >= 0) {
            return SPW_ERR_BACKEND;
        }
        if (errno == EINTR) {
            if (!deadline.infinite && deadline_expired(&deadline)) {
                return SPW_ERR_TIMEOUT;
            }
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return SPW_ERR_BACKEND;
        }

        {
            struct pollfd descriptor;
            int ready;
            memset(&descriptor, 0, sizeof(descriptor));
            descriptor.fd = backend->socket_fd;
            descriptor.events = POLLOUT;
            do {
                ready = poll(&descriptor, 1,
                             timeout_ms(deadline_remaining(&deadline)));
            } while (ready < 0 && errno == EINTR);
            if (ready == 0) {
                return SPW_ERR_TIMEOUT;
            }
            if (ready < 0 || (descriptor.revents & POLLOUT) == 0) {
                return SPW_ERR_BACKEND;
            }
        }
    }
}
'''
new_send = r'''static spw_result_t send_datagram_raw(spw_udp_backend_t* backend,
                                      const uint8_t* bytes,
                                      size_t size,
                                      spw_timeout_us_t timeout_us) {
    struct sockaddr_in remote;

    if (backend->socket_fd < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    memset(&remote, 0, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port = htons(backend->config.remote_port);
    if (inet_pton(AF_INET, backend->config.remote_address, &remote.sin_addr) != 1) {
        return SPW_ERR_BACKEND;
    }

#if defined(MSG_DONTWAIT)
    {
        spw_udp_deadline_t deadline = deadline_make(timeout_us);
        for (;;) {
            const ssize_t sent = sendto(backend->socket_fd, bytes, size,
                                        MSG_DONTWAIT,
                                        (const struct sockaddr*)&remote,
                                        sizeof(remote));
            if (sent == (ssize_t)size) {
                return SPW_OK;
            }
            if (sent >= 0) {
                return SPW_ERR_BACKEND;
            }
            if (errno == EINTR) {
                if (!deadline.infinite && deadline_expired(&deadline)) {
                    return SPW_ERR_TIMEOUT;
                }
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return SPW_ERR_BACKEND;
            }

            {
                struct pollfd descriptor;
                int ready;
                memset(&descriptor, 0, sizeof(descriptor));
                descriptor.fd = backend->socket_fd;
                descriptor.events = POLLOUT;
                do {
                    ready = poll(&descriptor, 1,
                                 timeout_ms(deadline_remaining(&deadline)));
                } while (ready < 0 && errno == EINTR);
                if (ready == 0) {
                    return SPW_ERR_TIMEOUT;
                }
                if (ready < 0 || (descriptor.revents & POLLOUT) == 0) {
                    return SPW_ERR_BACKEND;
                }
            }
        }
    }
#else
    {
        struct pollfd descriptor;
        int ready;
        ssize_t sent;
        memset(&descriptor, 0, sizeof(descriptor));
        descriptor.fd = backend->socket_fd;
        descriptor.events = POLLOUT;
        do {
            ready = poll(&descriptor, 1, timeout_ms(timeout_us));
        } while (ready < 0 && errno == EINTR);
        if (ready == 0) {
            return SPW_ERR_TIMEOUT;
        }
        if (ready < 0 || (descriptor.revents & POLLOUT) == 0) {
            return SPW_ERR_BACKEND;
        }
        sent = sendto(backend->socket_fd, bytes, size, 0,
                      (const struct sockaddr*)&remote, sizeof(remote));
        return sent == (ssize_t)size ? SPW_OK : SPW_ERR_BACKEND;
    }
#endif
}
'''

old_pump = r'''    wait_timeout = min_timeout(timeout_us, service_slice);
    {
        spw_udp_deadline_t receive_deadline = deadline_make(wait_timeout);
        for (;;) {
            memset(&source, 0, sizeof(source));
            source_size = sizeof(source);
            received = recvfrom(backend->socket_fd, backend->rx_datagram,
                                sizeof(backend->rx_datagram), MSG_DONTWAIT,
                                (struct sockaddr*)&source, &source_size);
            if (received >= 0) {
                wait_result = SPW_OK;
                break;
            }
            if (errno == EINTR) {
                if (!receive_deadline.infinite &&
                    deadline_expired(&receive_deadline)) {
                    wait_result = SPW_ERR_TIMEOUT;
                    break;
                }
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                wait_result = SPW_ERR_BACKEND;
                break;
            }
            wait_result = wait_readable(
                backend, deadline_remaining(&receive_deadline));
            if (wait_result != SPW_OK) {
                break;
            }
        }
    }
'''
new_pump = r'''    wait_timeout = min_timeout(timeout_us, service_slice);
#if defined(MSG_DONTWAIT)
    {
        spw_udp_deadline_t receive_deadline = deadline_make(wait_timeout);
        for (;;) {
            memset(&source, 0, sizeof(source));
            source_size = sizeof(source);
            received = recvfrom(backend->socket_fd, backend->rx_datagram,
                                sizeof(backend->rx_datagram), MSG_DONTWAIT,
                                (struct sockaddr*)&source, &source_size);
            if (received >= 0) {
                wait_result = SPW_OK;
                break;
            }
            if (errno == EINTR) {
                if (!receive_deadline.infinite &&
                    deadline_expired(&receive_deadline)) {
                    wait_result = SPW_ERR_TIMEOUT;
                    break;
                }
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                wait_result = SPW_ERR_BACKEND;
                break;
            }
            wait_result = wait_readable(
                backend, deadline_remaining(&receive_deadline));
            if (wait_result != SPW_OK) {
                break;
            }
        }
    }
#else
    wait_result = wait_readable(backend, wait_timeout);
    if (wait_result == SPW_OK) {
        memset(&source, 0, sizeof(source));
        source_size = sizeof(source);
        received = recvfrom(backend->socket_fd, backend->rx_datagram,
                            sizeof(backend->rx_datagram), 0,
                            (struct sockaddr*)&source, &source_size);
        if (received < 0) {
            return errno == EINTR ? SPW_ERR_TIMEOUT : SPW_ERR_BACKEND;
        }
    }
#endif
'''

for label, old, new in (("send portability", old_send, new_send),
                        ("receive portability", old_pump, new_pump)):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    text = text.replace(old, new, 1)

path.write_text(text)
print("Applied guarded UDP readiness portability patch")
