// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
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
                auto pr_fi = proto::FileInfo();
                proto::set_name(pr_fi, "a.txt");
                proto::set_sequence(pr_fi, 10);
                auto &v = proto::get_version(pr_fi);
                proto::add_counters(v, proto::Counter(peer_device->device_id().get_uint(), 0));
                auto index_builder = builder.make_index(peer_id.get_sha256(), folder->get_id());

                SECTION("regular file") {
                    auto ec = index_builder.add(pr_fi, peer_device).finish().apply();
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
                SECTION("symblink") {
                    proto::set_symlink_target(pr_fi, "b.txt");
                    proto::set_type(pr_fi, proto::FileInfoType::SYMLINK);
                    auto ec = index_builder.add(pr_fi, peer_device).finish().apply();
                    REQUIRE(ec);

                    auto [f, action] = file_iterator->next();
#ifdef SYNCSPIRIT_WIN
                    CHECK(action == A::ignore);
#else
                    REQUIRE(f);
                    CHECK(f->get_name() == "a.txt");
                    CHECK(!f->is_locked());
                    CHECK(action == A::remote_copy);
#endif
                }
            }
            SECTION("invalid file is ignored") {
                auto pr_fi = proto::FileInfo();
                proto::set_name(pr_fi, "a.txt");
                proto::set_sequence(pr_fi, 10);
                proto::set_invalid(pr_fi, true);
                REQUIRE(builder.apply());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
            }

            SECTION("version checks") {
                auto pr_fi = proto::FileInfo();
                proto::set_name(pr_fi, "a.txt");
                proto::set_sequence(pr_fi, 10);
                auto &v = proto::get_version(pr_fi);
                auto &c_1 = proto::add_counters(v);
                proto::set_id(c_1, 1);
                proto::set_value(c_1, 5);

                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish();

                SECTION("my version < peer version") {
                    proto::set_value(c_1, 3);
                    REQUIRE(builder.apply());

                    auto my_file = file_info_t::create(sequencer->next_uuid(), pr_fi, my_folder).value();
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
                    proto::set_value(c_1, 3);

                    auto my_file = file_info_t::create(sequencer->next_uuid(), pr_fi, my_folder).value();
                    my_files.put(my_file);

                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }

                SECTION("my version > peer version") {
                    REQUIRE(builder.apply());
                    proto::set_value(c_1, 10);

                    auto my_file = file_info_t::create(sequencer->next_uuid(), pr_fi, my_folder).value();
                    my_file->mark_local();
                    my_files.put(my_file);

                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }

                SECTION("my version == peer version") {
                    REQUIRE(builder.apply());

                    auto my_file = file_info_t::create(sequencer->next_uuid(), pr_fi, my_folder).value();
                    my_file->mark_local();
                    my_files.put(my_file);

                    CHECK(file_iterator->next() == R{nullptr, A::ignore});
                }
            }
        }
        SECTION("2+ files") {
            auto files = model::file_infos_map_t();
            auto pr_fi_1 = proto::FileInfo();
            proto::set_name(pr_fi_1, "a.txt");
            proto::set_sequence(pr_fi_1, 10ul);
            auto pr_fi_2 = proto::FileInfo();
            proto::set_name(pr_fi_2, "b.txt");
            proto::set_sequence(pr_fi_2, 11ul);
            builder.make_index(peer_id.get_sha256(), folder->get_id())
                .add(pr_fi_1, peer_device)
                .add(pr_fi_2, peer_device)
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

                auto pr_fi_3 = proto::FileInfo();
                proto::set_name(pr_fi_3, "c.txt");
                proto::set_sequence(pr_fi_3, 12ul);
                auto ec = builder.make_index(peer_id.get_sha256(), folder->get_id())
                              .add(pr_fi_3, peer_device)
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
        auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
        auto b1 = proto::BlockInfo();
        proto::set_hash(b1, b1_hash);
        proto::set_size(b1, 5);

        auto b2_hash = utils::sha256_digest(as_bytes("67890")).value();
        auto b2 = proto::BlockInfo();
        proto::set_hash(b2, b2_hash);
        proto::set_size(b2, 5);

        auto &blocks_map = cluster->get_blocks();
        blocks_map.put(block_info_t::create(b1).value());
        blocks_map.put(block_info_t::create(b2).value());

        SECTION("missing locally") {
            auto pr_fi = proto::FileInfo();
            proto::set_name(pr_fi, "a.txt");
            proto::set_sequence(pr_fi, 10);
            proto::set_size(pr_fi, proto::get_size(b1));
            proto::set_block_size(pr_fi, proto::get_size(b1));
            proto::add_blocks(pr_fi, b1);

            REQUIRE(
                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish().apply());

            SECTION("folder is suspended") {
                REQUIRE(builder.suspend(*folder).apply());
                auto [f, action] = file_iterator->next();
                CHECK(!f);
                CHECK(action == A::ignore);
            }

            SECTION("folder is not suspended") {
                auto [f, action] = file_iterator->next();
                REQUIRE(f);
                CHECK(action == A::remote_copy);
                CHECK(f->get_name() == "a.txt");
                CHECK(!f->is_locked());
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
            }
        }

        SECTION("have local, but outdated") {
            auto pr_fi = proto::FileInfo();
            proto::set_name(pr_fi, "a.txt");
            proto::set_sequence(pr_fi, 10);
            proto::set_size(pr_fi, proto::get_size(b1));
            proto::set_block_size(pr_fi, proto::get_size(b1));
            proto::add_blocks(pr_fi, b1);

            auto &v = proto::get_version(pr_fi);
            proto::add_counters(v, proto::Counter(1, 1));

            auto my_file = file_info_t::create(sequencer->next_uuid(), pr_fi, my_folder).value();
            my_files.put(my_file);

            proto::add_counters(v, proto::Counter(2, 2));
            REQUIRE(
                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish().apply());

            SECTION("has not bee scanned") {
                auto peer_file = peer_files.begin()->item;
                file_iterator->recheck(*peer_file);
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
            }
            SECTION("has been scanned") {
                my_file->mark_local();

                auto [f, action] = file_iterator->next();
                REQUIRE(f);
                CHECK(action == A::remote_copy);
                CHECK(f->get_name() == "a.txt");
                CHECK(file_iterator->next() == R{nullptr, A::ignore});
                REQUIRE(builder.apply());
            }
        }

        SECTION("have local, local is newer") {
            auto pr_fi = proto::FileInfo();
            proto::set_name(pr_fi, "a.txt");
            proto::set_sequence(pr_fi, 10);
            proto::set_size(pr_fi, proto::get_size(b1));
            proto::set_block_size(pr_fi, proto::get_size(b1));
            proto::add_blocks(pr_fi, b1);

            auto &v = proto::get_version(pr_fi);
            proto::add_counters(v, proto::Counter(1, 1));

            builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish();

            proto::add_counters(v, proto::Counter(2, 2));
            auto my_file = file_info_t::create(sequencer->next_uuid(), pr_fi, my_folder).value();
            my_files.put(my_file);

            REQUIRE(builder.apply());
            CHECK(file_iterator->next() == R{nullptr, A::ignore});
        }

        SECTION("peer file is unreacheable") {
            auto pr_fi = proto::FileInfo();
            proto::set_name(pr_fi, "a.txt");
            proto::set_sequence(pr_fi, 10);
            proto::set_size(pr_fi, proto::get_size(b1));
            proto::set_block_size(pr_fi, proto::get_size(b1));
            proto::add_blocks(pr_fi, b1);

            REQUIRE(
                builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish().apply());
            auto f = peer_files.by_name(proto::get_name(pr_fi));
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

    auto &folders = cluster->get_folders();
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    auto folder = folders.by_id("1234-5678");
    auto &folder_infos = cluster->get_folders().by_id(folder->get_id())->get_folder_infos();
    REQUIRE(folder_infos.size() == 2u);

    auto peer_folder = folder_infos.by_device(*peer_device);
    auto my_folder = folder_infos.by_device(*my_device);
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

    auto pr_fi_1 = proto::FileInfo();
    proto::set_name(pr_fi_1, "a.txt");
    proto::set_sequence(pr_fi_1, 10);

    auto pr_fi_2 = proto::FileInfo();
    proto::set_name(pr_fi_2, "b.txt");
    proto::set_sequence(pr_fi_2, 11);

    using set_t = std::set<std::string_view>;

    SECTION("cloning") {
        REQUIRE(
            builder.make_index(sha256, "1234").add(pr_fi_1, peer_device).add(pr_fi_2, peer_device).finish().apply());

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
        auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
        auto b1 = proto::BlockInfo();
        proto::set_hash(b1, b1_hash);
        proto::set_size(b1, 5);

        auto b2_hash = utils::sha256_digest(as_bytes("67890")).value();
        auto b2 = proto::BlockInfo();
        proto::set_hash(b2, b1_hash);
        proto::set_size(b2, 5);

        auto &blocks_map = cluster->get_blocks();
        blocks_map.put(block_info_t::create(b1).value());
        blocks_map.put(block_info_t::create(b2).value());

        proto::add_blocks(pr_fi_1, b1);
        proto::set_size(pr_fi_1, proto::get_size(b1));

        proto::add_blocks(pr_fi_2, b2);
        proto::set_size(pr_fi_2, proto::get_size(b2));

        using set_t = std::set<std::string_view>;
        REQUIRE(
            builder.make_index(sha256, "1234").add(pr_fi_1, peer_device).add(pr_fi_2, peer_device).finish().apply());

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
    auto pr_fi = proto::FileInfo();
    proto::set_name(pr_fi, "a.txt");
    proto::set_sequence(pr_fi, 10);
    REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish().apply());

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
    REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish().apply());

    std::tie(f, action) = file_iterator->next();
    REQUIRE(f);
    CHECK(action == A::remote_copy);
    CHECK(f->get_name() == "a.txt");

    peer_device->release_iterator(file_iterator);
    file_iterator = peer_device->create_iterator(*cluster);
    std::tie(f, action) = file_iterator->next();
    REQUIRE(f);
    CHECK(action == A::remote_copy);
    CHECK(f->get_name() == "a.txt");

    auto folder_peer = folder->get_folder_infos().by_device(*peer_device);
    peer_device->release_iterator(file_iterator);
    file_iterator = peer_device->create_iterator(*cluster);
    REQUIRE(builder.upsert_folder_info(*folder_peer, folder_peer->get_index() + 1).apply());

    folder_peer = folder->get_folder_infos().by_device(*peer_device);
    file_iterator->on_upsert(folder_peer);
    std::tie(f, action) = file_iterator->next();
    CHECK(!f);
}

