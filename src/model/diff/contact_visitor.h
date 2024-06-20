// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

namespace contact {
struct connect_request_t;
struct peer_state_t;
struct relay_connect_request_t;
struct update_contact_t;
} // namespace contact

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::contact> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const contact::update_contact_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::relay_connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const contact::peer_state_t &, void *custom) noexcept;
};

using contact_visitor_t = generic_visitor_t<tag::contact>;

} // namespace syncspirit::model::diff
