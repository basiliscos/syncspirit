#include "global_discovery_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "../utils/discovery_support.h"
#include "http_actor.h"

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
      io_timeout(cfg.io_timeout) {

    rx_buff = std::make_shared<rx_buff_t::element_type>(rx_buff_size);
}

void global_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        addr_announce = p.create_address();
        addr_discovery = p.create_address();
    });
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
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&global_discovery_actor_t::on_discovery);
        p.subscribe_actor(&global_discovery_actor_t::on_announce_response, addr_announce);
        p.subscribe_actor(&global_discovery_actor_t::on_discovery_response, addr_discovery);
    });
}

void global_discovery_actor_t::on_start() noexcept {
    spdlog::trace("global_discovery_actor_t::on_start");
    announce();
}

void global_discovery_actor_t::announce() noexcept {
    spdlog::trace("global_discovery_actor_t::announce");

    fmt::memory_buffer tx_buff;
    auto res = utils::make_announce_request(tx_buff, announce_url, endpoint);
    if (!res) {
        spdlog::trace("global_discovery_actor_t::error making announce request :: {}", res.error().message());
        return do_shutdown();
    }
    auto timeout = r::pt::millisec{io_timeout};
    request_via<payload::http_request_t>(http_client, addr_announce, std::move(res.value()), std::move(tx_buff),
                                         rx_buff, rx_buff_size, make_context(ssl, dicovery_device_id))
        .send(timeout);
}

void global_discovery_actor_t::on_announce_response(message::http_response_t &message) noexcept {
    spdlog::trace("global_discovery_actor_t::on_announce_response");
    auto &ec = message.payload.ec;
    if (ec) {
        spdlog::error("global_discovery_actor_t, announcing error = {}", ec.message());
        return do_shutdown();
    }

    auto res = utils::parse_announce(message.payload.res->response);
    if (!res) {
        spdlog::warn("global_discovery_actor_t, parsing announce error = {}", res.error().message());
        return do_shutdown();
    }

    auto reannounce = res.value();
    spdlog::debug("global_discovery_actor_t:: will reannounce after {} seconds", reannounce);

    auto timeout = pt::seconds(reannounce);
    timer.expires_from_now(timeout);
    auto fwd_timer =
        ra::forwarder_t(*this, &global_discovery_actor_t::on_timer_trigger, &global_discovery_actor_t::on_timer_error);
    timer.async_wait(std::move(fwd_timer));
    resources->acquire(resource::timer);

    if (!announced) {
        send<payload::announce_notification_t>(coordinator, get_address());
        announced = true;
    }
}

void global_discovery_actor_t::on_discovery_response(message::http_response_t &message) noexcept {
    spdlog::trace("global_discovery_actor_t::on_discovery_response");
    auto &ec = message.payload.ec;
    auto orig_req = discovery_queue.front();
    discovery_queue.pop_front();
    if (ec) {
        return reply_with_error(*orig_req, ec);
    }

    auto res = utils::parse_announce(message.payload.res->response);
    if (!res) {
        spdlog::warn("global_discovery_actor_t, parsing discovery error = {}", res.error().message());
        return reply_with_error(*orig_req, res.error());
    }

    auto reannounce = res.value();
    spdlog::debug("global_discovery_actor_t:: will reannounce after {} seconds", reannounce);
}

void global_discovery_actor_t::on_discovery(message::discovery_request_t &req) noexcept {
    spdlog::trace("global_discovery_actor_t::on_on_discovery");

    fmt::memory_buffer tx_buff;
    auto res = utils::make_discovery_request(tx_buff, announce_url, req.payload.request_payload->peer);
    if (!res) {
        spdlog::trace("global_discovery_actor_t::error making discovery request :: {}", res.error().message());
        return do_shutdown();
    }

    auto timeout = r::pt::millisec{io_timeout};
    request_via<payload::http_request_t>(http_client, addr_discovery, std::move(res.value()), std::move(tx_buff),
                                         rx_buff, rx_buff_size, make_context(ssl, dicovery_device_id))
        .send(timeout);
    discovery_queue.emplace_back(&req);
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
