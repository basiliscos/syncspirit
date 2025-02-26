#pragma once

// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <protopuf/message.h>
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include <fmt/format.h>
#include <cstdint>


namespace syncspirit{

namespace proto {

using bytes_backend_t = std::vector<std::vector<unsigned char>>;

// clang-format off

enum class MessageType {
    CLUSTER_CONFIG    = 0,
    INDEX             = 1,
    INDEX_UPDATE      = 2,
    REQUEST           = 3,
    RESPONSE          = 4,
    DOWNLOAD_PROGRESS = 5,
    PING              = 6,
    CLOSE             = 7,
};

enum class MessageCompression {
    NONE = 0,
    LZ4  = 1,
};

enum class Compression {
    METADATA = 0,
    NEVER    = 1,
    ALWAYS   = 2,
};

enum class FileInfoType {
    FILE              = 0,
    DIRECTORY         = 1,
    SYMLINK_FILE      = 2,
    SYMLINK_DIRECTORY = 3,
    SYMLINK           = 4,
};

enum class ErrorCode {
    NO_BEP_ERROR = 0,
    GENERIC      = 1,
    NO_SUCH_FILE = 2,
    INVALID_FILE = 3,
};

enum class FileDownloadProgressUpdateType {
    APPEND = 0,
    FORGET = 1,
};

using Announce = pp::message<
    pp::bytes_field  <"id",          1              >,
    pp::string_field <"addresses",   1, pp::repeated>,
    pp::uint64_field <"instance_id", 3              >
>;

utils::bytes_view_t SYNCSPIRIT_API get_id(const Announce&);
void                SYNCSPIRIT_API set_id(Announce&, utils::bytes_view_t value);
std::size_t         SYNCSPIRIT_API get_addresses_size(const Announce&);
std::string_view    SYNCSPIRIT_API get_addresses(const Announce&, std::size_t i);
void                SYNCSPIRIT_API set_addresses(Announce&, std::size_t i, std::string_view);
void                SYNCSPIRIT_API add_addresses(Announce&, std::string_view);
std::uint64_t       SYNCSPIRIT_API get_instance_id(Announce&);
void                SYNCSPIRIT_API set_instance_id(Announce&, std::uint64_t value);

using Hello = pp::message<
    pp::string_field <"device_name",    1>,
    pp::string_field <"client_name",    2>,
    pp::string_field <"client_version", 3>
>;
std::string_view    SYNCSPIRIT_API get_device_name(const Hello&);
void                SYNCSPIRIT_API set_device_name(Hello&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_client_name(const Hello&);
void                SYNCSPIRIT_API set_client_name(Hello&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_client_version(const Hello&);
void                SYNCSPIRIT_API set_client_version(Hello&, std::string_view value);

using Header = pp::message<
    pp::enum_field <"type",        1, MessageType>,
    pp::enum_field <"compression", 2, MessageCompression>
>;
MessageType         SYNCSPIRIT_API get_type(const Header&);
void                SYNCSPIRIT_API set_type(Announce&, MessageType value);
MessageCompression  SYNCSPIRIT_API get_compression(const Header&);
void                SYNCSPIRIT_API set_compression(Announce&, MessageCompression value);

using Device = pp::message<
    pp::bytes_field  <"id",                         1              >,
    pp::string_field <"name",                       2              >,
    pp::string_field <"addresses",                  3, pp::repeated>,
    pp::enum_field   <"compression",                4, Compression >,
    pp::string_field <"cert_name",                  5              >,
    pp::int64_field  <"max_sequence",               6              >,
    pp::bool_field   <"introducer",                 7              >,
    pp::uint64_field <"index_id",                   8              >,
    pp::bool_field   <"skip_introduction_removals", 9              >
>;

utils::bytes_view_t SYNCSPIRIT_API get_id(const Device&);
void                SYNCSPIRIT_API set_id(Device&, utils::bytes_view_t value);
std::string_view    SYNCSPIRIT_API get_name(const Device&);
void                SYNCSPIRIT_API set_name(Device&, std::string_view value);
std::size_t         SYNCSPIRIT_API get_addresses_size(const Device&);
std::string_view    SYNCSPIRIT_API get_addresses(const Device&, std::size_t i);
void                SYNCSPIRIT_API set_addresses(Device&, std::size_t i, std::string_view);
Compression         SYNCSPIRIT_API get_compression(const Device&);
void                SYNCSPIRIT_API set_compression(Device&, Compression value);
std::string_view    SYNCSPIRIT_API get_cert_name(const Device&);
void                SYNCSPIRIT_API set_cert_name(Device&, std::string_view value);
std::int64_t        SYNCSPIRIT_API get_max_sequence(const Device&);
void                SYNCSPIRIT_API set_max_sequence(Device&, std::int64_t value);
bool                SYNCSPIRIT_API get_introducer(const Device&);
void                SYNCSPIRIT_API set_introducer(Device&, bool value);
std::uint64_t       SYNCSPIRIT_API get_index_id(const Device&);
void                SYNCSPIRIT_API set_index_id(Device&, std::uint64_t value);
bool                SYNCSPIRIT_API get_skip_introduction_removals(const Device&);
void                SYNCSPIRIT_API set_skip_introduction_removals(Device&, bool value);

using Folder = pp::message<
    pp::string_field    <"id",                    1                      >,
    pp::string_field    <"label",                 2                      >,
    pp::bool_field      <"read_only",             3                      >,
    pp::bool_field      <"ignore_permissions",    4                      >,
    pp::bool_field      <"ignore_delete",         5                      >,
    pp::bool_field      <"disable_temp_indexes",  6                      >,
    pp::bool_field      <"paused",                7                      >,
    pp::message_field   <"devices",              16, Device, pp::repeated>
>;

std::string_view    SYNCSPIRIT_API get_id(const Folder&);
void                SYNCSPIRIT_API set_id(Folder&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_label(const Folder&);
void                SYNCSPIRIT_API set_label(Folder&, std::string_view value);
bool                SYNCSPIRIT_API get_read_only(const Folder&);
void                SYNCSPIRIT_API set_read_only(Folder&, bool value);
bool                SYNCSPIRIT_API get_ignore_permissions(const Folder&);
void                SYNCSPIRIT_API set_ignore_permissions(Folder&, bool value);
bool                SYNCSPIRIT_API get_ignore_delete(const Folder&);
void                SYNCSPIRIT_API set_ignore_delete(Folder&, bool value);
bool                SYNCSPIRIT_API get_disable_temp_indexes(const Folder&);
void                SYNCSPIRIT_API set_disable_temp_indexes(Folder&, bool value);
bool                SYNCSPIRIT_API get_paused(const Folder&);
void                SYNCSPIRIT_API set_paused(Folder&, bool value);
std::size_t         SYNCSPIRIT_API get_devices_size(const Folder&);
Device&                            get_devices(const Folder&, std::size_t i);
void                SYNCSPIRIT_API set_devices(Folder&, std::size_t i, Device);
void                SYNCSPIRIT_API add_devices(Folder&, Device);


using ClusterConfig = pp::message<
    pp::message_field   <"folders", 1, Folder, pp::repeated>
>;
std::size_t         SYNCSPIRIT_API get_folders_size(const ClusterConfig&);
Folder&                            get_folders(const ClusterConfig&, std::size_t i);
void                SYNCSPIRIT_API set_folders(ClusterConfig&, std::size_t i, Folder);
void                SYNCSPIRIT_API add_folders(ClusterConfig&, Folder);

using Counter = pp::message<
    pp::uint64_field    <"id",    1>,
    pp::uint64_field    <"value", 2>
>;
std::uint64_t       SYNCSPIRIT_API get_id(const Counter&);
void                SYNCSPIRIT_API set_id(Counter&, std::uint64_t value);
std::uint64_t       SYNCSPIRIT_API get_value(const Counter&);
void                SYNCSPIRIT_API set_value(Counter&, std::uint64_t value);

using Vector = pp::message<
    pp::message_field   <"counters", 1, Counter, pp::repeated>
>;
std::size_t         SYNCSPIRIT_API get_counters_size(const Vector&);
Counter&                           get_counters(const Vector&, std::size_t i);
void                SYNCSPIRIT_API clear_counters(Vector&);
void                SYNCSPIRIT_API set_counters(Vector&, std::size_t i, Counter);
void                SYNCSPIRIT_API add_counters(Vector&, Counter);

using BlockInfo = pp::message<
    pp::int64_field    <"offset",    1>,
    pp::int32_field    <"size",      2>,
    pp::bytes_field    <"hash",      3>,
    pp::uint32_field   <"weak_hash", 4>
>;

std::int64_t        SYNCSPIRIT_API get_offset(BlockInfo&);
void                SYNCSPIRIT_API set_offset(BlockInfo&, std::int64_t value);
std::int32_t        SYNCSPIRIT_API get_size(const BlockInfo&);
void                SYNCSPIRIT_API set_size(BlockInfo&, std::int32_t value);
utils::bytes_view_t SYNCSPIRIT_API get_hash(const BlockInfo&);
void                SYNCSPIRIT_API set_hash(BlockInfo&, utils::bytes_view_t value);
std::uint32_t       SYNCSPIRIT_API get_weak_hash(const BlockInfo&);
void                SYNCSPIRIT_API set_weak_hash(BlockInfo&, std::uint32_t value);


using FileInfo = pp::message<
    pp::string_field    <"name",            1                           >,
    pp::enum_field      <"type",            2, FileInfoType             >,
    pp::int64_field     <"size",            3                           >,
    pp::uint32_field    <"permissions",     4                           >,
    pp::int64_field     <"modified_s",      5                           >,
    pp::int32_field     <"modified_ns",     11                          >,
    pp::uint64_field    <"modified_by",     12                          >,
    pp::bool_field      <"deleted",         6                           >,
    pp::bool_field      <"invalid",         7                           >,
    pp::bool_field      <"no_permissions",  8                           >,
    pp::message_field   <"version",         9, Vector                   >,
    pp::int64_field     <"sequence",        10                          >,
    pp::int32_field     <"block_size",      13                          >,
    pp::message_field   <"blocks",          16, BlockInfo, pp::repeated >,
    pp::string_field    <"symlink_target",  17                          >
>;

std::string_view    SYNCSPIRIT_API get_name(const FileInfo&);
void                SYNCSPIRIT_API set_name(FileInfo&, std::string_view value);
FileInfoType        SYNCSPIRIT_API get_type(const FileInfo&);
void                SYNCSPIRIT_API set_type(FileInfo&, FileInfoType value);
std::int64_t        SYNCSPIRIT_API get_size(const FileInfo&);
void                SYNCSPIRIT_API set_size(FileInfo&, std::int64_t value);
std::uint32_t       SYNCSPIRIT_API get_permissions(const FileInfo&);
void                SYNCSPIRIT_API set_permissions(FileInfo&, std::uint32_t value);
std::int64_t        SYNCSPIRIT_API get_modified_s(const FileInfo&);
void                SYNCSPIRIT_API set_modified_s(FileInfo&, std::int64_t value);
std::int32_t        SYNCSPIRIT_API get_modified_ns(const FileInfo&);
void                SYNCSPIRIT_API set_modified_ns(FileInfo&, std::int32_t value);
std::uint64_t       SYNCSPIRIT_API get_modified_by(const FileInfo&);
void                SYNCSPIRIT_API set_modified_by(FileInfo&, std::uint64_t value);
bool                SYNCSPIRIT_API get_deleted(const FileInfo&);
void                SYNCSPIRIT_API set_deleted(FileInfo&, bool value);
bool                SYNCSPIRIT_API get_invalid(const FileInfo&);
void                SYNCSPIRIT_API set_invalid(FileInfo&, bool value);
bool                SYNCSPIRIT_API get_no_permissions(const FileInfo&);
void                SYNCSPIRIT_API set_no_permissions(FileInfo&, bool value);
Vector&                            get_version(const FileInfo&);
void                SYNCSPIRIT_API set_version(FileInfo&, Vector value);
std::int64_t        SYNCSPIRIT_API get_sequence(const FileInfo&);
void                SYNCSPIRIT_API set_sequence(FileInfo&, std::int64_t value);
std::int32_t        SYNCSPIRIT_API get_block_size(const FileInfo&);
void                SYNCSPIRIT_API set_block_size(FileInfo&, std::int32_t value);
std::size_t         SYNCSPIRIT_API get_blocks_size(const FileInfo&);
BlockInfo&                         get_blocks(const FileInfo&, std::size_t i);
void                SYNCSPIRIT_API set_blocks(FileInfo&, std::size_t i, BlockInfo);
void                SYNCSPIRIT_API add_blocks(FileInfo&, BlockInfo);
std::string_view    SYNCSPIRIT_API get_symlink_target(const FileInfo&);
void                SYNCSPIRIT_API set_symlink_target(FileInfo&, std::string_view value);
void                SYNCSPIRIT_API set_symlink_target(FileInfo&, std::string value);


using IndexBase = pp::message<
    pp::string_field    <"folder",  1                        >,
    pp::message_field   <"files",   2, FileInfo, pp::repeated>
>;

std::string_view    SYNCSPIRIT_API get_folder(const IndexBase&);
void                SYNCSPIRIT_API set_folder(IndexBase&, std::string_view value);
std::size_t         SYNCSPIRIT_API get_files_size(const IndexBase&);
FileInfo&                          get_files(const IndexBase&, std::size_t i);
void                SYNCSPIRIT_API set_files(IndexBase&, std::size_t i, FileInfo);
void                SYNCSPIRIT_API add_files(IndexBase&, FileInfo);

struct Index: IndexBase {};
struct IndexUpdate: IndexBase {};

using Request = pp::message<
    pp::int32_field     <"id",              1>,
    pp::string_field    <"folder",          2>,
    pp::string_field    <"name",            3>,
    pp::int64_field     <"offset",          4>,
    pp::int32_field     <"size",            5>,
    pp::bytes_field     <"hash",            6>,
    pp::bool_field      <"from_temporary",  7>,
    pp::uint32_field    <"weak_hash",       8>
>;

std::int32_t        SYNCSPIRIT_API get_id(const Request&);
void                SYNCSPIRIT_API set_id(Request&, std::int32_t value);
std::string_view    SYNCSPIRIT_API get_folder(const Request&);
void                SYNCSPIRIT_API set_folder(Request&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_name(const Request&);
void                SYNCSPIRIT_API set_name(Request&, std::string_view value);
std::int64_t        SYNCSPIRIT_API get_offset(const Request&);
void                SYNCSPIRIT_API set_offset(Request&, std::int64_t value);
std::int32_t        SYNCSPIRIT_API get_size(const Request&);
void                SYNCSPIRIT_API set_size(Request&, std::int32_t value);
utils::bytes_view_t SYNCSPIRIT_API get_hash(const Request&);
void                SYNCSPIRIT_API set_hash(Request&, utils::bytes_view_t value);
bool                SYNCSPIRIT_API get_from_temporary(const Request&);
void                SYNCSPIRIT_API set_from_temporary(Request&, bool value);
std::int32_t        SYNCSPIRIT_API get_weak_hash(const Request&);
void                SYNCSPIRIT_API set_weak_hash(Request&, std::int32_t value);

using Response = pp::message<
    pp::int32_field     <"id",      1>,
    pp::bytes_field     <"data",    2>,
    pp::enum_field      <"code",    3, ErrorCode>
>;

std::int32_t        SYNCSPIRIT_API get_id(const Response&);
void                SYNCSPIRIT_API set_id(Response&, std::int32_t value);
utils::bytes_view_t SYNCSPIRIT_API get_data(const Response&);
void                SYNCSPIRIT_API set_data(Response&, utils::bytes_view_t value);
void                SYNCSPIRIT_API set_data(Response&, utils::bytes_t value);
ErrorCode           SYNCSPIRIT_API get_code(const Response&);
void                SYNCSPIRIT_API set_code(Response&, ErrorCode value);

using FileDownloadProgressUpdate = pp::message<
    pp::enum_field      <"update_type",     1, FileDownloadProgressUpdateType>,
    pp::string_field    <"name",            2                                >,
    pp::message_field   <"version",         3, Vector                        >,
    pp::int32_field     <"block_indexes",   4, pp::repeated                  >
>;

using DownloadProgress = pp::message<
    pp::string_field    <"folder",                     1                                          >,
    pp::message_field   <"FileDownloadProgressUpdate", 2, FileDownloadProgressUpdate, pp::repeated>
>;

// using Ping = pp::message<>;
struct Ping {};

using Close = pp::message<
    pp::string_field <"reason", 1>
>;
std::string_view    SYNCSPIRIT_API get_reason(const Close&);
void                SYNCSPIRIT_API set_reason(Close&, std::string_view value);
void                SYNCSPIRIT_API set_reason(Close&, std::string value);

#if 0
struct SYNCSPIRIT_API Announce;
struct SYNCSPIRIT_API Hello;
struct SYNCSPIRIT_API Header;
struct SYNCSPIRIT_API Device;
struct SYNCSPIRIT_API Folder;
struct SYNCSPIRIT_API ClusterConfig;
struct SYNCSPIRIT_API Counter;
struct SYNCSPIRIT_API Vector;
struct SYNCSPIRIT_API BlockInfo;
struct SYNCSPIRIT_API FileInfo;
struct SYNCSPIRIT_API Index;
struct SYNCSPIRIT_API IndexUpdate;
struct SYNCSPIRIT_API Request;
struct SYNCSPIRIT_API Response;
struct SYNCSPIRIT_API FileDownloadProgressUpdate;
struct SYNCSPIRIT_API DownloadProgress;
struct SYNCSPIRIT_API Ping;
struct SYNCSPIRIT_API Close;
#endif


namespace encode {

utils::bytes_t encode(const Announce&);
std::size_t    encode(const Hello&, fmt::memory_buffer&);
utils::bytes_t encode(const Hello&);
utils::bytes_t encode(const Header&);
utils::bytes_t encode(const BlockInfo&);
utils::bytes_t encode(const Device&);
utils::bytes_t encode(const FileInfo&);
utils::bytes_t encode(const Folder&);
utils::bytes_t encode(const IndexBase&);
utils::bytes_t encode(const Request&);
utils::bytes_t encode(const Response&);
utils::bytes_t encode(const Ping&);
utils::bytes_t encode(const Close&);
utils::bytes_t encode(const ClusterConfig&);
utils::bytes_t encode(const FileDownloadProgressUpdate&);
utils::bytes_t encode(const DownloadProgress&);

}

namespace decode {

bool decode(utils::bytes_view_t, Announce&);
bool decode(utils::bytes_view_t, Hello&);
bool decode(utils::bytes_view_t, Header&);
bool decode(utils::bytes_view_t, BlockInfo&);
bool decode(utils::bytes_view_t, Device&);
bool decode(utils::bytes_view_t, FileInfo&);
bool decode(utils::bytes_view_t, Folder&);
bool decode(utils::bytes_view_t, IndexBase&);
bool decode(utils::bytes_view_t, Request&);
bool decode(utils::bytes_view_t, Response&);
bool decode(utils::bytes_view_t, Ping&);
bool decode(utils::bytes_view_t, Close&);
bool decode(utils::bytes_view_t, ClusterConfig&);
bool decode(utils::bytes_view_t, FileDownloadProgressUpdate&);
bool decode(utils::bytes_view_t, DownloadProgress&);

}

}

namespace db {

// clang-format off

enum class FolderType {
    send             = 0,
    receive          = 1,
    send_and_receive = 2,
};

enum class PullOrder {
    random      = 0,
    alphabetic  = 1,
    smallest    = 2,
    largest     = 3,
    oldest      = 4,
    newest      = 5,
};

using FileInfoType = proto::FileInfoType;
using Vector = proto::Vector;
using Compression = proto::Compression;

#if 0

struct SYNCSPIRIT_API IgnoredFolder;
struct SYNCSPIRIT_API Device;
struct SYNCSPIRIT_API Folder;
struct SYNCSPIRIT_API FolderInfo;
struct SYNCSPIRIT_API PendingFolder;
struct SYNCSPIRIT_API FileInfo;
struct SYNCSPIRIT_API IngoredFolder;
struct SYNCSPIRIT_API BlockInfo;
struct SYNCSPIRIT_API SomeDevice;
#endif

using IgnoredFolder = pp::message<
    pp::string_field    <"label",   1>
>;
std::string_view    SYNCSPIRIT_API get_label(const IgnoredFolder&);
void                SYNCSPIRIT_API set_label(IgnoredFolder&, std::string_view value);

using Device = pp::message<
    pp::string_field    <"name",                       1              >,
    pp::string_field    <"addresses",                  2, pp::repeated>,
    pp::enum_field      <"compression",                3, Compression >,
    pp::string_field    <"cert_name",                  4              >,
    pp::bool_field      <"introducer",                 5              >,
    pp::bool_field      <"skip_introduction_removals", 6              >,
    pp::bool_field      <"auto_accept",                7              >,
    pp::bool_field      <"paused",                     8              >,
    pp::int64_field     <"last_seen",                  9              >
>;

std::string_view    SYNCSPIRIT_API get_name(const Device&);
void                SYNCSPIRIT_API set_name(Device&, std::string_view value);
std::size_t         SYNCSPIRIT_API get_addresses_size(const Device&);
std::string_view    SYNCSPIRIT_API get_addresses(const Device&, std::size_t i);
void                SYNCSPIRIT_API set_addresses(Device&, std::size_t i, std::string_view);
Compression         SYNCSPIRIT_API get_compression(const Device&);
void                SYNCSPIRIT_API set_compression(Device&, Compression value);
std::string_view    SYNCSPIRIT_API get_cert_name(const Device&);
void                SYNCSPIRIT_API set_cert_name(Device&, std::string_view value);
bool                SYNCSPIRIT_API get_introducer(const Device&);
void                SYNCSPIRIT_API set_introducer(Device&, bool value);
bool                SYNCSPIRIT_API get_skip_introduction_removals(const Device&);
void                SYNCSPIRIT_API set_skip_introduction_removals(Device&, bool value);
bool                SYNCSPIRIT_API get_auto_accept(const Device&);
void                SYNCSPIRIT_API set_auto_accept(Device&, bool value);
bool                SYNCSPIRIT_API get_paused(const Device&);
void                SYNCSPIRIT_API set_paused(Device&, bool value);
std::int64_t        SYNCSPIRIT_API get_last_seen(const Device&);
void                SYNCSPIRIT_API set_last_seen(Device&, std::int64_t value);

using Folder = pp::message<
    pp::string_field    <"id",                    1             >,
    pp::string_field    <"label",                 2             >,
    pp::bool_field      <"read_only",             3             >,
    pp::bool_field      <"ignore_permissions",    4             >,
    pp::bool_field      <"ignore_delete",         5             >,
    pp::bool_field      <"disable_temp_indexes",  6             >,
    pp::bool_field      <"paused",                7             >,
    pp::bool_field      <"scheduled",             8             >,
    pp::string_field    <"path",                  9             >,
    pp::enum_field      <"folder_type",          10, FolderType >,
    pp::enum_field      <"pull_order",           11, PullOrder  >,
    pp::uint32_field    <"rescan_interval",      12             >
>;
std::string_view    SYNCSPIRIT_API get_id(const Folder&);
void                SYNCSPIRIT_API set_id(Folder&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_label(const Folder&);
void                SYNCSPIRIT_API set_label(Folder&, std::string_view value);
bool                SYNCSPIRIT_API get_read_only(const Folder&);
void                SYNCSPIRIT_API set_read_only(Folder&, bool value);
bool                SYNCSPIRIT_API get_ignore_permissions(const Folder&);
void                SYNCSPIRIT_API set_ignore_permissions(Folder&, bool value);
bool                SYNCSPIRIT_API get_ignore_delete(const Folder&);
void                SYNCSPIRIT_API set_ignore_delete(Folder&, bool value);
bool                SYNCSPIRIT_API get_disable_temp_indexes(const Folder&);
void                SYNCSPIRIT_API set_disable_temp_indexes(Folder&, bool value);
bool                SYNCSPIRIT_API get_paused(const Folder&);
void                SYNCSPIRIT_API set_paused(Folder&, bool value);
bool                SYNCSPIRIT_API get_scheduled(const Folder&);
void                SYNCSPIRIT_API set_scheduled(Folder&, bool value);
std::string_view    SYNCSPIRIT_API get_path(const Folder&);
void                SYNCSPIRIT_API set_path(Folder&, std::string_view value);
FolderType          SYNCSPIRIT_API get_folder_type(const Folder&);
void                SYNCSPIRIT_API set_folder_type(Folder&, FolderType value);
PullOrder           SYNCSPIRIT_API get_pull_order(const Folder&);
void                SYNCSPIRIT_API set_pull_order(Folder&, PullOrder value);
std::uint32_t       SYNCSPIRIT_API get_rescan_interval(const Folder&);
void                SYNCSPIRIT_API set_rescan_interval(Folder&, std::uint32_t value);

using FolderInfo = pp::message<
    pp::uint64_field    <"index_id",     1>,
    pp::int64_field     <"max_sequence", 2>
>;

std::uint64_t SYNCSPIRIT_API get_index_id(const FolderInfo&);
void          SYNCSPIRIT_API set_index_id(FolderInfo&, std::uint64_t value);
std::int64_t  SYNCSPIRIT_API get_max_sequence(const FolderInfo&);
void          SYNCSPIRIT_API set_max_sequence(FolderInfo&, std::int64_t value);


using PendingFolder = pp::message<
    pp::message_field   <"folder",      1, Folder    >,
    pp::message_field   <"folder_info", 2, FolderInfo>
>;

Folder&                get_folder(const PendingFolder&);
void    SYNCSPIRIT_API set_folder(PendingFolder&, Folder value);
FolderInfo&            get_folder_info(const PendingFolder&);
void    SYNCSPIRIT_API set_folder_info(PendingFolder&, FolderInfo value);

using FileInfo = pp::message<
    pp::string_field    <"name",            1                                            >,
    pp::enum_field      <"type",            2, FileInfoType                              >,
    pp::int64_field     <"size",            3                                            >,
    pp::uint32_field    <"permissions",     4                                            >,
    pp::int64_field     <"modified_s",      5                                            >,
    pp::int32_field     <"modified_ns",     11                                           >,
    pp::uint64_field    <"modified_by",     12                                           >,
    pp::bool_field      <"deleted",         6                                            >,
    pp::bool_field      <"invalid",         7                                            >,
    pp::bool_field      <"no_permissions",  8                                            >,
    pp::message_field   <"version",         9, proto::Vector                             >,
    pp::int64_field     <"sequence",        10                                           >,
    pp::int32_field     <"block_size",      13                                           >,
    pp::string_field    <"symlink_target",  16                                           >,
    pp::bytes_field     <"blocks",          17, pp::repeated, proto::bytes_backend_t     >
>;

std::string_view    SYNCSPIRIT_API get_name(const FileInfo&);
void                SYNCSPIRIT_API set_name(FileInfo&, std::string_view value);
FileInfoType        SYNCSPIRIT_API get_type(const FileInfo&);
void                SYNCSPIRIT_API set_type(FileInfo&, FileInfoType value);
std::int64_t        SYNCSPIRIT_API get_size(const FileInfo&);
void                SYNCSPIRIT_API set_size(FileInfo&, std::int64_t value);
std::uint32_t       SYNCSPIRIT_API get_permissions(const FileInfo&);
void                SYNCSPIRIT_API set_permissions(FileInfo&, std::uint32_t value);
std::int64_t        SYNCSPIRIT_API get_modified_s(const FileInfo&);
void                SYNCSPIRIT_API set_modified_s(FileInfo&, std::int64_t value);
std::int32_t        SYNCSPIRIT_API get_modified_ns(const FileInfo&);
void                SYNCSPIRIT_API set_modified_ns(FileInfo&, std::int32_t value);
std::uint64_t       SYNCSPIRIT_API get_modified_by(const FileInfo&);
void                SYNCSPIRIT_API set_modified_by(FileInfo&, std::uint64_t value);
bool                SYNCSPIRIT_API get_deleted(const FileInfo&);
void                SYNCSPIRIT_API set_deleted(FileInfo&, bool value);
bool                SYNCSPIRIT_API get_invalid(const FileInfo&);
void                SYNCSPIRIT_API set_invalid(FileInfo&, bool value);
bool                SYNCSPIRIT_API get_no_permissions(const FileInfo&);
void                SYNCSPIRIT_API set_no_permissions(FileInfo&, bool value);
Vector&                            get_version(const FileInfo&);
void                SYNCSPIRIT_API set_version(FileInfo&, Vector value);
std::int64_t        SYNCSPIRIT_API get_sequence(const FileInfo&);
void                SYNCSPIRIT_API set_sequence(FileInfo&, std::int64_t value);
std::int32_t        SYNCSPIRIT_API get_block_size(const FileInfo&);
void                SYNCSPIRIT_API set_block_size(FileInfo&, std::int32_t value);
std::size_t         SYNCSPIRIT_API get_blocks_size(const FileInfo&);
utils::bytes_view_t get_blocks(const FileInfo&, std::size_t i);
void                SYNCSPIRIT_API set_blocks(FileInfo&, std::size_t i, utils::bytes_view_t);
void                SYNCSPIRIT_API add_blocks(FileInfo&, utils::bytes_view_t);
std::string_view    SYNCSPIRIT_API get_symlink_target(const FileInfo&);
void                SYNCSPIRIT_API set_symlink_target(FileInfo&, std::string_view value);

using BlockInfo = pp::message<
    pp::uint32_field   <"weak_hash", 1>,
    pp::int32_field    <"size",      2>
>;
std::uint32_t       SYNCSPIRIT_API get_weak_hash(const BlockInfo&);
void                SYNCSPIRIT_API set_weak_hash(BlockInfo&, std::uint32_t value);
std::int32_t        SYNCSPIRIT_API get_size(const BlockInfo&);
void                SYNCSPIRIT_API set_size(BlockInfo&, std::int32_t value);


using SomeDevice = pp::message<
    pp::string_field <"name",           1>,
    pp::string_field <"client_name",    2>,
    pp::string_field <"client_version", 3>,
    pp::string_field <"address",        4>,
    pp::int64_field  <"last_seen",      5>
>;
std::string_view    SYNCSPIRIT_API get_name(const SomeDevice&);
void                SYNCSPIRIT_API set_name(SomeDevice&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_client_name(const SomeDevice&);
void                SYNCSPIRIT_API set_client_name(SomeDevice&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_client_version(const SomeDevice&);
void                SYNCSPIRIT_API set_client_version(SomeDevice&, std::string_view value);
std::string_view    SYNCSPIRIT_API get_address(const SomeDevice&);
void                SYNCSPIRIT_API set_address(SomeDevice&, std::string_view value);
void                SYNCSPIRIT_API set_address(SomeDevice&, std::string value);
std::int64_t        SYNCSPIRIT_API get_last_seen(const SomeDevice&);
void                SYNCSPIRIT_API set_last_seen(SomeDevice&, std::int64_t value);

namespace encode {

utils::bytes_t device        (const Device&);
utils::bytes_t block_info    (const BlockInfo&);
utils::bytes_t file_info     (const FileInfo&);
utils::bytes_t folder        (const Folder&);
utils::bytes_t folder_info   (const FolderInfo&);
utils::bytes_t ignored_folder(const IgnoredFolder&);
utils::bytes_t pending_folder(const PendingFolder&);
utils::bytes_t some_device   (const SomeDevice&);

}

namespace decode {

std::optional<BlockInfo>     block_info     (utils::bytes_view_t);
std::optional<Device>        device         (utils::bytes_view_t);
std::optional<FileInfo>      file_info      (utils::bytes_view_t);
std::optional<FolderInfo>    folder_info    (utils::bytes_view_t);
std::optional<Folder>        folder         (utils::bytes_view_t);
std::optional<PendingFolder> pending_folder (utils::bytes_view_t);
std::optional<SomeDevice>    some_device    (utils::bytes_view_t);
std::optional<IgnoredFolder> ignored_folder (utils::bytes_view_t);

}

// clang-format on

}

}
