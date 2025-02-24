// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-structs.h"
#include <protopuf/skip.h>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace pp;

namespace syncspirit::db::changeable {

proto::changeable::Vector FileInfo::mutable_version() noexcept {
    auto& opt = (*impl)["_version"_f];
    if (!opt.has_value()) {
        opt = proto::details::Vector();
    }
    return proto::changeable::Vector(&(*impl)["_version"_f].value());
}

}

auto BlockInfo::encode() const noexcept -> utils::bytes_t {
    return proto::impl::generic_encode<details::BlockInfo>(*this);
}

auto BlockInfo::decode(utils::bytes_view_t bytes) noexcept -> std::optional<BlockInfo> {
    return proto::impl::generic_decode<BlockInfo, details::BlockInfo>(bytes);
}

auto Device::decode(utils::bytes_view_t bytes) noexcept -> std::optional<Device> {
    return proto::impl::generic_decode<Device, details::Device>(bytes);
}

utils::bytes_t Device::encode() const noexcept {
    return proto::impl::generic_encode<details::Device>(*this);
}

auto FileInfo::decode(utils::bytes_view_t bytes) noexcept -> std::optional<FileInfo> {
    return proto::impl::generic_decode<FileInfo, details::FileInfo>(bytes);
}

utils::bytes_t FileInfo::encode() const noexcept {
    return proto::impl::generic_encode<details::FileInfo>(*this);
}

auto Folder::decode(utils::bytes_view_t bytes) noexcept -> std::optional<Folder> {
    return proto::impl::generic_decode<Folder, details::Folder>(bytes);
}

utils::bytes_t Folder::encode() const noexcept {
    return proto::impl::generic_encode<details::Folder>(*this);
}

auto FolderInfo::decode(utils::bytes_view_t bytes) noexcept -> std::optional<FolderInfo> {
    return proto::impl::generic_decode<FolderInfo, details::FolderInfo>(bytes);
}

utils::bytes_t FolderInfo::encode() const noexcept {
    return proto::impl::generic_encode<details::FolderInfo>(*this);
}

auto IgnoredFolder::decode(utils::bytes_view_t bytes) noexcept -> std::optional<IgnoredFolder> {
    return proto::impl::generic_decode<IgnoredFolder, details::IgnoredFolder>(bytes);
}

utils::bytes_t IgnoredFolder::encode() const noexcept {
    return proto::impl::generic_encode<details::IgnoredFolder>(*this);
}

auto PendingFolder::decode(utils::bytes_view_t bytes) noexcept -> std::optional<PendingFolder> {
    return proto::impl::generic_decode<PendingFolder, details::PendingFolder>(bytes);
}

utils::bytes_t PendingFolder::encode() const noexcept {
    return proto::impl::generic_encode<details::PendingFolder>(*this);
}

auto SomeDevice::decode(utils::bytes_view_t bytes) noexcept -> std::optional<SomeDevice> {
    return proto::impl::generic_decode<SomeDevice, details::SomeDevice>(bytes);
}

utils::bytes_t SomeDevice::encode() const noexcept {
    return proto::impl::generic_encode<details::SomeDevice>(*this);
}
