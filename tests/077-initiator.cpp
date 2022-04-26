#include "catch.hpp"
#include "test-utils.h"
#include "access.h"

#include "utils/tls.h"
#include "model/cluster.h"
#include "net/names.h"
#include "net/initiator_actor.h"
#include "net/resolver_actor.h"
#include "transport/stream.h"
#include <rotor/asio.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace r = rotor;
namespace ra = r::asio;

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;

auto timeout = r::pt::time_duration{r::pt::millisec{1500}};
auto host = "127.0.0.1";

struct supervisor_t : ra::supervisor_asio_t {
    using ra::supervisor_asio_t::supervisor_asio_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        ra::supervisor_asio_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.register_name(names::coordinator, get_address()); });
        if (configure_callback) {
            configure_callback(plugin);
        }
    }

    void shutdown_finish() noexcept override {
        ra::supervisor_asio_t::shutdown_finish();
        if (acceptor) {
            acceptor->cancel();
        }
    }

    auto get_state() noexcept { return state; }

    asio::ip::tcp::acceptor *acceptor = nullptr;
    configure_callback_t configure_callback;
};

using supervisor_ptr_t = r::intrusive_ptr_t<supervisor_t>;
using actor_ptr_t = r::intrusive_ptr_t<initiator_actor_t>;

struct fixture_t {
    using acceptor_t = asio::ip::tcp::acceptor;
    using msg_ptr_t = r::intrusive_ptr_t<message::peer_connected_t>;

    fixture_t() noexcept : ctx(io_ctx), acceptor(io_ctx), peer_sock(io_ctx) {
        utils::set_default("trace");
        log = utils::get_logger("fixture");
    }

    void run() noexcept {
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using msg_t = message::peer_connected_t;
                p.subscribe_actor(r::lambda<msg_t>([&](msg_t &msg) {
                    message = &msg;
                    LOG_INFO(log, "received message::peer_connected_t");
                }));
            });
        };
        sup->start();

        sup->create_actor<resolver_actor_t>().resolve_timeout(timeout / 2).timeout(timeout).finish();
        sup->do_process();

        auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(host), 0);
        acceptor.open(ep.protocol());
        acceptor.bind(ep);
        acceptor.listen();
        listening_ep = acceptor.local_endpoint();
        peer_uri = utils::parse(get_uri(listening_ep)).value();
        log->debug("listening on {}", peer_uri.full);
        initiate_accept();

        my_keys = utils::generate_pair("me").value();
        peer_keys = utils::generate_pair("peer").value();

        my_device = model::device_id_t::from_cert(my_keys.cert_data).value();
        peer_device = model::device_id_t::from_cert(peer_keys.cert_data).value();

        main();
    }

    virtual void initiate_accept() noexcept {
        acceptor.async_accept(peer_sock, [this](auto ec) { this->accept(ec); });
        sup->acceptor = &acceptor;
    }

    virtual std::string get_uri(const asio::ip::tcp::endpoint &endpoint) noexcept {
        return fmt::format("tcp://{}", listening_ep);
    }

    virtual void accept(const sys::error_code &ec) noexcept {
        LOG_INFO(log, "accept, ec: {}", ec.message());
        peer_trans = transport::initiate_tls_passive(*sup, peer_keys, std::move(peer_sock));
        initiate_peer_handshake();
    }

    virtual void initiate_peer_handshake() noexcept {
        transport::handshake_fn_t handshake_fn = [this](bool valid_peer, utils::x509_t &cert,
                                                        const tcp::endpoint &peer_endpoint,
                                                        const model::device_id_t *peer_device) {
            valid_handshake = true;
            LOG_INFO(log, "peer handshake");
        };
        transport::error_fn_t on_error = [](const auto &) {};
        peer_trans->async_handshake(handshake_fn, on_error);
    }

    void initiate_active() noexcept {
        tcp::resolver resolver(io_ctx);
        auto addresses = resolver.resolve(host, std::to_string(listening_ep.port()));
        peer_trans = transport::initiate_tls_active(*sup, peer_keys, my_device, peer_uri);

        transport::error_fn_t on_error = [&](auto &ec) {
            LOG_WARN(log, "initiate_active/connect, err: {}", ec.message());
        };
        transport::connect_fn_t on_connect = [&](auto arg) {
            LOG_INFO(log, "initiate_active/peer connect");
            active_connect();
        };

        peer_trans->async_connect(addresses, on_connect, on_error);
    }

    virtual void active_connect() {
        transport::handshake_fn_t handshake_fn = [this](bool valid_peer, utils::x509_t &cert,
                                                        const tcp::endpoint &peer_endpoint,
                                                        const model::device_id_t *peer_device) {
            valid_handshake = true;
            LOG_INFO(log, "test_passive_success/peer handshake");
        };
        transport::error_fn_t on_hs_error = [&](const auto &ec) {
            LOG_WARN(log, "test_passive_success/peer handshake, err: {}", ec.message());
        };
        peer_trans->async_handshake(handshake_fn, on_hs_error);
    }

    virtual void main() noexcept {}

    virtual actor_ptr_t create_actor() noexcept {
        return sup->create_actor<initiator_actor_t>()
            .timeout(timeout)
            .peer_device_id(peer_device)
            .uris({peer_uri})
            .ssl_pair(&my_keys)
            .escalate_failure()
            .finish();
    }

    virtual actor_ptr_t create_passive_actor() noexcept {
        return sup->create_actor<initiator_actor_t>()
            .timeout(timeout)
            .sock(std::move(peer_sock))
            .ssl_pair(&my_keys)
            .escalate_failure()
            .finish();
    }

    asio::io_context io_ctx{1};
    ra::system_context_asio_t ctx;
    acceptor_t acceptor;
    supervisor_ptr_t sup;
    asio::ip::tcp::endpoint listening_ep;
    utils::logger_t log;
    asio::ip::tcp::socket peer_sock;
    config::bep_config_t bep_config;
    utils::key_pair_t my_keys;
    utils::key_pair_t peer_keys;
    utils::URI peer_uri;
    model::device_id_t my_device;
    model::device_id_t peer_device;
    transport::stream_sp_t peer_trans;
    msg_ptr_t message;
    bool valid_handshake = false;
};

