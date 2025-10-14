// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "hasher_proxy_actor.h"
#include <utility>

using namespace syncspirit::hasher;

namespace {
namespace resource {
r::plugin::resource_id_t hash = 0;
} // namespace resource
} // namespace

hasher_proxy_actor_t::hasher_proxy_actor_t(config_t &config) : r::actor_base_t(config) {
    hashers.resize(config.hasher_threads);
    hasher_scores.resize(config.hasher_threads);
    hasher_threads = config.hasher_threads;
    name = config.name;
}

void hasher_proxy_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(name, false);
        log = utils::get_logger(identity);
        reply_addr = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(name, get_address());
        for (size_t i = 0; i < hashers.size(); ++i) {
            auto name = fmt::format("hasher-{}", i + 1);
            p.discover_name(name, hashers[i], true).link(false);
        }
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&hasher_proxy_actor_t::on_validation_request);
        p.subscribe_actor(&hasher_proxy_actor_t::on_validation_response, reply_addr);
        p.subscribe_actor(&hasher_proxy_actor_t::on_digest_request);
        p.subscribe_actor(&hasher_proxy_actor_t::on_digest_response, reply_addr);
    });
}

void hasher_proxy_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "on_start");
}

void hasher_proxy_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
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

void hasher_proxy_actor_t::on_validation_request(hasher::message::validation_t &req) noexcept {
    LOG_TRACE(log, "on_validation_request");
    auto hasher = find_next_hasher();
    auto &p = req.payload;
    p.back_addr = std::exchange(req.next_route, reply_addr);
    p.hasher_addr = req.address = hasher;
    supervisor->put(&req);
}

void hasher_proxy_actor_t::on_validation_response(hasher::message::validation_t &res) noexcept {
    LOG_TRACE(log, "on_validation_response");
    auto &p = res.payload;
    res.next_route = p.back_addr;
    free_hasher(p.hasher_addr);
}

void hasher_proxy_actor_t::on_digest_request(hasher::message::digest_t &req) noexcept {
    LOG_TRACE(log, "on_digest_request");
    auto hasher = find_next_hasher();
    auto &p = req.payload;
    p.back_addr = std::exchange(req.next_route, reply_addr);
    p.hasher_addr = req.address = hasher;
    supervisor->put(&req);
}

void hasher_proxy_actor_t::on_digest_response(hasher::message::digest_t &res) noexcept {
    LOG_TRACE(log, "on_digest_response");
    auto &p = res.payload;
    res.next_route = p.back_addr;
    free_hasher(p.hasher_addr);
}
