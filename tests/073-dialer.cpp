// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/peer/peer_state.h"
#include "utils/error_code.h"

#include "net/dialer_actor.h"
#include "net/names.h"
#include "access.h"
#include "test_supervisor.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace {

struct fixture_t {
    using discovery_msg_t = net::message::discovery_notify_t;
    using discovery_ptr_t = r::intrusive_ptr_t<discovery_msg_t>;

    fixture_t() noexcept { utils::set_default("trace"); }

    virtual void run() noexcept {
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        auto my_device = device_t::create(my_id, "my-device").value();
        cluster = new cluster_t(my_device, 1, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<discovery_msg_t>([&](discovery_msg_t &msg) { discovery = &msg; }));
            });
        };

        sup->start();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto global_device_id =
            model::device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW");

        auto cfg = config::dialer_config_t{true, 500};
        auto dialer = sup->create_actor<dialer_actor_t>().cluster(cluster).dialer_config(cfg).timeout(timeout).finish();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(dialer.get())->access<to::state>() == r::state_t::OPERATIONAL);
        target_addr = dialer->get_address();
        main();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::address_ptr_t target_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::system_context_t ctx;
    discovery_ptr_t discovery;
};

} // namespace

void test_dialer() {
    struct F : fixture_t {
        void main() noexcept override {
            sup->send<net::payload::announce_notification_t>(sup->get_address());
            sup->do_process();
            CHECK(discovery);

            discovery.reset();
            REQUIRE(sup->timers.size() == 1);

            SECTION("peer is not online => discover it on timeout") {
                sup->do_invoke_timer((*sup->timers.begin())->request_id);
                sup->do_process();
                CHECK(discovery);
                CHECK(sup->timers.size() == 1);
            }

            SECTION("peer online & offline") {
                auto diff = model::diff::cluster_diff_ptr_t{};
                auto sample_addr = sup->get_address();
                auto peer_id = peer_device->device_id().get_sha256();
                diff = new model::diff::peer::peer_state_t(*cluster, peer_id, sample_addr, device_state_t::online);
                sup->send<model::payload::model_update_t>(sup->get_address(), diff);

                sup->do_process();
                CHECK(!discovery);
                CHECK(sup->timers.size() == 0);

                diff = new model::diff::peer::peer_state_t(*cluster, peer_id, sample_addr, device_state_t::offline);
                sup->send<model::payload::model_update_t>(sup->get_address(), diff);

                sup->do_process();
                CHECK(!discovery);
                CHECK(sup->timers.size() == 1);

                sup->do_invoke_timer((*sup->timers.begin())->request_id);
                sup->do_process();
                CHECK(discovery);
                CHECK(sup->timers.size() == 1);
            }
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_dialer, "test_dialer", "[net]");
    return 1;
}

static int v = _init();
