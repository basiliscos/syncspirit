// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "device_state.h"
#include <cassert>

using namespace syncspirit::model;

namespace {
device_state_t::token_id_t next_token_id = {0};
}

device_state_t device_state_t::make_offline() noexcept {
    return device_state_t(++next_token_id, connection_state_t::offline);
}

device_state_t::device_state_t(token_id_t token_, connection_state_t connection_state_, utils::uri_ptr_t url) noexcept
    : token{token_}, connection_state{connection_state_}, online_url{std::move(url)} {}

bool device_state_t::operator<(const device_state_t &other) const noexcept {
    if (other.connection_state == connection_state && connection_state == connection_state_t::online) {
        auto lhs_scheme = online_url->scheme();
        auto rhs_scheme = other.online_url->scheme();
        auto lhs_relay = lhs_scheme.find("relay") == 0;
        auto rhs_relay = rhs_scheme.find("relay") == 0;
        auto lhs_tcp = lhs_scheme.find("tcp") == 0;
        auto rhs_tcp = rhs_scheme.find("tcp") == 0;

        if (lhs_relay && rhs_tcp) {
            return true;
        }
        if (lhs_tcp && rhs_relay) {
            return false;
        }

        auto lhs_port = online_url->port_number();
        auto rhs_port = other.online_url->port_number();
        return lhs_port < rhs_port;
    }
    return connection_state < other.connection_state;
}

bool device_state_t::operator==(const device_state_t &other) const noexcept {
    if (token != other.token || connection_state != other.connection_state) {
        return false;
    }
    if (connection_state != connection_state_t::online) {
        return true;
    }
    if ((online_url && !other.online_url) || (!online_url && other.online_url)) {
        return false;
    }
    return *online_url == *other.online_url;
}

bool device_state_t::can_roollback_to(const device_state_t &other) const noexcept {
    return other.token == token && (other < *this);
}

device_state_t device_state_t::offline() const noexcept { return device_state_t(token, connection_state_t::offline); }

device_state_t device_state_t::connecting() const noexcept {
    assert(connection_state == connection_state_t::offline);
    return device_state_t(token, connection_state_t::connecting);
}

device_state_t device_state_t::connected() const noexcept {
    assert(connection_state == connection_state_t::connecting);
    return device_state_t(token, connection_state_t::connected);
}

device_state_t device_state_t::discover() const noexcept {
    assert(connection_state == connection_state_t::unknown);
    return device_state_t(token, connection_state_t::discovering);
}

device_state_t device_state_t::unknown() const noexcept {
    assert(connection_state == connection_state_t::offline);
    return device_state_t(token, connection_state_t::unknown);
}

device_state_t device_state_t::clone() const noexcept {
    auto url = online_url ? online_url->clone() : utils::uri_ptr_t{};
    return device_state_t(token, connection_state, std::move(url));
}

device_state_t device_state_t::online(std::string_view url) const noexcept {
    assert(connection_state == connection_state_t::connected);
    auto url_obj = utils::parse(url);
    assert((url_obj->scheme().find("tcp") == 0) || (url_obj->scheme().find("relay") == 0));
    return device_state_t(token, connection_state_t::online, std::move(url_obj));
}
