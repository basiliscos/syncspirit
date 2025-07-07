// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"

#include "utils/tls.h"
#include "utils/format.hpp"
#include "proto/bep_support.h"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/contact/peer_state.h"
#include "net/names.h"
#include "net/messages.h"
#include "net/peer_actor.h"
#include "transport/stream.h"
#include "diff-builder.h"
#include "constants.h"

#include <rotor/asio.hpp>
#include <boost/algorithm/string/replace.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace std::chrono_literals;
using Catch::Matchers::StartsWith;

namespace asio = boost::asio;
namespace sys = boost::system;
namespace r = rotor;
namespace ra = r::asio;

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;
using shutdown_callback_t = std::function<void()>;

auto timeout = r::pt::time_duration{r::pt::millisec{1500}};
auto host = "127.0.0.1";

struct supervisor_t : ra::supervisor_asio_t {
    using parent_t = ra::supervisor_asio_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
            p.register_name(names::coordinator, get_address());
            p.register_name(names::peer_supervisor, get_address());
        });

        if (configure_callback) {
            configure_callback(plugin);
        }
    }

    void on_child_shutdown(actor_base_t *actor) noexcept override {
        if (actor) {
            spdlog::info("child shutdown: {}, reason: {}", actor->get_identity(),
                         actor->get_shutdown_reason()->message());
        }
        parent_t::on_child_shutdown(actor);
    }

    void shutdown_finish() noexcept override {
        parent_t::shutdown_finish();
        if (acceptor) {
            acceptor->cancel();
        }
        if (shutdown_callback) {
            shutdown_callback();
        }
    }

    auto get_state() noexcept { return state; }

    asio::ip::tcp::acceptor *acceptor = nullptr;
    configure_callback_t configure_callback;
    shutdown_callback_t shutdown_callback;
};

using supervisor_ptr_t = r::intrusive_ptr_t<supervisor_t>;
using actor_ptr_t = r::intrusive_ptr_t<peer_actor_t>;

struct fixture_t : private model::diff::cluster_visitor_t {
    using acceptor_t = asio::ip::tcp::acceptor;
    using timer_t = asio::deadline_timer;
    using timer_ptr_t = std::unique_ptr<timer_t>;

    fixture_t() noexcept : ctx(io_ctx), acceptor(io_ctx), peer_sock(io_ctx), pre_main_counter{2} {
        test::init_logging();
        log = utils::get_logger("fixture");
    }

