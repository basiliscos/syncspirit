#include "governor_actor.h"
#include "../net/names.h"
#include "../utils/error_code.h"

using namespace syncspirit::daemon;

governor_actor_t::governor_actor_t(config_t &cfg) : r::actor_base_t{cfg}, commands{std::move(cfg.commands)} {
    log = utils::get_logger("daemon.governor_actor");
}

void governor_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("governor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                auto sup = supervisor->get_address();
                plugin->subscribe_actor(&governor_actor_t::on_model_update, sup);
                plugin->subscribe_actor(&governor_actor_t::on_io_error, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&governor_actor_t::on_model_response);
    });
}

void governor_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
    request<model::payload::model_request_t>(supervisor->get_address()).send(init_timeout);
}

void governor_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
}

void governor_actor_t::on_model_response(model::message::model_response_t &reply) noexcept {
    auto& ee = reply.payload.ee;
    if (ee) {
        LOG_ERROR(log, "{}, on_cluster_seed: {},", ee->message());
        return do_shutdown(ee);
    }
    LOG_TRACE(log, "{}, on_model_response", identity);
    cluster = std::move(reply.payload.res.cluster);
    process();
}


void governor_actor_t::on_model_update(model::message::forwarded_model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& payload = message.payload.message->payload;
    if (payload.custom == this) {
        process();
    }
}

void governor_actor_t::on_io_error(model::message::io_error_t& reply) noexcept {
    auto& errs = reply.payload.errors;
    LOG_TRACE(log, "{}, on_io_error, count = {}", identity, errs.size());
    for(auto& err: errs) {
        LOG_WARN(log, "{}, on_io_error (ignored) path: {}, problem: {}", identity, err.path, err.ec.message());
    }
}

void governor_actor_t::process() noexcept {
    LOG_DEBUG(log, "{}, process", identity);
NEXT:
    if (commands.empty()) {
        log->debug("{}, no commands left for processing", identity);
        return;
    }
    auto &cmd = commands.front();
    bool ok = cmd->execute(*this);
    commands.pop_front();
    if (!ok) {
        goto NEXT;
    }
}
