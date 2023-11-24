// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"

#include "utils/tls.h"
#include "utils/format.hpp"
#include "model/cluster.h"
#include "model/messages.h"
#include "net/names.h"
#include "net/initiator_actor.h"
#include "net/resolver_actor.h"
#include "proto/relay_support.h"
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
using finish_callback_t = std::function<void()>;

auto timeout = r::pt::time_duration{r::pt::millisec{2000}};
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
        if (finish_callback) {
            finish_callback();
        }
    }

    auto get_state() noexcept { return state; }

    finish_callback_t finish_callback;
    configure_callback_t configure_callback;
};

using supervisor_ptr_t = r::intrusive_ptr_t<supervisor_t>;
using actor_ptr_t = r::intrusive_ptr_t<initiator_actor_t>;

struct fixture_t {
    using acceptor_t = asio::ip::tcp::acceptor;
    using ready_ptr_t = r::intrusive_ptr_t<net::message::peer_connected_t>;
    using diff_ptr_t = r::intrusive_ptr_t<model::message::model_update_t>;
    using diff_msgs_t = std::vector<diff_ptr_t>;

    fixture_t() noexcept : ctx(io_ctx), acceptor(io_ctx), peer_sock(io_ctx) {
        utils::set_default("trace");
        log = utils::get_logger("fixture");
    }

    virtual void finish() {
        acceptor.cancel();
        if (peer_trans) {
            peer_trans->cancel();
        }
    }

    void run() noexcept {
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using connected_t = typename ready_ptr_t::element_type;
                using diff_t = typename diff_ptr_t::element_type;
                p.subscribe_actor(r::lambda<connected_t>([&](connected_t &msg) {
                    connected_message = &msg;
                    LOG_INFO(log, "received message::peer_connected_t");
                }));
                p.subscribe_actor(r::lambda<diff_t>([&](diff_t &msg) {
                    diff_msgs.emplace_back(&msg);
                    LOG_INFO(log, "received diff message");
                }));
            });
        };
        sup->finish_callback = [&]() { finish(); };
        sup->start();

        sup->create_actor<resolver_actor_t>().resolve_timeout(timeout / 2).timeout(timeout).finish();
        sup->do_process();

        my_keys = utils::generate_pair("me").value();
        peer_keys = utils::generate_pair("peer").value();

        auto md = model::device_id_t::from_cert(my_keys.cert_data).value();
        auto pd = model::device_id_t::from_cert(peer_keys.cert_data).value();

        my_device = device_t::create(md, "my-device").value();
        peer_device = device_t::create(pd, "peer-device").value();

        auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(host), 0);
        acceptor.open(ep.protocol());
        acceptor.bind(ep);
        acceptor.listen();
        listening_ep = acceptor.local_endpoint();
        peer_uri = utils::parse(get_uri(listening_ep)).value();
        log->debug("listening on {}", peer_uri.full);
        initiate_accept();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        main();
    }

    virtual void initiate_accept() noexcept {
        acceptor.async_accept(peer_sock, [this](auto ec) { this->accept(ec); });
    }

    virtual std::string get_uri(const asio::ip::tcp::endpoint &) noexcept {
        return fmt::format("tcp://{}", listening_ep);
    }

    virtual void accept(const sys::error_code &ec) noexcept {
        LOG_INFO(log, "accept, ec: {}", ec.message());
        peer_trans = transport::initiate_tls_passive(*sup, peer_keys, std::move(peer_sock));
        initiate_peer_handshake();
    }

    virtual void initiate_peer_handshake() noexcept {
        transport::handshake_fn_t handshake_fn = [this](bool valid_peer, utils::x509_t &, const tcp::endpoint &,
                                                        const model::device_id_t *) {
            valid_handshake = valid_peer;
            on_peer_hanshake();
        };
        transport::error_fn_t on_error = [](const auto &) {};
        peer_trans->async_handshake(handshake_fn, on_error);
    }

    virtual void on_peer_hanshake() noexcept { LOG_INFO(log, "peer handshake"); }

    void initiate_active() noexcept {
        tcp::resolver resolver(io_ctx);
        auto addresses = resolver.resolve(host, std::to_string(listening_ep.port()));
        peer_trans = transport::initiate_tls_active(*sup, peer_keys, my_device->device_id(), peer_uri);

        transport::error_fn_t on_error = [&](auto &ec) {
            LOG_WARN(log, "initiate_active/connect, err: {}", ec.message());
        };
        transport::connect_fn_t on_connect = [&](auto) {
            LOG_INFO(log, "initiate_active/peer connect");
            active_connect();
        };

        peer_trans->async_connect(addresses, on_connect, on_error);
    }

    virtual void active_connect() {
        LOG_TRACE(log, "active_connect");
        transport::handshake_fn_t handshake_fn = [this](bool, utils::x509_t &, const tcp::endpoint &,
                                                        const model::device_id_t *) {
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
            .peer_device_id(peer_device->device_id())
            .relay_session(relay_session)
            .relay_enabled(true)
            .uris({peer_uri})
            .cluster(use_model ? cluster : nullptr)
            .sink(sup->get_address())
            .ssl_pair(&my_keys)
            .router(*sup)
            .escalate_failure()
            .finish();
    }

    virtual actor_ptr_t create_passive_actor() noexcept {
        return sup->create_actor<initiator_actor_t>()
            .timeout(timeout)
            .sock(std::move(peer_sock))
            .ssl_pair(&my_keys)
            .router(*sup)
            .cluster(cluster)
            .sink(sup->get_address())
            .escalate_failure()
            .finish();
    }

    cluster_ptr_t cluster;
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
    model::device_ptr_t my_device;
    model::device_ptr_t peer_device;
    transport::stream_sp_t peer_trans;
    ready_ptr_t connected_message;
    diff_msgs_t diff_msgs;
    std::string relay_session;
    bool use_model = true;

    bool valid_handshake = false;
};

