#include "upnp_actor.h"
#include "../proto/upnp_support.h"
#include "../utils/error_code.h"
#include "spdlog/spdlog.h"
#include "names.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;
using namespace syncspirit::proto;

namespace {
namespace resource {
r::plugin::resource_id_t req_acceptor = 0;
r::plugin::resource_id_t external_port = 1;
r::plugin::resource_id_t http_req = 2;
} // namespace resource
} // namespace

upnp_actor_t::upnp_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, main_url{cfg.descr_url}, rx_buff_size{cfg.rx_buff_size}, external_port(cfg.external_port) {}

void upnp_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("upnp", false);
        addr_description = p.create_address();
        addr_external_ip = p.create_address();
        addr_mapping = p.create_address();
        addr_unmapping = p.create_address();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&upnp_actor_t::on_endpoint);
        p.subscribe_actor(&upnp_actor_t::on_igd_description, addr_description);
        p.subscribe_actor(&upnp_actor_t::on_external_ip, addr_external_ip);
        p.subscribe_actor(&upnp_actor_t::on_mapping_port, addr_mapping);
        p.subscribe_actor(&upnp_actor_t::on_unmapping_port, addr_unmapping);

        auto timeout = shutdown_timeout / 2;
        request<payload::endpoint_request_t>(acceptor).send(timeout);
        resources->acquire(resource::req_acceptor);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::http10, http_client, true).link(true);
        p.discover_name(names::coordinator, coordinator).link();
        p.discover_name(names::acceptor, acceptor, true).link(true);
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) {
        p.on_unlink([&](auto &req) {
            if (resources->has(resource::external_port)) {
                unlink_request = &req;
                return true;
            } else {
                return false;
            }
        });
    });
}

