#include "profiling/profile.h"

#if SPWKIT_ENABLE_PROFILING

volatile spw_profile_sample_t spw_profile_state = {0u, 0u, 0u, 0u};

void spw_profile_prepare(void) {
    spw_profile_counter_prepare();
    spw_profile_reset();
}

void spw_profile_reset(void) {
    spw_profile_state.start = 0u;
    spw_profile_state.end = 0u;
    spw_profile_state.delta = 0u;
    spw_profile_state.sequence = 0u;
}

const volatile spw_profile_sample_t* spw_profile_last_sample(void) {
    return &spw_profile_state;
}

#endif