    virtual void run(bool add_peer = true) noexcept {
        known_peer = add_peer;
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->shutdown_callback = [this]() { on_shutdown(); };
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using cluster_diff_ptr_t = r::intrusive_ptr_t<model::message::model_update_t>;
                using cluster_diff_t = typename cluster_diff_ptr_t::element_type;

                p.subscribe_actor(r::lambda<cluster_diff_t>([&](cluster_diff_t &msg) {
                    LOG_INFO(log, "received cluster diff message");
                    auto &diff = msg.payload.diff;
                    auto r = diff->apply(*cluster, get_apply_controller());
                    if (!r) {
                        LOG_ERROR(log, "error updating model: {}", r.assume_error().message());
                        sup->do_shutdown();
                        return;
                    }
                    std::ignore = diff->visit(*this, nullptr);
                }));
            });
        };
        sup->start();
        sup->do_process();

        my_keys = utils::generate_pair("my").value();
        auto md = model::device_id_t::from_cert(my_keys.cert_data).value();
        my_device = device_t::create(md, "my-device").value();

        peer_keys = utils::generate_pair("peer").value();
        auto pd = model::device_id_t::from_cert(peer_keys.cert_data).value();
        peer_device = device_t::create(pd, "peer-device").value();

        cluster = new cluster_t(my_device, 1);
        cluster->get_devices().put(my_device);
        if (add_peer) {
            cluster->get_devices().put(peer_device);
        }

        auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(host), 0);
        acceptor.open(ep.protocol());
        acceptor.bind(ep);
        acceptor.listen();
        auto local_ep = acceptor.local_endpoint();

        acceptor.async_accept(peer_sock, [this](auto ec) { this->accept(ec); });
        sup->acceptor = &acceptor;

        auto uri_str = fmt::format("tcp://{}:{}/", local_ep.address(), local_ep.port());
        LOG_TRACE(log, "Connecting to {}", uri_str);

        auto uri = utils::parse(uri_str);
        auto cfg = transport::transport_config_t{{}, uri, *sup, {}, true};
        client_trans = transport::initiate_stream(cfg);

        auto ip = asio::ip::make_address(host);
        auto peer_ep = tcp::endpoint(ip, local_ep.port());
        auto addresses = std::vector<tcp::endpoint>{peer_ep};
        auto addresses_ptr = std::make_shared<decltype(addresses)>(addresses);

        transport::error_fn_t on_error = [&](auto &ec) { on_client_error(ec); };
        transport::connect_fn_t on_connect = [addresses_ptr, this](const tcp::endpoint &ep) {
            LOG_INFO(log, "active/connected {}", ep);
            this->peer_ep = ep;
            try_main();
        };

        client_trans->async_connect(addresses_ptr, on_connect, on_error);

        std::this_thread::sleep_for(1ms);
        io_ctx.run();
        CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
    }

    void try_main() {
        --pre_main_counter;
        LOG_DEBUG(log, "try_main, counter = {}", pre_main_counter);
        if (!pre_main_counter) {
            main();
        }
    }

    virtual void main() noexcept {}

    virtual actor_ptr_t create_actor(uint32_t rx_timeout = 2000) noexcept {
        auto url_str = fmt::format("tcp+active://{}", peer_ep);
        auto state = device_state_t::make_offline();
        auto builder = diff_builder_t(*cluster);
        if (known_peer) {
            state = peer_device->get_state().clone();
        }
        state = state.connecting().connected();
        if (known_peer) {
            builder.update_state(*peer_device, {}, state.clone()).apply(*sup);
        }
        auto url = utils::parse(url_str);

        auto bep_config = config::bep_config_t();
        bep_config.rx_buff_size = 1024;
        bep_config.rx_timeout = rx_timeout;
        bep_config.stats_interval = -1;
        LOG_INFO(log, "crearing actor, timeout = {}", rx_timeout);

        return sup->create_actor<actor_ptr_t::element_type>()
            .timeout(timeout)
            .cluster(cluster)
            .coordinator(sup->get_address())
            .bep_config(bep_config)
            .transport(peer_trans)
            .peer_device_id(peer_device->device_id())
            .peer_state(std::move(state))
            .device_name("peer-device")
            .uri(url->clone())
            .autoshutdown_supervisor(true)
            .finish();
    }

    virtual void on_client_error(const sys::error_code &ec) noexcept { LOG_WARN(log, "client err: {}", ec.message()); }

    virtual void accept(const sys::error_code &ec) noexcept {
        LOG_INFO(log, "accept, ec: {}, remote = {}", ec.message(), peer_sock.remote_endpoint());
        auto uri = utils::parse("tcp://127.0.0.1:0/");
        auto cfg = transport::transport_config_t{{}, uri, *sup, std::move(peer_sock), false};
        peer_trans = transport::initiate_stream(cfg);
        try_main();
    }

    virtual void on_client_write(std::size_t bytes) noexcept { LOG_INFO(log, "client sent {} bytes", bytes); }

    virtual void on_client_read(std::size_t bytes) noexcept {
        LOG_INFO(log, "client received {} bytes", bytes);
        auto result = proto::parse_bep(utils::bytes_view_t(rx_buff, bytes)).value();
        auto hello = std::get_if<proto::Hello>(&result.message);
        // On some platforms 'on_client_read' triggers before 'on_server_send', but as we
        // check server code here, there might be problems. So, give server-side
        // some time via timer.
        if (hello) {
            assert(!hello_timer);
            hello_timer.reset(new timer_t(ctx.get_io_context()));
            hello_timer->expires_from_now(r::pt::milliseconds{1});
            hello_timer->async_wait([hello = std::move(*hello), this](const sys::error_code &ec) mutable {
                LOG_INFO(log, "on hello timer, ec: {}", ec.value());
                on_hello(hello);
                hello_timer.reset();
            });
        }
    }

    virtual void on_hello(proto::Hello &msg) noexcept {
        auto device_name = proto::get_device_name(msg);
        auto client_name = proto::get_client_name(msg);
        auto client_version = proto::get_client_version(msg);
        LOG_INFO(log, "client received hello message from {}, {}/{}", device_name, client_name, client_version);
    }

    virtual void send_hello() noexcept {
        tx_buff = proto::make_hello_message("self-name");
        transport::io_fn_t on_write = [&](size_t bytes) { on_client_write(bytes); };
        transport::error_fn_t on_error = [&](auto &ec) { on_client_error(ec); };
        auto tx_buff_ = asio::buffer(tx_buff.data(), tx_buff.size());
        client_trans->async_send(tx_buff_, on_write, on_error);
    }

    virtual void read_hello() noexcept {
        transport::io_fn_t on_read = [&](size_t bytes) { on_client_read(bytes); };
        transport::error_fn_t on_error = [&](auto &ec) { on_client_error(ec); };
        auto rx_buff_ = asio::buffer(rx_buff, sizeof(rx_buff));
        client_trans->async_recv(rx_buff_, on_read, on_error);
    }

    virtual void send_and_read_hello() noexcept {
        read_hello();
        send_hello();
    }

    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *custom) noexcept override {
        return outcome::success();
    }

    virtual void on_shutdown() noexcept {
        auto peer = cluster->get_devices().by_sha256(peer_device->device_id().get_sha256());
        if (peer) {
            CHECK(peer->get_state().is_offline());
        }
    }

    cluster_ptr_t cluster;
    supervisor_ptr_t sup;
    asio::io_context io_ctx;
    ra::system_context_asio_t ctx;
    acceptor_t acceptor;
    asio::ip::tcp::socket peer_sock;
    utils::logger_t log;
    utils::key_pair_t peer_keys;
    utils::key_pair_t my_keys;
    model::device_ptr_t peer_device;
    tcp::endpoint peer_ep;
    model::device_ptr_t my_device;
    transport::stream_sp_t peer_trans;
    transport::stream_sp_t client_trans;
    utils::bytes_t tx_buff;
    timer_ptr_t hello_timer;
    unsigned char rx_buff[2000];
    bool known_peer = false;
    int pre_main_counter;
};

