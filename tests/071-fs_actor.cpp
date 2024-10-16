// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "test-utils.h"
#include "fs/file_actor.h"
#include "fs/utils.h"
#include "diff-builder.h"
#include "net/messages.h"
#include "test_supervisor.h"
#include "access.h"
#include "model/cluster.h"
#include "access.h"
#include <boost/filesystem.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace bfs = boost::filesystem;

namespace {

struct fixture_t {
    using msg_t = net::message::load_cluster_response_t;
    using msg_ptr_t = r::intrusive_ptr_t<msg_t>;
    using blk_res_t = fs::message::block_response_t;
    using blk_res_ptr_t = r::intrusive_ptr_t<blk_res_t>;

    fixture_t() noexcept : root_path{bfs::unique_path()}, path_guard{root_path} {
        utils::set_default("trace");
        bfs::create_directory(root_path);
        sequencer = make_sequencer(67);
    }

    virtual supervisor_t::configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<msg_t>([&](msg_t &msg) { reply = &msg; }));
                p.subscribe_actor(r::lambda<blk_res_t>([&](blk_res_t &msg) { block_reply = &msg; }));
            });
        };
    }

    virtual void run() noexcept {
        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(my_device, 1);
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().auto_finish(false).timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto sha256 = peer_device->device_id().get_sha256();
        file_actor = sup->create_actor<fs::file_actor_t>()
                         .mru_size(2)
                         .cluster(cluster)
                         .sequencer(sup->sequencer)
                         .timeout(timeout)
                         .finish();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        file_addr = file_actor->get_address();

        auto builder = diff_builder_t(*cluster);
        builder.upsert_folder(folder_id, root_path.string(), "my-label")
            .apply(*sup)
            .update_peer(peer_device->device_id(), "some_name", "some-cn", true)
            .apply(*sup)
            .share_folder(sha256, folder_id)
            .apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);
        folder_my = folder->get_folder_infos().by_device(*my_device);
        folder_peer = folder->get_folder_infos().by_device(*peer_device);

        main();
        reply.reset();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::address_ptr_t file_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    model::device_ptr_t peer_device;
    model::device_ptr_t my_device;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_my;
    model::folder_info_ptr_t folder_peer;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<fs::file_actor_t> file_actor;
    bfs::path root_path;
    path_guard_t path_guard;
    r::system_context_t ctx;
    msg_ptr_t reply;
    blk_res_ptr_t block_reply;
    std::string_view folder_id = "1234-5678";
};
} // namespace

void test_clone_file() {
    struct F : fixture_t {
        void main() noexcept override {
            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            pr_fi.set_name("q.txt");
            pr_fi.set_modified_s(modified);
            auto version = pr_fi.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            auto make_file = [&]() {
                auto file = file_info_t::create(sequencer->next_uuid(), pr_fi, folder_peer).value();
                folder_peer->add(file, false);
                return file;
            };

            SECTION("empty regular file") {
                auto peer_file = make_file();
                diff_builder_t(*cluster).clone_file(*peer_file).apply(*sup);

                auto my_file = folder_my->get_file_infos().by_name(peer_file->get_name());
                auto &path = my_file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
                REQUIRE(bfs::last_write_time(path) == 1641828421);
            }

            SECTION("empty regular file a subdir") {
                pr_fi.set_name("a/b/c/d/e.txt");
                auto peer_file = make_file();
                diff_builder_t(*cluster).clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());
                auto &path = file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
            }

            SECTION("non-empty regular file") {
                pr_fi.set_size(5);
                pr_fi.set_block_size(5);

                auto b = proto::BlockInfo();
                b.set_hash(utils::sha256_digest("12345").value());
                b.set_weak_hash(555);
                b.set_size(5ul);
                auto bi = block_info_t::create(b).value();
                auto &blocks_map = cluster->get_blocks();
                blocks_map.put(bi);

                auto peer_file = make_file();
                peer_file->assign_block(bi, 0);
                diff_builder_t(*cluster).clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());

                auto filename = std::string(file->get_name()) + ".syncspirit-tmp";
                auto path = root_path / filename;
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 5);
            }

            SECTION("directory") {
                pr_fi.set_type(proto::FileInfoType::DIRECTORY);

                auto peer_file = make_file();
                diff_builder_t(*cluster).clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());

                auto &path = file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::is_directory(path));
            }

