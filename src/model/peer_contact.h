#pragma once

#include <string>
#include <vector>
#include <optional>
#include "../utils/uri.h"
#include <boost/date_time/posix_time/posix_time_types.hpp>

namespace syncspirit::model {

struct peer_contact_t {
    using uri_container_t = std::vector<utils::URI>;
    using date_t = boost::posix_time::ptime;

    date_t seen;
    uri_container_t uris;
};

using peer_contact_option_t = std::optional<peer_contact_t>;

} // namespace syncspirit::model
