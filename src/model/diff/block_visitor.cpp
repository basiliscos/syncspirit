// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "block_visitor.h"
#include "modify/append_block.h"
#include "modify/blocks_availability.h"
#include "modify/block_ack.h"
#include "modify/block_rej.h"
#include "modify/clone_block.h"

using namespace syncspirit::model::diff;

auto block_visitor_t::operator()(const modify::append_block_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto block_visitor_t::operator()(const modify::blocks_availability_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto block_visitor_t::operator()(const modify::block_ack_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto block_visitor_t::operator()(const modify::block_rej_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto block_visitor_t::operator()(const modify::clone_block_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}
