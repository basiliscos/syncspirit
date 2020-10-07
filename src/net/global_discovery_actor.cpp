#include "global_discovery_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "../utils/beast_support.h"
#include <charconv>

// for convenience
using json = nlohmann::json;

using namespace syncspirit::net;

global_discovery_actor_t::global_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, endpoint{cfg.endpoint}, announce_url{cfg.announce_url},
      dicovery_device_id{std::move(cfg.device_id)}, ssl{*cfg.ssl}, rx_buff_size{cfg.rx_buff_size},
      io_timeout(cfg.io_timeout), reannounce_after(cfg.reannounce_after) {

    rx_buff = std::make_shared<rx_buff_t::element_type>(rx_buff_size);
}

void global_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::http10, http_client, true).link(true);
        p.discover_name(names::coordinator, coordinator, false).link();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&global_discovery_actor_t::on_announce); });
}

void global_discovery_actor_t::on_start() noexcept {
    spdlog::trace("global_discovery_actor_t::on_start");
    json payload = json::object();
    payload["addresses"] = {fmt::format("tcp://{0}:{1}", endpoint.address().to_string(), endpoint.port())};

    http::request<http::string_body> req;
    req.method(http::verb::post);
    req.version(11);
    req.target(announce_url.path);
    req.set(http::field::host, announce_url.host);
    req.set(http::field::content_type, "application/json");

    req.body() = payload.dump();
    req.prepare_payload();

    fmt::memory_buffer tx_buff;
    auto res = utils::serialize(req, tx_buff);
    assert(res);
    auto timeout = r::pt::millisec{io_timeout};
    request<payload::http_request_t>(http_client, announce_url, std::move(tx_buff), rx_buff, rx_buff_size,
                                     make_context(ssl, dicovery_device_id))
        .send(timeout);
}

void global_discovery_actor_t::on_announce(message::http_response_t &message) noexcept {
    spdlog::trace("global_discovery_actor_t::on_announce");
    auto &ec = message.payload.ec;
    if (ec) {
        spdlog::error("global_discovery_actor_t, announcing error = {}", ec.message());
        return do_shutdown();
    }
    auto &res = message.payload.res->response;
    auto code = res.result_int();
    spdlog::debug("global_discovery_actor_t::on_announce code = {} ", code);
    if (code != 204 && code != 429) {
        spdlog::warn("global_discovery_actor_t, unexpected resonse code = {}", code);
        return do_shutdown();
    }

    auto convert = [&](const auto &it) -> int {
        if (it != res.end()) {
            auto str = it->value();
            int value;
            auto result = std::from_chars(str.begin(), str.end(), value);
            if (result.ec == std::errc()) {
                return value;
            }
        }
        return 0;
    };

    auto reannounce = convert(res.find("Reannounce-After"));
    if (reannounce < 1)
        reannounce = convert(res.find("Retry-After"));
    if (reannounce < 1)
        reannounce = this->reannounce_after / 1000;
    spdlog::debug("global_discovery_actor_t:: will reannounce after {} seconds", reannounce);

    send<payload::announce_notification_t>(coordinator);
}
