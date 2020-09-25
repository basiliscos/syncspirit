#include "resolver_actor.h"
#include "spdlog/spdlog.h"
#include "names.h"


using namespace syncspirit::net;
using namespace syncspirit::utils;

namespace {
namespace resource {
    r::plugin::resource_id_t io = 0;
    r::plugin::resource_id_t timer = 1;
}
}

resolver_actor_t::resolver_actor_t(resolver_actor_t::config_t &config)
    : r::actor_base_t{config}, io_timeout{config.resolve_timeout},
      strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()}, backend{strand.context()},
      timer(strand.context()) {}

void resolver_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&resolver_actor_t::on_request); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(names::resolver, get_address()); });
}

bool resolver_actor_t::maybe_shutdown() noexcept {
    if (state == r::state_t::SHUTTING_DOWN) {
        auto ec = r::make_error_code(r::error_code_t::actor_not_linkable);
        for (auto &req : queue) {
            reply_with_error(*req, ec);
        }
        queue.clear();

        if (resources->has(resource::io)) {
            backend.cancel();
        }
        if (resources->has(resource::timer)) {
            backend.cancel();
        }
        return true;
    }
    return false;
}

bool resolver_actor_t::cancel_timer() noexcept {
    sys::error_code ec;
    if (resources->has(resource::timer)) {
        timer.cancel(ec);
        if (ec) {
            get_supervisor().do_shutdown();
        }
    }
    return (bool)ec;
}

void resolver_actor_t::on_request(message::resolve_request_t &req) noexcept {
    if (state == r::state_t::SHUTTING_DOWN) {
        auto ec = r::make_error_code(r::error_code_t::actor_not_linkable);
        reply_with_error(req, ec);
        return;
    }

    queue.emplace_back(&req);
    if (!resources->has(resource::io))
        process();
}

void resolver_actor_t::mass_reply(const endpoint_t &endpoint, const resolve_results_t &results) noexcept {
    reply(endpoint, [&](auto &message) { reply_to(message, results); });
}

void resolver_actor_t::mass_reply(const endpoint_t &endpoint, const std::error_code &ec) noexcept {
    reply(endpoint, [&](auto &message) { reply_with_error(message, ec); });
}

void resolver_actor_t::process() noexcept {
    if (queue.empty())
        return;
    auto queue_it = queue.begin();
    auto& payload = (*queue_it)->payload.request_payload;
    auto endpoint = endpoint_t{payload->host, payload->port};
    auto cache_it = cache.find(endpoint);
    if (cache_it != cache.end()) {
        mass_reply(endpoint, cache_it->second);
    }
    if (queue.empty())
        return;
    resolve_start(*queue_it);
}

void resolver_actor_t::resolve_start(request_ptr_t &req) noexcept {
    if (resources->has_any())
        return;
    if (maybe_shutdown())
        return;
    if (queue.empty())
        return;

    auto &payload = req->payload.request_payload;
    auto fwd_resolver =
        ra::forwarder_t(*this, &resolver_actor_t::on_resolve, &resolver_actor_t::on_resolve_error);
    backend.async_resolve(payload->host, payload->port, std::move(fwd_resolver));
    resources->acquire(resource::io);

    timer.expires_from_now(io_timeout);
    auto fwd_timer =
        ra::forwarder_t(*this, &resolver_actor_t::on_timer_trigger, &resolver_actor_t::on_timer_error);
    timer.async_wait(std::move(fwd_timer));
    resources->acquire(resource::timer);
}

void resolver_actor_t::on_resolve(resolve_results_t results) noexcept {
    resources->release(resource::io);
    if (!queue.empty()) {
        auto &payload = queue.front()->payload.request_payload;;
        auto endpoint = endpoint_t{payload->host, payload->port};
        auto pair = cache.emplace(endpoint, results);
        auto &it = pair.first;
        mass_reply(it->first, it->second);
    }
    cancel_timer();
}

void resolver_actor_t::on_resolve_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    auto &payload = queue.front()->payload.request_payload;
    auto endpoint = endpoint_t{payload->host, payload->port};
    mass_reply(endpoint, ec);
    cancel_timer();
}

void resolver_actor_t::on_timer_error(const sys::error_code &ec) noexcept {
    resources->release(resource::timer);
    if (ec != asio::error::operation_aborted) {
        return get_supervisor().do_shutdown();
    }
    if (!maybe_shutdown()) {
        process();
    }
}

void resolver_actor_t::on_timer_trigger() noexcept {
    resources->release(resource::timer);
    if (!queue.empty()) {
        // could be actually some other ec...
        auto ec = r::make_error_code(r::error_code_t::request_timeout);
        auto &payload = queue.front()->payload.request_payload;
        auto endpoint = endpoint_t{payload->host, payload->port};
        mass_reply(endpoint, ec);
    }
    if (!maybe_shutdown()) {
        process();
    }
}

// cancel any pending async ops
void resolver_actor_t::shutdown_start() noexcept  {
    r::actor_base_t::shutdown_start();
    maybe_shutdown();
}
