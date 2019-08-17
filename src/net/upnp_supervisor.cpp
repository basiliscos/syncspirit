#include "upnp_supervisor.h"
#include "http_actor.h"
#include "ssdp_actor.h"
#include "../utils/upnp_support.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;

upnp_supervisor_t::upnp_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx,
                                     const ra::supervisor_config_t &sup_cfg, const config::upnp_config_t &cfg_)
    : ra::supervisor_asio_t(sup, ctx, sup_cfg), cfg{cfg_}, ssdp_errors{0} {
    addr_description = make_address();
    addr_external_ip = make_address();
}

void upnp_supervisor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    if (msg.payload.actor_address == address) {
        http_addr.reset();
        ssdp_addr.reset();
    }
    ra::supervisor_asio_t::on_shutdown(msg);
}

upnp_supervisor_t::~upnp_supervisor_t() { spdlog::trace("upnp_supervisor_t:~upnp_supervisor_t"); }

void upnp_supervisor_t::launch_ssdp() noexcept {
    spdlog::trace("upnp_supervisor_t::launch_ssdp");
    ssdp_addr = create_actor<ssdp_actor_t>(cfg.max_wait)->get_address();
    ssdp_failures = 0;
}

void upnp_supervisor_t::on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept {
    if (msg.payload.actor_address == address) {
        subscribe(&upnp_supervisor_t::on_ssdp);
        subscribe(&upnp_supervisor_t::on_ssdp_failure);
        subscribe(&upnp_supervisor_t::on_igd_description, addr_description);
        subscribe(&upnp_supervisor_t::on_external_ip, addr_external_ip);
    }
    ra::supervisor_asio_t::on_initialize(msg);
}

void upnp_supervisor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_start");
    launch_ssdp();
    http_addr = create_actor<http_actor_t>()->get_address();
    ra::supervisor_asio_t::on_start(msg);
}

void upnp_supervisor_t::on_shutdown_confirm(r::message_t<r::payload::shutdown_confirmation_t> &msg) noexcept {
    ra::supervisor_asio_t::on_shutdown_confirm(msg);
    auto &target = msg.payload.actor_address;
    bool self_shutdown = false;

    if (target.get() == ssdp_addr.get() && !igd_url && (ssdp_failures == 0)) {
        ssdp_addr.reset();
        ++ssdp_errors;
        if (ssdp_errors < MAX_SSDP_ERRORS - 1) {
            launch_ssdp();
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

void upnp_supervisor_t::on_ssdp(r::message_t<ssdp_result_t> &msg) noexcept {
    auto &url = msg.payload.location;
    spdlog::debug("IGD location = {}", url.full);
    igd_url = url;
    // no longer need of ssdp
    send<r::payload::shutdown_request_t>(address, ssdp_addr);

    if (!http_addr) {
        spdlog::error("upnp_supervisor_t:: no active http actor");
        return do_shutdown();
    }

    fmt::memory_buffer tx_buff;
    auto result = make_description_request(tx_buff, *igd_url);
    if (!result) {
        spdlog::error("upnp_supervisor_t:: cannot serialize IP-address request: {0}", result.error().message());
        return do_shutdown();
    }
    send<request_t>(http_addr, *igd_url, std::move(tx_buff), pt::milliseconds{cfg.timeout * 1000}, addr_description,
                    cfg.rx_buff_size);
}

void upnp_supervisor_t::on_ssdp_failure(r::message_t<ssdp_failure_t> &) noexcept {
    spdlog::trace("upnp_supervisor_t::on_ssdp_failure ({0})", ssdp_failures);
    if (ssdp_failures < MAX_SSDP_FAILURES - 1) {
        ++ssdp_failures;
        send<try_again_request_t>(ssdp_addr);
    } else {
        send<r::payload::shutdown_request_t>(address, ssdp_addr);
    }
}

void upnp_supervisor_t::on_igd_description(r::message_t<response_t> &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_igd_description");

    auto &body = msg.payload.response.body();
    auto igd_result = parse_igd(body.data(), body.size());
    if (!igd_result) {
        spdlog::warn("upnp_actor:: can't get IGD result: {}", igd_result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown();
    }

    msg.payload.data.consume(msg.payload.bytes);
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

    fmt::memory_buffer tx_buff;
    auto result = make_external_ip_request(tx_buff, *igd_control_url);
    if (!result) {
        spdlog::error("upnp_supervisor_t:: cannot serialize IP-address request: {0}", result.error().message());
        return do_shutdown();
    }
    send<request_t>(http_addr, *igd_control_url, std::move(tx_buff), pt::milliseconds{cfg.timeout * 1000},
                    addr_external_ip, cfg.rx_buff_size);
}

void upnp_supervisor_t::on_external_ip(r::message_t<response_t> &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_external_ip");
    auto &body = msg.payload.response.body();
    auto ip_addr_result = parse_external_ip(body.data(), body.size());
    if (!ip_addr_result) {
        spdlog::warn("upnp_actor:: can't get external IP address: {}", ip_addr_result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown();
    }
    spdlog::debug("external IP addr: {}", ip_addr_result.value());
    msg.payload.data.consume(msg.payload.bytes);
}
