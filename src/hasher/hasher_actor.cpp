// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "hasher_actor.h"
#include "../utils/tls.h"
#include <fmt/core.h>

using namespace syncspirit::hasher;
static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;

hasher_actor_t::hasher_actor_t(config_t &cfg) : r::actor_base_t(cfg), index(cfg.index) {}

void hasher_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(fmt::format("hasher-{}", index), false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(identity, get_address()); });

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&hasher_actor_t::on_digest); });
}

void hasher_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start");
    r::actor_base_t::on_start();
}

void hasher_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish");
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void hasher_actor_t::on_digest(message::digest_t &req) noexcept {
    LOG_TRACE(log, "{}, on_digest");

    unsigned char digest[SZ];
    auto &data = req.payload.data;

    utils::digest(data.data(), data.size(), digest);
    req.payload.result = utils::bytes_t(digest, digest + SZ);
}