TEST_CASE("file pull order", "[model]") {
    using names_t = std::vector<std::string>;
    struct file_meta_t {
        std::string_view name;
        std::int64_t size;
        std::int64_t modified;
    };
    file_meta_t file_metas[5] = {
        {"0.txt", 0, 123}, {"1/a.txt", 5, 5000}, {"1/c.txt", 15, 4000}, {"1/d.txt", 10, 4500}, {"1/e.txt", 20, 6000},
    };

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

    auto index = builder.make_index(peer_id.get_sha256(), folder->get_id());
    std::int64_t sequence = 10;
    for (auto &meta : file_metas) {
        auto pr = proto::FileInfo();
        proto::set_name(pr, meta.name);
        proto::set_size(pr, meta.size);
        proto::set_modified_s(pr, meta.modified);

        if (meta.size) {
            auto bytes_view = utils::bytes_view_t((unsigned char *)&file_metas, meta.size);
            auto hash = utils::sha256_digest(bytes_view).value();
            auto block = proto::BlockInfo();
            proto::set_hash(block, hash);
            proto::set_size(block, meta.size);
            proto::add_blocks(pr, std::move(block));
        }

        proto::set_sequence(pr, sequence++);
        index.add(pr, peer_device);
    }
    REQUIRE(index.finish().apply());

    SECTION("iteration anew") {
        auto iterate = [&]() -> names_t {
            auto file_iterator = peer_device->create_iterator(*cluster);
            auto names = names_t();
            while (true) {
                auto [fi, action] = file_iterator->next();
                if (!fi) {
                    break;
                };
                CHECK(action == A::remote_copy);
                names.emplace_back(std::string(fi->get_name()));
            }
            return names;
        };

        auto &pull_order = ((model::folder_data_t *)folder.get())->access<test::to::pull_order>();
        SECTION("defaul order (alphabetic)") {
            auto expected = names_t{"0.txt", "1/a.txt", "1/c.txt", "1/d.txt", "1/e.txt"};
            CHECK(iterate() == expected);
        }
        SECTION("random (aka alphabetic)") {
            pull_order = db::PullOrder::random;
            auto expected = names_t{"0.txt", "1/a.txt", "1/c.txt", "1/d.txt", "1/e.txt"};
            CHECK(iterate() == expected);
        }
        SECTION("alphabetic") {
            pull_order = db::PullOrder::alphabetic;
            auto expected = names_t{"0.txt", "1/a.txt", "1/c.txt", "1/d.txt", "1/e.txt"};
            CHECK(iterate() == expected);
        }
        SECTION("smallest") {
            pull_order = db::PullOrder::smallest;
            auto expected = names_t{"0.txt", "1/a.txt", "1/d.txt", "1/c.txt", "1/e.txt"};
            CHECK(iterate() == expected);
        }
        SECTION("largest") {
            pull_order = db::PullOrder::largest;
            auto expected = names_t{"0.txt", "1/e.txt", "1/c.txt", "1/d.txt", "1/a.txt"};
            CHECK(iterate() == expected);
        }
        SECTION("oldest") {
            pull_order = db::PullOrder::oldest;
            auto expected = names_t{"0.txt", "1/c.txt", "1/d.txt", "1/a.txt", "1/e.txt"};
            CHECK(iterate() == expected);
        }
        SECTION("newest") {
            pull_order = db::PullOrder::newest;
            auto expected = names_t{"0.txt", "1/e.txt", "1/a.txt", "1/d.txt", "1/c.txt"};
            CHECK(iterate() == expected);
        }
    }
    SECTION("change iteration order") {
        auto &pull_order = ((model::folder_data_t *)folder.get())->access<test::to::pull_order>();
        pull_order = db::PullOrder::alphabetic;
        auto file_iterator = peer_device->create_iterator(*cluster);
        auto names = names_t();
        auto next = [&]() {
            auto [fi, action] = file_iterator->next();
            REQUIRE(fi);
            CHECK(action == A::remote_copy);
            names.emplace_back(std::string(fi->get_name()));
        };

        next();
        next();

        pull_order = db::PullOrder::newest;
        file_iterator->on_upsert(*folder);
        next();
        next();
        next();

        auto expected = names_t{"0.txt", "1/a.txt", "1/e.txt", "1/d.txt", "1/c.txt"};
        CHECK(names == expected);
    }
}

