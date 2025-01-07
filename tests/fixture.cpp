// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "fixture.h"
#include "test-utils.h"
#include "sample_db.h"
#include "net/names.h"
#include "access.h"
#include "hasher/hasher_actor.h"
#include "hasher/hasher_proxy_actor.h"
#include "catch.hpp"

using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

Fixture::Fixture() { test::init_logging() }

void Fixture::run() {
    root_path = bfs::unique_path();
    bfs::create_directory(root_path);
    auto root_path_guard = path_guard_t(root_path);

    std::uint64_t key = 0;
    db::Device db_my;
    db_my.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
    db_my.set_name("d1");
    db_my.set_cert_name("d1_cert_name");
    device_my = model::device_ptr_t(new model::device_t(db_my, ++key));

    db::Device db_peer;
    db_peer.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
    db_peer.set_name("d2");
    db_peer.set_cert_name("d2_cert_name");
    device_peer = model::device_ptr_t(new model::device_t(db_peer, ++key));

    cluster = new cluster_t(device_my);
    setup();

    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();
    sup->create_actor<fs::file_actor_t>().timeout(timeout).finish();
    sup->create_actor<hasher::hasher_actor_t>().index(1).timeout(timeout).finish();
    sup->create_actor<hasher::hasher_proxy_actor_t>()
        .hasher_threads(1)
        .name(names::hasher_proxy)
        .timeout(timeout)
        .finish();
    sup->create_actor<sample_db_t>().timeout(timeout).finish();
    peer = sup->create_actor<sample_peer_t>().timeout(timeout).finish();

    pre_run();
    sup->do_process();

    create_controller();

    main();

    sup->shutdown();
    sup->do_process();
    CHECK(controller->access<to::state>() == r::state_t::SHUT_DOWN);
    CHECK(peer->access<to::state>() == r::state_t::SHUT_DOWN);
}

void Fixture::create_controller() {
    auto bep_config = config::bep_config_t();
    bep_config.rx_buff_size = 1024;
    auto timeout = r::pt::milliseconds{10};
    controller = sup->create_actor<controller_actor_t>()
                     .cluster(cluster)
                     .device(device_my)
                     .peer_addr(peer->get_address())
                     .peer(device_peer)
                     .request_timeout(timeout)
                     .peer_cluster_config(std::move(peer_cluster_config))
                     .ignored_folders(&ignored_folders)
                     .bep_config(bep_config)
                     .timeout(timeout)
                     .finish();
    sup->do_process();
    auto reason = controller->get_shutdown_reason();
    if (reason) {
        spdlog::warn("shutdown reason = {}", reason->message());
    };
    REQUIRE(!reason);
}

void Fixture::setup() {}

void Fixture::pre_run() {}
void Fixture::main() {}
