#pragma once

#include <boost/outcome.hpp>
#include "transaction.h"
#include "../model/folder.h"
#include "../config/folder.h"
#include "bep.pb.h"

namespace syncspirit {
namespace db {

extern std::uint32_t version;

outcome::result<std::uint32_t> get_version(transaction_t &txn) noexcept;
outcome::result<void> migrate(std::uint32_t from, transaction_t &txn) noexcept;
outcome::result<void> update_folder_info(const proto::Folder &folder, transaction_t &txn) noexcept;
outcome::result<void> create_folder_index(const proto::Folder &folder, const model::index_id_t &index_id,
                                          transaction_t &txn) noexcept;
outcome::result<model::folder_ptr_t> load_folder(config::folder_config_t &folder, const model::devices_map_t &devices,
                                                 transaction_t &txn) noexcept;

} // namespace db
} // namespace syncspirit
