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
#include <memory_resource>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::proto;
using namespace syncspirit::model;
using namespace syncspirit::presentation;

using F = file_presence_t::features_t;

namespace Catch {
template <> struct StringMaker<presence_stats_t> {
    static std::string convert(const presence_stats_t &value) {
        return fmt::format("presence state, entities[{}, local: {}, cluster: {}], size = {}", value.entities,
                           value.local_entries, value.cluster_entries, value.size);
    }
};
template <> struct StringMaker<entity_stats_t> {
    static std::string convert(const entity_stats_t &value) {
        return fmt::format("entity stats entities [{}], size = {}", value.entities, value.size);
    }
};

} // namespace Catch

struct aug_sample_t : model::augmentation_t {
    aug_sample_t(int &deleted_, int &updated_) : deleted{deleted_}, updated{updated_} {}
    void on_update() noexcept { ++updated; };
    void on_delete() noexcept { ++deleted; };

    int &deleted;
    int &updated;
};

TEST_CASE("path", "[presentation]") {
    using pieces_t = std::vector<std::string_view>;
    SECTION("a/bb/c.txt") {
        auto p = path_t("a/bb/c.txt");
        CHECK(p.get_parent_name() == "a/bb");
        CHECK(p.get_own_name() == "c.txt");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 3);
        CHECK(pieces[0] == "a");
        CHECK(pieces[1] == "bb");
        CHECK(pieces[2] == "c.txt");
        CHECK(p.contains(p));
        CHECK(!p.contains(path_t("a/bb/c.tx")));
        CHECK(!p.contains(path_t("a/bb/c.x")));
        CHECK(!p.contains(path_t("a/bb/c")));
        CHECK(!p.contains(path_t("a/bb")));
        CHECK(!p.contains(path_t("a")));
        CHECK(path_t("a").contains(p));
        CHECK(path_t("a/").contains(p));
        CHECK(path_t("a/b").contains(p));
        CHECK(path_t("a/bb/c").contains(p));
    }
    SECTION("dir/file.bin") {
        auto p = path_t("dir/file.bin");
        CHECK(p.get_parent_name() == "dir");
        CHECK(p.get_own_name() == "file.bin");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 2);
        CHECK(pieces[0] == "dir");
        CHECK(pieces[1] == "file.bin");
    }
}

auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
auto peer_2_id =
    model::device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW").value();
auto my_device = device_t::create(my_id, "my-device").value();
auto peer_device = device_t::create(peer_id, "peer-device").value();
auto peer_2_device = device_t::create(peer_2_id, "peer-device-2").value();

auto my_device_id = my_device->device_id().get_uint();
auto peer_device_id = peer_device->device_id().get_uint();

