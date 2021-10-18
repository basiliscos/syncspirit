#pragma once

#include "common.h"

namespace syncspirit::model::diff::load {

struct ignored_devices_t final : cluster_diff_t {
    template<typename T>
    ignored_devices_t(T&& devices_) noexcept: devices{std::forward<T>(devices_)}{}

    void apply(cluster_t&) const noexcept override;

    container_t devices;
};

} // namespace syncspirit::model::diff::cluster
