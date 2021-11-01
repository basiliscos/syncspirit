#pragma once

#include <memory>
#include <set>
#include "proto/bep_support.h"
#include "../cluster_diff.h"
#include "model/device.h"
#include "model/folder_info.h"

namespace syncspirit::model::diff::peer {

struct cluster_update_t final : cluster_diff_t {
    using message_t = proto::message::ClusterConfig;
    using unknown_folders_t = std::vector<proto::Folder>;
    struct update_info_t {
        std::string folder_id;
        proto::Device device;
    };
    using modified_folders_t = std::vector<update_info_t>;


    static outcome::result<cluster_diff_ptr_t> create(const cluster_t &cluster, const model::device_ptr_t& source, message_t message) noexcept;

    outcome::result<void> apply(cluster_t &) const noexcept override;
    outcome::result<void> visit(diff_visitor_t &) const noexcept override;

    unknown_folders_t unknown_folders;
    modified_folders_t reset_folders;
    modified_folders_t updated_folders;
private:
    cluster_update_t(unknown_folders_t unknown_folders, modified_folders_t reset_folders, modified_folders_t updated_folders) noexcept;
};

}
