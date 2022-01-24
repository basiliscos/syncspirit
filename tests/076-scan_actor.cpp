#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"

#include "model/cluster.h"
#include "model/diff/aggregate.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/clone_file.h"
#include "hasher/hasher_proxy_actor.h"
#include "hasher/hasher_actor.h"
#include "fs/scan_actor.h"
#include "net/names.h"
#include "utils/error_code.h"


using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::hasher;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<fs::scan_actor_t>;
    using error_msg_t = model::message::io_error_t;
    using error_msg_ptr_t = r::intrusive_ptr_t<error_msg_t>;
    using errors_container_t = std::vector<error_msg_ptr_t>;

    fixture_t() noexcept: root_path{ bfs::unique_path() }, path_quard{root_path} {
        utils::set_default("trace");
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device =  device_t::create(my_id, "my-device").value();
        auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device =  device_t::create(peer_id, "peer-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-f1");
        db_folder.set_path(root_path.string());

        auto diffs = diff::aggregate_t::diffs_t{};
        diffs.push_back(new diff::modify::create_folder_t(db_folder));
        diffs.push_back(new diff::modify::share_folder_t(peer_id.get_sha256(), db_folder.id(), 123ul));
        auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
        REQUIRE(diff->apply(*cluster));

        folder = cluster->get_folders().by_id(db_folder.id());
        folder_info = folder->get_folder_infos().by_device(my_device);
        files = &folder_info->get_file_infos();
        folder_info_peer = folder->get_folder_infos().by_device(peer_device);
        files_peer = &folder_info_peer->get_file_infos();

        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin){
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<error_msg_t>(
                    [&](error_msg_t &msg) { errors.push_back(&msg); }));
        });};

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t*>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        sup->create_actor<hasher_actor_t>().index(1).timeout(timeout).finish();
        auto proxy_addr = sup->create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(timeout)
            .hasher_threads(1)
            .name(net::names::hasher_proxy)
            .finish()
            ->get_address();

        sup->do_process();

        auto fs_config = config::fs_config_t {
            0,
            3600
        };

        target = sup->create_actor<fs::scan_actor_t>()
                .timeout(timeout)
                .cluster(cluster)
                .hasher_proxy(proxy_addr)
                .fs_config(fs_config)
                .requested_hashes_limit(2ul)
                .finish();

        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t*>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {
    }

    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    bfs::path root_path;
    path_guard_t path_quard;
    target_ptr_t target;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_info;
    model::folder_info_ptr_t folder_info_peer;
    model::file_infos_map_t* files;
    model::file_infos_map_t* files_peer;
    errors_container_t errors;
    model::device_ptr_t peer_device;
};

void test_meta_changes() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
            SECTION("no files"){
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
            }
            SECTION("just 1 dir"){
                CHECK(bfs::create_directories(root_path / "abc"));
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
            }
            SECTION("just 1 subdir, which cannot be read"){
                CHECK(bfs::create_directories(root_path / "abc" / "def", ec));
                bfs::permissions(root_path / "abc", bfs::perms::no_perms);
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
                bfs::permissions(root_path / "abc", bfs::perms::all_all);
                REQUIRE(errors.size() == 1);
                auto& errs = errors.at(0)->payload.errors;
                REQUIRE(errs.size() == 1);
                REQUIRE(errs.at(0).path == (root_path / "abc"));
                REQUIRE(errs.at(0).ec);
            }

            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            pr_fi.set_name("q.txt");
            pr_fi.set_modified_s(modified);
            pr_fi.set_block_size(5ul);
            pr_fi.set_size(5ul);

            auto version = pr_fi.mutable_version();
            auto counter = version->add_counters();
            counter->set_id(1);
            counter->set_value(peer_device->as_uint());

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);

            auto b = block_info_t::create(bi).value();

            SECTION("a file does not physically exists"){
                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                files_peer->put(file_peer);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());

                sup->do_process();
                CHECK(files->size() == 1);
                CHECK(!file->is_locally_available());
            }

            SECTION("complete file exists") {
                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                files_peer->put(file_peer);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());
                file->assign_block(b, 0);
                file->set_source(nullptr);
                auto path = file->get_path();

                SECTION("meta is not changed") {
                    write_file(path, "12345");
                    bfs::last_write_time(path, modified);
                    sup->do_process();
                    CHECK(files->size() == 1);
                    CHECK(file->is_locally_available());
                }
                SECTION("meta is changed (modification)") {
                    write_file(path, "12345");
                    sup->do_process();
                    CHECK(files->size() == 1);
                    CHECK(!file->is_locally_available());
                }
                SECTION("meta is changed (size)") {
                    write_file(path, "123456");
                    bfs::last_write_time(path, modified);
                    sup->do_process();
                    CHECK(files->size() == 1);
                    CHECK(!file->is_locally_available());
                }
            }

            SECTION("incomplete file exists") {
                pr_fi.set_size(10ul);
                pr_fi.set_block_size(5ul);

                auto bi_2 = proto::BlockInfo();
                bi_2.set_size(5);
                bi_2.set_weak_hash(12);
                bi_2.set_hash(utils::sha256_digest("67890").value());
                bi_2.set_offset(5);
                auto b2 = block_info_t::create(bi_2).value();

                auto file_peer = file_info_t::create(cluster->next_uuid(), pr_fi, folder_info_peer).value();
                file_peer->assign_block(b, 0);
                file_peer->assign_block(b2, 1);
                files_peer->put(file_peer);

                auto diff = diff::cluster_diff_ptr_t(new diff::modify::clone_file_t(*file_peer));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());
                auto path = file->get_path().string() + ".syncspirit-tmp";
                auto content = "12345\0\0\0\0\0";
                write_file(path, std::string(content, 10));

                SECTION("outdated -> just remove") {
                    bfs::last_write_time(path, modified - 24 * 3600);
                    sup->do_process();
                    CHECK(!file->is_locally_available());
                    CHECK(!bfs::exists(path));
                }

                SECTION("just 1st block is valid, tmp is kept") {
                    sup->do_process();
                    CHECK(!file->is_locally_available());
                    CHECK(!file->is_locally_available(0));
                    CHECK(!file->is_locally_available(1));
                    CHECK(!file_peer->is_locally_available());
                    CHECK(file_peer->is_locally_available(0));
                    CHECK(!file_peer->is_locally_available(1));
                    CHECK(bfs::exists(path));
                }

                SECTION("corrupted content") {
                    SECTION("1st block") {
                        write_file(path, "2234567890");
                    }
                    SECTION("2nd block") {
                        write_file(path, "1234567899");
                    }
                    SECTION("missing source file") {
                        file->set_source(nullptr);
                    }
                    sup->do_process();
                    CHECK(!file->is_locally_available(0));
                    CHECK(!file->is_locally_available(1));
                    CHECK(!file_peer->is_locally_available(0));
                    CHECK(!file_peer->is_locally_available(1));
                    CHECK(!bfs::exists(path));
                }

                SECTION("error on reading -> remove") {
                    bfs::permissions(path, bfs::perms::no_perms);
                    sup->do_process();
                    CHECK(!file->is_locally_available());
                    CHECK(!bfs::exists(path));

                    REQUIRE(errors.size() == 1);
                    auto& errs = errors.at(0)->payload.errors;
                    REQUIRE(errs.size() == 1);
                    CHECK(errs.at(0).path == path);
                    CHECK(errs.at(0).ec);
                }

            }
        }
    };
    F().run();
}


REGISTER_TEST_CASE(test_meta_changes, "test_meta_changes", "[fs]");
