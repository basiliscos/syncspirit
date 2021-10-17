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

struct devices_t final : cluster_diff_t {
    template<typename Devices>
    devices_t(Devices&& devices_) noexcept: devices{std::forward<Devices>(devices_)}{}

    void apply(cluster_t&) const noexcept override;

    container_t devices;
};

} // namespace syncspirit::model::diff::cluster
