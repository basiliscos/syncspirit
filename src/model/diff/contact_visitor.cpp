// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "contact_visitor.h"

using namespace syncspirit::model::diff;

auto contact_visitor_t::operator()(const modify::connect_request_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const modify::update_contact_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const modify::relay_connect_request_t &, void *) noexcept -> outcome::result<void> {
    return outcome::success();
}
