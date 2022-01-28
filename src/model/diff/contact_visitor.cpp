#include "contact_visitor.h"

using namespace syncspirit::model::diff;

auto contact_visitor_t::operator()(const modify::connect_request_t &) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto contact_visitor_t::operator()(const modify::update_contact_t &) noexcept -> outcome::result<void> {
    return outcome::success();
}
