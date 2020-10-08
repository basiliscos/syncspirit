#include "global_discovery_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "../utils/beast_support.h"
#include "http_actor.h"
#include <charconv>

// for convenience
using json = nlohmann::json;

using namespace syncspirit::net;

namespace private_names {
const char *https = "net::gda::https";
}

namespace {
namespace resource {
r::plugin::resource_id_t timer = 0;
} // namespace resource
} // namespace

global_discovery_actor_t::global_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()}, timer{strand},
      endpoint{cfg.endpoint}, announce_url{cfg.announce_url},
      dicovery_device_id{std::move(cfg.device_id)}, ssl{*cfg.ssl}, rx_buff_size{cfg.rx_buff_size},
      io_timeout(cfg.io_timeout), reannounce_after(cfg.reannounce_after) {

    rx_buff = std::make_shared<rx_buff_t::element_type>(rx_buff_size);
}

void global_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        auto timeout = (shutdown_timeout * 9) / 10;
        auto io_timeout = (shutdown_timeout * 8) / 10;
        get_supervisor()
            .create_actor<http_actor_t>()
            .timeout(timeout)
            .request_timeout(io_timeout)
            .resolve_timeout(io_timeout)
            .registry_name(private_names::https)
            .keep_alive(true)
            .finish();

        p.discover_name(private_names::https, http_client, true).link(true);
        p.discover_name(names::coordinator, coordinator, false).link();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&global_discovery_actor_t::on_announce); });
}

void global_discovery_actor_t::on_start() noexcept {
    spdlog::trace("global_discovery_actor_t::on_start");
    announce();
}

void global_discovery_actor_t::announce() noexcept {
    spdlog::trace("global_discovery_actor_t::announce");
    json payload = json::object();
    payload["addresses"] = {fmt::format("tcp://{0}:{1}", endpoint.address().to_string(), endpoint.port())};

    http::request<http::string_body> req;
    req.method(http::verb::post);
    req.version(11);
    req.keep_alive(true);
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
    if (reannounce < 1) {
        reannounce = convert(res.find("Retry-After"));
    }
    if (reannounce < 1) {
        reannounce = this->reannounce_after / 1000;
    }
    spdlog::debug("global_discovery_actor_t:: will reannounce after {} seconds", reannounce);

    // send<payload::announce_notification_t>(coordinator);
    auto timeout = pt::seconds(reannounce);
    timer.expires_from_now(timeout);
    auto fwd_timer =
        ra::forwarder_t(*this, &global_discovery_actor_t::on_timer_trigger, &global_discovery_actor_t::on_timer_error);
    timer.async_wait(std::move(fwd_timer));
    resources->acquire(resource::timer);
}

void global_discovery_actor_t::on_timer_error(const sys::error_code &ec) noexcept {
    resources->release(resource::timer);
    if (ec != asio::error::operation_aborted) {
        spdlog::warn("global_discovery_actor_t::on_timer_error :: {}", ec.message());
        do_shutdown();
    }
}

void global_discovery_actor_t::on_timer_trigger() noexcept {
    resources->release(resource::timer);
    announce();
}

void global_discovery_actor_t::timer_cancel() noexcept {
    sys::error_code ec;
    timer.cancel(ec);
    if (ec) {
        spdlog::error("global_discovery_actor_t:: timer cancellation : {}", ec.message());
    }
}

void global_discovery_actor_t::shutdown_start() noexcept {
    if (resources->has(resource::timer)) {
        timer_cancel();
    }
    r::actor_base_t::shutdown_start();
}
