// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

namespace load {
struct load_cluster_t;
}

namespace peer {
struct cluster_remove_t;
struct cluster_update_t;
struct peer_state_t;
struct update_folder_t;
} // namespace peer

namespace modify {
struct clone_file_t;
struct create_folder_t;
struct file_availability_t;
struct finish_file_t;
struct flush_file_t;
struct local_update_t;
struct lock_file_t;
struct new_file_t;
struct share_folder_t;
struct update_peer_t;
} // namespace modify

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::cluster> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const load::load_cluster_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_remove_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_update_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::peer_state_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &) noexcept;

    virtual outcome::result<void> operator()(const modify::clone_file_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::create_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::file_availability_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::finish_file_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::flush_file_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::local_update_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::lock_file_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::new_file_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::share_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &) noexcept;
};

using cluster_visitor_t = generic_visitor_t<tag::cluster>;

} // namespace syncspirit::model::diff
