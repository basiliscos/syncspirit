// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"

#include "utils/tls.h"
#include "utils/format.hpp"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/contact/relay_connect_request.h"
#include "model/diff/contact/unknown_connected.h"
#include "net/names.h"
#include "net/messages.h"
#include "net/relay_actor.h"
#include "transport/stream.h"
#include <rotor/asio.hpp>
#include <boost/algorithm/string/replace.hpp>

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
    using parent_t = ra::supervisor_asio_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
            p.register_name(names::coordinator, get_address());
            p.register_name(names::peer_supervisor, get_address());
            p.register_name(names::http11_relay, get_address());
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
    }

    auto get_state() noexcept { return state; }

    asio::ip::tcp::acceptor *acceptor = nullptr;
    configure_callback_t configure_callback;
};

using supervisor_ptr_t = r::intrusive_ptr_t<supervisor_t>;
using actor_ptr_t = r::intrusive_ptr_t<relay_actor_t>;

struct fixture_t : private model::diff::cluster_visitor_t {
    using acceptor_t = asio::ip::tcp::acceptor;

    fixture_t() noexcept : ctx(io_ctx), acceptor(io_ctx), peer_sock(io_ctx) {
        test::init_logging();
        log = utils::get_logger("fixture");
        relay_config = config::relay_config_t{
            true,
            true,
            utils::parse("https://some-endpoint.com/"),
            1024 * 1024,
        };
    }

