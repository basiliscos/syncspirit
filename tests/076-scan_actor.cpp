#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"

#include "model/cluster.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/new_file.h"
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

    fixture_t() noexcept: root_path{ bfs::unique_path() }, path_quard{root_path} {
        utils::set_default("trace");
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        my_device =  device_t::create(my_id, "my-device").value();
        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        db::Folder db_folder;
        db_folder.set_id("1234-5678");
        db_folder.set_label("my-f1");
        db_folder.set_path(root_path.string());

        auto diff = diff::cluster_diff_ptr_t();
        diff = new diff::modify::create_folder_t(db_folder);
        REQUIRE(diff->apply(*cluster));

        folder = cluster->get_folders().by_id(db_folder.id());
        folder_info = folder->get_folder_infos().by_device(my_device);
        files = &folder_info->get_file_infos();

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
    model::file_infos_map_t* files;
};

void test_meta_changes() {
    struct F : fixture_t {
        void main() noexcept override {

            SECTION("no files"){
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
            }
            SECTION("just 1 dir"){
                bfs::create_directories(root_path / "abc");
                sup->do_process();
                CHECK(folder_info->get_file_infos().size() == 0);
            }

            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            pr_fi.set_name("q.txt");
            pr_fi.set_modified_s(modified);
            pr_fi.set_block_size(5ul);
            pr_fi.set_size(5ul);

            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_weak_hash(12);
            bi.set_hash(utils::sha256_digest("12345").value());
            bi.set_offset(0);

            SECTION("a file does not physically exists"){
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, folder->get_id(), pr_fi, {bi}));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());

                sup->do_process();
                CHECK(files->size() == 1);
                CHECK(!file->is_locally_available());
            }

            SECTION("complete file exists"){
                auto diff = diff::cluster_diff_ptr_t(new diff::modify::new_file_t(*cluster, folder->get_id(), pr_fi, {bi}));
                REQUIRE(diff->apply(*cluster));
                auto file = files->by_name(pr_fi.name());
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

        }
    };
    F().run();
}


REGISTER_TEST_CASE(test_meta_changes, "test_meta_changes", "[fs]");
