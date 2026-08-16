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
     * Timeout used by ordinary successful contract transfers.
     *
     * Local backends can retain the immediate default. Distributed backends may
     * use a finite budget because one public receive call can need to service
     * multiple transport fragments/control datagrams before one logical
     * SpaceWire event is complete. The timeout is a fixture/environment
     * property, not a backend-name conditional in the shared assertions.
     */
    virtual spw_timeout_us_t transfer_timeout_us() const noexcept {
        return SPW_TIMEOUT_IMMEDIATE;
    }

    /**
     * Whether advertised queue depths are strict public non-blocking acceptance
     * limits that can be validated with SPW_TIMEOUT_IMMEDIATE.
     *
     * Some hosted service backends report bounded transport/service capacity but
     * synchronously confirm each public send across a process boundary. Those
     * depths remain useful diagnostics, but they are not an immediate-send
     * guarantee and therefore must not be tested as one.
     */
    virtual bool has_strict_bounded_queue_contract() const noexcept {
        return true;
    }

    /**
     * Optional zero-copy contract hook.
     *
     * The shared suite calls this only when both endpoints advertise
     * SPW_CAP_ZERO_COPY. Backends advertising that capability must override
     * this hook.
     */
    virtual bool has_zero_copy_contract() const noexcept { return false; }
    virtual void run_zero_copy_contract() {}
};

/**
 * Reusable extension contract for peer-oriented/distributed backends.
 *
 * Implementations perform peer disconnect/restart using only their normal
 * public configuration/lifecycle operations. The assertions themselves remain
 * backend-independent and use only the public SpWKit API.
 */
class DistributedBackendContractFixture : public BackendContractFixture {
public:
    /** Remove endpoint B so endpoint A can observe peer loss. */
    virtual void disconnect_endpoint_b() = 0;

    /** Recreate and start endpoint B with a new transport/session incarnation. */
    virtual void restart_endpoint_b() = 0;

    /** Maximum public-observation budget for loss/recovery state transitions. */
    virtual spw_timeout_us_t link_transition_timeout_us() const noexcept = 0;

    /**
     * Budget for confirming a send failure after peer loss is already visible.
     * Local/nonblocking distributed backends retain the immediate default;
     * service-backed transports may need a finite request/response round trip.
     */
    virtual spw_timeout_us_t peer_loss_send_timeout_us() const noexcept {
        return SPW_TIMEOUT_IMMEDIATE;
    }
};

/** Run the complete shared copied-I/O contract for one backend fixture. */
int run_backend_contract(BackendContractFixture& fixture);

/** Run reusable peer-loss/restart assertions for a distributed fixture. */
int run_distributed_backend_contract(DistributedBackendContractFixture& fixture);

} // namespace spwkit::test
