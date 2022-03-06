// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "block_visitor.h"

using namespace syncspirit::model::diff;

auto block_visitor_t::operator()(const modify::append_block_t &) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto block_visitor_t::operator()(const modify::blocks_availability_t &) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto block_visitor_t::operator()(const modify::clone_block_t &) noexcept -> outcome::result<void> {
    return outcome::success();
}