void test_shutdown_on_hello_timeout() {
    struct F : fixture_t {
        void main() noexcept override { auto act = create_actor(1); }

        void run(bool add_peer = true) noexcept override {
            fixture_t::run(add_peer);
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }
    };
    F().run();
}

void test_no_send_hello_timeout() {
    struct F : fixture_t {
        r::actor_ptr_t peer_actor;
        void main() noexcept override {
            peer_actor = create_actor(1);
            read_hello();
        }

        void on_hello(proto::Hello &) noexcept override {
            auto peer_id = peer_device->device_id();
            auto peer = cluster->get_devices().by_sha256(peer_id.get_sha256());
            CHECK(peer->get_state().is_connected());
        }

        void on_shutdown() noexcept override {
            auto peer = cluster->get_devices().by_sha256(peer_device->device_id().get_sha256());
            CHECK(peer->get_state().is_offline());
        }
    };
    F().run();
}

void test_online_on_hello() {
    struct F : fixture_t {
        void main() noexcept override {
            create_actor();
            send_and_read_hello();
        }

        void on_hello(proto::Hello &) noexcept override {
            auto peer = cluster->get_devices().by_sha256(peer_device->device_id().get_sha256());
            CHECK(peer->get_state().is_online());
            sup->do_shutdown();
            sup->do_process();
        }

        void on_shutdown() noexcept override {
            auto peer = cluster->get_devices().by_sha256(peer_device->device_id().get_sha256());
            CHECK(peer->get_state().is_offline());
        }
    };
    F().run();
}
void test_hello_read_then_write() {
    struct F : fixture_t {
        using tx_size_ptr_t = net::payload::controller_up_t::tx_size_ptr_t;

        r::actor_ptr_t peer_actor;
        bool seen_online = false;
        tx_size_ptr_t outgoing_buff;

        void main() noexcept override {
            outgoing_buff.reset(new std::uint32_t(0));
            peer_actor = create_actor();
            read_hello();
        }

        void on_hello(proto::Hello &) noexcept override {
            auto peer_id = peer_device->device_id();
            auto peer = cluster->get_devices().by_sha256(peer_id.get_sha256());
            CHECK(peer->get_state().is_connected());

            auto coordinator = sup->get_address();
            sup->send<net::payload::controller_up_t>(coordinator, coordinator, peer_id, outgoing_buff);

            send_hello();
        }

        outcome::result<void> operator()(const model::diff::contact::peer_state_t &diff, void *) noexcept override {
            if (diff.state.is_online()) {
                seen_online = true;
                sup->do_shutdown();
            }
            return outcome::success();
        }

        void on_shutdown() noexcept override {
            CHECK(seen_online);
            auto peer = cluster->get_devices().by_sha256(peer_device->device_id().get_sha256());
            CHECK(peer->get_state().is_offline());
            CHECK(*outgoing_buff == 0);
        }
    };
    F().run();
}

