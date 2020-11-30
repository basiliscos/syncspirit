#pragma once

#include <string>
#include <vector>
#include <optional>
#include "../utils/uri.h"
#include "device_id.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace syncspirit::model {

struct peer_contact_t {
    using uri_container_t = std::vector<utils::URI>;
    using date_t = boost::posix_time::ptime;
    /*
        peer_contact_t() noexcept {}
        template<typename Date, typename URIs>
        peer_contact_t(Date&& date_, URIs&& uris_) noexcept: seen{std::forward<Date>(date_)},
       uris{std::forward<URIs>(uris_)} {} peer_contact_t(const peer_contact_t&) noexcept = default; peer_contact_t(const
       peer_contact_t&) noexcept = default;
    */

    date_t seen;
    uri_container_t uris;
};

using peer_contact_option_t = std::optional<peer_contact_t>;

} // namespace syncspirit::model
