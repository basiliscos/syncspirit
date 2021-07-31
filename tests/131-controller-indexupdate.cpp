#include "catch.hpp"
#include "fixture.h"
#include "ui/messages.hpp"
#include "utils/error_code.h"
#include "test-utils.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;

void test_indexupdate_new_dirs() {
    struct F : Fixture {
        model::folder_ptr_t folder;

        void setup() override {
            auto config = proto::ClusterConfig();
            auto p_folder = proto::Folder();
            p_folder.set_label("my-folder");
            p_folder.set_id("123");

            auto d_my = proto::Device();
            d_my.set_id(device_my->device_id.get_sha256());
            d_my.set_index_id(21);
            d_my.set_max_sequence(0);

            auto d_peer = proto::Device();
            d_my.set_id(device_peer->device_id.get_sha256());
            d_my.set_index_id(22);
            d_my.set_max_sequence(1);

            *p_folder.add_devices() = d_my;
            *p_folder.add_devices() = d_peer;
            *config.add_folders() = p_folder;
            peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(config));

            auto db_folder = db::Folder();
            db_folder.set_id(p_folder.id());
            db_folder.set_label(p_folder.label());
            db_folder.set_path(root_path.string());
            folder = new model::folder_t(db_folder);
            folder->assign_device(device_my);

            auto db_fi_my = db::FolderInfo();
            db_fi_my.set_index_id(23);
            db_fi_my.set_max_sequence(0);
            auto fi_my =
                model::folder_info_ptr_t(new model::folder_info_t(db_fi_my, device_my.get(), folder.get(), 24));
            folder->add(fi_my);

            auto db_fi_peer = db::FolderInfo();
            db_fi_peer.set_index_id(25);
            db_fi_peer.set_max_sequence(1);
            auto fi_peer =
                model::folder_info_ptr_t(new model::folder_info_t(db_fi_peer, device_peer.get(), folder.get(), 26));
            folder->add(fi_peer);
            cluster->add_folder(folder);
        }

        void main() override {
            CHECK(peer->start_reading == 1);

            proto::IndexUpdate iu;
            iu.set_folder(folder->id());

            auto fi1 = proto::FileInfo();
            fi1.set_name("some_dir");
            fi1.set_type(proto::FileInfoType::DIRECTORY);
            fi1.set_sequence(3);

            *iu.add_files() = fi1;

            auto iu_ptr = proto::message::IndexUpdate(std::make_unique<proto::IndexUpdate>(std::move(iu)));
            peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(iu_ptr));
            sup->do_process();

            auto path = bfs::path(root_path / "some_dir");
            CHECK(bfs::exists(path));
            CHECK(bfs::is_directory(path));
        }
    };
    F().run();
}

REGISTER_TEST_CASE(test_indexupdate_new_dirs, "test_indexupdate_new_dirs", "[controller]");