void test_connect_timeout() {
    struct F : fixture_t {
        void initiate_accept() noexcept override {}
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
        }
    };
    F().run();
}

void test_connect_unsupproted_proto() {
    struct F : fixture_t {
        std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override {
            return fmt::format("xxx://{}", listening_ep);
        }
        void main() noexcept override {
            create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
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
            CHECK(!connected_message);
            REQUIRE(diff_msgs.size() == 2);
            CHECK(diff_msgs[0]->payload.diff->apply(*cluster));
            CHECK(peer_device->get_state() == device_state_t::dialing);
            CHECK(diff_msgs[1]->payload.diff->apply(*cluster));
            CHECK(peer_device->get_state() == device_state_t::offline);
        }
    };
    F().run();
}

void test_handshake_garbage() {
    struct F : fixture_t {

        void accept(const sys::error_code &) noexcept override {
            auto buff = asio::buffer("garbage-garbage-garbage");
            peer_sock.write_some(buff);
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            REQUIRE(diff_msgs.size() == 2);
            CHECK(diff_msgs[0]->payload.diff->apply(*cluster));
            CHECK(peer_device->get_state() == device_state_t::dialing);
            CHECK(diff_msgs[1]->payload.diff->apply(*cluster));
            CHECK(peer_device->get_state() == device_state_t::offline);
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
            CHECK(!connected_message);
        }
    };
    F().run();
}

void test_connection_refused_no_model() {
    struct F : fixture_t {
        F() { use_model = false; }

        std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override {
            return fmt::format("tcp://{}:0", host);
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
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
            CHECK(!connected_message);
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
            REQUIRE(connected_message);
            CHECK(connected_message->payload.proto == "tcp");
            CHECK(connected_message->payload.peer_device_id == peer_device->device_id());
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            REQUIRE(diff_msgs.size() == 1);
            CHECK(diff_msgs[0]->payload.diff->apply(*cluster));
            CHECK(peer_device->get_state() == device_state_t::dialing);
        }
    };
    F().run();
}

void test_success_no_model() {
    struct F : fixture_t {
        F() { use_model = false; }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            CHECK(connected_message);
            CHECK(connected_message->payload.peer_device_id == peer_device->device_id());
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            REQUIRE(diff_msgs.size() == 0);
        }
    };
    F().run();
}

