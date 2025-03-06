// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "utils/bytes.h"
#include <protopuf/message.h>
#include <protopuf/coder.h>

namespace syncspirit::details {

template <typename T> size_t generic_decode(utils::bytes_view_t bytes, T &storage) noexcept {
    using coder_t = pp::message_coder<T>;
    auto ptr = (std::byte *)bytes.data();
    auto span = std::span<std::byte>(ptr, bytes.size());
    auto pair = coder_t::template decode<pp::unsafe_mode>(span);
    storage = std::move(pair.first);
    return pair.second.size();
}

template <typename T> utils::bytes_t generic_encode(const T &object) noexcept {
    using coder_t = pp::message_coder<T>;
    using skipper_t = pp::skipper<coder_t>;
    auto size = skipper_t::encode_skip(object);
    auto storage = utils::bytes_t(size);
    auto bytes = pp::bytes((std::byte *)storage.data(), size);
    coder_t::template encode<pp::unsafe_mode>(object, bytes);
    return storage;
}

} // namespace syncspirit::details