void test_connect_timeout() {
    struct F : fixture_t {
        void initiate_accept() noexcept override {}
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!message);
        }
    };
    F().run();
}

void test_handshake_timeout() {
    struct F : fixture_t {

        void accept(const sys::error_code &ec) noexcept override { LOG_INFO(log, "accept (ignoring)", ec.message()); }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!message);
        }
    };
    F().run();
}

void test_connection_refused() {
    struct F : fixture_t {

        std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override {
            return fmt::format("tcp://{}:0", host);
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!message);
        }
    };
    F().run();
}

void test_resolve_failure() {
    struct F : fixture_t {

        std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override { return "tcp://x.example.com"; }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!message);
        }
    };
    F().run();
}

void test_success() {
    struct F : fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            CHECK(message);
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }
    };
    F().run();
}

void test_passive_success() {
    struct F : fixture_t {

        actor_ptr_t act;

        void accept(const sys::error_code &ec) noexcept override {
            LOG_INFO(log, "test_passive_success/accept, ec: {}", ec.message());
            act = create_passive_actor();
        }

        void main() noexcept override {
            initiate_active();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            CHECK(message);
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }
    };
    F().run();
}

void test_passive_timeout() {
    struct F : fixture_t {

        actor_ptr_t act;

        void accept(const sys::error_code &ec) noexcept override {
            LOG_INFO(log, "test_passive_timeout/accept, ec: {}", ec.message());
            act = create_passive_actor();
        }

        void active_connect() noexcept override { LOG_INFO(log, "test_passive_timeout/active_connect NOOP"); }

        void main() noexcept override {
            initiate_active();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!message);
        }
    };
    F().run();
}

REGISTER_TEST_CASE(test_connect_timeout, "test_connect_timeout", "[initiator]");
REGISTER_TEST_CASE(test_handshake_timeout, "test_handshake_timeout", "[initiator]");
REGISTER_TEST_CASE(test_connection_refused, "test_connection_refused", "[initiator]");
REGISTER_TEST_CASE(test_resolve_failure, "test_resolve_failure", "[initiator]");
REGISTER_TEST_CASE(test_success, "test_success", "[initiator]");
REGISTER_TEST_CASE(test_passive_success, "test_passive_success", "[initiator]");
REGISTER_TEST_CASE(test_passive_timeout, "test_passive_timeout", "[initiator]");
