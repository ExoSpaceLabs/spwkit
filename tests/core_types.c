// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stdint.h>

#ifdef NDEBUG
#error "SpWKit tests require assert() to remain enabled"
#endif

#include "spwkit/types.h"

int main(void)
{
    assert(SPWKIT_API_VERSION_MAJOR == SPWKIT_TEST_PROJECT_VERSION_MAJOR);
    assert(SPWKIT_API_VERSION_MINOR == SPWKIT_TEST_PROJECT_VERSION_MINOR);
    assert(SPWKIT_API_VERSION_PATCH == SPWKIT_TEST_PROJECT_VERSION_PATCH);

    assert(SPW_OK == 0);
    assert(SPW_ERR_INVALID_ARGUMENT < 0);
    assert(SPW_ERR_BACKEND < 0);

    assert(SPW_TIMEOUT_IMMEDIATE == 0u);
    assert(SPW_TIMEOUT_INFINITE == UINT64_MAX);

    assert(SPW_TERMINATOR_EOP != SPW_TERMINATOR_EEP);

    assert(SPW_LINK_ERROR_RESET == 0u);
    assert(SPW_LINK_ERROR_WAIT == 1u);
    assert(SPW_LINK_READY == 2u);
    assert(SPW_LINK_STARTED == 3u);
    assert(SPW_LINK_CONNECTING == 4u);
    assert(SPW_LINK_RUN == 5u);

    assert((SPW_CAP_ZERO_COPY & SPW_CAP_TIME_CODE) == 0u);
    assert((SPW_CAP_EEP | SPW_CAP_TIME_CODE) != SPW_CAP_NONE);

    {
        uint8_t storage[4] = {1u, 2u, 3u, 4u};
        spw_packet_t packet = {
            .data = storage,
            .length = 4u,
            .capacity = sizeof(storage),
            .terminator = SPW_TERMINATOR_EOP,
        };
        assert(packet.data == storage);
        assert(packet.length == packet.capacity);
        assert(packet.terminator == SPW_TERMINATOR_EOP);
    }

    {
        spw_time_code_t tc = {.time_count = 63u, .control_flags = 0u};
        assert(tc.time_count <= 63u);
        assert(tc.control_flags == 0u);
    }

    return 0;
}
