// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "fs/file_actor.h"
#include "hasher/hasher_proxy_actor.h"
#include "hasher/hasher_actor.h"
#include "net/controller_actor.h"
#include "diff-builder.h"
#include "net/names.h"
#include "test_peer.h"
#include "test_supervisor.h"
#include "access.h"
#include "model/cluster.h"
#include "access.h"
#include <filesystem>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;

namespace bfs = std::filesystem;

namespace {

struct fixture_t {
    using controller_ptr_t = r::intrusive_ptr_t<net::controller_actor_t>;
    using peer_ptr_t = r::intrusive_ptr_t<test_peer_t>;

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} { bfs::create_directory(root_path); }

    virtual configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
                p.register_name(net::names::db, sup->get_address());
                // p.register_name(net::names::hasher_proxy, sup->get_address());
            });
        };
    }

    virtual void run() noexcept {
        auto my_id_str = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id = device_id_t::from_string(my_id_str).value();
        my_device = device_t::create(my_id, "my-device").value();

        auto peer_id_str = "VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ";
        auto peer_id = device_id_t::from_string(peer_id_str).value();
        peer_device = device_t::create(peer_id, "peer-device").value();

        cluster = new cluster_t(my_device, 10);
        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_device);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>()
                  .auto_finish(false)
                  .auto_ack_io(false)
                  .timeout(timeout)
                  .create_registry()
                  .make_presentation(true)
                  .finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        rw_cache.reset(new fs::file_cache_t(2));
        file_actor = sup->create_actor<fs::file_actor_t>().rw_cache(rw_cache).timeout(timeout).finish();
        sup->do_process();

        sup->create_actor<hasher::hasher_actor_t>().index(1).timeout(timeout).finish();
        sup->create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(timeout)
            .hasher_threads(1)
            .name(net::names::hasher_proxy)
            .finish();

        auto url = "relay://1.2.3.4:5";

        peer_actor = sup->create_actor<test_peer_t>()
                         .cluster(cluster)
                         .peer_device(peer_device)
                         .url(url)
                         .coordinator(sup->get_address())
                         .timeout(timeout)
                         .finish();
        sup->do_process();

        controller_actor = sup->create_actor<controller_actor_t>()
                               .peer(peer_device)
                               .peer_addr(peer_actor->get_address())
                               .request_pool(1024)
                               .outgoing_buffer_max(1024'000)
                               .cluster(cluster)
                               .sequencer(sup->sequencer)
                               .timeout(timeout)
                               .request_timeout(timeout)
                               .blocks_max_requested(100)
                               .finish();

        sequencer = sup->sequencer;

        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto builder = diff_builder_t(*cluster);
        builder.upsert_folder(folder_id, root_path, "my-label").apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);

        CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        CHECK(static_cast<r::actor_base_t *>(controller_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

        main();

        CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(controller_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);

        sup->do_shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    model::device_ptr_t my_device;
    device_ptr_t peer_device;
    model::folder_ptr_t folder;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<fs::file_actor_t> file_actor;
    peer_ptr_t peer_actor;
    controller_ptr_t controller_actor;
    bfs::path root_path;
    test::path_guard_t path_guard;
    r::system_context_t ctx;
    std::string_view folder_id = "1234-5678";
    fs::file_cache_ptr_t rw_cache;
};
} // namespace

void test_shutdown_initiated_by_controller() {
    struct F : fixture_t {
        void main() noexcept override {
            controller_actor->do_shutdown();
            sup->do_process();

            CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            file_actor->do_shutdown();
            sup->do_process();
            CHECK(cluster->get_write_requests() == 10);
        }
    };
    F().run();
}

void test_shutdown_initiated_by_file_actor() {
    struct F : fixture_t {
        void main() noexcept override {
            file_actor->do_shutdown();
            sup->do_process();
            CHECK(cluster->get_write_requests() == 10);
        }
    };
    F().run();
}

void test_fs_actor_error() {
    struct F : fixture_t {
        void main() noexcept override {
            auto file_name = std::string_view("some-file");
            auto pr_file = proto::FileInfo();
            proto::set_name(pr_file, file_name);
            proto::set_type(pr_file, proto::FileInfoType::FILE);
            proto::set_sequence(pr_file, 5);
            proto::set_size(pr_file, 5);
            proto::set_block_size(pr_file, 5);

            auto &v = proto::get_version(pr_file);
            auto &counter = proto::add_counters(v);
            proto::set_id(counter, 1);
            proto::set_value(counter, 1);

            auto data_1 = as_owned_bytes("12345");
            auto data_1_h = utils::sha256_digest(data_1).value();
            auto &b1 = proto::add_blocks(pr_file);
            proto::set_hash(b1, data_1_h);
            proto::set_size(b1, data_1.size());

            auto folder_id = "1234-5678";
            auto builder = diff_builder_t(*cluster);
            auto sha256 = peer_device->device_id().get_sha256();
            auto folder_path = bfs::absolute(root_path) / L"йцукен";

            builder.upsert_folder(folder_id, folder_path)
                .share_folder(sha256, folder_id)
                .apply(*sup)
                .configure_cluster(sha256)
                .add(sha256, folder_id, 0x123, 999)
                .finish()
                .apply(*sup, controller_actor.get());

            SECTION("fs error -> controller down") {
                peer_actor->push_response(data_1, 0);
                write_file(folder_path, ""); // prevent dir creation

                builder.make_index(sha256, folder_id)
                    .add(pr_file, peer_device)
                    .finish()
                    .apply(*sup, controller_actor.get());
            }
            SECTION("fs error -> controller down") {
                builder.make_index(sha256, folder_id)
                    .add(pr_file, peer_device)
                    .finish()
                    .apply(*sup, controller_actor.get());

                CHECK("just 4 logging");

                peer_actor->push_response(data_1, 0);
                controller_actor->do_shutdown();
                peer_actor->process_block_requests();
                sup->do_process();

                CHECK(static_cast<r::actor_base_t *>(peer_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
                CHECK(static_cast<r::actor_base_t *>(controller_actor.get())->access<to::state>() ==
                      r::state_t::SHUT_DOWN);
                sup->do_process();
            }

            CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
            file_actor->do_shutdown();
            sup->do_process();
            CHECK(cluster->get_write_requests() == 10);
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_shutdown_initiated_by_controller, "test_shutdown_initiated_by_controller",
                       "[fs][controller]");
    REGISTER_TEST_CASE(test_shutdown_initiated_by_file_actor, "test_shutdown_initiated_by_file_actor",
                       "[fs][controller]");
    REGISTER_TEST_CASE(test_fs_actor_error, "test_fs_actor_error", "[fs][controller]");
    return 1;
}

static int v = _init();
