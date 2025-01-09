// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/cluster.h"
#include "net/controller_actor.h"
#include "fs/file_actor.h"
#include "sample_peer.h"
#include "test_supervisor.h"

namespace syncspirit::test {

namespace bfs = std::filesystem;
namespace payload = syncspirit::net::payload;

struct Fixture {
    model::device_ptr_t device_my;
    model::device_ptr_t device_peer;
    model::cluster_ptr_t cluster;
    model::ignored_folders_map_t ignored_folders;
    r::intrusive_ptr_t<sample_peer_t> peer;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<net::controller_actor_t> controller;
    payload::cluster_config_ptr_t peer_cluster_config;
    bfs::path root_path;

    Fixture();
    virtual void setup();
    virtual void pre_run();
    virtual void main();
    void create_controller();

    void run();
    uint64_t seq = 1;
};

}; // namespace syncspirit::test