    void run() noexcept {
        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using model_update_t = model::message::model_update_t;
                p.subscribe_actor(r::lambda<model_update_t>([&](model_update_t &msg) { on(msg); }));
                using http_req_t = net::message::http_request_t;
                p.subscribe_actor(r::lambda<http_req_t>([&](http_req_t &req) {
                    LOG_INFO(log, "received http request");
                    http::response<http::string_body> res;
                    res.result(200);
                    res.body() = public_relays;
                    sup->reply_to(req, std::move(res), public_relays.size());
                }));
                using connect_req_t = net::message::connect_request_t;
                p.subscribe_actor(r::lambda<connect_req_t>([&](connect_req_t &req) {
                    LOG_INFO(log, "(connect request)");
                    on(req);
                }));
            });
        };
        sup->start();
        sup->do_process();

        auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(host), 0);
        acceptor.open(ep.protocol());
        acceptor.bind(ep);
        acceptor.listen();
        listening_ep = acceptor.local_endpoint();

        my_keys = utils::generate_pair("me").value();
        relay_keys = utils::generate_pair("relay").value();
        peer_keys = utils::generate_pair("peer").value();

        auto md = model::device_id_t::from_cert(my_keys.cert_data).value();
        auto rd = model::device_id_t::from_cert(relay_keys.cert_data).value();
        auto pd = model::device_id_t::from_cert(peer_keys.cert_data).value();

        my_device = device_t::create(md, "my-device").value();
        relay_device = device_t::create(rd, "relay-device").value();
        peer_device = device_t::create(rd, "peer-device").value();

        public_relays = generate_public_relays(listening_ep, relay_device);
        log->debug("public relays json: {}", public_relays);
        initiate_accept();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        session_key = as_owned_bytes("lorem-imspum-dolor");

        main();
    }

    virtual void main() noexcept {}

    virtual std::string generate_public_relays(const asio::ip::tcp::endpoint &,
                                               model::device_ptr_t &relay_device) noexcept {
        std::string pattern = R""(
    {
      "relays": [
        {
          "url": "##URL##&pingInterval=0m1s&networkTimeout=2m0s&sessionLimitBps=0&globalLimitBps=0&statusAddr=:22070&providedBy=ina",
          "location": {
            "latitude": 50.1049,
            "longitude": 8.6295,
            "city": "Frankfurt am Main",
            "country": "DE",
            "continent": "EU"
          }
        }
    ]
    }
        )"";
        auto url = fmt::format("relay://{}/?id={}", listening_ep, relay_device->device_id().get_value());
        return boost::algorithm::replace_first_copy(pattern, "##URL##", url);
    }

    virtual void initiate_accept() noexcept {
        acceptor.async_accept(peer_sock, [this](auto ec) { this->accept(ec); });
        sup->acceptor = &acceptor;
    }

    virtual void accept(const sys::error_code &ec) noexcept {
        LOG_INFO(log, "accept (relay), ec: {}, remote = {}", ec.message(), peer_sock.remote_endpoint());
        auto uri = utils::parse("tcp://127.0.0.1:0/");
        auto cfg = transport::transport_config_t{{}, uri, *sup, std::move(peer_sock), false};
        relay_trans = transport::initiate_stream(cfg);
        relay_read();
    }

    virtual actor_ptr_t create_actor() noexcept {
        return sup->create_actor<actor_ptr_t::element_type>()
            .timeout(timeout)
            .cluster(cluster)
            .relay_config(relay_config)
            .escalate_failure()
            .finish();
    }

    virtual void on(net::message::connect_request_t &req) noexcept {
        auto &uri = req.payload.request_payload.uri;
        log->info("requested connect to {}", uri);
        auto cfg = transport::transport_config_t{{}, uri, *sup, {}, true};

        auto ip = asio::ip::make_address(host);
        auto peer_ep = tcp::endpoint(ip, uri->port_number());
        auto addresses = std::vector<tcp::endpoint>{peer_ep};
        auto addresses_ptr = std::make_shared<decltype(addresses)>(addresses);
        auto trans = transport::initiate_stream(cfg);
        transport::error_fn_t on_error = [&](auto &ec) { LOG_WARN(log, "active/connect, err: {}", ec.message()); };
        using ptr_t = model::intrusive_ptr_t<std::decay_t<decltype(req)>>;
        auto ptr = ptr_t(&req);
        transport::connect_fn_t on_connect = [ptr, trans, addresses_ptr, this](const tcp::endpoint &ep) {
            LOG_INFO(log, "active/connected");
            sup->reply_to(*ptr, trans, ep);
        };
        trans->async_connect(addresses_ptr, on_connect, on_error);
    }

    void send_relay(const proto::relay::message_t &msg) noexcept {
        proto::relay::serialize(msg, relay_tx);
        transport::error_fn_t on_error = [&](auto &ec) { LOG_WARN(log, "relay/write, err: {}", ec.message()); };
        transport::io_fn_t on_write = [&](size_t bytes) { LOG_TRACE(log, "relay/write, {} bytes", bytes); };
        relay_trans->async_send(asio::buffer(relay_tx), on_write, on_error);
    }

    virtual void on_relay(proto::relay::ping_t &) noexcept {};
    virtual void on_relay(proto::relay::pong_t &) noexcept {};
    virtual void on_relay(proto::relay::join_relay_request_t &) noexcept {
        LOG_INFO(log, "join_relay_request_t");
        send_relay(proto::relay::response_t{0, "ok"});
    };
    virtual void on_relay(proto::relay::join_session_request_t &) noexcept {};
    virtual void on_relay(proto::relay::response_t &) noexcept {};
    virtual void on_relay(proto::relay::connect_request_t &) noexcept {};
    virtual void on_relay(proto::relay::session_invitation_t &) noexcept {};
    virtual void on(model::message::model_update_t &update) noexcept {
        auto &diff = *update.payload.diff;
        auto r = diff.apply(*cluster, get_apply_controller());
        if (!r) {
            LOG_ERROR(log, "error applying diff: {}", r.error().message());
        }
        r = diff.visit(*this, nullptr);
        if (!r) {
            LOG_ERROR(log, "error visiting diff: {}", r.error().message());
        }
    }

    void relay_read() noexcept {
        transport::error_fn_t on_error = [&](auto &ec) { LOG_WARN(log, "relay/read, err: {}", ec.message()); };
        transport::io_fn_t on_read = [&](size_t bytes) {
            LOG_TRACE(log, "relay/read, {} bytes", bytes);
            auto msg = proto::relay::parse({relay_rx.data(), bytes});
            auto wrapped = std::get_if<proto::relay::wrapped_message_t>(&msg);
            if (!wrapped) {
                LOG_ERROR(log, "relay/read non-message?");
                return;
            }
            std::visit([&](auto &it) { on_relay(it); }, wrapped->message);
        };
        relay_rx.resize(1500);
        auto buff = asio::buffer(relay_rx.data(), relay_rx.size());
        relay_trans->async_recv(buff, on_read, on_error);
        LOG_TRACE(log, "relay/async recv");
    }

    config::relay_config_t relay_config;
    cluster_ptr_t cluster;
    asio::io_context io_ctx;
    ra::system_context_asio_t ctx;
    acceptor_t acceptor;
    supervisor_ptr_t sup;
    asio::ip::tcp::endpoint listening_ep;
    utils::logger_t log;
    asio::ip::tcp::socket peer_sock;
    std::string public_relays;
    utils::key_pair_t my_keys;
    utils::key_pair_t relay_keys;
    utils::key_pair_t peer_keys;
    model::device_ptr_t my_device;
    model::device_ptr_t relay_device;
    model::device_ptr_t peer_device;
    transport::stream_sp_t relay_trans;
    utils::bytes_t relay_rx;
    utils::bytes_t relay_tx;
    utils::bytes_t session_key;
};

