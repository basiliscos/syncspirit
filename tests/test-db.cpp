// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-db.h"
#include "test-utils.h"

namespace syncspirit::test {

env_t::~env_t() {
    if (env) {
        mdbx_env_close(env);
    }
    // std::cout << path.c_str() << "\n";
    bfs::remove_all(path);
}

env_t mk_env() {
    auto path = unique_path();
    MDBX_env *env;
    auto r = mdbx_env_create(&env);
    assert(r == MDBX_SUCCESS);
    MDBX_env_flags_t flags =
        MDBX_EXCLUSIVE | MDBX_SAFE_NOSYNC | MDBX_WRITEMAP | MDBX_NOSTICKYTHREADS | MDBX_LIFORECLAIM;
    r = mdbx_env_open(env, path.string().c_str(), flags, 0664);
    assert(r == MDBX_SUCCESS);
    // std::cout << path.c_str() << "\n";
    return env_t{env, std::move(path)};
}

db::transaction_t mk_txn(env_t &env, db::transaction_type_t type) {
    auto r = db::make_transaction(type, env.env);
    assert((bool)r);
    return std::move(r.value());
}

} // namespace syncspirit::test
