// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/outcome.hpp>
#include "utils/log.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct cluster_t;

namespace diff {

struct SYNCSPIRIT_API base_diff_t : boost::intrusive_ref_counter<base_diff_t, boost::thread_unsafe_counter> {
    base_diff_t() noexcept;
    virtual ~base_diff_t() = default;

    outcome::result<void> apply(cluster_t &) const noexcept;

    static utils::logger_t get_log() noexcept;

  protected:
    virtual outcome::result<void> apply_impl(cluster_t &) const noexcept = 0;
    utils::logger_t log;
};

template <typename T> using diff_ptr_t = boost::intrusive_ptr<T>;

} // namespace diff

} // namespace syncspirit::model
