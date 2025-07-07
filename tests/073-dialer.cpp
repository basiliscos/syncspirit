// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "model/diff/contact/peer_state.h"
#include "test-utils.h"
#include "access.h"
#include "model/cluster.h"
#include "model/diff/contact/dial_request.h"
#include "diff-builder.h"

#include "net/dialer_actor.h"
#include "access.h"
#include "test_supervisor.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

using state_t = model::device_state_t;

namespace {

struct test_supervisor_t : supervisor_t {
    using supervisor_t::supervisor_t;

    outcome::result<void> operator()(const model::diff::contact::peer_state_t &diff, void *custom) noexcept override {
        if (diff.state.is_unknown()) {
            ++seen_unknown;
        }
        return diff.visit_next(*this, custom);
    }

    outcome::result<void> operator()(const model::diff::contact::dial_request_t &diff, void *custom) noexcept override {
        ++seen_dial_requests;
        return diff.visit_next(*this, custom);
    }

    uint32_t seen_unknown = 0;
    uint32_t seen_dial_requests = 0;
};

struct fixture_t {

    fixture_t(bool start_dialer_) noexcept : start_dialer{start_dialer_} { test::init_logging(); }

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
        sup = ctx.create_supervisor<test_supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        sup->start();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto global_device_id =
            model::device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW");

        auto cfg = config::dialer_config_t{true, 500, 1};
        auto dialer = sup->create_actor<dialer_actor_t>().cluster(cluster).dialer_config(cfg).timeout(timeout).finish();
        target_addr = dialer->get_address();
        if (start_dialer) {
            sup->do_process();
            CHECK(static_cast<r::actor_base_t *>(dialer.get())->access<to::state>() == r::state_t::OPERATIONAL);
        }
        main();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    bool start_dialer;
    r::address_ptr_t target_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    r::intrusive_ptr_t<test_supervisor_t> sup;
    r::system_context_t ctx;
};

} // namespace

void test_dialer() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            auto builder = diff_builder_t(*cluster);

            REQUIRE(peer_device->get_state().get_connection_state() == connection_state_t::offline);

            sup->send<net::payload::announce_notification_t>(sup->get_address());
            sup->do_process();

            SECTION("peer is not online => discover it on timeout") {
                CHECK(peer_device->get_state().get_connection_state() == connection_state_t::offline);
                CHECK(sup->seen_unknown == 1);
            }

            SECTION("peer online & offline") {
                builder.update_state(*peer_device, {}, peer_device->get_state().connecting())
                    .then()
                    .update_state(*peer_device, {}, peer_device->get_state().offline())
                    .apply(*sup);
                REQUIRE(sup->timers.size() == 1);

                sup->do_invoke_timer((*sup->timers.begin())->request_id);
                sup->do_process();
                CHECK(sup->seen_unknown == 2);
                CHECK(sup->seen_dial_requests == 0);
                CHECK(peer_device->get_state().get_connection_state() == connection_state_t::offline);

                auto uri = utils::parse("tcp://127.0.0.1");
                builder.update_contact(peer_device->device_id(), {uri}).apply(*sup);
                CHECK(peer_device->get_state().get_connection_state() == connection_state_t::offline);
                CHECK(sup->timers.size() == 1);
                CHECK(sup->seen_unknown == 2);
                CHECK(sup->seen_dial_requests == 1);

                builder.update_state(*peer_device, {}, peer_device->get_state().connecting().connected())
                    .then()
                    .update_state(*peer_device, {}, peer_device->get_state().online(uri->c_str()))
                    .apply(*sup);

                CHECK(sup->timers.size() == 0);
                CHECK(sup->seen_unknown == 2);
                CHECK(sup->seen_dial_requests == 1);

                builder.update_state(*peer_device, {}, peer_device->get_state().offline()).apply(*sup);
                CHECK(sup->timers.size() == 1);
                sup->do_invoke_timer((*sup->timers.begin())->request_id);
                sup->do_process();
                CHECK(sup->timers.size() == 0);
                CHECK(sup->seen_dial_requests == 2);
            }

            SECTION("remove peer") {
                sup->seen_dial_requests = 0;
                sup->seen_unknown = 0;
                SECTION("start discover") {
                    builder.update_state(*peer_device, {}, peer_device->get_state().connecting().connected())
                        .then()
                        .update_state(*peer_device, {}, peer_device->get_state().offline())
                        .apply(*sup);
                    CHECK(sup->timers.size() == 1);
                }

                builder.remove_peer(*peer_device).apply(*sup);
                CHECK(sup->timers.size() == 0);
            }
        }
    };
    F(true).run();
}

void test_static_address() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            auto builder = diff_builder_t(*cluster);

            auto uri = utils::parse("tcp://127.0.0.1");
            peer_device->set_static_uris({uri});

            sup->do_process();
            CHECK(peer_device->get_state().is_offline());
            CHECK(sup->seen_dial_requests == 1);

            builder.update_state(*peer_device, {}, peer_device->get_state().connecting())
                .then()
                .update_state(*peer_device, {}, peer_device->get_state().offline())
                .apply(*sup);
            CHECK(sup->timers.size() == 1);

            SECTION("remove") {
                builder.remove_peer(*peer_device).apply(*sup);
                CHECK(sup->timers.size() == 0);
            }

            SECTION("invoke") {
                sup->do_invoke_timer((*sup->timers.begin())->request_id);
                sup->do_process();
                CHECK(sup->seen_dial_requests == 2);
            }
        }
    };
    F(false).run();
}

void test_peer_removal() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            REQUIRE(peer_device->get_state().get_connection_state() == connection_state_t::offline);

            SECTION("with announce") {
                sup->send<net::payload::announce_notification_t>(sup->get_address());
                sup->do_process();

                diff_builder_t(*cluster).remove_peer(*peer_device).apply(*sup);
                REQUIRE(sup->timers.size() == 0);
            }

            SECTION("without announce") {
                diff_builder_t(*cluster).remove_peer(*peer_device).apply(*sup);
                REQUIRE(sup->timers.size() == 0);
            }
        }
    };
    F(true).run();
}

int _init() {
    REGISTER_TEST_CASE(test_dialer, "test_dialer", "[net]");
    REGISTER_TEST_CASE(test_static_address, "test_static_address", "[net]");
    REGISTER_TEST_CASE(test_peer_removal, "test_peer_removal", "[net]");
    return 1;
}

static int v = _init();
