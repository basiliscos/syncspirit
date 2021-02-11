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
    CHECK(db::migrate(version.value(), txn));

    txn = mk_txn(env, transaction_type_t::RO);
    version = db::get_version(txn);
    CHECK(version.value() == 1);
}

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

    *folder_src.add_devices() = device_src_1;
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
