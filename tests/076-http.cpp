// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"

#include "net/http_actor.h"
#include "net/resolver_actor.h"
#include "net/names.h"
#include "utils/beast_support.h"
#include "utils/format.hpp"
#include <optional>

using namespace std::chrono_literals;

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;
using finish_callback_t = std::function<void()>;
using response_callback_t = std::function<void(message::http_response_t &message)>;
using url_callback_t = std::function<utils::uri_ptr_t(std::string_view)>;
using keep_alive_callback_t = std::function<bool()>;

auto timeout = r::pt::time_duration{r::pt::millisec{200}};
auto host = "127.0.0.1";

struct fixture_t;

struct supervisor_t : ra::supervisor_asio_t {
    using ra::supervisor_asio_t::supervisor_asio_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
            p.set_identity("supervisor", false);
            log = utils::get_logger(identity);
        });
    }

    void shutdown_finish() noexcept override {
        LOG_DEBUG(log, "shutdown_finish()");
        ra::supervisor_asio_t::shutdown_finish();
        if (finish_callback) {
            finish_callback();
        }
    }

    utils::logger_t log;
    configure_callback_t configure_callback;
    finish_callback_t finish_callback;
    fixture_t *fixture;
};
using supervisor_ptr_t = r::intrusive_ptr_t<supervisor_t>;

struct client_actor_t : r::actor_base_t {
    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
            p.set_identity("client", false);
            log = utils::get_logger(identity);
        });
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name("http", http_client, true).link(true); });
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&client_actor_t::on_response); });
    }

    void make_request(std::string_view path) {
        using rx_buff_t = payload::http_request_t::rx_buff_ptr_t;
        static constexpr std::uint32_t rx_buff_size = 1024;

        auto url = url_callback(path);
        auto tx_buff = utils::bytes_t();
        auto keep_alive = keep_alive_callback();

        http::request<http::empty_body> req;
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req.method(http::verb::get);
        req.version(11);
        req.keep_alive(keep_alive);
        req.target(url->encoded_target());
        req.set(http::field::host, url->host());

        auto ok = serialize(req, tx_buff);
        assert(ok);

        auto rx_buff = std::make_shared<rx_buff_t::element_type>(1024);

        http_request = request<payload::http_request_t>(http_client, url, std::move(tx_buff), std::move(rx_buff),
                                                        rx_buff_size, false, true, r::message_ptr_t{})
                           .send(timeout);
    }

    void cancel_request() {
        assert(http_request.has_value());
        send<message::http_cancel_t::payload_t>(http_client, *http_request, get_address());
        http_request.reset();
    }

    void on_response(message::http_response_t &message) noexcept {
        LOG_DEBUG(log, "on_response");
        if (response_callback) {
            response_callback(message);
        }
    }

    r::address_ptr_t http_client;
    utils::logger_t log;
    std::optional<r::request_id_t> http_request;
    url_callback_t url_callback;
    response_callback_t response_callback;
    keep_alive_callback_t keep_alive_callback;
};
using client_actor_ptr_t = r::intrusive_ptr_t<client_actor_t>;

struct fixture_t {
    using acceptor_t = asio::ip::tcp::acceptor;
    using message_opt_t = std::optional<http::message_generator>;

    fixture_t() noexcept : ctx(io_ctx), acceptor(io_ctx), peer_sock(io_ctx) {
        test::init_logging();
        log = utils::get_logger("fixture");
    }

