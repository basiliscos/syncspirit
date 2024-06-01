// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "generic_diff.hpp"
#include "syncspirit-export.h"

namespace syncspirit::model::diff {

namespace load {
struct devices_t;
struct ignored_devices_t;
struct load_cluster_t;
} // namespace load

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
struct finish_file_ack_t;
struct lock_file_t;
struct mark_reachable_t;
struct unshare_folder_t;
struct local_update_t;
struct share_folder_t;
struct update_peer_t;

} // namespace modify

template <> struct SYNCSPIRIT_API generic_visitor_t<tag::cluster> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const load::devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::ignored_devices_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const load::load_cluster_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_remove_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::peer_state_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &, void *custom) noexcept;

    virtual outcome::result<void> operator()(const modify::clone_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::create_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::file_availability_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::finish_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::finish_file_ack_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::lock_file_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::mark_reachable_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::local_update_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::share_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::unshare_folder_t &, void *custom) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &, void *custom) noexcept;
};

using cluster_visitor_t = generic_visitor_t<tag::cluster>;

} // namespace syncspirit::model::diff
