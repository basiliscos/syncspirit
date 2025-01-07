// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/file_iterator.h"
#include "model/misc/sequencer.h"
#include "diff-builder.h"
#include <set>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;

using R = file_iterator_t::result_t;
using A = advance_action_t;

TEST_CASE("file iterator, single folder", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);

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

    SECTION("emtpy folders (1)") { CHECK(file_iterator->next() == R{nullptr, A::ignore}); }

    REQUIRE(builder.configure_cluster(peer_id.get_sha256())
                .add(peer_id.get_sha256(), folder->get_id(), 123, 10u)
                .finish()
                .apply());

    SECTION("emtpy folders (2)") { CHECK(file_iterator->next() == R{nullptr, A::ignore}); }

    SECTION("cloning (empty files)") {
        SECTION("1 file") {
            SECTION("no local file") {
                auto file = proto::FileInfo();
                file.set_name("a.txt");
                file.set_sequence(10ul);
                auto ec =
                    builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply();
                REQUIRE(ec);

                auto [f, action] = file_iterator->next();
                REQUIRE(f);
                CHECK(f->get_name() == "a.txt");
                CHECK(!f->is_locked());
                CHECK(action == A::remote_copy);

                REQUIRE(builder.apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});

                REQUIRE(builder.remote_copy(*f).apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
                CHECK(!f->is_locked());
            }
            SECTION("invalid file is ignored") {
                auto file = proto::FileInfo();
                file.set_name("a.txt");
                file.set_sequence(10ul);
                file.set_invalid(true);
                REQUIRE(builder.apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
            }

            SECTION("version checks") {
                auto file = proto::FileInfo();
                file.set_name("a.txt");
                file.set_sequence(10ul);
                auto c_1 = file.mutable_version()->add_counters();
                c_1->set_id(1);
                c_1->set_value(5);

                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish();

                SECTION("my version < peer version") {
                    c_1->set_value(3);
                    REQUIRE(builder.apply());

                    auto my_file = file_info_t::create(sequencer->next_uuid(), file, my_folder).value();
                    my_file->mark_local();
                    my_files.put(my_file);

                    auto [f, action] = file_iterator->next();
                    REQUIRE(f);
                    CHECK(action == A::remote_copy);
                    CHECK(f->get_folder_info()->get_device() == peer_device.get());
                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }

                SECTION("my version < peer version, but not scanned yet") {
                    REQUIRE(builder.apply());
                    c_1->set_value(3);

                    auto my_file = file_info_t::create(sequencer->next_uuid(), file, my_folder).value();
                    my_files.put(my_file);

                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }

                SECTION("my version > peer version") {
                    REQUIRE(builder.apply());

                    c_1->set_value(10);

                    auto my_file = file_info_t::create(sequencer->next_uuid(), file, my_folder).value();
                    my_file->mark_local();
                    my_files.put(my_file);

                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }

                SECTION("my version == peer version") {
                    REQUIRE(builder.apply());

                    auto my_file = file_info_t::create(sequencer->next_uuid(), file, my_folder).value();
                    my_file->mark_local();
                    my_files.put(my_file);

                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }
            }
        }
        SECTION("2+ files") {
            auto files = model::file_infos_map_t();
            auto file_1 = proto::FileInfo();
            file_1.set_name("a.txt");
            file_1.set_sequence(10ul);
            auto file_2 = proto::FileInfo();
            file_2.set_name("b.txt");
            file_2.set_sequence(11ul);
            builder.make_index(peer_id.get_sha256(), folder->get_id())
                .add(file_1, peer_device)
                .add(file_2, peer_device)
                .finish();
            REQUIRE(builder.apply());

            SECTION("both files are missing on my side") {
                auto [f, action] = file_iterator->next();
                REQUIRE(f);
                CHECK(action == A::remote_copy);
                CHECK((f->get_name() == "a.txt" || f->get_name() == "b.txt"));
                CHECK(!f->is_locked());
                files.put(f);
                REQUIRE(builder.apply());

                REQUIRE(builder.remote_copy(*f).apply());

                std::tie(f, action) = file_iterator->next();
                REQUIRE(f);
                CHECK((f->get_name() == "a.txt" || f->get_name() == "b.txt"));
                CHECK(!f->is_locked());
                CHECK(action == A::remote_copy);
                files.put(f);
                REQUIRE(builder.apply());
                REQUIRE(builder.remote_copy(*f).apply());

                CHECK(file_iterator->next() == R{nullptr, A::ignore});

                CHECK(files.by_name("a.txt"));
                CHECK(files.by_name("b.txt"));
            }
            SECTION("1 file is missing on my side") {
                auto peer_file = peer_files.by_name("a.txt");
                REQUIRE(peer_file);
                REQUIRE(builder.remote_copy(*peer_file).apply());
                auto [f, action] = file_iterator->next();
                REQUIRE(f);
                CHECK(action == A::remote_copy);
                CHECK(f->get_name() == "b.txt");
                CHECK(file_iterator->next() == R{nullptr, A::ignore});

                REQUIRE(builder.apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});

                REQUIRE(builder.remote_copy(*f).apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
                CHECK(!f->is_locked());
            }

            SECTION("0 files are missing on my side") {
                auto peer_file_1 = peer_files.by_name("a.txt");
                auto peer_file_2 = peer_files.by_name("b.txt");
                REQUIRE(builder.remote_copy(*peer_file_1).remote_copy(*peer_file_2).apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
            }
            SECTION("new file in new peer update") {
                auto [f_1, action_1] = file_iterator->next();
                REQUIRE(f_1);
                CHECK(action_1 == A::remote_copy);
                CHECK((f_1->get_name() == "a.txt" || f_1->get_name() == "b.txt"));
                CHECK(!f_1->is_locked());
                files.put(f_1);

                auto [f_2, action_2] = file_iterator->next();
                REQUIRE(f_2);
                CHECK(action_2 == A::remote_copy);
                CHECK((f_2->get_name() == "a.txt" || f_2->get_name() == "b.txt"));
                CHECK(!f_2->is_locked());
                files.put(f_2);

                CHECK(files.by_name("a.txt"));
                CHECK(files.by_name("b.txt"));

                auto file_3 = proto::FileInfo();
                file_3.set_name("c.txt");
                file_3.set_sequence(12ul);
                auto ec = builder.make_index(peer_id.get_sha256(), folder->get_id())
                              .add(file_3, peer_device)
                              .finish()
                              .apply();

                auto [f_3, action_3] = file_iterator->next();
                REQUIRE(f_3);
                CHECK(action_3 == A::remote_copy);
                CHECK(f_3->get_name() == "c.txt");
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
            }
        }
    }

    SECTION("synchronizing non-empty files") {
        auto b1 = proto::BlockInfo();
        b1.set_hash(utils::sha256_digest("12345").value());
        b1.set_weak_hash(555);
        b1.set_size(5ul);
        auto b2 = proto::BlockInfo();
        b1.set_hash(utils::sha256_digest("67890").value());
        b1.set_weak_hash(123);
        b1.set_size(5ul);

        auto &blocks_map = cluster->get_blocks();
        blocks_map.put(block_info_t::create(b1).value());
        blocks_map.put(block_info_t::create(b2).value());

        SECTION("missing locally") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b1.size());
            file.set_block_size(b1.size());
            *file.add_blocks() = b1;

            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

            auto [f, action] = file_iterator->next();
            REQUIRE(f);
            CHECK(action == A::remote_copy);
            CHECK(f->get_name() == "a.txt");
            CHECK(!f->is_locked());
            CHECK(file_iterator->next() == R{nullptr, A::ignore});
        }

        SECTION("have local, but outdated") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b1.size());
            file.set_block_size(b1.size());
            *file.add_blocks() = b1;

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

                auto [f, action] = file_iterator->next();
                REQUIRE(f);
                CHECK(action == A::remote_copy);
                CHECK(f->get_name() == "a.txt");
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
                REQUIRE(builder.apply());
            }

            SECTION("has not bee scanned") { CHECK(file_iterator->next() == R{nullptr, A::ignore}); }
        }

        SECTION("have local, local is newer") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b1.size());
            file.set_block_size(b1.size());
            *file.add_blocks() = b1;

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
            CHECK(file_iterator->next() == R{nullptr, A::ignore});
        }

        SECTION("peer file is unreacheable") {
            auto file = proto::FileInfo();
            file.set_name("a.txt");
            file.set_sequence(10ul);
            file.set_size(b1.size());
            file.set_block_size(b1.size());
            *file.add_blocks() = b1;

            REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());
            auto f = peer_files.by_name(file.name());
            REQUIRE(builder.remote_copy(*f).mark_reacheable(f, false).apply());

            CHECK(file_iterator->next() == R{nullptr, A::ignore});
        }
    }
}

