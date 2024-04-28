// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "resolver_actor.h"
#include "names.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;

namespace {
namespace resource {
r::plugin::resource_id_t io = 0;
r::plugin::resource_id_t timer = 1;
} // namespace resource
} // namespace

resolver_actor_t::resolver_actor_t(resolver_actor_t::config_t &config)
    : r::actor_base_t{config}, io_timeout{config.resolve_timeout},
      strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()}, backend{strand.context()} {}

void resolver_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::resolver, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(names::resolver, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&resolver_actor_t::on_request);
        p.subscribe_actor(&resolver_actor_t::on_cancel);
    });
}

void resolver_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void resolver_actor_t::on_request(message::resolve_request_t &req) noexcept {
    queue.emplace_back(&req);
    process();
}

void resolver_actor_t::on_cancel(message::resolve_cancel_t &message) noexcept {
    if (queue.empty())
        return;
    auto &request_id = message.payload.id;
    auto &source = message.payload.source;
    auto matches = [&](auto &it) {
        auto &payload = it->payload;
        return payload.id == request_id && payload.origin == source;
    };
    if (matches(queue.front())) {
        assert(resources->has(resource::io));
        backend.cancel();
        cancel_timer();
    } else if (queue.size() > 1) {
        auto it = queue.begin();
        std::advance(it, 1);
        for (; it != queue.end(); ++it) {
            if (matches(*it)) {
                auto ec = r::make_error_code(r::error_code_t::cancelled);
                reply_with_error(**it, make_error(ec));
                queue.erase(it);
                return;
            }
        }
    }
}

void resolver_actor_t::mass_reply(const endpoint_t &endpoint, const resolve_results_t &results) noexcept {
    reply(endpoint, [&](auto &message) { reply_to(message, results); });
}

void resolver_actor_t::mass_reply(const endpoint_t &endpoint, const std::error_code &ec) noexcept {
    reply(endpoint, [&](auto &message) { reply_with_error(message, make_error(ec)); });
}

void resolver_actor_t::process() noexcept {
    if (resources->has(resource::io)) {
        return;
    }
    if (queue.empty())
        return;
    auto queue_it = queue.begin();
    auto &payload = (*queue_it)->payload.request_payload;
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
    if (queue.empty())
        return;

    auto &payload = req->payload.request_payload;
    auto fwd_resolver = ra::forwarder_t(*this, &resolver_actor_t::on_resolve, &resolver_actor_t::on_resolve_error);
    backend.async_resolve(payload->host, payload->port, std::move(fwd_resolver));
    resources->acquire(resource::io);

    timer_id = start_timer(io_timeout, *this, &resolver_actor_t::on_timer);
    resources->acquire(resource::timer);
}

void resolver_actor_t::on_resolve(resolve_results_t results) noexcept {
    resources->release(resource::io);
    if (!queue.empty()) {
        auto &payload = queue.front()->payload.request_payload;
        auto endpoint = endpoint_t{payload->host, payload->port};
        auto pair = cache.emplace(endpoint, results);
        auto &it = pair.first;
        mass_reply(it->first, it->second);
    }
    cancel_timer();
    process();
}

void resolver_actor_t::on_resolve_error(const sys::error_code &ec) noexcept {
    resources->release(resource::io);
    if (ec == asio::error::operation_aborted) {
        if (resources->has(resource::io)) {
            auto ec = r::make_error_code(r::error_code_t::cancelled);
            reply_with_error(*queue.front(), make_error(ec));
            queue.pop_front();
        }
    } else {
        if (!queue.empty()) {
            auto &payload = *queue.front()->payload.request_payload;
            auto endpoint = endpoint_t{payload.host, payload.port};
            mass_reply(endpoint, ec);
        }
    }

    cancel_timer();
    process();
}

void resolver_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    if (cancelled) {
        if (resources->has(resource::io)) {
            auto ec = r::make_error_code(r::error_code_t::cancelled);
            reply_with_error(*queue.front(), make_error(ec));
            queue.pop_front();
        }
    } else {
        if (!queue.empty()) {
            // could be actually some other ec...
            auto ec = r::make_error_code(r::error_code_t::request_timeout);
            auto &payload = queue.front()->payload.request_payload;
            auto endpoint = endpoint_t{payload->host, payload->port};
            mass_reply(endpoint, ec);
        }
    }
    timer_id.reset();
    process();
}

void resolver_actor_t::cancel_timer() noexcept {
    if (timer_id) {
        r::actor_base_t::cancel_timer(*timer_id);
    }
}
