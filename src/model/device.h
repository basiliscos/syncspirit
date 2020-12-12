#pragma once

#include "device_id.h"
#include <string>
#include <optional>
#include "../utils/uri.h"

namespace syncspirit::model {

struct device_t {
    using static_addresses_t = std::optional<utils::URI>;

    device_id_t device_id;
    std::string name;
    static_addresses_t static_addresses;
    bool intoducer;
    bool auto_accept;
};

} // namespace syncspirit::model
