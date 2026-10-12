// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "scan_start.h"
#include "model/cluster.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"
#include <list>
#include <memory_resource>

using namespace syncspirit::model::diff::local;

scan_start_t::scan_start_t(std::string_view folder_id_, utils::path_t path_, const pt::ptime &at_)
    : folder_id{folder_id_}, path{std::move(path_)}, at{at_} {
    LOG_DEBUG(log, "scan_start_t, folder = {}, sub_dir = {}", folder_id, path_);
}

auto scan_start_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    using queue_t = std::pmr::list<model::presence_t*>;
    using F = model::presence_t::features_t;
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    folder->set_scan_start(at);
    auto r = applicator_t::apply_sibling(controller, custom);

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);

    auto local_device = cluster.get_device();
    auto local_folder = folder->get_folder_info(*local_device);
    queue.push_back(local_folder);
    auto updated = false;

    while (!queue.empty()) {
        auto p = queue.front();
        queue.pop_front();
        if (p->get_features() & F::cluster) {
            auto &local_file = *static_cast<model::file_info_t*>(p);
            if (local_file.is_local()) {
                auto name = local_file.get_name()->get_full_name();
                if (path.contains(*local_file.get_name())) {
                    local_file.mark_local(false);
                    local_file.notify_update();
                    updated = true;
                }
            }
        }
        for (auto& c: p->get_children()) {
            queue.push_back(c);
        }
    }

    if (updated) {
        local_folder->notify_update();
        folder->notify_update();
    }
    return r;
}

auto scan_start_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto scan_start_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting scan_start_t");
    return visitor(*this, custom);
}
