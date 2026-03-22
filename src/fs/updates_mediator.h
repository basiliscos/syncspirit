// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "updates_support.h"
#include "syncspirit-export.h"
#include "model/misc/arc.hpp"
#include "utils/log.h"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <cstdint>
#include <filesystem>

namespace syncspirit::fs {

namespace pt = boost::posix_time;
namespace bfs = std::filesystem;

struct SYNCSPIRIT_API updates_mediator_t : model::arc_base_t<updates_mediator_t> {
    using timepoint_t = pt::ptime;
    updates_mediator_t(const pt::time_duration &interval, bool enabled = false);

    void mask(const bfs::path &path, const bfs::path &prev_path, const timepoint_t &deadline) noexcept;
    std::uint32_t is_masked(std::string_view path) noexcept;
    bool clean_expired() noexcept;
    void enable(bool value) noexcept;

  private:
    struct updates_t {
        support::file_updates_t updates;
        timepoint_t deadline;
    };

    pt::time_duration interval;
    updates_t next;
    updates_t postponed;
    utils::logger_t log;
    bool enabled;
};

using updates_mediator_ptr_t = model::intrusive_ptr_t<updates_mediator_t>;

} // namespace syncspirit::fs
