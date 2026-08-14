// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spwkit/spwkit.h>

namespace spwkit::test {

/**
 * Backend-independent fixture consumed by the shared public-API contract suite.
 *
 * A backend adapter owns any configuration and port handles required to expose
 * two logical contract endpoints. Backends that naturally loop data back may
 * return the same handle for endpoint_a() and endpoint_b().
 */
class BackendContractFixture {
public:
    virtual ~BackendContractFixture() = default;

    virtual const char* name() const noexcept = 0;
    virtual spw_port_t* endpoint_a() const noexcept = 0;
    virtual spw_port_t* endpoint_b() const noexcept = 0;

    /** Put both logical endpoints into a usable RUN state. */
    virtual void start_link() = 0;

    /** Stop the logical link without destroying the fixture. */
    virtual void stop_link() = 0;

    /** Clear pending traffic and return both endpoints to ERROR_RESET. */
    virtual void reset_link() = 0;

    /**
     * Optional zero-copy contract hook.
     *
     * The shared suite calls this only when both endpoints advertise
     * SPW_CAP_ZERO_COPY. Backends advertising that capability must override
     * this hook once the portable zero-copy API is defined by #10.
     */
    virtual bool has_zero_copy_contract() const noexcept { return false; }
    virtual void run_zero_copy_contract() {}
};

/** Run the complete shared copied-I/O contract for one backend fixture. */
int run_backend_contract(BackendContractFixture& fixture);

} // namespace spwkit::test
