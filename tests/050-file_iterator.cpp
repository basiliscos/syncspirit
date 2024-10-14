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

    SECTION("non-empty file cases") {
        auto b = proto::BlockInfo();
        b.set_hash(utils::sha256_digest("12345").value());
        b.set_weak_hash(555);
        b.set_size(5ul);
        auto bi = block_info_t::create(b).value();
        auto &blocks_map = cluster->get_blocks();
        blocks_map.put(bi);

        SECTION("1 file, missing locally") {
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

        SECTION("1 file, have local, but outdated") {
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

        SECTION("1 file, have local, local is newer") {
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

        SECTION("1 file, peer file is unreacheable") {
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

#if 0
    auto b = proto::BlockInfo();
    b.set_hash(utils::sha256_digest("12345").value());
    b.set_weak_hash(555);
    b.set_size(5ul);
    auto bi = block_info_t::create(b).value();
    auto &blocks_map = cluster->get_blocks();
    blocks_map.put(bi);

    SECTION("file locking && marking unreacheable") {
        auto file = proto::FileInfo();
        file.set_name("a.txt");
        file.set_sequence(10ul);
        REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

        auto peer_folder = folder_infos.by_device(*peer_device);
        auto peer_file = peer_folder->get_file_infos().by_name("a.txt");

        SECTION("locking") {
            peer_file->lock();
            auto f = next(true);
            REQUIRE(!f);

            peer_file->unlock();
            f = next(true);
            REQUIRE(f);
        }

        SECTION("unreacheable") {
            peer_file->mark_unreachable(true);
            auto f = next(true);
            REQUIRE(!f);
        }
    }

    SECTION("file locking && marking unreacheable") {
        auto file = proto::FileInfo();
        file.set_name("a.txt");
        file.set_sequence(10ul);
        file.set_invalid(true);
        auto peer_folder = folder_infos.by_device(*peer_device);

        REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());
        REQUIRE(!next(true));
    }

    SECTION("2 files at peer") {
        auto file_1 = proto::FileInfo();
        file_1.set_name("a.txt");
        file_1.set_sequence(10ul);

        SECTION("simple_cases") {
            auto file_2 = proto::FileInfo();
            file_2.set_name("b.txt");
            file_2.set_sequence(9ul);

            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id())
                        .add(file_1, peer_device)
                        .add(file_2, peer_device)
                        .finish()
                        .apply());

            SECTION("files are missing at my side") {
                auto f1 = next(true);
                REQUIRE(f1);
                CHECK(f1->get_name() == "a.txt");

                auto f2 = next();
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!next());
            }

            SECTION("one file is already exists on my side") {
                auto my_folder = folder_infos.by_device(*my_device);
                auto pr_file = proto::FileInfo();
                pr_file.set_name("a.txt");
                auto my_file = file_info_t::create(sequencer->next_uuid(), pr_file, my_folder).value();
                my_folder->add(my_file, false);

                auto peer_folder = folder_infos.by_device(*peer_device);
                REQUIRE(peer_folder->get_file_infos().size() == 2);

                auto f2 = next(true);
                REQUIRE(f2);
                CHECK(f2->get_name() == "b.txt");

                REQUIRE(!next());
            }
        }

        SECTION("a file on peer side is newer then on my") {
            auto oth_version = file_1.mutable_version();
            auto counter = oth_version->add_counters();
            counter->set_id(12345ul);
            counter->set_value(1233ul);

            builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file_1, peer_device).finish();
            REQUIRE(builder.apply());

            proto::Vector my_version;
            auto my_folder = folder_infos.by_device(*my_device);
            auto pr_file = proto::FileInfo();
            pr_file.set_name("a.txt");
            auto my_file = file_info_t::create(sequencer->next_uuid(), pr_file, my_folder).value();
            my_file->mark_local();
            my_folder->add(my_file, false);

            auto f = next(true);
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            REQUIRE(!next());
        }

        SECTION("local file is not scanned yet") {
            auto oth_version = file_1.mutable_version();
            auto counter = oth_version->add_counters();
            counter->set_id(12345ul);
            counter->set_value(1233ul);

            REQUIRE(
                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file_1, peer_device).finish().apply());

            proto::Vector my_version;
            auto my_folder = folder_infos.by_device(*my_device);
            auto pr_file = proto::FileInfo();
            pr_file.set_name("a.txt");
            auto my_file = file_info_t::create(sequencer->next_uuid(), pr_file, my_folder).value();
            my_folder->add(my_file, false);

            REQUIRE(!next(true));
        }

        SECTION("a file on peer side is incomplete") {
            file_1.set_size(5ul);
            file_1.set_block_size(5ul);
            auto b = file_1.add_blocks();
            b->set_hash("123");
            b->set_size(5ul);

            REQUIRE(
                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file_1, peer_device).finish().apply());

            auto my_folder = folder_infos.by_device(*my_device);
            my_folder->set_max_sequence(file_1.sequence());
            auto my_file = file_info_t::create(sequencer->next_uuid(), file_1, my_folder).value();
            my_file->mark_local();
            my_folder->add(my_file, false);

            auto f = next(true);
            REQUIRE(f);
            CHECK(f->get_name() == "a.txt");
            REQUIRE(!next());
        }
    }

    SECTION("file priorities") {
        auto file_1 = proto::FileInfo();
        file_1.set_name("a.txt");
        file_1.set_sequence(10ul);
        file_1.set_size(10ul);
        file_1.set_block_size(5ul);
        *file_1.add_blocks() = b;
        *file_1.add_blocks() = b;
        auto version_1 = file_1.mutable_version();
        auto counter_1 = version_1->add_counters();
        counter_1->set_id(14ul);
        counter_1->set_value(1ul);

        auto file_2 = proto::FileInfo();
        file_2.set_name("b.txt");
        file_2.set_sequence(9ul);
        file_2.set_size(10ul);
        file_2.set_block_size(5ul);
        *file_2.add_blocks() = b;
        *file_2.add_blocks() = b;
        auto version_2 = file_2.mutable_version();
        auto counter_2 = version_2->add_counters();
        counter_2->set_id(15ul);
        counter_2->set_value(1ul);

        REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id())
                    .add(file_1, peer_device)
                    .add(file_2, peer_device)
                    .finish()
                    .apply());

        auto peer_folder = folder_infos.by_device(*peer_device);
        auto &peer_files = peer_folder->get_file_infos();
        auto f1 = peer_files.by_name(file_1.name());
        auto f2 = peer_files.by_name(file_2.name());

        // temporally
        auto r1 = next(true);
        auto r2 = next(false);
        REQUIRE(!next());
        REQUIRE(r1);
        REQUIRE(((r1 == f1) || (r1 == f2)));
        REQUIRE(r2);
        REQUIRE(((r2 == f1) || (r2 == f2)));
    }

    SECTION("file actualization") {
        auto file_a = proto::FileInfo();
        file_a.set_name("a.txt");
        file_a.set_sequence(10ul);

        auto file_b = proto::FileInfo();
        file_b.set_name("b.txt");
        file_b.set_sequence(9ul);
        auto peer_folder = folder->get_folder_infos().by_device(*peer_device);

        REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id())
                    .add(file_a, peer_device)
                    .add(file_b, peer_device)
                    .finish()
                    .apply());
        auto orig_file = peer_folder->get_file_infos().by_name("b.txt");

        auto f_1 = next(true);
        REQUIRE(f_1);

        auto data = orig_file->as_db(false);
        auto updated_file = model::file_info_t::create(orig_file->get_key(), data, peer_folder).value();
        peer_folder->get_file_infos().put(updated_file);

        auto f_2 = next(false);
        REQUIRE(f_2);
        CHECK(f_2 == updated_file);
    }
#endif
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
