// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-helpers-db.h"
#include <protopuf/message.h>
#include <protopuf/coder.h>

namespace syncspirit::db {

template<typename T> bool generic_decode(utils::bytes_view_t bytes, T& storage) noexcept {
    using coder_t = pp::message_coder<T>;
    auto ptr = (std::byte*)bytes.data();
    auto span = std::span<std::byte>(ptr, bytes.size());
    auto opt = coder_t::decode(span);
    if (opt) {
        storage = std::move((*opt).first);
        return true;
    }
    return false;
}

template<typename T> utils::bytes_t generic_encode(const T& object) noexcept {
    using coder_t = pp::message_coder<T>;
    using skipper_t = pp::skipper<coder_t>;
    auto size = skipper_t::encode_skip(object);
    auto storage = utils::bytes_t(size);
    auto bytes = pp::bytes((std::byte*)storage.data(), size);
    coder_t::template encode<pp::unsafe_mode>(object, bytes);
    return storage;
}

bool decode(utils::bytes_view_t data, BlockInfo& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const BlockInfo& object) {
    return generic_encode(object);
}

bool decode(utils::bytes_view_t data, Device& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const Device& object) {
    return generic_encode(object);
}

bool decode(utils::bytes_view_t data, FileInfo& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const FileInfo& object) {
    return generic_encode(object);
}

bool decode(utils::bytes_view_t data, Folder& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const Folder& object) {
    return generic_encode(object);
}


bool decode(utils::bytes_view_t data, FolderInfo& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const FolderInfo& object) {
    return generic_encode(object);
}

bool decode(utils::bytes_view_t data, IgnoredFolder& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const IgnoredFolder& object) {
    return generic_encode(object);
}

bool decode(utils::bytes_view_t data, PendingFolder& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const PendingFolder& object) {
    return generic_encode(object);
}

bool decode(utils::bytes_view_t data, SomeDevice& object) {
    return generic_decode(data, object);
}

utils::bytes_t encode(const SomeDevice& object) {
    return generic_encode(object);
}

}
