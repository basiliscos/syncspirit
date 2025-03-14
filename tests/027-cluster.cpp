// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include <catch2/catch_all.hpp>
#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::test;

using Catch::Matchers::Matches;

TEST_CASE("cluster", "[model]") {

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id_1 =
        device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_id_2 =
        device_id_t::from_string("EAMTZPW-Q4QYERN-D57DHFS-AUP2OMG-PAHOR3R-ZWLKGAA-WQC5SVW-UJ5NXQA").value();

    auto peer_device_1 = device_t::create(peer_id_1, "peer-device-1").value();
    auto peer_device_2 = device_t::create(peer_id_2, "peer-device-2").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    auto &devices = cluster->get_devices();
    devices.put(my_device);
    devices.put(peer_device_1);
    devices.put(peer_device_2);

    auto builder = diff_builder_t(*cluster);
    auto folder_1_id = "1234";

    auto sha256_1 = peer_id_1.get_sha256();
    auto sha256_2 = peer_id_2.get_sha256();

    auto r = builder.upsert_folder(folder_1_id, "/my/path-1")
                 .then()
                 .share_folder(sha256_1, folder_1_id, {})
                 .share_folder(sha256_2, folder_1_id, {})
                 .apply();
    REQUIRE(r);

    SECTION("cluster config for d1") {
        auto cc = cluster->generate(*peer_device_1);
        REQUIRE(proto::get_folders_size(cc) == 1);
        auto &f = proto::get_folders(cc, 0);

        CHECK(proto::get_id(f) == folder_1_id);
        REQUIRE(proto::get_devices_size(f) == 3);

        auto dS = (const proto::Device *){nullptr};
        auto d1 = (const proto::Device *){nullptr};
        auto d2 = (const proto::Device *){nullptr};

        for (size_t i = 0; i < 3; ++i) {
            auto &d = proto::get_devices(f, i);
            if (proto::get_id(d) == sha256_1) {
                d1 = &d;
            } else if (proto::get_id(d) == sha256_2) {
                d2 = &d;
            } else if (proto::get_id(d) == my_id.get_sha256()) {
                dS = &d;
            }
        }

        REQUIRE(d1);
        REQUIRE(d2);
        REQUIRE(dS);

        CHECK(proto::get_id(*d1) == sha256_1);
        CHECK(proto::get_id(*d2) == sha256_2);
    }
}
