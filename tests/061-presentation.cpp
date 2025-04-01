// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "diff-builder.h"
#include "model/cluster.h"
#include "presentation/folder_presence.h"
#include "presentation/folder_entity.h"
#include "syncspirit-config.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;
using namespace syncspirit::proto;
using namespace syncspirit::model;
using namespace syncspirit::presentation;

TEST_CASE("presentation", "[presentation]") {
    test::init_logging();
    auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
    auto peer_2_id =
        model::device_id_t::from_string("LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW").value();
    auto my_device = device_t::create(my_id, "my-device").value();
    auto peer_device = device_t::create(peer_id, "peer-device").value();
    auto peer_2_device = device_t::create(peer_2_id, "peer-device-2").value();

    auto cluster = cluster_ptr_t(new cluster_t(my_device, 1));
    auto sequencer = make_sequencer(4);
    cluster->get_devices().put(my_device);
    cluster->get_devices().put(peer_device);
    cluster->get_devices().put(peer_2_device);

    auto builder = diff_builder_t(*cluster);

    SECTION("folder") {
        SECTION("single folder, shared with nobody") {
            REQUIRE(builder.upsert_folder("1234-5678", "some/path", "my-label").apply());
            auto folder = cluster->get_folders().by_id("1234-5678");
            CHECK(folder->use_count() == 2);

            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            CHECK(&folder_entity->get_folder() == folder.get());

            SECTION("check forwarding") {
                struct aug_sample_t : model::augmentation_t {
                    aug_sample_t(int &deleted_, int &updated_) : deleted{deleted_}, updated{updated_} {}
                    void on_update() noexcept { ++updated; };
                    void on_delete() noexcept { ++deleted; };

                    int &deleted;
                    int &updated;
                };

                int deleted = 0;
                int updated = 0;
                auto aug = model::augmentation_ptr_t(new aug_sample_t(deleted, updated));
                folder_entity->set_augmentation(aug);
                REQUIRE(folder->use_count() == 2);

                folder->notify_update();
                CHECK(updated == 1);
                CHECK(deleted == 0);

                cluster->get_folders().clear();
                REQUIRE(folder->use_count() == 1);
                REQUIRE(folder_entity->use_count() == 2);

                folder_entity.reset();
                CHECK(updated == 1);
                CHECK(deleted == 0);

                folder.reset();
                CHECK(updated == 1);
                CHECK(deleted == 1);
            }

            SECTION("presence") {
                CHECK(!folder_entity->get_presense(*peer_device));
                CHECK(!folder_entity->get_presense(*peer_2_device));

                auto self_presense = folder_entity->get_presense(*my_device);
                CHECK(self_presense);
                auto my_fi = folder->get_folder_infos().by_device(*my_device);
                CHECK(&self_presense->get_folder_info() == my_fi);
            }
        }
    }
}