#ifndef SYNCSPIRIT_WIN
            SECTION("symlink") {
                bfs::path target = root_path / "some-existing-file";
                write_file(target, "zzz");

                pr_fi.set_type(proto::FileInfoType::SYMLINK);
                pr_fi.set_symlink_target(target.string());

                auto peer_file = make_file();
                diff_builder_t(*cluster).clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());

                auto &path = file->get_path();
                CHECK(!bfs::exists(path));
                CHECK(bfs::is_symlink(path));
                CHECK(bfs::read_symlink(path) == target);
            }
#endif

            SECTION("deleted file") {
                pr_fi.set_deleted(true);
                bfs::path target = root_path / pr_fi.name();
                write_file(target, "zzz");
                REQUIRE(bfs::exists(target));

                auto peer_file = make_file();
                diff_builder_t(*cluster).clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());
                CHECK(file->is_deleted());

                REQUIRE(!bfs::exists(target));
            }
        }
    };
    F().run();
}

void test_append_block() {
    struct F : fixture_t {
        void main() noexcept override {
            std::int64_t modified = 1641828421;
            proto::FileInfo pr_source;
            pr_source.set_name("q.txt");
            pr_source.set_block_size(5ul);
            pr_source.set_modified_s(modified);
            auto version = pr_source.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);
            auto b = block_info_t::create(bi).value();

            auto bi2 = proto::BlockInfo();
            bi2.set_size(5);
            bi2.set_weak_hash(12);
            bi2.set_hash(utils::sha256_digest("67890").value());
            bi2.set_offset(0);
            auto b2 = block_info_t::create(bi2).value();

            cluster->get_blocks().put(b);
            cluster->get_blocks().put(b2);
            auto blocks = std::vector<block_info_ptr_t>{b, b2};

            auto make_file = [&](size_t count) {
                auto file = file_info_t::create(sequencer->next_uuid(), pr_source, folder_peer).value();
                for (size_t i = 0; i < count; ++i) {
                    file->assign_block(blocks[i], i);
                }
                folder_peer->add(file, false);
                return file;
            };

            auto builder = diff_builder_t(*cluster);

            auto callback = [&](diff::modify::block_transaction_t &diff) {
                REQUIRE(diff.errors.load() == 0);
                builder.ack_block(diff);
            };

            SECTION("file with 1 block") {
                pr_source.set_size(5ul);
                auto peer_file = make_file(1);
                builder.clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_source.name());

                builder.append_block(*peer_file, 0, "12345", callback).apply(*sup).finish_file(*peer_file).apply(*sup);

                auto path = root_path / std::string(file->get_name());
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 5);
                auto data = read_file(path);
                CHECK(data == "12345");
                CHECK(bfs::last_write_time(path) == 1641828421);
            }
            SECTION("file with 2 different blocks") {
                pr_source.set_size(10ul);

                auto peer_file = make_file(2);
                builder.clone_file(*peer_file).apply(*sup);

                auto file = folder_my->get_file_infos().by_name(pr_source.name());

                builder.append_block(*peer_file, 0, "12345", callback).apply(*sup);

                auto filename = std::string(file->get_name()) + ".syncspirit-tmp";
                auto path = root_path / filename;
#ifndef SYNCSPIRIT_WIN
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 10);
                auto data = read_file(path);
                CHECK(data.substr(0, 5) == "12345");
#endif
                builder.append_block(*peer_file, 1, "67890", callback).apply(*sup);

                SECTION("add 2nd block") {
                    builder.finish_file(*peer_file).apply(*sup);

                    filename = std::string(file->get_name());
                    path = root_path / filename;
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 10);
                    auto data = read_file(path);
                    CHECK(data == "1234567890");
                    CHECK(bfs::last_write_time(path) == 1641828421);
                }

#ifndef SYNCSPIRIT_WIN
                SECTION("remove folder (simulate err)") {
                    bfs::remove_all(root_path);
                    diff_builder_t(*cluster).finish_file(*peer_file).apply(*sup);
                    CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() ==
                          r::state_t::SHUT_DOWN);
                }
#endif
            }
        }
    };
    F().run();
}