void test_hello_from_unknown() {
    struct F : fixture_t {
        void main() noexcept override {
            create_actor();
            send_and_read_hello();
        }

        void on_hello(proto::Hello &) noexcept override {
            CHECK(cluster->get_devices().size() == 1);
            auto &unknown_devices = cluster->get_pending_devices();
            CHECK(unknown_devices.size() == 1);
            auto peer = unknown_devices.by_sha256(peer_device->device_id().get_sha256());
            REQUIRE(peer);
            CHECK(peer->get_name() == "self-name");
            CHECK(peer->get_client_name() == constants::client_name);
            CHECK(peer->get_client_version() == constants::client_version);
            REQUIRE_THAT(std::string(peer->get_address()), StartsWith("tcp://"));

            auto delta = pt::microsec_clock::local_time() - peer->get_last_seen();
            CHECK(delta.seconds() <= 2);
            CHECK((my_device->get_rx_bytes() > 0 || my_device->get_tx_bytes() > 0));
        }

        void on_shutdown() noexcept override {
            CHECK(my_device->get_rx_bytes() > 0);
            CHECK(my_device->get_tx_bytes() > 0);
        }
    };
    F().run(false);
}

void test_hello_from_known_unknown() {
    struct F : fixture_t {
        void main() noexcept override {
            diff_builder_t(*cluster).add_unknown_device(peer_device->device_id(), {}).apply(*sup);
            REQUIRE(cluster->get_pending_devices().size() == 1);
            create_actor();
            send_and_read_hello();
        }

        void on_hello(proto::Hello &) noexcept override {
            CHECK(cluster->get_devices().size() == 1);
            auto &unknown_devices = cluster->get_pending_devices();
            CHECK(unknown_devices.size() == 1);
            auto peer = unknown_devices.by_sha256(peer_device->device_id().get_sha256());
            REQUIRE(peer);
            CHECK(peer->get_name() == "self-name");
            CHECK(peer->get_client_name() == constants::client_name);
            CHECK(peer->get_client_version() == constants::client_version);
            REQUIRE_THAT(std::string(peer->get_address()), StartsWith("tcp://"));

            auto delta = pt::microsec_clock::local_time() - peer->get_last_seen();
            CHECK(delta.seconds() <= 2);
            CHECK((my_device->get_rx_bytes() > 0 || my_device->get_tx_bytes() > 0));
        }

        void on_shutdown() noexcept override {
            CHECK(my_device->get_rx_bytes() > 0);
            CHECK(my_device->get_tx_bytes() > 0);
        }
    };
    F().run(false);
}

void test_hello_from_ignored() {
    struct F : fixture_t {
        void main() noexcept override {
            diff_builder_t(*cluster).add_ignored_device(peer_device->device_id(), {}).apply(*sup);
            REQUIRE(cluster->get_ignored_devices().size() == 1);
            create_actor();
            send_and_read_hello();
        }

        void on_hello(proto::Hello &) noexcept override {
            CHECK(cluster->get_devices().size() == 1);
            auto &ignored_devices = cluster->get_ignored_devices();
            CHECK(ignored_devices.size() == 1);
            auto peer = ignored_devices.by_sha256(peer_device->device_id().get_sha256());
            REQUIRE(peer);
            CHECK(peer->get_name() == "self-name");
            CHECK(peer->get_client_name() == constants::client_name);
            CHECK(peer->get_client_version() == constants::client_version);
            REQUIRE_THAT(std::string(peer->get_address()), StartsWith("tcp://"));

            auto delta = pt::microsec_clock::local_time() - peer->get_last_seen();
            CHECK(delta.seconds() <= 2);
            REQUIRE(cluster->get_pending_devices().size() == 0);
        }
    };
    F().run(false);
}

int _init() {
    REGISTER_TEST_CASE(test_shutdown_on_hello_timeout, "test_shutdown_on_hello_timeout", "[peer]");
    REGISTER_TEST_CASE(test_no_send_hello_timeout, "test_no_send_hello_timeout", "[peer]");
    REGISTER_TEST_CASE(test_online_on_hello, "test_online_on_hello", "[peer]");
    REGISTER_TEST_CASE(test_hello_read_then_write, "test_hello_read_then_write", "[peer]");
    REGISTER_TEST_CASE(test_hello_from_unknown, "test_hello_from_unknown", "[peer]");
    REGISTER_TEST_CASE(test_hello_from_known_unknown, "test_hello_from_known_unknown", "[peer]");
    REGISTER_TEST_CASE(test_hello_from_ignored, "test_hello_from_ignored", "[peer]");
    return 1;
}

static int v = _init();
