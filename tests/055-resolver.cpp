// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "access.h"
#include "model/cluster.h"
#include "model/misc/resolver.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::model;

using A = advance_action_t;

TEST_CASE("resolver", "[model]") {
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
    proto::set_name(pr_remote, "a.txt");
    proto::set_sequence(pr_remote, 2);

    auto &peer_v = proto::get_version(pr_remote);
    auto &peer_c1 = proto::add_counters(peer_v);
    proto::set_id(peer_c1, 1);
    proto::set_value(peer_c1, 2);

    SECTION("no local file -> copy remote") {
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }

    SECTION("unreacheable -> ignore") {
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        file_remote->mark_unreachable(true);
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("invalid -> ignore") {
        proto::set_invalid(pr_remote, true);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("3rd party has global version -> ignore") {
        auto pr_remote_2 = pr_remote;
        auto &c2 = proto::add_counters(proto::get_version(pr_remote_2));
        proto::set_id(c2, 2);
        proto::set_value(c2, 3);

        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);

        auto file_remote_2 = file_info_t::create(sequencer->next_uuid(), pr_remote_2, folder_peer_2).value();
        folder_peer_2->add_strict(file_remote_2);

        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("has outdated local file, which is not scanned yet -> ignore") {
        auto pr_local = pr_remote;
        auto &c_local = proto::get_counters(proto::get_version(pr_local), 0);
        proto::set_value(c_local, 1);

        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);

        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("has outdated local file, scanned -> copy remote (1)") {
        auto pr_local = pr_remote;
        auto &c_local = proto::get_counters(proto::get_version(pr_local), 0);
        proto::set_value(c_local, 1);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }

    SECTION("has outdated local file, scanned -> copy remote (2)") {
        auto pr_local = pr_remote;
        auto &c2 = proto::add_counters(proto::get_version(pr_remote));
        proto::set_id(c2, 2);
        proto::set_value(c2, 3);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }

    SECTION("has outdated local file, scanned & remote deleted -> copy remote (3)") {
        auto pr_local = pr_remote;
        auto &c2 = proto::add_counters(proto::get_version(pr_remote));
        proto::set_id(c2, 2);
        proto::set_value(c2, 3);
        proto::set_deleted(pr_remote, true);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }

    SECTION("has newer local file, scanned -> ignore (1)") {
        auto pr_local = pr_remote;
        auto &c2 = proto::get_counters(proto::get_version(pr_local), 0);
        proto::set_value(c2, 3);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("has newer local file, scanned -> ignore (2)") {
        auto pr_local = pr_remote;
        auto &c2 = proto::add_counters(proto::get_version(pr_local));
        proto::set_id(c2, 2);
        proto::set_value(c2, 3);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("local & remote are both deleted-> ignore") {
        proto::set_deleted(pr_remote, true);
        auto pr_local = pr_remote;
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("remote is deleted, local is outdated, folder is marked as ignored deletes -> ignore") {
        ((model::folder_data_t *)folder.get())->access<test::to::ignore_delete>() = true;
        auto pr_local = pr_remote;
        auto &c_local = proto::get_counters(proto::get_version(pr_local), 0);
        proto::set_value(c_local, 1);
        proto::set_deleted(pr_remote, true);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
        file_local->mark_local(true);
        folder_peer->add_strict(file_remote);
        folder_my->add_strict(file_local);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }

    SECTION("conflicts") {
        auto pr_local = pr_remote;

        SECTION("remote deleted, locally modified -> ignore") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 3);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 3);

            proto::set_deleted(pr_remote, true);
            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::ignore);
        }
        SECTION("remote deleted, locally modified -> ignore (2)") {
            auto &v_remote = proto::get_version(pr_remote);
            auto &c2_remote = proto::get_counters(v_remote, 0);
            auto v = proto::get_value(c2_remote) + 1;
            proto::set_value(c2_remote, v);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, v);

            proto::set_deleted(pr_remote, true);
            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::ignore);
        }
        SECTION("locally deleted, remotely modified -> resurrect remote") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 3);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 1);

            proto::set_deleted(pr_remote, true);
            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::remote_copy);
        }
        SECTION("locally mod > remote mod -> ignore(local_win)") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 3);
            proto::set_modified_s(pr_remote, 1734680000);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 4);
            proto::set_modified_s(pr_local, 1734690000);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::ignore);
        }
        SECTION("locally mod < remote mod -> remote_win") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 4);
            proto::set_modified_s(pr_remote, 1734680000);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 3);
            proto::set_modified_s(pr_local, 1734670000);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::resolve_remote_win);
        }
        SECTION("locally version == remote version -> local_win(ignore), by device") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 4);
            proto::set_modified_s(pr_remote, 1734680000);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 4);
            proto::set_modified_s(pr_local, 1734680000);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::ignore);
        }
        SECTION("no recursive conflicts are allowed -> ignore") {
            auto name = std::string("a.txt..sync-conflict-20060102-150405-XBOWT");
            proto::set_name(pr_remote, name);
            proto::set_name(pr_local, name);

            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 4);

            auto &c2_local = proto::add_counters(proto::get_version(pr_local));
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 4);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_peer->add_strict(file_remote);
            folder_my->add_strict(file_local);
            auto action = resolve(*file_remote, file_local.get(), *folder_my);
            CHECK(action == A::ignore);
        }
        SECTION("locally version < remote version, already resolved -> ignore") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 4);

            auto &v_local = proto::get_version(pr_local);
            auto &c2_local = proto::add_counters(v_local);
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 3);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_my->add_strict(file_local);
            folder_peer->add_strict(file_remote);

            proto::clear_counters(v_local);
            auto &c3_local = proto::add_counters(v_local);
            proto::set_id(c3_local, 3);
            proto::set_value(c3_local, 1);

            proto::set_name(pr_local, file_local->make_conflicting_name());
            proto::set_sequence(pr_local, folder_my->get_max_sequence() + 1);
            auto file_resolved = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_resolved->mark_local(true);
            REQUIRE(folder_my->add_strict(file_resolved));

            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::ignore);
        }
        SECTION("locally version > remote version, already resolved -> ignore") {
            auto &c2_remote = proto::add_counters(proto::get_version(pr_remote));
            proto::set_id(c2_remote, 2);
            proto::set_value(c2_remote, 3);

            auto &v_local = proto::get_version(pr_local);
            auto &c2_local = proto::add_counters(v_local);
            proto::set_id(c2_local, 3);
            proto::set_value(c2_local, 4);

            auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
            auto file_local = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_local->mark_local(true);
            folder_my->add_strict(file_local);
            folder_peer->add_strict(file_remote);

            proto::clear_counters(v_local);
            auto &c3_local = proto::add_counters(v_local);
            proto::set_id(c3_local, 3);
            proto::set_value(c3_local, 1);

            proto::set_name(pr_local, file_remote->make_conflicting_name());
            proto::set_sequence(pr_local, folder_my->get_max_sequence() + 1);
            auto file_resolved = file_info_t::create(sequencer->next_uuid(), pr_local, folder_my).value();
            file_resolved->mark_local(true);
            REQUIRE(folder_my->add_strict(file_resolved));

            auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
            CHECK(action == A::ignore);
        }
    }
}

