// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "db/utils.h"
#include "test-utils.h"

namespace syncspirit::test {

namespace db = syncspirit::db;

struct env_t {
    MDBX_env *env;
    path_guard_t path;
    ~env_t();
};

env_t mk_env();

db::transaction_t mk_txn(env_t &env, db::transaction_type_t type);

} // namespace syncspirit::test
