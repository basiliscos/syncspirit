#pragma once

#include <boost/outcome.hpp>
#include "transaction.h"
#include "prefix.h"
#include "../model/diff/load/common.h"
#include "../model/device.h"
//#include "../model/block_info.h"
//#include "../model/device_id.h"
//#include "../model/folder.h"
//#include "../model/folder_info.h"
//#include "../model/file_info.h"
//#include "bep.pb.h"

namespace syncspirit {
namespace db {

extern std::uint32_t version;

using pair_t = model::diff::load::pair_t;
using container_t = model::diff::load::container_t;

outcome::result<std::uint32_t> get_version(transaction_t &txn) noexcept;
outcome::result<void> migrate(std::uint32_t from, model::device_ptr_t device, transaction_t &txn) noexcept;

outcome::result<container_t> load(discr_t prefix, transaction_t &txn) noexcept;
outcome::result<void> save(const pair_t &container, transaction_t &txn) noexcept;
outcome::result<void> remove(std::string_view key, transaction_t &txn) noexcept;

} // namespace db
} // namespace syncspirit
