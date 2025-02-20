// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-structs.h"
#include <protopuf/skip.h>

using namespace syncspirit;
using namespace syncspirit::db;

inline static auto as_bytes(utils::bytes_view_t bytes) noexcept -> std::span<std::byte> {
    auto ptr = (std::byte*)bytes.data();
    return std::span<std::byte>(ptr, bytes.size());
}

template<typename T, typename B> std::optional<T> generic_decode(utils::bytes_view_t bytes) noexcept {
    using coder_t = pp::message_coder<B>;
    auto span = as_bytes(bytes);
    auto opt = coder_t::decode(span);
    if (opt) {
        auto& impl = opt.value().first;
        return T(std::move(impl));
    }
    return {};
}

template<typename B, typename T> utils::bytes_t generic_encode(T& item) noexcept {
    using coder_t = pp::message_coder<B>;
    using skipper_t = pp::skipper<coder_t>;
    auto &message = item.expose();
    auto size = skipper_t::encode_skip(message);
    auto storage = utils::bytes_t(size);
    auto bytes = pp::bytes((std::byte*)storage.data(), size);
    coder_t::template encode<pp::unsafe_mode>(message, bytes);
    return storage;
}

auto BlockInfo::encode() noexcept -> utils::bytes_t {
    return generic_encode<details::BlockInfo>(*this);
}

auto BlockInfo::decode(utils::bytes_view_t bytes) noexcept -> std::optional<BlockInfo> {
    return generic_decode<BlockInfo, details::BlockInfo>(bytes);
}

auto Device::decode(utils::bytes_view_t bytes) noexcept -> std::optional<Device> {
    return generic_decode<Device, details::Device>(bytes);
}

utils::bytes_t Device::encode() noexcept {
    return generic_encode<details::Device>(*this);
}

utils::bytes_t FileInfo::encode() noexcept {
    return generic_encode<details::FileInfo>(*this);
}

auto Folder::decode(utils::bytes_view_t bytes) noexcept -> std::optional<Folder> {
    return generic_decode<Folder, details::Folder>(bytes);
}

utils::bytes_t Folder::encode() noexcept {
    return generic_encode<details::Folder>(*this);
}

auto FolderInfo::decode(utils::bytes_view_t bytes) noexcept -> std::optional<FolderInfo> {
    return generic_decode<FolderInfo, details::FolderInfo>(bytes);
}

auto PendingFolder::decode(utils::bytes_view_t bytes) noexcept -> std::optional<PendingFolder> {
    return generic_decode<PendingFolder, details::PendingFolder>(bytes);
}

auto SomeDevice::decode(utils::bytes_view_t bytes) noexcept -> std::optional<SomeDevice> {
    return generic_decode<SomeDevice, details::SomeDevice>(bytes);
}
