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
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&managed_hasher_t::on_validation); });
}

void managed_hasher_t::on_validation(validation_request_t &req) noexcept {
    queue.emplace_back(&req);
    if (auto_reply) {
        process_requests();
    }
}

void managed_hasher_t::process_requests() noexcept {
    static const constexpr size_t SZ = SHA256_DIGEST_LENGTH;

    LOG_TRACE(log, "{}, process_requests", identity);
    while (!queue.empty()) {
        auto req = queue.front();
        queue.pop_front();
        auto &payload = *req->payload.request_payload;

        unsigned char digest[SZ];
        auto &data = payload.data;

        utils::digest(data.data(), data.size(), digest);
        bool eq = payload.hash == utils::bytes_view_t(digest, SZ);
        reply_to(*req, eq);
    }
}

} // namespace syncspirit::test