TEST_CASE("presentation", "[presentation]") {
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
                int deleted = 0;
                int updated = 0;
                auto aug = model::augmentation_ptr_t(new aug_sample_t(deleted, updated));
                folder_entity->set_augmentation(aug);
                REQUIRE(folder->use_count() == 2);

                folder->notify_update();
                CHECK(updated == 1);
                CHECK(deleted == 0);

                REQUIRE(folder->use_count() == 2);
                REQUIRE(folder_entity->use_count() >= 2);

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
                CHECK(!folder_entity->get_presence(&*peer_device));
                CHECK(!folder_entity->get_presence(&*peer_2_device));

                auto self_presense = static_cast<folder_presence_t *>(folder_entity->get_presence(&*my_device));
                CHECK(self_presense);
                CHECK(!self_presense->get_parent());
                CHECK(self_presense->get_features() & (F::folder));

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

            auto my_presense = static_cast<folder_presence_t *>(folder_entity->get_presence(&*my_device));
            auto peer_presense = static_cast<folder_presence_t *>(folder_entity->get_presence(&*peer_device));
            REQUIRE(my_presense);
            REQUIRE(peer_presense);

            CHECK(!folder_entity->get_presence(&*peer_2_device));
            auto my_fi = folder->get_folder_infos().by_device(*my_device);
            auto peer_fi = folder->get_folder_infos().by_device(*peer_device);
            CHECK(&my_presense->get_folder_info() == my_fi);
            CHECK(&peer_presense->get_folder_info() == peer_fi);
            CHECK(my_presense->get_features() & (F::folder));
            CHECK(peer_presense->get_features() & (F::folder));

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

        auto my_folder_presence = folder_entity->get_presence(&*my_device);
        auto peer_folder_presence = folder_entity->get_presence(&*peer_device);
        auto peer_2_folder_presence = folder_entity->get_presence(&*peer_2_device);

        auto &children = folder_entity->get_children();
        REQUIRE(children.size() == 6);
        auto it = children.begin();
        auto next_entity = [&]() -> presentation::entity_ptr_t {
            if (it != children.end()) {
                return *it++;
            }
            return {};
        };

        auto file_a = next_entity();
        auto file_b = next_entity();
        auto file_c = next_entity();
        auto file_d = next_entity();
        auto file_e = next_entity();
        auto file_f = next_entity();
        REQUIRE(!next_entity());

        CHECK(file_a->get_path().get_own_name() == "a.txt");
        CHECK(file_b->get_path().get_own_name() == "b.txt");
        CHECK(file_c->get_path().get_own_name() == "c.txt");
        CHECK(file_d->get_path().get_own_name() == "d.txt");
        CHECK(file_e->get_path().get_own_name() == "e.txt");
        CHECK(file_f->get_path().get_own_name() == "f.txt");
        CHECK(file_a->get_parent() == folder_entity);
        CHECK(file_b->get_parent() == folder_entity);
        CHECK(file_c->get_parent() == folder_entity);
        CHECK(file_d->get_parent() == folder_entity);
        CHECK(file_e->get_parent() == folder_entity);
        CHECK(file_f->get_parent() == folder_entity);

        SECTION("only my device has a file (a.txt)") {
            auto f_my = static_cast<cluster_file_presence_t *>(file_a->get_presence(&*my_device));
            auto f_peer = static_cast<file_presence_t *>(file_a->get_presence(&*peer_device));
            auto f_peer_2 = static_cast<file_presence_t *>(file_a->get_presence(&*peer_2_device));

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_my->get_file_info().get_folder_info()->get_device() == my_device.get());
            CHECK(f_my->get_features() & (F::cluster | F::local | F::file));
            CHECK(f_peer->get_features() & (F::missing | F::file));
            CHECK(f_peer_2->get_features() & (F::missing | F::file));
            CHECK(f_peer == f_peer_2);

            CHECK(f_my->get_parent() == my_folder_presence);
            CHECK(!f_peer->get_parent());
        }

        SECTION("my & peer device have a file (b.txt)") {
            auto f_my = static_cast<cluster_file_presence_t *>(file_b->get_presence(&*my_device));
            auto f_peer = file_b->get_presence(&*peer_device);
            auto f_peer_2 = file_b->get_presence(&*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_my->get_file_info().get_folder_info()->get_device() == my_device.get());
            CHECK(f_my->get_features() & (F::cluster | F::local | F::file));
            CHECK(f_peer->get_features() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_features() & (F::missing | F::file));

            CHECK(f_my->get_parent() == my_folder_presence);
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(!f_peer_2->get_parent());
        }

        SECTION("all devices have a file (c.txt)") {
            auto f_my = static_cast<cluster_file_presence_t *>(file_c->get_presence(&*my_device));
            auto f_peer = static_cast<cluster_file_presence_t *>(file_c->get_presence(&*peer_device));
            auto f_peer_2 = static_cast<cluster_file_presence_t *>(file_c->get_presence(&*peer_2_device));

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);
            REQUIRE(f_my->get_file_info().get_folder_info()->get_device() == my_device.get());
            REQUIRE(f_peer->get_file_info().get_folder_info()->get_device() == peer_device.get());
            REQUIRE(f_peer_2->get_file_info().get_folder_info()->get_device() == peer_2_device.get());
            CHECK(f_my->get_features() & (F::cluster | F::local | F::file));
            CHECK(f_peer->get_features() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_features() & (F::cluster | F::peer | F::file));

            CHECK(f_my->get_parent() == my_folder_presence);
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(f_peer_2->get_parent() == peer_2_folder_presence);
        }

        SECTION("only peer device has a file (d.txt)") {
            auto f_my = file_d->get_presence(&*my_device);
            auto f_peer = static_cast<cluster_file_presence_t *>(file_d->get_presence(&*peer_device));
            auto f_peer_2 = file_d->get_presence(&*peer_2_device);

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_peer->get_file_info().get_folder_info()->get_device() == peer_device.get());
            CHECK(f_my->get_features() & (F::missing | F::file));
            CHECK(f_peer->get_features() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_features() & (F::missing | F::file));

            CHECK(!f_my->get_parent());
            CHECK(f_peer->get_parent() == peer_folder_presence);
            CHECK(!f_peer_2->get_parent());
        }

        SECTION("only peer device has a file (e.txt)") {
            auto f_my = file_e->get_presence(&*my_device);
            auto f_peer = file_e->get_presence(&*peer_device);
            auto f_peer_2 = static_cast<cluster_file_presence_t *>(file_e->get_presence(&*peer_2_device));

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_peer_2->get_file_info().get_folder_info()->get_device() == peer_2_device.get());
            CHECK(f_my->get_features() & (F::missing | F::file));
            CHECK(f_peer->get_features() & (F::missing | F::file));
            CHECK(f_peer_2->get_features() & (F::cluster | F::peer | F::file));

            CHECK(!f_my->get_parent());
            CHECK(!f_peer->get_parent());
            CHECK(f_peer_2->get_parent() == peer_2_folder_presence);
        }

        SECTION("only peer devices have a file (f.txt)") {
            auto f_my = file_f->get_presence(&*my_device);
            auto f_peer = static_cast<cluster_file_presence_t *>(file_f->get_presence(&*peer_device));
            auto f_peer_2 = static_cast<cluster_file_presence_t *>(file_f->get_presence(&*peer_2_device));

            REQUIRE(f_my);
            REQUIRE(f_peer);
            REQUIRE(f_peer_2);

            REQUIRE(f_peer->get_file_info().get_folder_info()->get_device() == peer_device.get());
            REQUIRE(f_peer_2->get_file_info().get_folder_info()->get_device() == peer_2_device.get());
            CHECK(f_my->get_features() & (F::missing | F::file));
            CHECK(f_peer->get_features() & (F::cluster | F::peer | F::file));
            CHECK(f_peer_2->get_features() & (F::cluster | F::peer | F::file));

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

            auto p_folder_my = folder_entity->get_presence(&*my_device);
            auto p_folder_peer = folder_entity->get_presence(&*peer_device);
            REQUIRE(p_folder_my);
            REQUIRE(p_folder_peer);
            CHECK(!p_folder_my->get_parent());
            CHECK(!p_folder_peer->get_parent());

            auto &children = folder_entity->get_children();
            REQUIRE(children.size() == 1);
            auto &dir_a_entity = *children.begin();
            REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
            REQUIRE(dir_a_entity->get_children().size() == 1);
            CHECK(dir_a_entity->get_parent() == folder_entity);
            auto p_dir_a_my = dir_a_entity->get_presence(&*my_device);
            auto p_dir_a_peer = dir_a_entity->get_presence(&*peer_device);
            CHECK(p_dir_a_my->get_features() & (F::directory | F::local));
            CHECK(p_dir_a_peer->get_features() & (F::directory | F::peer));
            CHECK(p_dir_a_my->get_parent() == p_folder_my);
            CHECK(p_dir_a_peer->get_parent() == p_folder_peer);

            auto &dir_b_entity = *dir_a_entity->get_children().begin();
            REQUIRE(dir_b_entity->get_path().get_own_name() == "b");
            REQUIRE(dir_b_entity->get_children().size() == 1);
            CHECK(dir_b_entity->get_parent() == dir_a_entity);
            auto p_dir_b_my = dir_b_entity->get_presence(&*my_device);
            auto p_dir_b_peer = dir_b_entity->get_presence(&*peer_device);
            CHECK(p_dir_b_my->get_features() & (F::directory | F::local));
            CHECK(p_dir_b_peer->get_features() & (F::directory | F::peer));
            CHECK(p_dir_b_my->get_parent() == p_dir_a_my);
            CHECK(p_dir_b_peer->get_parent() == p_dir_a_peer);

            auto &dir_c_entity = *dir_b_entity->get_children().begin();
            REQUIRE(dir_c_entity->get_path().get_own_name() == "c");
            REQUIRE(dir_c_entity->get_children().size() == 1);
            CHECK(dir_c_entity->get_parent() == dir_b_entity);
            auto p_dir_c_my = dir_c_entity->get_presence(&*my_device);
            auto p_dir_c_peer = dir_c_entity->get_presence(&*peer_device);
            CHECK(p_dir_c_my->get_features() & (F::directory | F::local));
            CHECK(p_dir_c_peer->get_features() & (F::directory | F::peer));
            CHECK(p_dir_c_my->get_parent() == p_dir_b_my);
            CHECK(p_dir_c_peer->get_parent() == p_dir_b_peer);

            auto &dir_d_entity = *dir_c_entity->get_children().begin();
            REQUIRE(dir_d_entity->get_path().get_own_name() == "d");
            REQUIRE(dir_d_entity->get_children().size() == 1);
            CHECK(dir_d_entity->get_parent() == dir_c_entity);
            auto p_dir_d_my = dir_d_entity->get_presence(&*my_device);
            auto p_dir_d_peer = dir_d_entity->get_presence(&*peer_device);
            CHECK(p_dir_d_my->get_features() & (F::directory | F::local));
            CHECK(p_dir_d_peer->get_features() & (F::directory | F::missing));
            CHECK(p_dir_d_my->get_parent() == p_dir_c_my);
            CHECK(!p_dir_d_peer->get_parent());

            auto &file_e_entity = *dir_d_entity->get_children().begin();
            REQUIRE(file_e_entity->get_path().get_own_name() == "e.txt");
            REQUIRE(file_e_entity->get_children().size() == 0);
            CHECK(file_e_entity->get_parent() == dir_d_entity);
            auto p_file_my = file_e_entity->get_presence(&*my_device);
            auto p_file_peer = file_e_entity->get_presence(&*peer_device);
            CHECK(p_file_my->get_features() & (F::file | F::local));
            CHECK(p_file_peer->get_features() & (F::file | F::missing));
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

            auto p_folder_my = folder_entity->get_presence(&*my_device);
            auto p_folder_peer = folder_entity->get_presence(&*peer_device);
            REQUIRE(p_folder_my);
            REQUIRE(p_folder_peer);
            CHECK(!p_folder_my->get_parent());
            CHECK(!p_folder_peer->get_parent());

            auto &children = folder_entity->get_children();
            REQUIRE(children.size() == 1);
            auto &dir_a_entity = *children.begin();
            REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
            REQUIRE(dir_a_entity->get_children().size() == 1);
            CHECK(dir_a_entity->get_parent() == folder_entity);
            auto p_dir_a_my = dir_a_entity->get_presence(&*my_device);
            auto p_dir_a_peer = dir_a_entity->get_presence(&*peer_device);
            CHECK(p_dir_a_my->get_features() & (F::directory | F::missing));
            CHECK(p_dir_a_peer->get_features() & (F::directory | F::peer));
            CHECK(!p_dir_a_my->get_parent());
            CHECK(p_dir_a_peer->get_parent() == p_folder_peer);

            auto &dir_b_entity = *dir_a_entity->get_children().begin();
            REQUIRE(dir_b_entity->get_path().get_own_name() == "b");
            REQUIRE(dir_b_entity->get_children().size() == 0);
            CHECK(dir_b_entity->get_parent() == dir_a_entity);
            auto p_dir_b_my = dir_b_entity->get_presence(&*my_device);
            auto p_dir_b_peer = dir_b_entity->get_presence(&*peer_device);
            CHECK(p_dir_b_my->get_features() & (F::directory | F::missing));
            CHECK(p_dir_b_peer->get_features() & (F::directory | F::peer));
            CHECK(!p_dir_b_my->get_parent());
            CHECK(p_dir_b_peer->get_parent() == p_dir_a_peer);

            auto file_c_peer = add_file("a/b/c", *peer_device);
            folder_entity->on_insert(*file_c_peer);
            folder_entity->on_insert(*file_c_peer); // should be ignored
            REQUIRE(dir_b_entity->get_children().size() == 1);
            auto &dir_c_entity = *dir_b_entity->get_children().begin();
            REQUIRE(dir_c_entity->get_path().get_own_name() == "c");
            REQUIRE(dir_c_entity->get_children().size() == 0);
            CHECK(dir_c_entity->get_parent() == dir_b_entity);
            auto p_dir_c_my = dir_c_entity->get_presence(&*my_device);
            auto p_dir_c_peer = dir_c_entity->get_presence(&*peer_device);
            CHECK(p_dir_c_my->get_features() & (F::missing));
            CHECK(p_dir_c_peer->get_features() & (F::directory | F::peer | F::cluster));
            CHECK(!p_dir_c_my->get_parent());
            CHECK(p_dir_c_peer->get_parent() == p_dir_b_peer);

            auto file_a_my = add_file("a", *my_device);
            folder_entity->on_insert(*file_a_my);
            REQUIRE(children.size() == 1);
            p_dir_a_my = dir_a_entity->get_presence(&*my_device);
            CHECK(p_dir_a_my->get_parent() == p_folder_my);
        }
        SECTION("dynamically create hierarchy-2") {
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(!folder_entity->get_parent());

            auto file_1 = add_file("a-file", *my_device, proto::FileInfoType::FILE);
            auto e_file_1 = folder_entity->on_insert(*file_1);
            REQUIRE(e_file_1);
            CHECK(e_file_1->get_path().get_full_name() == file_1->get_name());

            auto file_3 = add_file("a-file-3", *my_device, proto::FileInfoType::FILE);
            auto e_file_3 = folder_entity->on_insert(*file_3);
            REQUIRE(e_file_3);
            CHECK(e_file_3->get_path().get_full_name() == file_3->get_name());

            auto file_2 = add_file("a-file-2", *my_device, proto::FileInfoType::FILE);
            auto e_file_2 = folder_entity->on_insert(*file_2);
            REQUIRE(e_file_2);
            CHECK(e_file_2->get_path().get_full_name() == file_2->get_name());
        }
        SECTION("presence & entity hierarchy order") {
            REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
            auto file_1 = add_file("file-a", *my_device, proto::FileInfoType::FILE);
            auto file_2 = add_file("file-b", *my_device, proto::FileInfoType::DIRECTORY);

            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            auto &children = folder_entity->get_children();
            auto it_children = children.begin();
            auto &c1 = *it_children++;
            auto &c2 = *it_children++;
            CHECK(c1->get_path().get_full_name() == "file-a");
            CHECK(c2->get_path().get_full_name() == "file-b");

            auto &p_my_children = folder_entity->get_presence(&*my_device)->get_children();
            REQUIRE(p_my_children.size() == 2);
            CHECK(p_my_children[0]->get_entity()->get_path().get_full_name() == "file-b");
            CHECK(p_my_children[1]->get_entity()->get_path().get_full_name() == "file-a");

            auto &p_peer_children = folder_entity->get_presence(&*peer_device)->get_children();
            REQUIRE(p_peer_children.size() == 2);
            CHECK(p_peer_children[0]->get_entity()->get_path().get_full_name() == "file-b");
            CHECK(p_peer_children[1]->get_entity()->get_path().get_full_name() == "file-a");
        }
        SECTION("orphans") {
            SECTION("simple") {
                add_file("a/b", *my_device);

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(!folder_entity->get_parent());

                auto &children = folder_entity->get_children();
                REQUIRE(children.size() == 0);

                auto dir_a_my = add_file("a", *my_device);
                folder_entity->on_insert(*dir_a_my);

                REQUIRE(children.size() == 1);
                auto &dir_a_entity = *children.begin();
                REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
                REQUIRE(dir_a_entity->get_children().size() == 1);
                CHECK(dir_a_entity->get_parent() == folder_entity);
                auto p_dir_a_my = dir_a_entity->get_presence(&*my_device);
                CHECK(p_dir_a_my->get_features() & (F::directory | F::local));

                auto &dir_b_entity = *dir_a_entity->get_children().begin();
                REQUIRE(dir_b_entity->get_path().get_own_name() == "b");
                REQUIRE(dir_b_entity->get_children().size() == 0);
                CHECK(dir_b_entity->get_parent() == dir_a_entity);
                auto p_dir_b_my = dir_b_entity->get_presence(&*my_device);
                CHECK(p_dir_b_my->get_features() & (F::directory | F::local));
            }
            SECTION("lazy") {
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));

                auto f_b = add_file("a/b", *my_device);
                auto f_c = add_file("a/b/c", *my_device);

                folder_entity->on_insert(*f_b);
                folder_entity->on_insert(*f_c);

                auto f_a = add_file("a", *my_device);
                folder_entity->on_insert(*f_a);

                auto &children = folder_entity->get_children();
                REQUIRE(children.size() == 1);

                auto &dir_a_entity = *children.begin();
                REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
                REQUIRE(dir_a_entity->get_children().size() == 1);
                CHECK(dir_a_entity->get_parent() == folder_entity);

                auto &dir_b_entity = *dir_a_entity->get_children().begin();
                REQUIRE(dir_b_entity->get_path().get_own_name() == "b");
                REQUIRE(dir_b_entity->get_children().size() == 1);
                CHECK(dir_b_entity->get_parent() == dir_a_entity);

                auto &dir_c_entity = *dir_b_entity->get_children().begin();
                REQUIRE(dir_c_entity->get_path().get_own_name() == "c");
                REQUIRE(dir_c_entity->get_children().size() == 0);
                CHECK(dir_c_entity->get_parent() == dir_b_entity);
            }
            SECTION("lazy, with peer") {
                REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                auto f_b_my = add_file("a/b.bin", *my_device);
                auto f_b_peer = add_file("a/b.bin", *peer_device);
                folder_entity->on_insert(*f_b_my);
                folder_entity->on_insert(*f_b_peer);

                auto f_a_my = add_file("a", *my_device);
                folder_entity->on_insert(*f_a_my);

                auto f_a_peer = add_file("a", *peer_device);
                folder_entity->on_insert(*f_a_peer);

                auto &children = folder_entity->get_children();
                REQUIRE(children.size() == 1);

                auto &dir_a_entity = *children.begin();
                REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
                REQUIRE(dir_a_entity->get_children().size() == 1);
                CHECK(dir_a_entity->get_parent() == folder_entity);

                auto p_dir_a_my = dir_a_entity->get_presence(&*my_device);
                auto p_dir_a_peer = dir_a_entity->get_presence(&*peer_device);
                CHECK(p_dir_a_my->get_features() & (F::directory | F::local));
                CHECK(p_dir_a_peer->get_features() & (F::directory | F::peer));
                CHECK(p_dir_a_my->get_parent());

                auto &file_b_entity = *dir_a_entity->get_children().begin();
                REQUIRE(file_b_entity->get_path().get_own_name() == "b.bin");
                REQUIRE(file_b_entity->get_children().size() == 0);
                CHECK(file_b_entity->get_parent() == dir_a_entity);

                auto p_file_my = file_b_entity->get_presence(&*my_device);
                auto p_file_peer = file_b_entity->get_presence(&*peer_device);
                CHECK(p_file_my->get_features() & (F::file | F::local));
                CHECK(p_file_peer->get_features() & (F::file | F::peer));
                CHECK(p_file_my->get_parent() == p_dir_a_my);
                CHECK(p_file_peer->get_parent() == p_dir_a_peer);
            }
        }
        SECTION("removal") {
            REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
            SECTION("empty folder info removal") {
                auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(!folder_entity->get_parent());

                REQUIRE(folder_entity->get_presence(&*my_device));
                REQUIRE(folder_entity->get_presence(&*peer_device));

                REQUIRE(builder.unshare_folder(*fi_peer).apply());

                CHECK(folder_entity->get_presence(&*my_device));
                CHECK(!folder_entity->get_presence(&*peer_device));
            }
            SECTION("folder info with a file removal") {
                auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();
                add_file("a", *peer_device);

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(!folder_entity->get_parent());

                REQUIRE(folder_entity->get_presence(&*my_device));
                REQUIRE(folder_entity->get_presence(&*peer_device));

                REQUIRE(builder.unshare_folder(*fi_peer).apply());

                CHECK(folder_entity->get_presence(&*my_device));
                CHECK(!folder_entity->get_presence(&*peer_device));
            }
            SECTION("folder info with a file hierarchy removal") {
                auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();
                add_file("a", *peer_device);
                add_file("a/b", *peer_device);
                add_file("a/b/c", *peer_device);

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(!folder_entity->get_parent());

                REQUIRE(folder_entity->get_presence(&*my_device));
                REQUIRE(folder_entity->get_presence(&*peer_device));

                REQUIRE(builder.unshare_folder(*fi_peer).apply());

                CHECK(folder_entity->get_presence(&*my_device));
                CHECK(!folder_entity->get_presence(&*peer_device));
            }
            SECTION("folder info with a file hierarchy removal (2)") {
                auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();
                add_file("a", *my_device);
                add_file("a", *peer_device);
                add_file("a/b", *peer_device);

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(!folder_entity->get_parent());

                REQUIRE(folder_entity->get_presence(&*my_device));
                REQUIRE(folder_entity->get_presence(&*peer_device));
                REQUIRE(folder_entity->get_children().size() == 1);

                auto &dir_a_entity = *folder_entity->get_children().begin();
                REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
                REQUIRE(dir_a_entity->get_children().size() == 1);

                REQUIRE(builder.unshare_folder(*fi_peer).apply());

                CHECK(folder_entity->get_presence(&*my_device));
                CHECK(!folder_entity->get_presence(&*peer_device));

                REQUIRE(dir_a_entity->get_children().size() == 0);
            }
            SECTION("folder info with a file hierarchy removal (3)") {
                using strings_t = std::vector<std::string_view>;

                struct sample_aug_t : model::augmentation_t {
                    sample_aug_t(strings_t &strings_, std::string_view name_) : strings{strings_}, name{name_} {}
                    ~sample_aug_t() { strings.emplace_back(name); }

                    strings_t &strings;
                    std::string_view name;
                };

                auto strings = strings_t();
                auto augment = [&](entity_t *presense, std::string_view name) {
                    auto aug = model::augmentation_ptr_t();
                    aug = new sample_aug_t(strings, name);
                    presense->set_augmentation(aug);
                };
                {
                    auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();
                    add_file("a", *peer_device);
                    add_file("a/b", *peer_device);
                    add_file("a/b/c", *peer_device);

                    auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                    CHECK(!folder_entity->get_parent());

                    auto &dir_a_entity = *folder_entity->get_children().begin();
                    REQUIRE(dir_a_entity->get_path().get_own_name() == "a");
                    REQUIRE(dir_a_entity->get_children().size() == 1);

                    auto &dir_b_entity = *dir_a_entity->get_children().begin();
                    REQUIRE(dir_b_entity->get_path().get_own_name() == "b");
                    REQUIRE(dir_a_entity->get_children().size() == 1);

                    auto &file_c_entity = *dir_b_entity->get_children().begin();
                    REQUIRE(file_c_entity->get_path().get_own_name() == "c");
                    REQUIRE(file_c_entity->get_children().size() == 0);

                    augment(dir_a_entity, "a");
                    augment(dir_b_entity, "a/b");
                    augment(file_c_entity, "a/b/c");

                    REQUIRE(folder_entity->get_presence(&*my_device));

                    auto p_folder_peer = folder_entity->get_presence(&*peer_device);
                    REQUIRE(p_folder_peer);

                    REQUIRE(builder.unshare_folder(*fi_peer).apply());

                    CHECK(folder_entity->get_presence(&*my_device));
                    CHECK(!folder_entity->get_presence(&*peer_device));
                }

                REQUIRE(strings.size() == 3);
                CHECK(strings[0] == "a/b/c");
                CHECK(strings[1] == "a/b");
                CHECK(strings[2] == "a");
            }
            SECTION("folder info with an orphan removal") {
                auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();
                add_file("a/b", *peer_device);

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(!folder_entity->get_parent());

                REQUIRE(folder_entity->get_presence(&*my_device));
                REQUIRE(folder_entity->get_presence(&*peer_device));

                REQUIRE(builder.unshare_folder(*fi_peer).apply());

                CHECK(folder_entity->get_presence(&*my_device));
                CHECK(!folder_entity->get_presence(&*peer_device));
            }
        }
    }
}

