// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "access.h"
#include "test_supervisor.h"
#include "managed_hasher.h"
#include "diff-builder.h"

#include "model/cluster.h"
#include "hasher/hasher_proxy_actor.h"
#include "fs/fs_slave.h"
#include "net/local_keeper.h"
#include "net/names.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;
using namespace syncspirit::hasher;

struct fixture_t {
    using target_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    using builder_ptr_t = std::unique_ptr<diff_builder_t>;

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} {
        test::init_logging();
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        auto my_hash = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id =device_id_t::from_string(my_hash).value();
        my_device = device_t::create(my_id, "my-device").value();

        auto peer_hash = "VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ";
        auto peer_id = device_id_t::from_string(peer_hash).value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster = new cluster_t(my_device, 1);

        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().make_presentation(true).timeout(timeout).create_registry().finish();
        sup->cluster = cluster;

        auto folder_id = "1234-5678";

        sup->start();
        sup->do_process();
        builder = std::make_unique<diff_builder_t>(*cluster);
        builder->upsert_folder(folder_id, root_path)
            .apply(*sup)
            .share_folder(peer_id.get_sha256(), folder_id)
            .apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);
        folder_info = folder->get_folder_infos().by_device(*my_device);
        files = &folder_info->get_file_infos();
        folder_info_peer = folder->get_folder_infos().by_device(*peer_device);
        files_peer = &folder_info_peer->get_file_infos();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        hasher = sup->create_actor<managed_hasher_t>().index(1).auto_reply(true).timeout(timeout).finish().get();
        auto proxy_addr = sup->create_actor<hasher::hasher_proxy_actor_t>()
                              .timeout(timeout)
                              .hasher_threads(1)
                              .name(net::names::hasher_proxy)
                              .finish()
                              ->get_address();

        sup->do_process();

        auto fs_config = config::fs_config_t{3600, 10, 1024 * 1024, files_scan_iteration_limit};
        rw_cache.reset(new fs::file_cache_t(5));

        target = sup->create_actor<net::local_keeper_t>()
                     .timeout(timeout)
                     .cluster(cluster)
                     .finish();
        sup->do_process();

        sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster, std::this_thread::get_id());
        sup->do_process();

        main();

        sup->do_process();
        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    std::int64_t files_scan_iteration_limit = 100;
    builder_ptr_t builder;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::intrusive_ptr_t<supervisor_t> sup;
    managed_hasher_t *hasher;
    cluster_ptr_t cluster;
    device_ptr_t my_device;
    bfs::path root_path;
    test::path_guard_t path_guard;
    target_ptr_t target;
    model::folder_ptr_t folder;
    model::folder_info_ptr_t folder_info;
    model::folder_info_ptr_t folder_info_peer;
    model::file_infos_map_t *files;
    model::file_infos_map_t *files_peer;
    model::device_ptr_t peer_device;
    fs::file_cache_ptr_t rw_cache;
};



void test_local_keeper() {
    struct F : fixture_t {
        void main() noexcept override {
            sys::error_code ec;
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_local_keeper, "test_local_keeper", "[net]");
    return 1;
}

static int v = _init();
