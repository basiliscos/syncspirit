// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <functional>

namespace syncspirit {

namespace hasher {

struct hasher_plugin_t;

}

namespace fs {

namespace task {
struct scan_dir_t;
}

namespace pt = boost::posix_time;

struct fs_slave_t;
struct updates_mediator_t;

struct execution_context_t {
    using clock_t = boost::posix_time::microsec_clock;
    using scan_dir_callback_t = std::function<void(const task::scan_dir_t &)>;

    execution_context_t() = default;
    execution_context_t(const execution_context_t &) = delete;
    execution_context_t(execution_context_t &&) = delete;

    virtual pt::ptime get_deadline() const = 0;

    hasher::hasher_plugin_t *plugin;
    updates_mediator_t *mediator = nullptr;
    scan_dir_callback_t scan_dir_callback;
};
} // namespace fs

} // namespace syncspirit
