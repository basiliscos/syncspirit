// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/misc/file_iterator.h"

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::proto;
using namespace syncspirit::test;

using A = advance_action_t;

TEST_CASE("folder upsert", "[model]") {
    using names_t = std::vector<std::string>;
    struct file_meta_t {
        std::string_view name;
        std::int64_t size;
        std::int64_t modified;
    };
    file_meta_t file_metas[5] = {
        {"0.txt", 0, 123}, {"1/a.txt", 5, 5000}, {"1/c.txt", 15, 4000}, {"1/d.txt", 10, 4500}, {"1/e.txt", 20, 6000},
    };

    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();

    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);

    auto builder = diff_builder_t(*cluster);

    auto &folders = cluster->get_folders();
    REQUIRE(builder.upsert_folder("1234-5678", "/my/path").apply());
    REQUIRE(builder.share_folder(peer_id.get_sha256(), "1234-5678").apply());
    auto folder = folders.by_id("1234-5678");
    auto folder_infos = &folder->get_folder_infos();
    REQUIRE(folder_infos->size() == 2u);

    auto index = builder.make_index(peer_id.get_sha256(), folder->get_id());
    std::int64_t sequence = 10;
    for (auto &meta : file_metas) {
        auto pr = proto::FileInfo();
        proto::set_name(pr, meta.name);
        proto::set_size(pr, meta.size);
        proto::set_modified_s(pr, meta.modified);

        if (meta.size) {
            auto bytes_view = utils::bytes_view_t((unsigned char *)&file_metas, meta.size);
            auto hash = utils::sha256_digest(bytes_view).value();
            auto block = proto::BlockInfo();
            proto::set_hash(block, hash);
            proto::set_size(block, meta.size);
            proto::add_blocks(pr, std::move(block));
        }

        proto::set_sequence(pr, sequence++);
        index.add(pr, peer_device);
    }
    REQUIRE(index.finish().apply());

    auto file_iterator = peer_device->create_iterator(*cluster);
    auto names = names_t();
    auto next = [&]() {
        auto [fi, action] = file_iterator->next();
        REQUIRE(fi);
        CHECK(action == A::remote_copy);
        names.emplace_back(std::string(fi->get_name()->get_full_name()));
    };

    next();
    next();
    auto expected_1 = names_t{"0.txt", "1/a.txt"};
    CHECK(names == expected_1);

    auto db_folder = db::Folder();
    folder->serialize(db_folder);
    db::set_pull_order(db_folder, db::PullOrder::largest);
    REQUIRE(builder.upsert_folder(db_folder).apply());
    REQUIRE(cluster->get_folders().size() == 1);
    folder = cluster->get_folders().by_id(folder->get_id());

    REQUIRE(folder->is_shared_with(*peer_device));

    next();
    next();
    next();

    auto expected_2 = names_t{"0.txt", "1/a.txt", "1/e.txt", "1/c.txt", "1/d.txt"};
    CHECK(names == expected_2);
}
