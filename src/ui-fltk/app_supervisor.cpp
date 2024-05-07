#include "app_supervisor.h"
#include "net/names.h"

using namespace syncspirit::fltk;

namespace {
namespace resource {
r::plugin::resource_id_t model = 0;
}
} // namespace

app_supervisor_t::app_supervisor_t(config_t& config):parent_t(config),
    dist_sink(std::move(config.dist_sink)) {
}

auto app_supervisor_t::get_dist_sink() -> utils::dist_sink_t& {
    return dist_sink;
}

auto app_supervisor_t::get_cluster() -> model::cluster_ptr_t& {
    return cluster;
}

void app_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept{
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("fltk", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&app_supervisor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&app_supervisor_t::on_block_update, coordinator);
                request<model::payload::model_request_t>(coordinator).send(init_timeout);
                resources->acquire(resource::model);
            }
        });
    });

    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
            p.subscribe_actor(&app_supervisor_t::on_model_response);
        },
        r::plugin::config_phase_t::PREINIT);
}

void app_supervisor_t::on_model_response(model::message::model_response_t &res) noexcept {
    LOG_TRACE(log, "on_model_response");
    resources->release(resource::model);
    auto ee = res.payload.ee;
    if (ee) {
        LOG_ERROR(log, "cannot get model: {}", ee->message());
        return do_shutdown(ee);
    }
    cluster = std::move(res.payload.res.cluster);
    for(auto listener: load_listeners) {
        (*listener)(res);
    }
}

void app_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
}

void app_supervisor_t::on_block_update(model::message::block_update_t &message) noexcept {
}

void app_supervisor_t::add(model_load_listener_t* listener) noexcept {
    load_listeners.insert(listener);
}

void app_supervisor_t::remove(model_load_listener_t* listener) noexcept {
    load_listeners.erase(listener);
}
