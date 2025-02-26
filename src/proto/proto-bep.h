// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "proto-fwd.hpp"

#if 0
#include <protopuf/message.h>
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include <cstdint>

namespace syncspirit::proto {

namespace details {

// clang-format off

using Announce = pp::message<
    pp::bytes_field  <"id",          1              >,
    pp::string_field <"addresses",   1, pp::repeated>,
    pp::uint64_field <"instance_id", 3              >
>;

utils::bytes_view_t SYNCSPIRIT_API get_id(Announce&) noexcept;
void                SYNCSPIRIT_API set_id(Announce&, utils::bytes_view_t value) noexcept;
std::size_t         SYNCSPIRIT_API get_addresses_size(Announce&) noexcept;
std::string_view    SYNCSPIRIT_API get_addresses(Announce&, std::size_t i) noexcept;
void                SYNCSPIRIT_API set_addresses(Announce&, std::size_t i, std::string_view) noexcept;
std::uint64_t       SYNCSPIRIT_API get_instance_id(Announce&) noexcept;
void                SYNCSPIRIT_API set_instance_id(Announce&, std::uint64_t value) noexcept;

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
    pp::int64_field    <"offset",    1>,
    pp::int32_field    <"size",      2>,
    pp::bytes_field    <"hash",      3>,
    pp::uint32_field   <"weak_hash", 4>
>;

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

using Index = pp::message<
    pp::string_field    <"folder",  1                        >,
    pp::message_field   <"files",   2, FileInfo, pp::repeated>
>;

using IndexUpdate = Index;

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

using Response = pp::message<
    pp::int32_field     <"id",      1>,
    pp::bytes_field     <"data",    2>,
    pp::enum_field      <"code",    3, ErrorCode>
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

// clang-format on

};

}
#endif
