// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
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

    fixture_t() noexcept { log = utils::get_logger("fixture"); }

    virtual configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::fs_actor, sup->get_address()); });
        };
    }

    virtual void run() noexcept {
        auto my_id_str = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id = device_id_t::from_string(my_id_str).value();
        my_device = device_t::create(my_id, "my-device").value();

        auto peer_1_id_str = "VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ";
        auto peer_1_id = device_id_t::from_string(peer_1_id_str).value();
        peer_devices[0] = device_t::create(peer_1_id, "peer-device-1").value();

        auto peer_2_id_str = "Y2Q7FEX-3UKIFDM-GRUYLKR-YBP4DHJ-2YMXCFT-OKBMEPQ-PJFL65J-ITTFOAX";
        auto peer_2_id = device_id_t::from_string(peer_2_id_str).value();
        peer_devices[1] = device_t::create(peer_2_id, "peer-device-2").value();

        cluster = new cluster_t(my_device, 2);
        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_devices[0]);
        cluster->get_devices().put(peer_devices[1]);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>()
                  .auto_finish(true)
                  .auto_ack_io(true)
                  .timeout(timeout)
                  .create_registry()
                  .make_presentation(true)
                  .finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        sup->create_actor<hasher::hasher_actor_t>().index(1).timeout(timeout).finish();

        sup->configure_callback = [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::fs_actor, sup->get_address()); });
        };

        peer_actors[0] = sup->create_actor<test_peer_t>()
                             .cluster(cluster)
                             .peer_device(peer_devices[0])
                             .url("relay://1.2.3.4:5")
                             .coordinator(sup->get_address())
                             .timeout(timeout)
                             .finish();
        peer_actors[1] = sup->create_actor<test_peer_t>()
                             .cluster(cluster)
                             .peer_device(peer_devices[1])
                             .url("relay://1.2.3.4:6")
                             .coordinator(sup->get_address())
                             .timeout(timeout)
                             .finish();

        sup->do_process();

        controller_actors[0] = sup->create_actor<controller_actor_t>()
                                   .peer(peer_devices[0])
                                   .peer_addr(peer_actors[0]->get_address())
                                   .request_pool(1024)
                                   .outgoing_buffer_max(1024'000)
                                   .cluster(cluster)
                                   .sequencer(sup->sequencer)
                                   .timeout(timeout)
                                   .hasher_threads(1)
                                   .blocks_max_requested(1)
                                   .finish();

        controller_actors[1] = sup->create_actor<controller_actor_t>()
                                   .peer(peer_devices[1])
                                   .peer_addr(peer_actors[1]->get_address())
                                   .request_pool(1024)
                                   .outgoing_buffer_max(1024'000)
                                   .cluster(cluster)
                                   .sequencer(sup->sequencer)
                                   .timeout(timeout)
                                   .hasher_threads(1)
                                   .blocks_max_requested(1)
                                   .finish();

        sup->do_process();

        sequencer = sup->sequencer;

        auto builder = diff_builder_t(*cluster);
        builder.upsert_folder(folder_id, {}, "my-label").apply(*sup);

        folder = cluster->get_folders().by_id(folder_id);

        CHECK(static_cast<r::actor_base_t *>(peer_actors[0].get())->access<to::state>() == r::state_t::OPERATIONAL);
        CHECK(static_cast<r::actor_base_t *>(peer_actors[1].get())->access<to::state>() == r::state_t::OPERATIONAL);
        CHECK(static_cast<r::actor_base_t *>(controller_actors[0].get())->access<to::state>() ==
              r::state_t::OPERATIONAL);
        CHECK(static_cast<r::actor_base_t *>(controller_actors[1].get())->access<to::state>() ==
              r::state_t::OPERATIONAL);

        main();

        sup->do_shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(peer_actors[0].get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(peer_actors[1].get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(controller_actors[0].get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(controller_actors[1].get())->access<to::state>() == r::state_t::SHUT_DOWN);

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    model::device_ptr_t my_device;
    device_ptr_t peer_devices[2];
    model::folder_ptr_t folder;
    r::intrusive_ptr_t<supervisor_t> sup;
    peer_ptr_t peer_actors[2];
    controller_ptr_t controller_actors[2];
    test::path_guard_t path_guard;
    r::system_context_t ctx;
    std::string_view folder_id = "1234-5678";
    fs::file_cache_ptr_t rw_cache;
    utils::logger_t log;
};
} // namespace

void test_concurrent_up_n_down() {
    struct F : fixture_t {
        void main() noexcept override {}
    };
    F().run();
}

void test_concurrent_downloading() {
    struct F : fixture_t {
        using blocks_map_t = std::unordered_map<utils::bytes_t, utils::bytes_t>;
        using requested_blocks_sz_t = std::unordered_map<r::actor_base_t *, size_t>;

        void main() noexcept override {
            static constexpr size_t N = 10;
            auto builder = diff_builder_t(*cluster);
            auto sha256_local = my_device->device_id().get_sha256();
            auto sha256_1 = peer_devices[0]->device_id().get_sha256();
            auto sha256_2 = peer_devices[1]->device_id().get_sha256();

            builder.share_folder(sha256_1, folder->get_id()).share_folder(sha256_2, folder->get_id()).apply(*sup);

            auto index_1 = uint64_t{101};
            auto index_2 = uint64_t{102};
            auto local_folder = folder->get_folder_infos().by_device(*my_device);

            builder.configure_cluster(sha256_1)
                .add(sha256_1, folder_id, index_1, 10)
                .add(sha256_local, folder_id, local_folder->get_index(), local_folder->get_max_sequence())
                .finish()
                .apply(*sup, controller_actors[0].get());

            builder.configure_cluster(sha256_2)
                .add(sha256_2, folder_id, index_2, 10)
                .add(sha256_local, folder_id, local_folder->get_index(), local_folder->get_max_sequence())
                .finish()
                .apply(*sup, controller_actors[1].get());

            for (size_t idx = 0; idx < 2; ++idx) {
                auto peer = peer_devices[idx];
                auto index = builder.make_index(peer->device_id().get_sha256(), folder_id);

                for (size_t i = 0; i < N; ++i) {
                    char data[5] = {static_cast<char>('0' + i)};
                    auto bytes_view = std::string(data, 5);
                    auto bytes = test::as_owned_bytes(bytes_view);
                    auto data_h = utils::sha256_digest(bytes).value();
                    auto block = proto::BlockInfo();
                    proto::set_hash(block, data_h);
                    proto::set_size(block, bytes.size());

                    blocks_map[data_h] = bytes;

                    auto file = proto::FileInfo();
                    auto file_name = fmt::format("file-{}.bin", i);
                    proto::set_name(file, file_name);
                    proto::set_type(file, proto::FileInfoType::FILE);
                    proto::set_sequence(file, i + 1);
                    proto::set_size(file, 5);
                    proto::set_block_size(file, 5);
                    proto::add_blocks(file, block);

                    auto version = proto::Vector();
                    auto &c = proto::add_counters(version);
                    proto::set_id(c, 5);
                    proto::set_value(c, i);
                    proto::set_version(file, version);

                    index.add(file, peer, false);
                }
                index.finish().apply(*sup);
            }

            auto pushed_blocks = size_t{0};
            // REQUIRE(requested.size() == 2);
            while (pushed_blocks < N) {
                // for (auto &[peer_actor, queue] : requested) {
                for (auto &actor : peer_actors) {
                    if (actor->in_requests.size()) {
                        auto &p = actor->in_requests.front();
                        auto hash = utils::bytes_t(proto::get_hash(p));
                        auto request_id = proto::get_id(p);
                        auto &bytes = blocks_map.at(hash);
                        actor->push_response(bytes, request_id);
                        actor->process_block_requests();
                        ++pushed_blocks;
                        ++requested_blocks_sz[actor.get()];
                    }
                }
                sup->do_process();
            }
            CHECK(peer_actors[0]->in_requests.empty());
            CHECK(peer_actors[1]->in_requests.empty());
            CHECK(requested_blocks_sz[peer_actors[0].get()] == N / 2);
            CHECK(requested_blocks_sz[peer_actors[1].get()] == N / 2);
            CHECK(local_folder->get_file_infos().size() == N);

            int index_updates[2] = {0, 0};
            for (size_t idx = 0; idx < 2; ++idx) {
                for (auto &m : peer_actors[idx]->messages) {
                    if (auto u = std::get_if<proto::IndexUpdate>(&m->payload); u) {
                        ++index_updates[idx];
                        auto peer = peer_devices[idx];
                        auto &file = proto::get_files(*u, 0);
                        auto file_name = proto::get_name(file);
                        log->debug("{} got index update for '{}'", peer->device_id().get_short(), file_name);
                    } else if (auto u = std::get_if<proto::Index>(&m->payload); u) {
                        ++index_updates[idx];
                        auto peer = peer_devices[idx];
                        auto &file = proto::get_files(*u, 0);
                        auto file_name = proto::get_name(file);
                        log->debug("{} got index for '{}'", peer->device_id().get_short(), file_name);
                    }
                }
            }
            CHECK(index_updates[0] == N);
            CHECK(index_updates[1] == N);
        }

        blocks_map_t blocks_map;
        requested_blocks_sz_t requested_blocks_sz;
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_concurrent_up_n_down, "test_concurrent_up_n_down", "[controller]");
    REGISTER_TEST_CASE(test_concurrent_downloading, "test_concurrent_downloading", "[controller]");
    return 1;
}

static int v = _init();
