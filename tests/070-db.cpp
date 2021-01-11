#include "catch.hpp"
#include "test-utils.h"
#include "db/utils.h"
#include <boost/filesystem.hpp>

using namespace syncspirit;
using namespace syncspirit::db;
namespace fs = boost::filesystem;

struct env_t {
    MDBX_env* env;
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
    MDBX_env* env;
    auto r = mdbx_env_create(&env);
    assert(r == MDBX_SUCCESS);
    MDBX_env_flags_t flags = MDBX_EXCLUSIVE | MDBX_SAFE_NOSYNC | MDBX_WRITEMAP | MDBX_NOTLS | MDBX_COALESCE | MDBX_LIFORECLAIM;
    r = mdbx_env_open(env, path.c_str(), flags, 0664);
    assert(r == MDBX_SUCCESS);
    return env_t{env, std::move(path)};
}

static transaction_t mk_txn(env_t& env, transaction_type_t type) {
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