struct passive_fixture_t : fixture_t {
    actor_ptr_t act;
    bool active_connect_invoked = false;

    void active_connect() override {
        LOG_TRACE(log, "active_connect");
        if (!act || active_connect_invoked) {
            return;
        }
        active_connect_invoked = true;
        active_connect_impl();
    }

    virtual void active_connect_impl() { fixture_t::active_connect(); }

    void accept(const sys::error_code &ec) noexcept override {
        LOG_INFO(log, "test_passive_success/accept, ec: {}", ec.message());
        act = create_passive_actor();
        sup->do_process();
        active_connect();
    }
};

void test_passive_success() {
    struct F : passive_fixture_t {
        void main() noexcept override {
            initiate_active();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            REQUIRE(connected_message);
            CHECK(connected_message->payload.proto == "tcp");
            CHECK(connected_message->payload.peer_device_id == peer_device->device_id());
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }
    };
    F().run();
}

void test_passive_garbage() {
    struct F : passive_fixture_t {

        tcp::socket client_sock;
        tcp::resolver::results_type addresses;

        F() : client_sock{io_ctx} {}

        void active_connect_impl() noexcept override {
            tcp::resolver resolver(io_ctx);
            addresses = resolver.resolve(host, std::to_string(listening_ep.port()));
            asio::async_connect(client_sock, addresses.begin(), addresses.end(), [&](auto ec, auto) {
                LOG_INFO(log, "test_passive_garbage/peer connect, ec: {}", ec.message());
                auto buff = asio::buffer("garbage-garbage-garbage");
                client_sock.write_some(buff);
                sup->do_process();
            });
        }

        void main() noexcept override {
            initiate_active();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
        }
    };
    F().run();
}

void test_passive_timeout() {
    struct F : passive_fixture_t {

        void active_connect() noexcept override { LOG_INFO(log, "test_passive_timeout/active_connect NOOP"); }

        void main() noexcept override {
            initiate_active();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
        }
    };
    F().run();
}

struct passive_relay_fixture_t : fixture_t {
    std::string rx_buff;
    bool initiate_handshake = true;
    passive_relay_fixture_t() {
        relay_session = "relay-session-key";
        rx_buff.resize(128);
    }

    void on_read(size_t bytes) noexcept {
        LOG_TRACE(log, "read (relay/passive), {} bytes", bytes);
        auto r = proto::relay::parse({rx_buff.data(), bytes});
        auto &wrapped = std::get<proto::relay::wrapped_message_t>(r);
        auto &msg = std::get<proto::relay::join_session_request_t>(wrapped.message);
        CHECK(msg.key == relay_session);
        relay_reply();
    }

    virtual void on_write(size_t bytes) noexcept {
        LOG_TRACE(log, "write (relay/passive), {} bytes", bytes);

        if (initiate_handshake) {
            auto upgradeable = static_cast<transport::upgradeable_stream_base_t *>(peer_trans.get());
            auto ssl = transport::ssl_junction_t{my_device->device_id(), &peer_keys, false, "bep"};
            peer_trans = upgradeable->upgrade(ssl, true);
            initiate_peer_handshake();
        }
    }

    virtual void relay_reply() noexcept { write(proto::relay::response_t{0, "success"}); }

    virtual void write(const proto::relay::message_t &msg) noexcept {
        proto::relay::serialize(msg, rx_buff);
        transport::error_fn_t err_fn([&](auto ec) { log->error("(relay/passive), read_err: {}", ec.message()); });
        transport::io_fn_t write_fn = [this](size_t bytes) { on_write(bytes); };
        peer_trans->async_send(asio::buffer(rx_buff), write_fn, err_fn);
    }

    void accept(const sys::error_code &ec) noexcept override {
        LOG_INFO(log, "accept (relay/passive), ec: {}", ec.message());
        auto uri = utils::parse("tcp://127.0.0.1:0/").value();
        auto cfg = transport::transport_config_t{{}, uri, *sup, std::move(peer_sock), false};
        peer_trans = transport::initiate_stream(cfg);

        transport::error_fn_t read_err_fn([&](auto ec) { log->error("(relay/passive), read_err: {}", ec.message()); });
        transport::io_fn_t read_fn = [this](size_t bytes) { on_read(bytes); };
        peer_trans->async_recv(asio::buffer(rx_buff), read_fn, read_err_fn);
    }
};

