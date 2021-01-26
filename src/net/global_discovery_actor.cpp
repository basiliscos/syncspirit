#include "global_discovery_actor.h"
#include "names.h"
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "../proto/discovery_support.h"
#include "../utils/error_code.h"
#include "http_actor.h"

using namespace syncspirit::net;

namespace private_names {
const char *https = "net::gda::https";
}

namespace {
namespace resource {
r::plugin::resource_id_t timer = 0;
r::plugin::resource_id_t http = 1;
} // namespace resource
} // namespace

global_discovery_actor_t::global_discovery_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, endpoint{cfg.endpoint}, announce_url{cfg.announce_url},
      dicovery_device_id{std::move(cfg.device_id)}, ssl_pair{*cfg.ssl_pair}, rx_buff_size{cfg.rx_buff_size},
      io_timeout(cfg.io_timeout) {

    rx_buff = std::make_shared<rx_buff_t::element_type>(rx_buff_size);
}

void global_discovery_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        addr_announce = p.create_address();
        addr_discovery = p.create_address();
        p.set_identity("global_discovery", false);
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
    spdlog::trace("{}, on_start (addr = {})", identity, (void *)address.get());
    announce();
}

void global_discovery_actor_t::announce() noexcept {
    spdlog::trace("{}, announce", identity);

    fmt::memory_buffer tx_buff;
    auto res = proto::make_announce_request(tx_buff, announce_url, endpoint);
    if (!res) {
        spdlog::trace("{}, error making announce request :: {}", identity, res.error().message());
        return do_shutdown(make_error(res.error()));
    }
    make_request(addr_announce, res.value(), std::move(tx_buff));
}

void global_discovery_actor_t::on_announce_response(message::http_response_t &message) noexcept {
    spdlog::trace("{}, on_announce_response", identity);
    resources->release(resource::http);
    http_request.reset();

    auto &ec = message.payload.ec;
    if (ec) {
        spdlog::error("{}, announcing error = {}", identity, ec->message());
        auto inner = utils::make_error_code(utils::error_code::announce_failed);
        return do_shutdown(make_error(inner, ec));
    }

    auto res = proto::parse_announce(message.payload.res->response);
    if (!res) {
        spdlog::warn("{}, parsing announce error = {}", identity, res.error().message());
        return do_shutdown(make_error(res.error()));
    }

    if (state >= r::state_t::SHUTTING_DOWN)
        return;

    auto reannounce = res.value();
    spdlog::debug("{}, will reannounce after {} seconds", identity, reannounce);

    auto timeout = pt::seconds(reannounce);
    timer_request = start_timer(timeout, *this, &global_discovery_actor_t::on_timer);
    resources->acquire(resource::timer);

    if (!announced) {
        send<payload::announce_notification_t>(coordinator, get_address());
        announced = true;
    }
}

void global_discovery_actor_t::on_discovery_response(message::http_response_t &message) noexcept {
    spdlog::trace("{}, on_discovery_response", identity);
    resources->release(resource::http);
    http_request.reset();

    auto &ec = message.payload.ec;
    auto orig_req = discovery_queue.front();
    discovery_queue.pop_front();
    if (ec) {
        auto inner = utils::make_error_code(utils::error_code::discovery_failed);
        return reply_with_error(*orig_req, make_error(inner, ec));
    }

    auto res = proto::parse_contact(message.payload.res->response);
    if (!res) {
        spdlog::warn("{}, parsing discovery error = {}", identity, res.error().message());
        return reply_with_error(*orig_req, make_error(res.error()));
    }

#if 0
    if (res && res.value()) {
        auto& uris = res.value().value().uris;
        auto& id = orig_req->payload.request_payload->device_id;
        for(auto uri: uris) {
            spdlog::trace("global_discovery_actor_t::on_discovery_response, {} reacheable via {}", id, uri.full);
        }
    }
#endif
    reply_to(*orig_req, std::move(res.value()));
}

void global_discovery_actor_t::on_discovery(message::discovery_request_t &req) noexcept {
    spdlog::trace("{}, on_discovery", identity);

    fmt::memory_buffer tx_buff;
    auto r = proto::make_discovery_request(tx_buff, announce_url, req.payload.request_payload->device_id);
    if (!r) {
        spdlog::trace("{}, error making discovery request :: {}", identity, r.error().message());
        return do_shutdown(make_error(r.error()));
    }

    make_request(addr_discovery, r.value(), std::move(tx_buff));
    discovery_queue.emplace_back(&req);
}

void global_discovery_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    if (!cancelled) {
        announce();
    }
}

void global_discovery_actor_t::make_request(const r::address_ptr_t &addr, utils::URI &uri,
                                            fmt::memory_buffer &&tx_buff) noexcept {
    auto timeout = r::pt::millisec{io_timeout};
    transport::ssl_junction_t ssl{dicovery_device_id, &ssl_pair, true};
    http_request = request_via<payload::http_request_t>(http_client, addr, uri, std::move(tx_buff), rx_buff,
                                                        rx_buff_size, std::move(ssl))
                       .send(timeout);
    resources->acquire(resource::http);
}

void global_discovery_actor_t::shutdown_start() noexcept {
    spdlog::trace("{}, shutdown_start", identity);
    if (resources->has(resource::http)) {
        send<message::http_cancel_t::payload_t>(http_client, *http_request, get_address());
    }
    send<payload::http_close_connection_t>(http_client);

    if (resources->has(resource::timer)) {
        cancel_timer(*timer_request);
    }
    r::actor_base_t::shutdown_start();
}
