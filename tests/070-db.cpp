#include "catch.hpp"
#include "test-utils.h"
//#include "test-db.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/aggregate.h"
#include "test_supervisor.h"
#include "access.h"
#include "model/cluster.h"
#include "db/utils.h"
#include "net/db_actor.h"
#include "access.h"
#include <boost/filesystem.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;

namespace fs = boost::filesystem;

namespace {
struct env {};
} // namespace

namespace syncspirit::net {

template <> inline auto &db_actor_t::access<env>() noexcept { return env; }

} // namespace syncspirit::net


namespace  {

struct fixture_t {
    using msg_t = message::load_cluster_response_t;
    using msg_ptr_t = r::intrusive_ptr_t<msg_t>;


    fixture_t() noexcept: root_path{ bfs::unique_path() }, path_quard{root_path} {
        utils::set_default("trace");
    }

    virtual supervisor_t::configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin){
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<msg_t>(
                    [&](msg_t &msg) { reply = &msg; }));
            });
        };
    }


    cluster_ptr_t make_cluster() noexcept {
        auto my_id = device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
        auto my_device =  device_t::create(my_id, "my-device").value();

        auto peer_id = device_id_t::from_string("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ").value();
        peer_device =  device_t::create(peer_id, "peer-device").value();

        return cluster_ptr_t(new cluster_t(my_device, 1));
    }

    virtual void run() noexcept {
        cluster = make_cluster();

        auto root_path = bfs::unique_path();
        bfs::create_directory(root_path);
        auto root_path_guard = path_guard_t(root_path);

        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->cluster = cluster;
        sup->configure_callback = configure();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t*>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        db_actor = sup->create_actor<db_actor_t>().cluster(cluster).db_dir(root_path.string()).timeout(timeout).finish();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t*>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        db_addr = db_actor->get_address();
        main();
        reply.reset();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t*>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {
    }

    r::address_ptr_t db_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    cluster_ptr_t cluster;
    device_ptr_t peer_device;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<net::db_actor_t> db_actor;
    bfs::path root_path;
    path_guard_t path_quard;
    r::system_context_t ctx;
    msg_ptr_t reply;
};

}

void test_db_migration() {
    struct F : fixture_t {
        void main() noexcept override {
            auto& db_env = db_actor->access<env>();
            auto txn_opt = db::make_transaction(db::transaction_type_t::RW, db_env);
            REQUIRE(txn_opt);
            auto& txn = txn_opt.value();
            auto load_opt = db::load(db::prefix::device, txn);
            REQUIRE(load_opt);
            auto& values = load_opt.value();
            REQUIRE(values.size() == 1);

        }
    };
    F().run();
}

void test_loading_empty_db() {
    struct F : fixture_t {

        void main() noexcept override {
            sup->request<payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);

            auto diff = reply->payload.res.diff;
            REQUIRE(diff->apply(*cluster));

            auto devices = cluster->get_devices();
            REQUIRE(devices.size() == 1);
            REQUIRE(devices.by_sha256(cluster->get_device()->device_id().get_sha256()));
        }
    };

    F().run();
}

void test_folder_creation() {
    struct F : fixture_t {
        void main() noexcept override {
            db::Folder db_folder;
            db_folder.set_id("1234-5678");
            db_folder.set_label("my-label");
            db_folder.set_path("/my/path");
            auto diff = diff::cluster_diff_ptr_t(new diff::modify::create_folder_t(db_folder));
            sup->send<payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            auto folder = cluster->get_folders().by_id(db_folder.id());
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().by_device(cluster->get_device()));

            sup->request<payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            auto folder_clone = cluster_clone->get_folders().by_id(folder->get_id());
            REQUIRE(folder_clone);
            REQUIRE(folder.get() != folder_clone.get());
            REQUIRE(folder_clone->get_label() == db_folder.label());
            REQUIRE(folder_clone->get_path().string() == db_folder.path());
            REQUIRE(folder_clone->get_folder_infos().size() == 1);
            REQUIRE(folder_clone->get_folder_infos().by_device(cluster->get_device()));
        }
    };

    F().run();
}

