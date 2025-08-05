// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/platform.h"
#include "transaction.h"
#include "prefix.h"
#include "model/diff/load/common.h"
#include "model/device.h"
#include "utils/bytes.h"
#include <boost/outcome.hpp>

namespace syncspirit {
namespace db {

extern std::uint32_t version;

using pair_t = model::diff::load::pair_t;
using container_t = model::diff::load::container_t;

SYNCSPIRIT_API outcome::result<std::uint32_t> get_version(transaction_t &txn) noexcept;
SYNCSPIRIT_API outcome::result<void> save_version(std::uint32_t v, transaction_t &txn) noexcept;
SYNCSPIRIT_API outcome::result<void> migrate(std::uint32_t from, model::device_ptr_t device,
                                             transaction_t &txn) noexcept;

SYNCSPIRIT_API outcome::result<container_t> load(discr_t prefix, transaction_t &txn) noexcept;
SYNCSPIRIT_API outcome::result<void> save(const pair_t &container, transaction_t &txn) noexcept;
SYNCSPIRIT_API outcome::result<void> remove(utils::bytes_view_t key, transaction_t &txn) noexcept;

} // namespace db
} // namespace syncspirit
