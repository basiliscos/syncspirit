#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "db/utils.h"
#include <boost/filesystem.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
namespace fs = boost::filesystem;

struct env_t {
    MDBX_env *env;
    fs::path path;
    ~env_t() {
        if (env) {
            mdbx_env_close(env);
        }
        //std::cout << path.c_str() << "\n";
        fs::remove_all(path);
    }
};

static env_t mk_env() {
    auto path = fs::unique_path();
    MDBX_env *env;
    auto r = mdbx_env_create(&env);
    assert(r == MDBX_SUCCESS);
    MDBX_env_flags_t flags =
        MDBX_EXCLUSIVE | MDBX_SAFE_NOSYNC | MDBX_WRITEMAP | MDBX_NOTLS | MDBX_COALESCE | MDBX_LIFORECLAIM;
    r = mdbx_env_open(env, path.c_str(), flags, 0664);
    assert(r == MDBX_SUCCESS);
    //std::cout << path.c_str() << "\n";
    return env_t{env, std::move(path)};
}

static transaction_t mk_txn(env_t &env, transaction_type_t type) {
    auto r = db::make_transaction(type, env.env);
    assert((bool)r);
    return std::move(r.value());
}

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
        devices.put(d2);

        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_device(d1, txn);
        REQUIRE(r);
        CHECK(d1->get_db_key() > 0);
        r = db::store_device(d2, txn);
        REQUIRE(txn.commit());
        CHECK(d2->get_db_key() > 0);

        txn = mk_txn(env, transaction_type_t::RO);
        auto devices_opt = db::load_devices(txn);
        REQUIRE(devices_opt);
        auto& devices2 = devices_opt.value();
        REQUIRE(devices2.size() == devices.size() + 1);
        REQUIRE(devices2.by_id(d1->device_id.get_sha256()));
        REQUIRE(devices2.by_id(d2->device_id.get_sha256()));

        REQUIRE(*devices2.by_id(d1->device_id.get_sha256()) == *d1);
        REQUIRE(*devices2.by_id(d2->device_id.get_sha256()) == *d2);
        auto& ld1 = *devices2.by_id(d1->device_id.get_sha256());
        auto& ld2 = *devices2.by_id(d2->device_id.get_sha256());
        CHECK(ld1.get_db_key() != 0);
        CHECK(ld2.get_db_key() != 0);
    }

    SECTION("save & load ignored device") {
        auto d1 =  model::device_id_t::from_sha256(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD")).value();
        auto ignored_device = model::ignored_device_ptr_t(new model::device_id_t(d1));
        auto txn = mk_txn(env, transaction_type_t::RW);
        auto r = db::store_ignored_device(ignored_device, txn);
        REQUIRE(r);
        REQUIRE(txn.commit());

        txn = mk_txn(env, transaction_type_t::RO);
        auto ignored_devices_opt = db::load_ignored_devices(txn);
        CHECK(ignored_devices_opt);
        auto& ignored_devices = ignored_devices_opt.value();
        REQUIRE(ignored_devices.size() == 1);
        CHECK(*ignored_devices.by_key(ignored_device->get_sha256()) == *ignored_device);
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
        auto& ignored_folders = ignored_folders_opt.value();
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
        auto& folders2 = folders_opt.value();
        REQUIRE(folders2.size() == folders.size());
        REQUIRE(folders2.by_id(f1->id()));
        REQUIRE(folders2.by_id(f2->id()));

        REQUIRE(*folders2.by_id(f1->id()) == *f1);
        REQUIRE(*folders2.by_id(f2->id()) == *f2);
    }

    SECTION("save & load folder_infos") {
        db::Device db_d1;
        db_d1.set_id(test::device_id2sha256("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD"));
        db_d1.set_name("d1");
        auto d1 = model::device_ptr_t(new model::device_t(db_d1));
        auto devices = model::devices_map_t();
        devices.put(d1);

        db::Folder db_f1;
        db_f1.set_id("1111");
        db_f1.set_label("1111-l");
        auto f1 = model::folder_ptr_t(new model::folder_t(db_f1));
        auto folders = model::folders_map_t();
        folders.put(f1);

        auto txn = mk_txn(env, transaction_type_t::RW);
        db::FolderInfo db_fi;
        db_fi.set_index_id(1234);
        //db_fi.set_max_sequence(1235);
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
            db::FileInfo db_fi1;
            db_fi1.set_name("a/b/c.txt");
            auto fi1 = model::file_info_ptr_t(new model::file_info_t(db_fi1, fi.get()));
            auto txn = mk_txn(env, transaction_type_t::RW);
            auto r = db::store_file_info(fi1, txn);
            REQUIRE(r);
            REQUIRE(txn.commit());

            txn = mk_txn(env, transaction_type_t::RO);
            auto fi_infos = db::load_file_infos(infos.value(), txn);
            REQUIRE(fi_infos);
            auto fi1_x = fi_infos.value().by_key(fi1->get_db_key());
            CHECK(*fi1_x == *fi1);
            CHECK(fi1_x->get_folder_info() == fi_x.get());
        }
    }
}