void test_clone_block() {
    struct F : fixture_t {
        void main() noexcept override {

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);
            auto b = block_info_t::create(bi).value();

            auto bi2 = proto::BlockInfo();
            bi2.set_size(5);
            bi2.set_weak_hash(12);
            bi2.set_hash(utils::sha256_digest("67890").value());
            bi2.set_offset(0);
            auto b2 = block_info_t::create(bi2).value();

            cluster->get_blocks().put(b);
            cluster->get_blocks().put(b2);
            auto blocks = std::vector<block_info_ptr_t>{b, b2};

            std::int64_t modified = 1641828421;
            proto::FileInfo pr_source;
            pr_source.set_name("a.txt");
            pr_source.set_block_size(5ul);
            pr_source.set_modified_s(modified);
            auto version = pr_source.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            auto make_file = [&](const proto::FileInfo &fi, size_t count) {
                auto file = file_info_t::create(sequencer->next_uuid(), fi, folder_peer).value();
                for (size_t i = 0; i < count; ++i) {
                    file->assign_block(blocks[i], i);
                }
                folder_peer->add(file, false);
                return file;
            };

            auto builder = diff_builder_t(*cluster);

            auto callback = [&](diff::modify::block_transaction_t &diff) {
                REQUIRE(diff.errors.load() == 0);
                builder.ack_block(diff);
            };

            SECTION("source & target are different files") {
                proto::FileInfo pr_target;
                pr_target.set_name("b.txt");
                pr_target.set_block_size(5ul);
                (*pr_target.mutable_version()) = *version;

                SECTION("single block target file") {
                    pr_source.set_size(5ul);
                    pr_target.set_size(5ul);
                    pr_target.set_modified_s(modified);

                    auto source = make_file(pr_source, 1);
                    auto target = make_file(pr_target, 1);

                    builder.clone_file(*source).clone_file(*target).apply(*sup);

                    auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                    auto target_file = folder_peer->get_file_infos().by_name(pr_target.name());

                    builder.append_block(*source_file, 0, "12345", callback)
                        .apply(*sup)
                        .finish_file(*source_file)
                        .apply(*sup);

                    auto block = source_file->get_blocks()[0];
                    auto file_block = model::file_block_t(block.get(), target_file.get(), 0);
                    builder.clone_block(file_block, callback).apply(*sup).finish_file(*target).apply(*sup);

                    auto path = root_path / std::string(target_file->get_name());
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 5);
                    auto data = read_file(path);
                    CHECK(data == "12345");
                    CHECK(bfs::last_write_time(path) == 1641828421);
                }

                SECTION("multi block target file") {
                    pr_source.set_size(10ul);
                    pr_target.set_size(10ul);
                    auto source = make_file(pr_source, 2);
                    auto target = make_file(pr_target, 2);

                    builder.clone_file(*source).clone_file(*target).apply(*sup);

                    auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                    auto target_file = folder_peer->get_file_infos().by_name(pr_target.name());

                    builder.append_block(*source_file, 0, "12345", callback)
                        .append_block(*source_file, 1, "67890", callback)
                        .apply(*sup);

                    auto blocks = source_file->get_blocks();
                    auto fb_1 = model::file_block_t(blocks[0].get(), target_file.get(), 0);
                    auto fb_2 = model::file_block_t(blocks[1].get(), target_file.get(), 1);
                    builder.clone_block(fb_1, callback)
                        .clone_block(fb_2, callback)
                        .apply(*sup)
                        .finish_file(*target)
                        .apply(*sup);

                    auto filename = std::string(target_file->get_name());
                    auto path = root_path / filename;
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 10);
                    auto data = read_file(path);
                    CHECK(data == "1234567890");
                }

                SECTION("source/target different sizes") {
                    pr_source.set_size(5ul);
                    pr_target.set_size(10ul);
                    auto target = make_file(pr_target, 2);

                    auto source = file_info_t::create(sequencer->next_uuid(), pr_source, folder_peer).value();
                    source->assign_block(blocks[1], 0);
                    folder_peer->add(source, false);

                    builder.clone_file(*source).clone_file(*target).apply(*sup);

                    auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                    auto target_file = folder_peer->get_file_infos().by_name(pr_target.name());

                    builder.append_block(*source_file, 0, "67890", callback)
                        .append_block(*target_file, 0, "12345", callback)
                        .apply(*sup);

                    auto blocks = source_file->get_blocks();
                    auto fb = model::file_block_t(blocks[0].get(), target_file.get(), 1);
                    builder.clone_block(fb, callback).apply(*sup).finish_file(*target).apply(*sup);

                    auto filename = std::string(target_file->get_name());
                    auto path = root_path / filename;
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 10);
                    auto data = read_file(path);
                    CHECK(data == "1234567890");
                }
            }

            SECTION("source & target are is the same file") {
                pr_source.set_size(10ul);

                auto source = file_info_t::create(sequencer->next_uuid(), pr_source, folder_peer).value();
                source->assign_block(blocks[0], 0);
                source->assign_block(blocks[0], 1);
                folder_peer->add(source, false);

                builder.clone_file(*source).apply(*sup);

                auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                auto target_file = source_file;

                builder.append_block(*source_file, 0, "12345", callback).apply(*sup);

                auto block = source_file->get_blocks()[0];
                auto file_block = model::file_block_t(block.get(), target_file.get(), 1);
                builder.clone_block(file_block, callback).apply(*sup).finish_file(*source).apply(*sup);

                auto path = root_path / std::string(target_file->get_name());
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 10);
                auto data = read_file(path);
                CHECK(data == "1234512345");
                CHECK(bfs::last_write_time(path) == 1641828421);
            }
        }
    };
    F().run();
}

