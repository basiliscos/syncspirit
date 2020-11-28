#pragma once

#include <string>
#include <cstdint>
#include "device_id.h"

namespace syncspirit::model {

enum compression_t { METADATA = 0, NEVER = 1, ALWAYS = 1 };

struct device_t {
    device_id_t device_id;
    std::string name;
    std::vector<std::string> addresses;
    compression_t compression;
    std::string cert_name;
    std::int64_t max_sequence;
    bool introducer;
    std::uint64_t index_id;
    bool skip_introduction_removals;
};

} // namespace syncspirit::model
