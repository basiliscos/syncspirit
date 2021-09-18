#include "catch.hpp"
#include "fixture.h"
#include "ui/messages.hpp"
#include "utils/error_code.h"
#include "test-utils.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;

namespace {
namespace to {
struct get_starter_plugin {};
} // namespace to
} // namespace

namespace rotor {
template <> inline auto &actor_base_t::access<to::get_starter_plugin>() noexcept {
    auto plugin = get_plugin(plugin::starter_plugin_t::class_identity);
    return *static_cast<plugin::starter_plugin_t *>(plugin);
}
} // namespace rotor

void test_indexupdate() {
    struct F : Fixture {
        model::folder_ptr_t folder;

        void setup() override {
            auto config = proto::ClusterConfig();
            auto p_folder = proto::Folder();
            p_folder.set_label("my-folder");
            p_folder.set_id("123");

            auto d_my = proto::Device();
            d_my.set_id(device_my->device_id.get_sha256());
            d_my.set_index_id(++seq);
            d_my.set_max_sequence(0);

            auto d_peer = proto::Device();
            d_my.set_id(device_peer->device_id.get_sha256());
            d_my.set_index_id(++seq);
            d_my.set_max_sequence(1);

            *p_folder.add_devices() = d_my;
            *p_folder.add_devices() = d_peer;
            *config.add_folders() = p_folder;
            peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(config));

            auto db_folder = db::Folder();
            db_folder.set_id(p_folder.id());
            db_folder.set_label(p_folder.label());
            db_folder.set_path(root_path.string());
            folder = new model::folder_t(db_folder, ++seq);
            folder->assign_device(device_my);

            auto db_fi_my = db::FolderInfo();
            db_fi_my.set_index_id(++seq);
            db_fi_my.set_max_sequence(0);
            auto fi_my =
                model::folder_info_ptr_t(new model::folder_info_t(db_fi_my, device_my.get(), folder.get(), ++seq));
            folder->add(fi_my);

            auto db_fi_peer = db::FolderInfo();
            db_fi_peer.set_index_id(++seq);
            db_fi_peer.set_max_sequence(1);
            auto fi_peer =
                model::folder_info_ptr_t(new model::folder_info_t(db_fi_peer, device_peer.get(), folder.get(), ++seq));
            folder->add(fi_peer);
            cluster->add_folder(folder);

            proto::FileInfo pr_fileinfo;
            pr_fileinfo.set_name("some-file");
            pr_fileinfo.set_sequence(++seq);
            pr_fileinfo.set_size(0);
            auto file_my = model::file_info_ptr_t(new model::file_info_t(pr_fileinfo, fi_my.get()));
            file_my->record_update(*device_peer);
            fi_my->add(file_my);

            pr_fileinfo.set_sequence(++seq);
            auto file_peer = model::file_info_ptr_t(new model::file_info_t(pr_fileinfo, fi_peer.get()));
            fi_peer->add(file_peer);
            file_peer->record_update(*device_peer);
        }

        void main() override {
            CHECK(peer->start_reading == 1);

            proto::IndexUpdate iu;
            iu.set_folder(folder->id());

            auto fi = proto::FileInfo();

            SECTION("send a folder") {
                fi.set_name("some_dir");
                fi.set_type(proto::FileInfoType::DIRECTORY);
                fi.set_sequence(3);

                *iu.add_files() = fi;

                auto iu_ptr = proto::message::IndexUpdate(std::make_unique<proto::IndexUpdate>(std::move(iu)));
                peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(iu_ptr));
                sup->do_process();

                auto path = bfs::path(root_path / "some_dir");
                CHECK(bfs::exists(path));
                CHECK(bfs::is_directory(path));
            }

            SECTION("send a symlink") {
                using message_t = message::file_update_notify_t;
                auto &starter_plugin = static_cast<r::actor_base_t *>(sup.get())->access<to::get_starter_plugin>();
                bool sup_notify_received = false;
                auto handler = r::lambda<message_t>([&](message_t &msg) noexcept {
                    sup_notify_received = true;
                    auto &file = msg.payload.file;
                    CHECK(file);
                    sup->send<payload::file_update_t>(controller->get_address(), file);
                });
                starter_plugin.subscribe_actor(handler);

                fi.set_name("link");
                fi.set_type(proto::FileInfoType::SYMLINK);
                fi.set_sequence(4);
                bfs::path target = root_path / "not-existing";
                fi.set_symlink_target(target.string());

                *iu.add_files() = fi;

                auto iu_ptr = proto::message::IndexUpdate(std::make_unique<proto::IndexUpdate>(std::move(iu)));
                peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(iu_ptr));
                sup->do_process();

                auto path = bfs::path(root_path / "link");
                CHECK(bfs::is_symlink(path));
                CHECK(bfs::read_symlink(path) == target);
                CHECK(sup_notify_received);
            }

            SECTION("send deleted file") {
                uint64_t change_id;
                auto &sha = device_peer->device_id.get_sha256();
                std::copy(sha.data(), sha.data() + sizeof(change_id), reinterpret_cast<char *>(&change_id));

                fi.set_type(proto::FileInfoType::FILE);
                fi.set_name("some-file");
                fi.set_sequence(++seq);
                fi.set_size(0);
                fi.set_deleted(true);
                auto c1 = fi.mutable_version()->add_counters();
                c1->set_id(change_id);
                c1->set_value(1ul);

                auto c2 = fi.mutable_version()->add_counters();
                c2->set_id(change_id);
                c2->set_value(2ul);

                *iu.add_files() = fi;
                auto file_path = root_path / "some-file";
                write_file(file_path, "");

                auto iu_ptr = proto::message::IndexUpdate(std::make_unique<proto::IndexUpdate>(std::move(iu)));
                peer->send<payload::forwarded_message_t>(controller->get_address(), std::move(iu_ptr));
                sup->do_process();

                CHECK(!bfs::exists(file_path));
            }
        }
    };
    F().run();
}

REGISTER_TEST_CASE(test_indexupdate, "test_indexupdate", "[controller]");
