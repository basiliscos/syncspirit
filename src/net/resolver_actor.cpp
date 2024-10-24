// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "resolver_actor.h"
#include "model/cluster.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "names.h"

using namespace syncspirit::net;
using namespace syncspirit::utils;

namespace {

template <typename T> using guard_t = std::unique_ptr<T, std::function<void(T *)>>;

template <typename T, typename G> guard_t<T> make_guard(T *ptr, G &&fn) {
    return guard_t<T>{ptr, [fn = std::move(fn)](T *it) { fn(it); }};
}

namespace resource {
r::plugin::resource_id_t timer = 0;
r::plugin::resource_id_t send = 1;
r::plugin::resource_id_t recv = 2;
} // namespace resource
} // namespace

resolver_actor_t::resolver_actor_t(resolver_actor_t::config_t &config)
    : r::actor_base_t{config}, io_timeout{config.resolve_timeout}, hosts_path{config.hosts_path},
      server_addresses{std::move(config.server_addresses)},
      strand{static_cast<ra::supervisor_asio_t *>(config.supervisor)->get_strand()}, channel{nullptr} {

    rx_buff.resize(1500);
    tx_buff = nullptr;
}

void resolver_actor_t::do_initialize(r::system_context_t *ctx) noexcept {
    r::actor_base_t::do_initialize(ctx);

    ares_options opts;
    int opts_mask = 0;
    memset(&opts, 0, sizeof(opts));

    if (hosts_path.size()) {
        opts.hosts_path = const_cast<char *>(hosts_path.data());
        opts_mask |= ARES_OPT_HOSTS_FILE;
    }

    auto status = ares_init_options(&channel, &opts, opts_mask);
    if (status != ARES_SUCCESS) {
        LOG_ERROR(log, "cannot do ares_init, code = {}", static_cast<int>(status));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        return do_shutdown(make_error(ec));
    }

    auto dns_servers = std::string(server_addresses);
    if (dns_servers.empty()) {
        auto servers = ares_get_servers_csv(channel);
        if (!servers) {
            LOG_ERROR(log, "cannot get dns servers");
            auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
            return do_shutdown(make_error(ec));
        }

        auto servers_guard = make_guard(servers, [](auto str) { ares_free_string(str); });
        LOG_TRACE(log, "got dns servers: {}", servers);
        dns_servers = servers;
    }

    auto dns_addresses = utils::parse_dns_servers(dns_servers);
    if (dns_addresses.empty()) {
        LOG_ERROR(log, "no valid dns servers found");
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        return do_shutdown(make_error(ec));
    }

    auto dns_address = dns_addresses[0];
    for (auto &addr : dns_addresses) {
        if (addr.ip.is_v4()) {
            dns_address = addr;
            break;
        }
    }
    LOG_DEBUG(log, "selected dns server: {}:{}", dns_address.ip, dns_address.port);

    sys::error_code ec;
    auto s = udp_socket_t{strand.context()};
    s.open(boost::asio::ip::udp::v4(), ec);
    if (ec) {
        LOG_WARN(log, "init, can't open socket: {}", ec.message());
        return do_shutdown(make_error(ec));
    }

    auto endpoint = asio::ip::udp::endpoint(dns_address.ip, dns_address.port);
    s.connect(endpoint, ec);
    if (ec) {
        LOG_WARN(log, "init, can't connect to {}: {}", endpoint, ec.message());
        return do_shutdown(make_error(ec));
    }

    sock.reset(new udp_socket_t(std::move(s)));
}

void resolver_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::resolver, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(names::resolver, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&resolver_actor_t::on_request);
        p.subscribe_actor(&resolver_actor_t::on_cancel);
    });
}

void resolver_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void resolver_actor_t::shutdown_finish() noexcept {
    r::actor_base_t::shutdown_finish();
    ares_destroy(channel);
}

void resolver_actor_t::on_request(message::resolve_request_t &req) noexcept {
    queue.emplace_back(&req);
    process();
}

