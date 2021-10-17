#pragma once

#include <string>
#include "bep.pb.h"

#include "../cluster_diff.h"

namespace syncspirit::model::diff::cluster {

struct index_refresher_t final : cluster_diff_t {
    void apply(cluster_t &) const noexcept override;
    std::string folder_id;
    std::string device_id;
    proto::Device device;
};

} // namespace syncspirit::model::diff::cluster
