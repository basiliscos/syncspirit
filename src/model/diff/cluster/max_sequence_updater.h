#pragma once

#include <vector>
#include <string>
#include "../cluster_diff.h"

namespace syncspirit::model::diff::cluster {

struct max_sequence_updater_t final : cluster_diff_t {

    template <typename F, typename D>
    max_sequence_updater_t(std::int64_t max_sequence_, F &&folder_id_, D &&device_id_) noexcept
        : max_sequence{max_sequence_}, folder_id{folder_id_}, device_id{device_id_} {}

    void apply(cluster_t &) const noexcept override;

    std::int64_t max_sequence;
    std::string folder_id;
    std::string device_id;
};

} // namespace syncspirit::model::diff::cluster