void test_relay_passive_success() {
    struct F : passive_relay_fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            REQUIRE(connected_message);
            CHECK(connected_message->payload.proto == "relay");
            CHECK(connected_message->payload.peer_device_id == peer_device->device_id());
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 0);
        }
    };
    F().run();
}

void test_relay_passive_gargabe() {
    struct F : passive_relay_fixture_t {

        void write(const proto::relay::message_t &) noexcept override {
            rx_buff = "garbage-garbage-garbae";
            initiate_handshake = false;
            transport::error_fn_t err_fn([&](auto ec) { log->error("(relay/passive), read_err: {}", ec.message()); });
            transport::io_fn_t write_fn = [this](size_t bytes) { on_write(bytes); };
            peer_trans->async_send(asio::buffer(rx_buff), write_fn, err_fn);
        }

        void main() noexcept override {
            create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            CHECK(!valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 0);
        }
    };
    F().run();
}

void test_relay_passive_wrong_message() {
    struct F : passive_relay_fixture_t {

        void relay_reply() noexcept override { write(proto::relay::pong_t{}); }

        void main() noexcept override {
            initiate_handshake = false;
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            CHECK(!valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 0);
        }
    };
    F().run();
}

void test_relay_passive_unsuccessful_join() {
    struct F : passive_relay_fixture_t {

        void relay_reply() noexcept override { write(proto::relay::response_t{5, "some-fail-reason"}); }

        void main() noexcept override {
            initiate_handshake = false;
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            CHECK(!valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 0);
        }
    };
    F().run();
}