#if 0

TEST_CASE("save & load folder", "[db]") {
    auto env = mk_env();
    auto txn = mk_txn(env, transaction_type_t::RW);

    auto d1 =
        model::device_id_t::from_string("KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD").value();
    auto d2 =
        model::device_id_t::from_string("KUEQE66-JJ7P6AD-BEHD4ZW-GPBNW6Q-Y4C3K4Y-X44WJWZ-DVPIDXS-UDRJMA7").value();
    proto::Folder folder_src;
    folder_src.set_id("folder-id");
    folder_src.set_label("folder-label");
    folder_src.set_read_only(false);
    folder_src.set_ignore_delete(false);
    folder_src.set_ignore_permissions(false);
    folder_src.set_paused(false);
    folder_src.set_read_only(false);

    proto::Device device_src_1;
    device_src_1.set_id(d1.get_sha256());
    device_src_1.set_name("d1-name");
    device_src_1.set_cert_name("d1-cert-name");
    device_src_1.set_compression(proto::Compression::ALWAYS);
    device_src_1.set_max_sequence(0);
    device_src_1.set_index_id(5);
    device_src_1.set_introducer(false);

    proto::Device device_src_2;
    device_src_1.set_id(d2.get_sha256());
    device_src_1.set_name("d2-name");
    device_src_1.set_cert_name("d2-cert-name");
    device_src_1.set_compression(proto::Compression::ALWAYS);
    device_src_1.set_max_sequence(3);
    device_src_1.set_index_id(5);
    device_src_1.set_introducer(false);

    3*folder_src.add_devices() = device_src_1;
    *folder_src.add_devices() = device_src_2;
    REQUIRE(db::update_folder_info(folder_src, txn));
    REQUIRE(db::create_folder(folder_src, 123, d2, txn));
    REQUIRE(txn.commit());

    txn = mk_txn(env, transaction_type_t::RO);
    model::devices_map_t devices;

    config::device_config_t d1_cfg{d1.get_value(),
                                   device_src_1.name(),
                                   config::compression_t::all,
                                   device_src_1.cert_name(),
                                   device_src_1.introducer(),
                                   false,
                                   false,
                                   device_src_1.skip_introduction_removals(),
                                   {},
                                   {}};
    config::device_config_t d2_cfg{
        d2.get_value(), "d2-name", config::compression_t::all, "d2-cert-name", false, false, false, true, {}, {}};
    model::device_ptr_t device1 = new model::device_t(d1_cfg);
    model::device_ptr_t device2 = new model::device_t(d2_cfg);
    devices.insert({device1->device_id.get_value(), device1});
    devices.insert({device2->device_id.get_value(), device2});

    config::folder_config_t folder_cfg{
        folder_src.id(),
        folder_src.label(),
        "/some/path",
        {d1.get_value(), d2.get_value()},
        config::folder_type_t::send_and_receive,
        3600,
        config::pull_order_t::random,
        true,
        true,
        false,
        false,
        false,
        false,
    };
    auto folder_opt = db::load_folder(folder_cfg, device1, devices, txn);
    REQUIRE(folder_opt);
    auto &folder = folder_opt.value();
    CHECK(folder->id() == folder_src.id());
    CHECK(folder->access<test::to::device>() == device1);
    REQUIRE(folder->access<test::to::devices>().size() == 2);

    auto &fd1 = *folder->access<test::to::devices>().begin();
    CHECK(fd1.device == device1);
    CHECK(fd1.index_id == 123);
    CHECK(fd1.max_sequence == 0);

    auto &fd2 = *(++folder->access<test::to::devices>().begin());
    CHECK(fd2.device == device2);
    CHECK(fd2.index_id == 5);
    CHECK(fd2.max_sequence == 3);
}
#endif