TEST_CASE("resolver, reserved names", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto &folders = cluster->get_folders();
    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = folder->get_folder_infos();
    auto folder_my = folder_infos.by_device(*my_device);
    auto folder_peer = folder_infos.by_device(*peer_device);

    auto pr_remote = proto::FileInfo();
    auto &peer_v = proto::get_version(pr_remote);
    auto &peer_c1 = proto::add_counters(peer_v);
    proto::set_id(peer_c1, 1);
    proto::set_value(peer_c1, 2);

    proto::set_sequence(pr_remote, 2);

    SECTION("valid name => copy") {
        proto::set_name(pr_remote, "a.txt");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }
    SECTION("symlinks are not supported") {
        proto::set_name(pr_remote, "b.txt");
        proto::set_type(pr_remote, proto::FileInfoType::SYMLINK);
        proto::set_symlink_target(pr_remote, "b.txt");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
#ifndef SYNCSPIRIT_WIN
        CHECK(action == A::remote_copy);
#else
        CHECK(action == A::ignore);
#endif
    }
    SECTION("deleted symlinks are supported") {
        proto::set_name(pr_remote, "b.txt");
        proto::set_type(pr_remote, proto::FileInfoType::SYMLINK);
        proto::set_symlink_target(pr_remote, "b.txt");
        proto::set_deleted(pr_remote, true);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }
#ifdef SYNCSPIRIT_WIN
    SECTION("prohibed char in path (1)") {
        proto::set_name(pr_remote, "a/|/b.txt");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("prohibed char in path (2)") {
        auto name = std::string("a/x/b.txt");
        name[2] = 0;
        proto::set_name(pr_remote, name);
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("aux.c") {
        proto::set_name(pr_remote, "aux.c");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("aUx.c") {
        proto::set_name(pr_remote, "aux.c");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("aux") {
        proto::set_name(pr_remote, "aux");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("auxX.c") {
        proto::set_name(pr_remote, "auxX.c");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }
    SECTION("con.com") {
        proto::set_name(pr_remote, "con.com");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("/path/con.txt/tail") {
        proto::set_name(pr_remote, "/path/con.txt/tail");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("com9") {
        proto::set_name(pr_remote, "com9");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("com^2") {
        auto name = L"com²";
        proto::set_name(pr_remote, boost::nowide::narrow(name));
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("/LPt7/tail") {
        proto::set_name(pr_remote, "/LPt7/tail");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("/LPt77/tail") {
        proto::set_name(pr_remote, "/LPt77/tail");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }
    SECTION("nuL") {
        proto::set_name(pr_remote, "nuL");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("Prn") {
        proto::set_name(pr_remote, "Prn");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::ignore);
    }
    SECTION("p/api.rst") {
        proto::set_name(pr_remote, "p/api.rst");
        auto file_remote = file_info_t::create(sequencer->next_uuid(), pr_remote, folder_peer).value();
        folder_peer->add_strict(file_remote);
        auto action = resolve(*file_remote, folder_my->get_file_infos().by_name("a.txt").get(), *folder_my);
        CHECK(action == A::remote_copy);
    }
#endif
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