void resolver_actor_t::on_cancel(message::resolve_cancel_t &message) noexcept {
    if (queue.empty())
        return;
    auto &request_id = message.payload.id;
    auto &source = message.payload.source;
    auto matches = [&](auto &it) {
        auto &payload = it->payload;
        return payload.id == request_id && payload.origin == source;
    };
    if (matches(queue.front())) {
        assert(timer_id);
        cancel_timer();
    } else if (queue.size() > 1) {
        auto it = queue.begin();
        std::advance(it, 1);
        for (; it != queue.end(); ++it) {
            if (matches(*it)) {
                auto ec = r::make_error_code(r::error_code_t::cancelled);
                reply_with_error(**it, make_error(ec));
                queue.erase(it);
                return;
            }
        }
    }
}

void resolver_actor_t::mass_reply(const utils::dns_query_t &query, const resolve_results_t &results,
                                  bool update_cache) noexcept {
    reply(query, [&](auto &message) { reply_to(message, results); });
    current_query.reset();
    cache[query] = results;
}

void resolver_actor_t::mass_reply(const utils::dns_query_t &quey, const std::error_code &ec) noexcept {
    reply(quey, [&](auto &message) { reply_with_error(message, make_error(ec)); });
}

void resolver_actor_t::process() noexcept {
    if (resources->has(resource::recv) || resources->has(resource::send)) {
        return;
    }
    if (queue.empty())
        return;
    auto queue_it = queue.begin();
    auto &payload = (*queue_it)->payload.request_payload;
    auto &query = *payload;
    auto cache_it = cache.find(query);
    if (cache_it != cache.end()) {
        mass_reply(query, cache_it->second, false);
        LOG_TRACE(log, "cache hit for '{}'", query.host);
        return process();
    }
    if (queue.empty())
        return;
    resolve_start(*queue_it);
}

bool resolver_actor_t::resolve_locally(const utils::dns_query_t &query) noexcept {
    auto host = query.host.c_str();
    hostent *host_ent;
    auto result = ares_gethostbyname_file(channel, host, AF_INET, &host_ent);
    if (result != ARES_SUCCESS) {
        LOG_TRACE(log, "host '{}' is not found in hosts file", host);
    } else {
        auto guard = make_guard(host_ent, [](auto *ptr) { ares_free_hostent(ptr); });
        auto results = payload::address_response_t::resolve_results_t();
        auto p_addr = host_ent->h_addr_list;
        while (p_addr && *p_addr) {
            char buff[INET6_ADDRSTRLEN + 1] = {0};
            auto family = host_ent->h_length == 4 ? AF_INET : AF_INET6;
            ares_inet_ntop(family, *p_addr, buff, INET6_ADDRSTRLEN);
            auto addr_string = std::string_view(buff);
            LOG_DEBUG(log, "{} => {}, resolved via hosts file", host, addr_string);

            auto ec = sys::error_code{};
            auto ip = asio::ip::make_address(buff, ec);
            if (ec) {
                LOG_WARN(log, "invalid ip address {}: ", buff, ec.message());
                continue;
            }
            results.emplace_back(std::move(ip));
            ++p_addr;
        }

        if (results.size()) {
            mass_reply(query, std::move(results), true);
            return true;
        }
    }
    return false;
}

bool resolver_actor_t::resolve_as_ip(const utils::dns_query_t &query) noexcept {
    sys::error_code ec;
    auto host = std::string_view(query.host);
    if (host.size() && host[0] == '[') {
        auto idx = host.find_last_of(']');
        if (idx != host.npos) {
            host = host.substr(1, idx - 1);
        }
    }
    auto ip = asio::ip::make_address(host, ec);
    if (!ec) {
        auto results = payload::address_response_t::resolve_results_t();
        LOG_DEBUG(log, "{} resolved as ip address", query.host);
        results.emplace_back(std::move(ip));
        mass_reply(query, std::move(results), false);
        return true;
    }

    return false;
}

