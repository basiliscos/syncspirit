#include "catch.hpp"
#include "fixture.h"
#include "ui/messages.hpp"
#include "utils/error_code.h"
#include "test-utils.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;

void test_start_reading() {
    struct F : Fixture {
        void setup() override { peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig()); }
        void main() override { CHECK(peer->start_reading == 1); }
    };
    F().run();
}

void test_new_folder() {
    using notify_t = ui::message::new_folder_notify_t;
    using notify_ptr_t = r::intrusive_ptr_t<notify_t>;
    using cluster_msg_t = r::intrusive_ptr_t<message::cluster_config_t>;

    struct F : Fixture {
        notify_ptr_t notify;
        proto::Folder p_folder;
        cluster_msg_t cluster_msg;

        void setup() override {
            auto config = proto::ClusterConfig();
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
            *config.add_folders() = folder;
            peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(config));
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

            auto folder = model::folder_ptr_t(new model::folder_t(db_folder));
            cluster->get_folders().put(folder);
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

            SECTION("file with content") {
                auto block_info = proto::BlockInfo();
                block_info.set_size(5);
                block_info.set_hash(utils::sha256_digest("12345").value());

                auto fi = proto::FileInfo();
                fi.set_name("a.txt");
                fi.set_type(proto::FileInfoType::FILE);
                fi.set_sequence(5);
                fi.set_block_size(5);

                auto path = dir / "a.txt";

                SECTION("non-empty") {
                    fi.set_size(5);
                    *fi.add_blocks() = block_info;
                    *index.add_files() = fi;

                    auto index_ptr = proto::message::Index(std::make_unique<proto::Index>(std::move(index)));
                    peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(index_ptr));

                    SECTION("correct small block") {
                        peer->responses.push_back("12345");
                        sup->do_process();
                        CHECK(read_file(path) == "12345");
                    }

                    SECTION("block hash mismatches") {
                        peer->responses.push_back("1234");
                        sup->do_process();
                        CHECK(!bfs::exists(path));
                        auto ec = controller->get_shutdown_reason()->next->ec;
                        CHECK(ec.value() == (int)utils::protocol_error_code_t::digest_mismatch);
                    }
                }

                SECTION("empty") {
                    *index.add_files() = fi;

                    auto index_ptr = proto::message::Index(std::make_unique<proto::Index>(std::move(index)));
                    peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(index_ptr));
                    sup->do_process();
                    REQUIRE(bfs::exists(path));
                    CHECK(bfs::file_size(path) == 0);
                }
            }

            SECTION("sync deleted file") {
                auto fi = proto::FileInfo();
                fi.set_name("b.txt");
                fi.set_type(proto::FileInfoType::FILE);
                fi.set_sequence(4);
                fi.set_deleted(true);
                *index.add_files() = fi;

                SECTION("file does not exist") {
                    auto index_ptr = proto::message::Index(std::make_unique<proto::Index>(std::move(index)));
                    peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(index_ptr));
                    sup->do_process();
                    CHECK(!controller->get_shutdown_reason());
                }

                SECTION("file does exist, and then it is deleted") {
                    auto index_ptr = proto::message::Index(std::make_unique<proto::Index>(std::move(index)));
                    peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(index_ptr));
                    auto p = dir / "b.txt";
                    write_file(p, "");
                    sup->do_process();
                    CHECK(!controller->get_shutdown_reason());
                    CHECK(!bfs::exists(p));
                }
            }
        };
    };
    F().run();
}

REGISTER_TEST_CASE(test_start_reading, "test_start_reading", "[controller]");
REGISTER_TEST_CASE(test_new_folder, "test_new_folder", "[controller]");
