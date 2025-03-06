// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/misc/sequencer.h"

#include "diff-builder.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

TEST_CASE("folder update (Index)", "[model]") {
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = model::make_sequencer(5);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto index_id = uint64_t{0x1234};

    auto max_seq = int64_t(10);
    auto builder = diff_builder_t(*cluster);
    auto sha256 = peer_id.get_sha256();
    REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
    REQUIRE(builder.share_folder(sha256, "1234-5678").apply());
    REQUIRE(builder.configure_cluster(sha256).add(sha256, "1234-5678", index_id, max_seq).finish().apply());

    proto::FileInfo pr_fi;
    proto::set_name(pr_fi, "a.txt");
    proto::set_block_size(pr_fi, 5ul);
    proto::set_size(pr_fi, 6);
    proto::set_sequence(pr_fi, 5);

    auto b1_hash = utils::sha256_digest(as_bytes("12345")).value();
    auto &b1 = proto::add_blocks(pr_fi);
    proto::set_hash(b1, as_bytes("12345"));
    proto::set_size(b1, 5);

    auto ec = builder.make_index(sha256, "1234-5678").add(pr_fi, peer_device).fail();
    REQUIRE(ec);
    CHECK(ec == make_error_code(error_code_t::mismatch_file_size));
};
