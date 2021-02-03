#include "folder_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

folder_actor_t::folder_actor_t(config_t &config)
    : r::actor_base_t{config}, folder{config.folder}, device{config.device} {}

void folder_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string id = "folder/";
        id += folder->id;
        p.set_identity(id, false);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(names::db, db, false).link(true); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&folder_actor_t::on_start_sync);
        p.subscribe_actor(&folder_actor_t::on_stop_sync);
    });
}

void folder_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    spdlog::trace("{}, on_start", identity);
}

void folder_actor_t::on_start_sync(message::start_sync_t &message) noexcept {
    spdlog::trace("{}, on_start_sync", identity);
}

void folder_actor_t::on_stop_sync(message::stop_sync_t &message) noexcept {
    spdlog::trace("{}, on_stop_sync", identity);
}