void test_master_connect() {
    struct F : fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();

            CHECK(sup->get_state() == r::state_t::OPERATIONAL);
            REQUIRE(my_device->get_uris().size() == 1);
            CHECK(my_device->get_uris()[0]->scheme() == "relay");

            sup->shutdown();
            io_ctx.restart();
            io_ctx.run();

            CHECK(my_device->get_uris().size() == 0);
            io_ctx.restart();
            io_ctx.run();

            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }

        void on(model::message::model_update_t &update) noexcept override {
            LOG_INFO(log, "contact_update_t");
            fixture_t::on(update);
            io_ctx.stop();
        }
    };

    F().run();
}

void test_passive() {
    struct F : fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(sent);
            CHECK(received);
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);

            sup->shutdown();
            io_ctx.restart();
            io_ctx.run();

            CHECK(my_device->get_uris().size() == 0);
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }

        void on(model::message::model_update_t &update) noexcept override {
            LOG_INFO(log, "contact_update_t");
            fixture_t::on(update);
            if (my_device->get_uris().size() == 1 && !sent) {
                sent = true;
                auto sha256 = peer_device->device_id().get_sha256();
                auto id = utils::bytes_t(sha256.begin(), sha256.end());
                auto msg = proto::relay::session_invitation_t{std::move(id), session_key, {}, 12345, true};
                send_relay(msg);
            }
        }

        outcome::result<void> operator()(const model::diff::contact::relay_connect_request_t &diff,
                                         void *) noexcept override {
            CHECK(diff.peer == peer_device->device_id());
            CHECK(diff.session_key == session_key);
            CHECK(diff.relay.port() == 12345);
            CHECK(diff.relay.address().to_string() == "127.0.0.1");
            received = true;
            io_ctx.stop();
            return outcome::success();
        }

        bool sent = false;
        bool received = false;
    };
    F().run();
}

void test_passive_ping_pong() {
    struct F : fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            io_ctx.run();
            CHECK(received);
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);

            sup->shutdown();
            io_ctx.restart();
            io_ctx.run();

            CHECK(my_device->get_uris().size() == 0);
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }

        void on_relay(proto::relay::join_relay_request_t &request) noexcept override {
            fixture_t::on_relay(request);
            relay_read();
        };

        void on_relay(proto::relay::ping_t &message) noexcept override {
            fixture_t::on_relay(message);
            send_relay(proto::relay::pong_t{});
            auto sha256 = peer_device->device_id().get_sha256();
            auto id = utils::bytes_t(sha256.begin(), sha256.end());
            auto msg = proto::relay::session_invitation_t{std::move(id), session_key, {}, 12345, true};
            send_relay(msg);
        };

        outcome::result<void> operator()(const model::diff::contact::relay_connect_request_t &diff,
                                         void *) noexcept override {
            CHECK(diff.peer == peer_device->device_id());
            CHECK(diff.session_key == session_key);
            CHECK(diff.relay.port() == 12345);
            CHECK(diff.relay.address().to_string() == "127.0.0.1");
            received = true;
            io_ctx.stop();
            return outcome::success();
        }

        bool received = false;
    };
    F().run();
}

void test_passive_unknown() {
    struct F : fixture_t {
        void main() noexcept override {
            auto act = create_actor();
            cluster->get_devices().remove(peer_device);
            io_ctx.run();
            CHECK(sent);
            CHECK(unknownon_connected);
            CHECK(sup->get_state() == r::state_t::OPERATIONAL);

            sup->shutdown();
            io_ctx.restart();
            io_ctx.run();

            CHECK(my_device->get_uris().size() == 0);
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }

        void on(model::message::model_update_t &update) noexcept override {
            LOG_INFO(log, "contact_update_t");
            fixture_t::on(update);
            if (my_device->get_uris().size() == 1 && !sent) {
                sent = true;
                auto sha256 = peer_device->device_id().get_sha256();
                auto id = utils::bytes_t(sha256.begin(), sha256.end());
                auto msg = proto::relay::session_invitation_t{std::move(id), session_key, {}, 12345, true};
                send_relay(msg);
            }
        }

        outcome::result<void> operator()(const model::diff::contact::unknown_connected_t &diff,
                                         void *) noexcept override {
            CHECK(diff.device_id == peer_device->device_id());
            unknownon_connected = true;
            io_ctx.stop();
            return outcome::success();
        }

        bool sent = false;
        bool unknownon_connected = false;
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_master_connect, "test_master_connect", "[relay]");
    REGISTER_TEST_CASE(test_passive, "test_passive", "[relay]");
    REGISTER_TEST_CASE(test_passive_ping_pong, "test_passive_ping_pong", "[relay]");
    REGISTER_TEST_CASE(test_passive_unknown, "test_passive_unknown", "[relay]");

    return 1;
}

static int v = _init();