    void run() noexcept {
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {});
        };
        sup->finish_callback = [&]() { finish(); };
        sup->fixture = this;
        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        resolver_actor = make_resolver();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(resolver_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

        http_actor = sup->create_actor<http_actor_t>()
                         .resolve_timeout(timeout / 2)
                         .request_timeout(timeout / 2)
                         .timeout(timeout)
                         .registry_name("http")
                         .keep_alive(request_keep_alive())
                         .finish();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(http_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(host), 0);
        acceptor.open(ep.protocol());
        acceptor.bind(ep);
        acceptor.listen();
        listening_ep = acceptor.local_endpoint();
        log->info("acceptor is listening on {}", listening_ep);
        acceptor.async_accept(peer_sock, [this](auto ec) { on_accept(ec); });

        client_actor = sup->create_actor<client_actor_t>().timeout(timeout).finish();
        client_actor->response_callback = [this](auto &res) { return on_response(res); };
        client_actor->url_callback = [this](auto path) { return make_url(path); };
        client_actor->keep_alive_callback = [this]() { return request_keep_alive(); };
        io_ctx.run_for(1ms);

        sup->do_process();

        main();

        // std::this_thread::sleep_for(1ms);
        sup->do_shutdown();
        sup->do_process();
        io_ctx.run();
        // sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(http_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(resolver_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual utils::uri_ptr_t make_url(std::string_view path) {
        auto url_str = fmt::format("http://{}{}", listening_ep, path);
        auto url = utils::parse(url_str);
        LOG_DEBUG(log, "making request url {}", url);
        return url;
    }

    virtual void finish() {
        LOG_DEBUG(log, "finish");
        auto ec = sys::error_code();
        acceptor.cancel(ec);
        if (ec) {
            LOG_DEBUG(log, "error cancelling acceptor: {}", ec.message());
        }
        peer_sock.cancel(ec);
        if (ec) {
            LOG_DEBUG(log, "error cancelling peer: {}", ec.message());
        }
    }

    virtual bool request_keep_alive() noexcept { return false; }

    virtual void main() noexcept {}

    virtual void on_accept(const sys::error_code &ec) noexcept {
        if (ec) {
            LOG_DEBUG(log, "on_accept, ec: {}", ec.message());
            return;
        }
        LOG_INFO(log, "on_accept, peer = {}", peer_sock.remote_endpoint());
        async_read();
    }

    virtual void async_read() {
        LOG_TRACE(log, "async_read...");
        auto handler = boost::beast::bind_front_handler(&fixture_t::on_read, this);
        http::async_read(peer_sock, rx_buff, request, std::move(handler));
    }

    virtual void on_read(const sys::error_code &ec, std::size_t bytes) noexcept {
        if (ec) {
            LOG_DEBUG(log, "on_read, ec: {}", ec.message());
        } else {
            auto path = request.target();
            LOG_DEBUG(log, "on_read, {} bytes, target = {}", bytes, path);
            auto res_opt = handle_request(path);
            if (res_opt) {
                auto &msg = *res_opt;
                bool keep_alive = msg.keep_alive();
                LOG_DEBUG(log, "on_read, have some responce, going to send it (keep alive = {})", keep_alive);

                auto handler = boost::beast::bind_front_handler(&fixture_t::on_write, this, keep_alive);
                boost::beast::async_write(peer_sock, std::move(msg), std::move(handler));
            }
        }
        rx_buff.clear();
        request.clear();
    }

    virtual void on_write(bool keep_alive, const sys::error_code &ec, std::size_t bytes) noexcept {
        if (ec) {
            LOG_DEBUG(log, "on_read, ec: {}", ec.message());
            return;
        }
        LOG_DEBUG(log, "on_write, {} bytes, keep alive = {}", bytes, keep_alive);
    }

    virtual message_opt_t handle_request(std::string_view path) noexcept {
        if (path.find("/success") != std::string::npos) {
            http::response<http::empty_body> res{http::status::ok, request.version()};
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(request.keep_alive());
            res.prepare_payload();
            return res;
        }
        return {};
    }

    virtual void on_response(message::http_response_t &message) noexcept { LOG_DEBUG(log, "on_response"); }

    virtual r::actor_ptr_t make_resolver() noexcept {
        return sup->create_actor<resolver_actor_t>().resolve_timeout(timeout / 2).timeout(timeout).finish();
    }

    asio::io_context io_ctx{1};
    ra::system_context_asio_t ctx;
    acceptor_t acceptor;
    supervisor_ptr_t sup;
    asio::ip::tcp::endpoint listening_ep;
    utils::logger_t log;
    asio::ip::tcp::socket peer_sock;
    r::actor_ptr_t resolver_actor;
    r::actor_ptr_t http_actor;
    client_actor_ptr_t client_actor;
    http::request<http::string_body> request;
    boost::beast::flat_buffer rx_buff;
};

void test_http_start_and_shutdown() {
    struct F : fixture_t {
        void main() noexcept override {}
    };
    F().run();
}

void test_200_ok() {
    struct F : fixture_t {
        void main() noexcept override {
            client_actor->make_request("/success");
            sup->do_process();
            io_ctx.run();
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            REQUIRE(!ee);

            auto &res = message.payload.res;
            CHECK(res->response.result_int() == 200);
        }
    };
    F().run();
}

