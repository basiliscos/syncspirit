#pragma once

#include <string>
#include <vector>

#include "../cluster_diff.h"

namespace syncspirit::model::diff::load {

struct pair_t {
    std::string_view key;
    std::string_view value;
};

using container_t = std::vector<pair_t>;

}
