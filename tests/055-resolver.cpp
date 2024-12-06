// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/misc/resolver.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;

using A = advance_action_t;

TEST_CASE("resolver", "[model]") {
    utils::set_default("trace");
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_2_id =
        model::device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto peer_2_device = device_t::create(peer_2_id, "peer-device-2").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    cluster->get_devices().put(peer_2_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    REQUIRE(builder.share_folder(peer_2_id.get_sha256(), "1234-5678").apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);
    auto folder_peer = folder_infos.by_device(*peer_device);
    auto folder_peer_2 = folder_infos.by_device(*peer_2_device);

    auto pr_remote = proto::FileInfo();
    pr_remote.set_sequence(2);
    pr_remote.set_name("a.txt");
    auto *peer_v = pr_remote.mutable_version();
    auto *peer_c1 = peer_v->add_counters();
    peer_c1->set_id(1);
    peer_c1->set_value(2);

    SECTION("no local file -> copy remote") {
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote);
        CHECK(action == A::remote_copy);
    }

    SECTION("unreacheable -> ignore") {
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        file_remote->mark_unreachable(true);
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("invalid -> ignore") {
        pr_remote.set_invalid(true);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("3rd party has global version -> ignore") {
        auto pr_remote_2 = pr_remote;
        auto c2 = pr_remote_2.mutable_version()->add_counters();
        c2->set_id(2);
        c2->set_value(3);

        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);

        auto file_remote_2 = file_info_t::create(sequencer->next_uuid(), pr_remote_2, folder_peer_2).value();
        folder_peer_2->add_strict(file_remote_2);

        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("has outdated local file, which is not scanned yet -> ignore") {
        auto pr_local = pr_remote;
        pr_local.mutable_version()->mutable_counters(0)->set_value(1);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("has outdated local file, scanned -> copy remote (1)") {
        auto pr_local = pr_remote;
        pr_local.mutable_version()->mutable_counters(0)->set_value(1);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote);
        CHECK(action == A::remote_copy);
    }

    SECTION("has outdated local file, scanned -> copy remote (2)") {
        auto pr_local = pr_remote;
        auto c2 = pr_remote.mutable_version()->add_counters();
        c2->set_id(2);
        c2->set_value(3);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote);
        CHECK(action == A::remote_copy);
    }

    SECTION("has newer local file, scanned -> ignore (1)") {
        auto pr_local = pr_remote;
        pr_local.mutable_version()->mutable_counters(0)->set_value(2);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("has newer local file, scanned -> ignore (2)") {
        auto pr_local = pr_remote;
        auto c2 = pr_local.mutable_version()->add_counters();
        c2->set_id(2);
        c2->set_value(3);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("local & remote are both deleted-> ignore") {
        pr_remote.set_deleted(true);
        auto pr_local = pr_remote;
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote);
        CHECK(action == A::ignore);
    }

    SECTION("conflicts") {
        auto pr_local = pr_remote;

        SECTION("remote deleted, locally modified -> ignore") {
            auto c2_remote = pr_remote.mutable_version()->add_counters();
            c2_remote->set_id(2);
            c2_remote->set_value(3);

            auto c2_local = pr_local.mutable_version()->add_counters();
            c2_local->set_id(3);
            c2_local->set_value(1);

            pr_remote.set_deleted(true);
            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local();
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote);
            CHECK(action == A::ignore);
        }
        SECTION("locally deleted, remotely modified -> resurrect remote") {
            auto c2_remote = pr_remote.mutable_version()->add_counters();
            c2_remote->set_id(2);
            c2_remote->set_value(3);

            auto c2_local = pr_local.mutable_version()->add_counters();
            c2_local->set_id(3);
            c2_local->set_value(1);

            pr_local.set_deleted(true);
            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local();
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote);
            CHECK(action == A::resurrect_remote);
        }
        SECTION("locally version > remote version -> local_win") {
            auto c2_remote = pr_remote.mutable_version()->add_counters();
            c2_remote->set_id(2);
            c2_remote->set_value(3);

            auto c2_local = pr_local.mutable_version()->add_counters();
            c2_local->set_id(3);
            c2_local->set_value(4);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local();
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote);
            CHECK(action == A::resolve_local_win);
        }
        SECTION("locally version < remote version -> remote_win") {
            auto c2_remote = pr_remote.mutable_version()->add_counters();
            c2_remote->set_id(2);
            c2_remote->set_value(4);

            auto c2_local = pr_local.mutable_version()->add_counters();
            c2_local->set_id(3);
            c2_local->set_value(3);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local();
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote);
            CHECK(action == A::resolve_remote_win);
        }
        SECTION("locally version == remote version -> remote_win, by device") {
            auto c2_remote = pr_remote.mutable_version()->add_counters();
            c2_remote->set_id(2);
            c2_remote->set_value(4);

            auto c2_local = pr_local.mutable_version()->add_counters();
            c2_local->set_id(3);
            c2_local->set_value(4);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local();
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote);
            CHECK(action == A::resolve_local_win);
        }
    }
}
