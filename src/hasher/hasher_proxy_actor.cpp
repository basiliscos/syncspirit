// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "hasher_proxy_actor.h"
#include "../utils/error_code.h"
#include <fmt/fmt.h>
#include <numeric>

using namespace syncspirit::hasher;

namespace {
namespace resource {
r::plugin::resource_id_t hash = 0;
} // namespace resource
} // namespace

hasher_proxy_actor_t::hasher_proxy_actor_t(config_t &config) : r::actor_base_t(config) {
    log = utils::get_logger("net.hasher_proxy_actor");
    hashers.resize(config.hasher_threads);
    hasher_scores.resize(config.hasher_threads);
    hasher_threads = config.hasher_threads;
    name = config.name;
}

void hasher_proxy_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(name, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(name, get_address());
        for (size_t i = 0; i < hashers.size(); ++i) {
            auto name = fmt::format("hasher-{}", i + 1);
            p.discover_name(name, hashers[i], true).link(false);
        }
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&hasher_proxy_actor_t::on_validation_request);
        p.subscribe_actor(&hasher_proxy_actor_t::on_validation_response);
        p.subscribe_actor(&hasher_proxy_actor_t::on_digest_request);
        p.subscribe_actor(&hasher_proxy_actor_t::on_digest_response);
    });
}

void hasher_proxy_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "{}, on_start", identity);
}

void hasher_proxy_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    r::actor_base_t::shutdown_finish();
}

r::address_ptr_t hasher_proxy_actor_t::find_next_hasher() noexcept {
    uint32_t score = std::numeric_limits<uint32_t>::max();
    uint32_t min = 0;
    for (uint32_t j = 0; j < hasher_threads; ++j) {
        uint32_t i = (index + j) % hasher_threads;
        if (hasher_scores[i] == 0) {
            min = i;
            break;
        }
        if (hasher_scores[i] < score) {
            score = hasher_scores[i];
            min = i;
        }
    }
    ++hasher_scores[min];
    index = (index + 1) % hasher_threads;
    resources->acquire(resource::hash);
    return hashers[min];
}

void hasher_proxy_actor_t::free_hasher(r::address_ptr_t &addr) noexcept {
    resources->release(resource::hash);
    for (uint32_t i = 0; i < hashers.size(); ++i) {
        if (hashers[i] == addr) {
            --hasher_scores[i];
            return;
        }
    }
    assert(0 && "should not happen");
}

void hasher_proxy_actor_t::on_validation_request(hasher::message::validation_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_validation_request", identity);
    auto hasher = find_next_hasher();
    auto &p = *req.payload.request_payload;
    request<hasher::payload::validation_request_t>(hasher, p.data, p.hash, &req).send(init_timeout);
}

void hasher_proxy_actor_t::on_validation_response(hasher::message::validation_response_t &res) noexcept {
    using request_t = hasher::message::validation_request_t;
    LOG_TRACE(log, "{}, on_validation_response", identity);
    auto req = (request_t *)res.payload.req->payload.request_payload->custom.get();
    auto &payload = res.payload;
    auto &ee = payload.ee;
    if (ee) {
        reply_with_error(*req, std::move(ee));
    } else {
        reply_to(*req, payload.res.valid);
    }
    free_hasher(payload.req->address);
}

void hasher_proxy_actor_t::on_digest_request(hasher::message::digest_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_digest_request", identity);
    auto hasher = find_next_hasher();
    auto &p = req.payload.request_payload;
    request<hasher::payload::digest_request_t>(hasher, p.data, p.block_index, &req).send(init_timeout);
}

void hasher_proxy_actor_t::on_digest_response(hasher::message::digest_response_t &res) noexcept {
    LOG_TRACE(log, "{}, on_digest_response", identity);
    using request_t = hasher::message::digest_request_t;
    auto req = (request_t *)res.payload.req->payload.request_payload.custom.get();
    auto &payload = res.payload;
    auto &ee = payload.ee;
    if (ee) {
        reply_with_error(*req, std::move(ee));
    } else {
        reply_to(*req, payload.res.digest, payload.res.weak);
    }
    free_hasher(payload.req->address);
}
