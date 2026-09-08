#include <assert.h>
#include <stddef.h>

#include "profiling/profile.h"

int main(void) {
    const volatile spw_profile_sample_t* sample = NULL;

    spw_profile_prepare();

    SPW_PROFILE_TX_API_ENTRY();
    SPW_PROFILE_TX_BACKEND_ENTRY();
    SPW_PROFILE_TX_PROVIDER_ENTRY();
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();
    SPW_PROFILE_RX_PROVIDER_BOUNDARY();
    SPW_PROFILE_RX_PROVIDER_RETURN();
    SPW_PROFILE_RX_BACKEND_RETURN();
    SPW_PROFILE_RX_API_RETURN();

    sample = spw_profile_last_sample();
    assert(sample != NULL);
    assert(sample->sequence == 1u);
    assert(sample->end >= sample->start);
    assert(sample->delta == (sample->end - sample->start));
    assert(spw_profile_counter_kind() != NULL);

    return 0;
}