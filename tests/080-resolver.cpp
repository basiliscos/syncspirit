// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include <rotor.hpp>
#include <rotor/asio.hpp>
#include "test-utils.h"
#include "utils/error_code.h"
#include "net/resolver_actor.h"
#include "utils/format.hpp"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::net;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace r = rotor;
namespace ra = r::asio;

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;
using response_ptr_t = r::intrusive_ptr_t<message::resolve_response_t>;

auto timeout = r::pt::time_duration{r::pt::millisec{1000}};

struct my_supervisor_t : ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using parent_t::parent_t;
    using responses_t = std::vector<response_ptr_t>;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        ra::supervisor_asio_t::configure(plugin);
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&my_supervisor_t::on_resolve); });

        if (configure_callback) {
            configure_callback(plugin);
        }
    }

    void on_resolve(message::resolve_response_t &res) noexcept { responses.emplace_back(&res); }

    responses_t responses;
    configure_callback_t configure_callback;
};

using supervisor_ptr_t = r::intrusive_ptr_t<my_supervisor_t>;
using actor_ptr_t = r::intrusive_ptr_t<resolver_actor_t>;

struct fixture_t {

    fixture_t() : ctx(io_ctx), root_path{bfs::unique_path()}, path_quard{root_path}, remote_resolver{io_ctx} {
        utils::set_default("trace");
        log = utils::get_logger("fixture");
        rx_buff.resize(1500);
    }

    virtual void main() noexcept = 0;

    void run() {
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<my_supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->start();
        sup->do_process();

        resolv_conf_path = root_path / "resolv.conf";
        hosts_path = root_path / "hosts";

        auto ep = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 0);
        remote_resolver.open(ep.protocol());
        remote_resolver.bind(ep);

        auto local_ep = remote_resolver.local_endpoint();
        log->info("remote resolver: {}", local_ep);

        main();

        sup->do_shutdown();
        sup->do_process();
        io_ctx.run();
    }

    asio::io_context io_ctx{1};
    ra::system_context_asio_t ctx;
    bfs::path root_path;
    bfs::path resolv_conf_path;
    bfs::path hosts_path;
    path_guard_t path_quard;
    utils::logger_t log;
    supervisor_ptr_t sup;
    actor_ptr_t resolver;
    udp_socket_t remote_resolver;
    udp::endpoint resolver_endpoint;
    fmt::memory_buffer rx_buff;
};

void test_local_resolver() {
    struct F : fixture_t {
        void main() noexcept override {

            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", 1234));
            write_file(hosts_path, "127.0.0.2 lclhst.localdomain lclhst\n");

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "lclhst", 123).send(timeout);
            sup->do_process();
            REQUIRE(sup->responses.size() == 1);

            auto results = sup->responses.at(0)->payload.res->results;
            REQUIRE(results.size() == 1);
            REQUIRE(results.at(0) == asio::ip::make_address("127.0.0.2"));

            // cache hit
            sup->request<payload::address_request_t>(resolver->get_address(), "lclhst", 123).send(timeout);
            sup->do_process();
            REQUIRE(sup->responses.size() == 2);

            results = sup->responses.at(1)->payload.res->results;
            REQUIRE(results.size() == 1);
            REQUIRE(results.at(0) == asio::ip::make_address("127.0.0.2"));
        }
    };
    F().run();
}

void test_success_resolver() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            auto buff = asio::buffer(rx_buff.data(), rx_buff.size());
            remote_resolver.async_receive_from(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) -> void {
                log->info("received {} bytes from resolver", bytes);
                const unsigned char reply[] = {0x0e, 0x51, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
                                               0x00, 0x06, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x03, 0x63, 0x6f,
                                               0x6d, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc0, 0x0c, 0x00, 0x01, 0x00,
                                               0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x04, 0x8e, 0xfa, 0xcb, 0x8e};
                auto reply_str = std::string_view(reinterpret_cast<const char *>(reply), sizeof(reply));
                auto buff = asio::buffer(reply_str.data(), reply_str.size());
                remote_resolver.async_send_to(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) {
                    log->info("sent {} bytes to resolver", bytes);
                });
            });

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "google.com", 80).send(timeout);
            io_ctx.run();
            REQUIRE(sup->responses.size() == 1);

            auto &results = sup->responses.at(0)->payload.res->results;
            REQUIRE(results.size() == 1);
            REQUIRE(results.at(0) == asio::ip::make_address("142.250.203.142"));
        }
    };
    F().run();
}

void test_success_ip() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "127.0.0.1", 80).send(timeout);
            sup->do_process();

            REQUIRE(sup->responses.size() == 1);

            auto &results = sup->responses.at(0)->payload.res->results;
            REQUIRE(results.size() == 1);
            REQUIRE(results.at(0) == asio::ip::make_address("127.0.0.1"));
        }
    };
    F().run();
}

void test_garbage() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            auto buff = asio::buffer(rx_buff.data(), rx_buff.size());
            remote_resolver.async_receive_from(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) -> void {
                log->info("received {} bytes from resolver", bytes);
                const unsigned char reply[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                auto reply_str = std::string_view(reinterpret_cast<const char *>(reply), sizeof(reply));
                auto buff = asio::buffer(reply_str.data(), reply_str.size());
                remote_resolver.async_send_to(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) {
                    log->info("sent {} bytes to resolver", bytes);
                });
            });

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "google.com", 80).send(timeout);
            io_ctx.run();
            REQUIRE(sup->responses.size() == 1);

            auto &ee = sup->responses.at(0)->payload.ee;
            REQUIRE(ee);
            REQUIRE(ee->ec.value() == static_cast<int>(utils::error_code_t::cares_failure));
        }
    };
    F().run();
}

