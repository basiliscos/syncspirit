#include "upnp_supervisor.h"
#include "http_actor.h"
#include "ssdp_actor.h"
#include "names.h"
#include "../utils/upnp_support.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;

upnp_supervisor_t::upnp_supervisor_t(ra::supervisor_asio_t *sup, const ra::supervisor_config_asio_t &sup_cfg,
                                     const config::upnp_config_t &cfg_, r::address_ptr_t registry_addr_)
    : ra::supervisor_asio_t(sup, sup_cfg), registry_addr{registry_addr_}, cfg{cfg_}, ssdp_errors{0} {
    addr_description = make_address();
    addr_external_ip = make_address();
    addr_mapping = make_address();
    rx_buff = std::make_shared<payload::http_request_t::rx_buff_t>();
}

void upnp_supervisor_t::shutdown_finish() noexcept {
    http_addr.reset();
    ssdp_addr.reset();
}

upnp_supervisor_t::~upnp_supervisor_t() { spdlog::trace("upnp_supervisor_t:~upnp_supervisor_t"); }

void upnp_supervisor_t::launch_ssdp() noexcept {
    spdlog::trace("upnp_supervisor_t::launch_ssdp");
    rotor::pt::seconds timeout{cfg.max_wait};
    ssdp_addr = create_actor<ssdp_actor_t>(timeout, cfg.max_wait)->get_address();
    ssdp_failures = 0;
}

void upnp_supervisor_t::launch_http() noexcept {
    spdlog::trace("upnp_supervisor_t::launch_http");
    rotor::pt::seconds timeout{cfg.max_wait};
    http_addr = create_actor<http_actor_t>(timeout)->get_address();
}

void upnp_supervisor_t::init_start() noexcept {
    subscribe(&upnp_supervisor_t::on_discovery);
    subscribe(&upnp_supervisor_t::on_ssdp_reply);
    subscribe(&upnp_supervisor_t::on_listen);
    subscribe(&upnp_supervisor_t::on_igd_description, addr_description);
    subscribe(&upnp_supervisor_t::on_external_ip, addr_external_ip);
    subscribe(&upnp_supervisor_t::on_mapping_ip, addr_mapping);
    request<r::payload::discovery_request_t>(registry_addr, names::acceptor).send(default_timeout);
    // init postponed
    // ra::supervisor_asio_t::init_start();
}

void upnp_supervisor_t::on_discovery(r::message::discovery_response_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_discovery");
    auto &ec = msg.payload.ec;
    if (ec) {
        spdlog::trace("upnp_supervisor_t::on_registration:: {} (requested: {})", ec.message(),
                      msg.payload.req->payload.request_payload.service_name);
        return;
    }

    auto &service_addr = msg.payload.res.service_addr;
    acceptor_addr = service_addr;
    registry_addr.reset();
    unsubscribe(&upnp_supervisor_t::on_discovery);
    launch_ssdp();
    launch_http();
    ra::supervisor_asio_t::init_start();
}

void upnp_supervisor_t::on_start(r::message::start_trigger_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_start");
    rotor::pt::seconds timeout{cfg.max_wait};
    request<payload::ssdp_request_t>(ssdp_addr).send(timeout);
    ra::supervisor_asio_t::on_start(msg);
}

void upnp_supervisor_t::on_shutdown_confirm(r::message::shutdown_response_t &msg) noexcept {
    ra::supervisor_asio_t::on_shutdown_confirm(msg);
    if (state == r::state_t::SHUTTING_DOWN) {
        return;
    }

    auto &target = msg.payload.req->payload.request_payload.actor_address;
    bool self_shutdown = false;

    if (target.get() == ssdp_addr.get() && !igd_url && (ssdp_failures == 0)) {
        ssdp_addr.reset();
        ++ssdp_errors;
        if (ssdp_errors < MAX_SSDP_ERRORS - 1) {
            std::abort();
        } else {
            self_shutdown = true;
        }
    }
    if (target.get() == http_addr.get()) {
        self_shutdown = true;
    }
    if (self_shutdown && state == r::state_t::OPERATIONAL) {
        do_shutdown();
    }
}

