// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/bytes.h"
#include <rotor.hpp>

namespace syncspirit {
namespace hasher {

namespace r = rotor;

namespace payload {

struct digest_response_t {
    utils::bytes_t digest;
    uint32_t weak;
};

struct digest_request_t {
    using response_t = digest_response_t;
    utils::bytes_t data;
    size_t block_index;
    r::message_ptr_t custom;
};

struct validation_response_t {
    bool valid;
};

struct validation_request_t : r::arc_base_t<validation_request_t> {
    using response_t = validation_response_t;
    utils::bytes_view_t data;
    utils::bytes_t hash;
    r::message_ptr_t custom;

    validation_request_t(utils::bytes_view_t data_, utils::bytes_t hash_, r::message_ptr_t custom_ = nullptr) noexcept
        : data{data_}, hash{std::move(hash_)}, custom{std::move(custom_)} {}
};

using package_t = r::message_ptr_t;

} // namespace payload

namespace message {

using digest_request_t = r::request_traits_t<payload::digest_request_t>::request::message_t;
using digest_response_t = r::request_traits_t<payload::digest_request_t>::response::message_t;

using validation_request_t = r::request_traits_t<payload::validation_request_t>::request::message_t;
using validation_response_t = r::request_traits_t<payload::validation_request_t>::response::message_t;

using package_t = r::message_t<payload::package_t>;

} // namespace message

} // namespace hasher
} // namespace syncspirit
