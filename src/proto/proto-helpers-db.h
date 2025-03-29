// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "proto-fwd.hpp"
#include "utils/bytes.h"
#include "syncspirit-export.h"

namespace syncspirit::db {

utils::bytes_t SYNCSPIRIT_API encode(const BlockInfo &);
utils::bytes_t SYNCSPIRIT_API encode(const Device &);
utils::bytes_t SYNCSPIRIT_API encode(const FileInfo &);
utils::bytes_t SYNCSPIRIT_API encode(const Folder &);
utils::bytes_t SYNCSPIRIT_API encode(const FolderInfo &);
utils::bytes_t SYNCSPIRIT_API encode(const IgnoredFolder &);
utils::bytes_t SYNCSPIRIT_API encode(const PendingFolder &);
utils::bytes_t SYNCSPIRIT_API encode(const SomeDevice &);

int SYNCSPIRIT_API decode(utils::bytes_view_t, BlockInfo &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, Device &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, FileInfo &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, Folder &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, FolderInfo &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, IgnoredFolder &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, PendingFolder &);
int SYNCSPIRIT_API decode(utils::bytes_view_t, SomeDevice &);

/*****************/
/*** BlockInfo ***/
/*****************/

inline std::uint32_t get_weak_hash(const BlockInfo &msg) {
    using namespace pp;
    return msg["weak_hash"_f].value_or(0);
}
inline void set_weak_hash(BlockInfo &msg, std::uint32_t value) {
    using namespace pp;
    msg["weak_hash"_f] = value;
}
inline std::int32_t get_size(const BlockInfo &msg) {
    using namespace pp;
    return msg["size"_f].value_or(0);
}
inline void set_size(BlockInfo &msg, std::int32_t value) {
    using namespace pp;
    msg["size"_f] = value;
}

/**************/
/*** Device ***/
/**************/

inline std::string_view get_name(const Device &msg) {
    using namespace pp;
    auto &opt = msg["name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_name(Device &msg, std::string_view value) {
    using namespace pp;
    msg["name"_f] = std::string(value);
}
inline std::size_t get_addresses_size(const Device &msg) {
    using namespace pp;
    return msg["addresses"_f].size();
}
inline std::string_view get_addresses(const Device &msg, std::size_t i) {
    using namespace pp;
    return msg["addresses"_f][i];
}
inline void set_addresses(Device &msg, std::size_t i, std::string_view value) {
    using namespace pp;
    msg["addresses"_f][i] = std::string(value);
}
inline void clear_addresses(Device &msg) {
    using namespace pp;
    msg["addresses"_f].clear();
}
inline void add_addresses(Device &msg, std::string_view value) {
    using namespace pp;
    msg["addresses"_f].emplace_back(std::string(value));
}
inline Compression get_compression(const Device &msg) {
    using namespace pp;
    return msg["compression"_f].value_or(Compression{});
}
inline void set_compression(Device &msg, Compression value) {
    using namespace pp;
    msg["compression"_f] = value;
}
inline std::string_view get_cert_name(const Device &msg) {
    using namespace pp;
    auto &opt = msg["cert_name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_cert_name(Device &msg, std::string_view value) {
    using namespace pp;
    msg["cert_name"_f] = std::string(value);
}
inline bool get_introducer(const Device &msg) {
    using namespace pp;
    return msg["introducer"_f].value_or(false);
}
inline void set_introducer(Device &msg, bool value) {
    using namespace pp;
    msg["introducer"_f] = value;
}
inline bool get_skip_introduction_removals(const Device &msg) {
    using namespace pp;
    return msg["skip_introduction_removals"_f].value_or(false);
}
inline void set_skip_introduction_removals(Device &msg, bool value) {
    using namespace pp;
    msg["skip_introduction_removals"_f] = value;
}
inline bool get_auto_accept(const Device &msg) {
    using namespace pp;
    return msg["auto_accept"_f].value_or(false);
}
inline void set_auto_accept(Device &msg, bool value) {
    using namespace pp;
    msg["auto_accept"_f] = value;
}
inline bool get_paused(const Device &msg) {
    using namespace pp;
    return msg["paused"_f].value_or(false);
}
inline void set_paused(Device &msg, bool value) {
    using namespace pp;
    msg["paused"_f] = value;
}
inline std::int64_t get_last_seen(const Device &msg) {
    using namespace pp;
    return msg["last_seen"_f].value_or(0);
}
inline void set_last_seen(Device &msg, std::int64_t value) {
    using namespace pp;
    msg["last_seen"_f] = value;
}

/****************/
/*** FileInfo ***/
/****************/

inline std::string_view get_name(const FileInfo &msg) {
    using namespace pp;
    auto &opt = msg["name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_name(FileInfo &msg, std::string_view value) {
    using namespace pp;
    msg["name"_f] = std::string(value);
}
inline FileInfoType get_type(const FileInfo &msg) {
    using namespace pp;
    return msg["type"_f].value_or(FileInfoType{});
}
inline void set_type(FileInfo &msg, FileInfoType value) {
    using namespace pp;
    msg["type"_f] = value;
}
inline std::int64_t get_size(const FileInfo &msg) {
    using namespace pp;
    return msg["size"_f].value_or(0);
}
inline void set_size(FileInfo &msg, std::int64_t value) {
    using namespace pp;
    msg["size"_f] = value;
}
inline std::uint32_t get_permissions(const FileInfo &msg) {
    using namespace pp;
    return msg["permissions"_f].value_or(0);
}
inline void set_permissions(FileInfo &msg, std::uint32_t value) {
    using namespace pp;
    msg["permissions"_f] = value;
}
inline std::int64_t get_modified_s(const FileInfo &msg) {
    using namespace pp;
    return msg["modified_s"_f].value_or(0);
}
inline void set_modified_s(FileInfo &msg, std::int64_t value) {
    using namespace pp;
    msg["modified_s"_f] = value;
}
inline std::int32_t get_modified_ns(const FileInfo &msg) {
    using namespace pp;
    return msg["modified_ns"_f].value_or(0);
}
inline void set_modified_ns(FileInfo &msg, std::int32_t value) {
    using namespace pp;
    msg["modified_ns"_f] = value;
}
inline std::uint64_t get_modified_by(const FileInfo &msg) {
    using namespace pp;
    return msg["modified_by"_f].value_or(0);
}
inline void set_modified_by(FileInfo &msg, std::uint64_t value) {
    using namespace pp;
    msg["modified_by"_f] = value;
}
inline bool get_deleted(const FileInfo &msg) {
    using namespace pp;
    return msg["deleted"_f].value_or(false);
}
inline void set_deleted(FileInfo &msg, bool value) {
    using namespace pp;
    msg["deleted"_f] = value;
}
inline bool get_invalid(const FileInfo &msg) {
    using namespace pp;
    return msg["invalid"_f].value_or(false);
}
inline void set_invalid(FileInfo &msg, bool value) {
    using namespace pp;
    msg["invalid"_f] = value;
}
inline bool get_no_permissions(const FileInfo &msg) {
    using namespace pp;
    return msg["no_permissions"_f].value_or(false);
}
inline void set_no_permissions(FileInfo &msg, bool value) {
    using namespace pp;
    msg["no_permissions"_f] = value;
}
inline const Vector &get_version(const FileInfo &msg) {
    using namespace pp;
    auto &opt = msg["version"_f];
    if (!opt) {
        using Opt = std::remove_cv_t<std::remove_reference_t<decltype(opt)>>;
        auto &mutable_opt = const_cast<Opt &>(opt);
        mutable_opt = Vector();
    }
    return opt.value();
}
inline Vector &get_version(FileInfo &msg) {
    using namespace pp;
    auto &opt = msg["version"_f];
    if (!opt) {
        opt = Vector();
    }
    return opt.value();
}
inline void set_version(FileInfo &msg, Vector value) {
    using namespace pp;
    msg["version"_f] = std::move(value);
}
inline std::int64_t get_sequence(const FileInfo &msg) {
    using namespace pp;
    return msg["sequence"_f].value_or(0);
}
inline void set_sequence(FileInfo &msg, std::int64_t value) {
    using namespace pp;
    msg["sequence"_f] = value;
}
inline std::int32_t get_block_size(const FileInfo &msg) {
    using namespace pp;
    return msg["block_size"_f].value_or(0);
}
inline void set_block_size(FileInfo &msg, std::int32_t value) {
    using namespace pp;
    msg["block_size"_f] = value;
}
inline std::size_t get_blocks_size(const FileInfo &msg) {
    using namespace pp;
    return msg["blocks"_f].size();
}
inline utils::bytes_view_t get_blocks(const FileInfo &msg, std::size_t i) {
    using namespace pp;
    return msg["blocks"_f][i];
}
inline void set_blocks(FileInfo &msg, std::size_t i, utils::bytes_view_t value) {
    using namespace pp;
    msg["blocks"_f][i] = utils::bytes_t(value.begin(), value.end());
}
inline void add_blocks(FileInfo &msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["blocks"_f].emplace_back(utils::bytes_t(value.begin(), value.end()));
}
inline std::string_view get_symlink_target(const FileInfo &msg) {
    using namespace pp;
    auto &opt = msg["symlink_target"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_symlink_target(FileInfo &msg, std::string_view value) {
    using namespace pp;
    msg["symlink_target"_f] = std::string(value);
}
inline void set_symlink_target(FileInfo &msg, std::string value) {
    using namespace pp;
    msg["symlink_target"_f] = std::move(value);
}

/**************/
/*** Folder ***/
/**************/

inline std::string_view get_id(const Folder &msg) {
    using namespace pp;
    auto &opt = msg["id"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_id(Folder &msg, std::string_view value) {
    using namespace pp;
    msg["id"_f] = std::string(value);
}
inline std::string_view get_label(const Folder &msg) {
    using namespace pp;
    auto &opt = msg["label"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_label(Folder &msg, std::string_view value) {
    using namespace pp;
    msg["label"_f] = std::string(value);
}
inline bool get_ignore_permissions(const Folder &msg) {
    using namespace pp;
    return msg["ignore_permissions"_f].value_or(false);
}
inline void set_ignore_permissions(Folder &msg, bool value) {
    using namespace pp;
    msg["ignore_permissions"_f] = value;
}
inline bool get_ignore_delete(const Folder &msg) {
    using namespace pp;
    return msg["ignore_delete"_f].value_or(false);
}
inline void set_ignore_delete(Folder &msg, bool value) {
    using namespace pp;
    msg["ignore_delete"_f] = value;
}
inline bool get_disable_temp_indexes(const Folder &msg) {
    using namespace pp;
    return msg["disable_temp_indexes"_f].value_or(false);
}
inline void set_disable_temp_indexes(Folder &msg, bool value) {
    using namespace pp;
    msg["disable_temp_indexes"_f] = value;
}
inline bool get_paused(const Folder &msg) {
    using namespace pp;
    return msg["paused"_f].value_or(false);
}
inline void set_paused(Folder &msg, bool value) {
    using namespace pp;
    msg["paused"_f] = value;
}
inline bool get_scheduled(const Folder &msg) {
    using namespace pp;
    return msg["scheduled"_f].value_or(false);
}
inline void set_scheduled(Folder &msg, bool value) {
    using namespace pp;
    msg["scheduled"_f] = value;
}
inline std::string_view get_path(const Folder &msg) {
    using namespace pp;
    auto &opt = msg["path"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_path(Folder &msg, std::string_view value) {
    using namespace pp;
    msg["path"_f] = std::string(value);
}
template <typename T = void> inline void set_path(Folder &msg, std::string value) {
    using namespace pp;
    msg["path"_f] = std::move(value);
}
inline FolderType get_folder_type(const Folder &msg) {
    using namespace pp;
    return msg["folder_type"_f].value_or(FolderType{});
}
inline void set_folder_type(Folder &msg, FolderType value) {
    using namespace pp;
    msg["folder_type"_f] = value;
}
inline PullOrder get_pull_order(const Folder &msg) {
    using namespace pp;
    return msg["pull_order"_f].value_or(PullOrder{});
}
inline void set_pull_order(Folder &msg, PullOrder value) {
    using namespace pp;
    msg["pull_order"_f] = value;
}
inline std::uint32_t get_rescan_interval(const Folder &msg) {
    using namespace pp;
    return msg["rescan_interval"_f].value_or(0);
}
inline void set_rescan_interval(Folder &msg, std::uint32_t value) {
    using namespace pp;
    msg["rescan_interval"_f] = value;
}

/******************/
/*** FolderInfo ***/
/******************/

inline std::uint64_t get_index_id(const FolderInfo &msg) {
    using namespace pp;
    return msg["index_id"_f].value_or(0);
}
inline void set_index_id(FolderInfo &msg, std::uint64_t value) {
    using namespace pp;
    msg["index_id"_f] = value;
}
inline std::int64_t get_max_sequence(const FolderInfo &msg) {
    using namespace pp;
    return msg["max_sequence"_f].value_or(0);
}
inline void set_max_sequence(FolderInfo &msg, std::int64_t value) {
    using namespace pp;
    msg["max_sequence"_f] = value;
}
inline utils::bytes_view_t get_introducer_device_key(const FolderInfo &msg) {
    using namespace pp;
    auto &opt = msg["introducer_device_key"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_introducer_device_key(FolderInfo &msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["introducer_device_key"_f] = utils::bytes_t{value.begin(), value.end()};
}

/*********************/
/*** IgnoredFolder ***/
/*********************/

inline std::string_view get_label(const IgnoredFolder &msg) {
    using namespace pp;
    auto &opt = msg["label"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_label(IgnoredFolder &msg, std::string_view value) {
    using namespace pp;
    msg["label"_f] = std::string(value);
}

/*********************/
/*** PendingFolder ***/
/*********************/

inline const Folder &get_folder(const PendingFolder &msg) {
    using namespace pp;
    auto &opt = msg["folder"_f];
    if (!opt) {
        using Opt = std::remove_cv_t<std::remove_reference_t<decltype(opt)>>;
        auto &mutable_opt = const_cast<Opt &>(opt);
        mutable_opt = Folder();
    }
    return opt.value();
}
inline Folder &get_folder(PendingFolder &msg) {
    using namespace pp;
    auto &opt = msg["folder"_f];
    if (!opt) {
        opt = Folder();
    }
    return opt.value();
}
inline void set_folder(PendingFolder &msg, Folder value) {
    using namespace pp;
    msg["folder"_f] = std::move(value);
}
inline const FolderInfo &get_folder_info(const PendingFolder &msg) {
    using namespace pp;
    auto &opt = msg["folder_info"_f];
    if (!opt) {
        using Opt = std::remove_cv_t<std::remove_reference_t<decltype(opt)>>;
        auto &mutable_opt = const_cast<Opt &>(opt);
        mutable_opt = FolderInfo();
    }
    return opt.value();
}
inline FolderInfo &get_folder_info(PendingFolder &msg) {
    using namespace pp;
    auto &opt = msg["folder_info"_f];
    if (!opt) {
        opt = FolderInfo();
    }
    return opt.value();
}
inline void set_folder_info(PendingFolder &msg, FolderInfo value) {
    using namespace pp;
    msg["folder_info"_f] = std::move(value);
}

/******************/
/*** SomeDevice ***/
/******************/

inline std::string_view get_name(const SomeDevice &msg) {
    using namespace pp;
    auto &opt = msg["name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_name(SomeDevice &msg, std::string_view value) {
    using namespace pp;
    msg["name"_f] = std::string(value);
}
inline std::string_view get_client_name(const SomeDevice &msg) {
    using namespace pp;
    auto &opt = msg["client_name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_client_name(SomeDevice &msg, std::string_view value) {
    using namespace pp;
    msg["client_name"_f] = std::string(value);
}
inline std::string_view get_client_version(const SomeDevice &msg) {
    using namespace pp;
    auto &opt = msg["client_version"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_client_version(SomeDevice &msg, std::string_view value) {
    using namespace pp;
    msg["client_version"_f] = std::string(value);
}
inline std::string_view get_address(const SomeDevice &msg) {
    using namespace pp;
    auto &opt = msg["address"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_address(SomeDevice &msg, std::string_view value) {
    using namespace pp;
    msg["address"_f] = std::string(value);
}
template <typename T = void> inline void set_address(SomeDevice &msg, std::string value) {
    using namespace pp;
    msg["address"_f] = std::move(value);
}
inline std::int64_t get_last_seen(const SomeDevice &msg) {
    using namespace pp;
    return msg["last_seen"_f].value_or(0);
}
inline void set_last_seen(SomeDevice &msg, std::int64_t value) {
    using namespace pp;
    msg["last_seen"_f] = value;
}

} // namespace syncspirit::db