void upnp_actor_t::on_start() noexcept {
    spdlog::trace("{}, on_start", identity);
    rx_buff = std::make_shared<payload::http_request_t::rx_buff_t>();

    fmt::memory_buffer tx_buff;
    auto res = make_description_request(tx_buff, main_url);
    if (!res) {
        auto &ec = res.error();
        spdlog::trace("{}, error making description request :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }
    make_request(addr_description, main_url, std::move(tx_buff), true);
}

void upnp_actor_t::make_request(const r::address_ptr_t &addr, utils::URI &uri, fmt::memory_buffer &&tx_buff,
                                bool get_local_address) noexcept {
    resources->acquire(resource::http_req);
    auto timeout = shutdown_timeout * 8 / 9;
    http_request = request_via<payload::http_request_t>(http_client, addr, uri, std::move(tx_buff), rx_buff,
                                                        rx_buff_size, get_local_address)
                       .send(timeout);
}

void upnp_actor_t::request_finish() noexcept {
    resources->release(resource::http_req);
    http_request.reset();
}

void upnp_actor_t::on_endpoint(message::endpoint_response_t &res) noexcept {
    spdlog::trace("{}, on_endpoint", identity);
    resources->release(resource::req_acceptor);
    auto &ee = res.payload.ee;
    if (ee) {
        auto inner = utils::make_error_code(utils::error_code::endpoint_failed);
        spdlog::warn("{}, on_endpoint, cannot get acceptor endpoint :: {}", identity, ee->message());
        return do_shutdown(make_error(inner, ee));
    }
    accepting_endpoint = res.payload.res.local_endpoint;
    spdlog::debug("{}, local endpoint = {}", identity, accepting_endpoint);
}

void upnp_actor_t::on_igd_description(message::http_response_t &msg) noexcept {
    spdlog::trace("{}, on_igd_description", identity);
    request_finish();

    auto &ee = msg.payload.ee;
    if (ee) {
        auto inner = utils::make_error_code(utils::error_code::igd_description_failed);
        spdlog::warn("{}, get IGD description: {}", identity, ee->message());
        return do_shutdown(make_error(inner, ee));
    }

    local_address = msg.payload.res->local_addr.value();
    auto &body = msg.payload.res->response.body();
    auto igd_result = parse_igd(body.data(), body.size());
    if (!igd_result) {
        auto &ec = igd_result.error();
        spdlog::warn("{}, can't get IGD result: {}", identity, ec.message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown(make_error(ec));
    }

    rx_buff->consume(msg.payload.res->bytes);
    auto &igd = igd_result.value();
    std::string control_url = fmt::format("http://{0}:{1}{2}", main_url.host, main_url.port, igd.control_path);
    std::string descr_url = fmt::format("http://{0}:{1}{2}", main_url.host, main_url.port, igd.description_path);
    spdlog::debug("{}, IGD control url: {}, description url: {}", identity, control_url, descr_url);

    auto url_option = utils::parse(control_url.c_str());
    if (!url_option) {
        spdlog::error("{}, can't parse IGD url {}", identity, control_url);
        auto ec = utils::make_error_code(utils::error_code::unparseable_control_url);
        return do_shutdown(make_error(ec));
    }
    igd_control_url = url_option.value();

    fmt::memory_buffer tx_buff;
    auto res = make_external_ip_request(tx_buff, igd_control_url);
    if (!res) {
        auto &ec = res.error();
        spdlog::trace("{}, error making external ip address request :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }
    make_request(addr_external_ip, igd_control_url, std::move(tx_buff));
}

void upnp_actor_t::on_external_ip(message::http_response_t &msg) noexcept {
    spdlog::trace("{}, on_external_ip", identity);
    request_finish();

    auto &ee = msg.payload.ee;
    if (ee) {
        spdlog::warn("{}, get external IP address: {}", identity, ee->message());
        auto inner = utils::make_error_code(utils::error_code::external_ip_failed);
        return do_shutdown(make_error(inner, ee));
    }
    auto &body = msg.payload.res->response.body();
    auto ip_addr_result = parse_external_ip(body.data(), body.size());
    if (!ip_addr_result) {
        auto &ec = ip_addr_result.error();
        spdlog::warn("{}, can't get external IP address: {}", identity, ec.message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
        return do_shutdown(make_error(ec));
    }
    auto &ip_addr = ip_addr_result.value();
    spdlog::debug("{}, external IP addr: {}", identity, ip_addr);
    rx_buff->consume(msg.payload.res->bytes);

    sys::error_code io_ec;
    external_addr = asio::ip::address::from_string(ip_addr, io_ec);
    if (ee) {
        spdlog::warn("{}, can't external IP address '{}' is incorrect: {}", identity, ip_addr, io_ec.message());
        return do_shutdown(make_error(io_ec));
    }

    auto local_port = accepting_endpoint.port();
    spdlog::debug("{}, going to map {}:{} => {}:{}", identity, external_addr.to_string(), external_port, local_address,
                  local_port);

    fmt::memory_buffer tx_buff;
    auto res = make_mapping_request(tx_buff, igd_control_url, external_port, local_address.to_string(), local_port);
    if (!res) {
        auto &ec = res.error();
        spdlog::trace("{}, error making port mapping request :: {}", identity, ec.message());
        return do_shutdown(make_error(ec));
    }
    make_request(addr_mapping, igd_control_url, std::move(tx_buff));
}

void upnp_actor_t::on_mapping_port(message::http_response_t &msg) noexcept {
    spdlog::trace("{}, on_mapping_port", identity);
    request_finish();

    bool ok = false;
    auto &ee = msg.payload.ee;
    if (ee) {
        spdlog::warn("{}, unsuccessfull port mapping: {}", ee->message(), identity);
    } else if (state < r::state_t::SHUTTING_DOWN) {
        auto &body = msg.payload.res->response.body();
        auto result = parse_mapping(body.data(), body.size());
        if (!result) {
            spdlog::warn("{}, can't parse port mapping reply : {}", identity, result.error().message());
            std::string xml(body);
            spdlog::debug("xml:\n{0}\n", xml);
        } else {
            rx_buff->consume(msg.payload.res->bytes);
            if (!result.value()) {
                spdlog::warn("{}, unsuccessfull port mapping", identity);
            } else {
                spdlog::debug("{}, port mapping succeeded", identity);
                ok = true;
                resources->acquire(resource::external_port);
            }
        }
    }
    send<payload::port_mapping_notification_t>(coordinator, external_addr, ok);
}

void upnp_actor_t::on_unmapping_port(message::http_response_t &msg) noexcept {
    spdlog::trace("{}, on_unmapping_port", identity);
    request_finish();
    resources->release(resource::external_port);

    auto &ee = msg.payload.ee;
    if (ee) {
        spdlog::warn("upnp_actor:: unsuccessfull port mapping: {}", ee->message());
        return;
    }
    auto &body = msg.payload.res->response.body();
    auto result = parse_unmapping(body.data(), body.size());
    if (!result) {
        spdlog::warn("{}, can't parse port unmapping reply : {}", identity, result.error().message());
        std::string xml(body);
        spdlog::debug("xml:\n{0}\n", xml);
    } else {
        spdlog::debug("{}, succesfully unmapped external port {}", identity, external_port);
    }
    if (unlink_request) {
        auto p = get_plugin(r::plugin::link_client_plugin_t::class_identity);
        auto plugin = static_cast<r::plugin::link_client_plugin_t *>(p);
        plugin->forget_link(*unlink_request);
        unlink_request.reset();
    }
}

void upnp_actor_t::shutdown_start() noexcept {
    spdlog::trace("{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
    if (resources->has(resource::req_acceptor)) {
        resources->release(resource::req_acceptor);
    }

    if (resources->has(resource::http_req)) {
        send<message::http_cancel_t::payload_t>(http_client, *http_request, get_address());
    }

    if (resources->has(resource::external_port)) {
        spdlog::trace("{}, going to unmap extenal port {}", identity, external_port);
        fmt::memory_buffer tx_buff;
        auto res = make_unmapping_request(tx_buff, igd_control_url, external_port);
        if (!res) {
            spdlog::warn("{}, error making port mapping request :: {}", identity, res.error().message());
            resources->release(resource::external_port);
            return;
        }
        make_request(addr_unmapping, igd_control_url, std::move(tx_buff));
    }
}