void resolver_actor_t::resolve_start(request_ptr_t &req) noexcept {
    if (resources->has_any())
        return;
    if (queue.empty())
        return;

    auto &query = *req->payload.request_payload;
    if (resolve_as_ip(query)) {
        return process();
    }
    if (resolve_locally(query)) {
        return process();
    }

    auto host = query.host.c_str();
    ares_dns_record_t *record_raw;
    auto result = ares_dns_record_create(&record_raw, 0, ARES_FLAG_RD, ARES_OPCODE_QUERY, ARES_RCODE_NOERROR);
    if (result != ARES_SUCCESS) {
        LOG_WARN(log, "cannot create dns record: {}", static_cast<int>(result));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        reply_with_error(*queue.front(), make_error(ec));
        return queue.pop_front();
    }
    auto record = make_guard(record_raw, [](ares_dns_record_t *ptr) { ares_dns_record_destroy(ptr); });

    result = ares_dns_record_query_add(record.get(), host, ARES_REC_TYPE_A, ARES_CLASS_IN);
    if (result != ARES_SUCCESS) {
        LOG_WARN(log, "cannot record dns query: {}", static_cast<int>(result));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        reply_with_error(*queue.front(), make_error(ec));
        return queue.pop_front();
    }

    assert(this->tx_buff == nullptr);
    size_t buff_sz;
    result = ares_dns_write(record.get(), &this->tx_buff, &buff_sz);
    if (result != ARES_SUCCESS) {
        LOG_WARN(log, "cannot serialized dns query: {}", static_cast<int>(result));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        reply_with_error(*queue.front(), make_error(ec));
        return queue.pop_front();
    }

    auto fwd_read = ra::forwarder_t(*this, &resolver_actor_t::on_read, &resolver_actor_t::on_read_error);
    auto rx_buff = asio::buffer(this->rx_buff.data(), this->rx_buff.size());
    sock->async_receive(rx_buff, std::move(fwd_read));
    resources->acquire(resource::recv);

    auto fwd_write = ra::forwarder_t(*this, &resolver_actor_t::on_write, &resolver_actor_t::on_write_error);
    auto tx_buff = asio::buffer(this->tx_buff, buff_sz);
    sock->async_send(tx_buff, std::move(fwd_write));
    resources->acquire(resource::send);

    timer_id = start_timer(io_timeout, *this, &resolver_actor_t::on_timer);
    resources->acquire(resource::timer);
    current_query = req;
}

void resolver_actor_t::on_timer(r::request_id_t, bool cancelled) noexcept {
    resources->release(resource::timer);
    auto cancel_socket = [this]() {
        if (sock) {
            sys::error_code ec;
            sock->cancel(ec);
            if (ec) {
                LOG_WARN(log, "cannot cancel socket: {}", ec.message());
            }
        }
    };
    if (cancelled) {
        if (resources->has(resource::recv) || resources->has(resource::send)) {
            cancel_socket();
            // reply_with_error(*current_query, make_error(ec));
            // queue.pop_front();
        }
    } else {
        if (!queue.empty() && current_query) {
            // could be actually some other ec...
            auto ec = r::make_error_code(r::error_code_t::request_timeout);
            auto &payload = current_query->payload.request_payload;
            LOG_DEBUG(log, "timeout while resolving '{}'", payload->host);
            mass_reply(*payload, ec);
            cancel_socket();
        }
    }
    timer_id.reset();
    process();
}

void resolver_actor_t::cancel_timer() noexcept {
    if (timer_id) {
        r::actor_base_t::cancel_timer(*timer_id);
    }
}

void resolver_actor_t::on_write(size_t bytes) noexcept {
    resources->release(resource::send);
    ares_free_string(tx_buff);
    tx_buff = nullptr;
}

void resolver_actor_t::on_write_error(const sys::error_code &ec) noexcept {
    resources->release(resource::send);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "on_write_error, error = {}", ec.message());
    }
    if (current_query) {
        auto &payload = current_query->payload.request_payload;
        mass_reply(*payload, ec);
    }
    process();
}

