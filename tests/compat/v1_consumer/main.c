#include <stddef.h>
#include <stdint.h>

#include <spwkit/spwkit.h>

/*
 * Candidate frozen v1 consumer.
 *
 * This translation unit intentionally uses representative installed-header
 * surface without reaching into src/. At v1.0.0 it is frozen; later 1.x CI
 * must continue compiling/linking it unchanged.
 */
int spwkit_v1_consumer_surface(spw_port_t* port, spw_buffer_t* buffer)
{
    spw_result_t rc = SPW_OK;
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    spw_packet_t packet = {NULL, 0u, 0u, SPW_TERMINATOR_EOP};
    spw_buffer_view_t view = {NULL, 0u, 0u, SPW_TERMINATOR_EOP};
    spw_capabilities_t capabilities = {0u, 0u, 0u, 0u, 0u};
    spw_statistics_t statistics = {0u};
    spw_fault_statistics_t fault_statistics = {0u};
    spw_time_code_t time_code = {0u, 0u};
    spw_ready_events_t ready = SPW_READY_NONE;
    spw_link_state_t link_state = SPW_LINK_ERROR_RESET;
    spw_port_t* opened = NULL;
    spw_buffer_t* owned = buffer;

    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);

    rc |= spw_port_workspace_requirements(&config, &requirements);
    rc |= spw_port_open_in_place(&config, NULL, 0u, &opened);
    rc |= spw_port_open(&config, &opened);
    rc |= spw_port_start(port);
    rc |= spw_port_stop(port);
    rc |= spw_port_reset(port);
    rc |= spw_port_get_link_state(port, &link_state);
    rc |= spw_port_get_capabilities(port, &capabilities);
    rc |= spw_port_wait(port, SPW_READY_ALL, SPW_TIMEOUT_IMMEDIATE, &ready);
    rc |= spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE);
    rc |= spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE);
    rc |= spw_port_send_time_code(port, &time_code, SPW_TIMEOUT_IMMEDIATE);
    rc |= spw_port_receive_time_code(port, &time_code, SPW_TIMEOUT_IMMEDIATE);
    rc |= spw_port_get_statistics(port, &statistics);
    rc |= spw_port_clear_statistics(port);
    rc |= spw_port_get_fault_statistics(port, &fault_statistics);
    rc |= spw_port_clear_fault_statistics(port);

    rc |= spw_buffer_get_view(buffer, &view);
    rc |= spw_buffer_set_packet(buffer, 0u, SPW_TERMINATOR_EOP);
    rc |= spw_port_acquire_tx_buffer(port, 1u, SPW_TIMEOUT_IMMEDIATE, &owned);
    rc |= spw_port_submit_tx_buffer(port, &owned, SPW_TIMEOUT_IMMEDIATE);
    rc |= spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &owned);
    rc |= spw_port_release_tx_buffer(port, &owned);
    rc |= spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &owned);
    rc |= spw_port_release_rx_buffer(port, &owned);
    rc |= spw_port_close(port);

    {
        spw_driver_ops_t driver_ops = SPW_DRIVER_OPS_INITIALIZER;
        spw_driver_config_t driver_config =
            SPW_DRIVER_CONFIG_INITIALIZER(&driver_ops, NULL);
        config.backend = SPW_BACKEND_DRIVER;
        config.backend_config = &driver_config;
        config.backend_config_size = sizeof(driver_config);
        rc |= (spw_result_t)(config.backend_config_size == 0u);
    }

    return (int)rc;
}

int main(void)
{
    return 0;
}