void test_relay_malformed_uri() {
    struct F : fixture_t {
        std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override {
            return fmt::format("relay://{}", listening_ep);
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            CHECK(!valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

void test_relay_active_wrong_relay_deviceid() {
    struct F : fixture_t {

        std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override {
            return fmt::format("relay://{}?id={}", listening_ep, my_device->device_id().get_value());
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            CHECK(!valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

struct active_relay_fixture_t : fixture_t {
    utils::key_pair_t relay_keys;
    model::device_id_t relay_device;
    std::string rx_buff;
    std::string session_key = "lorem-session-dolor";
    transport::stream_sp_t relay_trans;
    bool session_mode = false;

    active_relay_fixture_t() {
        relay_keys = utils::generate_pair("relay").value();
        relay_device = model::device_id_t::from_cert(relay_keys.cert_data).value();
        rx_buff.resize(128);
    }

    std::string get_uri(const asio::ip::tcp::endpoint &) noexcept override {
        return fmt::format("relay://{}?id={}", listening_ep, relay_device.get_value());
    }

    void accept(const sys::error_code &ec) noexcept override {
        LOG_INFO(log, "relay/accept, ec: {}", ec.message());
        if (!session_mode) {
            relay_trans = transport::initiate_tls_passive(*sup, relay_keys, std::move(peer_sock));
            transport::handshake_fn_t handshake_fn = [this](bool valid_peer, utils::x509_t &, const tcp::endpoint &,
                                                            const model::device_id_t *) {
                valid_handshake = valid_peer;
                on_relay_hanshake();
            };
            transport::error_fn_t on_error = [](const auto &) {};
            relay_trans->async_handshake(handshake_fn, on_error);
            return;
        }
        auto uri = utils::parse("tcp://127.0.0.1:0/").value();
        auto cfg = transport::transport_config_t{{}, uri, *sup, std::move(peer_sock), false};
        peer_trans = transport::initiate_stream(cfg);

        transport::error_fn_t read_err_fn([&](auto ec) { log->error("(relay/active), read_err: {}", ec.message()); });
        transport::io_fn_t read_fn = [this](size_t bytes) { on_read_peer(bytes); };
        peer_trans->async_recv(asio::buffer(rx_buff), read_fn, read_err_fn);
    }

    virtual void on_relay_hanshake() noexcept {
        transport::error_fn_t read_err_fn([&](auto ec) { log->error("(relay/active), read_err: {}", ec.message()); });
        transport::io_fn_t read_fn = [this](size_t bytes) { on_read(bytes); };
        relay_trans->async_recv(asio::buffer(rx_buff), read_fn, read_err_fn);
    }

    virtual void relay_reply() noexcept {
        write(relay_trans, proto::relay::session_invitation_t{std::string(peer_device->device_id().get_sha256()),
                                                              session_key, "", listening_ep.port(), false});
    }

    virtual void session_reply() noexcept { write(peer_trans, proto::relay::response_t{0, "ok"}); }

    virtual void write(transport::stream_sp_t &stream, const proto::relay::message_t &msg) noexcept {
        proto::relay::serialize(msg, rx_buff);
        transport::error_fn_t err_fn([&](auto ec) { log->error("(relay/passive), read_err: {}", ec.message()); });
        transport::io_fn_t write_fn = [this](size_t bytes) { on_write(bytes); };
        stream->async_send(asio::buffer(rx_buff), write_fn, err_fn);
    }

    virtual void on_read_peer(size_t bytes) {
        log->debug("(relay/active) read peer {} bytes", bytes);
        auto r = proto::relay::parse({rx_buff.data(), bytes});
        auto &wrapped = std::get<proto::relay::wrapped_message_t>(r);
        auto &msg = std::get<proto::relay::join_session_request_t>(wrapped.message);
        CHECK(msg.key == session_key);
        session_reply();
    }

    virtual void on_read(size_t bytes) {
        log->debug("(relay/active) read {} bytes", bytes);
        auto r = proto::relay::parse({rx_buff.data(), bytes});
        auto &wrapped = std::get<proto::relay::wrapped_message_t>(r);
        auto &msg = std::get<proto::relay::connect_request_t>(wrapped.message);
        CHECK(msg.device_id == peer_device->device_id().get_sha256());
        relay_reply();
    }

    virtual void on_write(size_t bytes) {
        log->debug("(relay/active) write {} bytes", bytes);
        if (!session_mode) {
            acceptor.async_accept(peer_sock, [this](auto ec) { this->accept(ec); });
            session_mode = true;
        } else {
            auto upgradeable = static_cast<transport::upgradeable_stream_base_t *>(peer_trans.get());
            auto ssl = transport::ssl_junction_t{my_device->device_id(), &peer_keys, false, "bep"};
            peer_trans = upgradeable->upgrade(ssl, false);
            initiate_peer_handshake();
        }
    }
};

void test_relay_active_success() {
    struct F : active_relay_fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            REQUIRE(connected_message);
            CHECK(connected_message->payload.proto == "relay");
            CHECK(connected_message->payload.peer_device_id == peer_device->device_id());
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            REQUIRE(diff_msgs.size() == 1);
            CHECK(diff_msgs[0]->payload.diff->apply(*cluster));
            CHECK(peer_device->get_state() == device_state_t::dialing);
        }
    };
    F().run();
}

void test_relay_active_not_enabled() {
    struct F : active_relay_fixture_t {

        actor_ptr_t create_actor() noexcept override {
            return sup->create_actor<initiator_actor_t>()
                .timeout(timeout)
                .peer_device_id(peer_device->device_id())
                .relay_session(relay_session)
                .uris({peer_uri})
                .cluster(use_model ? cluster : nullptr)
                .sink(sup->get_address())
                .ssl_pair(&my_keys)
                .router(*sup)
                .escalate_failure()
                .finish();
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(peer_device->get_state() == device_state_t::offline);
        }
    };
    F().run();
}

void test_relay_wrong_device() {
    struct F : active_relay_fixture_t {

        void relay_reply() noexcept override {
            write(relay_trans, proto::relay::session_invitation_t{std::string(relay_device.get_sha256()), session_key,
                                                                  "", listening_ep.port(), false});
        }
        void on_write(size_t) override {}

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            CHECK(valid_handshake);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

void test_relay_non_conneteable() {
    struct F : active_relay_fixture_t {

        void relay_reply() noexcept override {
            write(relay_trans, proto::relay::session_invitation_t{std::string(peer_device->device_id().get_sha256()),
                                                                  session_key, "", 0, false});
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

void test_relay_malformed_address() {
    struct F : active_relay_fixture_t {

        void relay_reply() noexcept override {
            write(relay_trans, proto::relay::session_invitation_t{std::string(peer_device->device_id().get_sha256()),
                                                                  session_key, "8.8.8.8z", listening_ep.port(), false});
        }

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

void test_relay_garbage_reply() {
    struct F : active_relay_fixture_t {

        void write(transport::stream_sp_t &stream, const proto::relay::message_t &) noexcept override {
            rx_buff = "garbage-garbage-garbage";
            transport::error_fn_t err_fn([&](auto ec) { log->error("(relay/passive), read_err: {}", ec.message()); });
            transport::io_fn_t write_fn = [this](size_t bytes) { on_write(bytes); };
            stream->async_send(asio::buffer(rx_buff), write_fn, err_fn);
        }

        void on_write(size_t) override {}

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

void test_relay_noninvitation_reply() {
    struct F : active_relay_fixture_t {

        void relay_reply() noexcept override { write(relay_trans, proto::relay::pong_t{}); }
        void on_write(size_t) override {}

        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(!connected_message);
            sup->do_shutdown();
            sup->do_process();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
            CHECK(diff_msgs.size() == 2);
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_connect_unsupproted_proto, "test_connect_unsupproted_proto", "[initiator]");
    REGISTER_TEST_CASE(test_connect_timeout, "test_connect_timeout", "[initiator]");
    REGISTER_TEST_CASE(test_handshake_timeout, "test_handshake_timeout", "[initiator]");
    REGISTER_TEST_CASE(test_handshake_garbage, "test_handshake_garbage", "[initiator]");
    REGISTER_TEST_CASE(test_connection_refused, "test_connection_refused", "[initiator]");
    REGISTER_TEST_CASE(test_connection_refused_no_model, "test_connection_refused_no_model", "[initiator]");
    REGISTER_TEST_CASE(test_resolve_failure, "test_resolve_failure", "[initiator]");
    REGISTER_TEST_CASE(test_success, "test_success", "[initiator]");
    REGISTER_TEST_CASE(test_success_no_model, "test_success_no_model", "[initiator]");
    REGISTER_TEST_CASE(test_passive_success, "test_passive_success", "[initiator]");
    REGISTER_TEST_CASE(test_passive_garbage, "test_passive_garbage", "[initiator]");
    REGISTER_TEST_CASE(test_passive_timeout, "test_passive_timeout", "[initiator]");
    REGISTER_TEST_CASE(test_relay_passive_success, "test_relay_passive_success", "[initiator]");
    REGISTER_TEST_CASE(test_relay_passive_gargabe, "test_relay_passive_gargabe", "[initiator]");
    REGISTER_TEST_CASE(test_relay_passive_wrong_message, "test_relay_passive_wrong_message", "[initiator]");
    REGISTER_TEST_CASE(test_relay_passive_unsuccessful_join, "test_relay_passive_unsuccessful_join", "[initiator]");
    REGISTER_TEST_CASE(test_relay_malformed_uri, "test_relay_malformed_uri", "[initiator]");
    REGISTER_TEST_CASE(test_relay_active_wrong_relay_deviceid, "test_relay_active_wrong_relay_deviceid", "[initiator]");
    REGISTER_TEST_CASE(test_relay_active_success, "test_relay_active_success", "[initiator]");
    REGISTER_TEST_CASE(test_relay_active_not_enabled, "test_relay_active_not_enabled", "[initiator]");
    REGISTER_TEST_CASE(test_relay_wrong_device, "test_relay_wrong_device", "[initiator]");
    REGISTER_TEST_CASE(test_relay_non_conneteable, "test_relay_non_conneteable", "[initiator]");
    REGISTER_TEST_CASE(test_relay_malformed_address, "test_relay_malformed_address", "[initiator]");
    REGISTER_TEST_CASE(test_relay_garbage_reply, "test_relay_garbage_reply", "[initiator]");
    REGISTER_TEST_CASE(test_relay_noninvitation_reply, "test_relay_noninvitation_reply", "[initiator]");

    return 1;
}

static int v = _init();
