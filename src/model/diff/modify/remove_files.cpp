// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "remove_files.h"
#include "remove_blocks.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"
#include <memory_resource>
#include <list>

using namespace syncspirit;
using namespace syncspirit::model::diff::modify;

remove_files_t::remove_files_t(const folder_info_t &fi, orphaned_blocks_t *orphaned_blocks_) noexcept {
    using queue_t = std::pmr::list<model::presence_t*>;
    using F = model::presence_t::features_t;

    auto &device = *fi.get_device();
    folder_id = fi.get_folder()->get_id();
    device_id = device.device_id().get_sha256();

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);

    auto local_orphaned_blocks = orphaned_blocks_t();
    auto &orphaned_blocks = orphaned_blocks_ ? *orphaned_blocks_ : local_orphaned_blocks;
    queue.push_back(&const_cast<folder_info_t&>(fi));

    while (!queue.empty()) {
        auto p = queue.front();
        queue.pop_front();
        if (p->get_features() & F::cluster) {
            auto file = static_cast<model::file_info_t*>(p);
            orphaned_blocks.record(*file);
            auto id = file->get_full_id();
            auto key = utils::bytes_t(id.size() + 1);
            key[0] = db::prefix::file_info;
            std::copy(id.begin(), id.end(), key.data() + 1);
            keys.push_back(std::move(key));
            paths.push_back(file->get_name()->clone());
        }
        for (auto& c: p->get_children()) {
            queue.push_back(c);
        }
    };

    if (!orphaned_blocks_) {
        auto block_keys = local_orphaned_blocks.deduce();
        if (block_keys.size()) {
            assign_child(new remove_blocks_t(std::move(block_keys)));
        }
    }
    LOG_DEBUG(log, "remove_files_t, device = {}, files count = {}", device.device_id(), paths.size());
}

auto remove_files_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(controller, custom);
    if (!r) {
        return r;
    }
    auto &cluster = controller.get_cluster();
    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    if (folder) {
        auto peer_folder = folder->get_folder_info(device_id);
        if (peer_folder) {
            peer_folder->clear();
        }
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto remove_files_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting remove_files_t");
    return visitor(*this, custom);
}
