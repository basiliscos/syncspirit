// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "stack_context.h"
#include "constants.h"
#include "model/cluster.h"
#include "model/diff/advance/local_update.h"
#include "proto/proto-helpers-bep.h"

#include <chrono>

using namespace syncspirit::net::local_keeper;

stack_context_t::stack_context_t(model::cluster_t &cluster_, model::sequencer_t &sequencer_, std::int32_t hashes_pool_,
                                 std::int32_t hashes_pool_max_, syncspirit_watcher_impl_t watcher_impl_) noexcept
    : parent_t{constants::diffs_batch}, cluster{cluster_}, sequencer{sequencer_}, hashes_pool{hashes_pool_},
      hashes_pool_max{hashes_pool_max_}, slave{nullptr}, watcher_impl{watcher_impl_}, now{0},
      pool(buffer.data(), buffer.size()), allocator(&pool) {}

std::int64_t stack_context_t::get_now() noexcept {
    using clock_t = std::chrono::system_clock;
    if (!now) {
        now = clock_t::to_time_t(clock_t::now());
    }
    return now;
}

void stack_context_t::local_update(proto::FileInfo data, std::string_view folder_id,
                                   bool disable_blocks_removal) noexcept {
    using namespace model::diff::advance;
    auto &folder = *cluster.get_folders().by_id(folder_id);
    auto relative_path = proto::get_name(data);
    push_back(new local_update_t(cluster, sequencer, std::move(data), folder_id, disable_blocks_removal));
}
