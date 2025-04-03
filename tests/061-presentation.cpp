// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "presentation/cluster_file_presence.h"
#include "presentation/file_presence.h"
#include "presentation/folder_presence.h"
#include "presentation/folder_entity.h"
#include "syncspirit-config.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::proto;
using namespace syncspirit::model;
using namespace syncspirit::presentation;

using F = file_presence_t::features_t;

TEST_CASE("presentation", "[presentation]") {
    test::init_logging();
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

    auto builder = diff_builder_t(*cluster);

    SECTION("empty folder") {
        SECTION("shared with nobody") {
            REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
            auto folder = cluster->get_folders().by_id("1234-5678");
            CHECK(folder->use_count() == 2);

            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(&folder_entity->get_folder() == folder.get());
            CHECK(folder_entity->get_children().size() == 0);
            CHECK(!folder_entity->get_parent());

            SECTION("check forwarding") {
                struct aug_sample_t : model::augmentation_t {
                    aug_sample_t(int &deleted_, int &updated_) : deleted{deleted_}, updated{updated_} {}
                    void on_update() noexcept { ++updated; };
                    void on_delete() noexcept { ++deleted; };

                    int &deleted;
                    int &updated;
                };

                int deleted = 0;
                int updated = 0;
                auto aug = model::augmentation_ptr_t(new aug_sample_t(deleted, updated));
                folder_entity->set_augmentation(aug);
                REQUIRE(folder->use_count() == 2);

                folder->notify_update();
                CHECK(updated == 1);
                CHECK(deleted == 0);

                REQUIRE(folder->use_count() == 2);
                REQUIRE(folder_entity->use_count() == 2);

                folder_entity.reset();
                CHECK(updated == 1);
                CHECK(deleted == 0);

                SECTION("direct remvoal") {
                    folder.reset();
                    cluster->get_folders().clear();
                    CHECK(updated == 1);
                    CHECK(deleted == 1);
                }
                SECTION("remove via diff") {
                    REQUIRE(builder.remove_folder(*folder).apply());
                    folder.reset();
                    CHECK(updated == 1);
                    CHECK(deleted == 1);
                }
            }

            SECTION("presence") {
                CHECK(!folder_entity->get_presense<folder_presence_t>(*peer_device));
                CHECK(!folder_entity->get_presense<folder_presence_t>(*peer_2_device));

                auto self_presense = folder_entity->get_presense<folder_presence_t>(*my_device);
                CHECK(self_presense);
                CHECK(!self_presense->get_parent());
                CHECK(self_presense->get_presence_feautres() & (F::folder));

                auto my_fi = folder->get_folder_infos().by_device(*my_device);
                CHECK(&self_presense->get_folder_info() == my_fi);
            }
        }

        SECTION("shared with a peer") {
            REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
            REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
            auto folder = cluster->get_folders().by_id("1234-5678");
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(!folder_entity->get_parent());

            auto my_presense = folder_entity->get_presense<folder_presence_t>(*my_device);
            auto peer_presense = folder_entity->get_presense<folder_presence_t>(*peer_device);
            REQUIRE(my_presense);
            REQUIRE(peer_presense);

            CHECK(!folder_entity->get_presense<folder_presence_t>(*peer_2_device));
            auto my_fi = folder->get_folder_infos().by_device(*my_device);
            auto peer_fi = folder->get_folder_infos().by_device(*peer_device);
            CHECK(&my_presense->get_folder_info() == my_fi);
            CHECK(&peer_presense->get_folder_info() == peer_fi);
            CHECK(my_presense->get_presence_feautres() & (F::folder));
            CHECK(peer_presense->get_presence_feautres() & (F::folder));

            CHECK(!my_presense->get_parent());
            CHECK(!peer_presense->get_parent());
        }
    }

    SECTION("non-emtpy folder, flat hierarchy") {
        REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
        REQUIRE(builder.share_folder(peer_2_id.get_sha256(), "1234-5678").apply());
        auto folder = cluster->get_folders().by_id("1234-5678");

        auto add_file = [&](std::string_view name, model::device_t &device) {
            auto folder_info = folder->get_folder_infos().by_device(device);

            proto::FileInfo pr_fi;
            proto::set_name(pr_fi, name);
            proto::set_sequence(pr_fi, folder_info->get_max_sequence() + 1);

            auto &v = proto::get_version(pr_fi);
            proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

            auto file = model::file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info.get()).value();
            folder_info->add_strict(file);
        };

        add_file("f.txt", *peer_device);
        add_file("f.txt", *peer_2_device);
        add_file("e.txt", *peer_2_device);
        add_file("d.txt", *peer_device);
        add_file("c.txt", *my_device);
        add_file("c.txt", *peer_device);
        add_file("c.txt", *peer_2_device);
        add_file("b.txt", *my_device);
        add_file("b.txt", *peer_device);
        add_file("a.txt", *my_device);

        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        CHECK(!folder_entity->get_parent());

        auto my_folder_presence = folder_entity->get_presense<folder_presence_t>(*my_device);
        auto peer_folder_presence = folder_entity->get_presense<folder_presence_t>(*peer_device);
        auto peer_2_folder_presence = folder_entity->get_presense<folder_presence_t>(*peer_2_device);

        auto &children = folder_entity->get_children();
        REQUIRE(children.size() == 6);
        auto it = children.begin();
        auto next_entry = [&]() -> presentation::entity_ptr_t {
            if (it != children.end()) {
                return *it++;
            }
            return {};
        };

        auto file_a = next_entry();
        auto file_b = next_entry();
        auto file_c = next_entry();
        auto file_d = next_entry();
        auto file_e = next_entry();
        auto file_f = next_entry();
        REQUIRE(!next_entry());

        CHECK(file_a->get_name() == "a.txt");
        CHECK(file_b->get_name() == "b.txt");
        CHECK(file_c->get_name() == "c.txt");
        CHECK(file_d->get_name() == "d.txt");
        CHECK(file_e->get_name() == "e.txt");
        CHECK(file_f->get_name() == "f.txt");
        CHECK(file_a->get_parent() == folder_entity);
        CHECK(file_b->get_parent() == folder_entity);
        CHECK(file_c->get_parent() == folder_entity);
        CHECK(file_d->get_parent() == folder_entity);
        CHECK(file_e->get_parent() == folder_entity);
        CHECK(file_f->get_parent() == folder_entity);

        SECTION("only my device has a file (a.txt)") {
            auto f_my = file_a->get_presense<cluster_file_presence_t>(*my_device);
            auto f_peer = file_a->get_presense<file_presence_t>(*peer_device);
            auto f_peer_2 = file_a->get_presense<file_presence_t>(*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_my->get_file_info().get_folder_info()->get_device() == my_device.get());
            CHECK(f_my->get_presence_feautres() & (F::cluster | F::local | F::file));
            CHECK(f_peer->get_presence_feautres() & (F::missing | F::file));
            CHECK(f_peer_2->get_presence_feautres() & (F::missing | F::file));
            CHECK(f_peer == f_peer_2);

            CHECK(f_my->get_parent() == my_folder_presence);
            CHECK(!f_peer->get_parent());
        }

        SECTION("my & peer device have a file (b.txt)") {
            auto f_my = file_b->get_presense<cluster_file_presence_t>(*my_device);
            auto f_peer = file_b->get_presense<file_presence_t>(*peer_device);
            auto f_peer_2 = file_b->get_presense<file_presence_t>(*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_my->get_file_info().get_folder_info()->get_device() == my_device.get());
            CHECK(f_my->get_presence_feautres() & (F::cluster | F::local | F::file));
            CHECK(f_peer->get_presence_feautres() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_presence_feautres() & (F::missing | F::file));

            CHECK(f_my->get_parent() == my_folder_presence);
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(!f_peer_2->get_parent());
        }

        SECTION("all devices have a file (c.txt)") {
            auto f_my = file_c->get_presense<cluster_file_presence_t>(*my_device);
            auto f_peer = file_c->get_presense<cluster_file_presence_t>(*peer_device);
            auto f_peer_2 = file_c->get_presense<cluster_file_presence_t>(*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);
            REQUIRE(f_my->get_file_info().get_folder_info()->get_device() == my_device.get());
            REQUIRE(f_peer->get_file_info().get_folder_info()->get_device() == peer_device.get());
            REQUIRE(f_peer_2->get_file_info().get_folder_info()->get_device() == peer_2_device.get());
            CHECK(f_my->get_presence_feautres() & (F::cluster | F::local | F::file));
            CHECK(f_peer->get_presence_feautres() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_presence_feautres() & (F::cluster | F::peer | F::file));

            CHECK(f_my->get_parent() == my_folder_presence);
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(f_peer_2->get_parent() == peer_2_folder_presence);
        }

        SECTION("only peer device has a file (d.txt)") {
            auto f_my = file_d->get_presense<file_presence_t>(*my_device);
            auto f_peer = file_d->get_presense<cluster_file_presence_t>(*peer_device);
            auto f_peer_2 = file_d->get_presense<file_presence_t>(*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_peer->get_file_info().get_folder_info()->get_device() == peer_device.get());
            CHECK(f_my->get_presence_feautres() & (F::missing | F::file));
            CHECK(f_peer->get_presence_feautres() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_presence_feautres() & (F::missing | F::file));

            CHECK(!f_my->get_parent());
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(!f_peer_2->get_parent());
        }

        SECTION("only peer device has a file (e.txt)") {
            auto f_my = file_e->get_presense<file_presence_t>(*my_device);
            auto f_peer = file_e->get_presense<file_presence_t>(*peer_device);
            auto f_peer_2 = file_e->get_presense<cluster_file_presence_t>(*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_peer_2->get_file_info().get_folder_info()->get_device() == peer_2_device.get());
            CHECK(f_my->get_presence_feautres() & (F::missing | F::file));
            CHECK(f_peer->get_presence_feautres() & (F::missing | F::file));
            CHECK(f_peer_2->get_presence_feautres() & (F::cluster | F::peer | F::file));

            CHECK(!f_my->get_parent());
            CHECK(!f_peer->get_parent());
            CHECK(f_peer_2->get_parent() == peer_2_folder_presence);
        }

        SECTION("only peer devices have a file (f.txt)") {
            auto f_my = file_f->get_presense<file_presence_t>(*my_device);
            auto f_peer = file_f->get_presense<cluster_file_presence_t>(*peer_device);
            auto f_peer_2 = file_f->get_presense<cluster_file_presence_t>(*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_peer->get_file_info().get_folder_info()->get_device() == peer_device.get());
            REQUIRE(f_peer_2->get_file_info().get_folder_info()->get_device() == peer_2_device.get());
            CHECK(f_my->get_presence_feautres() & (F::missing | F::file));
            CHECK(f_peer->get_presence_feautres() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_presence_feautres() & (F::cluster | F::peer | F::file));

            CHECK(!f_my->get_parent());
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(f_peer_2->get_parent() == peer_2_folder_presence);
        }
    }

    SECTION("folder shared with a peer, simple hierarchy") {
        REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
        auto folder = cluster->get_folders().by_id("1234-5678");

        auto add_file = [&](std::string_view name, model::device_t &device,
                            proto::FileInfoType type = proto::FileInfoType::DIRECTORY) {
            auto folder_info = folder->get_folder_infos().by_device(device);

            proto::FileInfo pr_fi;
            proto::set_name(pr_fi, name);
            proto::set_type(pr_fi, type);
            proto::set_sequence(pr_fi, folder_info->get_max_sequence() + 1);

            auto &v = proto::get_version(pr_fi);
            proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 1));

            auto file = model::file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info.get()).value();
            folder_info->add_strict(file);
            return file;
        };

        SECTION("create hierarchy up-front") {
            REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
            add_file("a", *my_device);
            add_file("a/b", *my_device);
            add_file("a/b/c", *my_device);
            add_file("a/b/c/d", *my_device);
            add_file("a/b/c/d/e.txt", *my_device, proto::FileInfoType::FILE);
            add_file("a", *peer_device);
            add_file("a/b", *peer_device);
            add_file("a/b/c", *peer_device);

            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(!folder_entity->get_parent());

            auto p_folder_my = folder_entity->get_presense<folder_presence_t>(*my_device);
            auto p_folder_peer = folder_entity->get_presense<folder_presence_t>(*peer_device);
            REQUIRE(p_folder_my);
            REQUIRE(p_folder_peer);
            CHECK(!p_folder_my->get_parent());
            CHECK(!p_folder_peer->get_parent());

            auto &children = folder_entity->get_children();
            REQUIRE(children.size() == 1);
            auto &dir_a_entry = *children.begin();
            REQUIRE(dir_a_entry->get_name() == "a");
            REQUIRE(dir_a_entry->get_children().size() == 1);
            CHECK(dir_a_entry->get_parent() == folder_entity);
            auto p_dir_a_my = dir_a_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_a_peer = dir_a_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_a_my->get_presence_feautres() & (F::file | F::local));
            CHECK(p_dir_a_peer->get_presence_feautres() & (F::file | F::peer));
            CHECK(p_dir_a_my->get_parent() == p_folder_my);
            CHECK(p_dir_a_peer->get_parent() == p_folder_peer);

            auto &dir_b_entry = *dir_a_entry->get_children().begin();
            REQUIRE(dir_b_entry->get_name() == "b");
            REQUIRE(dir_b_entry->get_children().size() == 1);
            CHECK(dir_b_entry->get_parent() == dir_a_entry);
            auto p_dir_b_my = dir_b_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_b_peer = dir_b_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_b_my->get_presence_feautres() & (F::file | F::local));
            CHECK(p_dir_b_peer->get_presence_feautres() & (F::file | F::peer));
            CHECK(p_dir_b_my->get_parent() == p_dir_a_my);
            CHECK(p_dir_b_peer->get_parent() == p_dir_a_peer);

            auto &dir_c_entry = *dir_b_entry->get_children().begin();
            REQUIRE(dir_c_entry->get_name() == "c");
            REQUIRE(dir_c_entry->get_children().size() == 1);
            CHECK(dir_c_entry->get_parent() == dir_b_entry);
            auto p_dir_c_my = dir_c_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_c_peer = dir_c_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_c_my->get_presence_feautres() & (F::file | F::local));
            CHECK(p_dir_c_peer->get_presence_feautres() & (F::file | F::peer));
            CHECK(p_dir_c_my->get_parent() == p_dir_b_my);
            CHECK(p_dir_c_peer->get_parent() == p_dir_b_peer);

            auto &dir_d_entry = *dir_c_entry->get_children().begin();
            REQUIRE(dir_d_entry->get_name() == "d");
            REQUIRE(dir_d_entry->get_children().size() == 1);
            CHECK(dir_d_entry->get_parent() == dir_c_entry);
            auto p_dir_d_my = dir_d_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_d_peer = dir_d_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_d_my->get_presence_feautres() & (F::file | F::local));
            CHECK(p_dir_d_peer->get_presence_feautres() & (F::file | F::missing));
            CHECK(p_dir_d_my->get_parent() == p_dir_c_my);
            CHECK(!p_dir_d_peer->get_parent());

            auto &file_e_entry = *dir_d_entry->get_children().begin();
            REQUIRE(file_e_entry->get_name() == "e.txt");
            REQUIRE(file_e_entry->get_children().size() == 0);
            CHECK(file_e_entry->get_parent() == dir_d_entry);
            auto p_file_my = file_e_entry->get_presense<file_presence_t>(*my_device);
            auto p_file_peer = file_e_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_file_my->get_presence_feautres() & (F::file | F::local));
            CHECK(p_file_peer->get_presence_feautres() & (F::file | F::missing));
            CHECK(p_file_my->get_parent() == p_dir_d_my);
            CHECK(!p_file_peer->get_parent());
        }

        SECTION("dynamically create hierarchy") {
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(!folder_entity->get_parent());

            REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
            add_file("a", *peer_device);
            add_file("a/b", *peer_device);

            auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
            folder_entity->on_insert(*fi_peer);

            auto p_folder_my = folder_entity->get_presense<folder_presence_t>(*my_device);
            auto p_folder_peer = folder_entity->get_presense<folder_presence_t>(*peer_device);
            REQUIRE(p_folder_my);
            REQUIRE(p_folder_peer);
            CHECK(!p_folder_my->get_parent());
            CHECK(!p_folder_peer->get_parent());

            auto &children = folder_entity->get_children();
            REQUIRE(children.size() == 1);
            auto &dir_a_entry = *children.begin();
            REQUIRE(dir_a_entry->get_name() == "a");
            REQUIRE(dir_a_entry->get_children().size() == 1);
            CHECK(dir_a_entry->get_parent() == folder_entity);
            auto p_dir_a_my = dir_a_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_a_peer = dir_a_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_a_my->get_presence_feautres() & (F::file | F::missing));
            CHECK(p_dir_a_peer->get_presence_feautres() & (F::file | F::peer));
            CHECK(!p_dir_a_my->get_parent());
            CHECK(p_dir_a_peer->get_parent() == p_folder_peer);

            auto &dir_b_entry = *dir_a_entry->get_children().begin();
            REQUIRE(dir_b_entry->get_name() == "b");
            REQUIRE(dir_b_entry->get_children().size() == 0);
            CHECK(dir_b_entry->get_parent() == dir_a_entry);
            auto p_dir_b_my = dir_b_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_b_peer = dir_b_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_b_my->get_presence_feautres() & (F::file | F::missing));
            CHECK(p_dir_b_peer->get_presence_feautres() & (F::file | F::peer));
            CHECK(!p_dir_b_my->get_parent());
            CHECK(p_dir_b_peer->get_parent() == p_dir_a_peer);

            auto file_c = add_file("a/b/c", *peer_device);
            folder_entity->on_insert(*file_c);
            REQUIRE(dir_b_entry->get_children().size() == 1);
            auto &dir_c_entry = *dir_b_entry->get_children().begin();
            REQUIRE(dir_c_entry->get_name() == "c");
            REQUIRE(dir_c_entry->get_children().size() == 0);
            CHECK(dir_c_entry->get_parent() == dir_b_entry);
            auto p_dir_c_my = dir_c_entry->get_presense<file_presence_t>(*my_device);
            auto p_dir_c_peer = dir_c_entry->get_presense<file_presence_t>(*peer_device);
            CHECK(p_dir_c_my->get_presence_feautres() & (F::file | F::local));
            CHECK(p_dir_c_peer->get_presence_feautres() & (F::file | F::peer));
            CHECK(!p_dir_c_my->get_parent());
            CHECK(p_dir_c_peer->get_parent() == p_dir_b_peer);
        }
    }
}
