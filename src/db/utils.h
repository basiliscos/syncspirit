#pragma once

#include <boost/outcome.hpp>
#include "transaction.h"
#include "cstdint"

namespace syncspirit {
namespace db {

extern std::uint32_t version;

outcome::result<std::uint32_t> get_version(transaction_t &txn) noexcept;
outcome::result<void> migrate(std::uint32_t from, transaction_t &txn) noexcept;

} // namespace db
} // namespace syncspirit