TEST_CASE("statistics, update", "[presentation]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    auto folder = cluster->get_folders().by_id("1234-5678");
    auto &blocks = cluster->get_blocks();

    auto add_file = [&](std::string_view name, model::device_t &device, std::int32_t file_size = 0,
                        proto::FileInfoType type = proto::FileInfoType::DIRECTORY, std::uint64_t modified_by = 0,
                        std::uint64_t modified_v = 0) {
        auto folder_info = folder->get_folder_infos().by_device(device);

        proto::FileInfo pr_fi;
        proto::set_name(pr_fi, name);
        proto::set_type(pr_fi, type);
        proto::set_sequence(pr_fi, folder_info->get_max_sequence() + 1);

        auto block = model::block_info_ptr_t();
        if (file_size) {
            proto::set_size(pr_fi, file_size);
            proto::set_block_size(pr_fi, file_size);
            assert(type == proto::FileInfoType::FILE);
            auto bytes = utils::bytes_t(file_size);
            auto b_hash = utils::sha256_digest(bytes).value();
            auto &b = proto::add_blocks(pr_fi);
            proto::set_size(b, file_size);
            block = blocks.by_hash(b_hash);
            if (!block) {
                block = model::block_info_t::create(b).value();
                blocks.put(block);
            }
        }

        auto &v = proto::get_version(pr_fi);
        auto modified_device = modified_by == 0 ? device.device_id().get_uint() : modified_by;
        auto modified_version = modified_v == 0 ? 1 : modified_v;
        proto::add_counters(v, proto::Counter(modified_device, modified_version));
        proto::set_modified_s(pr_fi, modified_version);

        auto file = model::file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info.get()).value();
        if (block) {
            file->assign_block(block, 0);
        }
        folder_info->add_strict(file);
        return file;
    };

    auto f_a_my = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY);
    auto f_c_my = add_file("a/b.txt", *my_device, 5, proto::FileInfoType::FILE);

    auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
    CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});

    REQUIRE(folder_entity->get_children().size() == 1);
    auto dir_a = *folder_entity->get_children().begin();
    CHECK(dir_a->get_stats() == entity_stats_t{5, 2});

    auto p_a_my = dir_a->get_presence(&*my_device);
    CHECK(p_a_my->get_stats() == presence_stats_t{5, 2, 2, 1});

    f_a_my->get_augmentation()->on_update();
    CHECK(p_a_my->get_stats() == presence_stats_t{5, 2, 2, 1});
    CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});
}

