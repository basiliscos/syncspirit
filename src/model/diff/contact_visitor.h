// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

namespace modify {
struct update_contact_t;
struct connect_request_t;
struct relay_connect_request_t;
} // namespace modify

namespace peer {
struct peer_state_t;
}

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::contact> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const modify::update_contact_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::relay_connect_request_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::peer_state_t &, void *custom) noexcept;
};

using contact_visitor_t = generic_visitor_t<tag::contact>;

} // namespace syncspirit::model::diff
