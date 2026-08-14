// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

bool track_allocations = false;
std::size_t allocation_count = 0u;

void note_allocation() noexcept {
    if (track_allocations) {
        ++allocation_count;
    }
}

[[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "[noheap][FAIL] %s\n", message);
    std::abort();
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void* allocate_unaligned(std::size_t size) {
    note_allocation();
    if (void* pointer = std::malloc(size == 0u ? 1u : size)) {
        return pointer;
    }
    std::abort();
}

void* allocate_aligned(std::size_t size, std::size_t alignment) {
    note_allocation();
    const std::size_t nonzero_size = size == 0u ? alignment : size;
    const std::size_t padded_size =
        ((nonzero_size + alignment - 1u) / alignment) * alignment;
#if defined(_MSC_VER)
    if (void* pointer = _aligned_malloc(padded_size, alignment)) {
        return pointer;
    }
#else
    if (void* pointer = std::aligned_alloc(alignment, padded_size)) {
        return pointer;
    }
#endif
    std::abort();
}

} // namespace

void* operator new(std::size_t size) { return allocate_unaligned(size); }
void* operator new[](std::size_t size) { return allocate_unaligned(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return allocate_unaligned(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate_unaligned(size);
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }

void* operator new(std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t size,
                   std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size,
                     std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    return allocate_aligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_MSC_VER)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
    operator delete(pointer, alignment);
}
void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    operator delete(pointer, alignment);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    operator delete(pointer, alignment);
}

int main() {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

    spw_port_workspace_requirements_t requirements{};
    require(spw_port_workspace_requirements(&config, &requirements) == SPW_OK,
            "workspace requirement query failed");
    require(requirements.size > 0u, "workspace size is zero");
    require(requirements.alignment > 0u, "workspace alignment is zero");

    alignas(std::max_align_t) static std::uint8_t workspace[64u * 1024u];
    require(requirements.size <= sizeof(workspace),
            "test workspace is smaller than advertised requirements");
    require(reinterpret_cast<std::uintptr_t>(workspace) % requirements.alignment == 0u,
            "test workspace does not meet advertised alignment");

    spw_port_t* port = nullptr;
    require(spw_port_open(&config, &port) == SPW_ERR_UNSUPPORTED,
            "heap-backed open must be disabled in no-heap profile");
    require(port == nullptr, "disabled heap open returned a port");

    require(spw_port_open_in_place(&config,
                                   workspace,
                                   requirements.size - 1u,
                                   &port) == SPW_ERR_BUFFER_TOO_SMALL,
            "undersized workspace was accepted");
    require(port == nullptr, "undersized workspace returned a port");

    allocation_count = 0u;
    track_allocations = true;

    require(spw_port_open_in_place(&config, workspace, sizeof(workspace), &port) == SPW_OK,
            "in-place open failed");
    require(port != nullptr, "in-place open returned null port");
    require(spw_port_start(port) == SPW_OK, "start failed");

    std::uint8_t tx_data[] = {0x53u, 0x50u, 0x57u, 0x06u};
    spw_packet_t tx{tx_data, sizeof(tx_data), sizeof(tx_data), SPW_TERMINATOR_EEP};
    require(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK,
            "send failed");

    std::uint8_t rx_data[sizeof(tx_data)]{};
    spw_packet_t rx{rx_data, 0u, sizeof(rx_data), SPW_TERMINATOR_EOP};
    require(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK,
            "receive failed");
    require(rx.length == sizeof(tx_data), "received length mismatch");
    require(rx.terminator == SPW_TERMINATOR_EEP, "received terminator mismatch");
    for (std::size_t i = 0; i < sizeof(tx_data); ++i) {
        require(rx_data[i] == tx_data[i], "received payload mismatch");
    }

    spw_time_code_t tx_time{23u, 0u};
    spw_time_code_t rx_time{};
    require(spw_port_send_time_code(port, &tx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK,
            "time-code send failed");
    require(spw_port_receive_time_code(port, &rx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK,
            "time-code receive failed");
    require(rx_time.time_count == tx_time.time_count, "time-code mismatch");

    spw_statistics_t statistics{};
    require(spw_port_get_statistics(port, &statistics) == SPW_OK,
            "statistics query failed");
    require(statistics.tx_packets == 1u && statistics.rx_packets == 1u,
            "packet statistics mismatch");

    require(spw_port_close(port) == SPW_OK, "close failed");
    port = nullptr;

    // Reuse the exact same caller-owned storage after close.
    require(spw_port_open_in_place(&config, workspace, sizeof(workspace), &port) == SPW_OK,
            "workspace reuse failed");
    require(spw_port_start(port) == SPW_OK, "start after reuse failed");
    require(spw_port_close(port) == SPW_OK, "close after reuse failed");

    track_allocations = false;

    require(allocation_count == 0u,
            "mandatory in-place core path performed dynamic allocation");

    std::printf("[noheap] allocation-free loopback path verified; workspace=%zu alignment=%zu\n",
                requirements.size,
                requirements.alignment);
    return 0;
}