void test_200_keep_alive() {
    struct F : fixture_t {
        bool request_keep_alive() noexcept override { return true; }

        void main() noexcept override {
            client_actor->make_request("/success-1");
            sup->do_process();
            io_ctx.run();
            CHECK(responces == 2);
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response ({})", ++responces);
            auto &ee = message.payload.ee;
            REQUIRE(!ee);
            auto &res = message.payload.res;
            CHECK(res->response.result_int() == 200);
            if (responces < 2) {
                async_read();
                client_actor->make_request("/success-2");
            }
        }

        int responces = 0;
    };
    F().run();
}

void test_403_fail() {
    struct F : fixture_t {
        void main() noexcept override {
            client_actor->make_request("/non-authorized");
            sup->do_process();
            io_ctx.run();
        }

        message_opt_t handle_request(std::string_view path) noexcept override {
            if (path == "/non-authorized") {
                http::response<http::empty_body> res{http::status::forbidden, request.version()};
                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::content_type, "text/html");
                res.keep_alive(request.keep_alive());
                res.prepare_payload();
                return res;
            }
            return {};
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &res = message.payload.res;
            CHECK(res->response.result_int() == 403);
        }
    };
    F().run();
}

void test_garbage() {
    struct F : fixture_t {
        void on_read(const sys::error_code &ec, std::size_t bytes) noexcept override {
            if (ec) {
                LOG_DEBUG(log, "on_read, ec: {}", ec.message());
                return;
            }
            auto path = request.target();
            LOG_DEBUG(log, "on_read, {} bytes, target = {}", bytes, path);
            auto buff = asio::buffer("garbage-garbage-garbage!");
            boost::asio::async_write(peer_sock, buff, [this](auto ec, auto bytes) { on_write(false, ec, bytes); });
        }

        void main() noexcept override {
            client_actor->make_request("/garbage");
            sup->do_process();
            io_ctx.run();
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec.message() != "");
        }
    };
    F().run();
}

void test_network_error() {
    struct F : fixture_t {
        void main() noexcept override {
            client_actor->make_request("/bla-bla");
            sup->do_process();
            io_ctx.run();
        }

        utils::uri_ptr_t make_url(std::string_view path) override {
            auto wrong_ep = decltype(listening_ep)(listening_ep.address(), 0);
            auto url_str = fmt::format("http://{}{}", wrong_ep, path);
            auto url = utils::parse(url_str);
            LOG_DEBUG(log, "making request url {}", url);
            return url;
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec.message() != "");
            acceptor.cancel();
        }
    };
    F().run();
}

void test_response_timeout() {
    struct F : fixture_t {
        void main() noexcept override {
            client_actor->make_request("/timeout");
            sup->do_process();
            io_ctx.run();
        }

        message_opt_t handle_request(std::string_view path) noexcept override { return {}; }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec == r::make_error_code(r::error_code_t::request_timeout));
            acceptor.cancel();
        }
    };
    F().run();
}

void test_resolve_timeout() {
    struct F : fixture_t {
        r::actor_ptr_t make_resolver() noexcept override {
            struct sample_resolver_t : r::actor_base_t {
                using r::actor_base_t::actor_base_t;

                void configure(r::plugin::plugin_base_t &plugin) noexcept override {
                    r::actor_base_t::configure(plugin);
                    plugin.with_casted<r::plugin::address_maker_plugin_t>(
                        [&](auto &p) { p.set_identity(names::resolver, false); });
                    plugin.with_casted<r::plugin::registry_plugin_t>(
                        [&](auto &p) { p.register_name(names::resolver, get_address()); });
                }
            };
            return sup->create_actor<sample_resolver_t>().timeout(timeout).finish();
        }

        void main() noexcept override {
            client_actor->make_request("/resolve-timeout");
            sup->do_process();
            io_ctx.run();
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec);
            CHECK(ee->ec == r::make_error_code(r::error_code_t::request_timeout));
            acceptor.cancel();
        }
    };
    F().run();
}

