#include "catch.hpp"
#include "test-utils.h"
#include "fs/file_actor.h"
#include "model/diff/aggregate.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/new_file.h"
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

namespace  {

struct fixture_t {
    using msg_t = net::message::load_cluster_response_t;
    using msg_ptr_t = r::intrusive_ptr_t<msg_t>;


    fixture_t() noexcept: root_path{ bfs::unique_path() }, path_quard{root_path} {
        utils::set_default("trace");
        root_path = bfs::unique_path();
        bfs::create_directory(root_path);
    }

    virtual supervisor_t::configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin){
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<msg_t>(
                    [&](msg_t &msg) { reply = &msg; }));
            });
        };
    }


    cluster_ptr_t make_cluster() noexcept {
        auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        auto my_device =  device_t::create(my_id, "my-device").value();

        return cluster_ptr_t(new cluster_t(my_device, 1));
    }

    virtual void run() noexcept {
        cluster = make_cluster();

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t*>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        file_actor = sup->create_actor<fs::file_actor_t>().mru_size(2).cluster(cluster).timeout(timeout).finish();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t*>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        file_addr = file_actor->get_address();

        db_folder.set_id("1234-5678");
        db_folder.set_label("my-label");
        db_folder.set_path(root_path.string());
        auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
        sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
        sup->do_process();

        main();
        reply.reset();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t*>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {
    }

    r::address_ptr_t file_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<fs::file_actor_t> file_actor;
    bfs::path root_path;
    path_guard_t path_quard;
    r::system_context_t ctx;
    msg_ptr_t reply;
    db::Folder db_folder;
};
}


void test_new_files() {
    struct F : fixture_t {
        void main() noexcept override {
            proto::FileInfo pr_fi;
            pr_fi.set_name("q.txt");

            SECTION("empty regular file") {
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_fi, {}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_fi.name());

                auto& path = file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
            }

            SECTION("empty regular file a subdir") {
                pr_fi.set_name("a/b/c/d/e.txt");
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_fi, {}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_fi.name());

                auto& path = file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
            }

            SECTION("non-empty regular file") {
                pr_fi.set_size(5);
                pr_fi.set_block_size(5);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_fi, {}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_fi.name());

                auto filename = std::string(file->get_name()) + ".syncspirit-tmp";
                auto path = root_path / filename;
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 5);
            }

            SECTION("directory") {
                pr_fi.set_type(proto::FileInfoType::DIRECTORY);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_fi, {}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_fi.name());

                auto& path = file->get_path();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::is_directory(path));
            }

            SECTION("symlink") {
                bfs::path target = root_path / "not-existing";

                pr_fi.set_type(proto::FileInfoType::SYMLINK);
                pr_fi.set_symlink_target(target.string());

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_fi, {}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_fi.name());

                auto& path = file->get_path();
                CHECK(!bfs::exists(path));
                CHECK(bfs::is_symlink(path));
                CHECK(bfs::read_symlink(path) == target);
            }

            SECTION("deleted file") {
                pr_fi.set_deleted(true);
                bfs::path target = root_path / pr_fi.name();
                write_file(target, "zzz");
                REQUIRE(bfs::exists(target));

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_fi, {}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_fi.name());
                CHECK(file->is_deleted());

                auto& path = file->get_path();
                REQUIRE(!bfs::exists(target));
            }

        }
    };
    F().run();
}

void test_append_block() {
    struct F : fixture_t {
        void main() noexcept override {
            proto::FileInfo pr_source;
            pr_source.set_name("q.txt");
            pr_source.set_block_size(5ul);

            auto bi1 = proto::BlockInfo();
            bi1.set_size(5);
            bi1.set_weak_hash(12);
            bi1.set_hash(utils::sha256_digest("12345").value());
            bi1.set_offset(0);

            SECTION("file with 1 block") {
                pr_source.set_size(5ul);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_source, {bi1}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_source.name());

                auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*file, 0, "12345"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                auto path = root_path / std::string(file->get_name());
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 5);
                auto data = read_file(path);
                CHECK(data == "12345");
            }

            SECTION("file with 2 different blocks") {
                pr_source.set_size(10ul);

                auto bi2 = proto::BlockInfo();
                bi2.set_size(5);
                bi2.set_weak_hash(12);
                bi2.set_hash(utils::sha256_digest("67890").value());
                bi2.set_offset(0);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_source, {bi1, bi2}));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto file = files_info.by_name(pr_source.name());

                auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*file, 0, "12345"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                auto filename = std::string(file->get_name()) + ".syncspirit-tmp";
                auto path = root_path / filename;
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 10);
                auto data = read_file(path);
                CHECK(data.substr(0, 5) == "12345");

                SECTION("add 2nd block") {
                    bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*file, 1, "67890"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();
                    filename = std::string(file->get_name());
                    path = root_path / filename;
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 10);
                    data = read_file(path);
                    CHECK(data == "1234567890");
                }

                SECTION("remove folder (simulate err)") {
                    bfs::remove_all(root_path);
                    bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*file, 1, "67890"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();
                    CHECK(static_cast<r::actor_base_t*>(file_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
                }
            }

        }
    };
    F().run();
}

