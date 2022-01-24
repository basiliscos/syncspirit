#include "cluster_visitor.h"

using namespace syncspirit::model::diff;

auto cluster_visitor_t::operator()(const load::load_cluster_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator ()(const peer::cluster_remove_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator ()(const peer::cluster_update_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator ()(const peer::update_folder_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator ()(const peer::peer_state_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::create_folder_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::clone_file_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::share_folder_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::update_peer_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::file_availability_t &) noexcept ->  outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::finish_file_t &) noexcept ->  outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::flush_file_t &) noexcept ->  outcome::result<void> {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::new_file_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::local_update_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto cluster_visitor_t::operator()(const modify::lock_file_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

