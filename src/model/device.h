#pragma once

#include "device_id.h"
#include <string>
#include <vector>
#include "../utils/uri.h"

namespace syncspirit::model {

struct device_t {
    using static_addresses_t = std::vector<utils::URI>;

    device_id_t device_id;
    std::string name;
    static_addresses_t static_addresses;
    bool intoducer;
    bool auto_accept;

    inline bool is_dynamic() const noexcept { return static_addresses.empty(); }
};

} // namespace syncspirit::model