void test_clone_block() {
    struct F : fixture_t {
        void main() noexcept override {

            auto bi1 = proto::BlockInfo();
            bi1.set_size(5);
            bi1.set_weak_hash(12);
            bi1.set_hash(utils::sha256_digest("12345").value());
            bi1.set_offset(0);

            proto::FileInfo pr_source;
            pr_source.set_name("a.txt");
            pr_source.set_block_size(5ul);

            SECTION("source & target are different files") {
                proto::FileInfo pr_target;
                pr_target.set_name("b.txt");
                pr_target.set_block_size(5ul);

                SECTION("single block target file") {
                    pr_source.set_size(5ul);
                    pr_target.set_size(5ul);

                    auto diffs = diff::aggregate_t::diffs_t{};
                    diffs.push_back(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_source, {bi1}));
                    diffs.push_back(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_target, {bi1}));

                    auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

                    auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                    auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                    auto source_file = files_info.by_name(pr_source.name());
                    auto target_file = files_info.by_name(pr_target.name());

                    auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 0, "12345"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    auto block = source_file->get_blocks()[0];
                    bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *block));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    auto path = root_path / std::string(target_file->get_name());
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 5);
                    auto data = read_file(path);
                    CHECK(data == "12345");
                }

                SECTION("multi block target file") {
                    pr_source.set_size(5ul);
                    pr_target.set_size(10ul);

                    auto diffs = diff::aggregate_t::diffs_t{};
                    diffs.push_back(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_source, {bi1}));
                    diffs.push_back(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_target, {bi1, bi1}));

                    auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
                    sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                    sup->do_process();

                    auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                    auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                    auto source_file = files_info.by_name(pr_source.name());
                    auto target_file = files_info.by_name(pr_target.name());

                    auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 0, "12345"));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    auto block = source_file->get_blocks()[0];
                    bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *block));
                    sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                    sup->do_process();

                    auto filename = std::string(target_file->get_name()) + ".syncspirit-tmp";
                    auto path = root_path / filename;
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 10);
                    auto data = read_file(path);
                    CHECK(data.substr(0, 5) == "12345");
                }
            }

            SECTION("source & target are is the same fiel") {
                pr_source.set_size(10ul);

                auto diffs = diff::aggregate_t::diffs_t{};
                diffs.push_back(new diff::modify::new_file_t(*cluster, db_folder.id(), pr_source, {bi1, bi1}));

                auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
                sup->send<model::payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
                sup->do_process();

                auto& folders_info  = cluster->get_folders().by_id(db_folder.id())->get_folder_infos();
                auto& files_info = folders_info.by_device(cluster->get_device())->get_file_infos();
                auto source_file = files_info.by_name(pr_source.name());
                auto target_file = source_file;

                auto bdiff = diff::block_diff_ptr_t(new diff::modify::append_block_t(*source_file, 0, "12345"));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                auto block = source_file->get_blocks()[0];
                bdiff = diff::block_diff_ptr_t(new diff::modify::clone_block_t(*target_file, *block));
                sup->send<model::payload::block_update_t>(sup->get_address(), std::move(bdiff), nullptr);
                sup->do_process();

                auto path = root_path / std::string(target_file->get_name());
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 10);
                auto data = read_file(path);
                CHECK(data == "1234512345");
            }
        }
    };
    F().run();
}



REGISTER_TEST_CASE(test_new_files, "test_new_files", "[fs]");
REGISTER_TEST_CASE(test_append_block, "test_append_block", "[fs]");
REGISTER_TEST_CASE(test_clone_block, "test_clone_block", "[fs]");
