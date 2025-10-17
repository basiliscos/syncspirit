// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "managed_hasher.h"
#include "utils/tls.h"

namespace syncspirit::test {

managed_hasher_t::managed_hasher_t(config_t &cfg)
    : r::actor_base_t{cfg}, index{cfg.index}, auto_reply{cfg.auto_reply} {}

void managed_hasher_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(fmt::format("hasher-{}", 1), false);
        log = utils::get_logger(fmt::format("managed-hasher-{}", 1));
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(identity, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&managed_hasher_t::on_validation);
        p.subscribe_actor(&managed_hasher_t::on_digest);
    });
}

void managed_hasher_t::on_validation(validation_request_t &req) noexcept {
    validation_queue.emplace_back(&req);
    if (auto_reply) {
        process_requests();
    }
}

void managed_hasher_t::on_digest(digest_request_t &req) noexcept {
    digest_queue.emplace_back(&req);
    if (auto_reply) {
        process_requests();
    }
}

void managed_hasher_t::process_requests() noexcept {
    static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;
    LOG_TRACE(log, "process_requests");
    while (!validation_queue.empty()) {
        auto req = validation_queue.front();
        validation_queue.pop_front();
        auto &payload = req->payload;

        unsigned char digest[SZ];
        auto &data = payload.data;

        utils::digest(data.data(), data.size(), digest);
        bool eq = payload.hash == utils::bytes_view_t(digest, SZ);
        if (eq) {
            payload.result = hasher::outcome::success();
        }
        redirect(req, std::exchange(req->next_route, {}));
    }
    while (!digest_queue.empty()) {
        auto req = digest_queue.front();
        digest_queue.pop_front();

        auto &payload = req->payload;
        auto &data = payload.data;
        unsigned char digest[SZ];
        utils::digest(data.data(), data.size(), digest);

        req->payload.result = utils::bytes_t(digest, digest + SZ);
        redirect(req, std::exchange(req->next_route, {}));
    }
}

} // namespace syncspirit::test