TEST_CASE("statistics", "[presentation]") {
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    cluster->get_devices().put(peer_2_device);

    auto builder = diff_builder_t(*cluster);
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    auto folder = cluster->get_folders().by_id("1234-5678");
    auto &blocks = cluster->get_blocks();

    auto add_file = [&](std::string_view name, model::device_t &device, std::int32_t file_size = 0,
                        proto::FileInfoType type = proto::FileInfoType::DIRECTORY, std::uint64_t modified_by = 0,
                        std::uint64_t modified_v = 0) {
        auto folder_info = folder->get_folder_infos().by_device(device);

        proto::FileInfo pr_fi;
        proto::set_name(pr_fi, name);
        proto::set_type(pr_fi, type);
        proto::set_sequence(pr_fi, folder_info->get_max_sequence() + 1);

        auto block = model::block_info_ptr_t();
        if (file_size) {
            proto::set_size(pr_fi, file_size);
            proto::set_block_size(pr_fi, file_size);
            assert(type == proto::FileInfoType::FILE);
            auto bytes = utils::bytes_t(file_size);
            auto b_hash = utils::sha256_digest(bytes).value();
            auto &b = proto::add_blocks(pr_fi);
            proto::set_size(b, file_size);
            block = blocks.by_hash(b_hash);
            if (!block) {
                block = model::block_info_t::create(b).value();
                blocks.put(block);
            }
        }

        auto &v = proto::get_version(pr_fi);
        auto modified_device = modified_by == 0 ? device.device_id().get_uint() : modified_by;
        auto modified_version = modified_v == 0 ? 1 : modified_v;
        proto::add_counters(v, proto::Counter(modified_device, modified_version));
        proto::set_modified_s(pr_fi, modified_version);

        auto file = model::file_info_t::create(sequencer->next_uuid(), pr_fi, folder_info.get()).value();
        if (block) {
            file->assign_block(block, 0);
        }
        folder_info->add_strict(file);
        return file;
    };

    SECTION("emtpy folder") {
        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        CHECK(folder_entity->get_stats() == entity_stats_t{});
    }
    SECTION("shared with nobody folder") {
        SECTION("single file") {
            add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE);
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});

            auto folder_my = folder_entity->get_presence(&*my_device);
            CHECK(folder_my->get_stats() == presence_stats_t{5, 1, 1, 0});
        }
        SECTION("two files") {
            SECTION("flat hierarchy") {
                add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE);
                add_file("b.txt", *my_device, 4, proto::FileInfoType::FILE);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{9, 2});
            }
            SECTION("some hierarchy") {
                add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY);
                add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY);
                add_file("a/b/c.txt", *my_device, 5, proto::FileInfoType::FILE);
                add_file("a/d.txt", *my_device, 4, proto::FileInfoType::FILE);

                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{9, 4});

                auto folder_my = folder_entity->get_presence(&*my_device);
                CHECK(folder_my->get_stats() == presence_stats_t{9, 4, 4, 2});

                auto dir_a = *folder_entity->get_children().begin();
                auto p_a = dir_a->get_presence(&*my_device);
                auto it_a = dir_a->get_children().begin();
                auto dir_b = *it_a++;
                auto f_d = *it_a++;
                auto p_b = dir_b->get_presence(&*my_device);
                auto f_c = *dir_b->get_children().begin();
                auto p_c = f_c->get_presence(&*my_device);
                auto p_d = f_d->get_presence(&*my_device);

                CHECK(dir_a->get_stats() == entity_stats_t{9, 4});
                CHECK(p_a->get_stats() == presence_stats_t{9, 4, 4, 2});
                CHECK(dir_b->get_stats() == entity_stats_t{5, 2});
                CHECK(p_b->get_stats() == presence_stats_t{5, 2, 2, 1});
                CHECK(f_c->get_stats() == entity_stats_t{5, 1});
                CHECK(p_c->get_stats() == presence_stats_t{5, 1, 1, 0});
                CHECK(f_d->get_stats() == entity_stats_t{4, 1});
                CHECK(p_d->get_stats() == presence_stats_t{4, 1, 1, 0});
            }
        }
    }

    SECTION("shared with a peer") {
        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
        auto fi_peer = folder->get_folder_infos().by_device(*peer_device).get();
        SECTION("single file") {
            SECTION("same file") {
                add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                add_file("a.txt", *peer_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});
            }
            SECTION("peer has newer") {
                add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE, peer_device_id, 1);
                add_file("a.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 2);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{6, 1});

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});
            }
            SECTION("me has newer") {
                add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 2);
                add_file("a.txt", *peer_device, 6, proto::FileInfoType::FILE, my_device_id, 1);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});
            }
            SECTION("coflicted file (peer has newer)") {
                add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                add_file("a.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 2);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});
            }
            SECTION("coflicted file (me has newer)") {
                add_file("a.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 2);
                add_file("a.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 1);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});
            }
        }
        SECTION("two files do not intersect") {
            SECTION("simple hierarchy") {
                add_file("dir", *my_device);
                add_file("dir/a.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                add_file("dir", *peer_device);
                add_file("dir/b.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 1);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{11, 3});

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});
            }
            SECTION("complex hierarchy") {
                add_file("dir", *my_device);
                add_file("dir/a.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                add_file("dir", *peer_device);
                add_file("dir/b", *peer_device);
                add_file("dir/b/c", *peer_device);
                add_file("dir/b/c/d", *peer_device);
                add_file("dir/b/c/d/x.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 1);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{11, 6});

                REQUIRE(builder.unshare_folder(*fi_peer).apply());
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});
            }
        }
    }

    SECTION("dynamically add files") {
        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        SECTION("for local device") {
            SECTION("no orphans") {
                auto f_a = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_a);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 1});

                REQUIRE(folder_entity->get_children().size() == 1);
                auto dir_a = *folder_entity->get_children().begin();
                CHECK(dir_a->get_stats() == entity_stats_t{0, 1});
                auto p_a = dir_a->get_presence(&*my_device);
                CHECK(p_a->get_stats() == presence_stats_t{0, 1, 1, 1});

                auto f_b = add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_b);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 2});

                auto dir_b = *dir_a->get_children().begin();
                CHECK(dir_a->get_stats() == entity_stats_t{0, 2});
                CHECK(p_a->get_stats() == presence_stats_t{0, 2, 2, 2});
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 2});

                auto p_b = dir_b->get_presence(&*my_device);
                CHECK(p_b->get_stats() == presence_stats_t{0, 1, 1, 1});
                CHECK(dir_b->get_stats() == entity_stats_t{0, 1});

                auto f_c = add_file("a/b/c.txt", *my_device, 5, proto::FileInfoType::FILE);
                folder_entity->on_insert(*f_c);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});
                CHECK(dir_a->get_stats() == entity_stats_t{5, 3});
                CHECK(p_a->get_stats() == presence_stats_t{5, 3, 3, 2});
                CHECK(dir_b->get_stats() == entity_stats_t{5, 2});
                CHECK(p_b->get_stats() == presence_stats_t{5, 2, 2, 1});

                auto file_c = *dir_b->get_children().begin();
                CHECK(file_c->get_stats() == entity_stats_t{5, 1});
                auto p_c = file_c->get_presence(&*my_device);
                CHECK(p_c->get_stats() == presence_stats_t{5, 1, 1, 0});
            }

            SECTION("with orphans") {
                auto f_c = add_file("a/b/c.txt", *my_device, 5, proto::FileInfoType::FILE);
                folder_entity->on_insert(*f_c);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 0});

                auto f_b = add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_b);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 0});

                auto f_a = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_a);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});
            }
        }
        SECTION("for local & peer device") {
            REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
            SECTION("no orphans") {
                auto f_a_my = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_a_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 1});

                auto f_a_peer = add_file("a", *peer_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_a_peer);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 1});

                auto f_b_peer = add_file("a/b", *peer_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_b_peer);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 2});

                auto f_b_my = add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY);
                folder_entity->on_insert(*f_b_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 2});

                auto f_c = add_file("a/b/c.txt", *peer_device, 5, proto::FileInfoType::FILE);
                folder_entity->on_insert(*f_c);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});
            }
            SECTION("simple hierarchy") {
                auto f_a_my = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 2);
                folder_entity->on_insert(*f_a_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 1});

                auto dir_a = *folder_entity->get_children().begin();
                CHECK(dir_a->get_stats() == entity_stats_t{0, 1});

                auto p_a_my = dir_a->get_presence(&*my_device);
                CHECK(p_a_my->get_stats() == presence_stats_t{0, 1, 1, 1});

                auto p_a_peer = dir_a->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == presence_stats_t{0, 0, 0, 0});

                auto f_x_my = add_file("a/x.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 2);
                folder_entity->on_insert(*f_x_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});
                CHECK(dir_a->get_stats() == entity_stats_t{5, 2});
                CHECK(p_a_my->get_stats() == presence_stats_t{5, 2, 2, 1});
                CHECK(p_a_peer->get_stats() == presence_stats_t{0, 0});

                auto e_x = *dir_a->get_children().begin();
                CHECK(e_x->get_stats() == entity_stats_t{5, 1});
                auto p_x_my = e_x->get_presence(&*my_device);
                CHECK(p_x_my->get_stats() == presence_stats_t{5, 1, 1});
                CHECK(p_x_my->get_parent() == p_a_my);
                CHECK(p_a_my->get_children().size() == 1);
                CHECK(p_a_my->get_children()[0] == p_x_my);

                CHECK(p_a_peer->get_children().size() == 1);
                CHECK(p_a_peer->get_children()[0]->get_features() & F::missing);

                auto f_a_peer = add_file("a", *peer_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
                folder_entity->on_insert(*f_a_peer);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});
                CHECK(dir_a->get_stats() == entity_stats_t{5, 2});
                CHECK(p_a_my->get_stats() == presence_stats_t{5, 2, 2, 1});
                CHECK(p_x_my->get_stats() == presence_stats_t{5, 1, 1, 0});

                p_a_peer = dir_a->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == entity_stats_t{0, 1});

                auto p_x_peer = e_x->get_presence(&*peer_device);
                CHECK(p_x_peer->get_features() & (F::missing | F::file));
                CHECK(p_x_peer->get_stats() == presence_stats_t{0, 0});

                auto pr_x = f_x_my->as_proto(true);
                auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
                auto file_x_peer = model::file_info_t::create(sequencer->next_uuid(), pr_x, fi_peer.get()).value();
                auto block = *blocks.begin();
                file_x_peer->assign_block(block.item, 0);
                fi_peer->add_strict(file_x_peer);

                CHECK(p_a_peer->get_children().size() == 1);
                CHECK(p_a_peer->get_children()[0]->get_features() & F::missing);
                folder_entity->on_insert(*file_x_peer);

                CHECK(folder_entity->get_stats() == entity_stats_t{5, 2});
                CHECK(dir_a->get_stats() == entity_stats_t{5, 2});
                CHECK(p_a_my->get_stats() == presence_stats_t{5, 2, 2, 1});
                CHECK(p_x_my->get_stats() == presence_stats_t{5, 1, 1, 0});
                CHECK(p_a_peer->get_stats() == presence_stats_t{5, 2, 1, 0});

                p_x_peer = e_x->get_presence(&*peer_device);
                CHECK(p_x_peer->get_stats() == presence_stats_t{5, 1, 1, 0});
                CHECK(p_a_peer->get_children().size() == 1);
                CHECK(p_a_peer->get_children()[0] == p_x_peer);
            }
            SECTION("with orphans") {
                auto f_c_peer = add_file("a/b/c.txt", *peer_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                folder_entity->on_insert(*f_c_peer);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 0});

                auto f_c_my = add_file("a/b/c.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);
                folder_entity->on_insert(*f_c_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 0});

                auto f_b_peer = add_file("a/b", *peer_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
                folder_entity->on_insert(*f_b_peer);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 0});

                auto f_b_my = add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
                folder_entity->on_insert(*f_b_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{0, 0});

                auto f_a_peer = add_file("a", *peer_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
                folder_entity->on_insert(*f_a_peer);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});

                auto f_a_my = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
                folder_entity->on_insert(*f_a_my);
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});

                auto dir_a = *folder_entity->get_children().begin();
                auto dir_b = *dir_a->get_children().begin();
                auto file_c = *dir_b->get_children().begin();

                auto p_a_my = dir_a->get_presence(&*my_device);
                CHECK(p_a_my->get_stats() == presence_stats_t{5, 3, 3, 2});

                auto p_a_peer = dir_a->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == presence_stats_t{5, 3, 3, 0});

                auto p_b_my = dir_b->get_presence(&*my_device);
                CHECK(p_b_my->get_stats() == presence_stats_t{5, 2, 2, 1});

                auto p_b_peer = dir_b->get_presence(&*peer_device);
                CHECK(p_b_peer->get_stats() == presence_stats_t{5, 2, 2, 0});

                auto p_c_my = file_c->get_presence(&*my_device);
                CHECK(p_c_my->get_stats() == presence_stats_t{5, 1, 1, 0});

                auto p_c_peer = file_c->get_presence(&*peer_device);
                CHECK(p_c_peer->get_stats() == presence_stats_t{5, 1, 1, 0});
            }
        }
    }

    SECTION("file updates") {
        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());

        auto f_a_my = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        auto f_a_peer = add_file("a", *peer_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        auto f_b_peer = add_file("a/b", *peer_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        auto f_b_my = add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        auto f_c_my = add_file("a/b/c.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);

        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});

        REQUIRE(folder_entity->get_children().size() == 1);
        auto dir_a = *folder_entity->get_children().begin();
        CHECK(dir_a->get_stats() == entity_stats_t{5, 3});

        auto p_a_my = dir_a->get_presence(&*my_device);
        CHECK(p_a_my->get_stats() == presence_stats_t{5, 3, 3, 2});

        auto p_a_peer = dir_a->get_presence(&*peer_device);
        CHECK(p_a_peer->get_stats() == presence_stats_t{0, 2, 2, 0});

        auto p_b_my = dir_a->get_presence(&*my_device)->get_children().front();
        CHECK(p_b_my->get_stats() == presence_stats_t{5, 2, 2, 1});

        auto p_c_my = p_b_my->get_entity()->get_presence(&*my_device)->get_children().front();
        CHECK(p_c_my->get_stats() == presence_stats_t{5, 1, 1, 0});

        auto pr_fi = [&]() {
            auto pr_fi = f_c_my->as_proto(false);
            auto block = model::block_info_ptr_t();
            proto::set_size(pr_fi, 10);
            proto::set_block_size(pr_fi, 10);
            auto bytes = utils::bytes_t(10);
            std::fill_n(bytes.data(), bytes.size(), '1');
            auto b_hash = utils::sha256_digest(bytes).value();
            auto &b = proto::add_blocks(pr_fi);
            proto::set_size(b, 10);
            proto::set_hash(b, b_hash);
            block = blocks.by_hash(b_hash);
            block = model::block_info_t::create(b).value();
            blocks.put(block);
            return pr_fi;
        }();
        REQUIRE(builder.local_update("1234-5678", pr_fi).apply());
        CHECK(folder_entity->get_stats() == entity_stats_t{10, 3});
        CHECK(dir_a->get_stats() == entity_stats_t{10, 3});
        CHECK(p_a_my->get_stats() == presence_stats_t{10, 3, 3, 3});
        CHECK(p_c_my->get_stats() == presence_stats_t{10, 1, 1, 1});
        CHECK(p_a_peer->get_stats() == presence_stats_t{0, 2, 2, 0});

        // peer copies
        auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
        f_c_my = f_c_my->get_folder_info()->get_file_infos().by_name(f_c_my->get_name());
        proto::get_version(pr_fi) = f_c_my->get_version()->as_proto();
        auto file_c_peer = model::file_info_t::create(sequencer->next_uuid(), pr_fi, fi_peer.get()).value();
        {
            auto block = *blocks.begin();
            file_c_peer->assign_block(block.item, 0);
            fi_peer->add_strict(file_c_peer);
            folder_entity->on_insert(*file_c_peer);
            CHECK(folder_entity->get_stats() == entity_stats_t{10, 3});
            CHECK(dir_a->get_stats() == entity_stats_t{10, 3});
            CHECK(p_a_my->get_stats() == presence_stats_t{10, 3, 3, 3});
            CHECK(p_a_peer->get_stats() == presence_stats_t{10, 3, 3, 0});
            CHECK(p_c_my->get_stats() == presence_stats_t{10, 1, 1, 1});
        }

        // peer updates
        {
            auto pr_fi = [&]() {
                auto pr_fi = f_c_my->as_proto(false);
                auto &v = proto::get_version(pr_fi);
                auto value = proto::get_value(f_c_my->get_version()->get_best());
                proto::add_counters(v, proto::Counter(peer_device_id, value + 1));
                auto block = model::block_info_ptr_t();
                proto::set_size(pr_fi, 15);
                proto::set_block_size(pr_fi, 15);
                auto bytes = utils::bytes_t(15);
                std::fill_n(bytes.data(), bytes.size(), '2');
                auto b_hash = utils::sha256_digest(bytes).value();
                auto &b = proto::add_blocks(pr_fi);
                proto::set_size(b, 15);
                proto::set_hash(b, b_hash);
                block = blocks.by_hash(b_hash);
                block = model::block_info_t::create(b).value();
                blocks.put(block);
                return pr_fi;
            }();
            auto fi_peer_2 = folder->get_folder_infos().by_device(*peer_device);
            bu::uuid file_uuid;
            assign(file_uuid, file_c_peer->get_uuid());
            auto file_c_peer_2 = model::file_info_t::create(file_uuid, pr_fi, fi_peer.get()).value();
            auto block = *blocks.begin();
            file_c_peer_2->assign_block(block.item, 0);
            fi_peer_2->add_strict(file_c_peer);

            file_c_peer->update(*file_c_peer_2);
            file_c_peer->notify_update();

            CHECK(folder_entity->get_stats() == entity_stats_t{15, 3});
            CHECK(dir_a->get_stats() == entity_stats_t{15, 3});
            CHECK(p_a_my->get_stats() == presence_stats_t{10, 3, 2, 3});
            CHECK(p_a_peer->get_stats() == presence_stats_t{15, 3, 3, 0});
        }

        // local copy
        {
            REQUIRE(builder.remote_copy(*file_c_peer).apply());
            CHECK(folder_entity->get_stats() == entity_stats_t{15, 3});
            CHECK(dir_a->get_stats() == entity_stats_t{15, 3});
            CHECK(p_a_my->get_stats() == presence_stats_t{15, 3, 3, 3});
            CHECK(p_a_peer->get_stats() == presence_stats_t{15, 3, 3, 0});
        }

        // local update
        {
            auto pr_fi = [&]() {
                auto pr_fi = file_c_peer->as_proto(false);
                proto::clear_blocks(pr_fi);
                proto::set_deleted(pr_fi, true);
                proto::set_size(pr_fi, 0);
                auto &v = proto::get_version(pr_fi);
                proto::add_counters(v, proto::Counter(my_device_id, 3));
                return pr_fi;
            }();
            REQUIRE(builder.local_update("1234-5678", pr_fi).apply());
            CHECK(folder_entity->get_stats() == entity_stats_t{0, 3});
            CHECK(dir_a->get_stats() == entity_stats_t{0, 3});
            CHECK(p_a_my->get_stats() == presence_stats_t{0, 3, 3, 3});
            CHECK(p_a_peer->get_stats() == presence_stats_t{15, 3, 2, 0});
        }
    }

    SECTION("file deletion & insertion") {
        auto f_a = add_file("a", *my_device, 5, proto::FileInfoType::FILE);

        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});

        REQUIRE(folder_entity->get_children().size() == 1);
        auto e_a = *folder_entity->get_children().begin();
        CHECK(e_a->get_stats() == entity_stats_t{5, 1});

        auto p_a_my = e_a->get_presence(&*my_device);
        CHECK(p_a_my->get_stats() == presence_stats_t{5, 1, 1, 0});
        CHECK(!(p_a_my->get_features() & F::deleted));

        auto pr_fi = [&]() {
            auto pr_fi = f_a->as_proto(false);
            auto block = model::block_info_ptr_t();
            proto::set_size(pr_fi, 0);
            proto::set_block_size(pr_fi, 0);
            proto::set_deleted(pr_fi, true);
            return pr_fi;
        }();
        REQUIRE(builder.local_update("1234-5678", pr_fi).apply());
        CHECK(p_a_my->get_stats() == presence_stats_t{0, 1, 1, 1});
        CHECK(p_a_my->get_features() & F::deleted);
    }

    SECTION("folder updates & cluster entities") {
        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
        auto f_a_peer = add_file("a", *peer_device, 0, proto::FileInfoType::DIRECTORY);
        REQUIRE(builder.remote_copy(*f_a_peer).apply());

        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        CHECK(folder_entity->get_stats() == entity_stats_t{0, 1});

        REQUIRE(folder_entity->get_children().size() == 1);
        auto dir_a = *folder_entity->get_children().begin();
        CHECK(dir_a->get_stats() == entity_stats_t{0, 1});

        auto p_a_my = dir_a->get_presence(&*my_device);
        CHECK(p_a_my->get_stats() == presence_stats_t{0, 1, 1, 1});

        auto p_a_peer = dir_a->get_presence(&*peer_device);
        CHECK(p_a_peer->get_stats() == presence_stats_t{0, 1, 1, 0});

        auto pr_fi = [&]() {
            auto pr_fi = f_a_peer->as_proto(false);
            proto::set_deleted(pr_fi, true);
            return pr_fi;
        }();
        REQUIRE(builder.local_update("1234-5678", pr_fi).apply());
        CHECK(p_a_my->get_stats() == presence_stats_t{0, 1, 1, 1});
        CHECK(dir_a->get_stats() == entity_stats_t{0, 1});
        CHECK(p_a_peer->get_stats() == presence_stats_t{0, 1, 0, 0});
    }

    SECTION("yet not resolved conficts") {
        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
        SECTION("static") {
            SECTION("local win") {
                auto f_a_my = add_file("content.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 10);
                auto f_a_peer = add_file("content.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 9);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{5, 1});
                REQUIRE(folder_entity->get_children().size() == 1);

                auto file_entity = *folder_entity->get_children().begin();
                CHECK(file_entity->get_stats() == entity_stats_t{5, 1});

                auto p_my = file_entity->get_presence(&*my_device);
                CHECK(p_my->get_stats() == presence_stats_t{5, 1, 1, 0});

                auto p_a_peer = file_entity->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == presence_stats_t{6, 1, 0, 0});
            }
            SECTION("remote win") {
                auto f_a_my = add_file("content.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 9);
                auto f_a_peer = add_file("content.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 10);
                auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
                CHECK(folder_entity->get_stats() == entity_stats_t{6, 1});
                REQUIRE(folder_entity->get_children().size() == 1);

                auto file_entity = *folder_entity->get_children().begin();
                CHECK(file_entity->get_stats() == entity_stats_t{6, 1});

                auto p_my = file_entity->get_presence(&*my_device);
                CHECK(p_my->get_stats() == presence_stats_t{5, 1, 0, 0});

                auto p_a_peer = file_entity->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == presence_stats_t{6, 1, 1, 0});
            }
        }
        SECTION("dynamic") {
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            SECTION("local win") {
                auto f_a_my = add_file("content-2.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 10);
                auto f_a_peer =
                    add_file("content-2.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 9);

                folder_entity->on_insert(*f_a_my);
                folder_entity->on_insert(*f_a_peer);

                REQUIRE(folder_entity->get_children().size() == 1);
                auto file_entity = *folder_entity->get_children().begin();
                CHECK(file_entity->get_stats() == entity_stats_t{5, 1});

                auto p_my = file_entity->get_presence(&*my_device);
                CHECK(p_my->get_stats() == presence_stats_t{5, 1, 1, 0});

                auto p_a_peer = file_entity->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == presence_stats_t{6, 1, 0, 0});
            }
            SECTION("remote win") {
                auto f_a_my = add_file("content-3.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 9);
                auto f_a_peer =
                    add_file("content-3.txt", *peer_device, 6, proto::FileInfoType::FILE, peer_device_id, 10);

                folder_entity->on_insert(*f_a_my);
                folder_entity->on_insert(*f_a_peer);

                auto p_folder_my = folder_entity->get_presence(&*my_device);
                CHECK(p_folder_my->get_stats() == presence_stats_t{5, 1, 0, 0});
                auto p_folder_peer = folder_entity->get_presence(&*peer_device);
                CHECK(p_folder_peer->get_stats() == presence_stats_t{6, 1, 1, 0});

                REQUIRE(folder_entity->get_children().size() == 1);
                auto file_entity = *folder_entity->get_children().begin();
                CHECK(file_entity->get_stats() == entity_stats_t{6, 1});

                auto p_my = file_entity->get_presence(&*my_device);
                CHECK(p_my->get_stats() == presence_stats_t{5, 1, 0, 0});

                auto p_a_peer = file_entity->get_presence(&*peer_device);
                CHECK(p_a_peer->get_stats() == presence_stats_t{6, 1, 1, 0});
            }
        }
    }
    SECTION("updates propagation") {
        using sorted_entities_t = std::pmr::set<const entity_t *>;
        using unsorted_entities_t = std::pmr::set<entity_ptr_t, std::less<entity_ptr_t>>;
        struct monitor_t final : entities_monitor_t {
            monitor_t(unsorted_entities_t &deleted_, sorted_entities_t &updated_)
                : deleted{deleted_}, updated{updated_} {}
            void on_delete(entity_t &entity) noexcept override { deleted.emplace(entity_ptr_t{&entity}); }
            void on_update(const entity_t &entity) noexcept override { updated.insert(&entity); }

            unsorted_entities_t &deleted;
            sorted_entities_t &updated;
        };

        auto f_a_my = add_file("a", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        auto f_b_my = add_file("a/b", *my_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        auto f_c_my = add_file("a/b/c.txt", *my_device, 5, proto::FileInfoType::FILE, my_device_id, 1);

        auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
        CHECK(folder_entity->get_stats() == entity_stats_t{5, 3});

        REQUIRE(folder_entity->get_children().size() == 1);
        auto dir_a = *folder_entity->get_children().begin();
        auto dir_b = *dir_a->get_children().begin();
        auto file_c = *dir_b->get_children().begin();

        auto buffer = std::array<std::byte, 1024>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<std::string>(&pool);

        auto updated_entities = sorted_entities_t(allocator);
        auto deleted_entities = unsorted_entities_t(allocator);
        auto monitor = monitor_t(deleted_entities, updated_entities);
        auto monitor_guard = folder_entity->monitor(&monitor);

        SECTION("file update") {
            auto pr_fi = [&]() {
                auto pr_fi = f_c_my->as_proto(false);
                auto block = model::block_info_ptr_t();
                proto::set_size(pr_fi, 6);
                proto::set_block_size(pr_fi, 6);
                auto bytes = utils::bytes_t(6);
                std::fill_n(bytes.data(), bytes.size(), '1');
                auto b_hash = utils::sha256_digest(bytes).value();
                auto &b = proto::add_blocks(pr_fi);
                proto::set_size(b, 6);
                proto::set_hash(b, b_hash);
                block = blocks.by_hash(b_hash);
                block = model::block_info_t::create(b).value();
                blocks.put(block);
                return pr_fi;
            }();
            REQUIRE(builder.local_update("1234-5678", pr_fi).apply());
            REQUIRE(deleted_entities.size() == 0);
            REQUIRE(updated_entities.size() == 1);
            auto it = updated_entities.begin();
            REQUIRE(*it++ == file_c);
            updated_entities.clear();
        }

        auto f_d_my = add_file("a/b/d.txt", *my_device, 7, proto::FileInfoType::FILE, my_device_id, 1);
        folder_entity->on_insert(*f_d_my);

        auto file_d = *(++dir_b->get_children().begin());
        REQUIRE(deleted_entities.size() == 0);
        REQUIRE(updated_entities.size() == 1);
        REQUIRE(*updated_entities.begin() == file_d);
        updated_entities.clear();

        REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
        auto fi_peer = folder->get_folder_infos().by_device(*peer_device);
        folder_entity->on_insert(*fi_peer);
        REQUIRE(deleted_entities.size() == 0);
        REQUIRE(updated_entities.size() == 1);
        REQUIRE(*updated_entities.begin() == folder_entity);
        updated_entities.clear();

        auto f_a_peer = add_file("a", *peer_device, 0, proto::FileInfoType::DIRECTORY, my_device_id, 1);
        folder_entity->on_insert(*f_a_peer);
        REQUIRE(deleted_entities.size() == 0);
        REQUIRE(updated_entities.size() == 1);
        REQUIRE(*updated_entities.begin() == dir_a);
        updated_entities.clear();

        auto f_x_peer = add_file("x", *peer_device, 0, proto::FileInfoType::DIRECTORY, peer_device_id, 1);
        auto f_y_peer = add_file("x/y", *peer_device, 0, proto::FileInfoType::DIRECTORY, peer_device_id, 1);
        folder_entity->on_insert(*f_x_peer);
        folder_entity->on_insert(*f_y_peer);
        REQUIRE(deleted_entities.size() == 0);
        REQUIRE(updated_entities.size() == 2);
        updated_entities.clear();

        f_x_peer.reset();
        f_y_peer.reset();
        REQUIRE(builder.unshare_folder(*fi_peer).apply());
        REQUIRE(deleted_entities.size() == 2);
    }
}

static bool _init = []() -> bool {
    test::init_logging();
    return true;
}();
