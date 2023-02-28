// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/modify/update_contact.h"
#include "utils/tls.h"
#include "utils/error_code.h"
#include "net/global_discovery_actor.h"
#include "net/names.h"
#include "net/messages.h"

#include "access.h"
#include "test_supervisor.h"

#include <nlohmann/json.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace http = boost::beast::http;
using json = nlohmann::json;

namespace {

static auto ssl_pair = utils::generate_pair("sample").value();

struct dummy_http_actor_t : r::actor_base_t {
    using response_t = r::intrusive_ptr_t<net::payload::http_response_t>;
    using queue_t = std::list<response_t>;

    using r::actor_base_t::actor_base_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("dummy-http", true); });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&dummy_http_actor_t::on_request);
            p.subscribe_actor(&dummy_http_actor_t::on_close_connection);
        });
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.register_name(names::http11_gda, get_address()); });
    }

    void on_request(net::message::http_request_t &req) noexcept {
        if (!responses.empty()) {
            auto &res = *responses.front();
            reply_to(req, std::move(res.response), res.bytes, std::move(res.local_addr));
            connected = true;
            responses.pop_front();
        } else {
            auto ec = utils::make_error_code(utils::error_code_t::timed_out);
            reply_with_error(req, make_error(ec));
        }
    }

    void on_close_connection(net::message::http_close_connection_t &) noexcept { closed = true; }

    queue_t responses;
    bool connected = false;
    bool closed = false;
};

struct fixture_t {
    using http_actor_ptr_t = r::intrusive_ptr_t<dummy_http_actor_t>;
    using announce_msg_t = net::message::announce_notification_t;
    using announce_ptr_t = r::intrusive_ptr_t<announce_msg_t>;

    fixture_t() noexcept { utils::set_default("trace"); }

    virtual void run() noexcept {
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        auto my_device = device_t::create(my_id, "my-device").value();
        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<announce_msg_t>([&](announce_msg_t &msg) { announce = &msg; }));
            });
        };
        sup->start();
        http_actor = sup->create_actor<dummy_http_actor_t>().timeout(timeout).finish();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto global_device_id =
            model::device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW");

        gda = sup->create_actor<global_discovery_actor_t>()
                  .cluster(cluster)
                  .ssl_pair(&ssl_pair)
                  .announce_url(utils::parse("https://discovery.syncthing.net/").value())
                  .device_id(std::move(global_device_id.value()))
                  .rx_buff_size(32768ul)
                  .io_timeout(5ul)
                  .timeout(timeout)
                  .finish();
        bool started = preprocess();
        sup->do_process();

        if (started) {
            CHECK(static_cast<r::actor_base_t *>(gda.get())->access<to::state>() == r::state_t::OPERATIONAL);
            target_addr = gda->get_address();
            main();
        }

        sup->shutdown();
        sup->do_process();

        CHECK(http_actor->closed);
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual bool preprocess() noexcept { return true; }

    virtual void main() noexcept {}

    http_actor_ptr_t http_actor;
    r::address_ptr_t target_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<net::global_discovery_actor_t> gda;
    announce_ptr_t announce;
    r::system_context_t ctx;
};

} // namespace

void test_succesfull_announcement() {
    struct F : fixture_t {
        bool preprocess() noexcept override {
            auto uri = utils::parse("tcp://127.0.0.1").value();
            cluster->get_device()->assing_uris({uri});

            SECTION("successul (and empty) announce response") {
                http::response<http::string_body> res;
                res.result(204);
                res.set("Reannounce-After", "123");
                http_actor->responses.push_back(new net::payload::http_response_t(std::move(res), 0));
                sup->do_process();
                CHECK(http_actor->connected);
                CHECK(announce);
            }

            return true;
        }
    };
    F().run();
}

void test_failded_announcement() {
    struct F : fixture_t {
        bool preprocess() noexcept override {
            auto uri = utils::parse("tcp://127.0.0.1").value();
            cluster->get_device()->assing_uris({uri});

            SECTION("successul (and empty) announce response") {
                http::response<http::string_body> res;
                res.result(204);
                http_actor->responses.push_back(new net::payload::http_response_t(std::move(res), 0));
                sup->do_process();
                CHECK(http_actor->connected);
                CHECK(!announce);
                CHECK(static_cast<r::actor_base_t *>(gda.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            }

            return false;
        }
    };
    F().run();
}

void test_peer_discovery() {
    struct F : fixture_t {
        void main() noexcept override {

            sup->send<net::payload::discovery_notification_t>(sup->get_address(), peer_device->device_id());
            http::response<http::string_body> res;

            auto j = json::object();
            j["addresses"] = json::array({"tcp://127.0.0.2"});
            j["seen"] = "2020-10-13T18:41:37.02287354Z";

            SECTION("successful case") {
                res.body() = j.dump();

                http_actor->responses.push_back(new net::payload::http_response_t(std::move(res), 0));
                sup->do_process();

                REQUIRE(peer_device->get_uris().size() == 1);
                CHECK(peer_device->get_uris()[0].full == "tcp://127.0.0.2");

                // 2nd attempt
                peer_device->assing_uris({});
                res = {};
                res.body() = j.dump();
                http_actor->responses.push_back(new net::payload::http_response_t(std::move(res), 0));
                sup->send<net::payload::discovery_notification_t>(sup->get_address(), peer_device->device_id());

                sup->do_process();
                REQUIRE(peer_device->get_uris().size() == 1);
                CHECK(peer_device->get_uris()[0].full == "tcp://127.0.0.2");
            }

            SECTION("gargbage in response") {
                http_actor->responses.push_back(new net::payload::http_response_t(std::move(res), 0));
                sup->do_process();
                REQUIRE(peer_device->get_uris().size() == 0);
                CHECK(static_cast<r::actor_base_t *>(gda.get())->access<to::state>() == r::state_t::OPERATIONAL);
            }
        }
    };
    F().run();
}

void test_late_announcement() {
    struct F : fixture_t {
        void main() noexcept override {

            auto diff = model::diff::contact_diff_ptr_t{};
            diff = new model::diff::modify::update_contact_t(*cluster, {"127.0.0.3"});
            sup->send<model::payload::contact_update_t>(sup->get_address(), diff);

            http::response<http::string_body> res;
            res.result(204);
            res.set("Reannounce-After", "123");
            http_actor->responses.push_back(new net::payload::http_response_t(std::move(res), 0));

            sup->do_process();
            CHECK(http_actor->connected);
            CHECK(announce);
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_succesfull_announcement, "test_succesfull_announcement", "[net]");
    REGISTER_TEST_CASE(test_failded_announcement, "test_failded_announcement", "[net]");
    REGISTER_TEST_CASE(test_peer_discovery, "test_peer_discovery", "[net]");
    REGISTER_TEST_CASE(test_late_announcement, "test_late_announcement", "[net]");
    return 1;
}

static int v = _init();