TEST_CASE("no file iteration for send-only folder", "[model]") {
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

    db::Folder db_folder;
    db::set_id(db_folder, "1234-5678");
    db::set_label(db_folder, "l");
    db::set_path(db_folder, "/my/path");
    db::set_folder_type(db_folder, db::FolderType::send);

    REQUIRE(builder.upsert_folder(db_folder).apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    auto folder = folders.by_id("1234-5678");
    auto folder_infos = &folder->get_folder_infos();
    REQUIRE(folder_infos->size() == 2u);

    auto file_iterator = peer_device->create_iterator(*cluster);
    auto pr_fi = proto::FileInfo();
    proto::set_name(pr_fi, "a.txt");
    proto::set_sequence(pr_fi, 10);
    REQUIRE(builder.make_index(peer_id.get_sha256(), folder->get_id()).add(pr_fi, peer_device).finish().apply());

    auto [f, action] = file_iterator->next();
    REQUIRE(!f);

    SECTION("back to send & receive") {
        db::set_folder_type(db_folder, db::FolderType::send_and_receive);
        REQUIRE(builder.upsert_folder(db_folder).apply());
        auto [f, action] = file_iterator->next();
        REQUIRE(f);
        REQUIRE(action == A::remote_copy);
        CHECK(f->get_name() == "a.txt");
    }
}

int _init() {
    test::init_logging();
    return 1;
}

static int v = _init();