void test_peer_updating() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            db::Device db_device;
            db_device.set_cert_name("some-cn");
            db_device.set_name("some_name");
            db_device.set_auto_accept(true);

            auto diff = diff::cluster_diff_ptr_t(new diff::modify::update_peer_t(db_device, sha256));
            sup->send<payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();

            auto device = cluster->get_devices().by_sha256(sha256);
            REQUIRE(device);
            CHECK(device->get_name() == "some_name");
            CHECK(device->get_cert_name() == "some-cn");

            sup->request<payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            REQUIRE(cluster_clone->get_devices().size() == 2);
            auto device_clone = cluster_clone->get_devices().by_sha256(sha256);
            REQUIRE(device_clone);
            REQUIRE(device.get() != device_clone.get());
            CHECK(device_clone->get_name() == "some_name");
            CHECK(device_clone->get_cert_name() == "some-cn");
        }
    };

    F().run();
}

void test_folder_sharing() {
    struct F : fixture_t {
        void main() noexcept override {
            auto sha256 = peer_device->device_id().get_sha256();
            db::Device db_device;
            db_device.set_cert_name("some-cn");
            db_device.set_name("some_name");
            db_device.set_auto_accept(true);

            auto diffs = diff::aggregate_t::diffs_t{};
            diffs.push_back(new diff::modify::update_peer_t(db_device, sha256));

            db::Folder db_folder;
            db_folder.set_id("1234-5678");
            db_folder.set_label("my-label");
            db_folder.set_path("/my/path");
            diffs.push_back(new diff::modify::create_folder_t(db_folder));

            diffs.push_back(new diff::modify::share_folder_t(sha256, db_folder.id(), 5));

            auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
            sup->send<payload::model_update_t>(sup->get_address(), std::move(diff), nullptr);
            sup->do_process();
            CHECK(static_cast<r::actor_base_t*>(db_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

            sup->request<payload::load_cluster_request_t>(db_addr).send(timeout);
            sup->do_process();
            REQUIRE(reply);
            auto cluster_clone = make_cluster();
            REQUIRE(reply->payload.res.diff->apply(*cluster_clone));

            auto peer_device = cluster_clone->get_devices().by_sha256(sha256);
            REQUIRE(peer_device);
            auto folder = cluster_clone->get_folders().by_id(db_folder.id());
            REQUIRE(folder);
            REQUIRE(folder->get_folder_infos().size() == 2);
            auto fi = folder->get_folder_infos().by_device(peer_device);
            REQUIRE(fi);
            CHECK(fi->get_index() == 5);
            CHECK(fi->get_max_sequence() == 0);
        }
    };

    F().run();
}

REGISTER_TEST_CASE(test_db_migration, "test_db_migration", "[db]");
REGISTER_TEST_CASE(test_loading_empty_db, "test_loading_empty_db", "[db]");
REGISTER_TEST_CASE(test_folder_creation, "test_folder_creation", "[db]");
REGISTER_TEST_CASE(test_peer_updating, "test_peer_updating", "[db]");
REGISTER_TEST_CASE(test_folder_sharing, "test_folder_sharing", "[db]");


#if 0
TEST_CASE("get db version & migrate 0 -> 1", "[db]") {
    auto env = mk_env();
    auto txn = mk_txn(env, transaction_type_t::RW);
    auto version = db::get_version(txn);
    REQUIRE(version.value() == 0);
    db::Device db_d0;
    db_d0.set_id(test::device_id2sha256("VUV42CZ-IQD5A37-RPEBPM4-VVQK6E4-6WSKC7B-PVJQHHD-4PZD44V-ENC6WAZ"));
    db_d0.set_name("d1");
    auto d0 = model::device_ptr_t(new model::local_device_t(db_d0));

    CHECK(db::migrate(version.value(), d0, txn));

    txn = mk_txn(env, transaction_type_t::RO);
    version = db::get_version(txn);
    CHECK(version.value() == 1);

    SECTION("save & load device") {
        db::Device db_d1;
        db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
        db_d1.set_name("d1");
        auto d1 = model::device_ptr_t(new model::device_t(db_d1));

        db::Device db_d2;
        db_d2.set_id(test::device_id2sha256("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7"));
        db_d2.set_name("d2");
        auto d2 = model::device_ptr_t(new model::device_t(db_d2));

        model::devices_map_t devices;
        devices.put(d1);

        auto d1_key = d1->get_db_key();
        CHECK(!devices.by_key(d1_key));

        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_device(d1, txn);
        REQUIRE(r);
        CHECK(d1->get_db_key() > 0);
        CHECK(d1->get_db_key() != d1_key);
        CHECK(!devices.by_key(d1_key));

        devices.put(d1);
        CHECK(devices.by_key(d1->get_db_key()));

        devices.put(d2);
        r = db::store_device(d2, txn);
        REQUIRE(txn.commit());
        CHECK(d2->get_db_key() > 0);

        txn = mk_txn(env, transaction_type_t::RO);
        auto devices_opt = db::load_devices(txn);
        REQUIRE(devices_opt);
        auto &devices2 = devices_opt.value();
        REQUIRE(devices2.size() == devices.size() + 1);
        REQUIRE(devices2.by_id(d1->device_id.get_sha256()));
        REQUIRE(devices2.by_id(d2->device_id.get_sha256()));

        REQUIRE(*devices2.by_id(d1->device_id.get_sha256()) == *d1);
        REQUIRE(*devices2.by_id(d2->device_id.get_sha256()) == *d2);
        auto &ld1 = *devices2.by_id(d1->device_id.get_sha256());
        auto &ld2 = *devices2.by_id(d2->device_id.get_sha256());
        CHECK(ld1.get_db_key() != 0);
        CHECK(ld2.get_db_key() != 0);
    }

    SECTION("save & load ignored device") {
        auto d1 = model::device_id_t::from_sha256(
                      test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"))
                      .value();
        auto ignored_device = model::ignored_device_ptr_t(new model::device_id_t(d1));
        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_ignored_device(ignored_device, txn);
        REQUIRE(r);
        REQUIRE(txn.commit());

        txn = mk_txn(env, transaction_type_t::RO);
        auto ignored_devices_opt = db::load_ignored_devices(txn);
        CHECK(ignored_devices_opt);
        auto &ignored_devices = ignored_devices_opt.value();
        REQUIRE(ignored_devices.size() == 1);
        CHECK(*ignored_devices.by_key(ignored_device->get_sha256()) == *ignored_device);
    }

    SECTION("save, load & remove blocks") {
        db::BlockInfo db_bi;
        db_bi.set_hash("aaa");
        db_bi.set_size(123);
        auto bi = model::block_info_ptr_t(new model::block_info_t(db_bi));

        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_block_info(bi, txn);
        CHECK(r);
        REQUIRE(bi->get_db_key());
        REQUIRE(txn.commit());

        txn = mk_txn(env, transaction_type_t::RO);
        auto r_blocks = db::load_block_infos(txn);
        REQUIRE(r_blocks);
        auto &blocks = r_blocks.assume_value();
        CHECK(blocks.by_id(bi->get_hash()));
        CHECK(blocks.by_key(bi->get_db_key()));
        REQUIRE(txn.commit());

        txn = mk_txn(env, transaction_type_t::RW);
        r = db::remove(blocks, txn);
        CHECK(r);
        REQUIRE(txn.commit());

        txn = mk_txn(env, transaction_type_t::RO);
        r_blocks = db::load_block_infos(txn);
        REQUIRE(r_blocks);
        CHECK(r_blocks.assume_value().size() == 0);
        REQUIRE(txn.commit());
    }

    SECTION("save & load ignored folder") {
        auto db_f = db::IgnoredFolder{};
        db_f.set_id("123");
        db_f.set_label("my-label");
        auto ignored_folder = model::ignored_folder_ptr_t(new model::ignored_folder_t(db_f));
        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_ignored_folder(ignored_folder, txn);
        REQUIRE(r);
        REQUIRE(txn.commit());

        txn = mk_txn(env, transaction_type_t::RO);
        auto ignored_folders_opt = db::load_ignored_folders(txn);
        CHECK(ignored_folders_opt);
        auto &ignored_folders = ignored_folders_opt.value();
        REQUIRE(ignored_folders.size() == 1);
        CHECK(*ignored_folders.by_key(ignored_folder->id) == *ignored_folder);
    }

    SECTION("save & load folders") {
        db::Folder db_f1;
        db_f1.set_id("1111");
        db_f1.set_label("1111-l");
        auto f1 = model::folder_ptr_t(new model::folder_t(db_f1));

        db::Folder db_f2;
        db_f2.set_id("2222");
        db_f2.set_label("2222-l");
        auto f2 = model::folder_ptr_t(new model::folder_t(db_f2));

        model::folders_map_t folders;
        folders.put(f1);
        folders.put(f2);

        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_folder(f1, txn);
        REQUIRE(r);
        CHECK(f1->get_db_key() > 0);

        r = db::store_folder(f2, txn);
        REQUIRE(txn.commit());
        CHECK(f2->get_db_key() > 0);

        txn = mk_txn(env, transaction_type_t::RO);
        auto folders_opt = db::load_folders(txn);
        REQUIRE(folders_opt);
        auto &folders2 = folders_opt.value();
        REQUIRE(folders2.size() == folders.size());
        REQUIRE(folders2.by_id(f1->id()));
        REQUIRE(folders2.by_id(f2->id()));

        REQUIRE(*folders2.by_id(f1->id()) == *f1);
        REQUIRE(*folders2.by_id(f2->id()) == *f2);
    }

    SECTION("save & load folder_infos") {
        auto txn = mk_txn(env, transaction_type_t::RW);
        db::Device db_d1;
        db_d1.set_id(test::device_id2sha256("TXCLYU4-TK7GT6O-ZCECZI3-SJGAKRY-EAUJBJI-5XUY4YX-3YZQ6TH-3HGOBAU"));
        db_d1.set_name("d1");
        auto d1 = model::device_ptr_t(new model::device_t(db_d1));
        auto devices = model::devices_map_t();
        auto r_st = db::store_device(d1, txn);
        REQUIRE(r_st);
        devices.put(d1);

        db::Folder db_f1;
        db_f1.set_id("1111");
        db_f1.set_label("1111-l");
        auto f1 = model::folder_ptr_t(new model::folder_t(db_f1));
        r_st = db::store_folder(f1, txn);
        REQUIRE(r_st);

        auto cluster = model::cluster_ptr_t(new model::cluster_t(d1));
        cluster->add_folder(f1);
        f1->assign_cluster(cluster.get());
        auto &folders = cluster->get_folders();

        db::FolderInfo db_fi;
        db_fi.set_index_id(1234);
        // db_fi.set_max_sequence(1235);
        auto fi = model::folder_info_ptr_t(new model::folder_info_t(db_fi, d1.get(), f1.get(), 12345));
        auto r = db::store_folder_info(fi, txn);
        REQUIRE(r);
        REQUIRE(txn.commit());
        CHECK(fi->get_db_key());

        txn = mk_txn(env, transaction_type_t::RO);
        auto infos = db::load_folder_infos(devices, folders, txn);
        REQUIRE(infos);
        auto fi_x = infos.value().by_key(fi->get_db_key());
        CHECK(*fi_x == *fi);
        CHECK(fi_x->get_device() == fi->get_device());
        CHECK(fi_x->get_folder() == fi->get_folder());

        SECTION("save & load file_infos") {
            auto txn = mk_txn(env, transaction_type_t::RW);

            db::BlockInfo db_bi;
            db_bi.set_hash("aaa");
            db_bi.set_size(123);
            auto bi = model::block_info_ptr_t(new model::block_info_t(db_bi));

            auto r = db::store_block_info(bi, txn);
            CHECK(r);
            REQUIRE(bi->get_db_key());
            cluster->get_blocks().put(bi);

            db::FileInfo db_fi1;
            db_fi1.set_name("a/b/c.txt");
            db_fi1.mutable_blocks_keys()->Add(bi->get_db_key());
            auto fi1 = model::file_info_ptr_t(new model::file_info_t(db_fi1, fi.get()));
            r = db::store_file_info(fi1, txn);
            REQUIRE(r);
            REQUIRE(txn.commit());

            txn = mk_txn(env, transaction_type_t::RO);
            auto fi_infos = db::load_file_infos(infos.value(), txn);
            REQUIRE(fi_infos);
            auto fi1_x = fi_infos.value().by_key(fi1->get_db_key());
            CHECK(*fi1_x == *fi1);
            CHECK(fi1_x->get_folder_info()->get_folder() == fi.get()->get_folder());
            REQUIRE(fi1_x->get_blocks().size() == 1);
        }
    }
}
#endif
