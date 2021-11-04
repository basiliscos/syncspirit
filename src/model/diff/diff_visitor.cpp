#include "diff_visitor.h"

using namespace syncspirit::model::diff;

diff_visitor_t::~diff_visitor_t(){}

auto diff_visitor_t::operator ()(const peer::cluster_update_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}

auto diff_visitor_t::operator()(const modify::create_folder_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}