void upnp_supervisor_t::on_ssdp_reply(message::ssdp_response_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_ssdp_reply", ssdp_failures);
    if (msg.payload.ec) {
        if (ssdp_failures < MAX_SSDP_FAILURES - 1) {
            ++ssdp_failures;
            rotor::pt::seconds timeout{cfg.max_wait};
            request<payload::ssdp_request_t>(ssdp_addr).send(timeout);
        } else {
            do_shutdown();
        }
        return;
    }
    auto &url = msg.payload.res->igd.location;
    spdlog::debug("IGD location = {}", url.full);
    igd_url = url;
    // no longer need of ssdp
    send<r::payload::shutdown_trigger_t>(ssdp_addr);

    if (!http_addr) {
        spdlog::error("upnp_supervisor_t:: no active http actor");
        return do_shutdown();
    }

    make_request(addr_description, *igd_url,
                 [&](auto &tx_buff) { return make_description_request(tx_buff, *igd_url); });
}

void upnp_supervisor_t::on_igd_description(message::http_response_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_igd_description");
    if (msg.payload.ec) {
        spdlog::warn("upnp_actor:: get IGD description: {}", msg.payload.ec.message());
        return do_shutdown();
    }

    auto &body = msg.payload.res->response.body();
    auto igd_result = parse_igd(body.data(), body.size());
    if (!igd_result) {
        spdlog::warn("upnp_actor:: can't get IGD result: {}", igd_result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown();
    }

    rx_buff->consume(msg.payload.res->bytes);
    auto &igd = igd_result.value();
    auto &location = *igd_url;
    std::string control_url = fmt::format("http://{0}:{1}{2}", location.host, location.port, igd.control_path);
    std::string descr_url = fmt::format("http://{0}:{1}{2}", location.host, location.port, igd.description_path);
    spdlog::debug("IGD control url: {0}, description url: {1}", control_url, descr_url);

    auto url_option = utils::parse(control_url.c_str());
    if (!url_option) {
        spdlog::error("upnp_actor:: can't parse IGD url {}", control_url);
        return do_shutdown();
    }
    igd_control_url = url_option.value();

    make_request(addr_external_ip, *igd_control_url,
                 [&](auto &tx_buff) { return make_external_ip_request(tx_buff, *igd_control_url); });
}

void upnp_supervisor_t::on_external_ip(message::http_response_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_external_ip");
    if (msg.payload.ec) {
        spdlog::warn("upnp_actor:: get external IP address: {}", msg.payload.ec.message());
        return do_shutdown();
    }
    auto &body = msg.payload.res->response.body();
    auto ip_addr_result = parse_external_ip(body.data(), body.size());
    if (!ip_addr_result) {
        spdlog::warn("upnp_actor:: can't get external IP address: {}", ip_addr_result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown();
    }
    auto &ip_addr = ip_addr_result.value();
    spdlog::debug("external IP addr: {}", ip_addr);
    auto &rx_buff = msg.payload.req->payload.request_payload.rx_buff;
    rx_buff->consume(msg.payload.res->bytes);

    sys::error_code ec;
    external_addr = asio::ip::address::from_string(ip_addr, ec);
    if (ec) {
        spdlog::warn("upnp_actor:: can't external IP address '{0}' is incorrect: {}", ip_addr, ec.message());
        return do_shutdown();
    }

    auto listen_addr = msg.payload.res->local_endpoint.address();
    std::uint16_t port = 0; /* any port */
    request<payload::listen_request_t>(acceptor_addr, std::move(listen_addr), port).send(default_timeout);
}

void upnp_supervisor_t::on_listen(message::listen_response_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_listen");
    auto &ec = msg.payload.ec;
    if (ec) {
        spdlog::error("upnp_supervisor_t::on_listen :: ", ec.message());
        return do_shutdown();
    }

    auto &local_ep = msg.payload.res.listening_endpoint;
    spdlog::debug("going to map {0}:{1} => {2}:{3}", external_addr.to_string(), cfg.external_port,
                  local_ep.address().to_string(), local_ep.port());

    make_request(addr_mapping, *igd_control_url, [&](auto &tx_buff) {
        return make_mapping_request(tx_buff, *igd_control_url, cfg.external_port, local_ep.address().to_string(),
                                    local_ep.port());
    });
}

void upnp_supervisor_t::on_mapping_ip(message::http_response_t &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_mapping_ip");
    if (msg.payload.ec) {
        spdlog::warn("upnp_actor:: unsuccessfull port mapping: {}", msg.payload.ec.message());
        return do_shutdown();
    }
    auto &body = msg.payload.res->response.body();
    auto result = parse_mapping(body.data(), body.size());
    if (!result) {
        spdlog::warn("upnp_actor:: can't parse port mapping reply : {}", result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown();
    }
    rx_buff->consume(msg.payload.res->bytes);
    if (!result.value()) {
        spdlog::warn("upnp_actor:: unsuccessfull port mapping");
        return do_shutdown();
    }
    spdlog::trace("upnp_supervisor_t:: port mapping succeeded");
}
