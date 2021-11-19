#pragma once
#include <boost/outcome.hpp>


namespace syncspirit::model::diff {

namespace outcome = boost::outcome_v2;

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
    struct local_update_t;
}

struct diff_visitor_t {
    virtual ~diff_visitor_t();
    virtual outcome::result<void> operator()(const load::load_cluster_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::peer_state_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_remove_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::cluster_update_t &) noexcept;
    virtual outcome::result<void> operator()(const peer::update_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::create_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::share_folder_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::update_peer_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::local_update_t &) noexcept;
};

} // namespace syncspirit::model::diff
