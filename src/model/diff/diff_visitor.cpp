#include "diff_visitor.h"

using namespace syncspirit::model::diff;

diff_visitor_t::~diff_visitor_t(){}

auto diff_visitor_t::operator ()(const peer::cluster_update_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto diff_visitor_t::operator ()(const peer::peer_state_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto diff_visitor_t::operator()(const modify::create_folder_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto diff_visitor_t::operator()(const modify::share_folder_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto diff_visitor_t::operator()(const modify::update_peer_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto diff_visitor_t::operator()(const modify::local_update_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}
