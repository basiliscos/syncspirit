// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/file_iterator.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

struct dummy_sink_t final : model::diff_sink_t {
    dummy_sink_t(diff_builder_t &builder_) noexcept : builder{builder_} {}

    void push(diff::cluster_diff_ptr_t d) noexcept override { builder.assign(d.get()); }

    diff_builder_t &builder;
};

TEST_CASE("file iterator", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);
    auto sink = dummy_sink_t(builder);

    auto &blocks_map = cluster->get_blocks();
    auto &folders = cluster->get_folders();
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = cluster->get_folders().by_id(folder->get_id())->get_folder_infos();
    REQUIRE(folder_infos.size() == 2u);

    auto peer_folder = folder_infos.by_device(*peer_device);
    auto &peer_files = peer_folder->get_file_infos();

    auto my_folder = folder_infos.by_device(*my_device);
    auto &my_files = my_folder->get_file_infos();

    auto file_iterator = peer_device->create_iterator(*cluster);
    file_iterator->activate(sink);
    CHECK(!file_iterator->next_need_cloning());

    REQUIRE(builder.configure_cluster(peer_id.get_sha256())
                .add(peer_id.get_sha256(), folder->get_id(), 123, 10u)
                .finish()
                .apply());

    CHECK(!file_iterator->next_need_cloning());

    SECTION("empty file cases") {
        SECTION("1 emtpy file") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

            auto f = file_iterator->next_need_cloning();
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            CHECK(!f->is_locked());

            REQUIRE(builder.apply());
            CHECK(folder->is_synchronizing());
            REQUIRE(!file_iterator->next_need_cloning());
            REQUIRE(!file_iterator->next_need_sync());

            REQUIRE(builder.clone_file(*f).apply());
            REQUIRE(!file_iterator->next_need_cloning());
            REQUIRE(!file_iterator->next_need_sync());
            CHECK(!folder->is_synchronizing());
            CHECK(!f->is_locked());
        }

        SECTION("2 emtpy file") {
            auto files = model::file_infos_map_t();
            auto file_1 = proto::FileInfo();
            file_1.set_name("a.txt");
            file_1.set_sequence(10ul);
            auto file_2 = proto::FileInfo();
            file_2.set_name("b.txt");
            file_2.set_sequence(10ul);
            builder.make_index(peer_id.get_sha256(), folder->get_id())
                .add(file_1, peer_device)
                .add(file_2, peer_device)
                .finish();
            REQUIRE(builder.apply());

            SECTION("both files are missing on my side") {
                auto f = file_iterator->next_need_cloning();
                REQUIRE(f);
                CHECK((f->get_name() == "a.txt" || f->get_name() == "b.txt"));
                CHECK(!f->is_locked());
                files.put(f);
                REQUIRE(builder.apply());
                CHECK(folder->is_synchronizing());

                REQUIRE(builder.clone_file(*f).apply());

                f = file_iterator->next_need_cloning();
                REQUIRE(f);
                CHECK((f->get_name() == "a.txt" || f->get_name() == "b.txt"));
                CHECK(!f->is_locked());
                files.put(f);
                REQUIRE(builder.apply());
                CHECK(folder->is_synchronizing());
                REQUIRE(builder.clone_file(*f).apply());

                REQUIRE(!file_iterator->next_need_cloning());
                REQUIRE(!file_iterator->next_need_sync());

                CHECK(!folder->is_synchronizing());
                CHECK(files.by_name("a.txt"));
                CHECK(files.by_name("b.txt"));
            }

            SECTION("1 file is missing on my side") {
                auto peer_file = peer_files.by_name("a.txt");
                REQUIRE(peer_file);
                REQUIRE(builder.clone_file(*peer_file).apply());
                auto f = file_iterator->next_need_cloning();
                REQUIRE(f);
                CHECK(f->get_name() == "b.txt");
                CHECK(!f->is_locked());

                REQUIRE(builder.apply());
                CHECK(folder->is_synchronizing());
                REQUIRE(!file_iterator->next_need_cloning());

                REQUIRE(builder.clone_file(*f).apply());
                REQUIRE(!file_iterator->next_need_cloning());
                REQUIRE(!file_iterator->next_need_sync());
                CHECK(!folder->is_synchronizing());
                CHECK(!f->is_locked());
            }

            SECTION("0 files are missing on my side") {
                auto peer_file_1 = peer_files.by_name("a.txt");
                auto peer_file_2 = peer_files.by_name("b.txt");
                REQUIRE(builder.clone_file(*peer_file_1).clone_file(*peer_file_2).apply());
                REQUIRE(!file_iterator->next_need_cloning());
            }
        }

        SECTION("invalid file is ignored") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_invalid(true);
            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

            REQUIRE(!file_iterator->next_need_cloning());
            REQUIRE(!file_iterator->next_need_cloning());
            CHECK(!folder->is_synchronizing());
        }
    }

    SECTION("non-empty single file case") {
        auto b = proto::BlockInfo();
        b.set_hash(utils::sha256_digest("12345").value());
        b.set_weak_hash(555);
        b.set_size(5ul);
        auto bi = block_info_t::create(b).value();
        auto &blocks_map = cluster->get_blocks();
        blocks_map.put(bi);

        SECTION("missing locally") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b.size());
            file.set_block_size(b.size());
            *file.add_blocks() = b;

            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

            auto f = file_iterator->next_need_cloning();
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            CHECK(!f->is_locked());

            REQUIRE(builder.apply());
            CHECK(folder->is_synchronizing());
            CHECK(f->is_locked());

            REQUIRE(!file_iterator->next_need_cloning());
            REQUIRE(!file_iterator->next_need_sync());

            REQUIRE(builder.clone_file(*f).apply());
            CHECK(folder->is_synchronizing());
            CHECK(f->is_locked());

            REQUIRE(!file_iterator->next_need_cloning());
            f = file_iterator->next_need_sync();
            CHECK(folder->is_synchronizing());
            REQUIRE(f);
            REQUIRE(file_iterator->next_need_sync() == f);
            file_iterator->commit(f);

            CHECK(f->get_name() == "a.txt");
            CHECK(f->is_locked());
            CHECK(!file_iterator->next_need_sync());

            REQUIRE(builder.apply());

            auto lf = my_files.by_name(f->get_name());
            f->mark_local_available(0);

            REQUIRE(builder.finish_file_ack(*lf).apply());
            REQUIRE(!file_iterator->next_need_cloning());
            REQUIRE(!file_iterator->next_need_sync());
            CHECK(!folder->is_synchronizing());
            CHECK(!f->is_locked());
        }

        SECTION("have local, but outdated") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b.size());
            file.set_block_size(b.size());
            *file.add_blocks() = b;

            auto c_1 = file.mutable_version()->add_counters();
            c_1->set_id(1);
            c_1->set_value(1);

            auto my_file = file_info_t::create(sequencer->next_uuid(), file, my_folder).value();
            my_files.put(my_file);

            auto c_2 = file.mutable_version()->add_counters();
            c_2->set_id(2);
            c_2->set_value(2);
            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

            SECTION("has been scanned") {
                my_file->mark_local();

                CHECK(!file_iterator->next_need_cloning());
                auto f = file_iterator->next_need_sync();
                REQUIRE(f);
                CHECK(f->get_name() == "a.txt");
                CHECK(!f->is_locked());
                REQUIRE(file_iterator->next_need_sync() == f);
                REQUIRE(builder.apply());
                CHECK(folder->is_synchronizing());
                CHECK(f->is_locked());

                file_iterator->commit(f);
                REQUIRE(builder.apply());
                CHECK(!folder->is_synchronizing());
                CHECK(!f->is_locked());
            }

            SECTION("has not bee scanned") {
                CHECK(!file_iterator->next_need_cloning());
                CHECK(!file_iterator->next_need_sync());
            }
        }

        SECTION("have local, local is newer") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b.size());
            file.set_block_size(b.size());
            *file.add_blocks() = b;

            auto c_1 = file.mutable_version()->add_counters();
            c_1->set_id(1);
            c_1->set_value(1);
            builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish();

            auto c_2 = file.mutable_version()->add_counters();
            c_2->set_id(2);
            c_2->set_value(2);
            auto my_file = file_info_t::create(sequencer->next_uuid(), file, my_folder).value();
            my_files.put(my_file);

            REQUIRE(builder.apply());

            CHECK(!file_iterator->next_need_cloning());
            CHECK(!file_iterator->next_need_sync());
        }

        SECTION("peer file is unreacheable") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b.size());
            file.set_block_size(b.size());
            *file.add_blocks() = b;

            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());
            auto f = file_iterator->next_need_cloning();
            REQUIRE(f);

            REQUIRE(builder.clone_file(*f).mark_reacheable(f, false).apply());
            file_iterator->commit(f);
            REQUIRE(builder.apply());

            CHECK(!folder->is_synchronizing());
            CHECK(!f->is_locked());
            CHECK(!file_iterator->next_need_cloning());
            CHECK(!file_iterator->next_need_sync());
        }
    }

    file_iterator->deactivate();
}
#if 0
TEST_CASE("file iterator for 2 folders", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto file_iterator = file_iterator_ptr_t();
    auto next = [&](bool reset = false) -> file_info_ptr_t {
        if (reset) {
            file_iterator = new file_iterator_t(*cluster, peer_device);
        }
        auto f = file_iterator->next();
        if (f) {
            file_iterator->done();
        }
        return f;
    };

    auto builder = diff_builder_t(*cluster);
    auto &folders = cluster->get_folders();
    auto sha256 = peer_id.get_sha256();

    REQUIRE(builder.upsert_folder("1234", "/", "my-label-1").upsert_folder("5678", "/", "my-label-2").apply());
    REQUIRE(builder.share_folder(sha256, "1234").share_folder(sha256, "5678").apply());
    auto folder1 = folders.by_id("1234");
    auto folder2 = folders.by_id("5678");

    REQUIRE(builder.configure_cluster(sha256)
                .add(sha256, "1234", 123, 10u)
                .add(sha256, "5678", 1234u, 11)
                .finish()
                .apply());

    auto file1 = proto::FileInfo();
    file1.set_name("a.txt");
    file1.set_sequence(10ul);

    auto file2 = proto::FileInfo();
    file2.set_name("b.txt");
    file2.set_sequence(11ul);

    REQUIRE(builder.make_index(sha256, "1234").add(file1, peer_device).finish().apply());
    REQUIRE(builder.make_index(sha256, "5678").add(file2, peer_device).finish().apply());

    auto files = std::unordered_set<std::string>{};
    auto f = next(true);
    REQUIRE(f);
    files.emplace(f->get_full_name());

    f = next();
    REQUIRE(f);
    files.emplace(f->get_full_name());

    CHECK(files.size() == 2);
    CHECK(files.count("my-label-1/a.txt"));
    CHECK(files.count("my-label-2/b.txt"));
}
#endif
int _init() {
    utils::set_default("trace");
    return 1;
}

static int v = _init();