void test_requesting_block() {
    struct F : fixture_t {
        void main() noexcept override {

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);
            auto b = block_info_t::create(bi).value();

            auto bi2 = proto::BlockInfo();
            bi2.set_size(5);
            bi2.set_weak_hash(12);
            bi2.set_hash(utils::sha256_digest("67890").value());
            bi2.set_offset(0);
            auto b2 = block_info_t::create(bi2).value();

            cluster->get_blocks().put(b);
            cluster->get_blocks().put(b2);
            bfs::path target = root_path / "a.txt";

            std::int64_t modified = 1641828421;
            proto::FileInfo pr_source;
            pr_source.set_name("a.txt");
            pr_source.set_block_size(5ul);
            pr_source.set_modified_s(modified);
            pr_source.set_size(10);
            auto version = pr_source.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(my_device->as_uint());
            *pr_source.add_blocks() = bi;
            *pr_source.add_blocks() = bi2;

            auto file = file_info_t::create(sequencer->next_uuid(), pr_source, folder_my).value();
            file->assign_block(b, 0);
            file->assign_block(b2, 1);
            folder_my->add(file, false);

            auto req = proto::Request();
            req.set_folder(std::string(folder->get_id()));
            req.set_name("a.txt");
            req.set_offset(0);
            req.set_size(5);
            auto req_ptr = proto::message::Request(new proto::Request(req));
            auto msg = r::make_message<fs::payload::block_request_t>(file_actor->get_address(), std::move(req_ptr),
                                                                     sup->get_address());

            SECTION("error, no file") {
                sup->put(msg);
                sup->do_process();
                REQUIRE(block_reply);
                REQUIRE(block_reply->payload.remote_request);
                REQUIRE(block_reply->payload.ec);
                REQUIRE(block_reply->payload.data.empty());
            }

            SECTION("error, oversized request") {
                write_file(target, "1234");
                sup->put(msg);
                sup->do_process();
                REQUIRE(block_reply);
                REQUIRE(block_reply->payload.remote_request);
                REQUIRE(block_reply->payload.ec);
                REQUIRE(block_reply->payload.data.empty());
            }

            SECTION("successful file reading") {
                write_file(target, "1234567890");
                sup->put(msg);
                sup->do_process();
                REQUIRE(block_reply);
                REQUIRE(block_reply->payload.remote_request);
                REQUIRE(!block_reply->payload.ec);
                REQUIRE(block_reply->payload.data == "12345");

                req.set_offset(5);
                auto req_ptr = proto::message::Request(new proto::Request(req));
                auto msg = r::make_message<fs::payload::block_request_t>(file_actor->get_address(), std::move(req_ptr),
                                                                         sup->get_address());
                sup->put(msg);
                sup->do_process();
                REQUIRE(block_reply);
                REQUIRE(block_reply->payload.remote_request);
                REQUIRE(!block_reply->payload.ec);
                REQUIRE(block_reply->payload.data == "67890");
            }
        }
    };
    F().run();
}

int _init() {
    REGISTER_TEST_CASE(test_clone_file, "test_clone_file", "[fs]");
    REGISTER_TEST_CASE(test_append_block, "test_append_block", "[fs]");
    REGISTER_TEST_CASE(test_clone_block, "test_clone_block", "[fs]");
    REGISTER_TEST_CASE(test_requesting_block, "test_requesting_block", "[fs]");
    return 1;
}

static int v = _init();
