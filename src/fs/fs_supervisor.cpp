#include "fs_supervisor.h"
#include "fs_actor.h"
#include "../net/names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::fs;

fs_supervisor_t::fs_supervisor_t(config_t &cfg) : parent_t(cfg), fs_config{cfg.fs_config} {}
void fs_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        // not used, but for shutdown only
        p.discover_name(net::names::coordinator, coordinator, true).link();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &) { launch(); });
}

void fs_supervisor_t::launch() noexcept {
    auto &timeout = shutdown_timeout;
    create_actor<fs_actor_t>().fs_config(fs_config).timeout(timeout).finish();
}

void fs_supervisor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    r::actor_base_t::on_start();
}
