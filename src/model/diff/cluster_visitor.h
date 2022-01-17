#pragma once
#include "generic_diff.hpp"

namespace syncspirit::model::diff {

namespace load {
    struct load_cluster_t;
}

namespace peer {
    struct cluster_remove_t;
    struct cluster_update_t;
    struct peer_state_t;
    struct update_folder_t;
}

namespace modify {
    struct create_folder_t;
    struct share_folder_t;
    struct update_peer_t;
    struct file_availability_t;
    struct new_file_t;
    struct local_update_t;
    struct lock_file_t;
}

template<>
struct generic_visitor_t<tag::cluster> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const load::load_cluster_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::peer_state_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_remove_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_update_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::create_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::share_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::file_availability_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::new_file_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::local_update_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::lock_file_t &) noexcept;
};

using cluster_visitor_t = generic_visitor_t<tag::cluster>;


} // namespace syncspirit::model::diff
