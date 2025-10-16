// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/bytes.h"
#include "utils/error_code.h"

#include <rotor.hpp>
#include <boost/outcome.hpp>
#include <memory>

namespace syncspirit {
namespace hasher {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

namespace payload {

struct extendended_context_t {
    virtual ~extendended_context_t() = default;
};

using extendended_context_prt_t = std::unique_ptr<extendended_context_t>;

struct digest_t {
    utils::bytes_t data;
    extendended_context_prt_t context;
    r::address_ptr_t back_addr;
    r::address_ptr_t hasher_addr;
    outcome::result<utils::bytes_t> result;

    digest_t(utils::bytes_view_t data_, extendended_context_prt_t context_ = {}) noexcept
        : data{std::move(data_)}, result{utils::make_error_code(utils::error_code_t::no_action)},
          context{std::move(context_)} {}
    digest_t(const digest_t &) = delete;
    digest_t(digest_t &&) noexcept = default;
};

struct validation_t {
    utils::bytes_t data;
    utils::bytes_t hash;
    extendended_context_prt_t context;
    r::address_ptr_t back_addr;
    r::address_ptr_t hasher_addr;
    outcome::result<void> result;

    validation_t(utils::bytes_t data_, utils::bytes_t hash_, extendended_context_prt_t context_ = {}) noexcept
        : data{std::move(data_)}, hash{std::move(hash_)}, context{std::move(context_)},
          result{utils::make_error_code(utils::error_code_t::no_action)} {}

    validation_t(const validation_t &) = delete;
    validation_t(validation_t &&) noexcept = default;
};

} // namespace payload

namespace message {

using digest_t = r::message_t<payload::digest_t>;
using validation_t = r::message_t<payload::validation_t>;

} // namespace message

} // namespace hasher
} // namespace syncspirit