void test_multi_replies() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            auto buff = asio::buffer(rx_buff.data(), rx_buff.size());
            remote_resolver.async_receive_from(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) -> void {
                log->info("received {} bytes from resolver", bytes);
                const unsigned char reply[] = {
                    0x5e, 0x60, 0x81, 0x80, 0x00, 0x01, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x06, 0x72, 0x65, 0x6c,
                    0x61, 0x79, 0x73, 0x09, 0x73, 0x79, 0x6e, 0x63, 0x74, 0x68, 0x69, 0x6e, 0x67, 0x03, 0x6e, 0x65,
                    0x74, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc0, 0x0c, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x05, 0x31,
                    0x00, 0x0d, 0x0a, 0x70, 0x61, 0x72, 0x2d, 0x6b, 0x38, 0x73, 0x2d, 0x76, 0x34, 0xc0, 0x13, 0xc0,
                    0x32, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x67, 0x00, 0x04, 0x33, 0x9f, 0x56, 0xd0, 0xc0,
                    0x32, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x67, 0x00, 0x04, 0x33, 0x9f, 0x4b, 0x11};
                auto reply_str = std::string_view(reinterpret_cast<const char *>(reply), sizeof(reply));
                auto buff = asio::buffer(reply_str.data(), reply_str.size());
                remote_resolver.async_send_to(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) {
                    log->info("sent {} bytes to resolver", bytes);
                });
            });

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "relays.syncthing.net", 80).send(timeout);
            io_ctx.run();
            REQUIRE(sup->responses.size() == 1);

            auto &results = sup->responses.at(0)->payload.res->results;
            REQUIRE(results.size() == 2);
            REQUIRE(results.at(0) == asio::ip::make_address("51.159.86.208"));
            REQUIRE(results.at(1) == asio::ip::make_address("51.159.75.17"));
        }
    };
    F().run();
}

void test_wrong() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            auto buff = asio::buffer(rx_buff.data(), rx_buff.size());
            remote_resolver.async_receive_from(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) -> void {
                log->info("received {} bytes from resolver", bytes);
                const unsigned char reply[] = {0x0e, 0x51, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
                                               0x00, 0x06, 0x67, 0x6f, 0x6f, 0x67, 0x6c, 0x65, 0x03, 0x63, 0x61,
                                               0x6d, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc0, 0x0c, 0x00, 0x01, 0x00,
                                               0x01, 0x00, 0x00, 0x01, 0x02, 0x00, 0x04, 0x8e, 0xfa, 0xcb, 0x8e};
                auto reply_str = std::string_view(reinterpret_cast<const char *>(reply), sizeof(reply));
                auto buff = asio::buffer(reply_str.data(), reply_str.size());
                remote_resolver.async_send_to(buff, resolver_endpoint, [&](sys::error_code ec, size_t bytes) {
                    log->info("sent {} bytes to resolver", bytes);
                });
            });

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "google.com", 80).send(timeout);
            io_ctx.run();
            REQUIRE(sup->responses.size() == 1);

            auto &ee = sup->responses.at(0)->payload.ee;
            REQUIRE(ee);
            REQUIRE(ee->ec.value() == static_cast<int>(utils::error_code_t::cares_failure));
        }
    };
    F().run();
}

void test_timeout() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            sup->request<payload::address_request_t>(resolver->get_address(), "google.com", 80).send(timeout);
            io_ctx.run();
            REQUIRE(sup->responses.size() == 1);

            auto &ee = sup->responses.at(0)->payload.ee;
            REQUIRE(ee);
            REQUIRE(ee->ec.value() == static_cast<int>(r::error_code_t::request_timeout));
        }
    };
    F().run();
}

void test_cancellation() {
    struct F : fixture_t {
        void main() noexcept override {
            auto local_port = remote_resolver.local_endpoint().port();
            write_file(resolv_conf_path, fmt::format("nameserver 127.0.0.1:{}\n", local_port));
            write_file(hosts_path, "");

            resolver = sup->create_actor<resolver_actor_t>()
                           .resolve_timeout(timeout / 2)
                           .hosts_path(hosts_path.c_str())
                           .resolvconf_path(resolv_conf_path.c_str())
                           .timeout(timeout)
                           .finish();

            sup->do_process();

            auto resolver_address = resolver->get_address();
            auto request = sup->request<payload::address_request_t>(resolver_address, "google.com", 80).send(timeout);
            sup->send<message::resolve_cancel_t::payload_t>(resolver_address, request, sup->get_address());

            io_ctx.run();
            REQUIRE(sup->responses.size() == 1);

            auto &ee = sup->responses.at(0)->payload.ee;
            REQUIRE(ee);
            REQUIRE(ee->ec.value() == static_cast<int>(asio::error::operation_aborted));
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_local_resolver, "test_local_resolver", "[resolver]");
    REGISTER_TEST_CASE(test_success_resolver, "test_success_resolver", "[resolver]");
    REGISTER_TEST_CASE(test_success_ip, "test_success_ip", "[resolver]");
    REGISTER_TEST_CASE(test_multi_replies, "test_multi_replies", "[resolver]");
    REGISTER_TEST_CASE(test_garbage, "test_garbage", "[resolver]");
    REGISTER_TEST_CASE(test_wrong, "test_wrong", "[resolver]");
    REGISTER_TEST_CASE(test_timeout, "test_timeout", "[resolver]");
    REGISTER_TEST_CASE(test_cancellation, "test_cancellation", "[resolver]");
    return 1;
}

static int v = _init();
