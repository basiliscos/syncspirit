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
struct fs_proxy_t;

struct execution_context_t {
    using clock_t = boost::posix_time::microsec_clock;
    using scan_dir_callback_t = std::function<void(const task::scan_dir_t &)>;

    inline execution_context_t() = default;
    execution_context_t(const execution_context_t &) = delete;
    execution_context_t(execution_context_t &&) = delete;

    fs_proxy_t *fs_proxy{};
    hasher::hasher_plugin_t *plugin{};
    scan_dir_callback_t scan_dir_callback;
};
} // namespace fs

} // namespace syncspirit
