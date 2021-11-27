#include "catch.hpp"
#include "fixture.h"
#include "ui/messages.hpp"
#include "utils/error_code.h"
#include "test-utils.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;

void test_resume() {
    using notify_t = ui::message::new_folder_notify_t;
    using notify_ptr_t = r::intrusive_ptr_t<notify_t>;
    using cluster_msg_t = r::intrusive_ptr_t<message::cluster_config_t>;

    struct F : Fixture {
        notify_ptr_t notify;
        proto::Folder p_folder;
        proto::ClusterConfig p_cluster;
        cluster_msg_t cluster_msg;

        void setup() override {
            auto folder = proto::Folder();
            folder.set_label("my-folder");
            folder.set_id("123");

            auto d_my = proto::Device();
            d_my.set_id(device_my->device_id.get_sha256());
            d_my.set_index_id(21);
            d_my.set_max_sequence(0);

            auto d_peer = proto::Device();
            d_my.set_id(device_peer->device_id.get_sha256());
            d_my.set_index_id(22);
            d_my.set_max_sequence(1);

            *folder.add_devices() = d_my;
            *folder.add_devices() = d_peer;
            *p_cluster.add_folders() = folder;
            peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(p_cluster));
            p_folder = folder;
        }

        void pre_run() override {
            peer->configure_callback = [&](auto &plugin) {
                plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                    p.subscribe_actor(r::lambda<notify_t>([&](notify_t &msg) { notify = &msg; }), sup->get_address());
                    p.subscribe_actor(r::lambda<message::cluster_config_t>(
                        [&](message::cluster_config_t &msg) { cluster_msg = &msg; }));
                });
            };
        }
        void main() override {
            CHECK(peer->start_reading == 1);
            REQUIRE(notify);
            CHECK(notify->payload.folder.label() == "my-folder");
            CHECK(notify->payload.folder.id() == "123");
            CHECK(notify->payload.source == device_peer);
            CHECK(notify->payload.source_index == 22);

            auto dir = root_path / "my-folder";
            auto db_folder = db::Folder();
            db_folder.set_id(p_folder.id());
            db_folder.set_label(p_folder.label());
            db_folder.set_path(dir.string());

            auto folder = model::folder_ptr_t(new model::folder_t(db_folder, 1));
            cluster->add_folder(folder);
            folder->assign_device(device_my);
            folder->assign_cluster(cluster.get());

            auto db_fi_local = db::FolderInfo();
            db_fi_local.set_index_id(234);
            auto fi_local =
                model::folder_info_ptr_t(new model::folder_info_t(db_fi_local, device_my.get(), folder.get(), 2));

            auto db_fi_source = db::FolderInfo();
            db_fi_source.set_index_id(345);
            db_fi_source.set_max_sequence(5);
            auto fi_source =
                model::folder_info_ptr_t(new model::folder_info_t(db_fi_source, device_peer.get(), folder.get(), 3));

            folder->add(fi_local);
            folder->add(fi_source);

            peer->send<payload::store_new_folder_notify_t>(controller->get_address(), folder);
            sup->do_process();

            REQUIRE(cluster_msg);

            proto::Index index;
            index.set_folder(folder->id());

            auto bi1 = proto::BlockInfo();
            bi1.set_size(5);
            bi1.set_hash(utils::sha256_digest("12345").value());

            auto bi2 = proto::BlockInfo();
            bi2.set_size(5);
            bi2.set_hash(utils::sha256_digest("67890").value());

            auto fi = proto::FileInfo();
            fi.set_name("a.txt");
            fi.set_type(proto::FileInfoType::FILE);
            fi.set_sequence(5);
            fi.set_block_size(5);
            auto counters = fi.mutable_version()->add_counters();
            counters->set_id(1u);
            counters->set_value(1u);

            auto path = dir / "a.txt";
            fi.set_size(10);
            *fi.add_blocks() = bi1;
            *fi.add_blocks() = bi2;
            *index.add_files() = fi;

            auto index_ptr = proto::message::Index(std::make_unique<proto::Index>(index));
            peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(index_ptr));

            peer->push_response("12345");
            sup->do_process();

            peer->do_shutdown();
            sup->do_process();

            auto tmp_path = dir / "a.txt.syncspirit-tmp";
            CHECK(read_file(tmp_path).size() == 10);

            auto &file_infos = folder->get_folder_info(device_my)->get_file_infos();
            auto file = file_infos.by_id("my-folder/a.txt");
            REQUIRE(file);
            CHECK(!file->is_locked());
            CHECK(!file->is_dirty());
            CHECK(file->get_sequence() == fi.sequence());
            CHECK(file->is_incomplete());

            auto &blocks = file->get_blocks();
            CHECK(blocks[0]);
            CHECK(blocks[0]->get_hash() == bi1.hash());
            CHECK(!blocks[1]);

            peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(p_cluster));
            auto timeout = r::pt::milliseconds{10};
            peer = sup->create_actor<sample_peer_t>().timeout(timeout).finish();

            pre_run();
            CHECK(file->get_folder_info()->get_file_infos().size() == 1);

            SECTION("artificial start") {
                peer->push_response("67890", 1);
                create_controller();
                CHECK(read_file(path) == "1234567890");
            }

            SECTION("non-matching garbage will be overwritten (same file size)") {
                write_file(tmp_path, "0000000000");

                peer->push_response("12345");
                peer->push_response("67890", 1);
                db::BlockInfo db_b;
                db_b.set_hash(utils::sha256_digest("00000").value());
                db_b.set_size(5);
                auto b = model::block_info_ptr_t(new model::block_info_t(db_b, 99));
                cluster->get_blocks().put(b);
                file->remove_blocks();
                file->get_blocks().resize(2);
                file->assign_block(b, 0ul);
                file->assign_block(b, 1ul);

                create_controller();
                CHECK(read_file(path) == "1234567890");
            }

            SECTION("non-matching garbage will be overwritten (different file size)") {
                write_file(tmp_path, "000000000000000");

                peer->push_response("12345");
                peer->push_response("67890", 1);
                db::BlockInfo db_b;
                db_b.set_hash(utils::sha256_digest("00000").value());
                db_b.set_size(5);
                auto b = model::block_info_ptr_t(new model::block_info_t(db_b, 99));
                cluster->get_blocks().put(b);
                file->set_size(15);
                file->remove_blocks();
                file->get_blocks().resize(3);
                file->assign_block(b, 0ul);
                file->assign_block(b, 1ul);
                file->assign_block(b, 2ul);

                create_controller();
                CHECK(read_file(path) == "1234567890");
            }
        };
    };
    F().run();
}

REGISTER_TEST_CASE(test_resume, "test_resume", "[controller]");