void resolver_actor_t::on_read_error(const sys::error_code &ec) noexcept {
    resources->release(resource::recv);
    if (ec != asio::error::operation_aborted) {
        LOG_WARN(log, "on_read_error, error = {}", ec.message());
    }
    if (current_query) {
        auto &payload = current_query->payload.request_payload;
        mass_reply(*payload, ec);
    }
    process();
}

void resolver_actor_t::on_read(size_t bytes) noexcept {
    resources->release(resource::recv);

    ares_dns_record_t *record_raw;
    auto data = reinterpret_cast<const unsigned char *>(rx_buff.data());
    auto result = ares_dns_parse(data, bytes, ARES_DNS_PARSE_AN_BASE_RAW, &record_raw);
    if (result != ARES_SUCCESS) {
        LOG_WARN(log, "cannot parse dns reply: {}", static_cast<int>(result));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        mass_reply(*current_query->payload.request_payload, ec);
        return process();
    }
    auto record = make_guard(record_raw, [](ares_dns_record_t *ptr) { ares_dns_record_destroy(ptr); });
    auto query_count = ares_dns_record_query_cnt(record_raw);
    if (query_count != 1) {
        LOG_WARN(log, "dns reply query count mismatch {}, expected: 1", query_count);
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        mass_reply(*current_query->payload.request_payload, ec);
        return process();
    }
    const char *queried_name;
    ares_dns_rec_type_t queried_type;
    ares_dns_class_t queried_class;
    result = ares_dns_record_query_get(record_raw, 0, &queried_name, &queried_type, &queried_class);
    if (result != ARES_SUCCESS) {
        LOG_WARN(log, "cannot get initial query from dns reply: {}", static_cast<int>(result));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        mass_reply(*current_query->payload.request_payload, ec);
        return process();
    }
    auto &requested_host = current_query->payload.request_payload->host;
    if (queried_name != requested_host) {
        LOG_WARN(log, "dns reply for '{}', it is asked for: '{}'", queried_name, requested_host);
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        mass_reply(*current_query->payload.request_payload, ec);
        return process();
    }

    auto answers_count = ares_dns_record_rr_cnt(record_raw, ARES_SECTION_ANSWER);
    if (answers_count < 1) {
        LOG_WARN(log, "dns reply contains no answers: {}", static_cast<int>(result));
        auto ec = utils::make_error_code(utils::error_code_t::cares_failure);
        mass_reply(*current_query->payload.request_payload, ec);
        return process();
    }

    char addr_buff[INET6_ADDRSTRLEN + 1] = {0};
    auto port = current_query->payload.request_payload->port;
    auto results = payload::address_response_t::resolve_results_t();
    for (size_t i = 0; i < answers_count; ++i) {
        auto resource_record = ares_dns_record_rr_get_const(record_raw, ARES_SECTION_ANSWER, i);
        size_t keys_cnt;
        auto name = ares_dns_rr_get_name(resource_record);
        const ares_dns_rr_key_t *keys = ares_dns_rr_get_keys(ares_dns_rr_get_type(resource_record), &keys_cnt);
        for (size_t k = 0; k < keys_cnt; k++) {
            auto key = keys[k];
            auto type = ares_dns_rr_key_datatype(key);
            if (type == ARES_DATATYPE_INADDR) {
                auto addr = ares_dns_rr_get_addr(resource_record, key);
                ares_inet_ntop(AF_INET, addr, addr_buff, sizeof(addr_buff));
                auto ip = asio::ip::make_address(addr_buff);
                auto ep = tcp::endpoint(ip, port);

                LOG_DEBUG(log, "{} => {}, resolved", name, ep);
            } else if (type == ARES_DATATYPE_BIN) {
                size_t length;
                auto data = ares_dns_rr_get_bin(resource_record, key, &length);
                if (length == 4) {
                    ares_inet_ntop(AF_INET, data, addr_buff, sizeof(addr_buff));
                    auto ip = asio::ip::make_address(addr_buff);
                    LOG_DEBUG(log, "resolved: {} => {}", name, ip);
                    results.emplace_back(ip);
                }
            }
        }
    }

    mass_reply(*current_query->payload.request_payload, results, true);
    cancel_timer();
    process();
}
