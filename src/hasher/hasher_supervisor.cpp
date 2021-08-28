#include "hasher_supervisor.h"
#include "hasher_actor.h"
#include "../net/names.h"

using namespace syncspirit::hasher;

hasher_supervisor_t::hasher_supervisor_t(config_t &config) : parent_t(config), index{config.index} {
    log = utils::get_logger("hasher.supervisor");
}

void hasher_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("hasher::supervisor", true); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(net::names::coordinator, coordinator, true).link(false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &) { launch(); });
}

void hasher_supervisor_t::launch() noexcept {
    create_actor<hasher_actor_t>().index(index).timeout(shutdown_timeout).finish();
}

void hasher_supervisor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}
