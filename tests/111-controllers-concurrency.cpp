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

    fixture_t() noexcept = default;

    virtual supervisor_t::configure_callback_t configure() noexcept {
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

        cluster = new cluster_t(my_device, 1);
        cluster->get_devices().put(my_device);
        cluster->get_devices().put(peer_devices[0]);
        cluster->get_devices().put(peer_devices[1]);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>()
                  .auto_finish(false)
                  .auto_ack_blocks(false)
                  .timeout(timeout)
                  .create_registry()
                  .make_presentation(true)
                  .finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        sup->create_actor<hasher::hasher_actor_t>().index(1).timeout(timeout).finish();
        sup->create_actor<hasher::hasher_proxy_actor_t>()
            .timeout(timeout)
            .hasher_threads(1)
            .name(net::names::hasher_proxy)
            .finish();

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
                                   .request_timeout(timeout)
                                   .blocks_max_requested(100)
                                   .finish();

        controller_actors[1] = sup->create_actor<controller_actor_t>()
                                   .peer(peer_devices[1])
                                   .peer_addr(peer_actors[1]->get_address())
                                   .request_pool(1024)
                                   .outgoing_buffer_max(1024'000)
                                   .cluster(cluster)
                                   .sequencer(sup->sequencer)
                                   .timeout(timeout)
                                   .request_timeout(timeout)
                                   .blocks_max_requested(100)
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
};
} // namespace

void test_concurrent_downloading() {
    struct F : fixture_t {
        void main() noexcept override {}
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_concurrent_downloading, "test_concurrent_downloading", "[controller]");
    return 1;
}

static int v = _init();
