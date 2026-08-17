from pathlib import Path

p = Path("src/vspwd/server.c")
text = p.read_text()
old = '''    if (state != server->bridge.state) {
        server->bridge.state = state;
        vspwd_update_states(server);
    }
}'''
new = '''    if (state != server->bridge.state) {
        vspwd_port_t* bridge_port = &server->ports[server->bridge.port_id];
        server->bridge.state = state;
        if (bridge_port->state != state) {
            if (state == SPW_LINK_ERROR_WAIT) {
                ++bridge_port->statistics.link_errors;
            }
            bridge_port->state = state;
            vspwd_mark_port_changed(server, server->bridge.port_id);
        }
        /* Publish the bridge endpoint first so the paired local port is
         * recalculated from the new remote state in this same pass. */
        vspwd_update_states(server);
    }
}'''
if old not in text:
    raise SystemExit("bridge refresh marker missing")
p.write_text(text.replace(old, new, 1))
print("bridge state propagation fix applied")
