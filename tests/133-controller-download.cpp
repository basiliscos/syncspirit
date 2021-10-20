#include "catch.hpp"
#include "fixture.h"
#include "ui/messages.hpp"
#include "utils/error_code.h"
#include "test-utils.h"
#include "test-db.h"

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::utils;

namespace {

struct DonwloadFixture : Fixture {
    model::folder_ptr_t folder;
    model::folder_info_ptr_t fi_my;
    model::folder_info_ptr_t fi_peer;

    virtual void setup_files() noexcept {}

    void setup() override {
        db::Folder db_f;
        db_f.set_id("folder-id");
        db_f.set_label("f1-label");
        db_f.set_path(root_path.string());
        folder = model::folder_ptr_t(new model::folder_t(db_f, ++seq));

        cluster->add_folder(folder);

        db::FolderInfo db_folder_info;
        db_folder_info.set_index_id(++seq);
        db_folder_info.set_max_sequence(++seq);

        fi_my =
            model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, device_my.get(), folder.get(), ++seq));
        folder->add(fi_my);

        db_folder_info.set_index_id(++seq);
        fi_peer =
            model::folder_info_ptr_t(new model::folder_info_t(db_folder_info, device_peer.get(), folder.get(), ++seq));
        folder->add(fi_peer);

        setup_files();

        proto::ClusterConfig p_cluster;
        auto p_folder = p_cluster.add_folders();
        p_folder->set_label(folder->get_label());
        p_folder->set_id(folder->get_id());

        auto p_device_my = p_folder->add_devices();
        p_device_my->set_id(device_my->device_id.get_sha256());
        p_device_my->set_index_id(fi_my->get_index());
        p_device_my->set_max_sequence(0);

        auto p_device_peer = p_folder->add_devices();
        p_device_peer->set_id(device_my->device_id.get_sha256());
        p_device_peer->set_index_id(fi_peer->get_index());
        p_device_peer->set_max_sequence(fi_peer->get_max_sequence());

        peer_cluster_config = payload::cluster_config_ptr_t(new proto::ClusterConfig(p_cluster));
    }
};

} // namespace

void test_download_new() {
    struct F : DonwloadFixture {
        void setup_files() noexcept override {
            auto bi = proto::BlockInfo();
            bi.set_size(5);
            bi.set_hash(utils::sha256_digest("12345").value());

            auto block = model::block_info_ptr_t(new model::block_info_t(bi));
            block->set_db_key(++seq);
            cluster->get_blocks().put(block);

            db::FileInfo db_file;
            db_file.set_sequence(fi_peer->get_max_sequence());
            db_file.set_name("a.txt");
            db_file.set_size(5);
            db_file.add_blocks_keys(block->get_db_key());
            auto file = model::file_info_ptr_t(new model::file_info_t(db_file, fi_peer.get()));

            fi_peer->add(file);
        }

        void pre_run() override { peer->push_response("12345"); }

        void main() override {
            REQUIRE(peer->start_reading == 1);
            sup->do_process();

            auto file = fi_my->get_file_infos().begin()->second;
            CHECK(file);
            auto path = file->get_path();
            CHECK(read_file(path) == "12345");
        };
    };
    F().run();
}

void test_download_overwrite() {
    struct F : DonwloadFixture {
        void setup_files() noexcept override {
            fi_peer->set_max_sequence(fi_my->get_max_sequence() + 1);

            auto bi_1 = proto::BlockInfo();
            bi_1.set_size(5);
            bi_1.set_hash(utils::sha256_digest("12345").value());

            auto bi_2 = proto::BlockInfo();
            bi_2.set_size(5);
            bi_2.set_hash(utils::sha256_digest("67890").value());

            auto b1 = model::block_info_ptr_t(new model::block_info_t(bi_1));
            b1->set_db_key(++seq);
            cluster->get_blocks().put(b1);

            auto b2 = model::block_info_ptr_t(new model::block_info_t(bi_2));
            b2->set_db_key(++seq);
            cluster->get_blocks().put(b2);

            db::FileInfo db_file_my;
            db_file_my.set_sequence(fi_my->get_max_sequence());
            db_file_my.set_name("a.txt");
            db_file_my.set_size(5);
            db_file_my.add_blocks_keys(b1->get_db_key());
            auto file_my = model::file_info_ptr_t(new model::file_info_t(db_file_my, fi_my.get()));
            fi_my->add(file_my);

            db::FileInfo db_file_peer;
            db_file_peer.set_sequence(fi_peer->get_max_sequence());
            db_file_peer.set_name("a.txt");
            db_file_peer.set_size(5);
            db_file_peer.add_blocks_keys(b2->get_db_key());
            auto file_peer = model::file_info_ptr_t(new model::file_info_t(db_file_peer, fi_peer.get()));
            fi_peer->add(file_peer);

            auto env = mk_env();
            auto txn = mk_txn(env, db::transaction_type_t::RW);
            REQUIRE(db::store_block_info(b1, txn));
            REQUIRE(txn.commit());
        }

        void pre_run() override { peer->push_response("67890"); }

        void main() override {
            REQUIRE(peer->start_reading == 1);
            sup->do_process();

            auto file = fi_my->get_file_infos().begin()->second;
            CHECK(file);
            auto path = file->get_path();
            CHECK(read_file(path) == "67890");
            CHECK(cluster->get_blocks().size() == 1);
            CHECK(cluster->get_deleted_blocks().size() == 0);
        };
    };
    F().run();
}

REGISTER_TEST_CASE(test_download_new, "test_download_new", "[controller]");
REGISTER_TEST_CASE(test_download_overwrite, "test_download_overwrite", "[controller]");
