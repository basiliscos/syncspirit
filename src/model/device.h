#pragma once

#include "device_id.h"
#include <string>
#include <vector>
#include "../utils/uri.h"
#include "../configuration.h"
#include "arc.hpp"

namespace syncspirit::model {

struct device_t : arc_base_t<device_t> {
    using static_addresses_t = std::vector<utils::URI>;
    device_t(config::device_config_t &config) noexcept;

    device_id_t device_id;
    std::string name;
    std::optional<std::string> cert_name;
    static_addresses_t static_addresses;
    bool intoducer;
    bool auto_accept;
    bool paused;
    bool online = false;

    inline bool is_dynamic() const noexcept { return static_addresses.empty(); }
};

using device_ptr_t = intrusive_ptr_t<device_t>;

} // namespace syncspirit::model