TEST_CASE("file iterator for 2 folders", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);

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

    using set_t = std::set<std::string_view>;

    SECTION("cloning") {
        REQUIRE(builder.make_index(sha256, "1234").add(file1, peer_device).add(file2, peer_device).finish().apply());

        auto [f1, action1] = file_iterator->next();
        auto [f2, action2] = file_iterator->next();
        REQUIRE(f1);
        REQUIRE(f2);
        CHECK(action1 == A::remote_copy);
        CHECK(action2 == A::remote_copy);

        auto files = set_t{};
        files.emplace(f1->get_name());
        files.emplace(f2->get_name());

        CHECK((files == set_t{"a.txt", "b.txt"}));
        CHECK(file_iterator->next() == R{nullptr, A::ignore});
    }

    SECTION("syncing") {
        auto b1 = proto::BlockInfo();
        b1.set_hash(utils::sha256_digest("12345").value());
        b1.set_weak_hash(555);
        b1.set_size(5ul);
        auto b2 = proto::BlockInfo();
        b2.set_hash(utils::sha256_digest("67890").value());
        b2.set_weak_hash(123);
        b2.set_size(5ul);

        auto &blocks_map = cluster->get_blocks();
        blocks_map.put(block_info_t::create(b1).value());
        blocks_map.put(block_info_t::create(b2).value());

        *file1.add_blocks() = b1;
        file1.set_size(b1.size());

        *file2.add_blocks() = b2;
        file2.set_size(b2.size());

        using set_t = std::set<std::string_view>;
        REQUIRE(builder.make_index(sha256, "1234").add(file1, peer_device).add(file2, peer_device).finish().apply());

        auto files = set_t{};
        auto [f1, action1] = file_iterator->next();
        REQUIRE(f1);
        CHECK(action1 == A::remote_copy);
        files.emplace(f1->get_name());

        auto [f2, action2] = file_iterator->next();
        REQUIRE(f2);
        CHECK(action2 == A::remote_copy);
        files.emplace(f2->get_name());

        files.emplace(f1->get_name());
        files.emplace(f2->get_name());

        CHECK((files == set_t{"a.txt", "b.txt"}));
    }
}

TEST_CASE("file iterator, create, share, iterae, unshare, share, iterate", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);

    auto &folders = cluster->get_folders();
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    auto folder = folders.by_id("1234-5678");
    auto folder_infos = &folder->get_folder_infos();
    REQUIRE(folder_infos->size() == 2u);

    auto file_iterator = peer_device->create_iterator(*cluster);
    auto file = proto::FileInfo();
    file.set_name("a.txt");
    file.set_sequence(10ul);
    REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

    auto [f, action] = file_iterator->next();
    REQUIRE(f);
    CHECK(f->get_name() == "a.txt");
    CHECK(!f->is_locked());
    CHECK(action == A::remote_copy);

    REQUIRE(builder.apply());
    CHECK(file_iterator->next() == R{nullptr, A::ignore});
    REQUIRE(builder.remove_folder(*folder).apply());
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    folder = folders.by_id("1234-5678");
    folder_infos = &folder->get_folder_infos();
    REQUIRE(folder_infos->size() == 2u);
    REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(file, peer_device).finish().apply());

    std::tie(f, action) = file_iterator->next();
    REQUIRE(f);
    CHECK(action == A::remote_copy);
    CHECK(f->get_name() == "a.txt");
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
