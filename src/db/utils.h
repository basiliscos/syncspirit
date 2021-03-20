#pragma once

#include <boost/outcome.hpp>
#include "transaction.h"
#include "../model/device_id.h"
#include "../model/folder.h"
#include "../model/folder_info.h"
#include "../model/file_info.h"
#include "bep.pb.h"

namespace syncspirit {
namespace db {

extern std::uint32_t version;

outcome::result<std::uint32_t> get_version(transaction_t &txn) noexcept;
outcome::result<void> migrate(std::uint32_t from, model::device_ptr_t device, transaction_t &txn) noexcept;

outcome::result<void> store_device(model::device_ptr_t &device, transaction_t &txn) noexcept;
outcome::result<model::devices_map_t> load_devices(transaction_t &txn) noexcept;

outcome::result<void> store_folder(model::folder_ptr_t &folder, transaction_t &txn) noexcept;
outcome::result<model::folders_map_t> load_folders(transaction_t &txn) noexcept;

outcome::result<void> store_folder_info(model::folder_info_ptr_t &info, transaction_t &txn) noexcept;
outcome::result<model::folder_infos_map_t>
load_folder_infos(model::devices_map_t &devices, model::folders_map_t &folders, transaction_t &txn) noexcept;

outcome::result<void> store_file_info(model::file_info_ptr_t &info, transaction_t &txn) noexcept;
outcome::result<model::file_infos_map_t> load_file_infos(model::folder_infos_map_t folder_infos,
                                                         transaction_t &txn) noexcept;

outcome::result<void> store_ignored_device(model::ignored_device_ptr_t &info, transaction_t &txn) noexcept;
outcome::result<model::ignored_devices_map_t> load_ignored_devices(transaction_t &txn) noexcept;

outcome::result<void> store_ignored_folder(model::ignored_folder_ptr_t &info, transaction_t &txn) noexcept;
outcome::result<model::ignored_folders_map_t> load_ignored_folders(transaction_t &txn) noexcept;

/*
outcome::result<void> update_folder_info(const proto::Folder &folder, transaction_t &txn) noexcept;

outcome::result<void> create_folder(const proto::Folder &folder, const model::index_id_t &index_id,
                                    const model::device_id_t &device_id, transaction_t &txn) noexcept;
outcome::result<model::folder_ptr_t> load_folder(config::folder_config_t &folder, const model::device_ptr_t &device,
                                                 const model::devices_map_t &devices, transaction_t &txn) noexcept;
*/

} // namespace db
} // namespace syncspirit
