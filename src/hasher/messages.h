// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "utils/bytes.h"
#include "utils/error_code.h"

#include <rotor.hpp>
#include <boost/outcome.hpp>

namespace syncspirit {
namespace hasher {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

namespace payload {

struct extendended_context_t : boost::intrusive_ref_counter<extendended_context_t, boost::thread_safe_counter> {
    virtual ~extendended_context_t() = default;
};

using extendended_context_prt_t = boost::intrusive_ptr<extendended_context_t>;

struct digest_t {
    utils::bytes_t data;
    std::int32_t block_index;
    extendended_context_prt_t context;
    r::address_ptr_t back_addr;
    r::address_ptr_t hasher_addr;
    outcome::result<utils::bytes_t> result;

    digest_t(utils::bytes_view_t data_, std::int32_t block_index_, extendended_context_prt_t context_ = {}) noexcept
        : data{std::move(data_)}, block_index{block_index_},
          result{utils::make_error_code(utils::error_code_t::no_action)}, context{std::move(context_)} {}
    digest_t(const digest_t &) = delete;
    digest_t(digest_t &&) noexcept = default;
};

} // namespace payload

namespace message {

using digest_t = r::message_t<payload::digest_t>;

} // namespace message

} // namespace hasher
} // namespace syncspirit