void test_resolve_fail() {
    static constexpr auto err = (int)std::errc::address_family_not_supported;
    struct F : fixture_t {
        r::actor_ptr_t make_resolver() noexcept override {
            struct sample_resolver_t : r::actor_base_t {
                using r::actor_base_t::actor_base_t;

                void configure(r::plugin::plugin_base_t &plugin) noexcept override {
                    r::actor_base_t::configure(plugin);
                    plugin.with_casted<r::plugin::address_maker_plugin_t>(
                        [&](auto &p) { p.set_identity(names::resolver, false); });
                    plugin.with_casted<r::plugin::registry_plugin_t>(
                        [&](auto &p) { p.register_name(names::resolver, get_address()); });
                    plugin.with_casted<r::plugin::starter_plugin_t>(
                        [&](auto &p) { p.subscribe_actor(&sample_resolver_t::on_request); });
                }

                void on_request(message::resolve_request_t &req) noexcept {
                    auto ec = std::error_code(err, std::system_category());
                    reply_with_error(req, make_error(ec));
                }
            };
            return sup->create_actor<sample_resolver_t>().timeout(timeout).finish();
        }

        void main() noexcept override {
            client_actor->make_request("/resolve-timeout");
            sup->do_process();
            io_ctx.run();
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec == std::error_code(err, std::system_category()));
            acceptor.cancel();
        }
    };
    F().run();
}

void test_cancellation_1() {
    struct F : fixture_t {
        void main() noexcept override {
            client_actor->make_request("/cancellable");
            sup->do_process();
            io_ctx.run();
        }

        message_opt_t handle_request(std::string_view path) noexcept override {
            client_actor->cancel_request();
            return {};
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec.message() != "");
        }
    };
    F().run();
}

void test_cancellation_2() {
    struct F : fixture_t {
        void on_accept(const sys::error_code &ec) noexcept override {
            client_actor->cancel_request();
            LOG_INFO(log, "on_accept, cancelling request...");
            if (ec) {
                LOG_DEBUG(log, "on_accept, ec: {}", ec.message());
                return;
            }
            LOG_INFO(log, "on_accept, peer = {}", peer_sock.remote_endpoint());
            async_read();
        }

        void main() noexcept override {
            client_actor->make_request("/success-cancellable");
            sup->do_process();
            io_ctx.run();
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec.message() != "");
            peer_sock.cancel();
        }
    };
    F().run();
}

void test_cancellation_3() {
    struct F : fixture_t {
        r::actor_ptr_t make_resolver() noexcept override {
            struct sample_resolver_t : r::actor_base_t {
                using r::actor_base_t::actor_base_t;

                void configure(r::plugin::plugin_base_t &plugin) noexcept override {
                    r::actor_base_t::configure(plugin);
                    plugin.with_casted<r::plugin::address_maker_plugin_t>(
                        [&](auto &p) { p.set_identity(names::resolver, false); });
                    plugin.with_casted<r::plugin::registry_plugin_t>(
                        [&](auto &p) { p.register_name(names::resolver, get_address()); });
                    plugin.with_casted<r::plugin::starter_plugin_t>(
                        [&](auto &p) { p.subscribe_actor(&sample_resolver_t::on_request); });
                }

                void on_request(message::resolve_request_t &req) noexcept {
                    auto &sup = static_cast<supervisor_t &>(get_supervisor());
                    sup.fixture->client_actor->cancel_request();
                }
            };
            return sup->create_actor<sample_resolver_t>().timeout(timeout).finish();
        }

        void main() noexcept override {
            client_actor->make_request("/success-cancellable");
            sup->do_process();
            io_ctx.run();
        }

        void on_response(message::http_response_t &message) noexcept override {
            LOG_DEBUG(log, "on_response");
            auto &ee = message.payload.ee;
            CHECK(ee);
            CHECK(ee->ec);
            CHECK(ee->ec.message() != "");
            acceptor.cancel();
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_http_start_and_shutdown, "test_http_start_and_shutdown", "[http]");
    REGISTER_TEST_CASE(test_200_ok, "test_200_ok", "[http]");
    REGISTER_TEST_CASE(test_200_keep_alive, "test_200_keep_alive", "[http]");
    REGISTER_TEST_CASE(test_403_fail, "test_403_fail", "[http]");
    REGISTER_TEST_CASE(test_garbage, "test_garbage", "[http]");
    REGISTER_TEST_CASE(test_network_error, "test_network_error", "[http]");
    REGISTER_TEST_CASE(test_response_timeout, "test_response_timeout", "[http]");
    REGISTER_TEST_CASE(test_resolve_timeout, "test_resolve_timeout", "[http]");
    REGISTER_TEST_CASE(test_resolve_fail, "test_resolve_fail", "[http]");
    REGISTER_TEST_CASE(test_cancellation_1, "test_cancellation_1", "[http]");
    REGISTER_TEST_CASE(test_cancellation_2, "test_cancellation_2", "[http]");
    REGISTER_TEST_CASE(test_cancellation_3, "test_cancellation_3", "[http]");
    return 1;
}

static int v = _init();
