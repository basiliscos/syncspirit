#pragma once

#include "../cluster_diff.h"
#include "structs.pb.h"

namespace syncspirit::model::diff::modify {

struct update_peer_t final : cluster_diff_t {

    template<typename T>
    update_peer_t(T&& item_, std::string_view peer_id_) noexcept: item{std::forward<T>(item_)}, peer_id{peer_id_} {}

    outcome::result<void> apply(cluster_t &) const noexcept override;

    db::Device item;
    std::string peer_id;
};

}
