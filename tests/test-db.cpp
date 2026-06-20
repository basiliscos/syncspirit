// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-db.h"
#include "test-utils.h"
#include "model/cluster.h"

namespace syncspirit::test {

env_t::~env_t() {
    if (env) {
        mdbx_env_close(env);
    }
}

env_t mk_env() {
    auto path = unique_path();
    MDBX_env *env;
    auto r = mdbx_env_create(&env);
    assert(r == MDBX_SUCCESS);
    (void)r;
    MDBX_env_flags_t flags =
        MDBX_EXCLUSIVE | MDBX_SAFE_NOSYNC | MDBX_WRITEMAP | MDBX_NOSTICKYTHREADS | MDBX_LIFORECLAIM;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto dir_view = path.get_view(allocator);
    r = mdbx_env_openW(env, dir_view.get_full_wname(true).data(), flags, 0664);
#else
    r = mdbx_env_open(env, path.get_full_name().data(), flags, 0664);
#endif
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
