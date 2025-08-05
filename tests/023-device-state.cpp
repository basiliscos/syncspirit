// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "model/device_state.h"

using namespace syncspirit;
using namespace syncspirit::utils;
using namespace syncspirit::model;

TEST_CASE("device-state", "[model]") {
    SECTION("offline") {
        auto s1 = device_state_t::make_offline();
        auto s2 = device_state_t::make_offline();
        CHECK(s1.get_connection_state() == connection_state_t::offline);
        CHECK(s1.is_offline());

        CHECK(s1 == s1);
        CHECK(s1 != s2);
        CHECK(!(s1 < s2));
        CHECK(!(s2 < s1));

        CHECK(!s1.can_roollback_to(s2));
        CHECK(!s2.can_roollback_to(s1));
    }

    SECTION("offline & connecting") {
        auto s0 = device_state_t::make_offline();
        auto s0_0 = device_state_t::make_offline();
        auto s1 = s0.connecting();
        CHECK(s1.get_connection_state() == connection_state_t::connecting);
        CHECK(s0 < s1);
        CHECK(!(s1 < s0));
        CHECK(s1.is_connecting());
        CHECK(s1.can_roollback_to(s0));
        CHECK(!s1.can_roollback_to(s0_0));
        CHECK(!s0.can_roollback_to(s1));
        CHECK(s1.can_roollback_to(s1.offline()));
        CHECK(s1.offline() == s0);

        auto s1_clone = s1.clone();
        CHECK(s1 == s1_clone);
        CHECK(s1.get_url() == s1_clone.get_url());
    }

    SECTION("offline & unknown") {
        auto s0 = device_state_t::make_offline();
        auto s0_0 = device_state_t::make_offline();
        auto s1 = s0.unknown();
        CHECK(s1.get_connection_state() == connection_state_t::unknown);
        CHECK(s0 < s1);
        CHECK(!(s1 < s0));
        CHECK(s1.is_unknown());
        CHECK(s1.can_roollback_to(s0));
        CHECK(!s1.can_roollback_to(s0_0));
        CHECK(!s0.can_roollback_to(s1));
        CHECK(s1.can_roollback_to(s1.offline()));
        CHECK(s1.offline() == s0);
    }

    SECTION("unknown & discovery") {
        auto s0 = device_state_t::make_offline();
        auto s0_0 = device_state_t::make_offline();
        auto s1 = s0.unknown().discover();
        CHECK(s1.get_connection_state() == connection_state_t::discovering);
        CHECK(s0 < s1);
        CHECK(!(s1 < s0));
        CHECK(s1.is_discovering());
        CHECK(s1.can_roollback_to(s0));
        CHECK(!s1.can_roollback_to(s0_0));
        CHECK(!s0.can_roollback_to(s1));
        CHECK(s1.can_roollback_to(s1.offline()));
        CHECK(s1.offline() == s0);
    }

    SECTION("offline, discovery & connect") {
        auto s0_0 = device_state_t::make_offline();
        auto s0_1 = device_state_t::make_offline();
        auto s_d = s0_0.unknown().discover();
        auto s_c = s0_1.connecting();
        CHECK(s_d < s_c);
        CHECK(!s_c.can_roollback_to(s_d));
        CHECK(!s_d.can_roollback_to(s_c));
        CHECK(s_d.can_roollback_to(s0_0));
        CHECK(!s_d.can_roollback_to(s0_1));
        CHECK(s_c.can_roollback_to(s0_1));
        CHECK(!s_c.can_roollback_to(s0_0));
    }

    SECTION("connecting & connected") {
        auto s1 = device_state_t::make_offline().connecting();
        auto s0 = device_state_t::make_offline().connecting().connected();
        CHECK(s1 < s0);
        CHECK(!s0.is_connecting());
        CHECK(s0.is_connected());
        CHECK(!s1.can_roollback_to(s0));
        CHECK(!s0.can_roollback_to(s1));
    }

    SECTION("online") {
        auto s0 = device_state_t::make_offline();
        auto s1 = s0.connecting().connected();
        auto s2 = s1.online("tcp://127.0.0.1:1234");
        CHECK(s1 < s2);
        CHECK(s0 < s2);
        CHECK(s2.is_online());
        CHECK(s2.can_roollback_to(s0));
        CHECK(s2.can_roollback_to(s1));
        CHECK(*s2.get_url() == *utils::parse("tcp://127.0.0.1:1234"));

        CHECK(s2 == s2.clone());
    }

    SECTION("tcp & relay onlines") {
        auto s0_1 = device_state_t::make_offline();
        auto s0_2 = device_state_t::make_offline();
        auto s0_3 = device_state_t::make_offline();
        auto s2_1 = s0_1.connecting().connected().online("tcp+active://127.0.0.1:1234");
        auto s2_2 = s0_1.connecting().connected().online("relay+offline://127.0.0.1:1235");
        auto s2_3 = s0_1.connecting().connected().online("tcp+passive://127.0.0.1:1236");

        CHECK(s2_2 < s2_1);
        CHECK(s2_2 < s2_3);
        CHECK(s2_1 < s2_3);
        CHECK(!(s2_3 < s2_1));
        CHECK(s2_1 == s2_1);
        CHECK(s2_1 != s2_2);
        CHECK(s2_1 != s0_1);
        CHECK(s2_1 == s2_1.clone());
    }
}
