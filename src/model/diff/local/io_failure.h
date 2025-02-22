// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "../cluster_diff.h"
#include <vector>
#include <boost/system/errc.hpp>
#include <filesystem>

namespace syncspirit::model::diff::local {

namespace sys = boost::system;
namespace bfs = std::filesystem;

struct io_error_t {
    bfs::path path;
    sys::error_code ec;
};

using io_errors_t = std::vector<io_error_t>;

struct SYNCSPIRIT_API io_failure_t final : cluster_diff_t {
    io_failure_t(io_error_t) noexcept;
    io_failure_t(io_errors_t) noexcept;

    outcome::result<void> visit(cluster_visitor_t &, void *) const noexcept override;

    io_errors_t errors;
};

} // namespace syncspirit::model::diff::local
