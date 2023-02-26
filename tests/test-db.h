// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "db/utils.h"
#include <boost/filesystem.hpp>

namespace syncspirit::test {

namespace bfs = boost::filesystem;
namespace db = syncspirit::db;

struct env_t {
    MDBX_env *env;
    bfs::path path;
    ~env_t();
};

env_t mk_env();

db::transaction_t mk_txn(env_t &env, db::transaction_type_t type);

} // namespace syncspirit::test
