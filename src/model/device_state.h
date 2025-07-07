// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <string_view>
#include <cstdint>
#include <utils/uri.h>

namespace syncspirit::model {

enum class connection_state_t { offline, unknown, discovering, connecting, connected, online };

struct device_state_t;

struct SYNCSPIRIT_API device_state_t {
    using token_id_t = std::uint32_t;

    device_state_t(const device_state_t &) = delete;
    device_state_t(device_state_t &&) = default;

    device_state_t &operator=(device_state_t &&) = default;
    device_state_t &operator=(const device_state_t &) = delete;
    bool operator==(const device_state_t &) const noexcept = default;
    bool operator<(const device_state_t &) const noexcept;
    bool can_roollback_to(const device_state_t &) const noexcept;

    static device_state_t make_offline() noexcept;

    inline bool is_unknown() const noexcept { return connection_state == connection_state_t::unknown; }
    inline bool is_connecting() const noexcept { return connection_state == connection_state_t::connecting; }
    inline bool is_connected() const noexcept { return connection_state == connection_state_t::connected; }
    inline bool is_online() const noexcept { return connection_state == connection_state_t::online; }
    inline bool is_offline() const noexcept { return connection_state == connection_state_t::offline; }
    inline bool is_discovering() const noexcept { return connection_state == connection_state_t::discovering; }

    device_state_t offline() const noexcept;
    device_state_t unknown() const noexcept;
    device_state_t discover() const noexcept;
    device_state_t connecting() const noexcept;
    device_state_t connected() const noexcept;
    device_state_t online(std::string_view url) const noexcept;
    device_state_t clone() const noexcept;

    inline connection_state_t get_connection_state() const noexcept { return connection_state; }
    inline utils::uri_ptr_t get_url() const noexcept { return online_url; }

  private:
    device_state_t(token_id_t token, connection_state_t connection_state, utils::uri_ptr_t url = {}) noexcept;

    token_id_t token;
    connection_state_t connection_state;
    utils::uri_ptr_t online_url;
};

} // namespace syncspirit::model
