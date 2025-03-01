// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "proto-fwd.hpp"

namespace syncspirit::proto {

std::size_t estimate(const Announce&);
std::size_t estimate(const BlockInfo&);
std::size_t estimate(const Close&);
std::size_t estimate(const ClusterConfig&);
std::size_t estimate(const Device&);
std::size_t estimate(const DownloadProgress&);
std::size_t estimate(const FileDownloadProgressUpdate&);
std::size_t estimate(const FileInfo&);
std::size_t estimate(const Folder&);
std::size_t estimate(const Header&);
std::size_t estimate(const Header&);
std::size_t estimate(const Hello&);
std::size_t estimate(const IndexBase&);
std::size_t estimate(const Ping&);
std::size_t estimate(const Request&);
std::size_t estimate(const Response&);

void encode(const BlockInfo&, void*);
void encode(const Close&, void*);
void encode(const ClusterConfig&, void*);
void encode(const Device&, void*);
void encode(const DownloadProgress&, void*);
void encode(const FileDownloadProgressUpdate&, void*);
void encode(const FileInfo&, void*);
void encode(const Folder&, void*);
void encode(const Header&, void*);
void encode(const IndexBase&, void*);
void encode(const Ping&, void*);
void encode(const Request&, void*);
void encode(const Response&, void*);

utils::bytes_t encode(const Hello&, std::size_t prefix);
utils::bytes_t encode(const Announce);

bool decode(utils::bytes_view_t, Announce&);
bool decode(utils::bytes_view_t, BlockInfo&);
bool decode(utils::bytes_view_t, Close&);
bool decode(utils::bytes_view_t, ClusterConfig&);
bool decode(utils::bytes_view_t, Device&);
bool decode(utils::bytes_view_t, DownloadProgress&);
bool decode(utils::bytes_view_t, FileDownloadProgressUpdate&);
bool decode(utils::bytes_view_t, FileInfo&);
bool decode(utils::bytes_view_t, Folder&);
bool decode(utils::bytes_view_t, Header&);
bool decode(utils::bytes_view_t, Hello&);
bool decode(utils::bytes_view_t, IndexBase&);
bool decode(utils::bytes_view_t, Ping&);
bool decode(utils::bytes_view_t, Request&);
bool decode(utils::bytes_view_t, Response&);

/****************/
/*** Announce ***/
/****************/

inline utils::bytes_view_t get_id(const Announce& msg) {
    using namespace pp;
    auto& opt = msg["id"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_id(Announce& msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["id"_f] = utils::bytes_t{value.begin(), value.end()};
}
inline std::size_t get_addresses_size(const Announce& msg) {
    using namespace pp;
    return msg["addresses"_f].size();
}
inline std::string_view get_addresses(const Announce& msg, std::size_t i) {
    using namespace pp;
    return msg["addresses"_f][i];
}
inline void set_addresses(Announce& msg, std::size_t i, std::string_view value) {
    using namespace pp;
    msg["addresses"_f][i] = value;
}
inline void add_addresses(Announce& msg, std::string_view value) {
    using namespace pp;
    msg["addresses"_f].emplace_back(value);
}
inline std::uint64_t get_instance_id(const Announce& msg) {
    using namespace pp;
    return msg["instance_id"_f].value_or(0);
}
inline void set_instance_id(Announce& msg, std::uint64_t value) {
    using namespace pp;
    msg["instance_id"_f] = value;
}

/*****************/
/*** BlockInfo ***/
/*****************/

inline std::int64_t get_offset(BlockInfo& msg) {
    using namespace pp;
    return msg["offset"_f].value_or(0);
}

inline void set_offset(BlockInfo& msg, std::int64_t value) {
    using namespace pp;
    msg["offset"_f] = value;
}
inline std::int32_t get_size(const BlockInfo& msg) {
    using namespace pp;
    return msg["size"_f].value_or(0);
}
inline void set_size(BlockInfo& msg, std::int32_t value) {
    using namespace pp;
    msg["size"_f] = value;
}
inline utils::bytes_view_t get_hash(const BlockInfo& msg) {
    using namespace pp;
    auto& opt = msg["hash"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_hash(BlockInfo& msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["hash"_f] = utils::bytes_t{value.begin(), value.end()};
}
inline std::uint32_t get_weak_hash(const BlockInfo& msg){
    using namespace pp;
    return msg["weak_hash"_f].value_or(0);
}
inline void set_weak_hash(BlockInfo& msg, std::uint32_t value){
    using namespace pp;
    msg["weak_hash"_f] = value;
}

/*************/
/*** Close ***/
/*************/

inline std::string_view get_reason(const Close& msg) {
    using namespace pp;
    auto& opt = msg["reason"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_reason(Close& msg, std::string value) {
    using namespace pp;
    msg["reason"_f] = std::move(value);
}

/*********************/
/*** ClusterConfig ***/
/*********************/

inline std::size_t get_folders_size(const ClusterConfig& msg) {
    using namespace pp;
    return msg["folders"_f].size();
}
inline const Folder& get_folders(const ClusterConfig& msg, std::size_t i) {
    using namespace pp;
    return msg["folders"_f][i];
}
inline void set_folders(ClusterConfig& msg, std::size_t i, Folder value) {
    using namespace pp;
    msg["folders"_f][i] = std::move(value);
}
inline void add_folders(ClusterConfig& msg, Folder value) {
    using namespace pp;
    msg["folders"_f].emplace_back(std::move(value));
}
inline Folder& add_folders(ClusterConfig& msg) {
    using namespace pp;
    auto& opt = msg["folders"_f];
    opt.emplace_back(Folder());
    return opt.back();
}

/***************/
/*** Counter ***/
/***************/

inline std::uint64_t get_id(const Counter& msg) {
    using namespace pp;
    return msg["id"_f].value_or(0);
}
inline void set_id(Counter& msg, std::uint64_t value) {
    using namespace pp;
    msg["id"_f] = value;
}
inline std::uint64_t get_value(const Counter& msg) {
    using namespace pp;
    return msg["value"_f].value_or(0);
}
inline void set_value(Counter& msg, std::uint64_t value) {
    using namespace pp;
    msg["value"_f] = value;
}

/**************/
/*** Device ***/
/**************/

inline utils::bytes_view_t get_id(const Device& msg) {
    using namespace pp;
    auto& opt = msg["id"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_id(Device& msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["id"_f] = utils::bytes_t{value.begin(), value.end()};
}
inline std::string_view get_name(const Device& msg) {
    using namespace pp;
    auto& opt = msg["name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_name(Device& msg, std::string_view value) {
    using namespace pp;
    msg["name"_f] = std::string(value);
}
inline std::size_t get_addresses_size(const Device& msg) {
    using namespace pp;
    return msg["addresses"_f].size();
}
inline std::string_view get_addresses(const Device& msg, std::size_t i) {
    using namespace pp;
    return msg["addresses"_f][i];
}
inline void set_addresses(Device& msg, std::size_t i, std::string_view value) {
    using namespace pp;
    msg["addresses"_f][i] = std::string(value);
}
inline Compression get_compression(const Device& msg) {
    using namespace pp;
    return msg["compression"_f].value_or(Compression{});
}
inline void set_compression(Device& msg, Compression value) {
    using namespace pp;
    msg["compression"_f] = value;
}
inline std::string_view get_cert_name(const Device& msg) {
    using namespace pp;
    auto& opt = msg["cert_name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_cert_name(Device& msg, std::string_view value) {
    using namespace pp;
    msg["cert_name"_f] = std::string(value);
}
inline std::int64_t get_max_sequence(const Device& msg) {
    using namespace pp;
    return msg["max_sequence"_f].value_or(0);
}
inline void set_max_sequence(Device& msg, std::int64_t value) {
    using namespace pp;
    msg["max_sequence"_f] = value;
}
inline bool get_introducer(const Device& msg) {
    using namespace pp;
    return msg["introducer"_f].value_or(false);
}
inline void set_introducer(Device& msg, bool value) {
    using namespace pp;
    msg["introducer"_f] = value;
}
inline std::uint64_t get_index_id(const Device& msg) {
    using namespace pp;
    return msg["index_id"_f].value_or(0);
}
inline void set_index_id(Device& msg, std::uint64_t value) {
    using namespace pp;
    msg["index_id"_f] = value;
}
inline bool get_skip_introduction_removals(const Device& msg) {
    using namespace pp;
    return msg["skip_introduction_removals"_f].value_or(false);
}
inline void set_skip_introduction_removals(Device& msg, bool value) {
    using namespace pp;
    msg["skip_introduction_removals"_f] = value;
}

/****************/
/*** FileInfo ***/
/****************/

inline std::string_view get_name(const FileInfo& msg) {
    using namespace pp;
    auto& opt = msg["name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_name(FileInfo& msg, std::string_view value) {
    using namespace pp;
    msg["name"_f] = std::string(value);
}
inline FileInfoType get_type(const FileInfo& msg) {
    using namespace pp;
    return msg["type"_f].value_or(FileInfoType{});
}
inline void set_type(FileInfo& msg, FileInfoType value) {
    using namespace pp;
    msg["type"_f] = value;
}
inline std::int64_t get_size(const FileInfo& msg) {
    using namespace pp;
    return msg["size"_f].value_or(0);
}
inline void set_size(FileInfo& msg, std::int64_t value) {
    using namespace pp;
    msg["size"_f] = value;
}
inline std::uint32_t get_permissions(const FileInfo& msg) {
    using namespace pp;
    return msg["permissions"_f].value_or(0);
}
inline void set_permissions(FileInfo& msg, std::uint32_t value) {
    using namespace pp;
    msg["permissions"_f] = value;
}
inline std::int64_t get_modified_s(const FileInfo& msg) {
    using namespace pp;
    return msg["modified_s"_f].value_or(0);
}
inline void set_modified_s(FileInfo& msg, std::int64_t value) {
    using namespace pp;
    msg["modified_s"_f] = value;
}
inline std::int32_t get_modified_ns(const FileInfo& msg) {
    using namespace pp;
    return msg["modified_ns"_f].value_or(0);
}
inline void set_modified_ns(FileInfo& msg, std::int32_t value) {
    using namespace pp;
    msg["modified_ns"_f] = value;
}
inline std::uint64_t get_modified_by(const FileInfo& msg) {
    using namespace pp;
    return msg["modified_by"_f].value_or(0);
}
inline void set_modified_by(FileInfo& msg, std::uint64_t value) {
    using namespace pp;
    msg["modified_by"_f] = value;
}
inline bool get_deleted(const FileInfo& msg) {
    using namespace pp;
    return msg["deleted"_f].value_or(false);
}
inline void set_deleted(FileInfo& msg, bool value) {
    using namespace pp;
    msg["deleted"_f] = value;
}
inline bool get_invalid(const FileInfo& msg) {
    using namespace pp;
    return msg["invalid"_f].value_or(false);
}
inline void set_invalid(FileInfo& msg, bool value) {
    using namespace pp;
    msg["invalid"_f] = value;
}
inline bool get_no_permissions(const FileInfo& msg) {
    using namespace pp;
    return msg["no_permissions"_f].value_or(false);
}
inline void set_no_permissions(FileInfo& msg, bool value) {
    using namespace pp;
    msg["no_permissions"_f] = value;
}
inline const Vector& get_version(const FileInfo& msg) {
    using namespace pp;
    auto& opt = msg["version"_f];
    if (!opt) {
        using Opt = std::remove_cv_t<std::remove_reference_t<decltype(opt)>>;
        auto& mutable_opt = const_cast<Opt&>(opt);
        mutable_opt = Vector();
    }
        return opt.value();
}
inline Vector& get_version(FileInfo& msg) {
    using namespace pp;
    auto& opt = msg["version"_f];
    if (!opt) {
        opt = Vector();
    }
        return opt.value();
}
inline void set_version(FileInfo& msg, Vector value) {
    using namespace pp;
    msg["version"_f] = std::move(value);
}
inline std::int64_t get_sequence(const FileInfo& msg) {
    using namespace pp;
    return msg["sequence"_f].value_or(0);
}
inline void set_sequence(FileInfo& msg, std::int64_t value) {
    using namespace pp;
    msg["sequence"_f] = value;
}
inline std::int32_t get_block_size(const FileInfo& msg) {
    using namespace pp;
    return msg["block_size"_f].value_or(0);
}
inline void set_block_size(FileInfo& msg, std::int32_t value) {
    using namespace pp;
    msg["block_size"_f] = value;
}
inline std::size_t get_blocks_size(const FileInfo& msg) {
    using namespace pp;
    return msg["blocks"_f].size();
}
inline const BlockInfo& get_blocks(const FileInfo& msg, std::size_t i) {
    using namespace pp;
    return msg["blocks"_f][i];
}
inline void set_blocks(FileInfo& msg, std::size_t i, BlockInfo block) {
    using namespace pp;
    msg["blocks"_f][i] = std::move(block);
}
inline void add_blocks(FileInfo& msg, BlockInfo block) {
    using namespace pp;
    msg["blocks"_f].emplace_back(std::move(block));
}
inline BlockInfo& add_blocks(FileInfo& msg) {
    using namespace pp;
    auto& opt = msg["blocks"_f];
    opt.emplace_back(BlockInfo());
    return opt.back();
}
inline void clear_blocks(FileInfo& msg) {
    using namespace pp;
    msg["blocks"_f].clear();
}
inline std::string_view get_symlink_target(const FileInfo& msg) {
    using namespace pp;
    auto& opt = msg["symlink_target"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_symlink_target(FileInfo& msg, std::string_view value) {
    using namespace pp;
    msg["symlink_target"_f] = std::string(value);
}
template<typename T = void>
inline void set_symlink_target(FileInfo& msg, std::string value) {
    using namespace pp;
    msg["symlink_target"_f] = std::move(value);
}

/**************/
/*** Folder ***/
/**************/

inline std::string_view get_id(const Folder& msg) {
    using namespace pp;
    auto& opt = msg["id"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_id(Folder& msg, std::string_view value) {
    using namespace pp;
    msg["id"_f] = std::string(value);
}
inline std::string_view get_label(const Folder& msg) {
    using namespace pp;
    auto& opt = msg["label"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_label(Folder& msg, std::string_view value) {
    using namespace pp;
    msg["label"_f] = std::string(value);
}
inline bool get_read_only(const Folder& msg) {
    using namespace pp;
    return msg["read_only"_f].value_or(false);
}
inline void set_read_only(Folder& msg, bool value) {
    using namespace pp;
    msg["read_only"_f] = value;
}
inline bool get_ignore_permissions(const Folder& msg) {
    using namespace pp;
    return msg["ignore_permissions"_f].value_or(false);
}
inline void set_ignore_permissions(Folder& msg, bool value) {
    using namespace pp;
    msg["ignore_permissions"_f] = value;
}
inline bool get_ignore_delete(const Folder& msg) {
    using namespace pp;
    return msg["ignore_delete"_f].value_or(false);
}
inline void set_ignore_delete(Folder& msg, bool value) {
    using namespace pp;
    msg["ignore_delete"_f] = value;
}
inline bool get_disable_temp_indexes(const Folder& msg) {
    using namespace pp;
    return msg["disable_temp_indexes"_f].value_or(false);
}
inline void set_disable_temp_indexes(Folder& msg, bool value) {
    using namespace pp;
    msg["disable_temp_indexes"_f] = value;
}
inline bool get_paused(const Folder& msg) {
    using namespace pp;
    return msg["paused"_f].value_or(false);
}
inline void set_paused(Folder& msg, bool value) {
    using namespace pp;
    msg["paused"_f] = value;
}
inline std::size_t get_devices_size(const Folder& msg) {
    using namespace pp;
    return msg["devices"_f].size();
}
inline const Device& get_devices(const Folder& msg, std::size_t i) {
    using namespace pp;
    return msg["devices"_f][i];
}
inline void set_devices(Folder& msg, std::size_t i, Device value) {
    using namespace pp;
    msg["devices"_f][i] = std::move(value);
}
inline void add_devices(Folder& msg, Device value) {
    using namespace pp;
    msg["devices"_f].emplace_back(std::move(value));
}
inline Device& add_devices(Folder& msg) {
    using namespace pp;
    auto& opt = msg["devices"_f];
    opt.emplace_back(Device());
    return opt.back();
}

/**************/
/*** Header ***/
/**************/

inline MessageType get_type(const Header& msg) {
    using namespace pp;
    return msg["type"_f].value_or(MessageType{});
}
inline void set_type(Header& msg, MessageType value) {
    using namespace pp;
    msg["type"_f] = value;
}
inline MessageCompression get_compression(const Header& msg) {
    using namespace pp;
    return msg["compression"_f].value_or(MessageCompression{});
}
inline void set_compression(Header& msg, MessageCompression value) {
    using namespace pp;
    msg["compression"_f] = value;
}

/*************/
/*** Hello ***/
/*************/

inline std::string_view get_device_name(const Hello& msg) {
    using namespace pp;
    auto& opt = msg["device_name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_device_name(Hello& msg, std::string_view value) {
    using namespace pp;
    msg["device_name"_f] = std::string(value);
}
inline std::string_view get_client_name(const Hello& msg) {
    using namespace pp;
    auto& opt = msg["client_name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_client_name(Hello& msg, std::string_view value) {
    using namespace pp;
    msg["client_name"_f] = std::string(value);
}
inline std::string_view get_client_version(const Hello& msg) {
    using namespace pp;
    auto& opt = msg["client_version"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_client_version(Hello& msg, std::string_view value) {
    using namespace pp;
    msg["client_version"_f] = std::string(value);
}

/*****************/
/*** IndexBase ***/
/*****************/

inline std::string_view get_folder(const IndexBase& msg) {
    using namespace pp;
    auto& opt = msg["folder"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_folder(IndexBase& msg, std::string_view value) {
    using namespace pp;
    msg["folder"_f] = std::string(value);
}
inline std::size_t get_files_size(const IndexBase& msg) {
    using namespace pp;
    return msg["files"_f].size();
}
inline const FileInfo& get_files(const IndexBase& msg, std::size_t i) {
    using namespace pp;
    return msg["files"_f][i];
}
inline void add_files(IndexBase& msg, FileInfo value) {
    using namespace pp;
    msg["files"_f].emplace_back(std::move(value));
}

/***************/
/*** Request ***/
/***************/

inline std::int32_t get_id(const Request& msg) {
    using namespace pp;
    return msg["id"_f].value_or(0);
}
inline void set_id(Request& msg, std::int32_t value) {
    using namespace pp;
    msg["id"_f] = value;
}
inline std::string_view get_folder(const Request& msg) {
    using namespace pp;
    auto& opt = msg["folder"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_folder(Request& msg, std::string_view value) {
    using namespace pp;
    msg["folder"_f] = std::string(value);
}
inline std::string_view get_name(const Request& msg) {
    using namespace pp;
    auto& opt = msg["name"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_name(Request& msg, std::string_view value) {
    using namespace pp;
    msg["name"_f] = std::string(value);
}
inline std::int32_t get_offset(const Request& msg) {
    using namespace pp;
    return msg["offset"_f].value_or(0);
}
inline void set_offset(Request& msg, std::int32_t value) {
    using namespace pp;
    msg["offset"_f] = value;
}
inline std::int32_t get_size(const Request& msg) {
    using namespace pp;
    return msg["size"_f].value_or(0);
}
inline void set_size(Request& msg, std::int32_t value) {
    using namespace pp;
    msg["size"_f] = value;
}
inline utils::bytes_view_t get_hash(const Request& msg) {
    using namespace pp;
    auto& opt = msg["hash"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_hash(Request& msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["hash"_f] = utils::bytes_t{value.begin(), value.end()};
}
inline bool get_from_temporary(const Request& msg) {
    using namespace pp;
    return msg["from_temporary"_f].value_or(false);
}
inline void set_from_temporary(Request& msg, bool value) {
    using namespace pp;
    msg["from_temporary"_f] = value;
}
inline std::uint32_t get_weak_hash(const Request& msg) {
    using namespace pp;
    return msg["weak_hash"_f].value_or(0);
}
inline void set_weak_hash(Request& msg, std::uint32_t value) {
    using namespace pp;
    msg["weak_hash"_f] = value;
}

/****************/
/*** Response ***/
/****************/

inline std::int32_t get_id(const Response& msg) {
    using namespace pp;
    return msg["id"_f].value_or(0);
}
inline void set_id(Response& msg, std::int32_t value) {
    using namespace pp;
    msg["id"_f] = value;
}
inline utils::bytes_view_t get_data(const Response& msg) {
    using namespace pp;
    auto& opt = msg["data"_f];
    if (opt) {
        return opt.value();
    }
    return {};
}
inline void set_data(Response& msg, utils::bytes_view_t value) {
    using namespace pp;
    msg["data"_f] = utils::bytes_t{value.begin(), value.end()};
}
inline void set_data(Response& msg, utils::bytes_t value) {
    using namespace pp;
    msg["data"_f] = std::move(value);
}
inline ErrorCode get_code(const Response& msg) {
    using namespace pp;
    return msg["code"_f].value_or(ErrorCode{});
}
inline void set_code(Response& msg, ErrorCode value) {
    using namespace pp;
    msg["code"_f] = value;
}

/**************/
/*** Vector ***/
/**************/

inline std::size_t get_counters_size(const Vector& msg) {
    using namespace pp;
    return msg["counters"_f].size();
}
inline const Counter& get_counters(const Vector& msg, std::size_t i) {
    using namespace pp;
    return msg["counters"_f][i];
}
inline Counter& get_counters(Vector& msg, std::size_t i) {
    using namespace pp;
    return msg["counters"_f][i];
}
inline void clear_counters(Vector& msg) {
    using namespace pp;
    msg["counters"_f].clear();
}
inline void set_counters(Vector& msg, std::size_t i, Counter value) {
    using namespace pp;
    msg["counters"_f][i] = std::move(value);
}
inline void add_counters(Vector& msg, Counter value) {
    using namespace pp;
    msg["counters"_f].emplace_back(std::move(value));
}
inline Counter& add_counters(Vector& msg) {
    using namespace pp;
    auto& opt = msg["counters"_f];
    opt.emplace_back(Counter());
    return opt.back();
}

}
