// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "hasher_actor.h"
#include "../utils/tls.h"
#include <fmt/core.h>
#include <zlib.h>

using namespace syncspirit::hasher;
static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;

hasher_actor_t::hasher_actor_t(config_t &cfg) : r::actor_base_t(cfg), index(cfg.index) {
    log = utils::get_logger("hasher.actor");
}

void hasher_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>(
        [&](auto &p) { p.set_identity(fmt::format("hasher-{}", index), false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(identity, get_address()); });

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&hasher_actor_t::on_digest);
        p.subscribe_actor(&hasher_actor_t::on_validation);
    });
}

void hasher_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void hasher_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void hasher_actor_t::on_digest(message::digest_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_digest", identity);

    char digest[SZ];
    auto &data = req.payload.request_payload.data;

    utils::digest(data.data(), data.length(), digest);

    // alder32
    auto weak_hash = adler32(0L, Z_NULL, 0);
    weak_hash = adler32(weak_hash, (const unsigned char *)data.data(), data.length());

    reply_to(req, std::string(digest, SZ), static_cast<uint32_t>(weak_hash));
}

void hasher_actor_t::on_validation(message::validation_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_validation", identity);
    auto &payload = *req.payload.request_payload;

    char digest[SZ];
    auto &data = payload.data;

    utils::digest(data.data(), data.length(), digest);
    bool eq = payload.hash == std::string_view(digest, SZ);
    reply_to(req, eq);
}
