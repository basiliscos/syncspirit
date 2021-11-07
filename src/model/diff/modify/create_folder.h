#pragma once

#include "../cluster_diff.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct create_folder_t final : cluster_diff_t {

    template<typename T>
    create_folder_t(T&& item_, std::string_view source_device_ = "", uint64_t source_index_ = 0, int64_t source_max_sequence_ = 0) noexcept:
        item{std::forward<T>(item_)}, source_device{source_device_}, source_index{source_index_},  source_max_sequence{source_max_sequence_} {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(diff_visitor_t &) const noexcept override;

    db::Folder item;
    std::string source_device;
    uint64_t source_index;
    int64_t source_max_sequence;
};

}
