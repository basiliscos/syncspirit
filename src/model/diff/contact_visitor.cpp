// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "contact_visitor.h"

using namespace syncspirit::model::diff;

auto contact_visitor_t::operator()(const contact::connect_request_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const contact::dial_request_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const contact::update_contact_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const contact::relay_connect_request_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const contact::peer_state_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}
