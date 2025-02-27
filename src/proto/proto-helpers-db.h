// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "proto-fwd.hpp"

namespace syncspirit::db {

namespace encode {

utils::bytes_t SYNCSPIRIT_API encode(const BlockInfo&);
utils::bytes_t SYNCSPIRIT_API encode(const Device&);
utils::bytes_t SYNCSPIRIT_API encode(const FileInfo&);
utils::bytes_t SYNCSPIRIT_API encode(const Folder&);
utils::bytes_t SYNCSPIRIT_API encode(const FolderInfo&);
utils::bytes_t SYNCSPIRIT_API encode(const IgnoredFolder&);
utils::bytes_t SYNCSPIRIT_API encode(const PendingFolder&);
utils::bytes_t SYNCSPIRIT_API encode(const SomeDevice&);

}

namespace decode {

bool SYNCSPIRIT_API decode(utils::bytes_view_t, BlockInfo&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, Device&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, FileInfo&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, Folder&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, FolderInfo&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, IgnoredFolder&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, PendingFolder&);
bool SYNCSPIRIT_API decode(utils::bytes_view_t, SomeDevice&);

}

// BlockInfo
std::uint32_t       get_weak_hash(const BlockInfo&);
void                set_weak_hash(BlockInfo&, std::uint32_t value);
std::int32_t        get_size(const BlockInfo&);
void                set_size(BlockInfo&, std::int32_t value);

// Device
std::string_view    get_name(const Device&);
void                set_name(Device&, std::string_view value);
std::size_t         get_addresses_size(const Device&);
std::string_view    get_addresses(const Device&, std::size_t i);
void                set_addresses(Device&, std::size_t i, std::string_view);
Compression         get_compression(const Device&);
void                set_compression(Device&, Compression value);
std::string_view    get_cert_name(const Device&);
void                set_cert_name(Device&, std::string_view value);
bool                get_introducer(const Device&);
void                set_introducer(Device&, bool value);
bool                get_skip_introduction_removals(const Device&);
void                set_skip_introduction_removals(Device&, bool value);
bool                get_auto_accept(const Device&);
void                set_auto_accept(Device&, bool value);
bool                get_paused(const Device&);
void                set_paused(Device&, bool value);
std::int64_t        get_last_seen(const Device&);
void                set_last_seen(Device&, std::int64_t value);

// FileInfo
std::string_view    get_name(const FileInfo&);
void                set_name(FileInfo&, std::string_view value);
FileInfoType        get_type(const FileInfo&);
void                set_type(FileInfo&, FileInfoType value);
std::int64_t        get_size(const FileInfo&);
void                set_size(FileInfo&, std::int64_t value);
std::uint32_t       get_permissions(const FileInfo&);
void                set_permissions(FileInfo&, std::uint32_t value);
std::int64_t        get_modified_s(const FileInfo&);
void                set_modified_s(FileInfo&, std::int64_t value);
std::int32_t        get_modified_ns(const FileInfo&);
void                set_modified_ns(FileInfo&, std::int32_t value);
std::uint64_t       get_modified_by(const FileInfo&);
void                set_modified_by(FileInfo&, std::uint64_t value);
bool                get_deleted(const FileInfo&);
void                set_deleted(FileInfo&, bool value);
bool                get_invalid(const FileInfo&);
void                set_invalid(FileInfo&, bool value);
bool                get_no_permissions(const FileInfo&);
void                set_no_permissions(FileInfo&, bool value);
Vector&             get_version(const FileInfo&);
void                set_version(FileInfo&, Vector value);
std::int64_t        get_sequence(const FileInfo&);
void                set_sequence(FileInfo&, std::int64_t value);
std::int32_t        get_block_size(const FileInfo&);
void                set_block_size(FileInfo&, std::int32_t value);
std::size_t         get_blocks_size(const FileInfo&);
utils::bytes_view_t get_blocks(const FileInfo&, std::size_t i);
void                set_blocks(FileInfo&, std::size_t i, utils::bytes_view_t);
void                add_blocks(FileInfo&, utils::bytes_view_t);
std::string_view    get_symlink_target(const FileInfo&);
void                set_symlink_target(FileInfo&, std::string_view value);

// Folder
std::string_view    get_id(const Folder&);
void                set_id(Folder&, std::string_view value);
std::string_view    get_label(const Folder&);
void                set_label(Folder&, std::string_view value);
bool                get_read_only(const Folder&);
void                set_read_only(Folder&, bool value);
bool                get_ignore_permissions(const Folder&);
void                set_ignore_permissions(Folder&, bool value);
bool                get_ignore_delete(const Folder&);
void                set_ignore_delete(Folder&, bool value);
bool                get_disable_temp_indexes(const Folder&);
void                set_disable_temp_indexes(Folder&, bool value);
bool                get_paused(const Folder&);
void                set_paused(Folder&, bool value);
bool                get_scheduled(const Folder&);
void                set_scheduled(Folder&, bool value);
std::string_view    get_path(const Folder&);
void                set_path(Folder&, std::string_view value);
FolderType          get_folder_type(const Folder&);
void                set_folder_type(Folder&, FolderType value);
PullOrder           get_pull_order(const Folder&);
void                set_pull_order(Folder&, PullOrder value);
std::uint32_t       get_rescan_interval(const Folder&);
void                set_rescan_interval(Folder&, std::uint32_t value);

// FolderInfo
std::uint64_t       get_index_id(const FolderInfo&);
void                set_index_id(FolderInfo&, std::uint64_t value);
std::int64_t        get_max_sequence(const FolderInfo&);
void                set_max_sequence(FolderInfo&, std::int64_t value);

// IgnoredFolder
std::string_view    get_label(const IgnoredFolder&);
void                set_label(IgnoredFolder&, std::string_view value);

// PendingFolder
Folder&             get_folder(const PendingFolder&);
void                set_folder(PendingFolder&, Folder value);
FolderInfo&         get_folder_info(const PendingFolder&);
void                set_folder_info(PendingFolder&, FolderInfo value);

// SomeDevice
std::string_view    get_name(const SomeDevice&);
void                set_name(SomeDevice&, std::string_view value);
std::string_view    get_client_name(const SomeDevice&);
void                set_client_name(SomeDevice&, std::string_view value);
std::string_view    get_client_version(const SomeDevice&);
void                set_client_version(SomeDevice&, std::string_view value);
std::string_view    get_address(const SomeDevice&);
void                set_address(SomeDevice&, std::string_view value);
void                set_address(SomeDevice&, std::string value);
std::int64_t        get_last_seen(const SomeDevice&);
void                set_last_seen(SomeDevice&, std::int64_t value);

}
