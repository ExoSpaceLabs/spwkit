#!/usr/bin/env python3
from pathlib import Path

path = Path("benchmarks/profile_device_comparison.c")
text = path.read_text()

old = r'''static ssize_t raw_receive_record(int fd, uint8_t* frame, size_t capacity) {
    for (;;) {
        ssize_t received;
        if (!wait_fd(fd, POLLIN, SPW_DEVICE_BENCH_TIMEOUT_MS)) {
            return -1;
        }
        received = recv(fd, frame, capacity, MSG_DONTWAIT);
        if (received >= 0) {
            return received;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        return -1;
    }
}
'''
new = r'''static ssize_t raw_receive_record(int fd, uint8_t* frame, size_t capacity) {
    for (;;) {
        const ssize_t received = recv(fd, frame, capacity, MSG_DONTWAIT);
        if (received >= 0) {
            return received;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!wait_fd(fd, POLLIN, SPW_DEVICE_BENCH_TIMEOUT_MS)) {
                return -1;
            }
            continue;
        }
        return -1;
    }
}
'''

count = text.count(old)
if count != 1:
    raise SystemExit(f"raw_receive_record: expected exactly one match, found {count}")
path.write_text(text.replace(old, new, 1))
print("Applied guarded DEVICE native-comparator fairness patch")
