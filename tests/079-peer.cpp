// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "access.h"

#include "utils/tls.h"
#include "utils/format.hpp"
#include "model/cluster.h"
#include "model/messages.h"
#include "model/diff/peer/peer_state.h"
#include "net/names.h"
#include "net/messages.h"
#include "net/peer_actor.h"
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
using actor_ptr_t = r::intrusive_ptr_t<peer_actor_t>;

struct fixture_t : private model::diff::contact_visitor_t {
    using acceptor_t = asio::ip::tcp::acceptor;
    using diff_ptr_t = r::intrusive_ptr_t<model::message::model_update_t>;

    fixture_t() noexcept : ctx(io_ctx), acceptor(io_ctx), peer_sock(io_ctx) {
        utils::set_default("trace");
        log = utils::get_logger("fixture");
    }

    virtual void run() noexcept {

        auto strand = std::make_shared<asio::io_context::strand>(io_ctx);
        sup = ctx.create_supervisor<supervisor_t>().strand(strand).timeout(timeout).create_registry().finish();
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                using diff_t = typename diff_ptr_t::element_type;
                p.subscribe_actor(r::lambda<diff_t>([&](diff_t &msg) {
                    LOG_INFO(log, "received diff message");
                    auto &diff = msg.payload.diff;
                    auto r = diff->apply(*cluster);
                    if (!r) {
                        LOG_ERROR(log, "error updating model: {}", r.assume_error().message());
                        sup->do_shutdown();
                    }
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

        cluster = new cluster_t(my_device, 1, 1);
        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

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

        tcp::resolver resolver(io_ctx);
        auto addresses = resolver.resolve(host, std::to_string(local_ep.port()));
        auto addresses_ptr = std::make_shared<decltype(addresses)>(addresses);

        transport::error_fn_t on_error = [&](auto &ec) { LOG_WARN(log, "active/connect, err: {}", ec.message()); };
        transport::connect_fn_t on_connect = [addresses_ptr, this](const tcp::endpoint &ep) {
            LOG_INFO(log, "active/connected");
            main();
        };

        client_trans->async_connect(*addresses_ptr, on_connect, on_error);

        io_ctx.run();
    }

    virtual void main() noexcept {}

    virtual actor_ptr_t create_actor() noexcept {

        auto diff = model::diff::cluster_diff_ptr_t();
        auto state = model::device_state_t::dialing;
        auto sha256 = peer_device->device_id().get_sha256();
        diff = new model::diff::peer::peer_state_t(*cluster, sha256, nullptr, state);
        sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff));

        auto bep_config = config::bep_config_t();
        bep_config.rx_buff_size = 1024;
        return sup->create_actor<actor_ptr_t::element_type>()
            .timeout(timeout)
            .cluster(cluster)
            .coordinator(sup->get_address())
            .bep_config(bep_config)
            .transport(peer_trans)
            .peer_device_id(peer_device->device_id())
            .device_name("peer-device")
            .peer_proto("tcp")
            .escalate_failure()
            .finish();
    }

    virtual void accept(const sys::error_code &ec) noexcept {
        LOG_INFO(log, "accept, ec: {}, remote = {}", ec.message(), peer_sock.remote_endpoint());
        auto uri = utils::parse("tcp://127.0.0.1:0/");
        auto cfg = transport::transport_config_t{{}, uri, *sup, std::move(peer_sock), false};
        peer_trans = transport::initiate_stream(cfg);
        main();
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
    model::device_ptr_t my_device;
    transport::stream_sp_t peer_trans;
    transport::stream_sp_t client_trans;
};

void test_shutdown_on_hello_timeout() {
    struct F : fixture_t {
        void main() noexcept override { auto act = create_actor(); }

        void run() noexcept override {
            fixture_t::run();
            CHECK(sup->get_state() == r::state_t::SHUT_DOWN);
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_shutdown_on_hello_timeout, "test_shutdown_on_hello_timeout", "[peer]");
    return 1;
}

static int v = _init();
