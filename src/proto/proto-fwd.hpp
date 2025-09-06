// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <protopuf/message.h>

namespace syncspirit {

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
    HELLO             = 8,
    UNKNOWN           = 9,
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
    pp::bytes_field  <"id",          1, pp::singular, proto::bytes_backend_t>,
    pp::string_field <"addresses",   2, pp::repeated>,
    pp::uint64_field <"instance_id", 3              >
>;

using Hello = pp::message<
    pp::string_field <"device_name",    1>,
    pp::string_field <"client_name",    2>,
    pp::string_field <"client_version", 3>
>;

using Header = pp::message<
    pp::enum_field <"type",        1, MessageType>,
    pp::enum_field <"compression", 2, MessageCompression>
>;

using Device = pp::message<
    pp::bytes_field  <"id",                         1, pp::singular, proto::bytes_backend_t>,
    pp::string_field <"name",                       2                                      >,
    pp::string_field <"addresses",                  3, pp::repeated                        >,
    pp::enum_field   <"compression",                4, Compression                         >,
    pp::string_field <"cert_name",                  5                                      >,
    pp::int64_field  <"max_sequence",               6                                      >,
    pp::bool_field   <"introducer",                 7                                      >,
    pp::uint64_field <"index_id",                   8                                      >,
    pp::bool_field   <"skip_introduction_removals", 9                                      >
>;

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

using ClusterConfig = pp::message<
    pp::message_field   <"folders", 1, Folder, pp::repeated>
>;

using Counter = pp::message<
    pp::uint64_field    <"id",    1>,
    pp::uint64_field    <"value", 2>
>;

using Vector = pp::message<
    pp::message_field   <"counters", 1, Counter, pp::repeated>
>;

using BlockInfo = pp::message<
    pp::int64_field  <"offset",    1                                      >,
    pp::int32_field  <"size",      2                                      >,
    pp::bytes_field  <"hash",      3, pp::singular, proto::bytes_backend_t>,
    pp::uint32_field <"weak_hash", 4                                      >
>;

using FileInfo = pp::message<
    pp::string_field  <"name",            1                           >,
    pp::enum_field    <"type",            2, FileInfoType             >,
    pp::int64_field   <"size",            3                           >,
    pp::uint32_field  <"permissions",     4                           >,
    pp::int64_field   <"modified_s",      5                           >,
    pp::int32_field   <"modified_ns",     11                          >,
    pp::uint64_field  <"modified_by",     12                          >,
    pp::bool_field    <"deleted",         6                           >,
    pp::bool_field    <"invalid",         7                           >,
    pp::bool_field    <"no_permissions",  8                           >,
    pp::message_field <"version",         9, Vector                   >,
    pp::int64_field   <"sequence",        10                          >,
    pp::int32_field   <"block_size",      13                          >,
    pp::message_field <"blocks",          16, BlockInfo, pp::repeated >,
    pp::string_field  <"symlink_target",  17                          >
>;

using IndexBase = pp::message<
    pp::string_field    <"folder",  1                        >,
    pp::message_field   <"files",   2, FileInfo, pp::repeated>
>;
struct Index: IndexBase {};
struct IndexUpdate: IndexBase {};

using Request = pp::message<
    pp::int32_field  <"id",              1                                      >,
    pp::string_field <"folder",          2                                      >,
    pp::string_field <"name",            3                                      >,
    pp::int64_field  <"offset",          4                                      >,
    pp::int32_field  <"size",            5                                      >,
    pp::bytes_field  <"hash",            6, pp::singular, proto::bytes_backend_t>,
    pp::bool_field   <"from_temporary",  7                                      >,
    pp::uint32_field <"weak_hash",       8                                      >
>;

using Response = pp::message<
    pp::int32_field <"id",      1                                      >,
    pp::bytes_field <"data",    2, pp::singular, proto::bytes_backend_t>,
    pp::enum_field  <"code",    3, ErrorCode                           >
>;

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

using IgnoredFolder = pp::message<
    pp::string_field    <"label",   1>
>;

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

using Folder = pp::message<
    pp::string_field    <"id",                    1             >,
    pp::string_field    <"label",                 2             >,
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

using FolderInfo = pp::message<
    pp::uint64_field  <"index_id",              1                                      >,
    pp::int64_field   <"max_sequence",          2                                      >,
    pp::bytes_field   <"introducer_device_key", 3, pp::singular, proto::bytes_backend_t>
>;

using PendingFolder = pp::message<
    pp::message_field   <"folder",      1, Folder    >,
    pp::message_field   <"folder_info", 2, FolderInfo>
>;

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

using BlockInfo = pp::message<
    pp::int32_field    <"size",      1>
>;

using SomeDevice = pp::message<
    pp::string_field <"name",           1>,
    pp::string_field <"client_name",    2>,
    pp::string_field <"client_version", 3>,
    pp::string_field <"address",        4>,
    pp::int64_field  <"last_seen",      5>
>;

// clang-format on

} // namespace db

} // namespace syncspirit
