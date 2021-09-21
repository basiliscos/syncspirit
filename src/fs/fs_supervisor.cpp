#include "fs_supervisor.h"
#include "../net/names.h"
#include "../net/hasher_proxy_actor.h"
#include "scan_actor.h"

using namespace syncspirit::fs;

fs_supervisor_t::fs_supervisor_t(config_t &cfg)
    : parent_t(cfg), fs_config{cfg.fs_config}, hasher_threads{cfg.hasher_threads} {
    log = utils::get_logger("fs.supervisor");
}

void fs_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("fs::supervisor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(net::names::coordinator, coordinator, true).link(false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &) { launch(); });
}

void fs_supervisor_t::launch() noexcept {
    auto &timeout = shutdown_timeout;
    auto hasher_addr = create_actor<net::hasher_proxy_actor_t>()
                           .hasher_threads(hasher_threads)
                           .name("fs::hasher_proxy")
                           .timeout(timeout)
                           .finish()
                           ->get_address();
    fs_actor = create_actor<scan_actor_t>().fs_config(fs_config).hasher_proxy(hasher_addr).timeout(timeout).finish();
}

void fs_supervisor_t::on_start() noexcept {
    log->trace("{}, on_start", identity);
    r::actor_base_t::on_start();
}
