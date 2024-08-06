// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "contact_visitor.h"
#include "contact_diff.h"
#include "contact/connect_request.h"
#include "contact/dial_request.h"
#include "contact/ignored_connected.h"
#include "contact/relay_connect_request.h"
#include "contact/peer_state.h"
#include "contact/unknown_connected.h"
#include "contact/update_contact.h"

using namespace syncspirit::model::diff;

auto contact_visitor_t::operator()(const contact::connect_request_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto contact_visitor_t::operator()(const contact::dial_request_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto contact_visitor_t::operator()(const contact::ignored_connected_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto contact_visitor_t::operator()(const contact::relay_connect_request_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto contact_visitor_t::operator()(const contact::peer_state_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto contact_visitor_t::operator()(const contact::unknown_connected_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}

auto contact_visitor_t::operator()(const contact::update_contact_t &diff, void *custom) noexcept -> outcome::result<void> {
    return diff.visit_next(*this, custom);
}
