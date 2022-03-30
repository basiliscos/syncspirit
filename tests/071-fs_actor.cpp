// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "catch.hpp"
#include "test-utils.h"
#include "fs/file_actor.h"
#include "fs/utils.h"
#include "model/diff/aggregate.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/flush_file.h"
#include "net/messages.h"
#include "test_supervisor.h"
#include "access.h"
#include "model/cluster.h"
#include "access.h"
#include <boost/filesystem.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SYNCSPIRIT_WIN
#endif

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

    fixture_t() noexcept : root_path{bfs::unique_path()}, path_quard{root_path} {
        utils::set_default("trace");
        bfs::create_directory(root_path);
    }

    virtual supervisor_t::configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::starter_plugin_t>(
                [&](auto &p) { p.subscribe_actor(r::lambda<msg_t>([&](msg_t &msg) { reply = &msg; })); });
        };
    }

    virtual void run() noexcept {
        auto my_id =
            device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        auto my_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(my_device, 1);
        auto peer_id =
            device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        file_actor = sup->create_actor<fs::file_actor_t>().mru_size(2).cluster(cluster).timeout(timeout).finish();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        file_addr = file_actor->get_address();

        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path(root_path.string());
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
        sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
        sup->do_process();

        auto sha256 = peer_device->device_id().get_sha256();
        diff = diff::cluster_diff_ptr_t(new diff::modify::share_folder_t(sha256, db_folder.id()));
        sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
        sup->do_process();

        folder = cluster->get_folders().by_id(db_folder.id());
        folder_my = folder->get_folder_infos().by_device(my_device);
        folder_peer = folder->get_folder_infos().by_device(peer_device);

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
    model::device_ptr_t peer_device;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_my;
    model::folder_info_ptr_t folder_peer;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<fs::file_actor_t> file_actor;
    bfs::path root_path;
    path_guard_t path_quard;
    r::system_context_t ctx;
    msg_ptr_t reply;
    db::Folder db_folder;
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
                auto file = file_info_t::create(cluster->next_uuid(), pr_fi, folder_peer).value();
                folder_peer->add(file);
                return file;
            };

            SECTION("empty regular file") {
                auto peer_file = make_file();
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto my_file = folder_my->get_file_infos().by_name(peer_file->get_name());

                auto &path = my_file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
                REQUIRE(bfs::last_write_time(path) == 1641828421);
            }

            SECTION("empty regular file a subdir") {
                pr_fi.set_name("a/b/c/d/e.txt");
                auto peer_file = make_file();
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

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
                auto bi = block_info_t::create(b).value();
                auto &blocks_map = cluster->get_blocks();
                blocks_map.put(bi);

                auto peer_file = make_file();
                peer_file->assign_block(bi, 0);
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());

                auto filename = std::string(file->get_name()) + ".syncspirit-tmp";
                auto path = root_path / filename;
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 5);
            }

            SECTION("directory") {
                pr_fi.set_type(proto::FileInfoType::DIRECTORY);

                auto peer_file = make_file();
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

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
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

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
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto file = folder_my->get_file_infos().by_name(pr_fi.name());
                CHECK(file->is_deleted());

                auto &path = file->get_path();
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
                auto file = file_info_t::create(cluster->next_uuid(), pr_source, folder_peer).value();
                for (size_t i = 0; i < count; ++i) {
                    file->assign_block(blocks[i], i);
                }
                folder_peer->add(file);
                return file;
            };

            SECTION("file with 1 block") {
                pr_source.set_size(5ul);

                auto peer_file = make_file(1);
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto file = folder_my->get_file_infos().by_name(pr_source.name());

                auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*peer_file, 0, "12345"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                diff = new diff::modify::flush_file_t(*peer_file);
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

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
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*peer_file));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto file = folder_my->get_file_infos().by_name(pr_source.name());

                auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*peer_file, 0, "12345"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                auto filename = std::string(file->get_name()) + ".syncspirit-tmp";
                auto path = root_path / filename;
#ifndef SYNCSPIRIT_WIN
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 10);
                auto data = read_file(path);
                CHECK(data.substr(0, 5) == "12345");
#endif

                bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*peer_file, 1, "67890"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                SECTION("add 2nd block") {
                    diff = new diff::modify::flush_file_t(*peer_file);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

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
                    diff = new diff::modify::flush_file_t(*peer_file);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();
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
                auto file = file_info_t::create(cluster->next_uuid(), fi, folder_peer).value();
                for (size_t i = 0; i < count; ++i) {
                    file->assign_block(blocks[i], i);
                }
                folder_peer->add(file);
                return file;
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

                    auto diffs = diff::aggregate_t::diffs_t{};
                    diffs.push_back(new diff::modify::clone_file_t(*source));
                    diffs.push_back(new diff::modify::clone_file_t(*target));
                    auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

                    auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                    auto target_file = folder_peer->get_file_infos().by_name(pr_target.name());

                    auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 0, "12345"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    diff = new diff::modify::flush_file_t(*source);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

                    auto block = source_file->get_blocks()[0];
                    bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *block));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    diff = new diff::modify::flush_file_t(*target);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

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

                    auto diffs = diff::aggregate_t::diffs_t{};
                    diffs.push_back(new diff::modify::clone_file_t(*source));
                    diffs.push_back(new diff::modify::clone_file_t(*target));
                    auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

                    auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                    auto target_file = folder_peer->get_file_infos().by_name(pr_target.name());

                    auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 0, "12345"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 1, "67890"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    auto blocks = source_file->get_blocks();
                    bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *blocks[0]));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *blocks[1]));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    diff = new diff::modify::flush_file_t(*target_file);
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

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

                auto source = file_info_t::create(cluster->next_uuid(), pr_source, folder_peer).value();
                source->assign_block(blocks[0], 0);
                source->assign_block(blocks[0], 1);
                folder_peer->add(source);

                auto diffs = diff::aggregate_t::diffs_t{};
                diffs.push_back(new diff::modify::clone_file_t(*source));
                auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto source_file = folder_peer->get_file_infos().by_name(pr_source.name());
                auto target_file = source_file;

                auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 0, "12345"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                auto block = source_file->get_blocks()[0];
                bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *block));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                diff = new diff::modify::flush_file_t(*source);
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();
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

REGISTER_TEST_CASE(test_clone_file, "test_clone_file", "[fs]");
REGISTER_TEST_CASE(test_append_block, "test_append_block", "[fs]");
REGISTER_TEST_CASE(test_clone_block, "test_clone_block", "[fs]");
